from PIL import Image
import numpy as np

p = '/home/jason/Pictures/drag_no_pupil.png'
im = Image.open(p).convert('RGBA')
a = np.asarray(im)
rgb = a[..., :3].astype(np.float32)
alpha = a[..., 3].astype(np.float32)

# Find white background (likely sclera)
white_mask = (rgb.mean(axis=2) > 230) & (alpha > 230)
# Find dark pupil (likely black)
dark_mask = (rgb.max(axis=2) < 30) & (alpha > 128)

# Find iris region: not white background and not dark pupil
iris_mask = ~white_mask & ~dark_mask

# Find bounding box of iris
ys, xs = np.where(iris_mask)
if len(ys) == 0:
    print("No iris found")
    exit(1)

x_min, x_max = xs.min(), xs.max()
y_min, y_max = ys.min(), ys.max()
print(f"Iris bounding box: ({x_min}, {y_min}) to ({x_max}, {y_max})")
print(f"Size: {x_max - x_min + 1} x {y_max - y_min + 1}")

# Show some stats
print(f"Total pixels: {rgb.shape[0] * rgb.shape[1]}")
print(f"White background pixels: {(white_mask).sum()}")
print(f"Dark pupil pixels: {dark_mask.sum()}")
print(f"Iris pixels: {iris_mask.sum()}")

# Show sample pixels at corners
for y in [y_min, (y_min + y_max)//2, y_max]:
    for x in [x_min, (x_min + x_max)//2, x_max]:
        if 0 <= x < rgb.shape[1] and 0 <= y < rgb.shape[0]:
            pixel = rgb[y, x]
            alpha_val = alpha[y, x]
            print(f"Corner ({x},{y}): RGB={pixel}, alpha={alpha_val}")
