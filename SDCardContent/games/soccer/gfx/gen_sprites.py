#!/usr/bin/env python3
"""Pixel-art sprite generator for the Soccer Penalty game.

Builds 16-bit RGB565 BMPs (BI_BITFIELDS) with pink (0xF81F) as the
transparent masking colour, matching the firmware BMP loader and
LoadSprite(..., 0, 0xf81f).

The keeper is reassembled as a skeleton from the reference drawing
(1793739452570.jpg), which already ships with separated bones: head+
torso+legs as one piece, upper arms, forearms and gloves as separate
parts. Each part is padded so its joint sits exactly at the sprite
centre, which is the pivot DrawSprite rotates around (ALIGN_*_CENTER).

Run:  python3 gen_sprites.py
"""

import math
import os
import struct
from collections import deque

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

PINK = (255, 0, 255)  # masking colour

# Goal geometry, matching init.lua (inside edges 92..228 x 58..118, 4px posts)
GOAL_X0, GOAL_X1 = 92, 228
GOAL_TOP, GOAL_BOTTOM = 58, 118
POST_W = 4

COL_FRAME = (200, 208, 220)
COL_FRAME_DK = (148, 156, 172)
COL_FRAME_HI = (214, 224, 238)
COL_NECK = (184, 133, 107)

