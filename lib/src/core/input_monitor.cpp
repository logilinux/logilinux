/*
 * LogiLinux - Input Monitor Implementation
 */

#include "input_monitor.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace LogiLinux {

InputMonitor::InputMonitor(const std::string &device_path)
    : device_path_(device_path), running_(false), should_stop_(false),
      device_fd_(-1) {}

InputMonitor::~InputMonitor() { stop(); }

bool InputMonitor::start(EventCallback callback) {
  if (running_) {
    return false;
  }

  callback_ = callback;

  device_fd_ = open(device_path_.c_str(), O_RDONLY | O_NONBLOCK);
  if (device_fd_ < 0) {
    return false;
  }

  should_stop_ = false;
  running_ = true;
  monitor_thread_ = std::thread(&InputMonitor::monitorLoop, this);

  return true;
}

bool InputMonitor::grabDevice(bool grab) {
  if (device_fd_ < 0) {
    return false;
  }

  int grab_flag = grab ? 1 : 0;
  if (ioctl(device_fd_, EVIOCGRAB, grab_flag) < 0) {
    return false;
  }

  return true;
}

void InputMonitor::stop() {
  should_stop_ = true;

  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }

  if (device_fd_ >= 0) {
    close(device_fd_);
    device_fd_ = -1;
  }

  running_ = false;
}

void InputMonitor::monitorLoop() {
  struct pollfd pfd;
  pfd.fd = device_fd_;
  pfd.events = POLLIN;

  struct input_event ev;

  while (!should_stop_) {
    int ret = poll(&pfd, 1, 100);

    if (ret < 0) {
      break;
    }

    if (ret == 0) {
      continue;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      break;
    }

    if (pfd.revents & POLLIN) {
      ssize_t bytes = read(device_fd_, &ev, sizeof(ev));

      if (bytes == sizeof(ev)) {
        processEvent(ev);
      } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        break;
      }
    }
  }

  running_ = false;
}

void InputMonitor::processEvent(const struct input_event &ev) {
  if (!callback_) {
    return;
  }

  if (ev.type == EV_REL) {
    if (ev.code == 0x06 || ev.code == 0x08 || ev.code == 0x0b ||
        ev.code == 0x0c || ev.code == REL_HWHEEL || ev.code == REL_MISC ||
        ev.code == REL_WHEEL || ev.code == REL_DIAL) {

      auto event = std::make_shared<RotationEvent>();
      event->timestamp =
          static_cast<uint64_t>(ev.time.tv_sec) * 1000000 + ev.time.tv_usec;
      event->type = EventType::ROTATION;
      event->raw_event_code = ev.code;

      // Code 8 (REL_WHEEL) or 11 (REL_WHEEL_HI_RES) = Scroll wheel
      // Code 6 (REL_HWHEEL) or 12 (REL_HWHEEL_HI_RES) = Dial/knob
      if (ev.code == 0x08 || ev.code == 0x0b) {
        event->rotation_type = RotationType::WHEEL;
      } else {
        event->rotation_type = RotationType::DIAL;
      }

      if (ev.code == 0x06 || ev.code == 0x08) {
        event->delta = ev.value;
        event->delta_high_res = ev.value * 120;
      } else if (ev.code == 0x0b || ev.code == 0x0c) {
        event->delta_high_res = ev.value;
        event->delta = ev.value / 120;
        if (event->delta == 0 && ev.value != 0) {
          event->delta = (ev.value > 0) ? 1 : -1;
        }
      } else if (ev.code == REL_DIAL) {
        event->delta = ev.value;
        event->delta_high_res = ev.value * 120;
      } else if (ev.code == REL_MISC) {
        event->delta_high_res = ev.value;
        event->delta = (ev.value > 0) ? 1 : -1;
      }

      callback_(event);
    }
  }

  else if (ev.type == EV_KEY) {
    auto event = std::make_shared<ButtonEvent>();
    event->timestamp =
        static_cast<uint64_t>(ev.time.tv_sec) * 1000000 + ev.time.tv_usec;
    event->button_code = ev.code;

    if (ev.value == 1) {
      event->type = EventType::BUTTON_PRESS;
      event->pressed = true;
    } else if (ev.value == 0) {
      event->type = EventType::BUTTON_RELEASE;
      event->pressed = false;
    } else {
      return;
    }

    callback_(event);
  }
}

} // namespace LogiLinux
