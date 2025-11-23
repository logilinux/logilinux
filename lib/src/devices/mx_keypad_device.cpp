#include "mx_keypad_device.h"
#include "../util/gif_decoder.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/hidraw.h>
#include <map>
#include <poll.h>
#include <set>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace LogiLinux {

constexpr size_t MAX_PACKET_SIZE = 4095;
constexpr size_t LCD_SIZE = 118;

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

  // GIF animation tracking
  std::map<int, std::unique_ptr<KeyAnimation>> animations;

  const std::vector<std::vector<uint8_t>> INIT_REPORTS = {
      {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa1, 0x03, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x11, 0xff, 0x0b, 0x3b, 0x01, 0xa2, 0x03, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  };

  std::vector<std::vector<uint8_t>>
  generateImagePackets(int keyIndex, const std::vector<uint8_t> &jpegData) {
    std::vector<std::vector<uint8_t>> result;

    int row = keyIndex / 3;
    int col = keyIndex % 3;
    uint16_t x = 23 + col * (118 + 40);
    uint16_t y = 6 + row * (118 + 40);

    const size_t PACKET1_HEADER = 20;
    size_t byteCount1 =
        std::min(jpegData.size(), MAX_PACKET_SIZE - PACKET1_HEADER);

    std::vector<uint8_t> packet1(MAX_PACKET_SIZE, 0);
    std::copy(jpegData.begin(), jpegData.begin() + byteCount1,
              packet1.begin() + PACKET1_HEADER);

    packet1[0] = 0x14;
    packet1[1] = 0xff;
    packet1[2] = 0x02;
    packet1[3] = 0x2b;
    packet1[4] =
        generateWritePacketByte(1, true, byteCount1 >= jpegData.size());
    packet1[5] = 0x01;
    packet1[6] = 0x00;
    packet1[7] = 0x01;
    packet1[8] = 0x00;
    packet1[9] = (x >> 8) & 0xff;
    packet1[10] = x & 0xff;
    packet1[11] = (y >> 8) & 0xff;
    packet1[12] = y & 0xff;
    packet1[13] = (LCD_SIZE >> 8) & 0xff;
    packet1[14] = LCD_SIZE & 0xff;
    packet1[15] = (LCD_SIZE >> 8) & 0xff;
    packet1[16] = LCD_SIZE & 0xff;
    packet1[18] = (jpegData.size() >> 8) & 0xff;
    packet1[19] = jpegData.size() & 0xff;

    result.push_back(packet1);

    size_t remainingBytes = jpegData.size() - byteCount1;
    size_t currentOffset = byteCount1;
    int part = 2;

    while (remainingBytes > 0) {
      const size_t headerSize = 5;
      size_t byteCount = std::min(remainingBytes, MAX_PACKET_SIZE - headerSize);

      std::vector<uint8_t> packet(MAX_PACKET_SIZE, 0);
      std::copy(jpegData.begin() + currentOffset,
                jpegData.begin() + currentOffset + byteCount,
                packet.begin() + headerSize);

      packet[0] = 0x14;
      packet[1] = 0xff;
      packet[2] = 0x02;
      packet[3] = 0x2b;
      packet[4] =
          generateWritePacketByte(part, false, remainingBytes - byteCount == 0);

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
        static uint8_t last_state = 0;
        uint8_t current_state = report[6];

        // Grid button detection (1-9 = buttons, 0 = no button)
        // IMPORTANT: Hardware quirk - when releasing buttons 2-9, the state
        // transitions through button 1 before reaching 0. Pattern: button_n → 1
        // → 0 We must ignore transitions TO button 1 unless coming FROM state 0

        if (current_state >= 1 && current_state <= 9) {
          uint8_t button_code = current_state - 1;

          // Ignore transition to button 1 (state=1) if not from idle (state=0)
          // This filters out the spurious button 1 events during other button
          // releases
          if (current_state == 1 && last_state != 0) {
            // Skip this spurious button 1 event - it's part of another button's
            // release
            last_state = current_state;
            continue;
          }

          // Real button press - add to set if not already there
          if (impl_->pressed_buttons.find(button_code) ==
              impl_->pressed_buttons.end()) {
            impl_->pressed_buttons.insert(button_code);

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
        } else if (current_state == 0) {
          // No button pressed - release all currently pressed buttons
          for (uint8_t button_code : impl_->pressed_buttons) {
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
          impl_->pressed_buttons.clear();
        }

        last_state = current_state;
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

  for (const auto &packet : packets) {
    write(impl_->hidraw_fd, packet.data(), packet.size());
    usleep(2000);
  }

  return true;
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
  for (auto &pair : impl_->animations) {
    pair.second->running = false;
    if (pair.second->animation_thread.joinable()) {
      pair.second->animation_thread.join();
    }
  }
  impl_->animations.clear();
}

} // namespace LogiLinux
