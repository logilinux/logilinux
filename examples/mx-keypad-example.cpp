#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <logilinux/device.h>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <random>
#include <thread>
#include <vector>

// Include the actual implementation header
#include "../lib/src/devices/mx_keypad_device.h"

std::atomic<bool> running(true);

void signalHandler(int signal) { running = false; }

std::vector<uint8_t> generateColorJPEG(uint8_t r, uint8_t g, uint8_t b) {
  char ppmname[256];
  char jpgname[256];
  snprintf(ppmname, sizeof(ppmname), "/tmp/color_%d_%d_%d.ppm", r, g, b);
  snprintf(jpgname, sizeof(jpgname), "/tmp/color_%d_%d_%d.jpg", r, g, b);

  FILE *ppm = fopen(ppmname, "wb");
  if (!ppm) {
    return {};
  }

  fprintf(ppm, "P6\n118 118\n255\n");

  for (int i = 0; i < 118 * 118; i++) {
    uint8_t rgb[3] = {r, g, b};
    fwrite(rgb, 1, 3, ppm);
  }

  fclose(ppm);

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "convert %s -quality 85 %s 2>/dev/null", ppmname,
           jpgname);

  if (system(cmd) != 0) {
    unlink(ppmname);
    return {};
  }

  FILE *f = fopen(jpgname, "rb");
  if (!f) {
    unlink(ppmname);
    return {};
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  std::vector<uint8_t> jpeg(size);
  fread(jpeg.data(), 1, size, f);
  fclose(f);

  unlink(ppmname);
  unlink(jpgname);

  return jpeg;
}

void onEvent(LogiLinux::EventPtr event,
             LogiLinux::MXKeypadDevice *device) {
  if (auto button =
          std::dynamic_pointer_cast<LogiLinux::ButtonEvent>(event)) {
    if (button->pressed) {
      int key_index = button->button_code;
      
      // Check if it's a navigation button (P1/P2)
      auto cc_button = LogiLinux::getMXKeypadButton(key_index);
      if (cc_button == LogiLinux::MXKeypadButton::P1_LEFT) {
        std::cout << "P1 (Left) button pressed!" << std::endl;
        return;
      } else if (cc_button == LogiLinux::MXKeypadButton::P2_RIGHT) {
        std::cout << "P2 (Right) button pressed!" << std::endl;
        return;
      }

      // Grid button (0-8)
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 255);

      uint8_t r = dis(gen);
      uint8_t g = dis(gen);
      uint8_t b = dis(gen);

      std::cout << "Button " << key_index << " pressed - Setting color RGB("
                << static_cast<int>(r) << ", " << static_cast<int>(g) << ", "
                << static_cast<int>(b) << ")" << std::endl;

      auto jpeg = generateColorJPEG(r, g, b);
      if (!jpeg.empty()) {
        device->setKeyImage(key_index, jpeg);
      }
    }
  }
}

int main() {
  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux MX Keypad Example v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Press any button to change its color!" << std::endl;
  std::cout << "Press Ctrl+C to exit\n" << std::endl;

  signal(SIGINT, signalHandler);

  LogiLinux::Library lib;

  std::cout << "Scanning for devices..." << std::endl;
  auto devices = lib.discoverDevices();

  if (devices.empty()) {
    std::cerr << "No Logitech devices found!" << std::endl;
    return 1;
  }

  LogiLinux::MXKeypadDevice *console_device = nullptr;

  for (const auto &device : devices) {
    const auto &info = device->getInfo();

    if (device->getType() == LogiLinux::DeviceType::MX_KEYPAD) {
      auto *cc_device =
          dynamic_cast<LogiLinux::MXKeypadDevice *>(device.get());
      
      // Check if this device has LCD capability
      if (cc_device && cc_device->hasCapability(LogiLinux::DeviceCapability::LCD_DISPLAY)) {
        console_device = cc_device;
        std::cout << "Found: " << info.name << " (" << info.device_path << ")"
                  << std::endl;
        std::cout << "  -> Using this MX Keypad with LCD!" << std::endl;
        break; // Use the first one with LCD
      }
    }
  }

  if (!console_device) {
    std::cerr << "No MX Keypad found!" << std::endl;
    return 1;
  }

  std::cout << "\nInitializing LCD..." << std::endl;
  
  if (!console_device->initialize()) {
    std::cerr << "Failed to initialize MX Keypad!" << std::endl;
    std::cerr << "Make sure you have permissions to access hidraw devices."
              << std::endl;
    return 1;
  }

  std::cout << "LCD initialized successfully!" << std::endl;

  // Set initial colors for all buttons
  std::cout << "\nSetting initial colors..." << std::endl;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);

  for (int i = 0; i < 9; i++) {
    uint8_t r = dis(gen);
    uint8_t g = dis(gen);
    uint8_t b = dis(gen);

    auto jpeg = generateColorJPEG(r, g, b);
    if (!jpeg.empty()) {
      console_device->setKeyImage(i, jpeg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nReady! Press buttons to change colors.\n" << std::endl;

  console_device->setEventCallback([console_device](LogiLinux::EventPtr event) {
    onEvent(event, console_device);
  });

  console_device->startMonitoring();

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  console_device->stopMonitoring();

  std::cout << "\nExiting..." << std::endl;
  return 0;
}
