#ifndef CLIENT_CONTROLLER_FACTORY_HPP
#define CLIENT_CONTROLLER_FACTORY_HPP

#include "IInputController.hpp"
#include "LinuxKeyboardMouseController.hpp"
#include "LinuxGamepadController.hpp"
#include "WindowsKeyboardMouseController.hpp"
#include "WindowsGamepadController.hpp"
#include <memory>

class ClientControllerFactory {
public:
    static std::unique_ptr<IInputController> createKeyboardMouseController() {
        #if defined(_WIN32)
            return std::make_unique<WindowsKeyboardMouseController>();
        #elif defined(__linux__)
            return std::make_unique<LinuxKeyboardMouseController>();
        #else
            #error "Sistema Operacional nao suportado no Cliente."
        #endif
    }

    static std::unique_ptr<IInputController> createGamepadController() {
        #if defined(_WIN32)
            return std::make_unique<WindowsGamepadController>(0);
        #elif defined(__linux__)
            return std::make_unique<LinuxGamepadController>("/dev/input/js0");
        #else
            #error "Sistema Operacional nao suportado no Cliente."
        #endif
    }
};

#endif