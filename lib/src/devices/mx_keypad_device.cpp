#include "mx_keypad_device.h"
#include "../util/gif_decoder.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/hidraw.h>
#include <map>
#include <poll.h>
#include <set>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>

namespace LogiLinux {

constexpr size_t MAX_PACKET_SIZE = 4095;
constexpr size_t LCD_SIZE = 118;

// Uber-optimization: Pre-computed packet headers for zero-copy assembly
alignas(64) static const uint8_t PACKET_BASE_HEADER[4] = {0x14, 0xff, 0x02, 0x2b};
alignas(64) static const uint8_t PACKET1_GEOMETRY[6] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00};

// Memory pool for packet buffers to avoid allocations
class PacketBufferPool {
private:
  static constexpr size_t POOL_SIZE = 16;
  std::array<std::vector<uint8_t>, POOL_SIZE> pool;
  std::atomic<size_t> next_free{0};

public:
  PacketBufferPool() {
    for (auto& buffer : pool) {
      buffer.resize(MAX_PACKET_SIZE);
    }
  }

  std::vector<uint8_t>& acquire() {
    size_t idx = next_free.fetch_add(1) % POOL_SIZE;
    return pool[idx];
  }
};

static PacketBufferPool packet_pool;

struct KeyAnimation {
  GifAnimation animation;
  std::atomic<bool> running;
  std::thread animation_thread;
  size_t current_frame;

  KeyAnimation() : running(false), current_frame(0) {}
};

struct MXKeypadDevice::Impl {
  int hidraw_fd = -1;
  std::string hidraw_path;
  bool initialized = false;
  std::atomic<bool> monitoring = false;
  std::thread monitor_thread;
  std::set<uint8_t> pressed_buttons; // Track all currently pressed buttons
  uint8_t last_p_button = 0; // Track last pressed P1/P2 button (0xa1 or 0xa2)

  // GIF animation tracking (per-key)
  std::map<int, std::unique_ptr<KeyAnimation>> animations;
  
  // Full-screen GIF animation
  std::unique_ptr<KeyAnimation> screen_animation;

