#!/usr/bin/env python3
"""Preview of the keeper poses with the firmware pivot semantics.

Firmware: screen = R(angle) * (local - (w//2, h//2)) + pos, so a bone sprite
that hangs DOWN at angle 0 points along (-sin a, cos a) at angle a.

Mirrors DrawKeeper in init.lua exactly: body first, then leg bones, then the
arm bones on top.

Writes /tmp/opencode/soccer_ref/keeper_poses.png and prints ASCII.
"""

import math
import struct
import sys

from PIL import Image

# keep in sync with init.lua
SHOULDER_DX, SHOULDER_DY = 9, -17
ARM_UP_LEN, ARM_FORE_LEN = 9, 5
HIP_DX, LEG_UP_LEN, LEG_SHIN_LEN = 6, 7, 14
LEG_IDLE_UP_L, LEG_IDLE_SHIN_L = 0.767, 0.377
LEG_IDLE_UP_R, LEG_IDLE_SHIN_R = -0.758, -0.379
LEG_FOLD = 0.45
DIVE_PHI, DIVE_UP, ARM_FOLD = 1.0, 0.6, 0.35


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


def lerp(a, b, t):
    return a + (b - a) * t


def render(spr, ke, dir_, ascii_=True):
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
    phi = dir_ * DIVE_PHI * ke
    c, s = math.cos(phi), math.sin(phi)

    # body first
    draw(spr["body"], ax, ay, phi, "#")

    # legs: idle stance -> trailing in the dive (aligned with the body,
    # trailing shin folded further back)
    thL = lerp(LEG_IDLE_UP_L, phi, ke)
    thR = lerp(LEG_IDLE_UP_R, phi, ke)
    shnL = lerp(LEG_IDLE_SHIN_L, phi + dir_ * LEG_FOLD, ke)
    shnR = lerp(LEG_IDLE_SHIN_R, phi + dir_ * LEG_FOLD, ke)
    for side, th, shn in ((-1, thL, shnL), (1, thR, shnR)):
        hipx, hipy = ax + c * (side * HIP_DX), ay + s * (side * HIP_DX)
        kx = hipx - LEG_UP_LEN * math.sin(th)
        ky = hipy + LEG_UP_LEN * math.cos(th)
        draw(spr["thigh" + ("L" if side < 0 else "R")], hipx, hipy, th, "T")
        draw(spr["shin" + ("L" if side < 0 else "R")], kx, ky, shn, "S")

    # arms on top, fixed outward side mapping
    spread = math.pi / 2 + DIVE_UP
    for side in (-1, 1):
        up = spr["upL" if side < 0 else "upR"]
        fore = spr["foreL" if side < 0 else "foreR"]
        glove = spr["glL" if side < 0 else "glR"]
        dx, dy = side * SHOULDER_DX, SHOULDER_DY
        shx = ax + c * dx - s * dy
        shy = ay + s * dx + c * dy
        ua = spread * ke
        fa = ua + ARM_FOLD * ke
        if side < 0:
            draw(up, shx, shy, ua, "A")
            ex = shx - ARM_UP_LEN * math.sin(ua)
            ey = shy + ARM_UP_LEN * math.cos(ua)
            draw(fore, ex, ey, fa, "F")
            wx = shx - (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fa)
            wy = shy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fa)
            draw(glove, wx, wy, fa, "G")
        else:
            # mirrored arm: bone at angle -a points to the mirrored side,
            # so the screen-left arm swings left and the screen-right arm
            # right regardless of dive direction
            draw(up, shx, shy, -ua, "a")
            ex = shx + ARM_UP_LEN * math.sin(ua)
            ey = shy + ARM_UP_LEN * math.cos(ua)
            draw(fore, ex, ey, -fa, "f")
            wx = shx + (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fa)
            wy = shy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fa)
            draw(glove, wx, wy, -fa, "G")

    if ascii_:
        return "\n".join("".join(row) for row in canvas)
    img = Image.new("RGB", (W, H), (255, 255, 255))
    for y in range(H):
        for x in range(W):
            if canvas[y][x] is not None:
                img.putpixel((x, y), canvas[y][x])
    return img


if __name__ == "__main__":
    spr = dict(
        body=load("keeper_body.bmp"),
        upL=load("arm_up.bmp"), upR=load("arm_up_r.bmp"),
        foreL=load("arm_fore.bmp"), foreR=load("arm_fore_r.bmp"),
        glL=load("glove.bmp"), glR=load("glove_r.bmp"),
        thighL=load("leg_thigh.bmp"), thighR=load("leg_thigh_r.bmp"),
        shinL=load("leg_shin.bmp"), shinR=load("leg_shin_r.bmp"),
    )
    poses = [(0, 0), (1, 1), (1, -1), (1, 0)]
    if "--ascii" in sys.argv:
        for ke, d in poses:
            print(f"ke={ke} dir={d}")
            print(render(spr, ke, d))
            print()
    else:
        sheet = Image.new("RGB", (130 * len(poses), 130), (255, 255, 255))
        for i, (ke, d) in enumerate(poses):
            sheet.paste(render(spr, ke, d, ascii_=False), (130 * i, 0))
        sheet = sheet.resize((260 * len(poses), 260), Image.NEAREST)
        sheet.save("/tmp/opencode/soccer_ref/keeper_poses.png")
        print("wrote /tmp/opencode/soccer_ref/keeper_poses.png")
