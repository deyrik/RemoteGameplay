#ifndef WINDOWS_VIRTUAL_GAMEPAD_HPP
#define WINDOWS_VIRTUAL_GAMEPAD_HPP

#if defined(_WIN32)

#include "IVirtualGamepad.hpp"
#include <windows.h>
#include <iostream>

#if defined(USE_VIGEM)
    #include <ViGEm/Client.h>
#endif

class WindowsVirtualGamepad : public IVirtualGamepad {
private:
#if defined(USE_VIGEM)
    PVIGEM_CLIENT client = nullptr;
    PVIGEM_TARGET pad = nullptr;
    XUSB_REPORT report{};
#endif

RumbleCallback rumbleCallback;
#if defined(USE_VIGEM)
    static VOID CALLBACK onVigemRumble(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR largeMotor, UCHAR smallMotor, UCHAR, LPVOID userData) {
        auto* self = static_cast<WindowsVirtualGamepad*>(userData);
        if (self && self->rumbleCallback) {
            self->rumbleCallback(static_cast<uint16_t>(largeMotor) * 257,
                                 static_cast<uint16_t>(smallMotor) * 257);
        }
    }
#endif

    // Auxiliar para modo Fallback (emula tecla via ScanCode quando ViGEmBus não está instalado)
    static void sendFallbackKey(WORD vk, bool isPressed) {
        WORD scan = static_cast<WORD>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (isPressed ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &input, sizeof(INPUT));
    }

public:
    void setProfile(GamepadProfile profile, uint16_t vendorId = 0, uint16_t productId = 0) override {}

    void setRumbleCallback(RumbleCallback cb) override {
        rumbleCallback = std::move(cb);
    }
    
    WindowsVirtualGamepad() {
        #if defined(USE_VIGEM)
                client = vigem_alloc();
                if (client && VIGEM_SUCCESS(vigem_connect(client))) {
                    pad = vigem_target_x360_alloc();
                    if (VIGEM_SUCCESS(vigem_target_add(client, pad))) {
                        XUSB_REPORT_INIT(&report);
                        std::cout << "[VirtualGamepad Win32] Controle Xbox 360 Virtual (ViGEmBus) ativo!" << std::endl;
                        return;
                    }
                }
                std::cerr << "[VirtualGamepad Win32] Driver ViGEmBus nao encontrado." << std::endl;
        #else
                std::cout << "[VirtualGamepad Win32] Modo Nativo (Fallback SendInput) ativo. "
                        << "(Compile com -DUSE_VIGEM para usar o driver ViGEmBus)." << std::endl;
        #endif
    }

    ~WindowsVirtualGamepad() override {
        #if defined(USE_VIGEM)
                if (client && pad) {
                    vigem_target_remove(client, pad);
                    vigem_target_free(pad);
                }
                if (client) {
                    vigem_disconnect(client);
                    vigem_free(client);
                }
        #endif
    }

    void injectButton(uint16_t buttonId, bool isPressed) override {
        #if defined(USE_VIGEM)
                if (client && pad) {
                    static constexpr USHORT XUSB_MAP[11] = {
                        XUSB_GAMEPAD_A, XUSB_GAMEPAD_B, XUSB_GAMEPAD_X, XUSB_GAMEPAD_Y,
                        XUSB_GAMEPAD_LEFT_SHOULDER, XUSB_GAMEPAD_RIGHT_SHOULDER,
                        XUSB_GAMEPAD_BACK, XUSB_GAMEPAD_START, XUSB_GAMEPAD_GUIDE,
                        XUSB_GAMEPAD_LEFT_THUMB, XUSB_GAMEPAD_RIGHT_THUMB
                    };
                    if (buttonId < 11) {
                        if (isPressed) report.wButtons |= XUSB_MAP[buttonId];
                        else           report.wButtons &= ~XUSB_MAP[buttonId];
                        vigem_target_x360_update(client, pad, report);
                    }
                    return;
                }
        #endif
            // Fallback nativo: Mapeia Botão 0 (A) -> Espaço, Botão 1 (B) -> Shift, Botão 2 (X) -> 'E'
            switch (buttonId) {
                case 0: sendFallbackKey(VK_SPACE, isPressed); break;
                case 1: sendFallbackKey(VK_SHIFT, isPressed); break;
                case 2: sendFallbackKey('E', isPressed); break;
                case 3: sendFallbackKey('R', isPressed); break;
                default: break;
            }
            std::cout << "[VirtualGamepad Win32] Botao " << buttonId
                    << (isPressed ? " PRESSIONADO" : " SOLTO") << std::endl;
    }

    void injectAxis(uint8_t axisId, int16_t value) override {
        #if defined(USE_VIGEM)
            if (client && pad) {
                switch (axisId) {
                    case 0: report.sThumbLX = value; break;
                    case 1: report.sThumbLY = static_cast<SHORT>(-value - 1); break;
                    case 2: report.sThumbRX = value; break;
                    case 3: report.sThumbRY = static_cast<SHORT>(-value - 1); break;
                    default: break;
                }
                vigem_target_x360_update(client, pad, report);
                return;
            }
        #endif
            std::cout << "[VirtualGamepad Win32] Eixo " << static_cast<int>(axisId) 
            << " Valor: " << value << std::endl;
    }
};

#endif // _WIN32
#endif