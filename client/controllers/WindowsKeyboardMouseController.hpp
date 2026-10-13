#ifndef WINDOWS_KEYBOARD_MOUSE_CONTROLLER_HPP
#define WINDOWS_KEYBOARD_MOUSE_CONTROLLER_HPP

#if defined(_WIN32)

#include "IInputController.hpp"
#include <windows.h>
#include <thread>
#include <atomic>
#include <iostream>

class WindowsKeyboardMouseController : public IInputController {
private:
    HWND hwnd = nullptr;
    bool isMouseLocked = false;
    std::atomic<bool> running{false};
    std::thread workerThread;

    PacketCallback onInputCallback;
    QuitCallback onQuitCallback;

    void setMouseLock(bool lock) {
        isMouseLocked = lock;
        if (lock) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            POINT ul = { rect.left, rect.top };
            POINT lr = { rect.right, rect.bottom };
            ClientToScreen(hwnd, &ul);
            ClientToScreen(hwnd, &lr);
            rect = { ul.x, ul.y, lr.x, lr.y };

            ClipCursor(&rect); // Confina o mouse fisicamente dentro da janela
            ShowCursor(FALSE); // Oculta o ponteiro
            SetWindowTextA(hwnd, "StreamGamer [MOUSE TRAVADO (RAW SENSOR) - F1 para soltar | ESC para sair]");
        } else {
            ClipCursor(NULL);
            ShowCursor(TRUE);
            SetWindowTextA(hwnd, "StreamGamer [Pressione F1 para travar o Mouse | ESC para sair]");
        }
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<WindowsKeyboardMouseController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProc(hwnd, uMsg, wParam, lParam);

        switch (uMsg) {
            case WM_KEYDOWN:
            case WM_KEYUP: {
                bool isPressed = (uMsg == WM_KEYDOWN);
                // No Windows, ignora auto-repeat se a tecla já estava pressionada (bit 30 de lParam)
                if (isPressed && (lParam & (1 << 30))) return 0;

                if (wParam == VK_F1 && isPressed) {
                    self->setMouseLock(!self->isMouseLocked);
                    return 0;
                }
                if (wParam == VK_ESCAPE && isPressed) {
                    if (self->isMouseLocked) self->setMouseLock(false);
                    if (self->onQuitCallback) self->onQuitCallback();
                    self->running = false;
                    return 0;
                }

                InputPacket pkt{};
                pkt.type = 1;
                pkt.is_pressed = isPressed ? 1 : 0;
                pkt.key_code = static_cast<uint16_t>(wParam);
                if (self->onInputCallback) self->onInputCallback(pkt);
                return 0;
            }
            case WM_INPUT: {
                UINT dwSize = sizeof(RAWINPUT);
                BYTE lpb[sizeof(RAWINPUT)];
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb);
                    if (raw->header.dwType == RIM_TYPEMOUSE) {
                        int dx = raw->data.mouse.lLastX;
                        int dy = raw->data.mouse.lLastY;
                        if (dx != 0 || dy != 0) {
                            InputPacket pkt{};
                            pkt.type = 2;
                            pkt.mouse_dx = static_cast<int16_t>(dx);
                            pkt.mouse_dy = static_cast<int16_t>(dy);
                            if (self->onInputCallback) self->onInputCallback(pkt);
                        }
                    }
                }
                return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP: {
                InputPacket pkt{};
                pkt.type = 3;
                pkt.is_pressed = (uMsg == WM_LBUTTONDOWN || uMsg == WM_MBUTTONDOWN || uMsg == WM_RBUTTONDOWN) ? 1 : 0;
                if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP)      pkt.key_code = 1;
                else if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP) pkt.key_code = 2;
                else                                                     pkt.key_code = 3;
                if (self->onInputCallback) self->onInputCallback(pkt);
                return 0;
            }
            case WM_MOUSEWHEEL: {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta != 0) {
                    InputPacket pkt{};
                    pkt.type = 3;
                    pkt.is_pressed = 1;
                    pkt.key_code = (delta > 0) ? 4 : 5; // 4 = Scroll Up, 5 = Scroll Down
                    if (self->onInputCallback) self->onInputCallback(pkt);
                }
                return 0;
            }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void messageLoop() {
        MSG msg{};
        while (running) {
            while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

public:
    WindowsKeyboardMouseController() {
        HINSTANCE hInstance = GetModuleHandle(NULL);
        WNDCLASSA wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "StreamGamerClientClass";
        RegisterClassA(&wc);

        hwnd = CreateWindowExA(
            0, wc.lpszClassName,
            "StreamGamer [Pressione F1 para travar o Mouse | ESC para sair]",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
            NULL, NULL, hInstance, NULL
        );

        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // Registra leitura direta do sensor óptico do mouse (Raw Input)
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // Generic Desktop Controls
        rid.usUsage = 0x02;     // Mouse
        rid.dwFlags = 0;
        rid.hwndTarget = hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }

    ~WindowsKeyboardMouseController() override {
        stop();
        if (isMouseLocked) setMouseLock(false);
        if (hwnd) DestroyWindow(hwnd);
    }

    uintptr_t getWindowHandle() const override {
        return reinterpret_cast<uintptr_t>(hwnd);
    }

    void start(PacketCallback onInputEvent, QuitCallback onQuitEvent) override {
        onInputCallback = std::move(onInputEvent);
        onQuitCallback = std::move(onQuitEvent);
        running = true;
        workerThread = std::thread(&WindowsKeyboardMouseController::messageLoop, this);
    }

    void stop() override {
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
};

#endif // _WIN32
#endif