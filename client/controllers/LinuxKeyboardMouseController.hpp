#ifndef KEYBOARD_MOUSE_CONTROLLER_HPP
#define KEYBOARD_MOUSE_CONTROLLER_HPP

#if defined(__linux__)

#include "IInputController.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

class LinuxKeyboardMouseController : public IInputController {
private:
    Display* display = nullptr;
    Window win = 0;
    Cursor invisibleCursor = 0;
    int xiOpcode = 0;
    int winWidth = 1280;
    int winHeight = 720;
    bool isMouseLocked = false;

    std::atomic<bool> running{false};
    std::thread workerThread;

    void createInvisibleCursor() {
        Pixmap bmNoData;
        XColor black{};
        static char noData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        bmNoData = XCreateBitmapFromData(display, win, noData, 8, 8);
        invisibleCursor = XCreatePixmapCursor(display, bmNoData, bmNoData, &black, &black, 0, 0);
        XFreePixmap(display, bmNoData);
    }

    void setupRawMouseInput() {
        int event, error;
        if (!XQueryExtension(display, "XInputExtension", &xiOpcode, &event, &error)) {
            std::cerr << "[KeyboardMouse] XInput2 nao disponivel." << std::endl;
            return;
        }

        // OBRIGATÓRIO: Negocia a versão 2.0+ para o XServer liberar o XI_RawMotion
        int major = 2, minor = 0;
        if (XIQueryVersion(display, &major, &minor) != Success) {
            std::cerr << "[KeyboardMouse] Servidor X11 nao suporta XInput 2.0." << std::endl;
            return;
        }

        XIEventMask evmask;
        unsigned char mask[(XI_LASTEVENT + 7) / 8] = { 0 };
        evmask.deviceid = XIAllMasterDevices;
        evmask.mask_len = sizeof(mask);
        evmask.mask = mask;
        XISetMask(mask, XI_RawMotion);
        XISelectEvents(display, DefaultRootWindow(display), &evmask, 1);
    }

    void setMouseLock(bool lock) {
        isMouseLocked = lock;
        if (lock) {
            // Oculta o cursor e o posiciona no centro sem usar XGrabPointer (para não bloquear o XI_RawMotion)
            XDefineCursor(display, win, invisibleCursor);
            XWarpPointer(display, None, win, 0, 0, 0, 0, winWidth / 2, winHeight / 2);
            XStoreName(display, win, "StreamGamer [MOUSE TRAVADO (RAW SENSOR) - F1 para soltar | ESC para sair]");
            std::cout << "[KeyboardMouse] Mouse TRAVADO (Leitura direta do sensor optico ativa)." << std::endl;
        } else {
            XUndefineCursor(display, win);
            XStoreName(display, win, "StreamGamer [Pressione F1 para travar o Mouse | ESC para sair]");
            std::cout << "[KeyboardMouse] Mouse DESTRAVADO." << std::endl;
        }
        XFlush(display);
    }

    void captureLoop(PacketCallback onInput, QuitCallback onQuit) {
        int lastX = -1, lastY = -1;

        while (running) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);

                // 1. MODO TRAVADO (F1): Lê direto do sensor óptico físico (XI_RawMotion)
                if (event.xcookie.type == GenericEvent && event.xcookie.extension == xiOpcode &&
                    XGetEventData(display, &event.xcookie)) {
                    if (event.xcookie.evtype == XI_RawMotion && isMouseLocked) {
                        XIRawEvent* raw = static_cast<XIRawEvent*>(event.xcookie.data);
                        double* rawVal = raw->raw_values;
                        int dx = 0, dy = 0;

                        if (XIMaskIsSet(raw->valuators.mask, 0)) {
                            dx = static_cast<int>(*rawVal);
                            rawVal++;
                        }
                        if (XIMaskIsSet(raw->valuators.mask, 1)) {
                            dy = static_cast<int>(*rawVal);
                        }

                        if (dx != 0 || dy != 0) {
                            InputPacket pkt{};
                            pkt.type = 2;
                            pkt.mouse_dx = static_cast<int16_t>(dx);
                            pkt.mouse_dy = static_cast<int16_t>(dy);
                            onInput(pkt);

                            // Mantém o ponteiro invisível no centro da janela (não gera XI_RawMotion!)
                            XWarpPointer(display, None, win, 0, 0, 0, 0, winWidth / 2, winHeight / 2);
                            XFlush(display);
                        }
                    }
                    XFreeEventData(display, &event.xcookie);
                    continue;
                }

