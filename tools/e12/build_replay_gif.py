"""Render a short, labelled GIF from one real E11 UART replay.

This is a visual excerpt of a recorded board run, not a synthetic execution.
Only log lines present in the replay are shown. Requires Pillow.
"""

from __future__ import annotations

import subprocess
import sys
import textwrap
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "experiments/e11/detector_20261021T042124Z_r01.uart"
OUT = ROOT / "docs/e12/media/deadlock-replay.gif"


def font(size: int) -> ImageFont.FreeTypeFont:
    for name in ("C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"):
        if Path(name).exists():
            return ImageFont.truetype(name, size)
    return ImageFont.load_default(size=size)


def main() -> None:
    run = subprocess.run(
        [sys.executable, str(ROOT / "tools/bridge.py"), "--replay", str(RAW)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
        encoding="utf-8",
        errors="replace",
    )
    lines = run.stdout.splitlines()
    expected = (
        "sentinel:/home/iker$ inject deadlock",
        "inject deadlock: 2 tareas, 2 mutexes; bloqueo tras 50 ms",
        "sentinel:/home/iker$ [ALERTA] DEADLOCK:",
        "  5 -> 6",
        "  6 -> 5",
        "ciclo: 5 -> 6 -> 5",
        "sentinel:/home/iker$ inject stop deadlock",
        "inject stop: deadlock detenido y limpio",
    )
    selected: list[str] = []
    for prefix in expected:
        match = next((line for line in lines if line.startswith(prefix)), None)
        if match is None:
            raise RuntimeError(f"Recorded line missing: {prefix}")
        selected.append(match)

    sequences = [selected[:2], selected[:3], selected[:6], selected]
    frames = []
    mono = font(25)
    small = font(17)
    for visible in sequences:
        canvas = Image.new("RGB", (1280, 720), "#101820")
        draw = ImageDraw.Draw(canvas)
        draw.text((54, 35), "SENTINEL OS  /  RECORDED UART REPLAY", font=font(31), fill="#e8f0f2")
        draw.text((54, 87), "E11 board capture · detector_20261021T042124Z_r01.uart", font=small, fill="#9eb0b8")
        draw.line((54, 124, 1226, 124), fill="#46636d", width=2)
        y = 160
        for line in visible:
            color = "#ff8f78" if "ALERTA" in line else "#d8e6e9"
            for fragment in textwrap.wrap(line, width=72, subsequent_indent="    "):
                draw.text((54, y), fragment, font=mono, fill=color)
                y += 40
            y += 15
        draw.text((54, 670), "Replay of measured bytes · not a live board session", font=small, fill="#9eb0b8")
        frames.append(canvas)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        OUT,
        save_all=True,
        append_images=frames[1:],
        duration=[1500, 1600, 2200, 2500],
        loop=0,
        optimize=True,
    )
    print(f"Saved {OUT} from {RAW.name}; {len(frames)} frames")


if __name__ == "__main__":
    main()
