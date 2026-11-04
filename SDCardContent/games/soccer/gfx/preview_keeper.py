#!/usr/bin/env python3
"""Preview of the keeper poses with the firmware pivot semantics.

Firmware: screen = R(angle) * (local - (w//2, h//2)) + pos, so a bone sprite
that hangs DOWN at angle 0 points along (-sin a, cos a) at angle a.

Writes /tmp/opencode/soccer_ref/keeper_poses.png and prints ASCII.
"""

import math
import struct
import sys

from PIL import Image


def load(path):
    with open(path, "rb") as fh:
        d = fh.read()
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    row = w * 2
    pad = (4 - (row & 3)) & 3
    px = []
    for y in range(h):
        src = off + (h - 1 - y) * (row + pad)
        vals = struct.unpack("<%dH" % w, d[src:src + row])
        px.append([None if v == 0xF81F else (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
                   for v in vals])
    return px


def opaque(px, x, y):
    return 0 <= x < len(px[0]) and 0 <= y < len(px) and px[y][x] is not None


def render(body, parts, sh_dx, sh_dy, l_up, l_fore, ke, dir_, ascii_=True):
    """parts = (upL, upR, foreL, foreR, gloveL, gloveR)"""
    W, H = 130, 130
    canvas = [[None] * W for _ in range(H)] if not ascii_ else [["."] * W for _ in range(H)]

    def draw(px, posx, posy, ang, mark):
        w, h = len(px[0]), len(px)
        c, s = math.cos(ang), math.sin(ang)
        for y in range(h):
            for x in range(w):
                if not opaque(px, x, y):
                    continue
                lx, ly = x - w // 2, y - h // 2
                sx = int(round(c * lx - s * ly + posx))
                sy = int(round(s * lx + c * ly + posy))
                if 0 <= sy < H and 0 <= sx < W:
                    canvas[sy][sx] = mark if ascii_ else px[y][x]

    ax, ay = 65.0, 65.0
    phi = dir_ * 1.0 * ke
    idle = 0.0
    tgt = math.pi if dir_ == 0 else -dir_ * (math.pi / 2 + 0.6)
    c, s = math.cos(phi), math.sin(phi)
    upL, upR, foreL, foreR, glL, glR = parts
    for side in (-1, 1):
        up = upL if side < 0 else upR
        fore = foreL if side < 0 else foreR
        glove = glL if side < 0 else glR
        dx, dy = side * sh_dx, sh_dy
        shx = ax + c * dx - s * dy
        shy = ay + s * dx + c * dy
        ua = idle + (tgt - idle) * ke
        draw(up, shx, shy, ua, "A" if side < 0 else "a")
        ex = shx - l_up * math.sin(ua)
        ey = shy + l_up * math.cos(ua)
        fa = ua - dir_ * 0.35 * ke
        draw(fore, ex, ey, fa, "F" if side < 0 else "f")
        wx = ex - l_fore * math.sin(fa)
        wy = ey + l_fore * math.cos(fa)
        draw(glove, wx, wy, fa, "G")
    draw(body, ax, ay, phi, "#")
    if ascii_:
        return "\n".join("".join(row) for row in canvas)
    img = Image.new("RGB", (W, H), (255, 255, 255))
    for y in range(H):
        for x in range(W):
            if canvas[y][x] is not None:
                img.putpixel((x, y), canvas[y][x])
    return img


if __name__ == "__main__":
    body = load("keeper_body.bmp")
    up = load("arm_up.bmp")
    upr = load("arm_up_r.bmp")
    fore = load("arm_fore.bmp")
    forr = load("arm_fore_r.bmp")
    gl = load("glove.bmp")
    glr = load("glove_r.bmp")
    dx, dy = 8, -10
    if len(sys.argv) > 2:
        dx, dy = int(sys.argv[1]), int(sys.argv[2])
    parts = (up, upr, fore, forr, gl, glr)
    poses = [(0, 0), (1, 1), (1, -1), (1, 0)]
    l_up, l_fore = len(up[0]) // 2, len(fore[0]) // 2
    if "--ascii" in sys.argv:
        for ke, d in poses:
            print(f"ke={ke} dir={d}")
            print(render(body, parts, dx, dy, l_up, l_fore, ke, d))
            print()
    else:
        sheet = Image.new("RGB", (130 * len(poses), 130), (255, 255, 255))
        for i, (ke, d) in enumerate(poses):
            sheet.paste(render(body, parts, dx, dy, l_up, l_fore, ke, d, ascii_=False),
                        (130 * i, 0))
        sheet = sheet.resize((260 * len(poses), 260), Image.NEAREST)
        sheet.save("/tmp/opencode/soccer_ref/keeper_poses.png")
        print("wrote /tmp/opencode/soccer_ref/keeper_poses.png")
