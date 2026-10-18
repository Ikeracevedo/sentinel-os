#!/usr/bin/env python3
"""Comprueba que TRACE=0 elimina simbolos y reduce texto y RAM estatica."""

from pathlib import Path
import re
import subprocess
import sys


ON = Path("build/kernel.elf")
OFF = Path("build/trace-off/kernel.elf")


def defined_symbols(path: Path) -> set[str]:
    output = subprocess.check_output(
        ["xtensa-esp32-elf-nm", "--defined-only", str(path)], text=True
    )
    return {line.split()[-1] for line in output.splitlines() if line.split()}


def section_sizes(path: Path) -> tuple[int, int, int]:
    output = subprocess.check_output(
        ["xtensa-esp32-elf-size", "-B", str(path)], text=True
    )
    fields = output.splitlines()[-1].split()
    return int(fields[0]), int(fields[1]), int(fields[2])


def main() -> int:
    if not ON.is_file() or not OFF.is_file():
        print("FAIL: primero compila make y make TRACE=0")
        return 1

    on_symbols = defined_symbols(ON)
    off_symbols = defined_symbols(OFF)
    trace_symbols = sorted(name for name in off_symbols
                           if re.match(r"^trace_(?:emit|init|pop|count|dropped|heap)", name))
    on_text, on_data, on_bss = section_sizes(ON)
    off_text, off_data, off_bss = section_sizes(OFF)

    print(f"TRACE=1: text={on_text} data={on_data} bss={on_bss}")
    print(f"TRACE=0: text={off_text} data={off_data} bss={off_bss}")
    print(f"Delta: text={on_text - off_text} B, bss={on_bss - off_bss} B")

    if "trace_emit_packed" not in on_symbols:
        print("FAIL: el binario TRACE=1 no contiene trace_emit_packed")
        return 1
    if trace_symbols:
        print("FAIL: TRACE=0 aun contiene: " + ", ".join(trace_symbols))
        return 1
    if off_text >= on_text or off_bss >= on_bss:
        print("FAIL: TRACE=0 no redujo text y bss")
        return 1

    print("PASS: TRACE=0 no contiene funciones de traza y reduce codigo y RAM")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"FAIL: no se pudo inspeccionar el binario: {exc}")
        sys.exit(1)
