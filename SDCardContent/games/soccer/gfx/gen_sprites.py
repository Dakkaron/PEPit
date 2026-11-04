#!/usr/bin/env python3
"""Pixel-art sprite generator for the Soccer Penalty game.

Writes 16-bit RGB565 BMPs (BI_BITFIELDS) with pink (0xF81F) as the
transparent masking colour, matching the firmware BMP loader and
LoadSprite(..., 0, 0xf81f).

The whole scene (crowd, goal, grass) comes from the reference image
copilot_image_1785244428162.jpeg; the game's goal geometry below is
measured from that image so the drawn goal and the aiming area agree.

The keeper is reassembled as a skeleton from the reference drawing
(1793739452570.jpg), which ships with separated bones: torso+legs as one
piece, head, upper arms, forearms, gloves and shoes separate. The legs are
split at the crotch line into thigh and shin+shoe bones. Every bone is
rotated in Python so its joint-to-tip axis is straight DOWN, then padded so
the joint pixel is the exact sprite centre — the pivot DrawSprite rotates
around. The torso sprite is pivoted at its crotch point. Angles passed to
DrawSprite therefore directly give the bone direction in screen space (y
grows downward, positive = clockwise).

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


def row_runs(mask_row):
    out, x = [], 0
    n = len(mask_row)
    while x < n:
        if mask_row[x]:
            s = x
            while x < n and mask_row[x]:
                x += 1
            out.append((s, x - 1))
        else:
            x += 1
    return out


def recolor(canvas, test, color):
    for y in range(canvas.h):
        for x in range(canvas.w):
            if canvas.px[y][x] != PINK and test(canvas.px[y][x]):
                canvas.px[y][x] = color


def leg_bone(pixels, rows, joint, tip, s, overrides=None):
    """Build a bone from mask `rows` (bool, full-image coords), rotating it
    about `joint` so joint->tip points straight down, and padding so the
    joint pixel is the canvas centre. Returns (canvas, length)."""
    ys, xs = np.nonzero(rows)
    ang = math.pi / 2 - math.atan2(tip[1] - joint[1], tip[0] - joint[0])
    ca, sa = math.cos(ang), math.sin(ang)
    pts = []
    r = 0.0
    for y, x in zip(ys, xs):
        dx, dy = s * (x - joint[0]), s * (y - joint[1])
        nx, ny = ca * dx - sa * dy, sa * dx + ca * dy
        col = overrides.get((y, x)) if overrides else None
        pts.append((nx, ny, col if col else tuple(int(v) for v in pixels[y, x])))
        r = max(r, abs(nx), abs(ny))
    r = int(math.ceil(r)) + 1
    c = Canvas(2 * r + 1, 2 * r + 1)
    cx = cy = float(r)
    for nx, ny, col in pts:
        ix, iy = int(round(cx + nx)), int(round(cy + ny))
        if c.px[iy][ix] == PINK:
            c.px[iy][ix] = col
    length = int(round(s * math.hypot(tip[0] - joint[0], tip[1] - joint[1])))
    return c, length


def load_keeper(scale_target):
    img = Image.open(os.path.join(HERE, "1793739452570.jpg")).convert("RGB")
    a = np.array(img)
    h, w = a.shape[:2]
    idx = np.arange(h)[:, None]

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

    order = [k for k in sorted(comps, key=lambda k: -comps[k]) if comps[k] > 1500]
    body_lbl, head_lbl = order[0], order[1]

    def ext(lbl):
        ys, xs = np.nonzero(lab == lbl)
        return int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())

    body_x0, body_x1, body_y0, body_y1 = ext(body_lbl)
    body_cx = (body_x0 + body_x1) / 2

    # The drawing ships arms as separate components; the shoes are separate
    # components BELOW the body (the socks end a few rows above them).
    arms = {lbl: ext(lbl) for lbl in order[2:] if ext(lbl)[3] < body_y1}
    shoe_lbls = [lbl for lbl in order[2:] if ext(lbl)[2] > body_y1 - 10]

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

    head_x0, head_x1, head_y0, head_y1 = ext(head_lbl)
    shoe_exts = {lbl: ext(lbl) for lbl in shoe_lbls}
    y_top = min([head_y0] + [e[2] for e in arms.values()])
    y_bot = max([body_y1] + [e[3] for e in shoe_exts.values()])
    s = scale_target / (y_bot - y_top + 1)

    def is_skin(p):
        r, g, b = (int(v) for v in p)
        return r > 120 and r - b > 25

    def is_blue(p):
        r, b = int(p[0]), int(p[2])
        return b > r + 25

    body_rows = lab == body_lbl
    # Hip line: first row (scanning down) where the body silhouette splits
    # into two separated leg runs; the gap midpoint is the crotch.
    hip_row, crotch_x = None, body_cx
    for y in range((body_y0 + body_y1) // 2, body_y1):
        r = row_runs(body_rows[y])
        if len(r) == 2 and r[1][0] - r[0][1] >= 6:
            hip_row = y
            crotch_x = (r[0][1] + r[1][0]) / 2
            break
    assert hip_row is not None, "no leg split found"

    # Knee line: where both legs switch to solid blue (the long socks) and
    # no skin pixels are left in the leg band.
    counts = {}
    for y in range(hip_row + 1, body_y1 + 1):
        pix = [a[y, x] for x in range(w) if body_rows[y, x]]
        counts[y] = (sum(1 for p in pix if is_blue(p)), sum(1 for p in pix if is_skin(p)))
    knee_row = next(y for y in range(hip_row + 40, body_y1 + 1)
                    if counts[y][0] >= 60 and counts[y][1] == 0
                    and counts.get(y + 4, (0, 1))[0] >= 60)

    legs = {}
    for side in ("L", "R"):
        leg_mask = body_rows & (idx >= hip_row)
        leg_mask &= (np.arange(w)[None, :] < crotch_x) if side == "L" \
            else (np.arange(w)[None, :] >= crotch_x)
        hr = row_runs(leg_mask[hip_row])
        hip_joint = (hr[0][0] + hr[-1][1]) / 2.0

        runs = row_runs(leg_mask[knee_row])
        knee = ((runs[0][0] + runs[-1][1]) / 2.0, float(knee_row))
        bottom = max(y for y in range(body_y1, knee_row, -1) if leg_mask[y].any())
        bruns = row_runs(leg_mask[bottom])

        thigh_mask = leg_mask & (idx < knee_row)
        th, len_thigh = leg_bone(a, thigh_mask, (hip_joint, float(hip_row)), knee, s)
        th_idle = math.atan2(hip_joint - knee[0], knee[1] - hip_row)

        shin_mask = leg_mask & (idx >= knee_row)

        def shoe_side_ok(lbl):
            cx = (shoe_exts[lbl][0] + shoe_exts[lbl][1]) / 2
            return cx < crotch_x if side == "L" else cx >= crotch_x

        shoe_lbl = next(lbl for lbl in shoe_lbls if shoe_side_ok(lbl))
        sx0, sx1, sy0, sy1 = shoe_exts[shoe_lbl]
        overrides = {}
        for y in range(bottom + 1, sy0):  # bridge the sock-sole / shoe-top gap
            for x in range(max(sx0, bruns[0][0]), min(sx1, bruns[-1][1]) + 1):
                shin_mask[y, x] = True
                overrides[(y, x)] = tuple(int(v) for v in a[sy0, x])
        shin_mask |= (lab == shoe_lbl)
        sruns = row_runs((lab == shoe_lbl)[sy1])
        tip = ((sruns[0][0] + sruns[-1][1]) / 2.0, float(sy1))
        sh, len_shin = leg_bone(a, shin_mask, knee, tip, s, overrides)
        sh_idle = math.atan2(knee[0] - tip[0], tip[1] - knee[1])
        legs[side] = dict(thigh=th, shin=sh, hip_joint=hip_joint,
                          len_thigh=len_thigh, len_shin=len_shin,
                          th_idle=th_idle, sh_idle=sh_idle)

    # Torso sprite: head + shirt + shorts top (the legs, including the
    # dangling shorts legs and socks, belong to the thigh/shin bones), neck
    # bridged; the pivot is the crotch point (hip line).
    torso_mask = (body_rows & (idx < hip_row)) | (lab == head_lbl)
    t_ys, t_xs = np.nonzero(torso_mask)
    my0, mx0 = int(t_ys.min()), int(t_xs.min())
    body = Canvas(int(t_xs.max()) - mx0 + 1, int(t_ys.max()) - my0 + 1)
    for y, x in zip(t_ys, t_xs):
        body.px[y - my0][x - mx0] = tuple(int(v) for v in a[y, x])
    head_ys = np.nonzero(lab == head_lbl)[0]
    head_xs = np.nonzero(lab == head_lbl)[1]
    torso_ys = np.nonzero(body_rows)[0]
    torso_xs = np.nonzero(body_rows)[1]
    hb, tt = int(head_ys.max()), int(torso_ys.min())
    hx0, hx1 = int(head_xs[head_ys == hb].min()), int(head_xs[head_ys == hb].max())
    tx0, tx1 = int(torso_xs[torso_ys == tt].min()), int(torso_xs[torso_ys == tt].max())
    for y in range(hb + 1, tt):
        t = (y - hb) / (tt - hb)
        for x in range(int(round(hx0 + (tx0 - hx0) * t)), int(round(hx1 + (tx1 - hx1) * t)) + 1):
            body.px[y - my0][x - mx0] = COL_NECK
    body = rescale(body, s)
    px, py = (crotch_x - mx0) * s, (hip_row - my0) * s
    pts = [(x, y) for y in range(body.h) for x in range(body.w) if body.px[y][x] != PINK]
    m = max(max(abs(x - px) for x, y in pts), max(abs(y - py) for x, y in pts))
    m = int(round(m))
    bodyc = Canvas(2 * m + 1, 2 * m + 1)
    for x, y in pts:
        bodyc.px[int(round(y - py + m))][int(round(x - px + m))] = body.px[y][x]

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

    up_l, len_up = arm_part(parts["L_up"], max_len=9)
    fore_l, len_fore = arm_part(parts["L_fore"], max_len=5)
    glove_l, len_glove = arm_part(parts["L_glove"], glove=True, max_len=7)
    up_r = mirror(up_l)
    fore_r = mirror(fore_l)
    glove_r = mirror(glove_l)

    # Shoulder joints: the closest approach between each upper-arm component
    # and the torso silhouette (the drawing keeps a thin gap at the seam).
    def arm_torso_joint(lbl):
        a0, a1 = arms[lbl][2], arms[lbl][3]
        pa = np.stack(np.nonzero((lab == lbl) & (idx >= a1 - 30))[::-1], axis=1).astype(float)
        pb = np.stack(np.nonzero(body_rows & (idx >= a0 - 10) & (idx <= a1 + 40))[::-1], axis=1).astype(float)
        best, jp = None, None
        for x, y in pa:
            d2 = (pb[:, 0] - x) ** 2 + (pb[:, 1] - y) ** 2
            i = int(np.argmin(d2))
            if best is None or d2[i] < best:
                best, jp = d2[i], (pb[i, 0], pb[i, 1])
        return jp

    jL = arm_torso_joint(parts["L_up"])
    jR = arm_torso_joint(parts["R_up"])
    # +1 px outward: exactly on the seam the arms read as tucked into the shirt
    sh_dx = int(round((jR[0] - crotch_x) * s)) + 1
    sh_dy = int(round(((jL[1] + jR[1]) / 2 - hip_row) * s))
    hip_dx = {side: int(round((legs[side]["hip_joint"] - crotch_x) * s))
              for side in ("L", "R")}
    sole_drop = int(round((y_bot - hip_row) * s))

    return dict(body=bodyc, up=(up_l, up_r), fore=(fore_l, fore_r),
                glove=(glove_l, glove_r),
                thigh=(legs["L"]["thigh"], legs["R"]["thigh"]),
                shin=(legs["L"]["shin"], legs["R"]["shin"]),
                sh_dx=sh_dx, sh_dy=sh_dy, hip_dx=hip_dx,
                len_up=len_up, len_fore=len_fore, len_glove=len_glove,
                len_thigh=legs["L"]["len_thigh"], len_shin=legs["L"]["len_shin"],
                idle=dict(L=(legs["L"]["th_idle"], legs["L"]["sh_idle"]),
                          R=(legs["R"]["th_idle"], legs["R"]["sh_idle"])),
                sole_drop=sole_drop)


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
    K = load_keeper(50)
    body = K["body"]
    write_bmp("keeper_body.bmp", [body], body.w, body.h)
    for name, key in (("arm_up", "up"), ("arm_fore", "fore"), ("glove", "glove"),
                      ("leg_thigh", "thigh"), ("leg_shin", "shin")):
        cL, cR = K[key]
        write_bmp(f"{name}.bmp", [cL], cL.w, cL.h)
        write_bmp(f"{name}_r.bmp", [cR], cR.w, cR.h)
    bg, geom = background()
    write_bmp("bg.bmp", [bg], 320, 240)
    ball = ball_sprite()
    write_bmp("ball.bmp", ball, 16, 16)

    shirt = dominant_shirt(body)
    q = lambda c: ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
    print(f"body {body.w}x{body.h}  arms {K['up'][0].w}x{K['up'][0].h} "
          f"{K['fore'][0].w}x{K['fore'][0].h} {K['glove'][0].w}x{K['glove'][0].h}  "
          f"legs {K['thigh'][0].w}x{K['thigh'][0].h} {K['shin'][0].w}x{K['shin'][0].h}")
    print(f"shoulder dx={K['sh_dx']} dy={K['sh_dy']}  L_up={K['len_up']} "
          f"L_fore={K['len_fore']} L_glove={K['len_glove']} sole_drop={K['sole_drop']}")
    print(f"hip dx L={K['hip_dx']['L']} R={K['hip_dx']['R']}  thigh={K['len_thigh']} "
          f"shin={K['len_shin']}")
    print(f"idle angles L thigh={K['idle']['L'][0]:.3f} shin={K['idle']['L'][1]:.3f}  "
          f"R thigh={K['idle']['R'][0]:.3f} shin={K['idle']['R'][1]:.3f}")
    print(f"shirt colour rgb{shirt} 565=0x{q(shirt):04x}")
    print(f"lua: GOAL_X0={geom[0]} GOAL_X1={geom[1]} GOAL_TOP={geom[2]} GOAL_BOTTOM={geom[3]}")

    img = Image.new("RGB", (320, 240))
    for y in range(240):
        for x in range(320):
            img.putpixel((x, y), bg.px[y][x])
    img.save("/tmp/opencode/soccer_ref/bg_preview.png")
