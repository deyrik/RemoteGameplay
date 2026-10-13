#ifndef LINUX_X11_INJECTOR_HPP
#define LINUX_X11_INJECTOR_HPP

#if defined(__linux__)

#include "../../common/Interfaces.hpp"
#include "LinuxVirtualGamepad.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <iostream>
#include <cctype>

#ifdef DEBUG_INPUT
    #define DBG_LOG(msg) do { std::cout << msg << std::endl; } while(0)
#else
    #define DBG_LOG(msg) do {} while(0)
#endif

class LinuxX11Injector : public IInputInjector {
private:
    Display* display = nullptr;
    LinuxVirtualGamepad virtualGamepad;
    bool devMode = false; // false = Produção (Injeta XTest) | true = Desenvolvedor (Apenas simula)

    static KeySym normalizeToX11KeySym(uint16_t code) {
        switch (code) {
            case 0x08: return XK_BackSpace;
            case 0x09: return XK_Tab;
            case 0x0D: return XK_Return;
            case 0x10:
            case 0xA0: return XK_Shift_L;
            case 0xA1: return XK_Shift_R;
            case 0x11:
            case 0xA2: return XK_Control_L;
            case 0xA3: return XK_Control_R;
            case 0x12:
            case 0xA4: return XK_Alt_L;
            case 0xA5: return XK_Alt_R;
            case 0x14: return XK_Caps_Lock;
            case 0x20: return XK_space;
            case 0x25: return XK_Left;
            case 0x26: return XK_Up;
            case 0x27: return XK_Right;
            case 0x28: return XK_Down;
            case 0x2D: return XK_Insert;
            case 0x2E: return XK_Delete;
            default:
                if (code >= 0x70 && code <= 0x7B) {
                    return XK_F1 + (code - 0x70);
                }
                return static_cast<KeySym>(code);
        }
    }

public:
    explicit LinuxX11Injector(bool isDeveloperMode = false) : devMode(isDeveloperMode) {
        display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "[LinuxInjector] ERRO: Nao foi possivel abrir o Display X11." << std::endl;
        } else {
            std::cout << "[LinuxInjector] Conectado ao X11 | Modo: "
                      << (devMode ? "DEVELOPER (XTest Fisico Desativado)" : "PRODUCAO (XTest Fisico Ativo)") << std::endl;
        }
    }

    ~LinuxX11Injector() override {
        if (display) {
            XCloseDisplay(display);
        }
    }

    void inject(const InputPacket& packet) override {
        if (!display) return;
        switch (packet.type) {
            case 1: { // Teclado (KeyDown / KeyUp real)
                KeySym keysym = normalizeToX11KeySym(packet.key_code);
                KeyCode keycode = XKeysymToKeycode(display, keysym);

                if (keycode != 0) {
                    Bool isPressed = (packet.is_pressed == 1) ? True : False;
                    if (!devMode) {
                        XTestFakeKeyEvent(display, keycode, isPressed, CurrentTime);
                        XFlush(display);
                    }
                    DBG_LOG("[LinuxInjector] Tecla (keysym=" << keysym 
                            << ") " << (isPressed ? "PRESSIONADA" : "SOLTA"));
                }
                break;
            }
            case 2: { // Movimento Relativo de Mouse (Delta X, Delta Y)
                if (!devMode) {
                    XTestFakeRelativeMotionEvent(display, packet.mouse_dx, packet.mouse_dy, CurrentTime);
                    XFlush(display);
                }
                DBG_LOG("[LinuxInjector] Movendo mouse: dx=" << packet.mouse_dx 
                        << ", dy=" << packet.mouse_dy);
                break;
            }
            case 3: { // Clique e Scroll de Mouse (1=Esq, 2=Meio, 3=Dir, 4=ScrollUp, 5=ScrollDown)
                unsigned int button = packet.key_code;
                Bool isPressed = (packet.is_pressed == 1) ? True : False;

                if (button == 4 || button == 5) {
                    if (isPressed) {
                        if (!devMode) {
                            XTestFakeButtonEvent(display, button, True, CurrentTime);
                            XTestFakeButtonEvent(display, button, False, CurrentTime);
                            XFlush(display);
                        }
                        DBG_LOG("[LinuxInjector] Mouse Scroll " << (button == 4 ? "CIMA (UP)" : "BAIXO (DOWN)"));
                    }
                    break;
                }

                if (!devMode) {
                    XTestFakeButtonEvent(display, button, isPressed, CurrentTime);
                    XFlush(display);
                }
                DBG_LOG("[LinuxInjector] Mouse Botao " << button 
                        << (isPressed ? " PRESSIONADO" : " SOLTO"));
                break;
            }
            case 4: { // Botão de Gamepad
                virtualGamepad.injectButton(packet.key_code, packet.is_pressed == 1);
                DBG_LOG("[LinuxInjector] Gamepad Botao " << packet.key_code 
                        << (packet.is_pressed == 1 ? " PRESSIONADO" : " SOLTO"));
                break;
            }
            case 5: { // Eixo Analógico de Gamepad
                virtualGamepad.injectAxis(packet.is_pressed, packet.mouse_dx);
                DBG_LOG("[LinuxInjector] Gamepad Eixo " << static_cast<int>(packet.is_pressed) 
                        << " Valor=" << packet.mouse_dx);
                break;
            }
            case 6: { // Handshake de Perfil de Controle
                auto profile = static_cast<GamepadProfile>(packet.is_pressed);
                uint16_t vendorId = packet.key_code;
                uint16_t productId = static_cast<uint16_t>(packet.mouse_dx);
                virtualGamepad.setProfile(profile, vendorId, productId);
                DBG_LOG("[LinuxInjector] Gamepad Perfil " << static_cast<int>(profile) 
                        << " Vendor=" << vendorId << " Product=" << productId);
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

#endif // __linux__
#endif