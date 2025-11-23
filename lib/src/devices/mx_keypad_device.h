#ifndef LOGILINUX_MX_KEYPAD_DEVICE_H
#define LOGILINUX_MX_KEYPAD_DEVICE_H

#include "logilinux/device.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace LogiLinux {

class MXKeypadDevice : public Device {
public:
  explicit MXKeypadDevice(const DeviceInfo &info);
  ~MXKeypadDevice() override;

  const DeviceInfo &getInfo() const override { return info_; }
  DeviceType getType() const override { return info_.type; }
  bool hasCapability(DeviceCapability cap) const override;

  void setEventCallback(EventCallback callback) override;
  void startMonitoring() override;
  void stopMonitoring() override;
  bool isMonitoring() const override;

  bool grabExclusive(bool grab) override;

  // MX Keypad specific API
  bool setKeyImage(int keyIndex, const std::vector<uint8_t> &jpegData);
  bool setKeyColor(int keyIndex, uint8_t r, uint8_t g, uint8_t b);
  bool initialize();
  bool hasLCD() const;

  // GIF support
  bool setKeyGif(int keyIndex, const std::vector<uint8_t> &gifData,
                 bool loop = true);
  bool setKeyGifFromFile(int keyIndex, const std::string &gifPath,
                         bool loop = true);
  void stopKeyAnimation(int keyIndex);
  void stopAllAnimations();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  DeviceInfo info_;
  std::vector<DeviceCapability> capabilities_;
  EventCallback event_callback_;
};

} // namespace LogiLinux

#endif // LOGILINUX_MX_KEYPAD_DEVICE_H
