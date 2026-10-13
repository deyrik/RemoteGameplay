#ifndef LINUX_VIRTUAL_GAMEPAD_HPP
#define LINUX_VIRTUAL_GAMEPAD_HPP

#if defined(__linux__)

#include "IVirtualGamepad.hpp"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include <iostream>

class LinuxVirtualGamepad : public IVirtualGamepad {
private:
    int fd = -1;
    GamepadProfile currentProfile = GamepadProfile::DISCONNECTED;
    RumbleCallback rumbleCallback;
    std::atomic<bool> ffRunning{false};
    std::thread ffThread;
    struct ff_effect effects[16]{};

    static constexpr uint16_t XBOX_BUTTON_MAP[15] = {
        BTN_A, BTN_B, BTN_X, BTN_Y,
        BTN_TL, BTN_TR, BTN_SELECT, BTN_START,
        BTN_MODE, BTN_THUMBL, BTN_THUMBR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT
    };

    static constexpr uint16_t PLAYSTATION_BUTTON_MAP[15] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
        BTN_SELECT, BTN_START, BTN_MODE,
        BTN_THUMBL, BTN_THUMBR, BTN_THUMBL, BTN_THUMBR
    };

    static constexpr uint16_t AXIS_MAP[8] = {
        ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y
    };

    // Thread que escuta os comandos de vibração enviados pelo Jogo ao Kernel (/dev/uinput)
    void ffListenLoop() {
        struct pollfd pfd{fd, POLLIN, 0};
        while (ffRunning && fd >= 0) {
            if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
                struct input_event ev{};
                if (read(fd, &ev, sizeof(ev)) != sizeof(ev)) continue;

                if (ev.type == EV_UINPUT) {
                    if (ev.code == UI_FF_UPLOAD) {
                        struct uinput_ff_upload upload{};
                        upload.request_id = ev.value;
                        if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload) == 0) {
                            if (upload.effect.id >= 0 && upload.effect.id < 16) {
                                effects[upload.effect.id] = upload.effect;
                            }
                            upload.retval = 0;
                            ioctl(fd, UI_END_FF_UPLOAD, &upload);
                        }
                    } else if (ev.code == UI_FF_ERASE) {
                        struct uinput_ff_erase erase{};
                        erase.request_id = ev.value;
                        if (ioctl(fd, UI_BEGIN_FF_ERASE, &erase) == 0) {
                            erase.retval = 0;
                            ioctl(fd, UI_END_FF_ERASE, &erase);
                        }
                    }
                } else if (ev.type == EV_FF && ev.code < 16) {
                    if (rumbleCallback) {
                        if (ev.value > 0) {
                            uint16_t strong = effects[ev.code].u.rumble.strong_magnitude;
                            uint16_t weak   = effects[ev.code].u.rumble.weak_magnitude;
                            rumbleCallback(strong, weak);
                        } else {
                            rumbleCallback(0, 0);
                        }
                    }
                }
            }
        }
    }

    void destroyDevice() {
        ffRunning = false;
        if (ffThread.joinable()) {
            ffThread.join();
        }
        if (fd >= 0) {
            ioctl(fd, UI_DEV_DESTROY);
            close(fd);
            fd = -1;
            std::cout << "[VirtualGamepad Linux] Controle virtual desconectado do Kernel." << std::endl;
        }
        currentProfile = GamepadProfile::DISCONNECTED;
    }

    void emitEvent(uint16_t type, uint16_t code, int32_t value) {
        if (fd < 0) return;
        struct input_event ev{};
        ev.type = type;
        ev.code = code;
        ev.value = value;
        if (write(fd, &ev, sizeof(ev)) < 0) {}
    }