# Not pure white: the rulebot's ball detector looks for the only solid
# white blob on the pitch, so gloves must stay clearly off-white.
COL_GLOVE = (216, 224, 236)
COL_GLOVE_DK = (180, 190, 204)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[PINK for _ in range(w)] for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def opaque(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h and self.px[y][x] != PINK

    def bbox(self):
        pts = [(x, y) for y in range(self.h) for x in range(self.w) if self.px[y][x] != PINK]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return min(xs), min(ys), max(xs), max(ys)

    def preview(self):
        out = []
        for y in range(self.h):
            out.append("".join("." if self.px[y][x] == PINK else "#" for x in range(self.w)))
        return "\n".join(out)


def part_canvas(pixels, mask):
    """Canvas holding a component's pixels, cropped to its bounding box."""
    ys, xs = np.nonzero(mask)
    if len(ys) == 0:
        return None
    y0, x0 = int(ys.min()), int(xs.min())
    c = Canvas(int(xs.max()) - x0 + 1, int(ys.max()) - y0 + 1)
    for y, x in zip(ys, xs):
        c.px[y - y0][x - x0] = tuple(int(v) for v in pixels[y, x])
    return c


def rescale(canvas, s):
    w = max(1, round(canvas.w * s))
    h = max(1, round(canvas.h * s))
    c = Canvas(w, h)
    for y in range(h):
        sy = min(canvas.h - 1, int(y / s))
        for x in range(w):
            sx = min(canvas.w - 1, int(x / s))
            c.px[y][x] = canvas.px[sy][sx]
    return c


def pivot_canvas(canvas, jx, jy):
    """Re-crop so the joint pixel (jx, jy) sits at the exact canvas centre."""
    x0, y0, x1, y1 = canvas.bbox()
    m = max(jx - x0, x1 - jx, jy - y0, y1 - jy)
    c = Canvas(2 * m + 1, 2 * m + 1)
    for y in range(canvas.h):
        for x in range(canvas.w):
            if canvas.px[y][x] != PINK:
                c.px[y - jy + m][x - jx + m] = canvas.px[y][x]
    return c


def mirror(canvas):
    c = Canvas(canvas.w, canvas.h)
    for y in range(canvas.h):
        for x in range(canvas.w):
            c.px[y][canvas.w - 1 - x] = canvas.px[y][x]
    return c


def recolor(canvas, test, color):
    for y in range(canvas.h):
        for x in range(canvas.w):
            if canvas.px[y][x] != PINK and test(canvas.px[y][x]):
                canvas.px[y][x] = color


def load_keeper(scale_target):
    img = Image.open(os.path.join(HERE, "1793739452570.jpg")).convert("RGB")
    a = np.array(img)
    h, w = a.shape[:2]

    # Flood-fill the connected near-white background starting from the border.
    bg = np.all(a > 235, axis=2)
    vis = np.zeros((h, w), bool)
    dq = deque()
    for x in range(w):
        for y in (0, h - 1):
            if bg[y, x] and not vis[y, x]:
                vis[y, x] = True
                dq.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if bg[y, x] and not vis[y, x]:
                vis[y, x] = True
                dq.append((y, x))
    while dq:
        y, x = dq.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and bg[ny, nx] and not vis[ny, nx]:
                vis[ny, nx] = True
                dq.append((ny, nx))

    obj = ~vis
    lab = np.zeros((h, w), np.int32)
    cur = 0
    comps = {}
    for y in range(h):
        for x in range(w):
            if obj[y, x] and lab[y, x] == 0:
                cur += 1
                dq.append((y, x))
                lab[y, x] = cur
                while dq:
                    yy, xx = dq.popleft()
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = yy + dy, xx + dx
                        if 0 <= ny < h and 0 <= nx < w and obj[ny, nx] and lab[ny, nx] == 0:
                            lab[ny, nx] = cur
                            dq.append((ny, nx))
                comps[cur] = int((lab == cur).sum())

    order = [k for k in sorted(comps, key=lambda k: -comps[k]) if comps[k] > 5000]
    body_lbl, head_lbl = order[0], order[1]
    bys, bxs = np.nonzero(lab == body_lbl)
    body_cx = (int(bxs.min()) + int(bxs.max())) / 2
    # The four remaining parts, left to right: L upper, L fore, L glove,
    # R upper, R fore, R glove (identified by x position of their bbox).
    arms = {}
    for lbl in order[2:]:
        ys, xs = np.nonzero(lab == lbl)
        arms[lbl] = (int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max()))
    # Pair by side, then top (upper arm) vs middle (forearm) vs bottom (glove).
    side_parts = {}
    for lbl, (x0, x1, y0, y1) in arms.items():
        cx = (x0 + x1) / 2
        side_parts.setdefault("L" if cx < body_cx else "R", []).append((lbl, y0, y1))
    parts = {}
    for side, lst in side_parts.items():
        lst.sort(key=lambda t: t[1])
        parts[side + "_up"] = lst[0][0]
        parts[side + "_fore"] = lst[1][0]
        parts[side + "_glove"] = lst[2][0]

    y_top = min(arms[l][2] for l in parts.values())
    y_bot = int(np.nonzero(lab == body_lbl)[0].max())
    keeper_h = y_bot - y_top + 1
    s = scale_target / keeper_h

    # Body = torso + head, neck bridged.
    body_mask = (lab == body_lbl) | (lab == head_lbl)
    body = part_canvas(a, body_mask)
    b_ys, b_xs = np.nonzero(body_mask)
    by0, bx0 = int(b_ys.min()), int(b_xs.min())
    head_ys = np.nonzero(lab == head_lbl)[0]
    head_xs = np.nonzero(lab == head_lbl)[1]
    torso_ys = np.nonzero(lab == body_lbl)[0]
    torso_xs = np.nonzero(lab == body_lbl)[1]
    hb, tt = int(head_ys.max()), int(torso_ys.min())
    hx0, hx1 = int(head_xs[head_ys == hb].min()), int(head_xs[head_ys == hb].max())
    tx0, tx1 = int(torso_xs[torso_ys == tt].min()), int(torso_xs[torso_ys == tt].max())
    for y in range(hb + 1, tt):
        t = (y - hb) / (tt - hb)
        for x in range(int(round(hx0 + (tx0 - hx0) * t)), int(round(hx1 + (tx1 - hx1) * t)) + 1):
            body.px[y - by0][x - bx0] = COL_NECK

    body = rescale(body, s)
    bx0, by0, bx1, by1 = body.bbox()
    ax, ay = (bx0 + bx1) // 2, (by0 + by1) // 2
    body = pivot_canvas(body, ax, ay)

    def arm_part(lbl, glove=False):
        c = part_canvas(a, lab == lbl)
        c = rescale(c, s)
        x0, y0, x1, y1 = c.bbox()
        top_xs = [x for x in range(x0, x1 + 1) if c.opaque(x, y0)]
        jx = (top_xs[0] + top_xs[-1]) // 2
        length = y1 - y0
        c = pivot_canvas(c, jx, y0)
        if glove:
            recolor(c, lambda p: min(p) > 190, COL_GLOVE)
            recolor(c, lambda p: 150 < min(p) <= 190, COL_GLOVE_DK)
        return c, length

    up_l, len_up = arm_part(parts["L_up"])
    fore_l, len_fore = arm_part(parts["L_fore"])
    glove_l, len_glove = arm_part(parts["L_glove"], glove=True)
    up_r = mirror(up_l)
    fore_r = mirror(fore_l)
    glove_r = mirror(glove_l)

    # Shoulder joints: torso edge inset 2px, 4 rows below the torso top
    # (the merged canvas top row is the head).
    torso_top = next(y for y in range(body.h)
                     if sum(1 for x in range(body.w) if body.opaque(x, y)) >= 18)
    row = torso_top + 4
    xs = [x for x in range(body.w) if body.opaque(x, row)]
    sh_dx = (xs[0] + xs[-1]) // 2 - ax - 2
    sh_dy = row - ay
    return body, (up_l, up_r), (fore_l, fore_r), (glove_l, glove_r), \
        sh_dx, sh_dy, len_up, len_fore, len_glove


