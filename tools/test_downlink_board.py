"""Prueba en ESP32: descarta un comando con CRC malo y acepta el siguiente.

Ejemplo: py tools/test_downlink_board.py --port COM9 --baud 921600
"""

from __future__ import annotations

import argparse
import time

from bridge import Decoder, FRAME_CMD, FRAME_LOG, encode_frame


def collect_logs(port, decoder: Decoder, seconds: float) -> str:
    deadline = time.monotonic() + seconds
    chunks: list[str] = []
    while time.monotonic() < deadline:
        for frame in decoder.feed(port.read(4096)):
            if frame.kind == FRAME_LOG:
                chunks.append(frame.payload.decode("ascii", "replace"))
    return "".join(chunks)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True, help="Puerto serie, por ejemplo COM9")
    ap.add_argument("--baud", type=int, default=921600)
    args = ap.parse_args()

    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Falta pyserial: py -m pip install pyserial") from exc

    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    decoder = Decoder()
    try:
        port.open()
        collect_logs(port, decoder, 0.5)  # Descartar texto anterior a la prueba.

        damaged = bytearray(encode_frame(FRAME_CMD, 0, b"ps"))
        damaged[-1] ^= 0x01  # Cambiar solo el CRC; el payload sigue diciendo "ps".
        port.write(damaged)
        rejected = collect_logs(port, decoder, 1.5)
        if "downlink: CRC invalido" not in rejected:
            print("FALLO: el dispositivo no informo el CRC invalido")
            print(repr(rejected))
            return 1
        if "TID NOMBRE" in rejected:
            print("FALLO: se ejecuto el comando con CRC invalido")
            print(repr(rejected))
            return 1
        print("CRC invalido: contado y comando descartado")

        port.write(encode_frame(FRAME_CMD, 1, b"ps"))
        accepted = collect_logs(port, decoder, 2.0)
        if "TID NOMBRE" not in accepted:
            print("FALLO: el comando valido no produjo la tabla ps")
            print(repr(accepted))
            return 1
        print("Siguiente comando valido: ps ejecutado; receptor recuperado")
        print(f"Recepcion PC: tramas_ok={decoder.frames_ok} "
              f"crc_subida={decoder.crc_errors} huecos={decoder.gaps}")
        return 0
    finally:
        port.close()


if __name__ == "__main__":
    raise SystemExit(main())
