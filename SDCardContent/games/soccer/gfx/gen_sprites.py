#!/usr/bin/env python3
"""Pixel-art sprite generator for the Soccer Penalty game.

Writes 16-bit RGB565 BMPs (BI_BITFIELDS) with pink (0xF81F) as the
transparent masking colour, matching the firmware BMP loader and
LoadSprite(..., 0, 0xf81f).

The whole scene (crowd, goal, grass) comes from the reference image
copilot_image_1785244428162.jpeg; the game's goal geometry below is
measured from that image so the drawn goal and the aiming area agree.

The keeper is reassembled as a skeleton from the reference drawing
(1793739452570.jpg), which ships with separated bones: head+torso+legs
as one piece, upper arms, forearms and gloves separate. Each arm part is
rotated in Python so its joint-to-tip axis is straight DOWN, then padded
so the joint pixel is the exact sprite centre — the pivot DrawSprite
rotates around. Angles passed to DrawSprite therefore directly give the
bone direction in screen space (y grows downward, positive = clockwise).

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

COL_NECK = (184, 133, 107)

# Not pure white: the rulebot finds the ball as the only solid white blob
# outside the goal rect, so gloves and grass stripes stay off-white.
COL_GLOVE = (216, 224, 236)
COL_GLOVE_DK = (180, 190, 204)
COL_GRASS_WHITE = (150, 205, 130)
COL_GOALNET = (198, 205, 214)


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


def joint_down(canvas):
    """Rotate a bone about its joint so joint-to-tip points straight down.

    The joint is the midpoint of the topmost opaque row. Returns
    (canvas, joint_x, joint_y, length) with the joint at the exact canvas
    centre and the bone hanging below it.
    """
    x0, y0, x1, y1 = canvas.bbox()
    top_xs = [x for x in range(x0, x1 + 1) if canvas.opaque(x, y0)]
    jx, jy = (top_xs[0] + top_xs[-1]) / 2.0, float(y0)
    tip = max(((x, y) for y in range(canvas.h) for x in range(canvas.w)
               if canvas.opaque(x, y)), key=lambda p: (p[0] - jx) ** 2 + (p[1] - jy) ** 2)
    ang = math.pi / 2 - math.atan2(tip[1] - jy, tip[0] - jx)
    ca, sa = math.cos(ang), math.sin(ang)
    r = math.ceil(math.hypot(x1 - jx, y1 - jy)) + 1
    size = 2 * r + 1
    c = Canvas(size, size)
    cx, cy = float(r), float(r)
    for y in range(canvas.h):
        for x in range(canvas.w):
            col = canvas.px[y][x]
            if col == PINK:
                continue
            dx, dy = x - jx, y - jy
            nx = int(round(cx + ca * dx - sa * dy))
            ny = int(round(cy + sa * dx + ca * dy))
            if c.px[ny][nx] == PINK:
                c.px[ny][nx] = col
    length = int(round(math.hypot(tip[0] - jx, tip[1] - jy)))
    return c, length


def trim_tip(canvas, max_len):
    """Cut the bone short: anything farther than max_len from the joint."""
    cx, cy = canvas.w // 2, canvas.h // 2
    for y in range(canvas.h):
        for x in range(canvas.w):
            if canvas.px[y][x] != PINK and (x - cx) ** 2 + (y - cy) ** 2 > max_len ** 2:
                canvas.px[y][x] = PINK
    return recentre(canvas)


def recentre(canvas):
    """Crop to the content and pad so the joint pixel is the exact centre."""
    pts = [(x, y) for y in range(canvas.h) for x in range(canvas.w) if canvas.px[y][x] != PINK]
    if not pts:
        return canvas
    jx, jy = canvas.w // 2, canvas.h // 2
    r = max(max(abs(x - jx) for x, y in pts), max(abs(y - jy) for x, y in pts))
    c = Canvas(2 * r + 1, 2 * r + 1)
    for x, y in pts:
        c.px[y - jy + r][x - jx + r] = canvas.px[y][x]
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
    arms = {}
    for lbl in order[2:]:
        ys, xs = np.nonzero(lab == lbl)
        arms[lbl] = (int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max()))
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
    y_bot = int(bys.max())
    s = scale_target / (y_bot - y_top + 1)
    # Body = torso + head, neck bridged; anchor is the torso's vertical
    # middle (roughly the pelvis/chest centre).
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
    m = max(ax - bx0, bx1 - ax, ay - by0, by1 - ay)
    bodyc = Canvas(2 * m + 1, 2 * m + 1)
    for y in range(body.h):
        for x in range(body.w):
            if body.px[y][x] != PINK:
                bodyc.px[y - ay + m][x - ax + m] = body.px[y][x]

    def arm_part(lbl, glove=False, max_len=None):
        c = part_canvas(a, lab == lbl)
        c = rescale(c, s)
        c, length = joint_down(c)
        if max_len:
            c = trim_tip(c, max_len)
            length = min(length, max_len)
        if glove:
            recolor(c, lambda p: min(p) > 190, COL_GLOVE)
            recolor(c, lambda p: 150 < min(p) <= 190, COL_GLOVE_DK)
        return c, length

    up_l, len_up = arm_part(parts["L_up"], max_len=11)
    fore_l, len_fore = arm_part(parts["L_fore"], max_len=6)
    glove_l, len_glove = arm_part(parts["L_glove"], glove=True, max_len=7)
    up_r = mirror(up_l)
    fore_r = mirror(fore_l)
    glove_r = mirror(glove_l)

    # Shoulder joints: outer edge of the first full-width torso row.
    torso_top = next(y for y in range(bodyc.h)
                     if sum(1 for x in range(bodyc.w) if bodyc.opaque(x, y)) >= 18)
    xs = [x for x in range(bodyc.w) if bodyc.opaque(x, torso_top)]
    sh_dx = xs[-1] - m - 1
    sh_dy = torso_top - m
    foot_drop = bodyc.h - 1 - m

    return bodyc, (up_l, up_r), (fore_l, fore_r), (glove_l, glove_r), \
        sh_dx, sh_dy, len_up, len_fore, len_glove, foot_drop


def background():
    img = Image.open(os.path.join(HERE, "copilot_image_1785244428162.jpeg")).convert("RGB")
    w, h = img.size
    # Crop to 4:3 (keeping the goal, which is horizontally centred) and
    # scale down to the screen size.
    cw = (h * 4) // 3
    x0 = (w - cw) // 2
    img = img.crop((x0, 0, x0 + cw, h)).resize((320, 240), Image.NEAREST)
    a = np.array(img)
    white = np.all(a > 230, axis=2)

    # Measure the goal straight off the scaled image: the posts are the
    # columns that stay white down the goal's height, the crossbar is the
    # solid white band at the top of the frame.
    cols = [x for x in range(320) if white[60:125, x].sum() > 50]
    groups = []
    for x in cols:
        if groups and x - groups[-1][1] <= 2:
            groups[-1][1] = x
        else:
            groups.append([x, x])
    lx0, lx1 = groups[0]
    rx0, rx1 = groups[-1]
    rows = [y for y in range(30, 90) if white[y, lx0:rx1 + 1].sum() > (rx1 - lx0) * 0.7]
    top0, top1 = min(rows), max(rows)
    bases = [y for y in range(top1, 160) if white[y, lx1] and white[y, rx0]]
    bottom = max(bases)
    print(f"goal frame in image: posts x {lx0}..{lx1} / {rx0}..{rx1}, "
          f"crossbar y {top0}..{top1}, line y {bottom}")

    c = Canvas(320, 240)
    for y in range(240):
        for x in range(320):
            p = a[y][x]
            if min(p) > 185:
                # Pure white anywhere in the play band would read as the
                # ball to the rulebot, so every white in the photo (net,
                # posts, grass stripes) is pushed off-white.
                c.px[y][x] = COL_GRASS_WHITE if y > bottom else COL_GOALNET
            else:
                c.px[y][x] = tuple(int(v) for v in p)
    return c, (lx1 + 1, rx0 - 1, top1 + 1, bottom)


def ball_sprite(r=8, frames=4):
    img = Image.open(os.path.join(HERE, "copilot_image_1793564531376.png")).convert("RGB")
    a = np.array(img)
    ys, xs = np.nonzero(np.max(a, axis=2) > 60)
    crop = img.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    d = 2 * r
    crop = crop.resize((d, d), Image.LANCZOS)
    a = np.array(crop)
    # Mask by the ball circle, not by brightness, so the black pentagons
    # stay opaque.
    base = [[PINK] * d for _ in range(d)]
    for y in range(d):
        for x in range(d):
            if (x - r + 0.5) ** 2 + (y - r + 0.5) ** 2 <= (r - 0.2) ** 2:
                base[y][x] = tuple(int(v) for v in a[y][x])
    out = []
    for f in range(frames):
        ang = f * math.pi / 4
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


def lerp(a, b, t):
    return a + (b - a) * t


def dominant_shirt(body):
    counts = {}
    for row in body.px:
        for c in row:
            if c != PINK and c[2] > c[0] and c[2] > c[1] and c[2] > 120:
                counts[c] = counts.get(c, 0) + 1
    return max(counts, key=counts.get) if counts else (40, 96, 190)


if __name__ == "__main__":
    body, ups, fores, gloves, sh_dx, sh_dy, l_up, l_fore, l_glove, foot = load_keeper(50)
    write_bmp("keeper_body.bmp", [body], body.w, body.h)
    write_bmp("arm_up.bmp", [ups[0]], ups[0].w, ups[0].h)
    write_bmp("arm_up_r.bmp", [ups[1]], ups[1].w, ups[1].h)
    write_bmp("arm_fore.bmp", [fores[0]], fores[0].w, fores[0].h)
    write_bmp("arm_fore_r.bmp", [fores[1]], fores[1].w, fores[1].h)
    write_bmp("glove.bmp", [gloves[0]], gloves[0].w, gloves[0].h)
    write_bmp("glove_r.bmp", [gloves[1]], gloves[1].w, gloves[1].h)
    bg, geom = background()
    write_bmp("bg.bmp", [bg], 320, 240)
    ball = ball_sprite()
    write_bmp("ball.bmp", ball, 16, 16)

    shirt = dominant_shirt(body)
    q = lambda c: ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
    print(f"body {body.w}x{body.h}  arms {ups[0].w}x{ups[0].h} {fores[0].w}x{fores[0].h} {gloves[0].w}x{gloves[0].h}")
    print(f"shoulder dx={sh_dx} dy={sh_dy}  L_up={l_up} L_fore={l_fore} L_glove={l_glove} foot_drop={foot}")
    print(f"shirt colour rgb{shirt} 565=0x{q(shirt):04x}")
    print(f"lua: GOAL_X0={geom[0]} GOAL_X1={geom[1]} GOAL_TOP={geom[2]} GOAL_BOTTOM={geom[3]}")

    img = Image.new("RGB", (320, 240))
    for y in range(240):
        for x in range(320):
            img.putpixel((x, y), bg.px[y][x])
    img.save("/tmp/opencode/soccer_ref/bg_preview.png")
