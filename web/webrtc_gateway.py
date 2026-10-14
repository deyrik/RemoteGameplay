#!/usr/bin/env python3
import asyncio
import json
import os
import threading
import gi

gi.require_version('Gst', '1.0')
gi.require_version('GstWebRTC', '1.0')
gi.require_version('GstSdp', '1.0')
from gi.repository import Gst, GstWebRTC, GstSdp, GLib
from aiohttp import web

Gst.init(None)

# Loop nativo do GLib em background para o agente ICE/DTLS (libnice)
glib_loop = GLib.MainLoop()
glib_thread = threading.Thread(target=glib_loop.run, daemon=True)
glib_thread.start()

UDP_SERVER_IP = "127.0.0.1"
UDP_INPUT_PORT = 5001
VIDEO_RTP_PORT = 5000
AUDIO_RTP_PORT = 5002
HTTP_PORT = 8080

active_session = None


class UdpFeedbackProtocol(asyncio.DatagramProtocol):
    """Escuta pacotes reversos (type=7 Force Feedback) vindos do host_server C++ na porta 5001"""
    def __init__(self, ws, loop):
        self.ws = ws
        self.loop = loop

    def datagram_received(self, data, addr):
        if len(data) == 8 and not self.ws.closed:
            asyncio.run_coroutine_threadsafe(self.ws.send_bytes(data), self.loop)


class WebRTCSession:
    """Encapsula o pipeline webrtcbin com fila ICE e sincronismo completo de SDP"""
    def __init__(self, ws, loop):
        self.ws = ws
        self.loop = loop
        self.pipeline = None
        self.webrtc = None
        self.offer_sent = False
        self.offer_dispatched_to_ws = False
        self.remote_desc_ready = False
        self.pending_remote_ice = []
        self.pending_local_ice = []
        self.offer_sdp_text = None
        self._refs = []

    def on_bus_message(self, bus, message):
        t = message.type
        if t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"[Gateway GStreamer ERRO]: {err.message} ({debug})")
        elif t == Gst.MessageType.WARNING:
            warn, debug = message.parse_warning()
            print(f"[Gateway GStreamer AVISO]: {warn.message} ({debug})")
        return True

    def start(self):
        # Fila dimensionada (2000 pacotes RTP) para nunca cortar fragmentos de Keyframes (IDR)
        pipeline_desc = (
            "webrtcbin name=sendrecv bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302 "
            
            f"udpsrc port={VIDEO_RTP_PORT} do-timestamp=true buffer-size=200000 "
            "caps=\"application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264,payload=(int)96\" ! "
            "rtpjitterbuffer latency=20 ! "
            "rtph264depay ! h264parse config-interval=-1 ! "
            "video/x-h264,stream-format=(string)byte-stream,alignment=(string)au ! "
            "rtph264pay config-interval=1 mtu=1200 pt=96 ! "
            "capssetter caps=\"application/x-rtp,profile-level-id=(string)42e01f,packetization-mode=(string)1\" ! "
            "queue max-size-buffers=2000 max-size-bytes=0 max-size-time=0 leaky=downstream ! sendrecv.sink_0 "
            
            f"udpsrc port={AUDIO_RTP_PORT} do-timestamp=true buffer-size=200000 "
            "caps=\"application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)OPUS,payload=(int)96\" ! "
            "rtpjitterbuffer latency=20 ! "
            "rtpopusdepay ! rtpopuspay pt=97 ! "
            "queue max-size-buffers=500 max-size-bytes=0 max-size-time=0 leaky=downstream ! sendrecv.sink_1"
        )

        self.pipeline = Gst.parse_launch(pipeline_desc)

        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.on_bus_message)

        self.webrtc = self.pipeline.get_by_name("sendrecv")
        self.webrtc.connect("on-ice-candidate", self.on_ice_candidate)
        self.webrtc.connect("notify::ice-connection-state", self.on_ice_connection_state)
        self.webrtc.connect("notify::connection-state", self.on_connection_state)

        for pad_name in ("sink_0", "sink_1"):
            pad = self.webrtc.get_static_pad(pad_name)
            if pad:
                pad.connect("notify::caps", self.on_pad_caps_ready)

        self.pipeline.set_state(Gst.State.PLAYING)
        print("[Gateway WebRTC] Pipeline iniciado! Aguardando pacotes RTP do host_server...")

    def on_ice_connection_state(self, element, _pspec):
        state = element.get_property("ice-connection-state")
        print(f"[Gateway WebRTC] Estado ICE: {state.value_nick}")

    def on_connection_state(self, element, _pspec):
        state = element.get_property("connection-state")
        print(f"[Gateway WebRTC] Estado DTLS/PeerConnection: {state.value_nick}")

    def stop(self):
        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
            # Aguarda o GStreamer liberar de fato as portas UDP 5000 e 5002
            self.pipeline.get_state(Gst.CLOCK_TIME_NONE)
            self.pipeline = None
            self.webrtc = None
            self._refs.clear()
            print("[Gateway WebRTC] Sessao encerrada.")

    def try_create_offer(self):
        if self.offer_sent or not self.webrtc:
            return False

        sink0 = self.webrtc.get_static_pad("sink_0")
        if sink0 and sink0.has_current_caps():
            self.offer_sent = True
            print("[Gateway WebRTC] Video (sink_0) pronto! Criando Oferta SDP...")
            promise = Gst.Promise.new_with_change_func(self.on_offer_created)
            self._refs.append(promise)
            self.webrtc.emit("create-offer", None, promise)
        return False

    def on_pad_caps_ready(self, pad, _pspec):
        if not self.offer_sent:
            print(f"[Gateway WebRTC] Canal {pad.get_name()} negociado com sucesso!")
            GLib.timeout_add(150, self.try_create_offer)

    def on_offer_created(self, promise, *args):
        promise.wait()
        reply = promise.get_reply()
        offer = reply.get_value("offer")

        self.offer_sdp_text = offer.sdp.as_text()
        self._refs.extend([reply, offer])

        local_promise = Gst.Promise.new_with_change_func(self.on_local_description_set)
        self._refs.append(local_promise)
        self.webrtc.emit("set-local-description", offer, local_promise)

    def _flush_offer_and_local_ice(self):
        if not self.offer_sdp_text:
            return False
        msg = json.dumps({"type": "offer", "sdp": self.offer_sdp_text})
        asyncio.run_coroutine_threadsafe(self.ws.send_str(msg), self.loop)
        self.offer_dispatched_to_ws = True
        print("[Gateway WebRTC] Oferta SDP enviada ao navegador.")

        for ice_msg in self.pending_local_ice:
            asyncio.run_coroutine_threadsafe(self.ws.send_str(ice_msg), self.loop)
        self.pending_local_ice.clear()
        return False

    def on_local_description_set(self, promise, *args):
        promise.wait()
        GLib.idle_add(self._flush_offer_and_local_ice)

    def _queue_or_send_local_ice(self, mlineindex, candidate):
        msg = json.dumps({
            "type": "ice",
            "candidate": candidate,
            "sdpMLineIndex": mlineindex
        })
        if not self.offer_dispatched_to_ws:
            self.pending_local_ice.append(msg)
        else:
            asyncio.run_coroutine_threadsafe(self.ws.send_str(msg), self.loop)
        return False

    def on_ice_candidate(self, _, mlineindex, candidate):
        if not candidate:
            return
        GLib.idle_add(self._queue_or_send_local_ice, mlineindex, candidate)

    def _apply_sdp_answer(self, sdp_text):
        if not self.webrtc:
            return False
        _, sdpmsg = GstSdp.SDPMessage.new()
        GstSdp.sdp_message_parse_buffer(bytes(sdp_text.encode()), sdpmsg)
        answer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdpmsg)

        remote_promise = Gst.Promise.new_with_change_func(self.on_remote_description_set)
        self._refs.extend([sdpmsg, answer, remote_promise])
        self.webrtc.emit("set-remote-description", answer, remote_promise)
        return False

    def _flush_remote_ice(self):
        if not self.webrtc:
            return False
        self.remote_desc_ready = True
        print(f"[Gateway WebRTC] Resposta SDP aplicada! Liberando {len(self.pending_remote_ice)} candidatos ICE...")
        for mlineindex, candidate in self.pending_remote_ice:
            self.webrtc.emit("add-ice-candidate", mlineindex, candidate)
        self.pending_remote_ice.clear()
        return False

    def on_remote_description_set(self, promise, *args):
        promise.wait()
        GLib.idle_add(self._flush_remote_ice)

    def handle_sdp_answer(self, sdp_text):
        GLib.idle_add(self._apply_sdp_answer, sdp_text)

    def _apply_ice_candidate(self, mlineindex, candidate):
        if not self.webrtc:
            return False
        if not self.remote_desc_ready:
            self.pending_remote_ice.append((mlineindex, candidate))
        else:
            self.webrtc.emit("add-ice-candidate", mlineindex, candidate)
        return False

    def handle_ice_candidate(self, mlineindex, candidate):
        GLib.idle_add(self._apply_ice_candidate, mlineindex, candidate)


