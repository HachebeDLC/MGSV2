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
# The panel surface. The real LCD is #C2C7A5, a grey with an olive cast; the
# closest emery can render is #AAAAAA (its palette is 2 bits per channel), which
# is far nearer the reference than white. Aplite is 1-bit and ignores this.
PANEL = (170, 170, 170)
# Emblem / battery bar. Pure yellow rather than amber: the panel washes warm
# colours toward the red end (the emulator renders 255,170,0 as 241,170,134),
# so starting saturated is what keeps it reading as gold on the watch.
AMBER = (255, 255, 0)

# Rail labels, chosen so every one of them means something the watchface
# actually tracks. The first three are state lamps (see marks_update_proc in
# src/c/main.c); the last three are readouts, so their boxes are taller and
# carry the label at the top with the value underneath.
LAMPS = ["CHA", "ALM", "DATA"]
READOUTS = ["STEP", "TEMP", "SUN"]


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


def octagon_points(box, chamfer):
    x0, y0, x1, y1 = box
    c = chamfer
    return [
        (x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c),
        (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c),
    ]


def draw_octagon(d, box, chamfer, fill=None, outline=None, width=1):
    pts = octagon_points(box, chamfer)
    if fill is not None:
        d.polygon(pts, fill=fill)
    if outline is not None:
        d.line(pts + [pts[0]], fill=outline, width=width, joint="curve")


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
            d.rectangle([ox - dot, oy - dot, ox + dot, oy + dot], fill=BLACK)
        else:
            label = "60" if i == 0 else str(i)
            tb = font.getbbox(label)
            tw, th = tb[2] - tb[0], tb[3] - tb[1]
            lx, ly = px + vx / n * (gap + 2), py + vy / n * (gap + 2)
            d.text((lx - tw / 2 - tb[0], ly - th / 2 - tb[1]), label,
                   font=font, fill=BLACK)


