#!/usr/bin/env python3
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
        bgr = src
        alpha = np.full(src.shape[:2], 255, dtype=np.uint8)
    
    h, w = bgr.shape[:2]
    if w != h:
        print(f"WARNING: input {w}x{h} is not square; cropping to square", file=sys.stderr)
    size = min(w, h)
    bgr = bgr[:size, :size]
    alpha = alpha[:size, :size]
    
    cx = (size - 1) / 2.0
    cy = (size - 1) / 2.0
    R = size / 2.0 - 1.0
    
    out_w = 512   # angle: 0..2pi across columns (matches upstream 512-angle scale)
    out_h = 128   # radius: 0=outer rim .. 127=inner edge (matches tablegen 0..127)
    
    angles = np.linspace(0, 2 * np.pi, out_w, endpoint=False)
    radii = np.linspace(1.0, 0.01, out_h)  # 1.0 = outer, 0.01 = inner
    theta_grid = (radii[:, None] - 0.5) * np.pi  # -pi/2 (top=outer) .. +pi/2 (bottom=inner)
    cos_a = np.cos(angles)[None, :]
    sin_a = np.sin(angles)[None, :]
    
    sx = cx + (theta_grid * np.cos(theta_grid)) * (R / np.pi) * cos_a
    sy = cy + (theta_grid * np.cos(theta_grid)) * (R / np.pi) * sin_a
    
    # OpenCV remap maps destination -> source coordinates (x,y).
    map1 = sx.T.astype(np.float32)
    map2 = (size - 1 - sy.T).astype(np.float32)
    
    bgr32 = bgr.astype(np.float32)
    alpha32 = alpha.astype(np.float32)
    out_bgr = cv2.remap(bgr32, map1, map2, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
    out_alpha = cv2.remap(alpha32, map1, map2, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    out_bgr8 = np.clip(out_bgr, 0, 255).astype(np.uint8)
    out_alpha8 = np.clip(out_alpha, 0, 255).astype(np.uint8)
    
    # Mask away everything outside the original circular eye: only keep pixels whose source was inside the alpha mask.
    # Compute source alpha at each output pixel from a coarse remap as a validity mask.
    out_bgr8[..., 3] = out_alpha8
    out = out_bgr8
    
    # Build a transparency mask: pixels whose mapped source was outside the input circle are transparent.
    # Approximate by checking if the radius in source > R
    dist2 = (sx - cx) ** 2 + (sy - cy) ** 2
    outside = dist2 > (R + 0.5) ** 2
    out[outside.T] = [0, 0, 0, 0]
    
    import PIL.Image
    pil_out = PIL.Image.fromarray(out, "RGBA")
    pil_out.save(png_out)
    print(f"wrote {png_out} ({out_w}x{out_h})")
    
    if csv_out:
        with open(csv_out, "w") as f:
            for row in out:
                f.write(",".join(f"{int(r)},{int(g)},{int(b)},{int(av)}" for r, g, b, av in row) + "\n")
        print(f"wrote {csv_out}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 polarize.py input.png output.png [table.csv]")
        sys.exit(2)
    csv = sys.argv[4] if len(sys.argv) > 4 else None
    polarize(sys.argv[1], sys.argv[2], csv)
