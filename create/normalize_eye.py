#!/usr/bin/env python3
"""Normalise a raw single-eyeball crop into Uncanny Eyes source parts.

Emits, into a variant folder:
    sclera.png                  SCLERA x SCLERA RGB, pupil centred, dark
    iris.png                    IRIS_W x IRIS_H RGB, unrolled polar strip
    lid-upper.png               copied from the reference variant
    lid-lower.png               copied from the reference variant
    lid-upper-symmetrical.png   copied from the reference variant
    lid-lower-symmetrical.png   copied from the reference variant

No pupilMap.png: the pupil is round, so tablegen assumes one.

Geometry is measured from the image rather than hardcoded, because IRIS_SIZE has
to track the real limbus radius -- if the polar table is much larger than the
iris, the iris texture bleeds out into the sclera when the eye dilates.

Usage:
    python3 normalize_eye.py raw/cold_blue.png --name coldBlue
    python3 normalize_eye.py raw/cold_blue.png --name coldBlue --dry-run

Angle convention (matches tablegen exactly):
    the polar table stores  a = int((atan2(dy,dx) + pi) / (2*pi) * 512)
    with dy measured in IMAGE coordinates (y down), and the renderer indexes
    iris.png by that same a. So column 0 is -x (9 o'clock), 128 is -y (12
    o'clock), 256 is +x, 384 is +y -- i.e. it sweeps clockwise on screen.
"""

import argparse
import math
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
CONV = os.path.join(PROJ, "Uncanny_Eyes", "convert")
REF_VARIANT = "defaultEye"

# The polar angle field is 9 bits (0..511); the radial field is 7 bits (0..127).
POLAR_ANGLES = 512


def flood_from(mask, seed):
    """4-connected component containing `seed`, as a boolean mask.

    Plain BFS over a numpy bool mask -- no scipy dependency. The pupil is found
    this way so the dark vignette in the frame corners cannot be mistaken for it.
    """
    H, W = mask.shape
    if not mask[seed]:
        return np.zeros_like(mask)
    out = np.zeros_like(mask)
    stack = [seed]
    out[seed] = True
    while stack:
        y, x = stack.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < H and 0 <= nx < W and mask[ny, nx] and not out[ny, nx]:
                out[ny, nx] = True
                stack.append((ny, nx))
    return out



def find_pupil(lum, dark_thr):
    """Coarse pupil CENTRE via the dark blob containing the frame centre.

    Returns (cx, cy, flood_radius, area). The flood radius is only a by-product
    and is NOT used as the pupil radius -- see find_pupil_radius, which measures
    that properly from the radial profile. A dark-but-textured pupil (an organ
    rather than a black hole) breaks an absolute luma threshold: the fill stops
    at the first bright texel and reports a radius far too small.
    """
    H, W = lum.shape
    mask = lum < dark_thr
    cy0, cx0 = H // 2, W // 2
    # Seed at the darkest pixel within a small window around the centre, so a
    # slightly off-centre pupil is still found.
    win = min(H, W) // 8
    sub = lum[cy0 - win:cy0 + win + 1, cx0 - win:cx0 + win + 1]
    oy, ox = np.unravel_index(np.argmin(sub), sub.shape)
    seed = (cy0 - win + int(oy), cx0 - win + int(ox))
    comp = flood_from(mask, seed)
    area = int(comp.sum())
    ys, xs = np.nonzero(comp)
    return (float(xs.mean()), float(ys.mean()),
            math.sqrt(area / math.pi), area)


