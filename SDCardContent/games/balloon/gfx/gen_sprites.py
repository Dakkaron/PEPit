#!/usr/bin/env python3
"""Pixel-art sprite generator for the Hot Air Balloon game.

Writes 16-bit RGB565 BMPs (BI_BITFIELDS) with pink (0xF81F) as the
transparent masking colour, matching what the firmware BMP loader and
LoadSprite(..., 0, 0xf81f) expect.

Run:  python3 gen_sprites.py
"""

import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))

PINK = (255, 0, 255)  # 0xF81F -> masking colour

OUTLINE = (34, 26, 32)
RED = (214, 52, 46)
RED_HI = (240, 86, 72)
ORANGE = (248, 132, 48)
ORANGE_HI = (252, 168, 72)
ROPE = (204, 198, 188)
BASKET = (122, 82, 44)
BASKET_HI = (158, 112, 62)
BASKET_DK = (82, 52, 28)
FLAME_OUT = (250, 116, 30)
FLAME_MID = (252, 196, 60)
FLAME_IN = (252, 240, 150)
DEAD = (128, 132, 138)
DEAD_DK = (96, 100, 106)
STORM = (92, 104, 140)
STORM_LO = (66, 78, 104)
STORM_EDGE = (46, 54, 76)
WING = (58, 58, 60)
WING_HI = (106, 108, 112)
BIRD_BODY = (232, 240, 244)
BIRD_BEAK = (242, 190, 72)
BIRD_OUT = (24, 32, 34)
GLASS = (214, 224, 234)
GLASS_DK = (140, 152, 164)
GOLD = (232, 200, 90)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[PINK for _ in range(w)] for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else PINK

    def opaque(self, x, y):
        return self.get(x, y) != PINK

    def paste(self, other, ox=0, oy=0):
        for y in range(other.h):
            for x in range(other.w):
                if other.px[y][x] != PINK:
                    self.set(ox + x, oy + y, other.px[y][x])

    def disc(self, cx, cy, r, c):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.set(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.set(x, y, c)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        steps = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(steps + 1):
            self.set(round(x0 + (x1 - x0) * i / steps), round(y0 + (y1 - y0) * i / steps), c)

    def polygon(self, pts, c):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        for y in range(min(ys), max(ys) + 1):
            crossings = []
            for i in range(len(pts)):
                x1, y1 = pts[i]
                x2, y2 = pts[(i + 1) % len(pts)]
                if (y1 <= y < y2) or (y2 <= y < y1):
                    crossings.append(x1 + (y - y1) * (x2 - x1) / (y2 - y1))
            crossings.sort()
            for i in range(0, len(crossings) - 1, 2):
                for x in range(int(round(crossings[i])), int(round(crossings[i + 1])) + 1):
                    self.set(x, y, c)

    def outline(self, c, width=1, min_neighbours=0):
        """Trace a hard border around every opaque blob (classic pixel-art edge).

        min_neighbours skips thin protrusions, which a 1px border would eat.
        """
        edge = [(x, y) for y in range(self.h) for x in range(self.w)
                if self.opaque(x, y) and any(not self.opaque(x + dx, y + dy)
                                             for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                and sum(1 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                        if self.opaque(x + dx, y + dy)) >= min_neighbours]
        for x, y in edge:
            self.set(x, y, c)

    def bbox(self):
        ys = [y for y in range(self.h) if any(self.px[y][x] != PINK for x in range(self.w))]
        xs = [x for x in range(self.w) if any(self.px[y][x] != PINK for y in range(self.h))]
        return min(xs), min(ys), max(xs), max(ys)

    def rows(self):
        return (c for row in self.px for c in row)

    def preview(self):
        pal = {}
        chars = ".#*+oO@~"
        out = []
        for y in range(self.h):
            line = ""
            for x in range(self.w):
                c = self.px[y][x]
                if c == PINK:
                    line += "."
                else:
                    if c not in pal:
                        pal[c] = chars[len(pal) % len(chars)]
                    line += pal[c]
            out.append(line)
        return "\n".join(out)


def balloon(can_crash, c1=RED, c2=ORANGE, c1h=RED_HI, c2h=ORANGE_HI):
    """44x52 envelope + ropes + basket. Envelope centre is (22, 17).

    c1/c2 are the two envelope band colours; c1h/c2h the highlight variants.
    """
    c = Canvas(44, 52)
    cx, cy, R = 22, 17, 17
    for y in range(cy - R, cy + R + 1):
        dy = y - cy
        if dy <= 0:
            hw = math.sqrt(R * R - dy * dy)
        elif dy >= R - 3:
            hw = 4.0
        else:
            hw = max(4.0, math.sqrt(max(0.0, R * R - (dy * 1.2) ** 2)) * (1 - dy / 26))
        for x in range(int(cx - hw), int(cx + hw) + 1):
            band = ((x - cx + R) // 5) % 2
            col = c2 if band else c1
            if y < cy - 4 and abs(x - (cx - 7)) <= 2:
                col = c2h if band else c1h
            c.set(x, y, col)
    # neck where the burner sits
    c.rect(cx - 3, cy + R - 1, cx + 3, cy + R + 3, DEAD_DK if can_crash else BASKET_DK)
    # ropes (2 px wide so they survive the outline pass)
    c.line(cx - 4, cy + R + 3, cx - 7, cy + R + 9, ROPE)
    c.line(cx - 3, cy + R + 3, cx - 6, cy + R + 9, ROPE)
    c.line(cx + 4, cy + R + 3, cx + 7, cy + R + 9, ROPE)
    c.line(cx + 3, cy + R + 3, cx + 6, cy + R + 9, ROPE)
    # basket
    bx0, bx1, by0, by1 = cx - 8, cx + 8, cy + R + 9, cy + R + 16
    c.rect(bx0, by0, bx1, by1, BASKET)
    c.rect(bx0, by0, bx1, by0, BASKET_HI)
    for x in range(bx0 + 2, bx1, 4):
        c.line(x, by0 + 1, x, by1, BASKET_DK)
    if can_crash:
        for y in range(cy - R, cy + R + 1):
            for x in range(c.w):
                if c.opaque(x, y):
                    c.px[y][x] = DEAD if c.get(x, y) in (RED, RED_HI) else DEAD_DK
        # a torn envelope: hole on the upper right
        c.disc(cx + 8, cy - 6, 4, PINK)
    c.outline(OUTLINE)
    return c


def flame():
    """Animated 14x10 sprite, 2 frames: burner flame in the neck gap.

    The base is at the bottom (the burner above the basket) and the tip
    points up into the envelope neck, so the flame fits between the
    envelope bottom and the basket instead of covering the envelope.
    """
    H = 10
    def taper(c, cx, base, h, color):
        for dy in range(h):
            hw = max(1, int(base * (1 - dy / h)))
            c.rect(cx - hw, H - 1 - dy, cx + hw, H - 1 - dy, color)

    frames = []
    for f in range(2):
        c = Canvas(14, H)
        k = 1.0 if f == 0 else 0.8
        taper(c, 7, int(5 * k), H, FLAME_OUT)
        taper(c, 7, int(3 * k), H - 2, FLAME_MID)
        taper(c, 7, 1, 4, FLAME_IN)
        frames.append(c)
    return frames


def cloud():
    """34x20 angry storm cloud, centre (17, 10): dark tumbling grey-blue."""
    c = Canvas(34, 20)
    c.disc(17, 9, 8, STORM)
    c.disc(9, 11, 6, STORM)
    c.disc(25, 11, 6, STORM)
    c.disc(13, 6, 6, STORM)
    c.disc(21, 7, 5, STORM)
    # darker, jagged underbelly
    for x in range(34):
        column = [y for y in range(20) if c.opaque(x, y)]
        if not column:
            continue
        low = max(column)
        for y in range(low - 3, low + 1):
            if c.opaque(x, y):
                c.px[y][x] = STORM_LO if (x % 2 or y < low) else STORM_EDGE
    c.outline(STORM_EDGE)
    return c


def bird():
    """Animated gull assembled from head, torso, tail and two wing polygons.

    Every frame transforms the same part geometry, so the wingbeat is a rigid
    rotation and foreshortening about the shoulder rather than four redraws.
    """
    W, H = 32, 20
    body_pts = [(-8, -2), (6, -3), (10, -1), (6, 2), (-6, 2)]
    tail_pts = [(3, -2), (9, -3), (10, 0), (4, 1)]
    wing_pts = [(0, -1), (11, -2), (14, 0), (14, 1), (4, 3), (0, 1)]
    wing_hi = [(0, -1), (11, -2), (14, 0), (7, 0), (0, 0)]
    neck_pts = [(-2, -2), (2, -2), (2, 2), (-2, 2)]
    flap = math.radians(34.0)

    def limb(pts, angle, anchor, sx=1.0, sy=1.0):
        ca, sa = math.cos(angle), math.sin(angle)
        return [(round(anchor[0] + (x * sx) * ca - (y * sy) * sa),
                 round(anchor[1] + (x * sx) * sa + (y * sy) * ca)) for x, y in pts]

    def head(bob, tilt):
        c = Canvas(W, H)
        c.disc(6, 7 + bob, 3, BIRD_BODY)
        c.polygon(limb(neck_pts, tilt, (9, 8 + bob)), BIRD_BODY)
        c.polygon([(3, 6 + bob), (0, 7 + bob), (3, 8 + bob)], BIRD_BEAK)
        return c

    def torso(bob, tilt):
        c = Canvas(W, H)
        c.polygon(limb(body_pts, tilt, (14, 11 + bob)), BIRD_BODY)
        c.polygon(limb(tail_pts, tilt, (21, 11 + bob)), BIRD_BODY)
        return c

    def wing(bob, angle, dark, sx=1.0, sy=1.0, ox=0, oy=0):
        c = Canvas(W, H)
        c.polygon(limb(wing_pts, angle, (14 + ox, 9 + oy + bob), sx, sy), WING)
        if not dark:
            c.polygon(limb(wing_hi, angle, (14 + ox, 9 + oy + bob), sx, sy), WING_HI)
        return c

    frames = []
    for k in range(4):
        # sampling the beat a quarter period off centre gives four distinct
        # poses instead of the two duplicate pairs a zero-phase cosine yields
        cycle = math.cos(2 * math.pi * k / 4 + math.radians(30.0))
        angle = cycle * flap
        bob = int(round(-2 * cycle))
        # the wing is broad on the downstroke and folds on the upstroke
        sy = 1.0 if cycle > 0 else 0.6
        c = Canvas(W, H)
        # both wings beat in phase; the far one is smaller, darker and offset
        # behind the body, which is what makes the flap read as depth
        c.paste(wing(bob, angle, True, 0.75, 0.6 * sy, 2, 1))
        c.paste(torso(bob, angle * 0.25))
        c.paste(head(bob, -angle * 0.3))
        c.paste(wing(bob, angle, False, 1.0, sy))
        c.outline(BIRD_OUT)
        # the outline pass would otherwise reduce the 2px beak to a dark dash
        c.polygon([(3, 6 + bob), (0, 7 + bob), (3, 8 + bob)], BIRD_BEAK)
        frames.append(c)

    x0 = min(f.bbox()[0] for f in frames)
    y0 = min(f.bbox()[1] for f in frames)
    x1 = max(f.bbox()[2] for f in frames)
    y1 = max(f.bbox()[3] for f in frames)
    cropped = []
    for f in frames:
        c = Canvas(x1 - x0 + 1, y1 - y0 + 1)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                c.set(x - x0, y - y0, f.get(x, y))
        cropped.append(c)
    return cropped


def gauge():
    """22x52 glass tube frame; interior stays transparent."""
    c = Canvas(22, 52)
    c.rect(0, 0, 21, 51, GLASS_DK)
    c.rect(1, 1, 20, 50, GLASS)
    c.rect(3, 3, 18, 48, PINK)
    for y in range(4, 49, 9):
        c.rect(2, y, 3, y, GLASS_DK)
        c.rect(19, y, 20, y, GLASS_DK)
    c.rect(1, 49, 20, 50, GOLD)
    c.rect(1, 1, 20, 2, GOLD)
    c.outline(OUTLINE)
    return c


def sky():
    """320x167 playfield background: smooth sky, heavily blurred haze, ground."""
    W, H = 320, 167
    c = Canvas(W, H)
    top = (24, 92, 158)
    bot = (150, 202, 236)
    # smooth per-pixel vertical gradient (no hard bands)
    for y in range(H):
        t = y / (H - 1)
        col = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        c.rect(0, y, W - 1, y, col)
    # distant haze: wide ellipses with a soft gaussian falloff, then blurred
    # away so they read as mist and never as cloud obstacles
    haze = (220, 233, 246)
    for cx, cy, rx, ry in ((56, 66, 26, 11), (120, 50, 20, 9),
                           (196, 80, 28, 12), (258, 62, 18, 8)):
        for y in range(cy - ry - 2, cy + ry + 3):
            for x in range(cx - rx - 2, cx + rx + 3):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d >= 1:
                    continue
                w = (1 - d) ** 2 * 0.55
                bg = c.px[y][x]
                c.set(x, y, tuple(int(bg[i] + (haze[i] - bg[i]) * w)
                                  for i in range(3)))
    # box-blur the sky (two passes) so the haze merges into the gradient
    for _ in range(2):
        src = [row[:] for row in c.px]
        for y in range(H):
            for x in range(W):
                acc = [0, 0, 0]
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < H and 0 <= xx < W:
                            n += 1
                            for i in range(3):
                                acc[i] += src[yy][xx][i]
                c.set(x, y, tuple(v // n for v in acc))
    # sun, top right (pixel-art disc with chunky rays), crisp on the haze
    c.disc(304, 34, 8, GOLD)
    c.disc(304, 34, 5, (252, 232, 130))
    for dx, dy in ((-11, 0), (11, 0), (0, -11), (0, 11)):
        c.rect(304 + dx - 1, 34 + dy - 1, 304 + dx + 1, 34 + dy + 1, GOLD)
    # ground strip at the bottom: flat, the moving tufts drawn by
    # mainGame.lua are the only texture so the scroll is visible
    gy = H - 24
    c.rect(0, gy, W - 1, H - 1, (96, 132, 74))
    c.rect(0, gy, W - 1, gy + 1, (150, 186, 120))
    return c


def tuft():
    """12x10 grass clump drawn on the ground strip so its motion is visible."""
    c = Canvas(12, 10)
    for x in range(12):
        h = [4, 7, 3, 6, 4, 8, 3, 7, 4, 6, 3, 5][x]
        c.line(x, 9, x, 9 - h, (58, 84, 44))
    c.rect(0, 8, 11, 9, (124, 160, 96))
    return c


def logo():
    """40x40 selection-page icon: the balloon, scaled down."""
    c = Canvas(40, 40)
    cx, cy, R = 19, 14, 11
    for y in range(cy - R, cy + R + 1):
        dy = y - cy
        if dy <= 0:
            hw = math.sqrt(R * R - dy * dy)
        elif dy >= R - 2:
            hw = 3.0
        else:
            hw = max(3.0, math.sqrt(max(0.0, R * R - (dy * 1.2) ** 2)) * (1 - dy / 18))
        for x in range(int(cx - hw), int(cx + hw) + 1):
            col = ORANGE if ((x - cx + R) // 4) % 2 else RED
            c.set(x, y, col)
    c.rect(cx - 2, cy + R - 1, cx + 2, cy + R + 2, BASKET_DK)
    c.line(cx - 3, cy + R + 2, cx - 5, cy + R + 6, ROPE)
    c.line(cx + 3, cy + R + 2, cx + 5, cy + R + 6, ROPE)
    bx0, bx1, by0, by1 = cx - 6, cx + 6, cy + R + 6, cy + R + 11
    c.rect(bx0, by0, bx1, by1, BASKET)
    c.rect(bx0, by0, bx1, by0, BASKET_HI)
    c.rect(cx - 1, cy + R, cx + 1, cy + R + 1, FLAME_MID)
    c.outline(OUTLINE)
    return c


def write_bmp(path, frames, frame_w, frame_h):
    """16-bit RGB565, BI_BITFIELDS, rows bottom-up, rows padded to 4 bytes.

    Animation frames are stacked vertically. DrawSprite indexes frame n at
    buffer row n*frameH, and buffer row 0 is the last line written to a
    bottom-up BMP, so frame 0 must be written last: it ends up as the top
    block of the image.
    """
    row_bytes = frame_w * 2
    padding = (4 - (row_bytes & 3)) & 3
    data = bytearray()
    for f in reversed(frames):
        for row in range(frame_h - 1, -1, -1):
            for x in range(frame_w):
                r, g, b = f.px[row][x]
                v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                data += struct.pack("<H", v)
            data += bytes(padding)
    offset = 54 + 12  # BITMAPINFOHEADER + BI_BITFIELDS masks
    header = struct.pack("<2sIII", b"BM", offset + len(data), 0, offset)
    header += struct.pack("<IiiHHIIiiII", 40, frame_w, frame_h * len(frames), 1, 16, 3,
                          len(data), 2835, 2835, 0, 0)
    header += struct.pack("<III", 0x7C00, 0x03E0, 0x001F)
    with open(path, "wb") as fh:
        fh.write(header + data)


if __name__ == "__main__":
    c = balloon(False)
    write_bmp("balloon.bmp", [c], 44, 52)
    c = balloon(True)
    write_bmp("balloon_dead.bmp", [c], 44, 52)
    write_bmp("balloon_blue.bmp", [balloon(False, (52, 108, 214), (96, 160, 240),
                                           (86, 150, 240), (130, 196, 252))], 44, 52)
    write_bmp("balloon_green.bmp", [balloon(False, (44, 164, 72), (92, 212, 108),
                                            (74, 196, 100), (126, 236, 140))], 44, 52)
    write_bmp("balloon_gold.bmp", [balloon(False, (244, 196, 48), (252, 228, 108),
                                           (252, 212, 72), (255, 240, 148))], 44, 52)
    write_bmp("tuft.bmp", [tuft()], 12, 10)
    write_bmp("flame.bmp", flame(), 14, 10)
    write_bmp("cloud.bmp", [cloud()], 34, 20)
    frames = bird()
    write_bmp("bird.bmp", frames, frames[0].w, frames[0].h)
    write_bmp("gauge.bmp", [gauge()], 22, 52)
    write_bmp("sky.bmp", [sky()], 320, 167)
    write_bmp("../logo.bmp", [logo()], 40, 40)
    print("balloon.bmp")
    print(balloon(False).preview())
