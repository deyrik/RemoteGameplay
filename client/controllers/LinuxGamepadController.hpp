#ifndef LINUX_GAMEPAD_CONTROLLER_HPP
#define LINUX_GAMEPAD_CONTROLLER_HPP

#if defined(__linux__)

#include "IInputController.hpp"
#include "../../server/injectors/IVirtualGamepad.hpp"
#include <linux/joystick.h>
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iostream>
#include <string>

class LinuxGamepadController : public IInputController {
private:
    std::string devicePath;
    std::atomic<bool> running{false};
    std::thread workerThread;

    int ffFd = -1;
    int ffEffectId = -1;
    std::mutex ffMutex;

    static uint16_t readSysfsHexId(int jsIndex, const std::string& field) {
        std::string path = "/sys/class/input/js" + std::to_string(jsIndex) + "/device/id/" + field;
        std::ifstream file(path);
        std::string hexStr;
        if (file >> hexStr) {
            return static_cast<uint16_t>(std::stoul(hexStr, nullptr, 16));
        }
        return 0;
    }

    // Descobre o arquivo /dev/input/eventX associado ao jsIndex para enviar Force Feedback
    void openForceFeedbackDevice(int jsIndex) {
        std::lock_guard<std::mutex> lock(ffMutex);
        closeForceFeedbackDeviceLocked();

        std::string sysDir = "/sys/class/input/js" + std::to_string(jsIndex) + "/device";
        DIR* dir = opendir(sysDir.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::strncmp(entry->d_name, "event", 5) == 0) {
                std::string evPath = "/dev/input/" + std::string(entry->d_name);
                ffFd = open(evPath.c_str(), O_RDWR | O_NONBLOCK);
                if (ffFd >= 0) {
                    std::cout << "[Gamepad] Canal de Vibracao (Rumble) ativo em " << evPath << "!" << std::endl;
                } else {
                    std::cout << "[Gamepad] Aviso: Sem permissao de escrita em " << evPath 
                              << " para vibracao (rode: sudo chmod 666 /dev/input/event*)." << std::endl;
                }
                break;
            }
        }
        closedir(dir);
    }

    void closeForceFeedbackDeviceLocked() {
        if (ffFd >= 0) {
            if (ffEffectId != -1) {
                ioctl(ffFd, EVIOCRMFF, ffEffectId);
                ffEffectId = -1;
            }
            close(ffFd);
            ffFd = -1;
        }
    }

    static GamepadProfile detectProfile(const std::string& name, uint16_t vendor, uint16_t product) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (vendor == 0x054c || lower.find("playstation") != std::string::npos || lower.find("sony") != std::string::npos) {
            if (product == 0x0ce6 || product == 0x0df2 || lower.find("dualsense") != std::string::npos || lower.find("ps5") != std::string::npos) {
                return GamepadProfile::PS5_DUALSENSE;
            }
            return GamepadProfile::PS4_DUALSHOCK;
        }
        if (vendor == 0x045e || lower.find("xbox") != std::string::npos || lower.find("x-box") != std::string::npos) {
            if (product == 0x028e || lower.find("360") != std::string::npos) {
                return GamepadProfile::XBOX_360;
            }
            return GamepadProfile::XBOX_ONE_SERIES;
        }
        return GamepadProfile::GENERIC_USB;
    }

    void sendProfilePacket(PacketCallback& onInput, GamepadProfile profile, uint16_t vendor = 0, uint16_t product = 0) {
        InputPacket pkt{};
        pkt.type = 6;
        pkt.is_pressed = static_cast<uint8_t>(profile);
        pkt.key_code = vendor;
        pkt.mouse_dx = static_cast<int16_t>(product);
        onInput(pkt);
    }

    void readLoop(PacketCallback onInput) {
        int fd = -1;
        bool waitingMessageShown = false;

        while (running) {
            if (fd < 0) {
                for (int idx = 0; idx < 4; ++idx) {
                    std::string candidate = "/dev/input/js" + std::to_string(idx);
                    int testFd = open(candidate.c_str(), O_RDONLY | O_NONBLOCK);
                    if (testFd >= 0) {
                        char name[128] = "Desconhecido";
                        ioctl(testFd, JSIOCGNAME(sizeof(name)), name);

                        if (std::string(name).find("StreamGamer Virtual") != std::string::npos) {
                            close(testFd);
                            continue;
                        }

                        uint16_t vendor  = readSysfsHexId(idx, "vendor");
                        uint16_t product = readSysfsHexId(idx, "product");
                        GamepadProfile profile = detectProfile(name, vendor, product);

                        fd = testFd;
                        devicePath = candidate;
                        waitingMessageShown = false;

                        std::cout << "[Gamepad] Controle '" << name << "' [0x"
                                  << std::hex << vendor << ":0x" << product << std::dec
                                  << "] detectado em " << devicePath << " (Perfil="
                                  << static_cast<int>(profile) << ")" << std::endl;

                        openForceFeedbackDevice(idx);
                        sendProfilePacket(onInput, profile, vendor, product);
                        break;
                    }
                }

                if (fd < 0) {
                    if (!waitingMessageShown) {
                        std::cout << "[Gamepad] Aguardando controle fisico (Hot-Plug ativo)..." << std::endl;
                        waitingMessageShown = true;
                    }
                    for (int i = 0; i < 5 && running; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
            }

            struct pollfd pfd{fd, POLLIN, 0};
            int pollResult = poll(&pfd, 1, 100);

            if (pollResult < 0) {
                if (errno == EINTR) continue;
                sendProfilePacket(onInput, GamepadProfile::DISCONNECTED);
                { std::lock_guard<std::mutex> lock(ffMutex); closeForceFeedbackDeviceLocked(); }
                close(fd);
                fd = -1;
                continue;
            }

            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                std::cout << "[Gamepad] Controle desconectado de " << devicePath << "." << std::endl;
                sendProfilePacket(onInput, GamepadProfile::DISCONNECTED);
                { std::lock_guard<std::mutex> lock(ffMutex); closeForceFeedbackDeviceLocked(); }
                close(fd);
                fd = -1;
                continue;
            }

            if (pollResult > 0 && (pfd.revents & POLLIN)) {
                struct js_event event{};
                ssize_t bytes = read(fd, &event, sizeof(event));

                if (bytes == sizeof(event)) {
                    uint8_t eventType = event.type & ~JS_EVENT_INIT;
                    InputPacket pkt{};
                    if (eventType == JS_EVENT_BUTTON) {
                        pkt.type = 4;
                        pkt.is_pressed = event.value ? 1 : 0;
                        pkt.key_code = event.number;
                        onInput(pkt);
                    } else if (eventType == JS_EVENT_AXIS) {
                        pkt.type = 5;
                        pkt.is_pressed = event.number;
                        pkt.mouse_dx = event.value;
                        onInput(pkt);
                    }
                } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    sendProfilePacket(onInput, GamepadProfile::DISCONNECTED);
                    { std::lock_guard<std::mutex> lock(ffMutex); closeForceFeedbackDeviceLocked(); }
                    close(fd);
                    fd = -1;
                }
            }
        }

        if (fd >= 0) {
            sendProfilePacket(onInput, GamepadProfile::DISCONNECTED);
            { std::lock_guard<std::mutex> lock(ffMutex); closeForceFeedbackDeviceLocked(); }
            close(fd);
        }
    }

