#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <logilinux/device.h>
#include <logilinux/events.h>
#include <logilinux/logilinux.h>
#include <memory>
#include <thread>

std::atomic<bool> running(true);
std::atomic<bool> is_muted(false);

void signalHandler(int signal) { running = false; }

std::string exec(const char *cmd) {
  std::string full_cmd = cmd;

  const char *sudo_user = std::getenv("SUDO_USER");
  const char *sudo_uid = std::getenv("SUDO_UID");

  if (sudo_user && sudo_uid) {
    std::string runtime_dir = std::string("/run/user/") + sudo_uid;
    full_cmd = std::string("sudo -u ") + sudo_user +
               " XDG_RUNTIME_DIR=" + runtime_dir + " " + cmd;
  }

  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

int getCurrentVolume() {
  std::string output = exec("pactl get-sink-volume @DEFAULT_SINK@ | grep -oP "
                            "'\\d+%' | head -1 | tr -d '%'");
  return output.empty() ? 50 : std::stoi(output);
}

void setVolume(int volume) {
  if (volume < 0)
    volume = 0;

  std::string cmd =
      "pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(volume) + "%";
  exec(cmd.c_str());
}

bool isMuted() {
  std::string output = exec("pactl get-sink-mute @DEFAULT_SINK@");
  return output.find("yes") != std::string::npos;
}

void toggleMute() {
  exec("pactl set-sink-mute @DEFAULT_SINK@ toggle");
  is_muted = isMuted();
  std::cout << "\nAudio " << (is_muted ? "MUTED" : "UNMUTED") << std::endl;
}

void onEvent(LogiLinux::EventPtr event) {
  if (auto rotation =
          std::dynamic_pointer_cast<LogiLinux::RotationEvent>(event)) {

    if (rotation->rotation_type != LogiLinux::RotationType::DIAL) {
      return;
    }

    int current_volume = getCurrentVolume();
    int new_volume = current_volume + (rotation->delta * 5);

    setVolume(new_volume);

    std::cout << "Dial - Volume: " << new_volume << "% ";

    int bars = new_volume / 5;
    std::cout << "[";
    for (int i = 0; i < 20; i++) {
      std::cout << (i < bars ? "=" : " ");
    }
    std::cout << "]" << std::endl;
  } else if (auto button =
                 std::dynamic_pointer_cast<LogiLinux::ButtonEvent>(event)) {
    if (!button->pressed) {
      return;
    }

    auto dialpad_button = LogiLinux::getDialpadButton(button->button_code);

    if (dialpad_button == LogiLinux::DialpadButton::TOP_LEFT) {
      toggleMute();
    }
  }
}

int main() {
  auto version = LogiLinux::getVersion();
  std::cout << "LogiLinux Volume Controller Example v" << version.major << "."
            << version.minor << "." << version.patch << std::endl;
  std::cout << "Using PipeWire/PulseAudio via pactl" << std::endl;
  std::cout << "Press Ctrl+C to exit" << std::endl << std::endl;

  signal(SIGINT, signalHandler);

  LogiLinux::Library lib;

  std::cout << "Searching for MX Dialpad..." << std::endl;
  auto dialpad = lib.findDevice(LogiLinux::DeviceType::DIALPAD);
  if (!dialpad) {
    std::cerr << "No MX Dialpad found!" << std::endl;
    std::cerr << "Make sure your device is connected and you have permissions."
              << std::endl;
    return 1;
  }

  std::cout << "Found: " << dialpad->getInfo().name << std::endl;
  std::cout << "Current volume: " << getCurrentVolume() << "%" << std::endl;
  is_muted = isMuted();
  std::cout << "Mute status: " << (is_muted ? "MUTED" : "UNMUTED") << std::endl;
  std::cout << "\nControls:" << std::endl;
  std::cout << "  - Rotate dial: Adjust volume (5% per step)" << std::endl;
  std::cout << "  - Press TOP_LEFT button: Toggle mute" << std::endl
            << std::endl;

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
  dialpad->grabExclusive(false);
  dialpad->stopMonitoring();

  return 0;
}
