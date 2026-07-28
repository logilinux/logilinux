#include "devices/mx_keypad_device.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

bool waitForMonitorToExit(LogiLinux::MXKeypadDevice &device) {
  constexpr auto timeout = std::chrono::seconds(2);
  constexpr auto poll_interval = std::chrono::milliseconds(1);
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (device.isMonitoring() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(poll_interval);
  }

  return !device.isMonitoring();
}

} // namespace

int main() {
  LogiLinux::DeviceInfo info{
      "monitor restart test",
      "/dev/hidraw-logilinux-test-missing",
      0x046d,
      0xc354,
      LogiLinux::DeviceType::MX_KEYPAD,
  };
  LogiLinux::MXKeypadDevice device(info);
  device.setEventCallback([](LogiLinux::EventPtr) {});

  // Opening the deliberately missing path makes each worker exit immediately.
  // Restarting used to assign over that still-joinable worker and abort.
  for (int attempt = 0; attempt < 10; ++attempt) {
    device.startMonitoring();
    if (!waitForMonitorToExit(device)) {
      std::cerr << "monitor did not exit after failed open\n";
      return 1;
    }
  }

  device.stopMonitoring();
  return 0;
}