public:
    explicit LinuxGamepadController(std::string path = "/dev/input/js0")
        : devicePath(std::move(path)) {}

    ~LinuxGamepadController() override { stop(); }

    void setRumble(uint16_t strongMagnitude, uint16_t weakMagnitude) override {
        std::lock_guard<std::mutex> lock(ffMutex);
        std::cout << "[Gamepad Cliente] Vibrando controle! Forte=" << strongMagnitude
                  << ", Fraco=" << weakMagnitude << std::endl;

        if (ffFd < 0) return;

        struct ff_effect effect{};
        effect.type = FF_RUMBLE;
        effect.id = static_cast<int16_t>(ffEffectId);
        effect.u.rumble.strong_magnitude = strongMagnitude;
        effect.u.rumble.weak_magnitude   = weakMagnitude;
        effect.replay.length = 5000;
        effect.replay.delay  = 0;

        if (ioctl(ffFd, EVIOCSFF, &effect) < 0) return;
        ffEffectId = effect.id;

        struct input_event play{};
        play.type  = EV_FF;
        play.code  = static_cast<uint16_t>(ffEffectId);
        play.value = (strongMagnitude > 0 || weakMagnitude > 0) ? 1 : 0;
        if (write(ffFd, &play, sizeof(play)) < 0) {}
    }

    void start(PacketCallback onInputEvent, QuitCallback /*onQuitEvent*/) override {
        running = true;
        workerThread = std::thread(&LinuxGamepadController::readLoop, this, onInputEvent);
    }

    void stop() override {
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
};

#endif // __linux__
#endif