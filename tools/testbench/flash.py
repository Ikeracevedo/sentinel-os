"""Entrega el CH340 a WSL, compila y graba, y devuelve COM a Windows."""

from __future__ import annotations

import shlex
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def wsl_path(path: Path) -> str:
    resolved = path.resolve()
    if not resolved.drive or len(resolved.drive) != 2 or resolved.drive[1] != ":":
        raise ValueError(f"se esperaba ruta Windows con unidad: {resolved}")
    return f"/mnt/{resolved.drive[0].lower()}/{resolved.as_posix()[3:]}"


def wait_windows_port(port: str, seconds: float = 25) -> None:
    from serial.tools import list_ports

    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if any(item.device.upper() == port.upper()
               for item in list_ports.comports()):
            return
        time.sleep(0.5)
    raise RuntimeError(f"{port} no reaparecio en Windows tras soltar USB")


def flash_board(*, busid: str, port: str, distro: str,
                variant: str, benchmark: bool) -> None:
    if variant not in ("trace-on", "trace-off"):
        raise ValueError(f"variante inesperada: {variant}")
    if not benchmark and variant == "trace-off":
        raise ValueError("TRANSPORT=1 requiere TRACE=1; usar el benchmark OFF")
    trace = int(variant == "trace-on")
    build = (f"build/trace-overhead-{'on' if trace else 'off'}"
             if benchmark else "build/transport")
    make_args = ["make", "-B", f"TRACE={trace}", f"BUILD={build}"]
    if benchmark:
        make_args.append("TRACE_OVERHEAD_BOARD_TEST=1")
    else:
        make_args.append("TRANSPORT=1")
    make_args.append("flash")
    shell_script = (f"cd {shlex.quote(wsl_path(ROOT))} && "
                    "source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && "
                    "for n in 1 2 3 4 5 6 7 8 9 10; do "
                    "compgen -G '/dev/ttyUSB*' >/dev/null && break; sleep 1; done; "
                    + " ".join(shlex.quote(arg) for arg in make_args))
    print(f"Entregando USB {busid} a WSL; compilando {variant}...", flush=True)
    wait_windows_port(port, seconds=1)
    try:
        subprocess.run(["usbipd", "attach", "--wsl", "--busid", busid],
                       check=True)
        subprocess.run(["wsl", "-d", distro, "--", "bash", "-lc", shell_script],
                       cwd=ROOT, check=True)
    finally:
        print(f"Devolviendo USB {busid} a Windows...", flush=True)
        subprocess.run(["usbipd", "detach", "--busid", busid], check=True)
    wait_windows_port(port)
    print(f"Flasheo terminado; {port} disponible en Windows", flush=True)
