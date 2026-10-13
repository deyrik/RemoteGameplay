#ifndef INPUT_INJECTOR_FACTORY_HPP
#define INPUT_INJECTOR_FACTORY_HPP

#include "../../common/Interfaces.hpp"
#include "LinuxX11Injector.hpp"
#include "WindowsInputInjector.hpp"
#include <memory>

class InputInjectorFactory {
public:
    static std::unique_ptr<IInputInjector> createOSInjector(bool devMode = false) {
        #if defined(_WIN32)
            return std::make_unique<WindowsInputInjector>(devMode);
        #elif defined(__linux__)
            return std::make_unique<LinuxX11Injector>(devMode);
        #else
            #error "Sistema Operacional nao suportado para injecao de input."
        #endif
    }
};

#endif