public:
    LinuxVirtualGamepad() = default;

    ~LinuxVirtualGamepad() override {
        destroyDevice();
    }

    void setRumbleCallback(RumbleCallback cb) override {
        rumbleCallback = std::move(cb);
    }

    void setProfile(GamepadProfile profile, uint16_t vendorId = 0, uint16_t productId = 0) override {
        if (profile == currentProfile && fd >= 0) return;

        destroyDevice();
        if (profile == GamepadProfile::DISCONNECTED) return;

        // O_RDWR é necessário para ler eventos de Force Feedback (EV_FF) vindos do jogo
        fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "[VirtualGamepad Linux] Sem permissao em /dev/uinput (sudo chmod 666 /dev/uinput)." << std::endl;
            return;
        }

        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        ioctl(fd, UI_SET_EVBIT, EV_SYN);
        ioctl(fd, UI_SET_EVBIT, EV_FF);
        ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);

        const uint16_t* activeMap = (profile == GamepadProfile::PS4_DUALSHOCK || profile == GamepadProfile::PS5_DUALSENSE)
                                    ? PLAYSTATION_BUTTON_MAP : XBOX_BUTTON_MAP;

        for (int i = 0; i < 15; ++i) {
            ioctl(fd, UI_SET_KEYBIT, activeMap[i]);
        }

        struct uinput_user_dev uidev{};
        uidev.id.bustype = BUS_USB;
        uidev.id.version = 0x0111;
        uidev.ff_effects_max = 16; // Suporta até 16 efeitos simultâneos de vibração

        switch (profile) {
            case GamepadProfile::XBOX_360:
                std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "StreamGamer Virtual Xbox 360 Pad");
                uidev.id.vendor  = vendorId  ? vendorId  : 0x045e;
                uidev.id.product = productId ? productId : 0x028e;
                break;
            case GamepadProfile::XBOX_ONE_SERIES:
                std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "StreamGamer Virtual Xbox One/Series Pad");
                uidev.id.vendor  = vendorId  ? vendorId  : 0x045e;
                uidev.id.product = productId ? productId : 0x0b12;
                break;
            case GamepadProfile::PS4_DUALSHOCK:
                std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "StreamGamer Virtual Wireless Controller");
                uidev.id.vendor  = vendorId  ? vendorId  : 0x054c;
                uidev.id.product = productId ? productId : 0x09cc;
                break;
            case GamepadProfile::PS5_DUALSENSE:
                std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "StreamGamer Virtual DualSense Controller");
                uidev.id.vendor  = vendorId  ? vendorId  : 0x054c;
                uidev.id.product = productId ? productId : 0x0ce6;
                break;
            default:
                std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "StreamGamer Virtual Generic Gamepad");
                uidev.id.vendor  = vendorId  ? vendorId  : 0x0079;
                uidev.id.product = productId ? productId : 0x0006;
                break;
        }

        for (uint16_t axis : AXIS_MAP) {
            ioctl(fd, UI_SET_ABSBIT, axis);
            uidev.absmin[axis]  = -32767;
            uidev.absmax[axis]  =  32767;
            uidev.absfuzz[axis] = 16;
            uidev.absflat[axis] = 128;
        }

        if (write(fd, &uidev, sizeof(uidev)) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            std::cerr << "[VirtualGamepad Linux] Falha ao criar dispositivo uinput." << std::endl;
            close(fd);
            fd = -1;
            return;
        }

        currentProfile = profile;
        ffRunning = true;
        ffThread = std::thread(&LinuxVirtualGamepad::ffListenLoop, this);

        std::cout << "[VirtualGamepad Linux] Perfil Ativo com Force Feedback (Rumble): '" << uidev.name
                  << "' (Vendor=0x" << std::hex << uidev.id.vendor
                  << ", Product=0x" << uidev.id.product << std::dec << ")" << std::endl;
    }

    void injectButton(uint16_t buttonId, bool isPressed) override {
        if (fd < 0) setProfile(GamepadProfile::XBOX_360);
        if (fd < 0 || buttonId >= 15) return;

        const uint16_t* activeMap = (currentProfile == GamepadProfile::PS4_DUALSHOCK || currentProfile == GamepadProfile::PS5_DUALSENSE)
                                    ? PLAYSTATION_BUTTON_MAP : XBOX_BUTTON_MAP;

        emitEvent(EV_KEY, activeMap[buttonId], isPressed ? 1 : 0);
        emitEvent(EV_SYN, SYN_REPORT, 0);
    }

    void injectAxis(uint8_t axisId, int16_t value) override {
        if (fd < 0) setProfile(GamepadProfile::XBOX_360);
        if (fd < 0 || axisId >= 8) return;

        emitEvent(EV_ABS, AXIS_MAP[axisId], value);
        emitEvent(EV_SYN, SYN_REPORT, 0);
    }
};

constexpr uint16_t LinuxVirtualGamepad::XBOX_BUTTON_MAP[15];
constexpr uint16_t LinuxVirtualGamepad::PLAYSTATION_BUTTON_MAP[15];
constexpr uint16_t LinuxVirtualGamepad::AXIS_MAP[8];

#endif // __linux__
#endif