// [SOLID - SRP] Responsável exclusivamente pela negociação WebRTC e reprodução de mídia
export class WebRTCMediaEngine {
    constructor(videoElement, dispatcher, onStreamReady) {
        this.video = videoElement;
        this.audio = new Audio();
        this.audio.autoplay = true;
        this.dispatcher = dispatcher;
        this.onStreamReady = onStreamReady;
        this.pc = null;
        this.remoteDescReady = false;
        this.iceQueue = [];
        this.statsTimer = null;

        // Inicia mudo para o navegador permitir o Autoplay imediato do vídeo
        this.video.muted = true;
        this.dispatcher.setSignalingListener((msg) => this.handleSignaling(msg));
    }

    async handleSignaling(msg) {
        if (msg.type === "offer") {
            this.remoteDescReady = false;

            if (this.statsTimer) clearInterval(this.statsTimer);
            if (this.pc) this.pc.close();

            this.pc = new RTCPeerConnection({
                iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
            });

            this.pc.oniceconnectionstatechange = () => {
                console.log("[WebRTC Browser] Estado ICE:", this.pc.iceConnectionState);
            };

            this.pc.ontrack = (e) => {
                console.log("[WebRTC Browser] Track recebida:", e.track.kind);
                if (e.track.kind === "video") {
                    this.video.srcObject = new MediaStream([e.track]);
                    this.video.play().catch((err) => console.warn("[WebRTC Video Play]:", err));
                } else if (e.track.kind === "audio") {
                    this.audio.srcObject = new MediaStream([e.track]);
                    this.audio.play().catch((err) => console.warn("[WebRTC Audio Play]:", err));
                }
            };

            this.video.onloadeddata = () => {
                console.log("[WebRTC Browser] Vídeo renderizando na tela!");
                if (this.onStreamReady) this.onStreamReady();
            };

            this.pc.onicecandidate = (e) => {
                if (e.candidate && e.candidate.candidate) {
                    this.dispatcher.sendSignaling({
                        type: "ice",
                        candidate: e.candidate.candidate,
                        sdpMLineIndex: e.candidate.sdpMLineIndex
                    });
                }
            };

            await this.pc.setRemoteDescription(new RTCSessionDescription({ type: "offer", sdp: msg.sdp }));
            this.remoteDescReady = true;

            const answer = await this.pc.createAnswer();
            await this.pc.setLocalDescription(answer);
            this.dispatcher.sendSignaling({ type: "answer", sdp: answer.sdp });

            while (this.iceQueue.length > 0) {
                const cand = this.iceQueue.shift();
                await this.pc.addIceCandidate(new RTCIceCandidate(cand));
            }

            // Telemetria para diagnosticar recepção RTP e decodificação H.264 no navegador (F12)
            this.statsTimer = setInterval(async () => {
                if (!this.pc) return;
                const stats = await this.pc.getStats();
                stats.forEach((report) => {
                    if (report.type === "inbound-rtp" && report.kind === "video") {
                        console.log(
                            `[WebRTC Video Stats] Pacotes: ${report.packetsReceived} | ` +
                            `Frames Recebidos: ${report.framesReceived || 0} | ` +
                            `Frames Decodificados: ${report.framesDecoded || 0} | ` +
                            `KeyFrames: ${report.keyFramesDecoded || 0}`
                        );
                    }
                });
            }, 2000);
        } else if (msg.type === "ice" && msg.candidate) {
            const candidateInit = {
                candidate: msg.candidate,
                sdpMLineIndex: msg.sdpMLineIndex
            };
            if (!this.pc || !this.remoteDescReady) {
                this.iceQueue.push(candidateInit);
            } else {
                await this.pc.addIceCandidate(new RTCIceCandidate(candidateInit));
            }
        }
    }
}