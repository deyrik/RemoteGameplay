#include "../common/Interfaces.hpp"
#include "injectors/InputInjectorFactory.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdio>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketType = SOCKET;
    #define CLOSE_SOCKET(s) closesocket(s)
    #define INVALID_SOCK INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    using SocketType = int;
    #define CLOSE_SOCKET(s) close(s)
    #define INVALID_SOCK (-1)
#endif

// [SOLID - OCP] Estratégia 1: Captura de Desktop com Auto-Detecção de GPU / iGPU / CPU
class DesktopScreenPipelineBuilder : public IMediaPipelineBuilder {
private:
    std::string clientIp;
    int videoPort;
    int audioPort;

    // Testa fisicamente no driver do SO se o codificador de hardware existe e abre com sucesso
    static bool isHardwareEncoderAvailable(const char* elementName) {
        GstElement* elem = gst_element_factory_make(elementName, nullptr);
        if (!elem) return false;

        // GST_STATE_READY força o plugin a consultar o hardware real (GPU/iGPU)
        GstStateChangeReturn ret = gst_element_set_state(elem, GST_STATE_READY);
        gst_element_set_state(elem, GST_STATE_NULL);
        gst_object_unref(elem);

        return (ret == GST_STATE_CHANGE_SUCCESS);
    }

    // Seleciona automaticamente: 1º GPU Dedicada -> 2º Vídeo Integrado (iGPU) -> 3º CPU (Fallback)
    // Todos forçados em constrained-baseline + byte-stream para compatibilidade total com WebRTC
    static std::string selectBestVideoEncoder() {
        const std::string h264WebRtcCaps = 
            " ! video/x-h264,profile=constrained-baseline,stream-format=byte-stream ! h264parse config-interval=-1";

        #if defined(_WIN32)
            if (isHardwareEncoderAvailable("nvh264enc")) {
                std::cout << "[Encoder] GPU NVIDIA detectada! Usando aceleracao de hardware (NVENC)." << std::endl;
                return "videoconvert ! nvh264enc bitrate=4000 preset=low-latency-hq zerolatency=true gop-size=30 bframes=0" + h264WebRtcCaps;
            }
            if (isHardwareEncoderAvailable("amfh264enc")) {
                std::cout << "[Encoder] GPU AMD detectada! Usando aceleracao de hardware (AMF)." << std::endl;
                return "videoconvert ! amfh264enc bitrate=4000 usage=ultra-low-latency gop-size=30" + h264WebRtcCaps;
            }
            if (isHardwareEncoderAvailable("qsvh264enc")) {
                std::cout << "[Encoder] GPU/iGPU Intel detectada! Usando aceleracao de hardware (QuickSync)." << std::endl;
                return "videoconvert ! qsvh264enc bitrate=4000 low-latency=true gop-size=30 bframes=0" + h264WebRtcCaps;
            }
            if (isHardwareEncoderAvailable("vah264enc")) {
                std::cout << "[Encoder] GPU/iGPU AMD ou Intel detectada! Usando aceleracao VA-API moderna (vah264enc)." << std::endl;
                return "videoconvert ! vah264enc bitrate=4000 key-int-max=30 b-frames=0 aud=false" + h264WebRtcCaps;
            }
        #else
            if (isHardwareEncoderAvailable("nvh264enc")) {
                std::cout << "[Encoder] GPU NVIDIA detectada no Linux! Usando aceleracao de hardware (NVENC)." << std::endl;
                return "videoconvert ! nvh264enc bitrate=4000 preset=low-latency-hq zerolatency=true gop-size=30 bframes=0" + h264WebRtcCaps;
            }
            if (isHardwareEncoderAvailable("vah264enc")) {
                std::cout << "[Encoder] GPU/iGPU AMD ou Intel detectada! Usando aceleracao VA-API moderna (vah264enc WebRTC)." << std::endl;
                return "videoconvert ! vah264enc bitrate=4000 key-int-max=30 b-frames=0 aud=false" + h264WebRtcCaps;
            }
            if (isHardwareEncoderAvailable("vaapih264enc")) {
                std::cout << "[Encoder] GPU/iGPU AMD ou Intel detectada! Usando aceleracao VA-API (vaapih264enc)." << std::endl;
                return "videoconvert ! vaapih264enc rate-control=cbr bitrate=4000 keyframe-period=30 max-bframes=0" + h264WebRtcCaps;
            }
        #endif

        std::cout << "[Encoder] Nenhum acelerador de hardware ativo. Usando CPU (x264enc ultrafast)." << std::endl;
        return "videoconvert ! video/x-raw,format=I420,framerate=30/1 ! "
               "x264enc bitrate=4000 tune=zerolatency speed-preset=ultrafast key-int-max=30 bframes=0" + h264WebRtcCaps;
    }

    #if !defined(_WIN32)
        // Descobre automaticamente o canal "Monitor" (som interno do sistema) no Linux
        static std::string getLinuxSystemAudioMonitor() {
            char buffer[256];
            std::string sinkName;
            FILE* pipe = popen("pactl get-default-sink 2>/dev/null", "r");
            if (pipe) {
                if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    sinkName = buffer;
                    if (!sinkName.empty() && sinkName.back() == '\n') {
                        sinkName.pop_back();
                    }
                }
                pclose(pipe);
            }
            return sinkName.empty() ? "" : "device=\"" + sinkName + ".monitor\" ";
        }
    #endif