async def index_handler(request):
    return web.FileResponse(os.path.join(os.path.dirname(__file__), "static", "index.html"))


async def websocket_handler(request):
    global active_session
    if active_session is not None:
        active_session.stop()
        active_session = None

    ws = web.WebSocketResponse()
    await ws.prepare(request)

    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: UdpFeedbackProtocol(ws, loop),
        remote_addr=(UDP_SERVER_IP, UDP_INPUT_PORT)
    )

    rtc_session = WebRTCSession(ws, loop)
    active_session = rtc_session
    rtc_session.start()

    try:
        async for msg in ws:
            if msg.type == web.WSMsgType.BINARY:
                if len(msg.data) == 8:
                    transport.sendto(msg.data)
            elif msg.type == web.WSMsgType.TEXT:
                data = json.loads(msg.data)
                if data.get("type") == "answer":
                    rtc_session.handle_sdp_answer(data["sdp"])
                elif data.get("type") == "ice" and data.get("candidate"):
                    rtc_session.handle_ice_candidate(data["sdpMLineIndex"], data["candidate"])
    finally:
        if active_session is rtc_session:
            active_session = None
        rtc_session.stop()
        transport.close()

    return ws


if __name__ == "__main__":
    app = web.Application()
    static_dir = os.path.join(os.path.dirname(__file__), "static")

    app.router.add_get("/", index_handler)
    app.router.add_get("/ws", websocket_handler)
    app.router.add_static("/static/", path=static_dir, name="static")

    print(f"=== STREAMGAMER WEB GATEWAY ATIVO EM http://0.0.0.0:{HTTP_PORT} ===")
    web.run_app(app, host="0.0.0.0", port=HTTP_PORT)