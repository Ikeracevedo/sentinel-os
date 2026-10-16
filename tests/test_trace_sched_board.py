#!/usr/bin/env python3
"""Captura el resultado de las sondas del scheduler en la placa ESP32."""

import glob
import sys
import time

import serial


def main() -> int:
    ports = sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        print("FAIL: WSL no ve ningun puerto /dev/ttyUSB*")
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
            if b"TRACE_HEAP_STATS_DONE " in data:
                result = data.split(b"TRACE_HEAP_STATS_DONE ", 1)[1]
                if b"\n" in result:
                    break

    output = data.decode(errors="replace")
    lines = [line.strip() for line in output.splitlines()
             if "TRACE_SCHED" in line or "TRACE_HEAP_LOCK" in line
             or "TRACE_HEAP_STATS" in line]
    for line in lines:
        print(line)

    if ("TRACE_SCHED_DONE PASS" in output
            and "TRACE_HEAP_LOCK_DONE PASS" in output
            and "TRACE_HEAP_STATS_DONE PASS" in output):
        return 0
    if not lines:
        print("FAIL: no llego el resultado; ultimos bytes:")
        print(output[-1000:])
    return 1


if __name__ == "__main__":
    sys.exit(main())