public:
    DesktopScreenPipelineBuilder(std::string ip, int vPort, int aPort)
        : clientIp(std::move(ip)), videoPort(vPort), audioPort(aPort) {}

    std::string buildPipelineDescription() const override {
        std::string videoEncoder = selectBestVideoEncoder();

        #if defined(_WIN32)
            return "d3d11screencapturesrc ! queue max-size-buffers=1 leaky=downstream ! "
                   "videoscale ! video/x-raw,width=1280,height=720,framerate=30/1 ! " +
                   videoEncoder + " ! "
                   "rtph264pay config-interval=-1 pt=96 ! "
                   "udpsink host=" + clientIp + " port=" + std::to_string(videoPort) + " sync=false async=false "
                   
                   "wasapisrc loopback=true low-latency=true ! queue max-size-buffers=2 leaky=downstream ! "
                   "audioconvert ! audioresample ! "
                   "opusenc frame-size=10 inband-fec=true packet-loss-percentage=10 bitrate=96000 ! "
                   "rtpopuspay pt=96 ! "
                   "udpsink host=" + clientIp + " port=" + std::to_string(audioPort) + " sync=false async=false";
        #else
            return "ximagesrc use-damage=0 ! queue max-size-buffers=1 leaky=downstream ! "
                   "videoscale ! video/x-raw,width=1280,height=720,framerate=30/1 ! " +
                   videoEncoder + " ! "
                   "rtph264pay config-interval=-1 mtu=1200 pt=96 ! "
                   "udpsink host=" + clientIp + " port=" + std::to_string(videoPort) + " sync=false async=false "
                   
                   "pulsesrc " + getLinuxSystemAudioMonitor() +
                   "buffer-time=20000 latency-time=5000 ! queue max-size-buffers=2 leaky=downstream ! "
                   "audioconvert ! audioresample ! "
                   "opusenc frame-size=10 inband-fec=true packet-loss-percentage=10 bitrate=96000 ! "
                   "rtpopuspay pt=96 ! "
                   "udpsink host=" + clientIp + " port=" + std::to_string(audioPort) + " sync=false async=false";
        #endif
    }
};

// [SOLID - SRP / DIP] Escuta comandos via UDP e envia Feedback (Rumble) pelo mesmo socket
class UdpInputListener {
private:
    int port;
    IInputInjector& injector;
    SocketType sock = INVALID_SOCK;
    struct sockaddr_in lastClientAddr{};
    std::atomic<bool> hasClient{false};
    std::atomic<bool> running{false};
    std::thread workerThread;

    void sendFeedback(const InputPacket& pkt) {
        if (sock != INVALID_SOCK && hasClient) {
            sendto(sock, reinterpret_cast<const char*>(&pkt), sizeof(InputPacket), 0,
                   reinterpret_cast<const struct sockaddr*>(&lastClientAddr), sizeof(lastClientAddr));
        }
    }

    void listenLoop() {
        #if defined(_WIN32)
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        #endif

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_SOCK) {
            std::cerr << "[UDP Server] Erro ao abrir socket." << std::endl;
            return;
        }

        #if defined(_WIN32)
            DWORD timeout = 200;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        #else
            struct timeval tv{0, 200000};
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        #endif

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[UDP Server] Falha no bind da porta " << port << std::endl;
            CLOSE_SOCKET(sock);
            sock = INVALID_SOCK;
            return;
        }

        injector.setFeedbackCallback([this](const InputPacket& feedbackPkt) {
            this->sendFeedback(feedbackPkt);
        });

        std::cout << "[UDP Server] Escutando controles e pronto para Force Feedback na porta " << port << "..." << std::endl;

        InputPacket packet{};
        while (running) {
            struct sockaddr_in sender{};
            socklen_t senderLen = sizeof(sender);
            int bytes = recvfrom(sock, reinterpret_cast<char*>(&packet), sizeof(InputPacket), 0,
                                 reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
            if (bytes == sizeof(InputPacket)) {
                lastClientAddr = sender;
                hasClient = true;
                injector.inject(packet);
            }
        }

        CLOSE_SOCKET(sock);
        sock = INVALID_SOCK;
        #if defined(_WIN32)
            WSACleanup();
        #endif
    }

public:
    UdpInputListener(int listenPort, IInputInjector& inj)
        : port(listenPort), injector(inj) {}

    ~UdpInputListener() { stop(); }

    void start() {
        running = true;
        workerThread = std::thread(&UdpInputListener::listenLoop, this);
    }

    void stop() {
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
};

GStreamerEngine* g_serverEngine = nullptr;
void handleSigint(int) {
    if (g_serverEngine) g_serverEngine->stop();
}

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    std::signal(SIGINT, handleSigint);

    std::string clientIp = "127.0.0.1";
    bool devMode = false; // Oficial/Deploy por padrão

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dev" || arg == "--debug" || arg == "-d") {
            devMode = true;
        } else {
            clientIp = arg;
        }
    }

    std::cout << "=== INICIANDO HOST SERVER (Destino Cliente: " << clientIp << ") ===" << std::endl;

    auto inputInjector = InputInjectorFactory::createOSInjector(devMode);
    UdpInputListener inputListener(5001, *inputInjector);
    inputListener.start();

    DesktopScreenPipelineBuilder screenBuilder(clientIp, 5000, 5002);
    GStreamerEngine mediaEngine;
    g_serverEngine = &mediaEngine;

    if (mediaEngine.init(screenBuilder)) {
        mediaEngine.run();
    }

    inputListener.stop();
    return 0;
}