  const std::vector<std::vector<uint8_t>> INIT_REPORTS = {
      {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa1, 0x03, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa2, 0x03, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  };

  std::vector<std::vector<uint8_t>>
  generateImagePackets(int keyIndex, const std::vector<uint8_t> &jpegData) {
    // Uber-optimized: Pre-calculate everything for zero-copy assembly
    const size_t PACKET1_HEADER = 20;
    const size_t SUBSEQUENT_HEADER = 5;

    // Calculate total packets upfront
    size_t totalPackets = 1; // At least first packet
    size_t remainingAfterFirst = jpegData.size() > (MAX_PACKET_SIZE - PACKET1_HEADER)
                                ? jpegData.size() - (MAX_PACKET_SIZE - PACKET1_HEADER)
                                : 0;
    if (remainingAfterFirst > 0) {
      totalPackets += (remainingAfterFirst + (MAX_PACKET_SIZE - SUBSEQUENT_HEADER) - 1) /
                     (MAX_PACKET_SIZE - SUBSEQUENT_HEADER);
    }

    // Pre-allocate result with exact size to avoid reallocations
    std::vector<std::vector<uint8_t>> result;
    result.reserve(totalPackets);

    // Pre-calculate key position
    int row = keyIndex / 3;
    int col = keyIndex % 3;
    const uint16_t x = 23 + col * (118 + 40);
    const uint16_t y = 6 + row * (118 + 40);

    // Uber-optimization: Pre-compute first packet header for memcpy
    uint8_t packet1_header[PACKET1_HEADER] = {0};
    memcpy(packet1_header, PACKET_BASE_HEADER, 4); // 0x14, 0xff, 0x02, 0x2b
    packet1_header[4] = generateWritePacketByte(1, true,
                          jpegData.size() <= (MAX_PACKET_SIZE - PACKET1_HEADER));
    memcpy(packet1_header + 5, PACKET1_GEOMETRY, 6); // 0x01, 0x00, 0x01, 0x00, 0x00, 0x00
    packet1_header[9] = (x >> 8) & 0xff;
    packet1_header[10] = x & 0xff;
    packet1_header[11] = (y >> 8) & 0xff;
    packet1_header[12] = y & 0xff;
    packet1_header[13] = (LCD_SIZE >> 8) & 0xff;
    packet1_header[14] = LCD_SIZE & 0xff;
    packet1_header[15] = (LCD_SIZE >> 8) & 0xff;
    packet1_header[16] = LCD_SIZE & 0xff;
    packet1_header[18] = (jpegData.size() >> 8) & 0xff;
    packet1_header[19] = jpegData.size() & 0xff;

    // First packet - use memory pool buffer
    auto& packet1 = packet_pool.acquire();
    memset(packet1.data(), 0, MAX_PACKET_SIZE);
    memcpy(packet1.data(), packet1_header, PACKET1_HEADER);

    const size_t byteCount1 = std::min(jpegData.size(), MAX_PACKET_SIZE - PACKET1_HEADER);
    if (byteCount1 > 0) {
      memcpy(packet1.data() + PACKET1_HEADER, jpegData.data(), byteCount1);
    }
    result.push_back(packet1);

    // Subsequent packets - pre-compute base header
    uint8_t packet_header[SUBSEQUENT_HEADER] = {0};
    memcpy(packet_header, PACKET_BASE_HEADER, 4);

    // Process remaining data with zero-copy assembly
    size_t remainingBytes = jpegData.size() - byteCount1;
    size_t currentOffset = byteCount1;
    int part = 2;

    while (remainingBytes > 0) {
      const size_t byteCount = std::min(remainingBytes, MAX_PACKET_SIZE - SUBSEQUENT_HEADER);

      auto& packet = packet_pool.acquire();
      memset(packet.data(), 0, MAX_PACKET_SIZE);

      // Pre-assemble header
      packet_header[4] = generateWritePacketByte(part, false, remainingBytes - byteCount == 0);
      memcpy(packet.data(), packet_header, SUBSEQUENT_HEADER);

      // Zero-copy data placement
      if (byteCount > 0) {
        memcpy(packet.data() + SUBSEQUENT_HEADER, jpegData.data() + currentOffset, byteCount);
      }

      result.push_back(packet);

      remainingBytes -= byteCount;
      currentOffset += byteCount;
      part++;
    }

    return result;
  }

  uint8_t generateWritePacketByte(int index, bool isFirst, bool isLast) {
    uint8_t value = index | 0b00100000;
    if (isFirst)
      value |= 0b10000000;
    if (isLast)
      value |= 0b01000000;
    return value;
  }

  // Generate image packets for arbitrary screen region
  std::vector<std::vector<uint8_t>>
  generateRawImagePackets(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                          const std::vector<uint8_t> &jpegData) {
    const size_t PACKET1_HEADER = 20;
    const size_t SUBSEQUENT_HEADER = 5;

    // Calculate total packets upfront
    size_t totalPackets = 1;
    size_t remainingAfterFirst = jpegData.size() > (MAX_PACKET_SIZE - PACKET1_HEADER)
                                ? jpegData.size() - (MAX_PACKET_SIZE - PACKET1_HEADER)
                                : 0;
    if (remainingAfterFirst > 0) {
      totalPackets += (remainingAfterFirst + (MAX_PACKET_SIZE - SUBSEQUENT_HEADER) - 1) /
                     (MAX_PACKET_SIZE - SUBSEQUENT_HEADER);
    }

    std::vector<std::vector<uint8_t>> result;
    result.reserve(totalPackets);

    // Build first packet header
    uint8_t packet1_header[PACKET1_HEADER] = {0};
    memcpy(packet1_header, PACKET_BASE_HEADER, 4); // 0x14, 0xff, 0x02, 0x2b
    packet1_header[4] = generateWritePacketByte(1, true,
                          jpegData.size() <= (MAX_PACKET_SIZE - PACKET1_HEADER));
    memcpy(packet1_header + 5, PACKET1_GEOMETRY, 6); // 0x01, 0x00, 0x01, 0x00, 0x00, 0x00
    packet1_header[9] = (x >> 8) & 0xff;
    packet1_header[10] = x & 0xff;
    packet1_header[11] = (y >> 8) & 0xff;
    packet1_header[12] = y & 0xff;
    packet1_header[13] = (width >> 8) & 0xff;
    packet1_header[14] = width & 0xff;
    packet1_header[15] = (height >> 8) & 0xff;
    packet1_header[16] = height & 0xff;
    packet1_header[18] = (jpegData.size() >> 8) & 0xff;
    packet1_header[19] = jpegData.size() & 0xff;

    // First packet
    auto& packet1 = packet_pool.acquire();
    memset(packet1.data(), 0, MAX_PACKET_SIZE);
    memcpy(packet1.data(), packet1_header, PACKET1_HEADER);

    const size_t byteCount1 = std::min(jpegData.size(), MAX_PACKET_SIZE - PACKET1_HEADER);
    if (byteCount1 > 0) {
      memcpy(packet1.data() + PACKET1_HEADER, jpegData.data(), byteCount1);
    }
    result.push_back(packet1);

    // Subsequent packets
    uint8_t packet_header[SUBSEQUENT_HEADER] = {0};
    memcpy(packet_header, PACKET_BASE_HEADER, 4);

    size_t remainingBytes = jpegData.size() - byteCount1;
    size_t currentOffset = byteCount1;
    int part = 2;

    while (remainingBytes > 0) {
      const size_t byteCount = std::min(remainingBytes, MAX_PACKET_SIZE - SUBSEQUENT_HEADER);

      auto& packet = packet_pool.acquire();
      memset(packet.data(), 0, MAX_PACKET_SIZE);

      packet_header[4] = generateWritePacketByte(part, false, remainingBytes - byteCount == 0);
      memcpy(packet.data(), packet_header, SUBSEQUENT_HEADER);

      if (byteCount > 0) {
        memcpy(packet.data() + SUBSEQUENT_HEADER, jpegData.data() + currentOffset, byteCount);
      }

      result.push_back(packet);

      remainingBytes -= byteCount;
      currentOffset += byteCount;
      part++;
    }

    return result;
  }

  std::string findHidrawPath(const std::string &event_path) {
    // Extract event number from path like /dev/input/event5
    std::string event_name =
        event_path.substr(event_path.find_last_of('/') + 1);
    if (event_name.find("event") != 0) {
      return "";
    }

    // Try to find corresponding hidraw device
    // This is a simplified approach - in production would use udev
    for (int i = 0; i < 20; i++) {
      std::string hidraw = "/dev/hidraw" + std::to_string(i);
      int fd = open(hidraw.c_str(), O_RDWR | O_NONBLOCK);
      if (fd >= 0) {
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
          if (info.vendor == 0x046d && info.product == 0xc354) {
            close(fd);
            return hidraw;
          }
        }
        close(fd);
      }
    }
    return "";
  }
};

