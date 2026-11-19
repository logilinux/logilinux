#ifndef LOGILINUX_EVENTS_H
#define LOGILINUX_EVENTS_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace LogiLinux {

enum class RotationType { DIAL, WHEEL };

enum class DialpadButton {
  TOP_LEFT = 275,
  TOP_RIGHT = 276,
  BOTTOM_LEFT = 277,
  BOTTOM_RIGHT = 278,
  UNKNOWN = 0
};

enum class MXKeypadButton {
  GRID_0 = 0,
  GRID_1 = 1,
  GRID_2 = 2,
  GRID_3 = 3,
  GRID_4 = 4,
  GRID_5 = 5,
  GRID_6 = 6,
  GRID_7 = 7,
  GRID_8 = 8,
  P1_LEFT = 0xa1,
  P2_RIGHT = 0xa2,
  UNKNOWN = 0xff
};

enum class EventType {
  ROTATION,
  BUTTON_PRESS,
  BUTTON_RELEASE,
  DEVICE_CONNECTED,
  DEVICE_DISCONNECTED
};

struct Event {
  EventType type;
  uint64_t timestamp;

  Event() : type(EventType::ROTATION), timestamp(0) {}
  explicit Event(EventType t) : type(t), timestamp(0) {}
  virtual ~Event() = default;
};

struct RotationEvent : public Event {
  RotationType rotation_type;
  int32_t delta;
  int32_t delta_high_res;
  uint16_t raw_event_code;

  RotationEvent()
      : Event(EventType::ROTATION), rotation_type(RotationType::DIAL), delta(0),
        delta_high_res(0), raw_event_code(0) {}
};

struct ButtonEvent : public Event {
  uint32_t button_code;
  bool pressed;

  ButtonEvent()
      : Event(EventType::BUTTON_PRESS), button_code(0), pressed(false) {}
};

struct DeviceEvent : public Event {
  std::string device_path;

  DeviceEvent() : Event(EventType::DEVICE_CONNECTED) {}
};

using EventPtr = std::shared_ptr<Event>;
using RotationEventPtr = std::shared_ptr<RotationEvent>;
using ButtonEventPtr = std::shared_ptr<ButtonEvent>;
using DeviceEventPtr = std::shared_ptr<DeviceEvent>;

using EventCallback = std::function<void(EventPtr)>;

inline DialpadButton getDialpadButton(uint32_t button_code) {
  switch (button_code) {
  case 275:
    return DialpadButton::TOP_LEFT;
  case 276:
    return DialpadButton::TOP_RIGHT;
  case 277:
    return DialpadButton::BOTTOM_LEFT;
  case 278:
    return DialpadButton::BOTTOM_RIGHT;
  default:
    return DialpadButton::UNKNOWN;
  }
}

inline const char *getDialpadButtonName(DialpadButton button) {
  switch (button) {
  case DialpadButton::TOP_LEFT:
    return "TOP_LEFT";
  case DialpadButton::TOP_RIGHT:
    return "TOP_RIGHT";
  case DialpadButton::BOTTOM_LEFT:
    return "BOTTOM_LEFT";
  case DialpadButton::BOTTOM_RIGHT:
    return "BOTTOM_RIGHT";
  default:
    return "UNKNOWN";
  }
}

inline MXKeypadButton getMXKeypadButton(uint32_t button_code) {
  if (button_code >= 0 && button_code <= 8) {
    return static_cast<MXKeypadButton>(button_code);
  }
  switch (button_code) {
  case 0xa1:
    return MXKeypadButton::P1_LEFT;
  case 0xa2:
    return MXKeypadButton::P2_RIGHT;
  default:
    return MXKeypadButton::UNKNOWN;
  }
}

inline const char *getMXKeypadButtonName(MXKeypadButton button) {
  switch (button) {
  case MXKeypadButton::GRID_0:
    return "GRID_0";
  case MXKeypadButton::GRID_1:
    return "GRID_1";
  case MXKeypadButton::GRID_2:
    return "GRID_2";
  case MXKeypadButton::GRID_3:
    return "GRID_3";
  case MXKeypadButton::GRID_4:
    return "GRID_4";
  case MXKeypadButton::GRID_5:
    return "GRID_5";
  case MXKeypadButton::GRID_6:
    return "GRID_6";
  case MXKeypadButton::GRID_7:
    return "GRID_7";
  case MXKeypadButton::GRID_8:
    return "GRID_8";
  case MXKeypadButton::P1_LEFT:
    return "P1_LEFT";
  case MXKeypadButton::P2_RIGHT:
    return "P2_RIGHT";
  default:
    return "UNKNOWN";
  }
}

} // namespace LogiLinux

#endif // LOGILINUX_EVENTS_H