def find_pupil_radius(prof, rmax):
    """Pupil radius from the radial median profile.

    The pupil is a flat dark plateau; the iris is much brighter. Cross that gap
    at a quarter of the way up, using a percentile of the inner profile rather
    than its peak -- a peak search is defeated by internal pupil texture, and a
    percentile is immune to the blown specular highlights that plague stock eye
    photography.

    Deliberately lands slightly OUTSIDE the true pupil edge: the innermost strip
    row is the pupil border, so overshooting samples iris stroma just outside
    the pupil, whereas undershooting would bake a dark pupil into the strip.
    """
    prof = smooth(prof)
    # Plateau = the pupil's own brightness. Sample a WIDE inner band, not a
    # tiny one: a baked pure-black seed dot sits at the centre and is darker
    # than the pupil, so a narrow median reads the DOT instead and the measured
    # pupil radius shifts (chalk_blue 98->97, fire 93->90), which then offsets
    # the whole unrolled iris strip by a row or two. A wide median is dominated
    # by pupil and ignores the dot.
    band = max(2, int(0.20 * rmax))
    wide = float(np.median(prof[:band]))
    narrow = float(np.median(prof[:max(2, int(0.015 * rmax))]))
    # Only trust the wide band if the profile is still flat at its edge,
    # i.e. the band really is inside the pupil. Otherwise fall back.
    plateau = wide if prof[band - 1] <= wide * 2.5 + 2.0 else narrow
    lo = int(0.05 * rmax)
    hi = max(lo + 4, int(0.40 * rmax))
    ref = float(np.percentile(prof[lo:hi], 90))
    if ref <= plateau + 1.0:
        return None, plateau, ref
    thr = plateau + 0.25 * (ref - plateau)
    above = np.nonzero(prof[lo:hi] > thr)[0]
    if above.size == 0:
        return None, plateau, ref
    return lo + int(above[0]), plateau, ref


def radial_median(rgb, cx, cy, rmax):
    """Median luminance per radius, over 720 angles -- robust to iris crypts."""
    H, W, _ = rgb.shape
    angs = np.linspace(0, 2 * math.pi, 720, endpoint=False)
    prof = np.zeros(rmax)
    for r in range(rmax):
        xs = np.clip((cx + r * np.cos(angs)).round().astype(int), 0, W - 1)
        ys = np.clip((cy + r * np.sin(angs)).round().astype(int), 0, H - 1)
        prof[r] = np.median(rgb[ys, xs].mean(1))
    return prof


def smooth(prof, k=5):
    """Centred moving average; kills single-radius noise before extremum search."""
    if k < 2:
        return prof
    pad = k // 2
    padded = np.pad(prof, pad, mode="edge")
    kernel = np.ones(k) / k
    return np.convolve(padded, kernel, mode="valid")


def find_limbus(prof, pupil_r, rmax):
    """Limbus radius: the dark limbal ring at the outer edge of the iris.

    Two traps this has to avoid:
      * a plain argmax finds the SCLERA, which is brighter than the iris;
      * a "first local minimum" finds the shallow 1-luma flattening around
        r=70 in a real iris, not the limbal ring at the iris edge.
    So: find the iris body peak first (first local max outside the pupil), then
    take the darkest radius among those still below half the sclera level.

    Returns (limbus_r, peak_r, sclera_level) so the caller can sanity-check.
    """
    lo = int(pupil_r) + 3
    if lo >= rmax - 6:
        raise SystemExit("pupil too large / frame too small; crop tighter")

    prof = smooth(prof)
    peak_r = None
    for r in range(lo + 1, rmax - 1):
        if prof[r] > prof[r - 1] and prof[r] >= prof[r + 1]:
            peak_r = r
            break
    if peak_r is None:
        raise SystemExit("no iris-body peak found outside the pupil")

    # Sclera brightness is the peak of the outer profile. The eyeball's
    # geometric edge is found separately by find_eyeball_edge -- using this peak
    # as the edge instead is what flattened the sclera veins.
    eyeball_r = peak_r + int(np.argmax(prof[peak_r:rmax]))
    sclera_level = float(prof[eyeball_r])
    if sclera_level <= prof[peak_r]:
        raise SystemExit(
            f"sclera peak {sclera_level:.0f} is not brighter than the iris peak "
            f"{prof[peak_r]:.0f}; cannot locate the limbus reliably")

    # Limbal ring = darkest radius inside the eyeball, still below half sclera.
    iris_region = np.arange(peak_r, max(peak_r + 2, eyeball_r))
    below = iris_region[prof[iris_region] < 0.5 * sclera_level]
    pool = below if below.size >= 3 else iris_region
    limbus_r = int(pool[np.argmin(prof[pool])])
    return limbus_r, peak_r, sclera_level, eyeball_r