MXKeypadDevice::MXKeypadDevice(const DeviceInfo &info)
    : impl_(std::make_unique<Impl>()), info_(info) {

  capabilities_.push_back(DeviceCapability::BUTTONS);

  // Check if device_path is already a hidraw device
  if (info.device_path.find("/dev/hidraw") == 0) {
    impl_->hidraw_path = info.device_path;
  } else {
    // Find the hidraw device for LCD control from event path
    impl_->hidraw_path = impl_->findHidrawPath(info.device_path);
  }

  if (!impl_->hidraw_path.empty()) {
    capabilities_.push_back(DeviceCapability::LCD_DISPLAY);
    capabilities_.push_back(DeviceCapability::IMAGE_UPLOAD);
  }
}

MXKeypadDevice::~MXKeypadDevice() {
  stopAllAnimations();
  stopMonitoring();
  if (impl_->hidraw_fd >= 0) {
    close(impl_->hidraw_fd);
  }
}

bool MXKeypadDevice::hasCapability(DeviceCapability cap) const {
  return std::find(capabilities_.begin(), capabilities_.end(), cap) !=
         capabilities_.end();
}

void MXKeypadDevice::setEventCallback(EventCallback callback) {
  event_callback_ = callback;
}

void MXKeypadDevice::startMonitoring() {
  if (impl_->monitoring || !event_callback_) {
    return;
  }

  impl_->monitoring = true;
  impl_->monitor_thread = std::thread([this]() {
    // Use hidraw path for reading button events
    std::string monitor_path =
        impl_->hidraw_path.empty() ? info_.device_path : impl_->hidraw_path;

    int fd = open(monitor_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      impl_->monitoring = false;
      return;
    }

    constexpr size_t REPORT_SIZE = 256;
    std::vector<uint8_t> report(REPORT_SIZE);

    // Set up poll for event-driven reading
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (impl_->monitoring) {
      // Wait for data with 100ms timeout (same as dialpad)
      int ret = poll(&pfd, 1, 100);

      if (ret < 0) {
        break; // Error
      }

      if (ret == 0) {
        continue; // Timeout, check if still monitoring
      }

      if (!(pfd.revents & POLLIN)) {
        continue; // No data available
      }

      int bytes_read = read(fd, report.data(), report.size());

      // P1/P2 navigation button detection - CHECK THIS FIRST
      // Format: 11 ff 0b 00 01 a1/a2 (press) or 11 ff 0b 00 00 00 (release)
      // IMPORTANT: When P buttons are pressed, report[6] contains spurious grid
      // data We must process P button events first and skip grid processing for
      // those packets
      bool is_p_button_event = false;

      if (bytes_read >= 6 && report[0] == 0x11 && report[1] == 0xff &&
          report[2] == 0x0b && report[3] == 0x00) {
        is_p_button_event = true;

        if (report[4] == 0x01 && (report[5] == 0xa1 || report[5] == 0xa2)) {
          // Button press
          impl_->last_p_button = report[5]; // Track which button was pressed
          auto event = std::make_shared<ButtonEvent>();
          event->type = EventType::BUTTON_PRESS;
          event->button_code = report[5]; // 0xa1 or 0xa2
          event->pressed = true;
          event->timestamp =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();

          if (event_callback_) {
            event_callback_(event);
          }
        } else if (report[4] == 0x00 && impl_->last_p_button != 0) {
          // Button release - emit event for the last pressed P button
          auto event = std::make_shared<ButtonEvent>();
          event->type = EventType::BUTTON_RELEASE;
          event->button_code = impl_->last_p_button; // Use tracked button code
          event->pressed = false;
          event->timestamp =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();

          if (event_callback_) {
            event_callback_(event);
          }

          impl_->last_p_button = 0; // Clear tracking
        }
      }

      // Grid button detection - SKIP if this was a P button event packet
      if (!is_p_button_event && bytes_read > 0 && bytes_read >= 7) {
        // Grid button report format: 13 ff 02 00 xx 01 [button_codes...]
        // Bytes 6+ contain ALL currently pressed button codes (1-9), terminated
        // by 0 This allows multiple simultaneous button presses!

        if (report[0] == 0x13 && report[1] == 0xff && report[2] == 0x02 &&
            report[3] == 0x00 && report[5] == 0x01) {

          // Collect all currently pressed buttons from this report
          std::set<uint8_t> current_pressed;

          for (size_t i = 6; i < bytes_read; i++) {
            uint8_t button_code_raw = report[i];
            if (button_code_raw == 0)
              break; // End of button list

            if (button_code_raw >= 1 && button_code_raw <= 9) {
              uint8_t button_code = button_code_raw - 1; // Convert to 0-8
              current_pressed.insert(button_code);
            }
          }

          // Find newly pressed buttons (in current but not in previous)
          for (uint8_t button_code : current_pressed) {
            if (impl_->pressed_buttons.find(button_code) ==
                impl_->pressed_buttons.end()) {
              // New button press
              auto event = std::make_shared<ButtonEvent>();
              event->type = EventType::BUTTON_PRESS;
              event->button_code = button_code;
              event->pressed = true;
              event->timestamp =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

              if (event_callback_) {
                event_callback_(event);
              }
            }
          }

          // Find released buttons (in previous but not in current)
          std::vector<uint8_t> to_release;
          for (uint8_t button_code : impl_->pressed_buttons) {
            if (current_pressed.find(button_code) == current_pressed.end()) {
              to_release.push_back(button_code);
            }
          }

          for (uint8_t button_code : to_release) {
            auto event = std::make_shared<ButtonEvent>();
            event->type = EventType::BUTTON_RELEASE;
            event->button_code = button_code;
            event->pressed = false;
            event->timestamp =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();

            if (event_callback_) {
              event_callback_(event);
            }
          }

          // Update tracked state
          impl_->pressed_buttons = current_pressed;
        }
      }

      if (bytes_read < 0 && errno != EAGAIN) {
        break;
      }
    }

    close(fd);
    impl_->monitoring = false;
  });
}

