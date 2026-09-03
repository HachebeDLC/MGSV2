#!/usr/bin/env python3
"""Generate the watchface background for every target platform.

The face recreates the Seiko WIRED AGAM601 LCD rather than photographing the
watch: there is no case chrome, so the whole screen is display, the way the
Watchy MGSV face does it. Drawing it here (instead of scaling one bitmap)
means each platform gets art authored at its own resolution, with no
resampling to blur the pixel work.

Layout, left to right:  seconds scale | LCD window | seconds scale | rail
Top to bottom:          seconds scale | LCD window | seconds scale | band

Run from the project root:  python3 tools/make_background.py
"""

import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_LABEL = os.path.join(ROOT, "resources/fonts/5x5 (2).ttf")

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
# Emblem / battery bar. Pure yellow rather than amber: the panel washes warm
# colours toward the red end (the emulator renders 255,170,0 as 241,170,134),
# so starting saturated is what keeps it reading as gold on the watch.
AMBER = (255, 255, 0)

# Rail labels, chosen so every one of them means something the watchface
# actually tracks - see marks_update_proc in src/c/main.c.
RAIL = ["CHA", "ALM", "DATA", "STEP", "TEMP", "SUN"]


def scale_font(path, target_h, text="8"):
    """Largest size whose cap height fits target_h."""
    best = 6
    for size in range(6, 60):
        f = ImageFont.truetype(path, size)
        box = f.getbbox(text)
        if box[3] - box[1] > target_h:
            break
        best = size
    return ImageFont.truetype(path, best)


def draw_octagon(d, box, chamfer, fill, outline=None):
    x0, y0, x1, y1 = box
    c = chamfer
    pts = [
        (x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c),
        (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c),
    ]
    d.polygon(pts, fill=fill, outline=outline)


