#!/usr/bin/env python3
"""Procedural low-poly race car generator for the PEPit 3D engine.

Writes racecar.obj, racecar.mtl and a 128x128 texture atlas racecar.bmp into
SDCardContent/games/3ddemo/models/. The engine applies a single texture per
mesh, so every part gets UVs into a different region of one atlas.

Engine conventions honored:
- OBJ face indices 1-based, f v/vt/vn records, per-face vn for flat shading.
- The engine maps OBJ v=1 to the top row of the BMP as displayed.
- Faces must be wound so the area normal points outward (backface culling).
- Target: around 2000 triangles.
"""
import math
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "SDCardContent", "games", "3ddemo", "models")

TEX = 128
ZLEN = 1.2  # stretch all z positions/box depths; wheel radii stay round

# atlas rectangles: name -> (x0, y0, x1, y1) in pixels, y measured from top
REGION = {
    "body":      (0, 0, 64, 64),
    "glass":     (68, 0, 88, 18),
    "white":     (92, 0, 112, 20),
    "dark":      (116, 0, 127, 20),
    "tire":      (68, 24, 126, 58),
    "rim":       (68, 62, 126, 110),
    "headlight": (68, 114, 90, 126),
    "tail":      (94, 114, 116, 126),
}

verts = []      # (x,y,z)
uvs = []        # (u,v) for the OBJ vt list
normals = []    # flat face normals for the vn list
faces = []      # triangle index tuples

INSET = 2.5

def _region(name):
    x0, y0, x1, y1 = REGION[name]
    return x0 + INSET, y0 + INSET, x1 - INSET, y1 - INSET

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0])

def dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return (a[0]/l, a[1]/l, a[2]/l)

def centroid(poly):
    return tuple(sum(p[i] for p in poly) / len(poly) for i in range(3))

def _emit_tris(poly, corner_uv, n):
    tris = [(0, 1, 2), (0, 2, 3)] if len(poly) == 4 else [(0, 1, 2)]
    for a, b, c in tris:
        for idx in (a, b, c):
            verts.append(poly[idx])
            uvs.append(corner_uv[idx])
        ni = len(normals)
        normals.append(n)
        base = len(verts) - 3
        faces.append((base, base + 1, base + 2, base, base + 1, base + 2, ni))

def _uv_from_axes(poly, region_name, right, up):
    c = centroid(poly)
    dR = [dot(sub(p, c), right) for p in poly]
    dU = [dot(sub(p, c), up) for p in poly]
    r0, r1 = min(dR), max(dR)
    u0, u1 = min(dU), max(dU)
    spanR = (r1 - r0) or 1.0
    spanU = (u1 - u0) or 1.0
    return _uv_rect(poly, dR, dU, r0, r1, u0, u1, spanR, spanU, region_name)

def _uv_grid(poly, region_name, right, up, gR, gU):
    """Projection onto a global range shared by a family of faces, so the
    atlas tiles once across the whole surface instead of per face."""
    dR = [dot(p, right) for p in poly]
    dU = [dot(p, up) for p in poly]
    r0, r1 = gR
    u0, u1 = gU
    return _uv_rect(poly, dR, dU, r0, r1, u0, u1, (r1 - r0) or 1.0, (u1 - u0) or 1.0,
                    region_name)

def _uv_planar(poly, region_name, origin, right, up, halfR, halfU):
    """Planar UV anchored at a shared origin (e.g. a fan-triangulated disc)."""
    dR = [dot(sub(p, origin), right) for p in poly]
    dU = [dot(sub(p, origin), up) for p in poly]
    du = [0.5 + d / (2 * halfR) for d in dR]
    dv = [0.5 + d / (2 * halfU) for d in dU]
    rx0, ry0, rx1, ry1 = _region(region_name)
    out = []
    for a, b in zip(du, dv):
        a = min(max(a, 0.0), 1.0)
        b = min(max(b, 0.0), 1.0)
        px = rx0 + a * (rx1 - rx0)
        py = ry1 - b * (ry1 - ry0)
        out.append((px / TEX, 1.0 - py / TEX))
    return out

