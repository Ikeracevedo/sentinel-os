"""Check E12 local links, evidence numbers, and artifact structure."""

from __future__ import annotations

import csv
import re
import zipfile
from pathlib import Path
from urllib.parse import unquote

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "docs/e12/informe-tecnico.md"
README = ROOT / "README.md"
SLIDES = ROOT / "docs/e12/deliverables/presentacion-sentinel.pptx"
DOCX = ROOT / "docs/e12/deliverables/informe-tecnico.docx"
SRT = ROOT / "docs/e12/video/subtitulos.srt"
MASTER = ROOT / "experiments/e11/master_results.csv"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def links() -> int:
    paths = [README, REPORT, ROOT / "docs/e12/video/README.md", ROOT / "docs/e12/preguntas-sustentacion.md"]
    count = 0
    for doc in paths:
        text = doc.read_text(encoding="utf-8")
        for target in re.findall(r"!?(?:\[[^]]*\])\(([^)]+)\)", text):
            if target.startswith(("http:", "https:", "mailto:")):
                continue
            path_text = unquote(target.split("#", 1)[0])
            if not path_text:
                continue
            path = (doc.parent / path_text).resolve()
            check(path.exists(), f"Broken local link in {doc}: {target}")
            count += 1
    return count


def figures() -> None:
    text = REPORT.read_text(encoding="utf-8")
    check(text.count("![") == 4, "Report must contain four E11 figures")
    for name in ("switch_hist_20261021T012850Z.png", "nominal_overhead_comparison.png", "memory_leak_20261021T011525Z.png", "memory_frag_20261021T011525Z.png"):
        check(name in text, f"Missing E11 figure {name}")


def numbers() -> None:
    rows = list(csv.DictReader(MASTER.open(encoding="utf-8-sig", newline="")))
    lookup = {row["metric"]: row for row in rows}
    check(len(rows) == 24, f"Expected 24 master metrics, got {len(rows)}")
    expected = [
        ("Switch observado: media", "22.355", "22,355"),
        ("Costo de TRACE en CPU", "+1.285", "1,285"),
        ("Costo integral por evento", "109.111", "109,111"),
        ("Demora del detector", "320.862", "320,862"),
    ]
    report = REPORT.read_text(encoding="utf-8")
    readme = README.read_text(encoding="utf-8")
    for metric, csv_value, localized in expected:
        check(csv_value in lookup[metric]["measured"], f"Master value changed: {metric}")
        check(localized in report, f"Report missing {metric}")
        check(csv_value in readme or localized in readme, f"README missing {metric}")
    check("NO CUMPLE" in lookup["Switch observado: media"]["result"], "S1 verdict changed")
    check("no comprobable" in report.lower(), "Missing GPIO limitation")


def artifacts() -> None:
    check(DOCX.exists(), "Missing editable DOCX")
    with zipfile.ZipFile(DOCX) as doc:
        check("word/document.xml" in doc.namelist(), "Invalid DOCX")
    with zipfile.ZipFile(SLIDES) as deck:
        slides = [n for n in deck.namelist() if re.fullmatch(r"ppt/slides/slide\d+\.xml", n)]
        check(len(slides) == 12, f"Expected 12 slides, got {len(slides)}")
    gif = Image.open(ROOT / "docs/e12/media/deadlock-replay.gif")
    check(gif.n_frames == 4, "Unexpected GIF frame count")
    subtitle = SRT.read_text(encoding="utf-8")
    check("00:03:00,000" in subtitle and "00:03:00,001" not in subtitle, "SRT duration must end at 3 min")


def main() -> None:
    count = links()
    figures()
    numbers()
    artifacts()
    print(f"PASS E12: {count} local links; 24 E11 metrics; 4 figures; 12 slides; DOCX; 4 GIF frames; SRT 180 s")


if __name__ == "__main__":
    main()
