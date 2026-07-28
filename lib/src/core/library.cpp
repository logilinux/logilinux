
#include "core/device_manager.h"
#include "logilinux/logilinux.h"
#include "logilinux/version.h"
#include <algorithm>

namespace LogiLinux {

class Library::Impl {
public:
  Impl() : device_manager_(std::make_unique<DeviceManager>()) {}

  std::unique_ptr<DeviceManager> device_manager_;
  std::vector<DevicePtr> devices_;
};

Library::Library() : pImpl(std::make_unique<Impl>()) {}

Library::~Library() = default;

Version Library::getVersion() { return LogiLinux::getVersion(); }

std::vector<DevicePtr> Library::discoverDevices() {
  pImpl->devices_ = pImpl->device_manager_->scanDevices();
  return pImpl->devices_;
}

DevicePtr Library::findDevice(DeviceType type) {
  // Device nodes can change after an unplug or KVM switch.  Re-scan instead
  // of returning a cached object whose file descriptors refer to the old
  // connection.
  discoverDevices();

  // For MX_KEYPAD, prefer hidraw devices over event devices
  // because the keypad needs hidraw for LCD control and initialization
  if (type == DeviceType::MX_KEYPAD) {
    // First pass: look for hidraw device
    for (const auto &device : pImpl->devices_) {
      if (device->getType() == type) {
        const auto &info = device->getInfo();
        if (info.device_path.find("/dev/hidraw") != std::string::npos) {
          return device;
        }
      }
    }
    // Second pass: if no hidraw found, return any matching device
    for (const auto &device : pImpl->devices_) {
      if (device->getType() == type) {
        return device;
      }
    }
  } else {
    // For other devices, return first match
    for (const auto &device : pImpl->devices_) {
      if (device->getType() == type) {
        return device;
      }
    }
  }

  return nullptr;
}

std::vector<DevicePtr> Library::findDevices(DeviceType type) {
  discoverDevices();

  std::vector<DevicePtr> result;
  for (const auto &device : pImpl->devices_) {
    if (device->getType() == type) {
      result.push_back(device);
    }
  }

  return result;
}

} // namespace LogiLinux