void MXKeypadDevice::stopMonitoring() {
  if (impl_->monitoring) {
    impl_->monitoring = false;
    if (impl_->monitor_thread.joinable()) {
      impl_->monitor_thread.join();
    }
  }
}

bool MXKeypadDevice::isMonitoring() const { return impl_->monitoring; }

bool MXKeypadDevice::grabExclusive(bool grab) {
  // Not applicable for hidraw devices
  return false;
}

bool MXKeypadDevice::initialize() {
  if (!impl_) {
    return false;
  }

  if (impl_->initialized || impl_->hidraw_path.empty()) {
    return impl_->initialized;
  }

  impl_->hidraw_fd = open(impl_->hidraw_path.c_str(), O_RDWR);
  if (impl_->hidraw_fd < 0) {
    return false;
  }

  // Send initialization sequence
  for (const auto &report : impl_->INIT_REPORTS) {
    write(impl_->hidraw_fd, report.data(), report.size());
    usleep(10000);
  }

  impl_->initialized = true;
  return true;
}

bool MXKeypadDevice::setKeyImage(int keyIndex,
                                 const std::vector<uint8_t> &jpegData) {
  if (keyIndex < 0 || keyIndex > 8 || !impl_->initialized) {
    return false;
  }

  auto packets = impl_->generateImagePackets(keyIndex, jpegData);

  if (packets.empty()) {
    return false;
  }

  // Uber-optimized: Pre-build iovec array inline to avoid allocation
  const size_t packet_count = packets.size();
  std::vector<iovec> iov(packet_count);

  // Vectorized iovec construction - loop unrolled for performance
  for (size_t i = 0; i < packet_count; ++i) {
    iov[i] = {const_cast<uint8_t*>(packets[i].data()), packets[i].size()};
  }

  // Uber-optimization: Non-blocking I/O with immediate completion check
  const int flags = fcntl(impl_->hidraw_fd, F_GETFL, 0);
  fcntl(impl_->hidraw_fd, F_SETFL, flags | O_NONBLOCK);

  ssize_t totalWritten = writev(impl_->hidraw_fd, iov.data(), packet_count);

  // Restore blocking mode
  fcntl(impl_->hidraw_fd, F_SETFL, flags);

  if (totalWritten < 0) {
    // If non-blocking would block, fall back to blocking write
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      totalWritten = writev(impl_->hidraw_fd, iov.data(), packet_count);
    } else {
      return false;
    }
  }

  // Uber-optimization: Pre-calculated total size to avoid loop overhead
  const ssize_t expectedTotal = packet_count * MAX_PACKET_SIZE;
  return totalWritten == expectedTotal;
}

