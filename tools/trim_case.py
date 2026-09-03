#!/usr/bin/env python3
"""Trim the watch case from the artwork, keeping the LCD.

MentalShark's original bgv2.2.png pictures the whole watch: the display plus
the printed case bands, ADJUST/START.STOP along the top and MODE/RESET.LAP
along the bottom. Those two bands are chrome, not information, and cost about
10% of the screen.

Rather than redraw the art - a generated background came out markedly worse
than the hand-pixelled original - this cuts the two bands out and makes up the
height by repeating one flat row from inside the lower band, which has no
detail to lose. Everything else is untouched, pixel for pixel.

The full-watch original is kept as bgv2.2-full.png so this stays reversible
and cannot be applied twice.

Run from the project root:  python3 tools/trim_case.py
"""

import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = os.path.join(ROOT, "resources/images")

SOURCE = os.path.join(IMAGES, "bgv2.2-full.png")     # never modified
OUT_SMALL = os.path.join(IMAGES, "bgv2.2.png")       # aplite / basalt
OUT_EMERY = os.path.join(IMAGES, "bgv2.2~emery.png")

# Measured off the original by row profile:
CASE_TOP = 8          # rows 0..7 are ADJUST / START.STOP
CASE_BOTTOM = 159     # rows 159..167 are MODE / RESET.LAP
FILL_ROW = 150        # a flat row inside the lower band, safe to repeat


def trim(im):
    w, h = im.size
    keep = im.crop((0, CASE_TOP, w, CASE_BOTTOM))     # the display itself
    missing = h - keep.height
    if missing < 0:
        raise SystemExit("case bands are smaller than expected")

    out = Image.new("RGB", (w, h))
    # everything above the fill point
    split = FILL_ROW - CASE_TOP
    out.paste(keep.crop((0, 0, w, split)), (0, 0))
    # stretch the flat band row to make up the trimmed height
    row = keep.crop((0, split - 1, w, split))
    for i in range(missing):
        out.paste(row, (0, split + i))
    # and the band's bottom edge, so its rounded corners survive
    out.paste(keep.crop((0, split, w, keep.height)), (0, split + missing))
    return out


if __name__ == "__main__":
    if not os.path.exists(SOURCE):
        # First run: stash the untouched original before trimming anything.
        Image.open(OUT_SMALL).convert("RGB").save(SOURCE)
        print("saved untouched original -> bgv2.2-full.png")

    full = Image.open(SOURCE).convert("RGB")
    small = trim(full)
    small.save(OUT_SMALL)
    # Pixel art: nearest-neighbour, never a smooth resample.
    small.resize((200, 228), Image.NEAREST).save(OUT_EMERY)

    print("bgv2.2.png             %dx%d" % small.size)
    print("bgv2.2~emery.png       200x228")
    print("rows removed: %d top, %d bottom; %d flat rows inserted"
          % (CASE_TOP, full.height - CASE_BOTTOM,
             full.height - (CASE_BOTTOM - CASE_TOP)))
