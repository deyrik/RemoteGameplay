#ifndef INTERFACES_HPP
#define INTERFACES_HPP

#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <string>
#include <iostream>
#include <cstdint>
#include <functional>

// Estrutura binária escalável para protocolo de rede (Teclado + Mouse + Gamepad + Feedback)
#pragma pack(push, 1)
struct InputPacket {
    uint8_t type;       // 1=Key, 2=MouseMove, 3=MouseBtn, 4=PadBtn, 5=PadAxis, 6=PadProfile, 7=Rumble
    uint8_t is_pressed; // 1=Pressed, 0=Released (ou AxisID no tipo 5 / GamepadProfile no tipo 6)
    uint16_t key_code;  // KeyCode/ButtonID (ou VendorID no tipo 6 / StrongMagnitude no tipo 7)
    int16_t mouse_dx;   // Delta X (ou AxisValue no tipo 5 / ProductID no tipo 6 / WeakMagnitude no tipo 7)
    int16_t mouse_dy;   // Delta Y do mouse
};
#pragma pack(pop)

using FeedbackCallback = std::function<void(const InputPacket&)>;

// [SOLID - ISP / DIP] Interface para construção de Pipelines de Mídia
class IMediaPipelineBuilder {
public:
    virtual ~IMediaPipelineBuilder() = default;
    virtual std::string buildPipelineDescription() const = 0;
};

// [SOLID - ISP / DIP] Interface para quem vai executar os comandos no SO do Host
class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual void inject(const InputPacket& packet) = 0;

    // Permite que o injetor envie eventos reversos (ex: Vibração type=7) de volta ao Cliente
    virtual void setFeedbackCallback(FeedbackCallback /*cb*/) {}
};

// [SOLID - SRP] Responsável exclusivamente por gerenciar o ciclo de vida do GStreamer
class GStreamerEngine {
private:
    GstElement* pipeline = nullptr;
    GMainLoop* loop = nullptr;

    static gboolean busCallback(GstBus* bus, GstMessage* msg, gpointer data) {
        GMainLoop* mainLoop = static_cast<GMainLoop*>(data);
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS:
                std::cout << "\n[GStreamer] Fluxo encerrado com segurança (EOS)." << std::endl;
                g_main_loop_quit(mainLoop);
                break;
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                std::cerr << "\n[GStreamer Erro]: " << err->message << std::endl;
                g_error_free(err);
                g_free(debug);
                g_main_loop_quit(mainLoop);
                break;
            }
            default:
                break;
        }
        return TRUE;
    }

public:
    GStreamerEngine() {
        loop = g_main_loop_new(NULL, FALSE);
    }

    ~GStreamerEngine() {
        stop();
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        if (loop) {
            g_main_loop_unref(loop);
        }
    }

    bool init(const IMediaPipelineBuilder& builder, uintptr_t windowHandle = 0) {
        GError* error = nullptr;
        std::string desc = builder.buildPipelineDescription();
        pipeline = gst_parse_launch(desc.c_str(), &error);

        if (error) {
            std::cerr << "[Engine] Falha ao criar pipeline: " << error->message << std::endl;
            g_clear_error(&error);
            return false;
        }

        // Se uma janela nativa foi fornecida pelo Cliente, acopla o vídeo dentro dela
        if (windowHandle != 0) {
            GstElement* vsink = gst_bin_get_by_name(GST_BIN(pipeline), "vsink");
            if (vsink) {
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(vsink), windowHandle);
                gst_object_unref(vsink);
            }
        }

        GstBus* bus = gst_element_get_bus(pipeline);
        gst_bus_add_watch(bus, busCallback, loop);
        gst_object_unref(bus);
        return true;
    }

    void run() {
        if (!pipeline) return;
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        g_main_loop_run(loop);
    }

    void stop() {
        if (pipeline && loop && g_main_loop_is_running(loop)) {
            gst_element_send_event(pipeline, gst_event_new_eos());
        }
    }
};

#endif