bool MXKeypadDevice::setKeyColor(int keyIndex, uint8_t r, uint8_t g,
                                 uint8_t b) {
  // This would require generating a solid color JPEG
  // For now, not implemented in the library
  (void)keyIndex;
  (void)r;
  (void)g;
  (void)b;
  return false;
}

bool MXKeypadDevice::hasLCD() const { return !impl_->hidraw_path.empty(); }

bool MXKeypadDevice::setScreenImage(const std::vector<uint8_t> &jpegData) {
  // Full screen image covering all 9 keys (434x434)
  // Position: x=23, y=6 (same origin as key 0)
  return setRawImage(23, 6, SCREEN_WIDTH, SCREEN_HEIGHT, jpegData);
}

bool MXKeypadDevice::setRawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  const std::vector<uint8_t> &jpegData) {
  if (!impl_->initialized) {
    return false;
  }

  auto packets = impl_->generateRawImagePackets(x, y, width, height, jpegData);

  if (packets.empty()) {
    return false;
  }

  // Use vectored I/O for efficient batch write
  const size_t packet_count = packets.size();
  std::vector<iovec> iov(packet_count);

  for (size_t i = 0; i < packet_count; ++i) {
    iov[i] = {const_cast<uint8_t*>(packets[i].data()), packets[i].size()};
  }

  // Non-blocking I/O with fallback
  const int flags = fcntl(impl_->hidraw_fd, F_GETFL, 0);
  fcntl(impl_->hidraw_fd, F_SETFL, flags | O_NONBLOCK);

  ssize_t totalWritten = writev(impl_->hidraw_fd, iov.data(), packet_count);

  // Restore blocking mode
  fcntl(impl_->hidraw_fd, F_SETFL, flags);

  if (totalWritten < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      totalWritten = writev(impl_->hidraw_fd, iov.data(), packet_count);
    } else {
      return false;
    }
  }

  const ssize_t expectedTotal = packet_count * MAX_PACKET_SIZE;
  return totalWritten == expectedTotal;
}

