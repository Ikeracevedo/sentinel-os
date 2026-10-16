#!/usr/bin/env python3
"""Captura la prueba de latidos en el ESP32."""

import glob
import sys
import time

import serial


def main() -> int:
    ports = sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        print("FAIL: WSL no ve /dev/ttyUSB*")
        return 1

    data = bytearray()
    with serial.Serial(ports[-1], 57600, timeout=0.2) as port:
        port.setDTR(False)
        port.setRTS(True)
        time.sleep(0.1)
        port.setRTS(False)

        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            data.extend(port.read(max(1, port.in_waiting)))
            if b"TRACE_HEARTBEAT_DONE " in data:
                tail = data.split(b"TRACE_HEARTBEAT_DONE ", 1)[1]
                if b"\n" in tail:
                    break

    output = data.decode(errors="replace")
    for line in output.splitlines():
        if "TRACE_HEARTBEAT" in line:
            print(line.strip())
    if "TRACE_HEARTBEAT_DONE PASS" in output:
        return 0
    if "TRACE_HEARTBEAT" not in output:
        print("FAIL: no llego el resultado; ultimos bytes:")
        print(output[-1200:])
    return 1


if __name__ == "__main__":
    sys.exit(main())
