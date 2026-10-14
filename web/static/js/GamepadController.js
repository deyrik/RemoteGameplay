// [SOLID - SRP] Gerencia leitura de Gamepad HTML5, Handshake de Perfil (type=6) e Vibração (type=7)
export class GamepadController {
    constructor(onInputCallback, onStatusUpdate) {
        this.onInput = onInputCallback;
        this.onStatusUpdate = onStatusUpdate;
        this.activeGamepadIndex = null;
        this.prevButtons = [];
        this.prevAxes = [];
    }

    detectProfile(idStr) {
        const id = idStr.toLowerCase();
        if (id.includes("dualsense") || id.includes("0ce6")) return { profile: 4, vendor: 0x054c, product: 0x0ce6 };
        if (id.includes("dualshock") || id.includes("054c")) return { profile: 3, vendor: 0x054c, product: 0x09cc };
        if (id.includes("360") || id.includes("028e"))       return { profile: 1, vendor: 0x045e, product: 0x028e };
        if (id.includes("xbox") || id.includes("045e"))      return { profile: 2, vendor: 0x045e, product: 0x0b12 };
        return { profile: 5, vendor: 0x0079, product: 0x0006 };
    }

    setRumble(strongMag, weakMag) {
        if (this.activeGamepadIndex === null) return;
        const pad = navigator.getGamepads()[this.activeGamepadIndex];
        if (pad && pad.vibrationActuator) {
            pad.vibrationActuator.playEffect("dual-rumble", {
                startDelay: 0,
                duration: 250,
                strongMagnitude: strongMag / 65535.0,
                weakMagnitude: weakMag / 65535.0
            });
        }
    }

    pollLoop() {
        if (this.activeGamepadIndex !== null) {
            const pad = navigator.getGamepads()[this.activeGamepadIndex];
            if (pad) {
                pad.buttons.forEach((btn, idx) => {
                    const pressed = btn.pressed ? 1 : 0;
                    if (this.prevButtons[idx] !== pressed) {
                        this.onInput(4, pressed, idx, 0, 0);
                        this.prevButtons[idx] = pressed;
                    }
                });

                pad.axes.forEach((axisVal, idx) => {
                    const scaled = Math.max(-32767, Math.min(32767, Math.round(axisVal * 32767)));
                    if (Math.abs((this.prevAxes[idx] || 0) - scaled) > 128) {
                        this.onInput(5, idx, 0, scaled, 0);
                        this.prevAxes[idx] = scaled;
                    }
                });
            }
        }
        requestAnimationFrame(() => this.pollLoop());
    }

    start() {
        window.addEventListener("gamepadconnected", (e) => {
            this.activeGamepadIndex = e.gamepad.index;
            const info = this.detectProfile(e.gamepad.id);
            if (this.onStatusUpdate) {
                this.onStatusUpdate(`Comando Conectado: ${e.gamepad.id.split("(")[0]}`);
            }
            this.onInput(6, info.profile, info.vendor, info.product, 0);
        });

        window.addEventListener("gamepaddisconnected", (e) => {
            if (this.activeGamepadIndex === e.gamepad.index) {
                this.activeGamepadIndex = null;
                this.onInput(6, 0, 0, 0, 0); // DISCONNECTED = 0
            }
        });

        requestAnimationFrame(() => this.pollLoop());
    }
}