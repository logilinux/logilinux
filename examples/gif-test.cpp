#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <thread>

// Include the actual implementation header
#include "../lib/src/devices/mx_keypad_device.h"

std::atomic<bool> running(true);

void signalHandler(int signal) { running = false; }

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog << " [options] <gif_file.gif>" << std::endl;
  std::cerr << "\nOptions:" << std::endl;
  std::cerr << "  --fullscreen, -f   Use optimized full-screen mode (default)" << std::endl;
  std::cerr << "  --per-key, -k      Use per-key mode (9 separate animations)" << std::endl;
  std::cerr << "\nExample:" << std::endl;
  std::cerr << "  " << prog << " animation.gif" << std::endl;
  std::cerr << "  " << prog << " --per-key animation.gif" << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  bool fullscreen_mode = true;  // Default to optimized full-screen mode
  std::string gif_path;

  // Parse arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
      fullscreen_mode = true;
    } else if (strcmp(argv[i], "--per-key") == 0 || strcmp(argv[i], "-k") == 0) {
      fullscreen_mode = false;
    } else if (argv[i][0] != '-') {
      gif_path = argv[i];
    } else {
      std::cerr << "Unknown option: " << argv[i] << std::endl;
      printUsage(argv[0]);
      return 1;
    }
  }

  if (gif_path.empty()) {
    std::cerr << "Error: No GIF file specified" << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux GIF Animation Test v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Testing GIF: " << gif_path << std::endl;
  std::cout << "Mode: " << (fullscreen_mode ? "Full-screen (optimized)" : "Per-key (9 animations)") << "\n" << std::endl;

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

  if (fullscreen_mode) {
    // Optimized: Single full-screen GIF (1 HID write per frame instead of 9)
    std::cout << "\nStarting full-screen GIF animation..." << std::endl;
    
    if (!keypad->setScreenGifFromFile(gif_path, true)) {
      std::cerr << "Failed to start full-screen GIF animation!" << std::endl;
      return 1;
    }
  } else {
    // Legacy: Set the same GIF on all 9 buttons (9 HID writes per frame)
    std::cout << "\nLoading GIF and starting animation on all 9 buttons..." << std::endl;

    for (int i = 0; i < 9; i++) {
      std::cout << "Starting animation on button " << i << "..." << std::endl;
      if (!keypad->setKeyGifFromFile(i, gif_path, true)) {
        std::cerr << "Failed to set GIF on button " << i << std::endl;
      }
    }
  }

  std::cout << "\nAnimation running! Press Ctrl+C to stop.\n" << std::endl;

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nStopping animations..." << std::endl;
  keypad->stopAllAnimations();

  std::cout << "Done!" << std::endl;
  return 0;
}
