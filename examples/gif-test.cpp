#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <thread>

// Include the actual implementation header
#include "../lib/src/devices/mx_keypad_device.h"

std::atomic<bool> running(true);

void signalHandler(int signal) { running = false; }

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <gif_file.gif>" << std::endl;
    std::cerr << "Example: " << argv[0] << " animation.gif" << std::endl;
    return 1;
  }

  std::string gif_path = argv[1];

  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux GIF Animation Test v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Testing GIF: " << gif_path << "\n" << std::endl;

  signal(SIGINT, signalHandler);

  LogiLinux::Library lib;

  std::cout << "Scanning for devices..." << std::endl;
  auto devices = lib.discoverDevices();

  if (devices.empty()) {
    std::cerr << "No Logitech devices found!" << std::endl;
    return 1;
  }

  LogiLinux::MXKeypadDevice *keypad = nullptr;

  for (const auto &device : devices) {
    if (device->getType() == LogiLinux::DeviceType::MX_KEYPAD) {
      auto *kp = dynamic_cast<LogiLinux::MXKeypadDevice *>(device.get());

      if (kp && kp->hasCapability(LogiLinux::DeviceCapability::LCD_DISPLAY)) {
        keypad = kp;
        const auto &info = device->getInfo();
        std::cout << "Found: " << info.name << std::endl;
        break;
      }
    }
  }

  if (!keypad) {
    std::cerr << "No MX Keypad with LCD found!" << std::endl;
    return 1;
  }

  std::cout << "\nInitializing device..." << std::endl;

  if (!keypad->initialize()) {
    std::cerr << "Failed to initialize MX Keypad!" << std::endl;
    std::cerr << "Try running with sudo." << std::endl;
    return 1;
  }

  std::cout << "Device initialized!" << std::endl;

  std::cout << "\nLoading GIF and starting animation on all 9 buttons..."
            << std::endl;

  // Set the same GIF on all 9 buttons
  for (int i = 0; i < 9; i++) {
    std::cout << "Starting animation on button " << i << "..." << std::endl;
    if (!keypad->setKeyGifFromFile(i, gif_path, true)) {
      std::cerr << "Failed to set GIF on button " << i << std::endl;
    }
  }

  std::cout << "\nAnimations running! Press Ctrl+C to stop.\n" << std::endl;

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nStopping animations..." << std::endl;
  keypad->stopAllAnimations();

  std::cout << "Done!" << std::endl;
  return 0;
}