def draw_scale(d, box, chamfer, font, W):
    """60 dots around the LCD, numbered every five.

    Dots are spaced evenly along the perimeter, the same walk the seconds
    sweep uses at runtime, so the marks land exactly on their numbers.
    """
    x0, y0, x1, y1 = box
    w, h, c = x1 - x0, y1 - y0, chamfer
    run_x, run_y = w - 2 * c, h - 2 * c
    diag = int(c * 1.414)
    half = run_x // 2
    perim = 2 * run_x + 2 * run_y + 4 * diag

    def point(dist):
        t = dist
        if t < half:
            return x0 + w // 2 + t, y0
        t -= half
        if t < diag:
            k = t * c // diag
            return x1 - c + k, y0 + k
        t -= diag
        if t < run_y:
            return x1, y0 + c + t
        t -= run_y
        if t < diag:
            k = t * c // diag
            return x1 - k, y1 - c + k
        t -= diag
        if t < run_x:
            return x1 - c - t, y1
        t -= run_x
        if t < diag:
            k = t * c // diag
            return x0 + c - k, y1 - k
        t -= diag
        if t < run_y:
            return x0, y1 - c - t
        t -= run_y
        if t < diag:
            k = t * c // diag
            return x0 + k, y0 + c - k
        return x0 + c + (t - diag), y0

    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    gap = max(3, W // 40)        # dots sit this far outside the LCD edge
    dot = 1 if W < 180 else 1

    for i in range(60):
        px, py = point(i * perim // 60)
        vx, vy = px - cx, py - cy
        n = max(1.0, (vx * vx + vy * vy) ** 0.5)
        ox, oy = px + vx / n * gap, py + vy / n * gap
        if i % 5:
            d.rectangle([ox - dot, oy - dot, ox + dot, oy + dot], fill=WHITE)
        else:
            label = "60" if i == 0 else str(i)
            tb = font.getbbox(label)
            tw, th = tb[2] - tb[0], tb[3] - tb[1]
            lx, ly = px + vx / n * (gap + 2), py + vy / n * (gap + 2)
            d.text((lx - tw / 2 - tb[0], ly - th / 2 - tb[1]), label,
                   font=font, fill=WHITE)


def draw_emblem(d, cx, cy, r):
    """Diamond Dogs mark: amber diamond, quartered, on a dark rounded square."""
    d.rounded_rectangle([cx - r, cy - r, cx + r, cy + r],
                        radius=max(2, r // 4), fill=BLACK)
    i = r - max(2, r // 5)
    d.polygon([(cx, cy - i), (cx + i, cy), (cx, cy + i), (cx - i, cy)], fill=AMBER)
    d.line([(cx - i, cy), (cx + i, cy)], fill=BLACK, width=1)
    d.line([(cx, cy - i), (cx, cy + i)], fill=BLACK, width=1)


def build(W, H, path):
    im = Image.new("RGB", (W, H), BLACK)
    d = ImageDraw.Draw(im)

    # --- proportions, taken from the reference photo -----------------------
    rail_w = int(W * 0.24)                  # right rail
    scale_w = max(13, int(W * 0.088))       # seconds scale gutter
    band_h = int(H * 0.215)                 # lower band
    scale_h = max(13, int(H * 0.075))

    lcd = (scale_w, scale_h,
           W - rail_w - scale_w - 1, H - band_h - scale_h - 1)
    chamfer = max(4, W // 22)

    label_font = scale_font(FONT_LABEL, max(5, H // 34), "CHA")
    scale_font_ = scale_font(FONT_LABEL, max(4, H // 40), "60")

    # --- LCD window and its scale -----------------------------------------
    draw_octagon(d, lcd, chamfer, WHITE)
    draw_scale(d, lcd, chamfer, scale_font_, W)

    cx, cy = (lcd[0] + lcd[2]) // 2, (lcd[1] + lcd[3]) // 2
    emblem_r = max(9, int((lcd[3] - lcd[1]) * 0.135))
    draw_emblem(d, cx, cy, emblem_r)

    # --- right rail --------------------------------------------------------
    rx0, rx1 = W - rail_w, W - 2
    top, bottom = lcd[1], lcd[3]
    n = len(RAIL)
    slot = (bottom - top) // (n + 2)         # 2 slots left for box + bar
    pad = max(1, slot // 8)
    for i, name in enumerate(RAIL):
        by0 = top + i * slot
        by1 = by0 + slot - pad
        d.rectangle([rx0, by0, rx1, by1], fill=WHITE)
        tb = label_font.getbbox(name)
        d.text((rx0 + 2 - tb[0], (by0 + by1) / 2 - (tb[3] - tb[1]) / 2 - tb[1]),
               name, font=label_font, fill=BLACK)

    # empty box (steps readout) then the battery bar
    box_y0 = top + n * slot
    box_y1 = box_y0 + slot - pad
    d.rectangle([rx0, box_y0, rx1, box_y1], fill=WHITE)
    bar_y0 = box_y1 + pad + 1
    bar_y1 = min(bottom, bar_y0 + max(3, slot // 3))
    d.rectangle([rx0, bar_y0, rx1, bar_y1], fill=AMBER)

    # --- lower band --------------------------------------------------------
    band_top = lcd[3] + scale_h + 2
    draw_octagon(d, (1, band_top, W - 2, H - 2), chamfer, WHITE)

    # PIL antialiases text and polygon edges, which on a 64-colour screen
    # shows up as muddy fringes and on 1-bit aplite as dithering. Snap every
    # pixel to the three colours this artwork actually uses.
    palette = (BLACK, WHITE, AMBER)
    px = im.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            px[x, y] = min(palette, key=lambda c: (c[0]-r)**2 + (c[1]-g)**2 + (c[2]-b)**2)

    im.save(path)
    geom = {
        "lcd": lcd, "chamfer": chamfer, "rail": (rx0, rx1),
        "rail_slot": slot, "rail_top": top,
        "box": (box_y0, box_y1), "bar": (bar_y0, bar_y1),
        "band_top": band_top, "emblem": (cx, cy), "emblem_r": emblem_r,
    }
    return geom


def emit_defines(g, W, H):
    """Print the C constants for src/c/main.c.

    Both sizes are generated from the same fractions, so the design canvas
    stays 144x168 and the runtime keeps scaling from it.
    """
    x0, y0, x1, y1 = g["lcd"]
    rx0, rx1 = g["rail"]
    slot = g["rail_slot"]
    top = g["rail_top"]
    print("// generated by tools/make_background.py - do not hand-edit")
    print("#define LCD_X          %d" % x0)
    print("#define LCD_Y          %d" % y0)
    print("#define LCD_W          %d" % (x1 - x0))
    print("#define LCD_H          %d" % (y1 - y0))
    print("#define LCD_CHAMFER    %d" % g["chamfer"])
    print("#define RAIL_X         %d" % rx0)
    print("#define RAIL_W         %d" % (rx1 - rx0))
    for i, name in enumerate(RAIL):
        print("#define RAIL_%-9s %d" % (name, top + i * slot))
    print("#define RAIL_SLOT_H    %d" % slot)
    print("#define BOX_Y          %d" % g["box"][0])
    print("#define BOX_H          %d" % (g["box"][1] - g["box"][0]))
    print("#define BAR_Y          %d" % g["bar"][0])
    print("#define BAR_H          %d" % (g["bar"][1] - g["bar"][0]))
    print("#define BAND_Y         %d" % g["band_top"])
    print("#define BAND_H         %d" % (H - 2 - g["band_top"]))
    print("#define EMBLEM_CX      %d" % g["emblem"][0])
    print("#define EMBLEM_CY      %d" % g["emblem"][1])
    print("#define EMBLEM_R       %d" % g["emblem_r"])


if __name__ == "__main__":
    out = os.path.join(ROOT, "resources/images")
    for W, H, name in ((144, 168, "bgv2.2.png"), (200, 228, "bgv2.2~emery.png")):
        g = build(W, H, os.path.join(out, name))
        print("# %s  %dx%d" % (name, W, H))
        if (W, H) == (144, 168):
            emit_defines(g, W, H)
        print()