def _uv_rect(poly, dR, dU, r0, r1, u0, u1, spanR, spanU, region_name):
    rx0, ry0, rx1, ry1 = _region(region_name)
    out = []
    for pr, pu in zip(dR, dU):
        du = (pr - r0) / spanR
        dv = (pu - u0) / spanU
        px = rx0 + du * (rx1 - rx0)
        py = ry1 - dv * (ry1 - ry0)
        out.append((px / TEX, 1.0 - py / TEX))
    return out

def emit_face(poly, region_name, right, up, outward):
    if dot(cross(sub(poly[1], poly[0]), sub(poly[2], poly[0])), outward) < 0:
        poly = poly[::-1]
    n = norm(cross(sub(poly[1], poly[0]), sub(poly[2], poly[0])))
    _emit_tris(poly, _uv_from_axes(poly, region_name, right, up), n)

def add_box(cx, cy, cz, hx, hy, hz, regions):
    cz, hz = cz * ZLEN, hz * ZLEN
    x0, x1 = cx - hx, cx + hx
    y0, y1 = cy - hy, cy + hy
    z0, z1 = cz - hz, cz + hz
    emit_face([(x0,y1,z1),(x1,y1,z1),(x1,y1,z0),(x0,y1,z0)], regions["+y"], (-1,0,0), (0,0,-1), (0,1,0))
    emit_face([(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1)], regions["-y"], (1,0,0), (0,0,-1), (0,-1,0))
    emit_face([(x1,y0,z1),(x1,y0,z0),(x1,y1,z0),(x1,y1,z1)], regions["+x"], (0,0,-1), (0,1,0), (1,0,0))
    emit_face([(x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0)], regions["-x"], (0,0,1), (0,1,0), (-1,0,0))
    emit_face([(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)], regions["+z"], (1,0,0), (0,1,0), (0,0,1))
    emit_face([(x1,y0,z0),(x0,y0,z0),(x0,y1,z0),(x1,y1,z0)], regions["-z"], (-1,0,0), (0,1,0), (0,0,-1))

def chamfer_section(hw, yb, yt, ch):
    """8-point chamfered rectangle in the (x, y) plane."""
    return [( hw - ch, yt), (-hw + ch, yt),
            (-hw, yt - ch), (-hw, yb + ch),
            (-hw + ch, yb), ( hw - ch, yb),
            ( hw, yb + ch), ( hw, yt - ch)]

def add_loft(stations, region_fn, cap_front, cap_rear, axis_pt):
    """stations: [(z, [(x,y) clockwise-when-viewed-from-front])], front to rear.
    region_fn(outward_normal) -> region name. Grid UVs: u spans the loft
    front-to-back, v spans the section height (or width on top bands) so
    atlas features tile once across the whole surface."""
    stations = [(z * ZLEN, sec) for (z, sec) in stations]
    rings = [[(x, y, z) for (x, y) in sec] for (z, sec) in stations]
    allPts = [p for ring in rings for p in ring]
    gR = (min(dot(p, (0, 0, -1.0)) for p in allPts),
          max(dot(p, (0, 0, -1.0)) for p in allPts))
    gUy = (min(p[1] for p in allPts), max(p[1] for p in allPts))
    gUx = (min(p[0] for p in allPts), max(p[0] for p in allPts))
    for i in range(len(rings) - 1):
        a, b = rings[i], rings[i + 1]
        nk = len(a)
        for k in range(nk):
            k2 = (k + 1) % nk
            p = [a[k], a[k2], b[k2], b[k]]
            mid = centroid(p)
            hint = norm((mid[0] - axis_pt[0], mid[1] - axis_pt[1], 0.0))
            if dot(cross(sub(p[1], p[0]), sub(p[2], p[0])), hint) < 0:
                p = p[::-1]
            n = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])))
            region = region_fn(n)
            if n[1] > 0.7 or n[1] < -0.45:
                up = (1.0, 0.0, 0.0)
                gU = gUx
            else:
                up = (0.0, 1.0, 0.0)
                gU = gUy
            _emit_tris(p, _uv_grid(p, region, (0, 0, -1.0), up, gR, gU), n)
    for ring, zcap, outz, cap_region in ((rings[0], stations[0][0], 1, cap_front),
                                         (rings[-1], stations[-1][0], -1, cap_rear)):
        yc = sum(pt[1] for pt in ring) / len(ring)
        c = (axis_pt[0], yc, zcap)
        for k in range(len(ring)):
            k2 = (k + 1) % len(ring)
            tri = [(c[0], c[1], zcap), ring[k2], ring[k]] if outz < 0 \
                  else [(c[0], c[1], zcap), ring[k], ring[k2]]
            _emit_tris(tri, _uv_from_axes(tri, cap_region, (1, 0, 0), (0, 1, 0)),
                       (0, 0, float(outz)))

