#ifndef INPUT_CONTROLLER_MANAGER_HPP
#define INPUT_CONTROLLER_MANAGER_HPP

#include "IInputController.hpp"
#include <vector>
#include <memory>

// [SOLID - OCP / Composite Pattern] Orquestra múltiplos periféricos simultaneamente
class InputControllerManager {
private:
    std::vector<std::unique_ptr<IInputController>> controllers;

public:
    ~InputControllerManager() {
        stopAll();
    }

    void addController(std::unique_ptr<IInputController> controller) {
        controllers.push_back(std::move(controller));
    }

    void startAll(PacketCallback onInput, QuitCallback onQuit) {
        for (auto& ctrl : controllers) {
            ctrl->start(onInput, onQuit);
        }
    }

    void stopAll() {
        for (auto& ctrl : controllers) {
            ctrl->stop();
        }
    }

    void setRumble(uint16_t strongMagnitude, uint16_t weakMagnitude) {
        for (auto& ctrl : controllers) {
            ctrl->setRumble(strongMagnitude, weakMagnitude);
        }
    }
};

#endif


//Adicionar um endOneControler ?? 