def goal_sprite():
    w = (GOAL_X1 - GOAL_X0) + 2 * POST_W
    h = (GOAL_BOTTOM - GOAL_TOP) + POST_W
    c = Canvas(w, h)
    for x in range(w):
        for y in range(POST_W):
            c.px[y][x] = COL_FRAME_HI if y < 2 else COL_FRAME
    for y in range(POST_W, h):
        for x in range(POST_W):
            c.px[y][x] = COL_FRAME_HI if x < 2 else COL_FRAME_DK
        for x in range(w - POST_W, w):
            c.px[y][x] = COL_FRAME if x < w - 2 else COL_FRAME_DK
    # Net mesh: 1px grid lines every 10 px (same anchors as the old
    # primitive lines) plus a sparser half-offset line for texture.
    for x in range(POST_W + 10, w - POST_W, 10):
        for y in range(POST_W, h):
            c.px[y][x] = COL_FRAME
    for y in range(POST_W + 10, h, 10):
        for x in range(POST_W, w - POST_W):
            c.px[y][x] = COL_FRAME
    for x in range(POST_W + 5, w - POST_W, 10):
        for y in range(POST_W + 5, h, 10):
            c.px[y][x] = COL_FRAME_DK
    return c, w, h


def background():
    img = Image.open(os.path.join(HERE, "copilot_image_1785244401961.jpeg")).convert("RGB")
    w, h = img.size
    # Rows ~205..470 are the stands (crowd), ~540..1024 the grass.
    crowd = img.crop((0, 205, w, 470)).resize((320, 68), Image.NEAREST)
    grass = img.crop((0, 540, w, h)).resize((320, 122), Image.NEAREST)
    c = Canvas(320, 190)
    ca, ga = np.array(crowd), np.array(grass)
    # Clamp near-white speckles/stripes: the rulebot finds the ball as the
    # only solid white blob on the pitch.
    def safe(p):
        return tuple(min(v, 214) if min(p) > 170 else int(v) for v in p)
    for y in range(68):
        for x in range(320):
            c.px[y][x] = safe(ca[y][x])
    for y in range(122):
        for x in range(320):
            c.px[68 + y][x] = safe(ga[y][x])
    return c


def ball_sprite(r=8, frames=4):
    img = Image.open(os.path.join(HERE, "copilot_image_1793564531376.png")).convert("RGB")
    a = np.array(img)
    ys, xs = np.nonzero(np.max(a, axis=2) > 120)
    crop = img.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    d = 2 * r
    crop = crop.resize((d, d), Image.LANCZOS)
    a = np.array(crop)
    base = [[(255, 255, 255) if min(a[y][x]) > 150 else tuple(int(v) for v in a[y][x])
             if max(a[y][x]) > 45 else PINK for x in range(d)] for y in range(d)]
    out = []
    for f in range(frames):
        ang = f * math.pi / (2 * frames) * 2  # 45 degrees per frame
        c = Canvas(d, d)
        for y in range(d):
            for x in range(d):
                cx, cy = x - r + 0.5, y - r + 0.5
                nx = cx * math.cos(ang) + cy * math.sin(ang)
                ny = -cx * math.sin(ang) + cy * math.cos(ang)
                sx, sy = int(nx + r - 0.5), int(ny + r - 0.5)
                if 0 <= sx < d and 0 <= sy < d:
                    c.px[y][x] = base[sy][sx]
        out.append(c)
    return out