def add_wheel(cx, cy, cz, r, half_w, segs):
    cz *= ZLEN
    for k in range(segs):
        t0 = 2 * math.pi * k / segs
        t1 = 2 * math.pi * (k + 1) / segs
        tm = (t0 + t1) / 2
        p00 = (cx - half_w, cy + r * math.sin(t0), cz + r * math.cos(t0))
        p01 = (cx - half_w, cy + r * math.sin(t1), cz + r * math.cos(t1))
        p11 = (cx + half_w, cy + r * math.sin(t1), cz + r * math.cos(t1))
        p10 = (cx + half_w, cy + r * math.sin(t0), cz + r * math.cos(t0))
        outw = (0, math.sin(tm), math.cos(tm))
        tang = norm((0, math.cos(tm), -math.sin(tm)))
        emit_face([p10, p11, p01, p00], "tire", tang, (1, 0, 0), outw)
    # 0.02 outward offset keeps rim disc + sidewall strictly in
    # front of the tread band's side boundary; coplanar faces painter-sort ties
    for side, outx in ((cx + half_w + 0.02, 1), (cx - half_w - 0.02, -1)):
        center = (side, cy, cz)
        inner = r * 0.86
        for j in range(segs):
            a0 = 2 * math.pi * j / segs
            a1 = 2 * math.pi * (j + 1) / segs
            q0 = (side, cy + inner * math.sin(a0), cz + inner * math.cos(a0))
            q1 = (side, cy + inner * math.sin(a1), cz + inner * math.cos(a1))
            # fan winding must match the outward side or culling eats the disc
            tri = [center, q1, q0] if outx > 0 else [center, q0, q1]
            n = (outx, 0, 0)
            _emit_tris(tri, _uv_planar(tri, "rim", center, (0, 0, -outx), (0, 1, 0), inner, inner), n)
            # sidewall annulus between rim edge and tread
            w0 = (side, cy + r * math.sin(a0), cz + r * math.cos(a0))
            w1 = (side, cy + r * math.sin(a1), cz + r * math.cos(a1))
            wall = [q0, q1, w1, w0]
            if dot(cross(sub(wall[1], wall[0]), sub(wall[2], wall[0])), n) < 0:
                wall = wall[::-1]
            tm = (a0 + a1) / 2
            tang = (0, math.cos(tm), -math.sin(tm))
            radial = (0, math.sin(tm), math.cos(tm))
            _emit_tris(wall, _uv_planar(wall, "tire", center, tang, radial, r, r), n)

