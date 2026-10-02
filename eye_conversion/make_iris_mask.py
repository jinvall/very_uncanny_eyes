#!/usr/bin/env python3
import sys
import cv2
import numpy as np
from PIL import Image

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 make_iris_mask.py input.png output_mask.png")
        sys.exit(2)
    
    src = cv2.imread(sys.argv[1], cv2.IMREAD_UNCHANGED)
    if src is None:
        raise FileNotFoundError(f"could not open {sys.argv[1]}")
    
    if src.ndim == 3 and src.shape[2] == 4:
        bgr, alpha = src[..., :3], src[..., 3]
    else:
        bgr = src
        alpha = np.full(src.shape[:2], 255, dtype=np.uint8)
    
    # Detect white/light background (likely sclera)
    rgb = bgr.astype(np.float32)
    brightness = rgb.mean(axis=2)
    
    # White background mask: very bright, high alpha
    white_mask = (brightness > 230) & (alpha > 230)
    
    # Dark pupil mask: very dark, moderate-high alpha
    dark_mask = (rgb.max(axis=2) < 30) & (alpha > 128)
    
    # Iris mask: everything else
    iris_mask = (~white_mask) & (~dark_mask) & (alpha > 128)
    
    # Create visualization mask (RGB output)
    mask_vis = np.zeros_like(bgr)
    mask_vis[iris_mask] = [255, 255, 255]
    mask_vis[white_mask] = [0, 0, 255]   # red = background
    mask_vis[dark_mask] = [0, 255, 0]    # green = pupil
    
    out = Image.fromarray(cv2.cvtColor(mask_vis, cv2.COLOR_BGR2RGB), 'RGB')
    out.save(sys.argv[2])
    
    # Print stats
    ys, xs = np.where(iris_mask)
    if len(ys) > 0:
        print(f"Iris bbox: ({xs.min()}, {ys.min()}) to ({xs.max()}, {ys.max()})")
        print(f"Iris size: {xs.max() - xs.min() + 1} x {ys.max() - ys.min() + 1}")
    print(f"White: {white_mask.sum()}, Dark: {dark_mask.sum()}, Iris: {iris_mask.sum()}")

if __name__ == "__main__":
    main()
