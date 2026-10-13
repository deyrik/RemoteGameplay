#ifndef IVIRTUAL_GAMEPAD_HPP
#define IVIRTUAL_GAMEPAD_HPP

#include <cstdint>
#include <functional>

enum class GamepadProfile : uint8_t {
    DISCONNECTED    = 0,
    XBOX_360        = 1,
    XBOX_ONE_SERIES = 2,
    PS4_DUALSHOCK   = 3,
    PS5_DUALSENSE   = 4,
    GENERIC_USB     = 5
};

using RumbleCallback = std::function<void(uint16_t strongMagnitude, uint16_t weakMagnitude)>;

// [SOLID - ISP / DIP] Interface para criação dinâmica, injeção e Force Feedback de controles virtuais
class IVirtualGamepad {
public:
    virtual ~IVirtualGamepad() = default;
    virtual void setProfile(GamepadProfile profile, uint16_t vendorId = 0, uint16_t productId = 0) = 0;
    virtual void injectButton(uint16_t buttonId, bool isPressed) = 0;
    virtual void injectAxis(uint8_t axisId, int16_t value) = 0;
    virtual void setRumbleCallback(RumbleCallback /*cb*/) {}
};

#endif