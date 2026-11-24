#!/usr/bin/env python3
"""Generate demo BMP assets for the 3ddemo lua game (24-bit uncompressed BMP)."""
import struct, os

def write_bmp(path, w, h, pixel_fn):
    row_size = (w * 3 + 3) & ~3
    file_size = 54 + row_size * h
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", file_size, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, row_size * h, 0, 0, 0, 0))
        for y in range(h - 1, -1, -1):
            row = bytearray()
            for x in range(w):
                r, g, b = pixel_fn(x, y)
                row += bytes((b, g, r))
            row += bytes(row_size - len(row))
            f.write(bytes(row))

def bricks(x, y):
    # 64x64 brick wall pattern, 8 bricks per row, mortar lines
    bh, bw = 16, 32
    row = y // bh
    offset = (row % 2) * (bw // 2)
    bx = (x + offset) % bw
    by = y % bh
    if bx == 0 or by == 0:
        return (180, 170, 160)  # mortar
    # slight per-brick color variation
    brick_id = ((x + offset) // bw) * 7 + row * 13
    shade = 150 + (brick_id * 37) % 60
    r = min(255, shade + 40)
    g = int(shade * 0.55)
    b = int(shade * 0.35)
    if by == bh - 1 or bx == bw - 1:
        r, g, b = r * 3 // 4, g * 3 // 4, b * 3 // 4
    return (r, g, b)

def logo(x, y):
    # 64x64: dark background with a lit cube silhouette
    cx, cy = 32, 32
    dx, dy = abs(x - cx), abs(y - cy)
    if dx + dy < 18:
        light = 255 - (dx + dy) * 7
        return (light, int(light * 0.75), int(light * 0.3))
    if dx < 14 and dy < 14:
        return (90, 60, 30)
    return (10, 10, 20)

base = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(base, "..", "SDCardContent", "games", "3ddemo")
os.makedirs(os.path.join(out, "models"), exist_ok=True)
write_bmp(os.path.join(out, "models", "bricks.bmp"), 64, 64, bricks)
write_bmp(os.path.join(out, "logo.bmp"), 64, 64, logo)
print("done")
