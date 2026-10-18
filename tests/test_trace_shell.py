#!/usr/bin/env python3
"""Prueba los comandos de trace en la imagen normal del ESP32."""

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.shell_test import Shell  # noqa: E402


def check(name, condition, output=""):
    if condition:
        print(f"[PASS] {name}")
        return 0
    print(f"[FAIL] {name}: {output!r}")
    return 1


def main():
    failures = 0
    shell = Shell()
    try:
        banner, booted = shell.boot()
        failures += check("arranque del shell", booted, banner.decode(errors="replace")[-300:])
        if not booted:
            return 1

        output = shell.run("help trace")
        failures += check("comando registrado y documentado", "trace on|off|stats" in output, output)

        output = shell.run("trace stats")
        failures += check("estadisticas numericas", bool(re.search(
            r"emitidos=\d+ perdidos=\d+ perdidos_pct=\d+%", output)), output)
        failures += check("ocupacion y filtros", "buffer=" in output and
                          "filtros activos:" in output, output)

        output = shell.run("trace off")
        failures += check("apagado en runtime", "trace: off" in output, output)
        output = shell.run("trace stats")
        failures += check("estado global apagado", "trace: off," in output, output)

        output = shell.run("trace filter alloc off")
        failures += check("filtro por tipo", "filtro alloc off" in output, output)
        output = shell.run("trace stats")
        active = output.split("filtros activos:", 1)[-1]
        failures += check("filtro reflejado en stats", "alloc" not in active, output)

        output = shell.run("trace filter heartbeat off")
        failures += check("heartbeat no se puede apagar", "heartbeat es incondicional" in output, output)

        output = shell.run("trace sample 4")
        failures += check("muestreo configurado", "1 de cada 4" in output, output)
        output = shell.run("trace stats")
        failures += check("muestreo reflejado en stats", "sample=4" in output, output)

        output = shell.run("trace sample 0")
        failures += check("muestreo invalido rechazado", "requiere N" in output, output)

        output = shell.run("trace on")
        failures += check("encendido en runtime", "trace: on" in output, output)
        output = shell.run("trace filter alloc on")
        failures += check("filtro restaurado", "filtro alloc on" in output, output)

        print(f"TRACE SHELL: {14 - failures} PASS, {failures} FAIL")
        return 1 if failures else 0
    finally:
        shell.s.close()


if __name__ == "__main__":
    sys.exit(main())
