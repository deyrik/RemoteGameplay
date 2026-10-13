#ifndef WINDOWS_GAMEPAD_CONTROLLER_HPP
#define WINDOWS_GAMEPAD_CONTROLLER_HPP

#if defined(_WIN32)

#include "IInputController.hpp"
#include <windows.h>
#include <xinput.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

class WindowsGamepadController : public IInputController {
private:
    DWORD controllerIndex;
    std::atomic<bool> running{false};
    std::thread workerThread;

    void readLoop(PacketCallback onInput) {
        XINPUT_STATE prevState{};
        bool connectedMsgShown = false;

        while (running) {
            XINPUT_STATE state{};
            if (XInputGetState(controllerIndex, &state) == ERROR_SUCCESS) {
                if (!connectedMsgShown) {
                    std::cout << "[Gamepad Win32] Controle XInput #" << controllerIndex << " conectado!" << std::endl;
                    connectedMsgShown = true;
                }

                // Só processa se houve mudança física no controle desde a última leitura
                if (state.dwPacketNumber != prevState.dwPacketNumber) {
                    WORD changedButtons = state.Gamepad.wButtons ^ prevState.Gamepad.wButtons;
                    if (changedButtons != 0) {
                        for (int bit = 0; bit < 16; ++bit) {
                            WORD mask = (1 << bit);
                            if (changedButtons & mask) {
                                InputPacket pkt{};
                                pkt.type = 4; // Botão de Gamepad
                                pkt.is_pressed = (state.Gamepad.wButtons & mask) ? 1 : 0;
                                pkt.key_code = static_cast<uint16_t>(bit);
                                onInput(pkt);
                            }
                        }
                    }

                    // Eixos Analógicos (LX = 0, LY = 1, RX = 2, RY = 3)
                    auto checkAxis = [&](uint8_t axisId, SHORT curr, SHORT prev) {
                        if (curr != prev) {
                            InputPacket pkt{};
                            pkt.type = 5;
                            pkt.is_pressed = axisId;
                            pkt.mouse_dx = static_cast<int16_t>(curr);
                            onInput(pkt);
                        }
                    };

                    checkAxis(0, state.Gamepad.sThumbLX, prevState.Gamepad.sThumbLX);
                    checkAxis(1, state.Gamepad.sThumbLY, prevState.Gamepad.sThumbLY);
                    checkAxis(2, state.Gamepad.sThumbRX, prevState.Gamepad.sThumbRX);
                    checkAxis(3, state.Gamepad.sThumbRY, prevState.Gamepad.sThumbRY);

                    prevState = state;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4)); // 250Hz polling
        }
    }

public:
    explicit WindowsGamepadController(DWORD index = 0) : controllerIndex(index) {}

    ~WindowsGamepadController() override { stop(); }

    void start(PacketCallback onInputEvent, QuitCallback /*onQuitEvent*/) override {
        running = true;
        workerThread = std::thread(&WindowsGamepadController::readLoop, this, onInputEvent);
    }

    void stop() override {
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void setRumble(uint16_t strongMagnitude, uint16_t weakMagnitude) override {
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed  = strongMagnitude;
        vibration.wRightMotorSpeed = weakMagnitude;
        XInputSetState(controllerIndex, &vibration);
    }
};

#endif // _WIN32
#endif