def draw_emblem(d, cx, cy, r):
    """Diamond Dogs mark: amber diamond, quartered, on a dark rounded square."""
    d.rounded_rectangle([cx - r, cy - r, cx + r, cy + r],
                        radius=max(2, r // 4), fill=BLACK)
    i = r - max(2, r // 5)
    d.polygon([(cx, cy - i), (cx + i, cy), (cx, cy + i), (cx - i, cy)], fill=AMBER)
    d.line([(cx - i, cy), (cx + i, cy)], fill=BLACK, width=1)
    d.line([(cx, cy - i), (cx, cy + i)], fill=BLACK, width=1)


def build(W, H, path):
    # The panel itself is the light surface: on the real watch everything dark
    # is a printed mark or a lit segment on that surface, so the background
    # starts white and the artwork is drawn onto it.
    im = Image.new("RGB", (W, H), PANEL)
    d = ImageDraw.Draw(im)

    # --- proportions, taken from the reference photo -----------------------
    rail_w = int(W * 0.24)                  # right rail
    scale_w = max(13, int(W * 0.088))       # seconds scale gutter
    band_h = int(H * 0.215)                 # lower band
    scale_h = max(13, int(H * 0.075))

    lcd = (scale_w, scale_h,
           W - rail_w - scale_w - 1, H - band_h - scale_h - 1)
    chamfer = max(4, W // 22)
    rule = max(2, W // 70)                  # printed line weight

    label_font = scale_font(FONT_LABEL, max(5, H // 34), "CHA")
    scale_font_ = scale_font(FONT_LABEL, max(4, H // 40), "60")

    # --- the time window: an outlined octagon, not a filled one ------------
    draw_octagon(d, lcd, chamfer, outline=BLACK, width=rule)
    draw_scale(d, lcd, chamfer, scale_font_, W)

    cx, cy = (lcd[0] + lcd[2]) // 2, (lcd[1] + lcd[3]) // 2
    emblem_r = max(9, int((lcd[3] - lcd[1]) * 0.135))
    draw_emblem(d, cx, cy, emblem_r)

    # --- right rail --------------------------------------------------------
    rx0, rx1 = W - rail_w, W - 2
    top, bottom = lcd[1], lcd[3]
    avail = bottom - top
    bar_h = max(4, avail // 22)
    gap = max(1, avail // 90)
    # three lamps, three readouts, one bar
    lamp_h = int(avail * 0.085)
    read_h = (avail - 3 * lamp_h - bar_h - 7 * gap) // 3

    small = scale_font(FONT_LABEL, max(4, H // 42), "TEMP")
    y = top
    lamps = {}
    for name in LAMPS:
        d.rectangle([rx0, y, rx1, y + lamp_h], outline=BLACK, width=1)
        tb = small.getbbox(name)
        d.text((rx0 + 3 - tb[0], y + (lamp_h - (tb[3] - tb[1])) / 2 - tb[1]),
               name, font=small, fill=BLACK)
        lamps[name] = y
        y += lamp_h + gap

    reads = {}
    for name in READOUTS:
        d.rectangle([rx0, y, rx1, y + read_h], outline=BLACK, width=1)
        tb = small.getbbox(name)
        d.text((rx0 + 3 - tb[0], y + 2 - tb[1]), name, font=small, fill=BLACK)
        # value sits under the label, inside the same box
        reads[name] = (y + (tb[3] - tb[1]) + 4, y + read_h - 1)
        y += read_h + gap

    bar_y0 = y
    bar_y1 = min(bottom, y + bar_h)
    d.rectangle([rx0, bar_y0, rx1, bar_y1], fill=AMBER, outline=BLACK, width=1)

    # --- lower band ---------------------------------------------------------
    # Continuous with the panel on the real watch; only a rule separates it.
    band_top = lcd[3] + scale_h + 2
    d.line([(2, band_top), (W - 3, band_top)], fill=BLACK, width=rule)

    # PIL antialiases text and polygon edges, which on a 64-colour screen
    # shows up as muddy fringes and on 1-bit aplite as dithering. Snap every
    # pixel to the three colours this artwork actually uses.
    palette = (BLACK, PANEL, AMBER)
    px = im.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            px[x, y] = min(palette,
                           key=lambda c: (c[0]-r)**2 + (c[1]-g)**2 + (c[2]-b)**2)

    im.save(path)
    return {
        "lcd": lcd, "chamfer": chamfer, "rail": (rx0, rx1),
        "rail_top": top,
        "lamps": lamps, "reads": reads, "bar": (bar_y0, bar_y1),
        "band_top": band_top, "emblem": (cx, cy), "emblem_r": emblem_r,
    }


def emit_defines(g, W, H):
    """Print the C constants for src/c/main.c."""
    x0, y0, x1, y1 = g["lcd"]
    rx0, rx1 = g["rail"]
    print("// generated by tools/make_background.py - do not hand-edit")
    print("#define LCD_X          %d" % x0)
    print("#define LCD_Y          %d" % y0)
    print("#define LCD_W          %d" % (x1 - x0))
    print("#define LCD_H          %d" % (y1 - y0))
    print("#define LCD_CHAMFER    %d" % g["chamfer"])
    print("#define RAIL_X         %d" % rx0)
    print("#define RAIL_W         %d" % (rx1 - rx0))
    for name, y in g["lamps"].items():
        print("#define LAMP_%-9s %d" % (name, y))
    for name, (vy0, vy1) in g["reads"].items():
        print("#define VAL_%-10s %d" % (name + "_Y", vy0))
        print("#define VAL_%-10s %d" % (name + "_H", vy1 - vy0))
    print("#define BAR_Y          %d" % g["bar"][0])
    print("#define BAR_H          %d" % (g["bar"][1] - g["bar"][0]))
    print("#define BAND_Y         %d" % g["band_top"])
    print("#define EMBLEM_CX      %d" % g["emblem"][0])
    print("#define EMBLEM_CY      %d" % g["emblem"][1])
    print("#define EMBLEM_R       %d" % g["emblem_r"])


def sync_main_c(block):
    """Write the geometry block straight into src/c/main.c.

    The whole point of generating the art is that the code cannot drift from
    it, so the constants are written rather than copied by hand.
    """
    path = os.path.join(ROOT, "src/c/main.c")
    src = open(path).read()
    marker = "// generated by tools/make_background.py"
    start = src.index(marker)
    end = src.index("\n\n", start) + 1
    if src[start:end] == block:
        return "already current"
    open(path, "w").write(src[:start] + block + src[end:])
    return "updated"


if __name__ == "__main__":
    import io, contextlib
    out = os.path.join(ROOT, "resources/images")
    block = None
    for W, H, name in ((144, 168, "bgv2.2.png"), (200, 228, "bgv2.2~emery.png")):
        g = build(W, H, os.path.join(out, name))
        print("%-22s %dx%d" % (name, W, H))
        if (W, H) == (144, 168):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                emit_defines(g, W, H)
            block = buf.getvalue()
    print("src/c/main.c            %s" % sync_main_c(block))
