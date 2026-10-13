#ifndef UDP_INPUT_DISPATCHER_HPP
#define UDP_INPUT_DISPATCHER_HPP

#include "../../common/Interfaces.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <iostream>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using ClientSocketType = SOCKET;
    #define CLOSE_CLIENT_SOCK(s) closesocket(s)
    #define INVALID_CLIENT_SOCK INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using ClientSocketType = int;
    #define CLOSE_CLIENT_SOCK(s) close(s)
    #define INVALID_CLIENT_SOCK (-1)
#endif

// [SOLID - SRP] Transmite pacotes de Input e escuta pacotes reversos de Force Feedback (Rumble)
class UdpInputDispatcher {
private:
    ClientSocketType sock = INVALID_CLIENT_SOCK;
    struct sockaddr_in serverAddr{};
    std::atomic<bool> running{false};
    std::thread feedbackThread;

    void listenFeedbackLoop(std::function<void(const InputPacket&)> onFeedback) {
        InputPacket pkt{};
        while (running && sock != INVALID_CLIENT_SOCK) {
            int bytes = recvfrom(sock, reinterpret_cast<char*>(&pkt), sizeof(InputPacket), 0, nullptr, nullptr);
            if (bytes == sizeof(InputPacket) && pkt.type == 7 && onFeedback) {
                onFeedback(pkt);
            }
        }
    }

public:
    UdpInputDispatcher(const std::string& hostIp, int port) {
        #if defined(_WIN32)
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        #endif

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_CLIENT_SOCK) {
            std::cerr << "[UdpDispatcher] Erro ao criar socket UDP." << std::endl;
            return;
        }

        #if defined(_WIN32)
            DWORD timeout = 200;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        #else
            struct timeval tv{0, 200000};
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        #endif

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, hostIp.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "[UdpDispatcher] ERRO: Endereco IP invalido (" << hostIp 
                      << "). Usando 127.0.0.1 como fallback." << std::endl;
            inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
        }
    }

    ~UdpInputDispatcher() {
        stopFeedbackListener();
        if (sock != INVALID_CLIENT_SOCK) {
            CLOSE_CLIENT_SOCK(sock);
        }
        #if defined(_WIN32)
            WSACleanup();
        #endif
    }

    void dispatch(const InputPacket& packet) {
        if (sock != INVALID_CLIENT_SOCK) {
            sendto(sock, reinterpret_cast<const char*>(&packet), sizeof(InputPacket), 0,
                   reinterpret_cast<const struct sockaddr*>(&serverAddr), sizeof(serverAddr));
        }
    }

    void startFeedbackListener(std::function<void(const InputPacket&)> onFeedback) {
        running = true;
        feedbackThread = std::thread(&UdpInputDispatcher::listenFeedbackLoop, this, std::move(onFeedback));
    }

    void stopFeedbackListener() {
        running = false;
        if (feedbackThread.joinable()) {
            feedbackThread.join();
        }
    }
};

#endif