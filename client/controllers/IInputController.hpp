#ifndef IINPUT_CONTROLLER_HPP
#define IINPUT_CONTROLLER_HPP

#include "../../common/Interfaces.hpp"
#include <functional>
#include <cstdint>

using PacketCallback = std::function<void(const InputPacket&)>;
using QuitCallback   = std::function<void()>;

// [SOLID - ISP / OCP] Contrato único para qualquer dispositivo de entrada no Cliente
class IInputController {
public:
    virtual ~IInputController() = default;
    virtual void start(PacketCallback onInputEvent, QuitCallback onQuitEvent) = 0;
    virtual void stop() = 0;

    // Retorna o handle nativo da janela (Window no Linux X11 ou HWND no Windows)
    // Controladores sem janela (como Gamepads) simplesmente herdam o retorno 0.
    virtual uintptr_t getWindowHandle() const { return 0; }

    // Aciona os motores de vibração físicos no controle do Cliente
    virtual void setRumble(uint16_t /*strongMagnitude*/, uint16_t /*weakMagnitude*/) {}
};

#endif