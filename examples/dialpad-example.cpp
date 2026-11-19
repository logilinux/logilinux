#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <linux/input.h>
#include <logilinux/device.h>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <thread>

std::atomic<bool> running(true);

void signalHandler(int signal) { running = false; }

const char *getEventCodeName(uint16_t code) {
  switch (code) {
  case 0x06:
    return "REL_HWHEEL";
  case 0x07:
    return "REL_DIAL";
  case 0x08:
    return "REL_WHEEL";
  case 0x09:
    return "REL_MISC";
  case 0x0b:
    return "REL_WHEEL_HI_RES";
  case 0x0c:
    return "REL_HWHEEL_HI_RES";
  default:
    return "UNKNOWN";
  }
}

const char *getEventCodeDescription(uint16_t code) {
  switch (code) {
  case 0x06:
    return "Horizontal wheel (low-res)";
  case 0x07:
    return "Dial (low-res)";
  case 0x08:
    return "Scroll wheel (low-res)";
  case 0x09:
    return "Misc/High-res";
  case 0x0b:
    return "Scroll wheel (high-res)";
  case 0x0c:
    return "Knob/Dial (high-res)";
  default:
    return "Unknown code";
  }
}

void onEvent(LogiLinux::EventPtr event) {
  if (auto rotation =
          std::dynamic_pointer_cast<LogiLinux::RotationEvent>(event)) {
    std::string input_type =
        (rotation->rotation_type == LogiLinux::RotationType::DIAL) ? "DIAL"
                                                                   : "WHEEL";

    std::cout << "\n*** ROTATION EVENT ***" << std::endl;
    std::cout << "Classified as: [" << input_type << "]" << std::endl;
    std::cout << "Raw Code: " << getEventCodeName(rotation->raw_event_code)
              << " (" << rotation->raw_event_code << ") - "
              << getEventCodeDescription(rotation->raw_event_code) << std::endl;
    std::cout << "Delta: " << rotation->delta << " steps" << std::endl;
    std::cout << "High-res: " << rotation->delta_high_res << " units"
              << std::endl;
    std::cout << "Timestamp: " << rotation->timestamp << " μs" << std::endl;
  } else if (auto button =
                 std::dynamic_pointer_cast<LogiLinux::ButtonEvent>(event)) {
    auto dialpad_button = LogiLinux::getDialpadButton(button->button_code);

    std::cout << "\n*** BUTTON EVENT ***" << std::endl;
    std::cout << "Button: " << LogiLinux::getDialpadButtonName(dialpad_button)
              << " (code " << button->button_code << ")" << std::endl;
    std::cout << "Action: " << (button->pressed ? "PRESSED" : "RELEASED")
              << std::endl;
    std::cout << "Timestamp: " << button->timestamp << " μs" << std::endl;
  }
}

int main() {
  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux Dialpad Example v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Press Ctrl+C to exit" << std::endl << std::endl;

  signal(SIGINT, signalHandler);

  LogiLinux::Library lib;

  std::cout << "Scanning for devices..." << std::endl;
  auto devices = lib.discoverDevices();

  if (devices.empty()) {
    std::cerr << "No Logitech devices found!" << std::endl;
    std::cerr << "Make sure your device is connected and you have permissions "
                 "to access /dev/input/*"
              << std::endl;
    return 1;
  }

  std::cout << "Found " << devices.size() << " device(s):" << std::endl;
  for (const auto &device : devices) {
    const auto &info = device->getInfo();
    std::cout << "  - " << info.name << " (VID: 0x" << std::hex
              << info.vendor_id << ", PID: 0x" << info.product_id << std::dec
              << ")"
              << " at " << info.device_path << std::endl;
  }
  std::cout << std::endl;

  auto dialpad = lib.findDevice(LogiLinux::DeviceType::DIALPAD);
  if (!dialpad) {
    std::cerr << "No MX Dialpad found!" << std::endl;
    return 1;
  }

  std::cout << "Found MX Dialpad: " << dialpad->getInfo().name << std::endl;
  std::cout << "Monitoring events..." << std::endl << std::endl;

  dialpad->setEventCallback(onEvent);

  dialpad->startMonitoring();
  if (!dialpad->isMonitoring()) {
    std::cerr << "Failed to start monitoring!" << std::endl;
    std::cerr << "Try running with sudo if you get permission errors."
              << std::endl;
    return 1;
  }

  if (!dialpad->grabExclusive(true)) {
    std::cerr << "Warning: Could not grab device exclusively." << std::endl;
    std::cerr << "Default device functionality may still be active."
              << std::endl;
  } else {
    std::cout << "Device grabbed exclusively - default functionality disabled."
              << std::endl;
  }

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << std::endl << "Stopping..." << std::endl;
  dialpad->stopMonitoring();

  return 0;
}