def shift_to_centre(img, cx, cy):
    """Translate so (cx,cy) lands on the frame centre. No borders introduced."""
    W, H = img.size
    dx = int(round((W - 1) / 2 - cx))
    dy = int(round((H - 1) / 2 - cy))
    arr = np.array(img)
    out = np.empty_like(arr)
    ys = slice(max(0, dy), min(H, H + dy))
    xs = slice(max(0, dx), min(W, W + dx))
    ysrc = slice(max(0, -dy), min(H, H - dy))
    xsrc = slice(max(0, -dx), min(W, W - dx))
    out[ys, xs] = arr[ysrc, xsrc]
    if dy > 0:
        out[:dy] = 0
    elif dy < 0:
        out[H + dy:] = 0
    if dx > 0:
        out[:, :dx] = 0
    elif dx < 0:
        out[:, W + dx:] = 0
    return Image.fromarray(out), dx, dy


def make_slit_pupil_map(iris_size, along="vertical", width=0.12, reach=0.95):
    """Grayscale pupil map encoding a slit (reptilian/cat/goat) pupil.

    The semantics are inverted from the filename suggests, which is worth stating
    because it is easy to get backwards. tablegen reads the map as, for each
    direction, the NORMALISED IRIS EXTENT along that direction:

        d(theta) = 127 - int(map(theta)/255 * 128)
        iris where d < irisThreshold

    So the map is short where the iris should NOT reach. A map that is short
    along the slit axis therefore leaves sclera as a narrow wedge running from
    the centre out to the limbus along that axis -- which is exactly what reads
    as a slit. A compact disc pupil is impossible via this path: the decision
    becomes purely angular, which is fine for a slit and is why the round case
    omits the map entirely.

    `width` is the slit half-width as a fraction of the iris radius, but it is
    NOT a geometric half-width -- see the coupling note below.

    COUPLING: sclera appears where the map is at or below
        T = R * (127 - irisThreshold) / 128
    and the angular half-width of the resulting wedge works out to
        cos(delta) ~= width / (T/R)
    so the visible slit is narrow only when `width` is close to T/R. At the rest
    pose (irisScale 420 -> threshold 75) T/R is 0.406, hence the 0.40 default.
    Set it much lower and the "slit" becomes a huge wedge that eats most of the
    iris. It also widens automatically as the eye dilates, which is the correct
    behaviour for a constricting pupil.
    """
    n = int(iris_size)
    if n % 2:
        raise SystemExit(f"IRIS_SIZE must be even, got {n}")
    R = n / 2.0
    yy, xx = np.mgrid[0:n, 0:n]
    dx = xx - (n - 1) / 2.0
    dy = yy - (n - 1) / 2.0
    theta = np.arctan2(dy, dx)
    rr = np.hypot(dx, dy)

    short, long_ = width * R, reach * R
    if along == "vertical":
        ax, ay = long_, short          # small extent vertically -> vertical slit
    elif along == "horizontal":
        ax, ay = short, long_
    else:
        raise SystemExit(f"--pupil-slit-axis must be vertical or horizontal, got {along}")

    # Ellipse radius along each direction.
    rad = (ax * ay) / np.sqrt((ay * np.cos(theta)) ** 2 + (ax * np.sin(theta)) ** 2)
    val = np.clip(rad / R, 0.0, 1.0) * 255.0
    val[rr > R] = 0.0                  # outside the circle tablegen ignores this
    return Image.fromarray(val.round().astype(np.uint8), "L")


def dilate(mask, k):
    """Grow a boolean mask by k pixels (4-neighbour), no scipy."""
    out = mask.copy()
    for _ in range(k):
        nxt = out.copy()
        nxt[1:, :] |= out[:-1, :]
        nxt[:-1, :] |= out[1:, :]
        nxt[:, 1:] |= out[:, :-1]
        nxt[:, :-1] |= out[:, 1:]
        out = nxt
    return out


