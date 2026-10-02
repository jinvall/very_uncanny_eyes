#!/usr/bin/env python3
"""Unroll a circular eye PNG into the Adafruit Uncanny Eyes polar (angle x radius) iris map.

Renderer lookup (from tablegen.py + eye.cpp):
  angle    = (irisMapWidth * (polar >> 7)) / 512   -> column x
  distance = polar & 0x7f                          -> row y
  y=0   = OUTER rim (distance=127 in tablegen)
  y=127 = INNER edge (distance=0)
  x=0..511 = angle 0..2pi (0=left, wraps around)

Output: 512 wide (angle) x 128 tall (radius), RGBA PNG.
"""
import sys
import cv2
import numpy as np
from PIL import Image

def polarize(png_in, png_out, csv_out=None):
    src = cv2.imread(png_in, cv2.IMREAD_UNCHANGED)
    if src is None:
        raise FileNotFoundError(f"could not open {png_in}")

    if src.ndim == 3 and src.shape[2] == 4:
        bgr, alpha = src[..., :3], src[..., 3]
    else:
        bgr, alpha = src, np.full(src.shape[:2], 255, dtype=np.uint8)

    h, w = bgr.shape[:2]
    if w != h:
        print(f"WARNING: input {w}x{h} not square; cropping to square", file=sys.stderr)
    size = min(w, h)
    bgr = bgr[:size, :size]
    alpha = alpha[:size, :size]

    cx = (size - 1) / 2.0
    cy = (size - 1) / 2.0
    R = size / 2.0 - 1.0

    out_w, out_h = 512, 128
    angles = np.linspace(0, 2 * np.pi, out_w, endpoint=False)
    # Radii: 0 = inner (center), 1 = outer (rim)
    # Mapping to Adafruit expectation:
    #   y=0 (row 0) -> outer rim
    #   y=127 (row 127) -> inner edge
    radii = np.linspace(1.0, 0.0, out_h)
    theta_grid = radii[None, :] * R 

    cos_a = np.cos(angles)[:, None]
    sin_a = np.sin(angles)[:, None]

    # source coords for each output pixel
    sx = cx + theta_grid * cos_a
    sy = cy + theta_grid * sin_a

    bgr32 = bgr.astype(np.float32)
    alpha32 = alpha.astype(np.float32)

    # Remap expects map1 for x, map2 for y
    out_bgr = cv2.remap(bgr32, sx.astype(np.float32), sy.astype(np.float32), cv2.INTER_LINEAR,
                        borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
    out_alpha = cv2.remap(alpha32, sx.astype(np.float32), sy.astype(np.float32), cv2.INTER_LINEAR,
                          borderMode=cv2.BORDER_CONSTANT, borderValue=0)

    out_bgr8 = np.clip(out_bgr, 0, 255).astype(np.uint8)
    out_alpha8 = np.clip(out_alpha, 0, 255).astype(np.uint8)

    # Reassemble RGBA
    # cv2.remap returns (W=out_w, H=out_h) = (512, 128) because maps are (W, H)
    out = np.zeros((out_w, out_h, 4), dtype=np.uint8)
    out[..., :3] = out_bgr8
    out[..., 3] = out_alpha8
    # Final image must be (radius=128, angle=512) for Adafruit format
    out = np.transpose(out, (1, 0, 2))

    pil_out = Image.fromarray(out, 'RGBA')
    pil_out.save(png_out)
    print(f"wrote {png_out} ({out_w}x{out_h})")

    if csv_out:
        with open(csv_out, 'w') as f:
            for y in range(out_h):
                for x in range(out_w):
                    r, g, b, a = out[y, x]
                    f.write(f"{r},{g},{b},{a}\n")
        print(f"wrote {csv_out}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 polarize_fixed.py input.png output.png [table.csv]")
        sys.exit(2)
    csv = sys.argv[3] if len(sys.argv) > 3 else None
    polarize(sys.argv[1], sys.argv[2], csv)