def build():
    # monocoque: front (+z) to rear (-z), chamfered sections
    def tub_region(n):
        return "dark" if n[1] < -0.45 else "body"
    tub = [(z, chamfer_section(hw, yb, yt, ch)) for (z, hw, yb, yt, ch) in (
        (18.0, 0.9, 2.6, 3.9, 0.3),
        (16.8, 1.2, 2.5, 4.1, 0.35),
        (16.0, 1.5, 2.4, 4.4, 0.4),
        (14.6, 1.9, 2.2, 4.9, 0.55),
        (13.5, 2.4, 2.0, 5.4, 0.7),
        (11.4, 3.0, 1.8, 6.2, 0.8),
        (10.0, 3.5, 1.7, 7.0, 0.9),
        # stations just behind the cockpit front (8.0) and its first band:
        # split deck bands so no triangle's average depth ties the glass
        # cap/band depths - equal averages make the painter sort flip with
        # view angle (front cockpit flicker)
        (8.35, 3.91, 1.62, 7.66, 0.93),
        (6.95, 4.26, 1.5, 8.22, 0.98),
        (6.0, 4.5, 1.5, 8.6, 1.0),
        (1.0, 4.8, 1.5, 9.2, 1.0),
        (-4.0, 4.8, 1.5, 9.4, 1.0),
        # same offset-splitting around the cockpit rear cap (-6.5)
        (-6.85, 4.69, 1.57, 9.4, 1.0),
        (-9.0, 4.6, 1.6, 9.4, 1.0),
        (-11.5, 4.4, 1.7, 9.1, 0.95),
        (-13.0, 4.2, 1.8, 8.8, 0.9),
        (-16.0, 3.5, 2.1, 7.6, 0.7),
        (-17.5, 3.0, 2.4, 6.4, 0.6),
    )]
    add_loft(tub, tub_region, "body", "body", (0, 5.5))

    # cockpit bubble (glass sides, white roof)
    def cockpit_region(n):
        if n[1] > 0.7:
            return "white"
        if n[1] < -0.45:
            return "dark"
        return "glass"
    cockpit = [(z, chamfer_section(hw, yb, yt, ch)) for (z, hw, yb, yt, ch) in (
        (8.0, 2.4, 8.25, 10.2, 0.4),
        (6.6, 2.8, 8.8, 10.8, 0.45),
        (5.5, 3.1, 9.1, 11.4, 0.5),
        (3.0, 3.2, 9.4, 11.7, 0.5),
        (1.0, 3.3, 9.65, 12.0, 0.5),
        (-3.5, 3.1, 9.8, 11.7, 0.5),
        (-6.5, 2.7, 9.85, 10.9, 0.4),
    )]
    add_loft(cockpit, cockpit_region, "glass", "glass", (0, 10.0))

    # sidepods: lofted with a flat inner wall just clear of the body flank
    # (penetrating surfaces would painter-z-fight against the tub side)
    for sx in (1, -1):
        def pod_region(n):
            if n[1] < -0.45 or n[0] * sx < -0.6:
                return "dark"
            return "body"
        def pod_section(inner, outer, yb, yt, ch):
            return [(inner, yb), (outer - ch, yb), (outer, yb + ch),
                    (outer, yt - ch), (outer - ch, yt), (inner, yt)]
        stations = []
        for (z, yb, yt, ch) in ((6.0, 1.2, 5.6, 0.7), (-2.0, 1.2, 5.6, 0.7), (-9.5, 1.3, 5.3, 0.6)):
            outer = 8.4 if z > -5 else 8.2
            sec = pod_section(sx * 4.85, sx * outer, yb, yt, ch)
            stations.append((z, sec))
        add_loft(stations, pod_region, "body", "dark", (sx * 6.6, 3.4))

    # pod inlets flush with the pod top/front faces
    for sx in (1, -1):
        add_box(sx * 6.6, 6.5, 5.6, 1.4, 0.9, 0.4,
                {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})

    # floor plank, clear below the tub bottom
    add_box(0, 1.15, -1.0, 6.2, 0.25, 12.0, {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})

    # front wing + flap + endplates
    add_box(0, 0.8, 16.6, 8.6, 0.35, 1.7,
            {"+y": "body", "-y": "dark", "+z": "body", "-z": "body", "+x": "white", "-x": "white"})
    add_box(0, 1.7, 17.4, 7.8, 0.28, 1.0,
            {"+y": "body", "-y": "dark", "+z": "white", "-z": "white", "+x": "white", "-x": "white"})
    for sx in (1, -1):
        add_box(sx * 8.9, 1.7, 16.4, 0.3, 1.5, 2.0,
                {"+x": "body", "-x": "body", "+y": "dark", "-y": "dark", "+z": "dark", "-z": "dark"})

    # bargeboards
    for sx in (1, -1):
        add_box(sx * 5.6, 3.2, 8.8, 0.3, 1.6, 2.2,
                {"+x": "body", "-x": "body", "+y": "dark", "-y": "dark", "+z": "dark", "-z": "dark"})

    # rear wing: main plane, flap, struts, endplates
    add_box(0, 10.1, -15.8, 8.6, 0.35, 1.5,
            {"+y": "body", "-y": "dark", "+z": "body", "-z": "body", "+x": "white", "-x": "white"})
    add_box(0, 11.8, -14.7, 8.2, 0.3, 1.1,
            {"+y": "body", "-y": "dark", "+z": "body", "-z": "body", "+x": "white", "-x": "white"})
    for sx in (1, -1):
        add_box(sx * 4.6, 8.98, -15.2, 0.35, 0.75, 0.5, {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})
        add_box(sx * 8.95, 11.0, -15.5, 0.3, 1.6, 1.9,
                {"+x": "body", "-x": "body", "+y": "dark", "-y": "dark", "+z": "dark", "-z": "dark"})

    # diffuser fins
    for k in (-1, 0, 1):
        add_box(k * 2.0, 0.75, -16.6, 0.25, 0.65, 1.6, {k2: "dark" for k2 in ("+x", "-x", "+y", "-y", "+z", "-z")})

    # mirrors
    for sx in (1, -1):
        add_box(sx * 5.9, 9.9, 7.8, 0.7, 0.55, 0.35,
                {"+x": "glass", "-x": "glass", "+y": "dark", "-y": "dark", "+z": "dark", "-z": "dark"})

    # lights
    for sx in (1, -1):
        z0 = 18.08 * ZLEN
        emit_face([(sx * 0.45, 2.9, z0), (sx * 0.12, 2.9, z0),
                   (sx * 0.12, 3.7, z0), (sx * 0.45, 3.7, z0)],
                  "headlight", (-sx, 0, 0), (0, 1, 0), (0, 0, 1))
    zt = -17.55 * ZLEN
    emit_face([(-1.4, 4.2, zt), (1.4, 4.2, zt), (1.4, 5.6, zt), (-1.4, 5.6, zt)],
              "tail", (1, 0, 0), (0, 1, 0), (0, 0, -1))

    # exhaust
    add_box(0, 5.8, -18.6, 0.7, 0.7, 1.0, {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})

    # suspension arms
    for sx in (1, -1):
        for (z, wr, wy) in ((12.6, 4.6, 4.6), (-11.8, 5.1, 5.1)):
            add_box(sx * 5.95, wy + 1.2, z, 1.5, 0.3, 0.5, {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})
            add_box(sx * 5.95, 1.6, z, 1.5, 0.3, 0.5, {k: "dark" for k in ("+x", "-x", "+y", "-y", "+z", "-z")})