def find_background_radius(rgb, cx, cy, seed_radius):
    """Per-angle radius at which the eyeball ends and the photo background begins.

    Four earlier attempts failed on real samples, all documented so they are not
    retried:

      * the radial profile's luminance peak -- not the edge; using it flattened
        the sclera veins (fire.png luma std fell 56 -> 16);
      * flood fill on "differs from border colour" seeded at the pupil centre --
        with a black background a black pupil matches the background too, so the
        seed was invalid and the fill leaked to radius 853 on a 757px image;
      * the same fill seeded at the iris mid-radius, walking outward while the
        circle stayed >=98% covered -- dark iris crypts fall below the luma cut,
        so coverage never reached 98% (returned 47, then 90, on cold_blue);
      * walking OUTWARD per angle until luma matched the background -- same
        fault: dark iris crypts and dark sclera veins read as background
        (chalk_blue stopped at 170 when the eyeball runs to ~370).

    Scanning INWARD from outside the eyeball is immune to all of that. Dark
    texture inside the sphere can never terminate the search, because the search
    only ever moves toward the centre and stops at the first eyeball pixel. The
    match is on the border's full RGB rather than luma, so a dark blue iris is
    never mistaken for a black backdrop.

    Returns (angles, radius_per_angle, border_rgb). The median radius is a safe
    representative for sizing and reporting.
    """
    H, W, _ = rgb.shape
    b = max(2, min(H, W) // 40)
    border_px = np.concatenate([
        rgb[:b, :, :].reshape(-1, 3), rgb[-b:, :, :].reshape(-1, 3),
        rgb[:, :b, :].reshape(-1, 3), rgb[:, -b:, :].reshape(-1, 3),
    ])
    bg = np.median(border_px, axis=0)

    angs = np.linspace(0, 2 * math.pi, 720, endpoint=False)
    cos_t, sin_t = np.cos(angs), np.sin(angs)
    # Start beyond the farthest in-image pixel so the first step is always valid.
    r_start = float(np.hypot(max(cx, W - 1 - cx), max(cy, H - 1 - cy)))
    stops = np.empty(len(angs))
    step = 2.0
    tol = 30.0
    for j in range(len(angs)):
        r = r_start
        found = seed_radius
        while r > seed_radius:
            x = int(round(cx + r * cos_t[j]))
            y = int(round(cy + r * sin_t[j]))
            if 0 <= x < W and 0 <= y < H:
                d = float(np.linalg.norm(rgb[y, x] - bg))
                if d > tol:
                    found = r            # first eyeball texel from the outside
                    break
            r -= step
        stops[j] = found
    return angs, stops, bg


def highlight_mask(rgb, cx, cy, eyeball_r):
    """Boolean mask of genuinely blown specular highlights.

    Two earlier attempts, both rejected against real samples:

      * luma above a fixed cut -- on a warm bright iris the stroma p99 (170 for
        fire.png) sits close to the catchlight, so any useful cut also eats the
        brightest flames;
      * bright AND desaturated -- fire.png contains a deliberately PALE BLUE
        GREY SECTOR of iris next to the fire. It is desaturated and bright, so
        this flagged genuine artwork and filled it with radial-median orange,
        leaving speckle. Worse on cold_blue (1.3% of frame wrongly replaced).

    What actually separates a catchlight is that it is locally anomalous AND
    clipped: much brighter than its own neighbourhood, and near the top of the
    range. The pale iris sector is bright but sits close to its surroundings,
    so a local-contrast test leaves it alone.
    """
    H, W, _ = rgb.shape
    lum = rgb.mean(2)

    med = np.array(Image.fromarray(lum.astype(np.uint8))
                   .filter(ImageFilter.MedianFilter(size=9)), dtype=float)
    yy, xx = np.mgrid[0:H, 0:W]
    inside = np.hypot(xx - cx, yy - cy) <= eyeball_r

    floor = max(215.0, 0.85 * float(np.percentile(lum, 99.9)))
    blown = inside & (lum > floor) & ((lum - med) > 50.0)
    return dilate(blown, 2)


def despeckle(rgb, cx, cy, rmax, blown):
    """Replace masked highlight texels with the angular median at that radius.

    A catchlight is a small bright blob on the source, but the plate pans the
    whole eyeball horizontally, so a baked highlight visibly swims. Worse, the
    iris strip is radially stretched as the pupil dilates, which smears a
    highlight into a pale streak right across the iris.

    Taking the MEDIAN over the angles at each radius excludes the highlight (a
    minority of angles) while keeping the surrounding stroma colour, and stays
    radial so the circumferential fibre structure survives.

    Returns (cleaned_rgb, replaced_count).
    """
    H, W, _ = rgb.shape
    angs = np.linspace(0, 2 * math.pi, 720, endpoint=False)
    out = rgb.copy()
    replaced = 0
    for r in range(1, rmax):
        ix = np.clip((cx + r * np.cos(angs)).round().astype(int), 0, W - 1)
        iy = np.clip((cy + r * np.sin(angs)).round().astype(int), 0, H - 1)
        bad = blown[iy, ix]
        if not bad.any():
            continue
        good = ~bad
        if good.sum() < 8:                 # too few clean samples: leave alone
            continue
        fill = np.median(rgb[iy[good], ix[good]], axis=0)
        out[iy[bad], ix[bad]] = fill
        replaced += int(bad.sum())
    return out, replaced


def build_plate(rgb, cx, cy, size, limbus_r_src, limbus_r_plate, eyeball_r_src,
                bg_angs=None, bg_stops=None):
    """Radial resample of the eyeball into a `size` square plate.

    The eyeball is scaled so its limbus lands at `limbus_r_plate`, and the
    sclera margin beyond it is SYNTHESISED. That synthesis is unavoidable: a
    tightly cropped photo has almost no sclera, but a 200px plate with the
    limbus at 40px needs sclera out to 100px.

    Two approaches were tried and rejected:
      * repeating the outermost ring radially -- turns every sclera vein into
        an infinite spoke;
      * mirroring about the eyeball boundary -- leaves a visible fold ring.
    What works is blending into a flat median sclera colour over a short radial
    ramp. The lid maps cover the periphery anyway, and real sclera at that
    radius is close to uniform.
    """
    H, W, _ = rgb.shape
    k = limbus_r_src / limbus_r_plate          # source px per plate px
    half = size / 2.0

    n = size
    c = (n - 1) / 2.0
    py, px = np.mgrid[0:n, 0:n]
    dx = px - c
    dy = py - c
    rp = np.sqrt(dx * dx + dy * dy)
    theta = np.arctan2(dy, dx)
    # Clamp the sample radius at the eyeball edge, per angle: beyond it the
    # source is the photo background (black or white), which must never be
    # sampled or it fringes the plate margin.
    rs = rp * k
    if bg_stops is not None:
        idx = np.round((theta + math.pi) / (2 * math.pi) * len(bg_angs)).astype(int)
        idx = np.clip(idx, 0, len(bg_stops) - 1)
        rs = np.minimum(rs, bg_stops[idx])
    else:
        rs = np.minimum(rs, eyeball_r_src)
    sx = np.clip((cx + rs * np.cos(theta)).round().astype(int), 0, W - 1)
    sy = np.clip((cy + rs * np.sin(theta)).round().astype(int), 0, H - 1)
    out = rgb[sy, sx].astype(float)

    # Median colour of the sclera, for the synthesised margin only.
    angs = np.linspace(0, 2 * math.pi, 720, endpoint=False)
    ring = []
    for rr in np.linspace(0.78, 0.95, 6) * eyeball_r_src:
        ix = np.clip((cx + rr * np.cos(angs)).round().astype(int), 0, W - 1)
        iy = np.clip((cy + rr * np.sin(angs)).round().astype(int), 0, H - 1)
        ring.append(rgb[iy, ix])
    sclera_rgb = np.median(np.concatenate(ring, axis=0), axis=0)

    # Keep REAL sclera for as long as the source has any, then fade to flat.
    # Blending on the SOURCE radius instead (as an earlier version did) started
    # flattening veins that are still perfectly visible -- fire.png's sclera is
    # the whole point of the eye, and its luma std fell from 56 to 16.
    r_real = min(eyeball_r_src / k, half * 0.98)
    t = np.clip((rp - r_real) / max(1e-6, half - r_real), 0.0, 1.0)
    t = t * t * (3.0 - 2.0 * t)                       # smoothstep
    out = out * (1.0 - t[:, :, None]) + sclera_rgb[None, None, :] * t[:, :, None]

    # Gentle falloff toward the corners so the margin reads as curved sclera.
    fall = 1.0 - 0.30 * np.clip(rp / half, 0.0, 1.0) ** 2
    out *= fall[:, :, None]
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def unroll_iris(rgb, cx, cy, pupil_r, limbus_r, out_w, out_h):
    """Polar unroll: x = angle, row 0 = limbus, last row = pupil edge.

    Deliberately stops at the pupil radius rather than the centre, so the strip
    carries limbus-to-collarette texture and the pupil stays sclera's job.
    """
    H, W, _ = rgb.shape
    col = np.arange(out_w)
    row = np.arange(out_h)
    theta = -math.pi + 2 * math.pi * (col + 0.5) / out_w
    # row 0 -> limbus_r, row out_h-1 -> pupil_r
    radius = limbus_r - (limbus_r - pupil_r) * (row + 0.5) / out_h
    sx = cx + radius[:, None] * np.cos(theta)[None, :]
    sy = cy + radius[:, None] * np.sin(theta)[None, :]
    ix = np.clip(sx.round().astype(int), 0, W - 1)
    iy = np.clip(sy.round().astype(int), 0, H - 1)
    return Image.fromarray(rgb[iy, ix])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="raw single-eyeball crop (png/jpeg)")
    ap.add_argument("--name", required=True, help="variant name, e.g. coldBlue")
    ap.add_argument("--out", default=None,
                    help="output variant dir (default Uncanny_Eyes/convert/<name>)")
    ap.add_argument("--sclera-size", type=int, default=200)
    ap.add_argument("--framing", choices=("limbus", "eyeball"), default="limbus",
                    help="limbus: scale so the limbus sits at --limbus-radius, "
                         "synthesising a sclera margin (matches defaultEye, "
                         "IRIS_SIZE 80). eyeball: the eyeball itself fills the "
                         "plate, so IRIS_SIZE follows from the real limbus and "
                         "the iris ends up much larger than any shipped variant.")
    ap.add_argument("--iris-w", type=int, default=POLAR_ANGLES)
    ap.add_argument("--iris-h", type=int, default=128)
    ap.add_argument("--limbus-radius", type=int, default=40,
                    help="limbus framing only: target limbus radius in the plate, px")
    ap.add_argument("--pupil", choices=("round", "slit"), default="round",
                    help="round (no pupilMap.png) or slit (writes pupilMap.png)")
    ap.add_argument("--slit-axis", choices=("vertical", "horizontal"),
                    default="vertical", help="slit orientation")
    ap.add_argument("--slit-width", type=float, default=0.40,
                    help="slit half-width as a fraction of the iris radius; ~0.40 matches "
                         "T/R at the rest pose")
    ap.add_argument("--pupil-centre", default=None,
                    help="force the pupil centre as X,Y in source pixels; "
                         "skips detection entirely (most deterministic)")
    ap.add_argument("--pupil-threshold", type=int, default=12,
                    help="luma below this counts as pupil (centre estimate only)")
    ap.add_argument("--dry-run", action="store_true", help="measure only, write nothing")
    args = ap.parse_args()

    out_dir = args.out or os.path.join(CONV, args.name)

    src = Image.open(args.source)
    had_alpha = src.mode in ("RGBA", "LA") or "transparency" in src.info
    # Alpha is discarded by tablegen anyway; flatten onto black so the RGBA
    # channel cannot surprise anyone later.
    img = src.convert("RGBA") if had_alpha else src.convert("RGB")
    if had_alpha:
        bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
        img = Image.alpha_composite(bg, img)
    img = img.convert("RGB")

    rgb = np.array(img).astype(float)
    lum = rgb.mean(2)
    H, W = lum.shape

    rmax = min(H, W) // 2 - 2

    # Pupil centre, in order of decreasing determinism:
    #   1. --pupil-centre   (explicit; nothing is inferred)
    #   2. the darkest blob (inferred -- reliable, but least reproducible)
    #
    # A baked #000000 seed dot is deliberately NOT auto-detected. A black pupil
    # is full of scattered exactly-zero texels, so any flood fill started at the
    # dot merges with them and the blob is neither compact nor centred: the
    # guard that rejected those specks also rejected a genuine dot. On
    # 200px sources the deterministic path is to bake the dot AND pass its fixed
    # position via --pupil-centre.
    seed_note = "inferred from darkest blob"
    if args.pupil_centre:
        try:
            px, py = (float(v) for v in args.pupil_centre.split(","))
        except ValueError:
            raise SystemExit("--pupil-centre must be X,Y in source pixels")
        pupil_cx, pupil_cy = px, py
        seed_note = f"forced to ({px:g}, {py:g})"
        flood_r, flood_area = float("nan"), 0
    else:
        pupil_cx, pupil_cy, flood_r, flood_area = find_pupil(lum, args.pupil_threshold)

    prof = radial_median(rgb, pupil_cx, pupil_cy, rmax)

    pupil_r, plateau, ref = find_pupil_radius(prof, rmax)
    if pupil_r is None:
        raise SystemExit("could not find a pupil/iris boundary in the radial profile")
    limbus_r, peak_r, sclera_level, _ = find_limbus(prof, pupil_r, rmax)
    bg_angs, bg_stops, bg_rgb = find_background_radius(
        rgb, pupil_cx, pupil_cy, (pupil_r + limbus_r) / 2.0)
    eyeball_r = float(np.median(bg_stops))
    bg_luma = float(np.mean(bg_rgb))

    # Sanity checks -- these catch a mis-detected geometry before it silently
    # produces a variant that renders wrong.
    problems = []
    if pupil_r < 4:
        problems.append(f"pupil radius {pupil_r}px is implausibly small")
    if pupil_r > 0.35 * rmax:
        problems.append(f"pupil radius {pupil_r}px is implausibly large "
                        f"(>35% of frame); the crop may be wrong")
    if flood_r == flood_r and abs(pupil_r - flood_r) > 0.4 * pupil_r:
        problems.append(f"profile pupil r={pupil_r} disagrees with dark-blob "
                        f"r={flood_r:.0f}; check the crop")
    if limbus_r <= pupil_r * 1.15:
        problems.append(f"limbus {limbus_r} not clearly outside pupil {pupil_r:.1f}")
    if limbus_r >= rmax - 2:
        problems.append(f"limbus {limbus_r} hits the frame edge ({rmax}); crop tighter")
    off = math.hypot(pupil_cx - (W - 1) / 2, pupil_cy - (H - 1) / 2)
    if off > min(H, W) * 0.05:
        problems.append(f"pupil is {off:.1f}px off centre (>5%); recentring anyway")
    if args.iris_h > 128:
        problems.append(f"iris-h {args.iris_h} exceeds the 128-value radial field")

    # IRIS_SIZE is the polar table side and must match the limbus as it appears in
    # the PLATE, or the iris texture bleeds into the sclera as the eye dilates.
    if args.framing == "eyeball":
        # The eyeball itself is the plate: its edge maps to the plate half-width.
        limbus_r_plate = limbus_r * (args.sclera_size / 2.0) / eyeball_r
    else:
        limbus_r_plate = float(args.limbus_radius)

    iris_size = int(round(2 * limbus_r_plate))
    if iris_size % 2:
        iris_size += 1
    if iris_size > args.sclera_size:
        print(f"  ! limbus radius {limbus_r_plate:.0f} does not fit in a "
              f"{args.sclera_size}px plate")
    if args.sclera_size < 128:
        print("  ! sclera plate must be at least 128x128")

    print(f"source        {args.source}  {W}x{H}  mode={src.mode}"
          f"{'  (alpha flattened)' if had_alpha else ''}")
    print(f"pupil         centre ({pupil_cx:.1f}, {pupil_cy:.1f})  "
          f"r={pupil_r}px from profile")
    print(f"              centre source: {seed_note}")
    print(f"              plateau luma {plateau:.0f}, iris ref {ref:.0f}")
    print(f"iris peak     r={peak_r}  luma={prof[peak_r]:.0f}")
    print(f"limbus        r={limbus_r}  luma={prof[limbus_r]:.0f}")
    print(f"sclera peak   luma {sclera_level:.0f} (at r={peak_r}+)")
    print(f"eyeball edge  r={eyeball_r:.0f}  (per-angle median; border RGB "
          f"{bg_rgb[0]:.0f},{bg_rgb[1]:.0f},{bg_rgb[2]:.0f})")
    print(f"off centre    {off:.1f}px")
    print()
    print(f"framing         {args.framing}")
    print(f"derived IRIS_SIZE = {iris_size}  (limbus radius {limbus_r_plate:.1f}px "
          f"in a {args.sclera_size}px plate)")
    print(f"outputs        sclera {args.sclera_size}x{args.sclera_size} RGB, "
          f"iris {args.iris_w}x{args.iris_h} RGB")
    print(f"variant dir     {out_dir}")

    if problems:
        print("\nWARNINGS:")
        for p in problems:
            print(f"  ! {p}")
    if args.dry_run:
        print("\n--dry-run: nothing written")
        return 0

    recentred, dx, dy = shift_to_centre(img, pupil_cx, pupil_cy)
    print(f"\nrecentred by ({dx:+d}, {dy:+d}) so the pupil sits on the plate centre")

    src = np.array(recentred)
    src_c = ((W - 1) / 2.0, (H - 1) / 2.0)

    blown = highlight_mask(src, src_c[0], src_c[1], eyeball_r)
    src, n_replaced = despeckle(src, src_c[0], src_c[1], int(eyeball_r) + 2, blown)
    if n_replaced:
        pct = 100.0 * n_replaced / src[..., 0].size
        print(f"\ndespeckled {n_replaced} blown highlight texels ({pct:.2f}% of frame)")

    sclera = build_plate(src, src_c[0], src_c[1], args.sclera_size,
                         limbus_r, limbus_r_plate, eyeball_r,
                         bg_angs, bg_stops)
    iris = unroll_iris(src, src_c[0], src_c[1],
                       pupil_r, limbus_r, args.iris_w, args.iris_h)

    os.makedirs(out_dir, exist_ok=True)
    sclera.save(os.path.join(out_dir, "sclera.png"))
    iris.save(os.path.join(out_dir, "iris.png"))
    for f in ("lid-upper.png", "lid-lower.png",
              "lid-upper-symmetrical.png", "lid-lower-symmetrical.png"):
        shutil.copyfile(os.path.join(CONV, REF_VARIANT, f),
                        os.path.join(out_dir, f))

    pupil_arg = ""
    if args.pupil == "slit":
        pm = make_slit_pupil_map(iris_size, args.slit_axis, args.slit_width)
        pm.save(os.path.join(out_dir, "pupilMap.png"))
        pupil_arg = " \\\n           pupilMap.png"
        print(f"\nslit pupil  axis={args.slit_axis}  half-width={args.slit_width} "
              f"-> pupilMap.png ({iris_size}x{iris_size})")

    print(f"\nwrote {out_dir}/")
    for f in sorted(os.listdir(out_dir)):
        p = os.path.join(out_dir, f)
        with Image.open(p) as im:
            print(f"    {f:28s} {im.size[0]}x{im.size[1]}  {im.mode}")
    print(f"\nnext:  python3 Uncanny_Eyes/convert/tablegen.py \\\n"
          f"           sclera.png iris.png \\\n"
          f"           lid-upper-symmetrical.png lid-lower-symmetrical.png \\\n"
          f"           lid-upper.png lid-lower.png \\\n"
          f"           {iris_size}{pupil_arg}   >  src/generated/{args.name}Eye.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())