def write_bmp(path, frames, frame_w, frame_h):
    """16-bit RGB565, BI_BITFIELDS, rows bottom-up, rows padded to 4 bytes.

    Animation frames are stacked vertically; frame 0 is written last so it
    ends up as the top block of the bottom-up image (matching DrawSprite's
    frame indexing, which counts buffer rows from the top).
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
    offset = 54 + 12
    header = struct.pack("<2sIII", b"BM", offset + len(data), 0, offset)
    header += struct.pack("<IiiHHIIiiII", 40, frame_w, frame_h * len(frames), 1, 16, 3,
                          len(data), 2835, 2835, 0, 0)
    header += struct.pack("<III", 0x7C00, 0x03E0, 0x001F)
    with open(path, "wb") as fh:
        fh.write(header + data)


def dominant_shirt(body):
    counts = {}
    for row in body.px:
        for c in row:
            if c != PINK and c[2] > c[0] and c[2] > c[1] and c[2] > 120:
                counts[c] = counts.get(c, 0) + 1
    return max(counts, key=counts.get) if counts else (40, 96, 190)


def assemble_preview(body, ups, fores, gloves, sh_dx, sh_dy, l1, l2, l3):
    """Paste the skeleton into an idle pose to eyeball the part geometry."""
    c = Canvas(80, 80)
    ax, ay = 40, 45

    def paste(part, px, py, ang):
        for y in range(part.h):
            for x in range(part.w):
                col = part.px[y][x]
                if col == PINK:
                    continue
                lx, ly = x - part.w // 2, y - part.h // 2
                rx = lx * math.cos(ang) - ly * math.sin(ang)
                ry = lx * math.sin(ang) + ly * math.cos(ang)
                c.set(round(px + rx), round(py + ry), col)

    th1 = (math.pi * 95 / 180, math.pi * 85 / 180)
    th2 = math.pi / 2
    for i, side in enumerate((0, 1)):
        sx = ax + sh_dx if side else ax - sh_dx
        sy = ay + sh_dy
        paste(ups[side], sx, sy, th1[side])
        ex = sx + l1 * math.cos(th1[side])
        ey = sy + l1 * math.sin(th1[side])
        paste(fores[side], ex, ey, th2)
        wx = ex + l2 * math.cos(th2)
        wy = ey + l2 * math.sin(th2)
        paste(gloves[side], wx, wy, th2)
    for y in range(body.h):
        for x in range(body.w):
            if body.px[y][x] != PINK:
                c.set(ax - body.w // 2 + x, ay - body.h // 2 + y, body.px[y][x])
    img = Image.new("RGB", (c.w, c.h), (255, 255, 255))
    for y in range(c.h):
        for x in range(c.w):
            if c.px[y][x] != PINK:
                img.putpixel((x, y), c.px[y][x])
    return img


if __name__ == "__main__":
    body, ups, fores, gloves, sh_dx, sh_dy, l_up, l_fore, l_glove = load_keeper(50)
    write_bmp("keeper_body.bmp", [body], body.w, body.h)
    write_bmp("arm_up.bmp", [ups[0]], ups[0].w, ups[0].h)
    write_bmp("arm_up_r.bmp", [ups[1]], ups[1].w, ups[1].h)
    write_bmp("arm_fore.bmp", [fores[0]], fores[0].w, fores[0].h)
    write_bmp("arm_fore_r.bmp", [fores[1]], fores[1].w, fores[1].h)
    write_bmp("glove.bmp", [gloves[0]], gloves[0].w, gloves[0].h)
    write_bmp("glove_r.bmp", [gloves[1]], gloves[1].w, gloves[1].h)
    goal, gw, gh = goal_sprite()
    write_bmp("goal.bmp", [goal], gw, gh)
    bg = background()
    write_bmp("bg.bmp", [bg], 320, 190)
    ball = ball_sprite()
    write_bmp("ball.bmp", ball, 16, 16)

    shirt = dominant_shirt(body)
    q = lambda c: ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
    print(f"body {body.w}x{body.h}  arms {ups[0].w}x{ups[0].h} {fores[0].w}x{fores[0].h} {gloves[0].w}x{gloves[0].h}")
    print(f"shoulder dx={sh_dx} dy={sh_dy}  L_up={l_up} L_fore={l_fore} L_glove={l_glove}")
    print(f"shirt colour rgb{shirt} 565=0x{q(shirt):04x}")
    assemble_preview(body, ups, fores, gloves, sh_dx, sh_dy, l_up, l_fore, l_glove).save(
        "/tmp/opencode/soccer_ref/keeper_preview.png")
