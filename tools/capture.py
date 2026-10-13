"""Captura la salida serie de la placa a un archivo y resume el resultado.

Uso: python3 capture.py <segundos_maximos> <archivo_salida> [--no-reset]
Termina antes si aparece la linea 'TXBYTES: N' (fin de las pruebas o del soak).
Escribe al archivo mientras lee, para poder seguir el progreso desde fuera.
"""
import re
import sys
import time

import serial

max_s = float(sys.argv[1])
out = sys.argv[2]
do_reset = '--no-reset' not in sys.argv

baud = 115200
if '--baud' in sys.argv:
    baud = int(sys.argv[sys.argv.index('--baud') + 1])
import glob
ports = sorted(glob.glob('/dev/ttyUSB*'), key=lambda p: int(p[len('/dev/ttyUSB'):]))
port = ports[-1] if ports else '/dev/ttyUSB0'   # el mas reciente (usbipd puede renumerar)
s = serial.Serial(port, baud, timeout=1)
if do_reset:
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)

buf = b''
t0 = time.time()
with open(out, 'wb') as f:
    while time.time() - t0 < max_s:
        chunk = s.read(max(1, s.in_waiting))
        if chunk:
            buf += chunk
            f.write(chunk)
            f.flush()
        if re.search(rb'TXBYTES: \d+\r\n', buf):
            time.sleep(1.0)               # deja salir RESUMEN / TODAS
            tail = s.read(4096)
            buf += tail
            f.write(tail)
            break

a = buf.find(b'Sentinel OS v0.1')
m = re.search(rb'TXBYTES: (\d+)', buf)
print("duracion: %d s  bytes: %d" % (time.time() - t0, len(buf)))
for l in re.findall(rb'(?:SOAK|RESUMEN|===)[^\r\n]*', buf):
    print(l.decode(errors='replace'))
status = re.findall(rb'\[\d+s\][^\r\n]*', buf)
print("lineas de estado:", len(status))
if status:
    print(status[-1].decode(errors='replace'))
if a >= 0 and m:
    b = buf.find(b'TXBYTES: ')
    sent = int(m.group(1))
    print("enviados: %d recibidos: %d PERDIDOS: %d" % (sent, b - a, sent - (b - a)))
else:
    print("sin banner o sin TXBYTES")
