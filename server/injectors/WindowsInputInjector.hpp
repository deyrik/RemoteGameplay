#ifndef WINDOWS_INPUT_INJECTOR_HPP
#define WINDOWS_INPUT_INJECTOR_HPP

#if defined(_WIN32)

#include "../../common/Interfaces.hpp"
#include "WindowsVirtualGamepad.hpp"
#include <windows.h>
#include <iostream>

#ifdef DEBUG_INPUT
    #define DBG_LOG(msg) do { std::cout << msg << std::endl; } while(0)
#else
    #define DBG_LOG(msg) do {} while(0)
#endif

class WindowsInputInjector : public IInputInjector {
private:
    WindowsVirtualGamepad virtualGamepad;
    bool liveInjection = true;

    static WORD normalizeToVirtualKey(uint16_t code) {
        if (code >= 'a' && code <= 'z') {
            return static_cast<WORD>(code - ('a' - 'A'));
        }
        switch (code) {
            case 0xFF0D: return VK_RETURN;
            case 0xFF08: return VK_BACK;
            case 0xFF09: return VK_TAB;
            case 0xFFE1:
            case 0xFFE2: return VK_SHIFT;
            case 0xFFE3:
            case 0xFFE4: return VK_CONTROL;
            case 0xFFE9:
            case 0xFFEA: return VK_MENU;
            case 0xFF51: return VK_LEFT;
            case 0xFF52: return VK_UP;
            case 0xFF53: return VK_RIGHT;
            case 0xFF54: return VK_DOWN;
            default:     return static_cast<WORD>(code);
        }
    }

public:
    explicit WindowsInputInjector(bool devMode = false) : liveInjection(!devMode) {}


    void inject(const InputPacket& packet) override {
        switch (packet.type) {
            case 1: { // Teclado (KeyDown / KeyUp real via Hardware ScanCode)
                WORD vk = normalizeToVirtualKey(packet.key_code);
                WORD scanCode = static_cast<WORD>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));

                INPUT input{};
                input.type = INPUT_KEYBOARD;
                input.ki.wScan = scanCode;
                input.ki.dwFlags = KEYEVENTF_SCANCODE | (packet.is_pressed ? 0 : KEYEVENTF_KEYUP);

                if (vk >= VK_LEFT && vk <= VK_DOWN) {
                    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                }

                if (liveInjection) SendInput(1, &input, sizeof(INPUT));
                DBG_LOG("[WinInjector] Tecla (VK=" << vk << ", Scan=" << scanCode << ") "
                        << (packet.is_pressed ? "PRESSIONADA" : "SOLTA"));
                break;
            }
            case 2: { // Movimento Relativo de Mouse (Delta X, Delta Y)
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dx = packet.mouse_dx;
                input.mi.dy = packet.mouse_dy;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                if (liveInjection) SendInput(1, &input, sizeof(INPUT));
                DBG_LOG("[WinInjector] Movendo mouse: dx=" << packet.mouse_dx << ", dy=" << packet.mouse_dy);
                break;
            }
            case 3: { // Clique e Scroll de Mouse (1=Esq, 2=Meio, 3=Dir, 4=ScrollUp, 5=ScrollDown)
                INPUT input{};
                input.type = INPUT_MOUSE;
                bool pressed = (packet.is_pressed == 1);

                if (packet.key_code == 4 || packet.key_code == 5) {
                    if (pressed) {
                        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                        input.mi.mouseData = static_cast<DWORD>((packet.key_code == 4) ? WHEEL_DELTA : -WHEEL_DELTA);
                        if (liveInjection) SendInput(1, &input, sizeof(INPUT));
                        DBG_LOG("[WinInjector] Mouse Scroll " << (packet.key_code == 4 ? "CIMA (UP)" : "BAIXO (DOWN)"));
                    }
                    break;
                }

                if (packet.key_code == 1) {
                    input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                } else if (packet.key_code == 2) {
                    input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                } else if (packet.key_code == 3) {
                    input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                }

                if (input.mi.dwFlags != 0) {
                    if (liveInjection) SendInput(1, &input, sizeof(INPUT));
                    DBG_LOG("[WinInjector] Mouse Botao " << packet.key_code
                            << (pressed ? " PRESSIONADO" : " SOLTO"));
                }
                break;
            }
            case 4: { // Botão de Gamepad
                virtualGamepad.injectButton(packet.key_code, packet.is_pressed == 1);
                DBG_LOG("[WinInjector] Gamepad Botao " << packet.key_code
                        << (packet.is_pressed == 1 ? " PRESSIONADO" : " SOLTO"));
                break;
            }
            case 5: { // Eixo Analógico de Gamepad
                virtualGamepad.injectAxis(packet.is_pressed, packet.mouse_dx);
                DBG_LOG("[WinInjector] Gamepad Eixo " << static_cast<int>(packet.is_pressed)
                        << " Valor=" << packet.mouse_dx);
                break;
            }
            case 6: { // Handshake de Perfil de Controle
                auto profile = static_cast<GamepadProfile>(packet.is_pressed);
                uint16_t vendorId = packet.key_code;
                uint16_t productId = static_cast<uint16_t>(packet.mouse_dx);
                virtualGamepad.setProfile(profile, vendorId, productId);
                DBG_LOG("[WinInjector] Gamepad Perfil " << static_cast<int>(profile));
                break;
            }
            default:
                break;
        }
    }

    void setFeedbackCallback(FeedbackCallback cb) override {
        virtualGamepad.setRumbleCallback([cb](uint16_t strongMag, uint16_t weakMag) {
            if (cb) {
                InputPacket pkt{};
                pkt.type = 7;
                pkt.key_code = strongMag;
                pkt.mouse_dx = static_cast<int16_t>(weakMag);
                cb(pkt);
            }
        });
    }
};

#endif // _WIN32
#endif