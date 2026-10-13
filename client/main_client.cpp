#include "../common/Interfaces.hpp"
#include "controllers/UdpInputDispatcher.hpp"
#include "controllers/InputControllerManager.hpp"
#include "controllers/ClientControllerFactory.hpp"
#include <iostream>
#include <csignal>
#include <memory>

// [SOLID - OCP] Construtor do Pipeline Receptor com Janela Embutida (vsink)
class ClientReceiverPipelineBuilder : public IMediaPipelineBuilder {
private:
    int videoPort;
    int audioPort;

public:
    ClientReceiverPipelineBuilder(int vPort, int aPort)
        : videoPort(vPort), audioPort(aPort) {}

    std::string buildPipelineDescription() const override {
        #if defined(_WIN32)
            std::string videoSink = "autovideosink name=vsink sync=false";
            std::string audioSink = "autoaudiosink sync=false";
        #else
            // handle-events=false impede que o GStreamer roube o mouse/teclado da nossa janela X11
            std::string videoSink = "xvimagesink name=vsink handle-events=false sync=false";
            std::string audioSink = "pulsesink buffer-time=20000 latency-time=5000 sync=false";
        #endif

        return "udpsrc port=" + std::to_string(videoPort) + " buffer-size=524288 "
               "caps=\"application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264,payload=(int)96\" ! "
               "rtpjitterbuffer latency=10 drop-on-latency=true ! "
               "rtph264depay ! avdec_h264 ! videoconvert ! "
               "queue max-size-buffers=1 leaky=downstream ! " + videoSink + " "
               
               "udpsrc port=" + std::to_string(audioPort) + " buffer-size=65536 "
               "caps=\"application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)OPUS,payload=(int)96\" ! "
               "rtpjitterbuffer latency=10 drop-on-latency=true ! "
               "rtpopusdepay ! opusdec plc=true ! audioconvert ! audioresample ! "
               "queue max-size-buffers=2 leaky=downstream ! " + audioSink;
    }
};

// [SOLID - OCP] Estratégia de Teste: Recebe Vídeo (5000) e Áudio (5002) via UDP e grava em arquivo
class ClientFileRecorderPipelineBuilder : public IMediaPipelineBuilder {
private:
    int videoPort;
    int audioPort;
    std::string outputFile;

public:
    ClientFileRecorderPipelineBuilder(int vPort, int aPort, std::string filename = "teste_stream.mkv")
        : videoPort(vPort), audioPort(aPort), outputFile(std::move(filename)) {}

    std::string buildPipelineDescription() const override {
        return "matroskamux name=mux streamable=true ! filesink location=" + outputFile + " "
               
               // Ramo de Vídeo (Recebe UDP 5000 -> Decodifica -> Grava no Mux)
               "udpsrc port=" + std::to_string(videoPort) + " do-timestamp=true buffer-size=524288 "
               "caps=\"application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264,payload=(int)96\" ! "
               "rtpjitterbuffer latency=10 drop-on-latency=true ! "
               "rtph264depay ! avdec_h264 ! videoconvert ! "
               "x264enc tune=zerolatency speed-preset=ultrafast ! queue ! mux.video_0 "
               
               // Ramo de Áudio (Recebe UDP 5002 -> Decodifica -> Grava no Mux)
               "udpsrc port=" + std::to_string(audioPort) + " do-timestamp=true buffer-size=65536 "
               "caps=\"application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)OPUS,payload=(int)96\" ! "
               "rtpjitterbuffer latency=10 drop-on-latency=true ! "
               "rtpopusdepay ! opusdec plc=true ! audioconvert ! audioresample ! "
               "opusenc ! queue ! mux.audio_0";
    }
};


GStreamerEngine* g_clientEngine = nullptr;
void handleSigint(int) {
    if (g_clientEngine) g_clientEngine->stop();
}

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    std::signal(SIGINT, handleSigint);

    std::string hostIp = "127.0.0.1";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] != '-') {
            hostIp = arg;
        }
    }
    std::cout << "=== INICIANDO GAME CLIENT (Conectando ao Host: " << hostIp << ") ===" << std::endl;

    // A Fábrica detecta o SO do Cliente (Windows ou Linux) automaticamente!
    auto kmController = ClientControllerFactory::createKeyboardMouseController();
    uintptr_t windowId = kmController->getWindowHandle();

    // 2. Inicializa o GStreamer acoplando o vídeo dentro da janela do controlador
    ClientReceiverPipelineBuilder receiverBuilder(5000, 5002);
    //ClientFileRecorderPipelineBuilder recorderBuilder(5000, 5002, "teste_stream.mkv"); //PRa testar gravação de arquivo
    GStreamerEngine mediaEngine;
    g_clientEngine = &mediaEngine;

    if (!mediaEngine.init(receiverBuilder, windowId)) {
        return -1;
    }

// 3. Registra os controladores e inicia o envio de pacotes UDP na porta 5001
    UdpInputDispatcher networkDispatcher(hostIp, 5001);
    InputControllerManager controllerManager;
    controllerManager.addController(std::move(kmController));
    controllerManager.addController(ClientControllerFactory::createGamepadController());

    // Inicia a captura de Teclado, Mouse e Gamepad enviando cada evento via UDP
    controllerManager.startAll(
        [&networkDispatcher](const InputPacket& pkt) {
            networkDispatcher.dispatch(pkt);
        },
        [&mediaEngine]() {
            mediaEngine.stop();
        }
    );

    // Inicia (uma única vez) a thread que escuta comandos reversos de Vibração (type=7) vindos do Servidor
    networkDispatcher.startFeedbackListener([&controllerManager](const InputPacket& pkt) {
        controllerManager.setRumble(pkt.key_code, static_cast<uint16_t>(pkt.mouse_dx));
    });

    // 4. Roda o stream na janela unificada
    mediaEngine.run();

    networkDispatcher.stopFeedbackListener();
    controllerManager.stopAll();

    return 0;
}