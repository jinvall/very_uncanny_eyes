#!/usr/bin/env python3
"""Generate the example figures referenced by EYE_ASSET_SPEC.md.

Outputs (into ./examples/):
  01_sclera_example.png        real sclera sheet, annotated with size + iris window
  02_iris_map_example.png      real polar iris texture (512x80) with axis legend
  03_iris_map_conventions.png diagram: how x=angle, y=radius map to the strip
  04_pupil_map_example.png     real pupilMap + grayscale ramp legend
  05_lid_maps_example.png      upper/lower x asym/sym 2x2 contact sheet
  06_lid_threshold_example.png threshold sweep showing lid closing
  07_raw_to_parts.png          raw photo -> sclera crop -> iris polar workflow
  08_variant_dimensions.png    table of every shipped variant's dimensions

Run:  python3 create/make_examples.py
"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
CONV = os.path.join(PROJ, "Uncanny_Eyes", "convert")
EX = os.path.join(HERE, "examples")

BG = (18, 20, 24)
FG = (232, 236, 240)
DIM = (140, 150, 160)
ACC = (255, 176, 60)
ACC2 = (90, 200, 255)
OK = (110, 220, 150)


def font(size):
    for p in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    ):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


F_T = font(20)
F_S = font(14)
F_M = font(17)


def canvas(w, h, title=None):
    im = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(im)
    if title:
        d.text((16, 12), title, font=F_T, fill=FG)
    return im, d


def label(d, xy, text, fill=DIM, f=F_S):
    # Tolerate the (font, color) argument order being swapped at call sites.
    if isinstance(fill, ImageFont.FreeTypeFont):
        fill, f = f, fill
    d.text(xy, text, font=f or F_S, fill=fill)


def frame(d, box, color=DIM, w=1):
    d.rectangle(box, outline=color, width=w)


def checker(size, sq=8, a=(48, 52, 58), b=(60, 64, 70)):
    """Transparency checkerboard so alpha reads correctly."""
    im = Image.new("RGB", (size, size), a)
    dr = ImageDraw.Draw(im)
    for y in range(0, size, sq):
        for x in range(0, size, sq):
            if (x // sq + y // sq) % 2:
                dr.rectangle([x, y, x + sq - 1, y + sq - 1], fill=b)
    return im


def on_checker(img):
    bg = checker(max(img.size))
    if img.mode == "RGBA":
        bg.paste(img, (0, 0), img)
        return bg
    return img.convert("RGB")


def upscale(img, f):
    return img.resize((img.width * f, img.height * f), Image.NEAREST)


def load(rel):
    return Image.open(os.path.join(CONV, rel))


# ---------------------------------------------------------------- 01 sclera
def fig_sclera():
    src = load("defaultEye/sclera.png")
    SW, SH = src.size                      # 200x200
    IRIS = 80                              # defaultEye IRIS_WIDTH/HEIGHT

    im, d = canvas(980, 780, "01  sclera.png  -  the eyeball background plate")
    s = upscale(on_checker(src), 2)        # 400x400
    ox, oy = 40, 70
    im.paste(s, (ox, oy))
    frame(d, [ox, oy, ox + s.width, oy + s.height], ACC2)

    label(d, (ox, oy + s.height + 10),
          f"{SW}x{SH} px  RGB -> RGB565   (upscale shown 2x, nearest)", ACC)

    # iris window is centred: irisX = scleraX + x - (SCLERA_W - IRIS_W)/2
    off = (SW - IRIS) // 2
    sx = ox + off * 2
    sy = oy + off * 2
    frame(d, [sx, sy, sx + IRIS * 2, sy + IRIS * 2], ACC)
    label(d, [sx, sy - 18], f"iris window {IRIS}x{IRIS} (centred)", ACC)

    # 128x128 gaze viewport at rest (gazeX=0) and mirrored (gazeX=SCLERA_W-128)
    vy = oy + s.height + 40
    vx0, vgap = 40, 170
    for i, gx in enumerate((0, SW - 128)):
        vx = vx0 + i * vgap
        s2 = src.crop((gx, 0, gx + 128, 128)).convert("RGB")
        im.paste(s2, (vx, vy))
        frame(d, [vx, vy, vx + 128, vy + 128], OK, 2)
        lab = "gazeX=0  (rest, left)" if i == 0 else f"gazeX={gx}  (mirrored, right)"
        label(d, (vx, vy + 134), lab, OK)

    tx = 500
    label(d, (tx, 76), "RULES", F_T, FG)
    body = [
        "size        any, but MUST be >= 128x128",
        "             the renderer pans a 128x128",
        "             window across it for gaze",
        "",
        "shipped     128, 160, 180, 200 px square",
        "             larger = more room to look around",
        "",
        "format      RGB (or RGBA / palette / 1-bit)",
        "             tablegen flattens all to RGB",
        "",
        "content     full eyeball: sclera + iris area.",
        "             the iris disc is drawn ON TOP at",
        "             runtime, so bake the iris hole in",
        "             dark/neutral here (upstream does)",
        "",
        "edges       hard rectangular edge is fine -",
        "             the lid maps cover the border",
    ]
    y = 110
    for ln in body:
        label(d, (tx, y), ln, DIM)
        y += 19
    label(d, (40, 700),
          "the 128x128 frame the renderer actually shows, at the two extreme",
          DIM)
    label(d, (40, 720),
          "gaze positions. Anything outside the sclera plate is never",
          DIM)
    label(d, (40, 740),
          "visible, and the lid maps hide the plate border.", DIM)
    im.save(os.path.join(EX, "01_sclera_example.png"))


# --------------------------------------------------------------- 02 iris map
def fig_iris_map():
    src = load("defaultEye/iris.png")     # 256x64
    W, H = src.size

    im, d = canvas(1120, 420, "02  iris.png  -  unrolled polar iris texture")
    f = 3
    s = upscale(src.convert("RGB"), f)    # 768x192
    ox, oy = 90, 116
    im.paste(s, (ox, oy))
    frame(d, [ox, oy, ox + s.width, oy + s.height], ACC2)

    label(d, (ox, oy + s.height + 58),
          f"{W}x{H} px  RGB -> RGB565   (shown {f}x, nearest)", ACC)

    # axes
    label(d, (ox, oy - 50), "x = angle, 0..511 wraps a full 360 degrees", ACC2)
    label(d, (ox, oy - 28), "y = radius: row 0 = OUTER rim, row H-1 = pupil edge", ACC2)

    # x ticks every 64
    for x in range(0, W, 64):
        px = ox + x * f
        d.line([px, oy + s.height, px, oy + s.height + 10], fill=DIM)
        deg = int(360 * x / W)
        label(d, (px - 10, oy + s.height + 14), f"{deg}", DIM)
    label(d, (ox + s.width + 14, oy + s.height + 14), "degrees", DIM)

    # y ticks
    for y in (0, H // 2, H - 1):
        py = oy + y * f
        d.line([ox - 10, py, ox, py], fill=DIM)
        label(d, (ox - 66, py - 8), f"row {y}", DIM)

    tx = 890
    label(d, (tx, 130), "LIMITS", F_T, FG)
    y = 164
    for ln in ["width  <= 512", "height <= 128",
               "any WxH is fine,", "renderer scales",
               "y by iris size"]:
        label(d, (tx, y), ln, DIM)
        y += 19
    im.save(os.path.join(EX, "02_iris_map_example.png"))


# --------------------------------------------------- 03 iris map conventions
def fig_iris_conventions():
    im, d = canvas(980, 800, "03  how the renderer indexes the iris strip")

    # left: the source disc
    N = 220
    src = load("dragonEye/iris1.png").convert("RGB")
    disc = src.resize((N, N), Image.LANCZOS)
    cx, cy = 90, 140
    m = checker(N)
    m.paste(disc, (0, 0))
    im.paste(m, (cx, cy))
    frame(d, [cx, cy, cx + N, cy + N], ACC2)
    label(d, (cx, cy + N + 12), "source: circular iris photo", ACC2)
    label(d, (cx, cy + N + 32), "angles in degrees", DIM)

    # draw angle spokes
    for a in range(0, 360, 45):
        rad = math.radians(a)
        d.line([cx + N / 2, cy + N / 2,
                cx + N / 2 + math.cos(rad) * N / 2,
                cy + N / 2 + math.sin(rad) * N / 2], fill=ACC)
        lx = cx + N / 2 + math.cos(rad) * (N / 2 + 12) - 8
        ly = cy + N / 2 + math.sin(rad) * (N / 2 + 12) - 8
        label(d, (lx, ly), f"{a}", ACC)

    # arrow
    d.line([cx + N + 30, cy + N / 2, cx + N + 130, cy + N / 2], fill=ACC, width=3)
    d.polygon([(cx + N + 130, cy + N / 2), (cx + N + 118, cy + N / 2 - 7),
               (cx + N + 118, cy + N / 2 + 7)], fill=ACC)
    label(d, (cx + N + 34, cy + N / 2 - 26), "unroll", ACC, F_M)

    # right: the strip with grid
    ox, oy = 480, 140
    strip = load("dragonEye/iris1.png").convert("RGB").resize((360, 56), Image.LANCZOS)
    im.paste(strip, (ox, oy))
    frame(d, [ox, oy, ox + 360, oy + 56], ACC2)
    label(d, (ox, oy - 24), "x = angle 0..511  (0 = +x / right, CCW)", ACC2)
    label(d, (ox, oy + 60), "y = radius, row 0 = outer rim", ACC2)
    label(d, (ox, oy + 78), "            row H-1 = pupil edge", ACC2)

    code = [
        "src/eye.cpp  (render lookup)",
        "",
        "polar = polar[irisY*IRIS_W + irisX]      uint16",
        "distance = polar & 0x7f        // 127 at CENTRE",
        "                               //   0 at limbus",
        "if (distance < irisThreshold) {",
        "    scaled = distance * IRIS_MAP_H*65536",
        "             / irisThreshold / 65536",
        "    angle  = (IRIS_MAP_W * (polar >> 7)) / 512",
        "    pixel  = iris[scaled*IRIS_MAP_W + angle]",
        "} else {",
        "    pixel  = sclera[...]",
        "}",
        "",
        "polar word layout (tablegen.py):",
        "    bits 15..7 = angle, 0..511  (0..2pi)",
        "    bits  6..0 = 127 - radius",
        "    outside the circle: 0x007F",
    ]
    ty = 440
    label(d, (90, ty), code[0], FG, F_M)
    y = ty + 26
    for ln in code[1:]:
        col = DIM if not ln.startswith("polar word") else ACC
        label(d, (90, y), ln, col, font(13) if ln.startswith(" ") else F_S)
        y += 18

    notes = [
        "WHY THIS SHAPE",
        "",
        "The iris has to scale and shift every frame",
        "without re-reading the disc, so it is stored",
        "already unrolled: one row per radius, one",
        "column per angle. Lookup is then two array",
        "indexes and no trigonometry.",
        "",
        "CONSEQUENCE FOR YOUR SAMPLES",
        "",
        "You cannot drop a rectangular photo in as",
        "iris.png - it has to be unrolled first, from",
        "the SAME crop as sclera.png, or the two will",
        "not line up when the eye looks around.",
        "",
        "Vertical detail in iris.png becomes radial",
        "detail on screen; horizontal detail becomes",
        "circumferential. Keep the strip wide (512)",
        "so the fibres do not visibly step.",
    ]
    nx = 540
    label(d, (nx, 440), notes[0], FG, F_M)
    for i, ln in enumerate(notes[1:]):
        col = DIM
        if ln.startswith("You cannot") or ln.startswith("Vertical"):
            col = ACC
        elif ln.startswith("CONSEQUENCE") or ln.startswith("WHY"):
            col = ACC2
        label(d, (nx, 468 + i * 18), ln, col)
    im.save(os.path.join(EX, "03_iris_map_conventions.png"))


# --------------------------------------------------------------- 04 pupilMap
def fig_pupil_map():
    src = load("dragonEye/pupilMap.png")   # 160x160 L
    W, H = src.size

    im, d = canvas(980, 630, "04  pupilMap.png  -  optional pupil shape mask")
    s = upscale(src.convert("RGB"), 2)
    ox, oy = 60, 84
    im.paste(s, (ox, oy))
    frame(d, [ox, oy, ox + s.width, oy + s.height], ACC2)
    label(d, (ox, oy + s.height + 8),
          f"{W}x{H} px  grayscale  ==  IRIS_WIDTH x IRIS_HEIGHT  (160x160)", ACC)

    # legend
    lx, ly = ox + s.width + 60, oy
    label(d, (lx, ly), "value = pupil radius at that angle", F_T, FG)
    label(d, (lx, ly + 30), "0   black  = pupil closed (radius 0)", DIM)
    label(d, (lx, ly + 50), "128 grey   = pupil half the iris radius", DIM)
    label(d, (lx, ly + 70), "255 white  = pupil fills the iris", DIM)
    for i, v in enumerate((0, 64, 128, 192, 255)):
        by = ly + 100 + i * 34
        d.rectangle([lx, by, lx + 60, by + 28], fill=(v, v, v))
        frame(d, [lx, by, lx + 60, by + 28])
        label(d, (lx + 70, by + 7), str(v), DIM)

    label(d, (60, 540),
          "Only needed for non-round pupils (dragon / goat / cat slits).", DIM)
    label(d, (60, 560),
          "Omit the file and tablegen assumes a perfectly round pupil.", DIM)
    label(d, (60, 584),
          "Build-time only - it bakes the polar table; nothing reads it at runtime.", ACC)
    im.save(os.path.join(EX, "04_pupil_map_example.png"))


# ------------------------------------------------------------------ 05 lids
def fig_lids():
    im, d = canvas(980, 930, "05  lid maps  -  four 128x128 grayscale threshold maps")

    names = [
        ("lid-upper.png", "asym  LEFT/RIGHT differ", ACC),
        ("lid-upper-symmetrical.png", "sym  mirrored", ACC2),
        ("lid-lower.png", "asym  LEFT/RIGHT differ", ACC),
        ("lid-lower-symmetrical.png", "sym  mirrored", ACC2),
    ]
    x0, y0, step = 60, 110, 240
    for i, (fn, note, col) in enumerate(names):
        p = os.path.join(CONV, "defaultEye", fn)
        g = Image.open(p).convert("L")
        x = x0 + (i % 2) * step
        y = y0 + (i // 2) * step
        s = upscale(on_checker(g.convert("RGB")), 1)
        im.paste(s, (x, y))
        frame(d, [x, y, x + 128, y + 128], col, 2)
        label(d, (x, y + 134), fn, col)
        label(d, (x, y + 152), note, DIM)

    # value semantics
    bx, by = 560, 110
    label(d, (bx, by), "VALUE SEMANTICS", F_T, FG)
    lines = [
        "value <= threshold  ->  pixel is EYELID (black)",
        "value >  threshold  ->  pixel is drawn",
        "",
        "upper lid: 0 at the very top of the frame,",
        "           255 down at the bottom.",
        "lower lid: 255 at the very top,",
        "           0 at the very bottom.",
        "",
        "A soft gradient between the two is what",
        "makes the blink slide instead of snap.",
        "",
        "rest pose in this project:",
        "    upperThreshold = 128",
        "    lowerThreshold = 0",
        "    (dozing: 218 / 36, blinks sweep",
        "     0..255)",
        "",
        "HARD REQUIREMENT: exactly 128x128.",
        "tablegen rejects any other size.",
    ]
    y = by + 32
    for ln in lines:
        col = DIM
        if ln.startswith("HARD"):
            col = (255, 120, 120)
        elif "threshold" in ln or "rest pose" in ln:
            col = ACC
        label(d, (bx, y), ln, col)
        y += 19

    # ruler from a real upper lid, column 64
    g = np.array(Image.open(os.path.join(CONV, "defaultEye", "lid-upper.png")).convert("L"))
    rx, ry = 560, 560
    label(d, (rx, ry - 22), "real lid-upper.png column x=64", F_M, FG)
    for i in range(0, 128, 2):
        v = int(g[i, 64])
        h = int(v / 255 * 70)
        d.rectangle([rx + i * 3, ry + 70 - h, rx + i * 3 + 2, ry + 70], fill=(v, v, v))
    d.line([rx, ry + 70, rx + 128 * 3, ry + 70], fill=DIM)
    label(d, (rx, ry + 76), "row 0 (top)   value 0    = always lidded", (255, 120, 120))
    label(d, (rx, ry + 96), "row 127(bot)  value 255  = never lidded", OK)
    label(d, (rx, ry + 122), "with uT=128 the lid edge lands mid-gradient", ACC)

    notes = [
        "ALL FOUR maps must be present.",
        "tablegen.py only needs the two '-symmetrical' files;",
        "if you skip an asymmetric pair it silently reuses the",
        "symmetrical one - which is why doe/nauga only ship 2.",
        "",
        "Mode is free: the shipped files are L (grey), P, 1-bit",
        "and RGBA. tablegen calls convert('L'), so alpha is",
        "DISCARDED - a transparent lid becomes black = always shut.",
        "",
        "Keep the outer 3-4 px of every edge map at 255.",
        "A value of 0 at the frame edge leaves a visible",
        "gap between the lid and the bezel when the eye looks",
        "to the corner.",
    ]
    ny = 740
    label(d, (40, ny), notes[0], ACC, F_M)
    for i, ln in enumerate(notes[1:]):
        col = DIM
        if ln.startswith("Keep") or ln.startswith("A value"):
            col = (255, 170, 120)
        label(d, (40, ny + 24 + i * 18), ln, col)
    im.save(os.path.join(EX, "05_lid_maps_example.png"))


# ------------------------------------------------------- 06 lid threshold
def fig_lid_threshold():
    im, d = canvas(980, 360, "06  lid threshold sweep  -  the blink, pixel by pixel")

    up = np.array(Image.open(os.path.join(CONV, "defaultEye", "lid-upper.png")).convert("L"))
    lo = np.array(Image.open(os.path.join(CONV, "defaultEye", "lid-lower.png")).convert("L"))

    thresholds = [0, 64, 128, 192, 255]
    x0, step = 40, 150
    for i, t in enumerate(thresholds):
        x = x0 + i * step
        rgb = np.zeros((128, 128, 3), np.uint8)
        # approximate: teal sclera, lid pixel black
        rgb[:, :] = (40, 150, 160)
        mask = (up <= t) | (lo <= 0)
        rgb[mask] = (0, 0, 0)
        s = Image.fromarray(rgb)
        im.paste(s, (x, 100))
        frame(d, [x, 100, x + 128, 228], ACC)
        label(d, (x, 236), f"upper={t}", ACC if t == 128 else DIM)

    label(d, (40, 62),
          "every variant uses the same 4 lid maps; only the threshold animates", DIM)
    label(d, (40, 262),
          "so a badly shaped lid map cannot be fixed by animation - fix the PNG.", (255, 170, 120))
    label(d, (40, 300),
          "Screw up: all-black at rest (threshold too high), or the iris clipped at rest (map too dark).", DIM)
    im.save(os.path.join(EX, "06_lid_threshold_example.png"))


# ------------------------------------------------------------ 07 raw -> parts
def fig_raw_to_parts():
    im, d = canvas(1120, 900, "07  normalising a raw eyeball sample into renderable parts")

    raw_path = os.path.join(HERE, "raw", "Eyeball003.jpeg")
    raw = Image.open(raw_path).convert("RGB")
    RW, RH = raw.size

    x, y = 40, 80
    im.paste(raw.resize((480, int(480 * RH / RW))), (x, y))
    h = int(480 * RH / RW)
    frame(d, [x, y, x + 480, y + h], DIM)
    label(d, (x, y + h + 8), f"raw sample  {RW}x{RH}  (a stock photo, not renderable)", DIM)

    # step 1 crop one eyeball, square
    eye = raw.crop((30, 10, 30 + 400, 10 + 400)).resize((200, 200), Image.LANCZOS)
    ax, ay = 600, 80
    im.paste(eye, (ax, ay))
    frame(d, [ax, ay, ax + 200, ay + 200], ACC)
    label(d, (ax, ay + 208), "STEP 1  sclera.png", ACC)
    label(d, (ax, ay + 228), "crop ONE eyeball, square it,", DIM)
    label(d, (ax, ay + 246), "200x200, pupil centred, no glare", DIM)

    # step 2 polar unroll
    strip = np.array(eye)
    hh, ww, _ = strip.shape
    c = (ww - 1) / 2.0
    # Destination grid: x = angle 0..511, y = radius row 0..127 (outer -> inner).
    OUT_W, OUT_H = 512, 128
    dx, dy = np.meshgrid(np.arange(OUT_W), np.arange(OUT_H))
    ang = (dx + 0.5) / OUT_W * 2 * np.pi
    rad = 1.0 - (dy + 0.5) / OUT_H          # row 0 = outer rim
    sx = np.clip((c + rad * c * np.cos(ang)).round().astype(int), 0, ww - 1)
    sy = np.clip((c + rad * c * np.sin(ang)).round().astype(int), 0, hh - 1)
    out = strip[sy, sx]
    pol = Image.fromarray(out)
    bx, by = 600, 344
    im.paste(pol, (bx, by))
    frame(d, [bx, by, bx + 512, by + 128], ACC2)
    label(d, (bx, by + 136), "STEP 2  iris.png   512x128", ACC2)
    label(d, (bx, by + 154), "same photo unrolled: x=angle, y=radius", DIM)
    label(d, (bx, by + 172), "row 0 = limbus, last row = collarette", DIM)
    label(d, (bx, by + 196),
          "NO black pupil hole here -", (255, 170, 120))
    label(d, (bx, by + 214),
          "the pupil is sclera showing through", (255, 170, 120))

    label(d, (600, 90),
          "STEP 0  crop the pupil to the exact circle", (255, 170, 120), F_M)

    notes = [
        "CHECKLIST for every raw sample",
        "",
        "1. one eyeball per file, square crop, iris centred",
        "2. sclera.png 200x200 RGB   (pupil area left dark)",
        "3. iris.png   512x128 RGB   (unrolled from the same crop)",
        "4. that dark pupil area in sclera.png IS the pupil -",
        "   the renderer draws iris only inside the iris",
        "   window and falls back to sclera beyond it, so",
        "   dilation reveals the hole rather than a texture",
        "5. no specular highlights - they swim when the eye moves",
        "6. lid maps: copy from defaultEye and only reshape the",
        "   silhouette if the eye shape needs it",
        "7. pupilMap.png only if the pupil is not round",
        "8. run tablegen.py, check the printed dimensions",
        "9. wrap in artwork_<name>.cpp, register, rebuild",
    ]
    ny = 600
    label(d, (40, ny), notes[0], FG, F_M)
    for i, ln in enumerate(notes[1:]):
        col = ACC if ln[:2] in ("1.", "2.", "3.") else DIM
        label(d, (40, ny + 26 + i * 18), ln, col)
    im.save(os.path.join(EX, "07_raw_to_parts.png"))


# ------------------------------------------------- 08 iris map height / dilation
def fig_iris_height():
    im, d = canvas(1160, 900,
                   "08  iris map height  -  why 128, and where the pupil comes from")

    # --- the rescale maths
    label(d, (40, 68), "THE STRIP IS RE-STRETCHED EVERY FRAME", F_T, FG)
    code = [
        "irisThreshold = (128*(1023-irisScale)+512)/1024   // 0..128",
        "irisScaleFixed = IRIS_MAP_HEIGHT * 65536 / irisThreshold",
        "row = (polar & 0x7f) * irisScaleFixed / 65536",
        "    = d * IRIS_MAP_HEIGHT / irisThreshold",
        "",
        "irisThreshold is the NORMALISER: the strip is scaled to span whatever",
        "the current iris radius is. Height is NOT the pupil diameter, and it",
        "is NOT captured at one dilation state.",
    ]
    y = 102
    for ln in code:
        col = ACC if ln.startswith("irisThreshold is") else DIM
        label(d, (40, y), ln, col, F_S)
        y += 18

    # --- the 128-level ceiling table
    ty = 258
    label(d, (40, ty),
          "7-BIT POLAR FIELD = 128 DISCRETE RADII (HARD CEILING)", ACC2, F_M)
    hdr = ["IRIS_MAP_HEIGHT", "at thr=75", "at thr=128", "verdict"]
    xs = [40, 250, 400, 560]
    for x, h in zip(xs, hdr):
        label(d, (x, ty + 30), h, FG, F_M)
    d.line([40, ty + 52, 1120, ty + 52], fill=DIM)

    rows = [
        ("64",  "64/64, LOSES 11",  "loses 64",
         "half the radial detail discarded", True),
        ("80",  "75/80, no loss",   "loses 48",
         "upstream middle ground", False),
        ("128", "75/128, no loss",  "128/128, no loss",
         "matches the field exactly", True),
        ("256", "no loss",          "no loss",
         "half the rows unreachable, wasted", True),
    ]
    y = ty + 64
    for h, a, b, verdict, hot in rows:
        col = ACC if hot else DIM
        label(d, (xs[0], y), h, col, F_M)
        label(d, (xs[1], y), a, col)
        label(d, (xs[2], y), b, col)
        label(d, (xs[3], y), verdict, col)
        y += 24
        d.line([40, y - 6, 1120, y - 6], fill=(40, 44, 50))

    label(d, (40, y + 10),
          "Shipped 256x64 and 512x80 exist because those tables live in ESP32 PROGMEM,",
          DIM)
    label(d, (40, y + 30),
          "where 128 KiB per variant is a real budget. customDragon already ships 512x128.",
          DIM)
    label(d, (40, y + 54),
          "This project is desktop SDL with no flash constraint: take the full 128.",
          ACC, F_M)

    # --- dilation trace, drawn at TRUE pixel scale, full width
    IRIS_SIZE = 80
    RADIUS = IRIS_SIZE / 2
    by = 620
    label(d, (40, by - 60),
          "DILATION: defaultEye, IRIS_SIZE=80, clamp 120..720, drawn 1:1 on the 128x128 screen",
          F_M, FG)

    for i, s in enumerate((120, 240, 420, 600, 720)):
        thr = (128 * (1023 - s) + 512) // 1024
        r = thr / 128 * RADIUS
        pupil = RADIUS - r
        cx = 130 + i * 170
        cy = by + 20
        # iris disc (true pixel radius), then the sclera hole showing as pupil
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=ACC2, width=2)
        d.ellipse([cx - pupil, cy - pupil, cx + pupil, cy + pupil], fill=BG)
        d.ellipse([cx - pupil, cy - pupil, cx + pupil, cy + pupil], outline=ACC, width=2)
        label(d, (cx - 22, cy + r + 14), f"irisScale {s}", ACC, F_M)
        label(d, (cx - 30, cy + r + 36), f"iris {2*r:.0f}px dia", ACC2, F_S)
        label(d, (cx - 34, cy + r + 54), f"pupil {2*pupil:.0f}px dia", ACC, F_S)

    label(d, (40, by + 130),
          "irisScale=1023 -> threshold 0 -> NO iris drawn at all. The window shows sclera,",
          (255, 170, 120))
    label(d, (40, by + 150),
          "i.e. the hole baked into sclera.png. That hole IS the fully dilated pupil.",
          (255, 170, 120))
    label(d, (40, by + 182),
          "=> DO NOT put a black pupil hole in iris.png. Row H-1 is the pupil edge only at",
          ACC, F_M)
    label(d, (40, by + 202),
          "the fully constricted extreme. Carry limbus-to-collarette texture and let",
          DIM)
    label(d, (40, by + 220),
          "sclera cut the pupil.", DIM)
    im.save(os.path.join(EX, "08_iris_map_height.png"))


# ------------------------------------------------------- 09 variant dimensions
def fig_variant_dimensions():
    rows = [
        # name, sclera, iris map, polar, iris min/max
        ("default",   "200x200", "256x64",  "80x80",   "-"),
        ("cat",       "180x180", "1x1",      "128x128", "100..600"),
        ("doe",       "180x180", "256x64",  "180x180", "250..500"),
        ("dragon",    "160x160", "512x80",  "160x160", "80..400"),
        ("goat",      "128x128", "402x64",  "128x128", "-"),
        ("nauga",     "180x180", "1x1",      "80x80",   "300..1023"),
        ("newt",      "200x200", "256x64",  "80x80",   "180..750"),
        ("noSclera",  "160x160", "512x80",  "160x160", "120..550"),
        ("owl",       "180x180", "1x1",      "80x80",   "300..800"),
        ("terminator","200x200", "256x64",  "80x80",   "-"),
        ("customDragon","160x160","512x128","160x160","80..400"),
    ]
    im, d = canvas(1000, 520, "08  every variant: sclera / iris map / polar / iris size")

    cols = [("variant", 40), ("sclera.png", 240), ("iris.png", 400),
            ("polar table", 560), ("IRIS_MIN..MAX", 730)]
    for name, x in cols:
        label(d, (x, 76), name, FG, F_M)
    d.line([40, 100, 960, 100], fill=DIM)

    y = 112
    for r in rows:
        for (name, x), v in zip(cols, r):
            col = ACC2 if name == "variant" else DIM
            label(d, (x, y), v, col, F_M if name == "variant" else F_S)
        y += 26
        d.line([40, y - 6, 960, y - 6], fill=(40, 44, 50))

    y += 16
    for ln in [
        "polar table is GENERATED - you never author it. Its size is IRIS_SIZE (must be even).",
        "iris.png 1x1 means a flat solid-colour iris (cat / nauga / owl).",
        "IRIS_MIN..IRIS_MAX are the iris animation clamp, not an image size.",
        "Lid maps are 128x128 for every single variant - no exceptions.",
        "owl has a generated header but NO source PNGs upstream; all others have both.",
    ]:
        label(d, (40, y), ln, DIM)
        y += 20
    im.save(os.path.join(EX, "09_variant_dimensions.png"))


def main():
    os.makedirs(EX, exist_ok=True)
    for fn in (fig_sclera, fig_iris_map, fig_iris_conventions, fig_pupil_map,
               fig_lids, fig_lid_threshold, fig_raw_to_parts, fig_iris_height,
               fig_variant_dimensions):
        fn()
        print("wrote", fn.__name__)
    print("examples ->", EX)


if __name__ == "__main__":
    sys.exit(main())