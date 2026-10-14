import { WsInputDispatcher } from "./WsInputDispatcher.js";
import { WebRTCMediaEngine } from "./WebRTCMediaEngine.js";
import { KeyboardMouseController } from "./KeyboardMouseController.js";
import { GamepadController } from "./GamepadController.js";

const videoElement = document.getElementById("stream");
const statusBadge  = document.getElementById("status");

function updateBadge(text, isError = false) {
    statusBadge.textContent = text;
    statusBadge.classList.toggle("disconnected", isError);
}

// 1. Inicializa o Despachante de Rede (WebSocket Binário 8 bytes)
const dispatcher = new WsInputDispatcher(`ws://${location.host}/ws`);
dispatcher.onStatusChange = updateBadge;

// 2. Inicializa o Motor WebRTC de Vídeo e Áudio
new WebRTCMediaEngine(videoElement, dispatcher, () => {
    updateBadge("Streaming Ativo (Baixa Latência)");
});

// 3. Inicializa os Controladores de Entrada (Teclado/Rato e Gamepad)
const sendPacket = (type, isPressed, keyCode, dx, dy) => {
    dispatcher.dispatch(type, isPressed, keyCode, dx, dy);
};

const kmController = new KeyboardMouseController(videoElement, sendPacket);
const padController = new GamepadController(sendPacket, (msg) => updateBadge(msg));

kmController.start();
padController.start();

// 4. Conecta o retorno de Force Feedback (Rumble type=7) ao Gamepad
dispatcher.setFeedbackListener((strongMag, weakMag) => {
    padController.setRumble(strongMag, weakMag);
});