                switch (event.type) {
                    case ConfigureNotify: {
                        winWidth = event.xconfigure.width;
                        winHeight = event.xconfigure.height;
                        break;
                    }
                    case MotionNotify: {
                        // 2. MODO DESTRAVADO: Envia movimentos normais quando o mouse passa sobre a janela
                        if (!isMouseLocked) {
                            if (lastX != -1 && lastY != -1) {
                                int dx = event.xmotion.x - lastX;
                                int dy = event.xmotion.y - lastY;
                                if (dx != 0 || dy != 0) {
                                    InputPacket pkt{};
                                    pkt.type = 2;
                                    pkt.mouse_dx = static_cast<int16_t>(dx);
                                    pkt.mouse_dy = static_cast<int16_t>(dy);
                                    onInput(pkt);
                                }
                            }
                            lastX = event.xmotion.x;
                            lastY = event.xmotion.y;
                        }
                        break;
                    }
                    case KeyPress:
                    case KeyRelease: {
                        KeySym keysym = XLookupKeysym(&event.xkey, 0);

                        if (keysym == XK_F1 && event.type == KeyPress) {
                            setMouseLock(!isMouseLocked);
                            break;
                        }

                        if (keysym == XK_Escape && event.type == KeyPress) {
                            if (isMouseLocked) setMouseLock(false);
                            std::cout << "[KeyboardMouse] ESC pressionado. Encerrando cliente..." << std::endl;
                            if (onQuit) onQuit();
                            running = false;
                            break;
                        }

                        InputPacket pkt{};
                        pkt.type = 1;
                        pkt.is_pressed = (event.type == KeyPress) ? 1 : 0;
                        pkt.key_code = static_cast<uint16_t>(keysym);
                        onInput(pkt);
                        break;
                    }
                    case ButtonPress:
                    case ButtonRelease: {
                        InputPacket pkt{};
                        pkt.type = 3;
                        pkt.is_pressed = (event.type == ButtonPress) ? 1 : 0;
                        pkt.key_code = static_cast<uint16_t>(event.xbutton.button);
                        onInput(pkt);
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 1000Hz polling
        }
    }

public:
    LinuxKeyboardMouseController() {
        XInitThreads();
        display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "[KeyboardMouse] Erro ao abrir Display X11." << std::endl;
            return;
        }

        Bool supported = False;
        XkbSetDetectableAutoRepeat(display, True, &supported);

        int screen = DefaultScreen(display);
        win = XCreateSimpleWindow(
            display, RootWindow(display, screen),
            50, 50, winWidth, winHeight, 1,
            BlackPixel(display, screen), BlackPixel(display, screen)
        );

        XStoreName(display, win, "StreamGamer [Pressione F1 para travar o Mouse | ESC para sair]");
        XSelectInput(display, win, KeyPressMask | KeyReleaseMask |
                                   ButtonPressMask | ButtonReleaseMask |
                                   PointerMotionMask | StructureNotifyMask);
        XMapWindow(display, win);
        createInvisibleCursor();
        setupRawMouseInput();
        XFlush(display);
    }

    ~LinuxKeyboardMouseController() override {
        stop();
        if (display) {
            if (isMouseLocked) XUndefineCursor(display, win);
            if (invisibleCursor) XFreeCursor(display, invisibleCursor);
            if (win) XDestroyWindow(display, win);
            XCloseDisplay(display);
        }
    }

    uintptr_t getWindowHandle() const override {
        return static_cast<uintptr_t>(win);
    }

    void start(PacketCallback onInputEvent, QuitCallback onQuitEvent) override {
        running = true;
        workerThread = std::thread(&LinuxKeyboardMouseController::captureLoop, this, onInputEvent, onQuitEvent);
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