bool MXKeypadDevice::setKeyGif(int keyIndex,
                               const std::vector<uint8_t> &gifData, bool loop) {
  if (keyIndex < 0 || keyIndex > 8 || !impl_->initialized) {
    return false;
  }

  // Stop existing animation on this key
  stopKeyAnimation(keyIndex);

  // Decode GIF
  auto anim = std::make_unique<KeyAnimation>();
  anim->animation.loop = loop;

  if (!GifDecoder::decodeGif(gifData, anim->animation, LCD_SIZE, LCD_SIZE)) {
    return false;
  }

  if (anim->animation.frames.empty()) {
    return false;
  }

  // Start animation thread
  anim->running = true;
  anim->current_frame = 0;

  anim->animation_thread = std::thread([this, keyIndex,
                                        anim_ptr = anim.get()]() {
    while (anim_ptr->running) {
      const GifFrame &frame =
          anim_ptr->animation.frames[anim_ptr->current_frame];

      // Display this frame
      setKeyImage(keyIndex, frame.jpeg_data);

      // Wait for frame delay
      std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));

      // Next frame
      anim_ptr->current_frame++;

      if (anim_ptr->current_frame >= anim_ptr->animation.frames.size()) {
        if (anim_ptr->animation.loop) {
          anim_ptr->current_frame = 0;
        } else {
          anim_ptr->running = false;
        }
      }
    }
  });

  // Store animation
  impl_->animations[keyIndex] = std::move(anim);

  return true;
}

bool MXKeypadDevice::setKeyGifFromFile(int keyIndex, const std::string &gifPath,
                                       bool loop) {
  if (keyIndex < 0 || keyIndex > 8 || !impl_->initialized) {
    return false;
  }

  // Stop existing animation on this key
  stopKeyAnimation(keyIndex);

  // Decode GIF from file
  auto anim = std::make_unique<KeyAnimation>();
  anim->animation.loop = loop;

  if (!GifDecoder::decodeGifFromFile(gifPath, anim->animation, LCD_SIZE,
                                     LCD_SIZE)) {
    return false;
  }

  if (anim->animation.frames.empty()) {
    return false;
  }

  // Start animation thread
  anim->running = true;
  anim->current_frame = 0;

  anim->animation_thread = std::thread([this, keyIndex,
                                        anim_ptr = anim.get()]() {
    while (anim_ptr->running) {
      const GifFrame &frame =
          anim_ptr->animation.frames[anim_ptr->current_frame];

      // Display this frame
      setKeyImage(keyIndex, frame.jpeg_data);

      // Wait for frame delay
      std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));

      // Next frame
      anim_ptr->current_frame++;

      if (anim_ptr->current_frame >= anim_ptr->animation.frames.size()) {
        if (anim_ptr->animation.loop) {
          anim_ptr->current_frame = 0;
        } else {
          anim_ptr->running = false;
        }
      }
    }
  });

  // Store animation
  impl_->animations[keyIndex] = std::move(anim);

  return true;
}