def write_obj(name):
    lines = ["# procedural race car part for PEPit 3D engine", "mtllib %s.mtl" % name, "o %s" % name]
    for x, y, z in verts:
        lines.append("v %.3f %.3f %.3f" % (x, y, z))
    for u, v in uvs:
        lines.append("vt %.5f %.5f" % (u, v))
    for x, y, z in normals:
        lines.append("vn %.4f %.4f %.4f" % (x, y, z))
    lines.append("usemtl racecar")
    for va, vb, vc, ta, tb, tc, ni in faces:
        lines.append("f %d/%d/%d %d/%d/%d %d/%d/%d" % (
            va + 1, ta + 1, ni + 1, vb + 1, tb + 1, ni + 1, vc + 1, tc + 1, ni + 1))
    with open(os.path.join(OUT_DIR, name + ".obj"), "w") as f:
        f.write("\n".join(lines) + "\n")

def draw_atlas():
    img = Image.new("RGB", (TEX, TEX), (80, 80, 90))
    d = ImageDraw.Draw(img)

    x0, y0, x1, y1 = REGION["body"]
    for y in range(y0, y1 + 1):
        t = (y - y0) / (y1 - y0)
        r = int(235 - 95 * t)
        g = int(52 - 24 * t)
        b = int(48 - 20 * t)
        d.line([(x0, y), (x1, y)], fill=(r, g, b))
    d.rectangle([x0, y0 + 18, x1, y0 + 26], fill=(245, 245, 245))
    cx, cy, rad = x0 + 40, y0 + 40, 12
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(250, 250, 250))
    glyph = ["1111111", "0000010", "0000100", "0001000", "0010000", "0100000", "0100000"]
    gs = 2
    gx0, gy0 = cx - 7 * gs // 2, cy - 7 * gs // 2
    for gy, row in enumerate(glyph):
        for gx, ch in enumerate(row):
            if ch == "1":
                d.rectangle([gx0 + gx * gs, gy0 + gy * gs,
                             gx0 + gx * gs + gs - 1, gy0 + gy * gs + gs - 1],
                            fill=(170, 20, 20))

    x0, y0, x1, y1 = REGION["glass"]
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            t = (x + y - x0 - y0) / (x1 - x0 + y1 - y0)
            img.putpixel((x, y), (int(35 + 30 * t), int(55 + 40 * t), int(80 + 60 * t)))

    x0, y0, x1, y1 = REGION["white"]
    for y in range(y0, y1 + 1):
        t = (y - y0) / (y1 - y0)
        val = int(245 - 60 * t)
        d.line([(x0, y), (x1, y)], fill=(val, val, val))

    x0, y0, x1, y1 = REGION["dark"]
    d.rectangle([x0, y0, x1, y1], fill=(34, 34, 38))

    x0, y0, x1, y1 = REGION["tire"]
    for y in range(y0, y1 + 1):
        groove = (y - y0) % 6 < 2
        base = 26 if groove else 52
        d.line([(x0, y), (x1, y)], fill=(base, base, base + 4))

    x0, y0, x1, y1 = REGION["rim"]
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    rad = min(x1 - x0, y1 - y0) // 2
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(60, 60, 64))
    d.ellipse([cx - rad + 2, cy - rad + 2, cx + rad - 2, cy + rad - 2], fill=(185, 185, 192))
    for k in range(6):
        a = math.pi * k / 3
        for t in range(1, rad - 5):
            px = int(cx + t * math.cos(a))
            py = int(cy + t * math.sin(a))
            for w in (-2, -1, 0, 1, 2):
                qy = py + w
                if y0 < qy < y1:
                    img.putpixel((px, qy), (96, 96, 102))
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(210, 210, 216))

    x0, y0, x1, y1 = REGION["headlight"]
    d.rectangle([x0, y0, x1, y1], fill=(250, 238, 150))
    d.ellipse([x0 + 4, y0 + 2, x1 - 4, y1 - 2], fill=(255, 252, 220))

    x0, y0, x1, y1 = REGION["tail"]
    d.rectangle([x0, y0, x1, y1], fill=(205, 26, 26))
    d.ellipse([x0 + 3, y0 + 2, x1 - 3, y1 - 2], fill=(255, 70, 60))

    img.save(os.path.join(OUT_DIR, "racecar.bmp"))

def write_mtl(name):
    with open(os.path.join(OUT_DIR, name + ".mtl"), "w") as f:
        f.write("newmtl racecar\nKd 1.000 1.000 1.000\nmap_Kd racecar.bmp\n")

def build_wheel(name, r, half_w, segs):
    global verts, uvs, normals, faces
    verts, uvs, normals, faces = [], [], [], []
    add_wheel(0, 0, 0, r, half_w, segs)
    write_obj(name)
    write_mtl(name)
    print(name + ":", len(faces), "tris")

# hub centers used by the game scripts to place wheel entities under the car
WHEEL_POS = {"front": (9.0, 4.6, 12.6 * ZLEN), "rear": (9.4, 5.1, -11.8 * ZLEN)}

if __name__ == "__main__":
    build()
    write_obj("racecar")
    print("body:", len(faces), "tris")
    draw_atlas()
    write_mtl("racecar")
    build_wheel("wheel_front", 4.6, 1.45, 36)
    build_wheel("wheel_rear", 5.1, 1.7, 36)
