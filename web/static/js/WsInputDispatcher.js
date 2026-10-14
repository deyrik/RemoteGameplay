// [SOLID - SRP] Responsável pela comunicação WebSocket e serialização binária (InputPacket 8 bytes)
export class WsInputDispatcher {
    constructor(wsUrl) {
        this.ws = new WebSocket(wsUrl);
        this.ws.binaryType = "arraybuffer";
        this.onFeedbackCallback = null;
        this.onSignalingCallback = null;
        this.onStatusChange = null;

        this.ws.onopen = () => {
            if (this.onStatusChange) this.onStatusChange("Conectado (Aguardando Vídeo...)", false);
        };

        this.ws.onclose = () => {
            if (this.onStatusChange) this.onStatusChange("Desconectado", true);
        };

        this.ws.onmessage = (event) => {
            if (event.data instanceof ArrayBuffer) {
                const view = new DataView(event.data);
                // Pacote reverso type = 7 (Force Feedback / Rumble)
                if (view.byteLength === 8 && view.getUint8(0) === 7 && this.onFeedbackCallback) {
                    const strongMag = view.getUint16(2, true);
                    const weakMag   = view.getUint16(4, true);
                    this.onFeedbackCallback(strongMag, weakMag);
                }
                return;
            }

            if (this.onSignalingCallback) {
                this.onSignalingCallback(JSON.parse(event.data));
            }
        };
    }

    dispatch(type, isPressed, keyCode, mouseDx, mouseDy) {
        if (this.ws.readyState !== WebSocket.OPEN) return;
        const buffer = new ArrayBuffer(8);
        const view = new DataView(buffer);
        view.setUint8(0, type);
        view.setUint8(1, isPressed);
        view.setUint16(2, keyCode & 0xFFFF, true);
        view.setInt16(4, mouseDx, true);
        view.setInt16(6, mouseDy, true);
        this.ws.send(buffer);
    }

    sendSignaling(dataObj) {
        if (this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(dataObj));
        }
    }

    setFeedbackListener(callback) {
        this.onFeedbackCallback = callback;
    }

    setSignalingListener(callback) {
        this.onSignalingCallback = callback;
    }
}