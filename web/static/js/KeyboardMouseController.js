// [SOLID - SRP] Captura eventos de Teclado, Cliques, Scroll e Sensor Bruto do Rato (Pointer Lock)
export class KeyboardMouseController {
    constructor(videoElement, onInputCallback) {
        this.video = videoElement;
        this.onInput = onInputCallback;
    }

    
    togglePointerLock() {
        this.video.muted = false; // Libera o áudio na primeira interação do utilizador
        if (document.pointerLockElement === this.video) {
            document.exitPointerLock();
        } else {
            this.video.requestPointerLock();
        }
    }

    mapBrowserKey(e) {
        if (e.key.length === 1) return e.key.toLowerCase().charCodeAt(0);
        return e.keyCode;
    }

    start() {
        this.video.addEventListener("click", () => {
            this.video.play();
            if (document.pointerLockElement !== this.video) {
                this.video.requestPointerLock();
            }
        });

        window.addEventListener("contextmenu", (e) => e.preventDefault());

        window.addEventListener("mousemove", (e) => {
            if (document.pointerLockElement === this.video) {
                if (e.movementX !== 0 || e.movementY !== 0) {
                    this.onInput(2, 0, 0, e.movementX, e.movementY);
                }
            }
        });

        window.addEventListener("mousedown", (e) => {
            if (document.pointerLockElement === this.video) {
                this.onInput(3, 1, e.button + 1, 0, 0);
            }
        });

        window.addEventListener("mouseup", (e) => {
            if (document.pointerLockElement === this.video) {
                this.onInput(3, 0, e.button + 1, 0, 0);
            }
        });

        window.addEventListener("wheel", (e) => {
            if (document.pointerLockElement === this.video) {
                e.preventDefault();
                const scrollCode = (e.deltaY < 0) ? 4 : 5; // 4 = Cima, 5 = Baixo
                this.onInput(3, 1, scrollCode, 0, 0);
            }
        }, { passive: false });

        window.addEventListener("keydown", (e) => {
            if (e.repeat) return;
            if (e.key === "F1") {
                e.preventDefault();
                this.togglePointerLock();
                return;
            }
            this.onInput(1, 1, this.mapBrowserKey(e), 0, 0);
        });

        window.addEventListener("keyup", (e) => {
            if (e.key === "F1") return;
            this.onInput(1, 0, this.mapBrowserKey(e), 0, 0);
        });
    }

}