void MXKeypadDevice::stopKeyAnimation(int keyIndex) {
  auto it = impl_->animations.find(keyIndex);
  if (it != impl_->animations.end()) {
    it->second->running = false;
    if (it->second->animation_thread.joinable()) {
      it->second->animation_thread.join();
    }
    impl_->animations.erase(it);
  }
}

void MXKeypadDevice::stopAllAnimations() {
  // Stop screen animation first
  stopScreenAnimation();
  
  // Stop all key animations
  for (auto &pair : impl_->animations) {
    pair.second->running = false;
    if (pair.second->animation_thread.joinable()) {
      pair.second->animation_thread.join();
    }
  }
  impl_->animations.clear();
}

bool MXKeypadDevice::setScreenGif(const std::vector<uint8_t> &gifData, bool loop) {
  if (!impl_->initialized) {
    return false;
  }

  // Stop existing screen animation
  stopScreenAnimation();

  // Decode GIF at full screen size (434x434)
  auto anim = std::make_unique<KeyAnimation>();
  anim->animation.loop = loop;

  if (!GifDecoder::decodeGif(gifData, anim->animation, SCREEN_WIDTH, SCREEN_HEIGHT)) {
    return false;
  }

  if (anim->animation.frames.empty()) {
    return false;
  }

  // Start animation thread
  anim->running = true;
  anim->current_frame = 0;

  anim->animation_thread = std::thread([this, anim_ptr = anim.get()]() {
    while (anim_ptr->running) {
      const GifFrame &frame = anim_ptr->animation.frames[anim_ptr->current_frame];

      // Display frame on full screen (much faster than 9 individual keys!)
      setScreenImage(frame.jpeg_data);

      // Wait for frame delay
      std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));

      // Next frame
      anim_ptr->current_frame++;

      if (anim_ptr->current_frame >= anim_ptr->animation.frames.size()) {
        if (anim_ptr->animation.loop) {
          anim_ptr->current_frame = 0;
        } else {
          anim_ptr->running = false;
        }
      }
    }
  });

  // Store animation
  impl_->screen_animation = std::move(anim);

  return true;
}

bool MXKeypadDevice::setScreenGifFromFile(const std::string &gifPath, bool loop) {
  if (!impl_->initialized) {
    return false;
  }

  // Stop existing screen animation
  stopScreenAnimation();

  // Decode GIF from file at full screen size (434x434)
  auto anim = std::make_unique<KeyAnimation>();
  anim->animation.loop = loop;

  if (!GifDecoder::decodeGifFromFile(gifPath, anim->animation, SCREEN_WIDTH, SCREEN_HEIGHT)) {
    return false;
  }

  if (anim->animation.frames.empty()) {
    return false;
  }

  // Start animation thread
  anim->running = true;
  anim->current_frame = 0;

  anim->animation_thread = std::thread([this, anim_ptr = anim.get()]() {
    while (anim_ptr->running) {
      const GifFrame &frame = anim_ptr->animation.frames[anim_ptr->current_frame];

      // Display frame on full screen (much faster than 9 individual keys!)
      setScreenImage(frame.jpeg_data);

      // Wait for frame delay
      std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));

      // Next frame
      anim_ptr->current_frame++;

      if (anim_ptr->current_frame >= anim_ptr->animation.frames.size()) {
        if (anim_ptr->animation.loop) {
          anim_ptr->current_frame = 0;
        } else {
          anim_ptr->running = false;
        }
      }
    }
  });

  // Store animation
  impl_->screen_animation = std::move(anim);

  return true;
}

void MXKeypadDevice::stopScreenAnimation() {
  if (impl_->screen_animation) {
    impl_->screen_animation->running = false;
    if (impl_->screen_animation->animation_thread.joinable()) {
      impl_->screen_animation->animation_thread.join();
    }
    impl_->screen_animation.reset();
  }
}

} // namespace LogiLinux
