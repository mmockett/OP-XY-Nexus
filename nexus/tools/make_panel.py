#!/usr/bin/env python3
"""Nexus's screen: an isometric night city and a white flying car.

Writes ../panel.svg.fragment (the artwork) and ../city.h (the tables display.cpp
reads), from one description, so the two always agree. With --preview <dir> it
also draws a few states as SVG, through a mirror of display.cpp's frame logic,
for looking at the picture without building anything.

    python3 tools/make_panel.py [--preview <dir>]

Pure Python; nothing is compiled.

The city is a grid of lots seen from the isometric camera. Each lot has a fixed
set of nodes (a box with its roof, an optional setback tier, lit windows, a neon
strip, and on some lots a mast with rings or a sign blade), all sharing one
pivot: the centre of the lot on the ground. Each page of the endless city is a
hash of the lot's world position (display.cpp): the lot's scale (its height),
whether it has a tier, which windows are lit. The car flies across while notes
are held; at the edge the city turns the page and the car comes back.
"""
import json
import math
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
ID = json.loads((HERE / "definition.json").read_text())["id"]
M32 = 0xFFFFFFFF
C30 = math.cos(math.pi / 6)
CELL, FOOT = 64, 34
OX, OY = 240, 40                  # the camera at the start of a page
PAGE_CELLS = 4                    # a page turn moves the camera four lots along x


def hash32(i, j, k):
    """display.cpp's hash: the same integers on both sides."""
    h = (i * 374761393 + j * 668265263 + k * 1442695041) & M32
    h = ((h ^ (h >> 13)) * 1274126177) & M32
    return h ^ (h >> 16)


def r16(i, j, k):
    return hash32(i, j, k) >> 16


def proj(x, y, z):
    return ((x - y) * C30, (x + y) * 0.5 - z)


def num(v):
    s = f"{v:.1f}"
    s = s.rstrip("0").rstrip(".") if "." in s else s
    return "0" if s in ("-0", "") else s


# ------------------------------------------------------------------ palette (display.cpp names these)
PALETTE = [  # fill, stroke, width
    ("#000000", "#000000", 0.5),   # 0
    ("#AFAFAF", "#AFAFAF", 0.3),   # 1 a lit window, front face
    ("#7D7D7D", "#7D7D7D", 0.3),   # 2 a lit window, side face
    ("#E6E6E6", "#E6E6E6", 0.3),   # 3 a window flaring under touch
    ("#5A5A5A", "#5A5A5A", 0.3),   # 4..7 neon, dim to bright with reso
    ("#8C8C8C", "#8C8C8C", 0.3),
    ("#BDBDBD", "#BDBDBD", 0.3),
    ("#F0F0F0", "#F0F0F0", 0.3),
    ("#000000", "#4A4A4A", 0.9),   # 8..10 a mast's ring, faint to bright with ring
    ("#000000", "#7D7D7D", 0.9),
    ("#000000", "#BDBDBD", 0.9),
    ("#CDCDCD", "#CDCDCD", 0.3),   # 11 a lit sign
]
BODY = ("#181818", "#5E5E5E", 0.6)
ROOF = ("#2C2C2C", "#6E6E6E", 0.5)
TIER = ("#202020", "#5E5E5E", 0.6)
TIER_ROOF = ("#343434", "#6E6E6E", 0.5)
MAST = ("#BDBDBD", "#BDBDBD", 0.3)
BLADE = ("#141414", "#4A4A4A", 0.5)


class Node:
    def __init__(self, role, points, paint, closed=True, fill=True):
        self.role, self.points, self.paint, self.fill = role, points, paint, fill
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        self.ox = round((min(xs) + max(xs)) / 2)
        self.oy = round((min(ys) + max(ys)) / 2)


def quad_left(x0, x1, y, z0, z1):     # a quad on a face of constant y (facing down-left)
    return [proj(x0, y, z1), proj(x1, y, z1), proj(x1, y, z0), proj(x0, y, z0)]


def quad_right(x, y0, y1, z0, z1):    # a quad on a face of constant x (facing down-right)
    return [proj(x, y0, z1), proj(x, y1, z1), proj(x, y1, z0), proj(x, y0, z0)]


def silhouette(x0, y0, x1, y1, z0, z1):
    """A box's outline from the camera: starts at the back top corner, whose edges are long."""
    return [proj(x0, y0, z1), proj(x1, y0, z1), proj(x1, y0, z0), proj(x1, y1, z0),
            proj(x0, y1, z0), proj(x0, y1, z1)]


def roof(x0, y0, x1, y1, z, down):
    """The roof, with a thin ribbon down the front corner: the box's one visible inner edge."""
    back, right, front, left = proj(x0, y0, z), proj(x1, y0, z), proj(x1, y1, z), proj(x0, y1, z)

    def toward(a, b, d):
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy)
        return (a[0] + dx * d / n, a[1] + dy * d / n)
    fr, fl = toward(front, right, 0.45), toward(front, left, 0.45)
    return [back, right, fr, (fr[0], front[1] + down), (fl[0], front[1] + down), fl, left]


def ellipse(cx, cy, rx, ry, n=12):
    return [(cx + rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n)) for k in range(n)]


# ------------------------------------------------------------------ the lots
def lots():
    out = []
    for j in range(-6, 10):
        for i in range(-8, 12):
            bx, by = proj(i * CELL + CELL / 2, j * CELL + CELL / 2, 0)
            bx, by = OX + bx, OY + by
            if -42 <= bx <= 522 and 44 <= by <= 270:
                out.append((i, j, round(bx), round(by)))
    out.sort(key=lambda l: (l[0] + l[1], l[0]))       # back to front: the drawing order
    return out


def static(i, j):
    """A lot's fixed parts, from a hash of its slot (never changes with the page)."""
    rs = lambda k: hash32(i, j, 100 + k) / 2 ** 32
    s = {}
    s["h0"] = h0 = round(34 + 50 * rs(0))
    s["ht"] = ht = round(12 + 16 * rs(1))
    s["mast"] = rs(2) < 0.3
    s["mh"] = round(22 + 16 * rs(3))
    s["sign"] = (not s["mast"]) and rs(4) < 0.25
    floors = max(1, (h0 - 14) // 7)
    # four lit runs, two on each face, on different floors
    for k in range(4):
        f0 = int(rs(20 + k) * floors)
        f = f0 if k < 2 else (f0 + 1 + int(rs(30 + k) * (floors - 1))) % floors if floors > 1 else f0
        s["w%d" % k] = (-14 + rs(5 + k) * 14, 6 + rs(9 + k) * 8, 6 + f * 7)
    s["sx"] = -12 + rs(13) * 16
    s["sz"] = round(h0 * (0.2 + 0.2 * rs(12)))
    s["sh"] = round(h0 * 0.45)
    return s


def lot_nodes(i, j):
    s = static(i, j)
    h0, ht = s["h0"], s["ht"]
    a, t = FOOT / 2, FOOT / 2 - 5                     # half the footprint; the tier's
    n = [Node("body", silhouette(-a, -a, a, a, 0, h0), BODY),
         Node("roof", roof(-a, -a, a, a, h0, h0), ROOF)]
    for k in range(4):
        x, w, z = s["w%d" % k]
        if k % 2 == 0:
            n.append(Node("win%d" % k, quad_left(x, x + w, a + 0.2, z, z + 2.6), PALETTE[1]))
        else:
            n.append(Node("win%d" % k, quad_right(a + 0.2, x, x + w, z, z + 2.6), PALETTE[2]))
    n.append(Node("neon", quad_left(a - 3.4, a - 1.2, a + 0.2, 4, h0 - 3), PALETTE[5]))
    if s["sign"]:
        x0, z0, hs = s["sx"], s["sz"], s["sh"]
        n.append(Node("blade", silhouette(x0, a, x0 + 2, a + 8, z0, z0 + hs), BLADE))
        n.append(Node("glow", quad_left(x0 + 0.2, x0 + 1.8, a + 8.2, z0 + 2, z0 + hs - 2), PALETTE[11]))
    n.append(Node("tier", silhouette(-t, -t, t, t, h0, h0 + ht), TIER))
    n.append(Node("tierroof", roof(-t, -t, t, t, h0 + ht, ht), TIER_ROOF))
    if s["mast"]:
        mx, my = proj(0, 0, h0 + ht)
        mh = s["mh"]
        n.append(Node("mast", [(mx - 0.9, my), (mx + 0.9, my), (mx + 0.35, my - mh), (mx - 0.35, my - mh)], MAST))
        for q in range(2):
            n.append(Node("ring%d" % q, ellipse(mx, my - mh * (0.3 + 0.2 * q), 6.6 - 1.2 * q, 2.4 - 0.3 * q), PALETTE[8 + q], fill=False))
    return s, n


# ------------------------------------------------------------------ the car: an original wedge flyer
PROFILE = [[0, 3], [34, 2], [41, 3.5], [45, 6], [43.5, 8.5], [32, 10.5], [26, 11.5], [9, 12], [2.5, 11], [0, 8]]
CANOPY = [[9, 11.8], [27, 11.4], [22.5, 17.5], [12, 18], [7.5, 15]]
POD = [[1, 1.5], [19, 1.5], [21.5, 5.5], [18, 9.5], [4, 9.5], [0, 6]]


def hull(points):
    pts = sorted(set((round(p[0], 3), round(p[1], 3)) for p in points))
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0: lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0: upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def extrusion(prof, x, y0, y1, sc):
    return [proj(x + u * sc, yy, w * sc) for u, w in prof for yy in (y0, y1)]


def deck(prof, x, y0, y1, sc):
    """The upward faces of the body, as one strip: along the top of the profile at y0, back at y1."""
    ks = []
    for k in range(len(prof)):
        a, b = prof[k], prof[(k + 1) % len(prof)]
        nx, nz = b[1] - a[1], -(b[0] - a[0])
        if nx + nz > 0.01 and nz / math.hypot(nx, nz) > 0.55:
            ks.append(k)
    first, last = ks[0], ks[-1] + 1
    idx = list(range(first, last + 1))
    pts = [proj(x + prof[k % len(prof)][0] * sc, y0, prof[k % len(prof)][1] * sc) for k in idx]
    pts += [proj(x + prof[k % len(prof)][0] * sc, y1, prof[k % len(prof)][1] * sc) for k in reversed(idx)]
    return pts


def car_nodes(sc, lead):
    W = 14 * sc
    x = -22 * sc                                      # the car's reference: mid-body on the ground line
    n = []
    if lead:
        n.append(Node("farpod", hull(extrusion(POD, x + 3 * sc, -4 * sc, 0, sc)), ("#BEBEBE", "#000000", 0.5)))
    n.append(Node("hull", hull(extrusion(PROFILE, x, 0, W, sc)), ("#C8C8C8", "#000000", 0.5)))
    if lead:
        n.append(Node("deck", deck(PROFILE, x, 0, W, sc), ("#F2F2F2", "#000000", 0.4)))
    n.append(Node("side", [proj(x + u * sc, W, w * sc) for u, w in PROFILE], ("#E2E2E2", "#000000", 0.5)))
    n.append(Node("glass", hull(extrusion(CANOPY, x, 2 * sc, W - 2 * sc, sc)), ("#303030", "#F2F2F2", 0.7)))
    if lead:
        n.append(Node("lamp", [proj(x + 13 * sc, 4 * sc, 18.6 * sc), proj(x + 20 * sc, 4 * sc, 18.4 * sc),
                               proj(x + 20 * sc, W - 4 * sc, 18.4 * sc), proj(x + 13 * sc, W - 4 * sc, 18.6 * sc)],
                      ("#FFFFFF", "#000000", 0.5)))
    n.append(Node("nearpod", hull(extrusion(POD, x + 3 * sc, W, W + 4 * sc, sc)), ("#D6D6D6", "#000000", 0.5)))
    # every part of a car turns about one origin, so they move as one
    ox = round(sum(min(p[0] for p in q.points) + max(p[0] for p in q.points) for q in n) / (2 * len(n)))
    oy = round(sum(min(p[1] for p in q.points) + max(p[1] for p in q.points) for q in n) / (2 * len(n)))
    for q in n:
        q.ox, q.oy = ox, oy
    return n


# ------------------------------------------------------------------ checks (the device's drawing rules)
def check(node, where):
    pts = node.points
    assert 3 <= len(pts) <= 64, where
    for k, p in enumerate(pts):
        q = pts[(k + 1) % len(pts)]
        d = math.hypot(q[0] - p[0], q[1] - p[1])
        assert d >= 0.125, f"{where}: points {k} and {k + 1} are {d:.3f} px apart"
        assert abs(p[0] - node.ox) <= 63 and abs(p[1] - node.oy) <= 63, f"{where}: point past 63 px from its origin"
    close = math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1])
    assert close >= 0.25, f"{where}: closing edge {close:.3f} px"
    # no turn within 2.5 degrees of straight back
    for k in range(len(pts)):
        a, b, c = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
        u = (a[0] - b[0], a[1] - b[1])
        v = (c[0] - b[0], c[1] - b[1])
        ang = math.degrees(math.acos(max(-1, min(1, (u[0] * v[0] + u[1] * v[1]) / (math.hypot(*u) * math.hypot(*v))))))
        assert ang >= 2.5, f"{where}: turn of {ang:.2f} degrees at point {k}"


# ------------------------------------------------------------------ the whole scene
def scene():
    L = lots()
    nodes, lot_rows = [], []
    first = 8                                          # nodes 0..7: the two-page header's bars
    for (i, j, bx, by) in L:
        s, ns = lot_nodes(i, j)
        for q in ns:
            q.ox_abs, q.oy_abs = q.ox, q.oy           # relative to the lot's pivot
        lot_rows.append(dict(i=i, j=j, bx=bx, by=by, first=first + len(nodes), sign=s["sign"], mast=s["mast"], s=s))
        nodes += ns
    lead, wing = car_nodes(1.5, True), car_nodes(0.95, False)
    car_first = first + len(nodes)
    nodes += lead + wing
    return L, lot_rows, nodes, lead, wing, car_first


def write_panel(lot_rows, nodes, lead, wing, car_first):
    out = [f'<g id="{ID}_panel">']
    for k, q in enumerate(nodes):
        check(q, f"node {8 + k} ({q.role})")
        fill, stroke, width = q.paint
        d = "M" + " L".join(f"{num(p[0] - q.ox)},{num(p[1] - q.oy)}" for p in q.points) + " Z"
        f = fill if q.fill else "none"
        out.append(f'<path id="{ID}_node_{8 + k}" d="{d}" fill="{f}" stroke="{stroke}" stroke-width="{num(width)}"/>')
    for k, (fill, stroke, width) in enumerate(PALETTE):
        out.append(f'<circle id="{ID}_palette_{k}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>')
    out.append(HEADER.replace("velvet_", ID + "_").strip())
    out.append("</g>")
    text = "\n".join(out) + "\n"
    assert len(text.encode()) <= 65536, f"artwork is {len(text.encode())} bytes, over 64 KiB"
    (HERE / "panel.svg.fragment").write_text(text)
    return len(text.encode())


def write_tables(lot_rows, nodes, lead, wing, car_first):
    n = len(nodes) + 8
    L = []
    L.append("// Generated by tools/make_panel.py from the same description as panel.svg.fragment: do not edit.")
    L.append("#pragma once")
    L.append('#include <opxy/types.hpp>')
    L.append("")
    L.append(f"namespace {ID} {{ namespace city {{")
    L.append(f"const opxy::u32 node_count = {n};")
    L.append(f"const opxy::u32 lot_count = {len(lot_rows)};")
    L.append(f"const opxy::u32 car_first = {car_first};      // the lead car's {len(lead)} nodes, then the wingman's {len(wing)}")
    L.append(f"const opxy::u32 lead_nodes = {len(lead)}, wing_nodes = {len(wing)};")
    L.append(f"const opxy::i32 lead_x = {lead[0].ox}, lead_y = {lead[0].oy}, wing_x = {wing[0].ox}, wing_y = {wing[0].oy};")
    L.append("// A lot: its slot in the grid, its pivot on screen (px), its first node, and whether it has")
    L.append("// a sign blade (two nodes after the neon) and a mast with two rings (three after the tier).")
    L.append("struct Lot { signed char i, j; short x, y; unsigned short first; unsigned char sign, mast; };")
    L.append("const Lot lots[lot_count] = {")
    for r in lot_rows:
        L.append(f"    {{{r['i']}, {r['j']}, {r['bx']}, {r['by']}, {r['first']}, {int(r['sign'])}, {int(r['mast'])}}},")
    L.append("};")
    L.append("// Each node's origin from its lot's pivot, px at unit scale (cars: from the car's reference).")
    L.append("const short origin_x[node_count] = {0, 0, 0, 0, 0, 0, 0, 0,")
    L.append("    " + ", ".join(str(q.ox) for q in nodes) + "};")
    L.append("const short origin_y[node_count] = {0, 0, 0, 0, 0, 0, 0, 0,")
    L.append("    " + ", ".join(str(q.oy) for q in nodes) + "};")
    L.append("}}")
    (HERE / "city.h").write_text("\n".join(L) + "\n")


# The SDK's two-page header block, as `sdk.py new --controls 8` writes it, with its
# bars on nodes 0..7. It comes last, so it sits over the tops of the tallest towers.
HEADER = """
<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>
<path id="velvet_node_0" transform="matrix(1 0 0 1 0 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#000000" stroke="#000000" stroke-width="0.5"/>
<path id="velvet_node_1" transform="matrix(1 0 0 1 60 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#323232" stroke="#323232" stroke-width="0.5"/>
<path id="velvet_node_2" transform="matrix(1 0 0 1 120 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#4B4B4B" stroke="#4B4B4B" stroke-width="0.5"/>
<path id="velvet_node_3" transform="matrix(1 0 0 1 180 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#646464" stroke="#646464" stroke-width="0.5"/>
<path id="velvet_node_4" transform="matrix(1 0 0 1 240 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#7D7D7D" stroke="#7D7D7D" stroke-width="0.5"/>
<path id="velvet_node_5" transform="matrix(1 0 0 1 300 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#969696" stroke="#969696" stroke-width="0.5"/>
<path id="velvet_node_6" transform="matrix(1 0 0 1 360 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#AFAFAF" stroke="#AFAFAF" stroke-width="0.5"/>
<path id="velvet_node_7" transform="matrix(1 0 0 1 420 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="#CDCDCD" stroke="#CDCDCD" stroke-width="0.5"/>
<text id="velvet_text_0" transform="matrix(1 0 0 1 5 10)" fill="#CBCBCB" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_1" transform="matrix(1 0 0 1 125 10)" fill="#CBCBCB" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_2" transform="matrix(1 0 0 1 245 10)" fill="#000000" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_3" transform="matrix(1 0 0 1 365 10)" fill="#000000" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_4" transform="matrix(1 0 0 1 65 17.4)" fill="#CBCBCB" font-family="UniversNextPro-Light" font-size="20px">-</text>
<text id="velvet_text_5" transform="matrix(1 0 0 1 185 17.4)" fill="#CBCBCB" font-family="UniversNextPro-Light" font-size="20px">-</text>
<text id="velvet_text_6" transform="matrix(1 0 0 1 305 17.4)" fill="#000000" font-family="UniversNextPro-Light" font-size="20px">-</text>
<text id="velvet_text_7" transform="matrix(1 0 0 1 425 17.4)" fill="#000000" font-family="UniversNextPro-Light" font-size="20px">-</text>
<text id="velvet_text_8" transform="matrix(1 0 0 1 5 33)" fill="#5A5A5A" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_9" transform="matrix(1 0 0 1 125 33)" fill="#5A5A5A" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_10" transform="matrix(1 0 0 1 245 33)" fill="#5A5A5A" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_11" transform="matrix(1 0 0 1 365 33)" fill="#5A5A5A" font-family="UniversNextPro-Light" font-size="10px">-</text>
<text id="velvet_text_12" transform="matrix(1 0 0 1 65 33)" fill="#707070" font-family="UniversNextPro-Light" font-size="13px">-</text>
<text id="velvet_text_13" transform="matrix(1 0 0 1 185 33)" fill="#707070" font-family="UniversNextPro-Light" font-size="13px">-</text>
<text id="velvet_text_14" transform="matrix(1 0 0 1 305 33)" fill="#707070" font-family="UniversNextPro-Light" font-size="13px">-</text>
<text id="velvet_text_15" transform="matrix(1 0 0 1 425 33)" fill="#707070" font-family="UniversNextPro-Light" font-size="13px">-</text>
"""


# ------------------------------------------------------------------ a mirror of display.cpp's frame, for previews
def district(i, j):
    d = r16((i + 1000) // 5 - 200, (j + 1000) // 4 - 250, 91)
    return 0 if d < 32768 else 1 if d < 52429 else 2


def smooth16(u):                                      # 3u^2 - 2u^3, Q16
    u2 = (u * u) >> 16
    return (u2 * (196608 - 2 * u)) >> 16


def wave16(ph, s):
    sine = smooth16(2 * ph) if ph < 32768 else smooth16(131070 - 2 * ph)
    saw = ph
    sq = 65535 if ph < 32768 else 0
    if s < 21845:
        return sine + (((saw - sine) * (s * 3)) >> 16)
    if s < 43690:
        return saw + (((sq - saw) * ((s - 21845) * 3)) >> 16)
    pw = 32768 - (((s - 43690) * 3 * 24904) >> 16)
    return 65535 if ph < pw else 0


def lot_state(row, page, c, held=False, hold_q10=0, serial=0):
    """display.cpp's desired_state: what a lot shows on a page at the controls c (Q16 0..65535)."""
    wi, wj = row["i"] + PAGE_CELLS * page, row["j"]
    dist = district(wi, wj)
    r = lambda k: r16(wi, wj, k)
    sky = wave16((wi * 9362 + wj * 5041) & 0xFFFF, c["shape"])
    mix = (sky * 39322 + r(1) * 26214) >> 16
    code = [24 + ((mix * 18) >> 16), 20 + ((mix * 11) >> 16), 18 + ((mix * 8) >> 16)][dist]
    tier = (dist == 0 and r(2) < 39322) or (dist == 1 and r(2) < 22938)
    ta = 154 + ((c["touch"] * 973) >> 16)
    win = []
    for k in range(4):
        if r(20 + k) < c["bright"]:
            win.append(1)
        elif held and c["touch"] and r16(wi * 7 + k, wj, serial) < c["touch"] >> 2 and hold_q10 >= ((r(40 + k) * ta) >> 16):
            win.append(2)
        else:
            win.append(0)
    neon = min(4, 1 + ((c["reso"] * 4) >> 16)) if r(6) > 36045 else 0
    mast = row["mast"] and tier
    g = c["ring"]
    ring0 = (0 if g == 0 else 1 + min(2, (g * 6) >> 16)) if mast else 0
    ring1 = (0 if g < 32768 else 1 + min(2, ((g - 32768) * 6) >> 16)) if mast else 0
    sign = row["sign"] and r(7) > 26214
    lit = sign and r(8) < c["bright"] + 9830
    return dict(code=code, tier=tier, win=win, neon=neon, mast=mast, ring=(ring0, ring1), sign=sign, lit=lit)


def preview(out_dir, name, c, page=0, held=False, car_x=None, hold_q10=0, serial=1):
    L, rows, nodes, lead, wing, car_first = scene()
    place = {}                                         # node index -> (x, y, scale, palette or None)
    for row in rows:
        st = lot_state(row, page, c, held, hold_q10, serial)
        s = st["code"] / 32
        roles = ["body", "roof", "win0", "win1", "win2", "win3", "neon"] + (["blade", "glow"] if row["sign"] else []) + \
                ["tier", "tierroof"] + (["mast", "ring0", "ring1"] if row["mast"] else [])
        for k, role in enumerate(roles):
            q = nodes[row["first"] - 8 + k]
            show, pal = True, None
            if role.startswith("win"):
                w = st["win"][int(role[3])]
                show = w > 0
                pal = 3 if w == 2 else (2 if int(role[3]) % 2 else 1)
            elif role == "neon":
                show, pal = st["neon"] > 0, 3 + st["neon"]
            elif role in ("tier", "tierroof"):
                show = st["tier"]
            elif role == "mast":
                show = st["mast"]
            elif role.startswith("ring"):
                sh = st["ring"][int(role[4])]
                show, pal = sh > 0, 7 + sh
            elif role == "blade":
                show = st["sign"]
            elif role == "glow":
                show, pal = st["lit"], 11
            if show:
                place[row["first"] + k] = (row["bx"] + s * q.ox, row["by"] + s * q.oy, s, pal)
    # the cars
    ribbon = c["ribbon"] / 65536
    cz = 170 + 30 * ribbon
    cx = (-150 + 300 if car_x is None else car_x)
    sx, sy = proj(cx, 300, cz)
    for k in range(len(lead)):
        place[car_first + k] = (OX + sx + lead[0].ox, OY + sy + lead[0].oy, 1, None)
    det = c["detune"] / 65536
    if det > 0.03:
        sx, sy = proj(cx - 50, 300 + 20 + 24 * det, cz - 10 + 10 * det)
        for k in range(len(wing)):
            place[car_first + len(lead) + k] = (OX + sx + wing[0].ox, OY + sy + wing[0].oy, 1, None)
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="960" height="444">',
           '<style>@font-face{font-family:UniversNextPro-Light;src:url(file:///Users/mmockett/Documents/GitHub/OP-XY/tools/render/font/UniversNextPro-Light.ttf)}</style>',
           '<rect width="480" height="222" fill="#000"/>']
    for k, q in enumerate(nodes):
        idx = 8 + k
        if idx not in place:
            continue
        x, y, s, pal = place[idx]
        fill, stroke, width = q.paint if pal is None else PALETTE[pal]
        if not q.fill:
            fill = "none"
        d = "M" + " L".join(f"{num(p[0] - q.ox)},{num(p[1] - q.oy)}" for p in q.points) + " Z"
        svg.append(f'<path transform="matrix({s:.4f} 0 0 {s:.4f} {x:.2f} {y:.2f})" d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')
    svg.append('<rect width="480" height="38" fill="#000"/>')
    shades = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for k, g in enumerate(shades):
        svg.append(f'<rect x="{60 * k}" width="60" height="20" fill="{g}"/>')
    titles = c.get("titles", ["shape", "detune", "bright", "reso"])
    values = c.get("values", ["", "", "", ""])
    for k in range(4):
        col = "#CBCBCB" if k < 2 else "#000"
        svg.append(f'<text x="{5 + 120 * k}" y="10" font-family="UniversNextPro-Light" font-size="10" fill="{col}">{titles[k]}</text>')
        svg.append(f'<text x="{65 + 120 * k}" y="17.4" font-family="UniversNextPro-Light" font-size="20" fill="{col}">{values[k]}</text>')
    svg.append("</svg>")
    pathlib.Path(out_dir).mkdir(parents=True, exist_ok=True)
    (pathlib.Path(out_dir) / f"{name}.svg").write_text("\n".join(svg))


def main():
    L, rows, nodes, lead, wing, car_first = scene()
    size = write_panel(rows, nodes, lead, wing, car_first)
    write_tables(rows, nodes, lead, wing, car_first)
    print(f"{len(rows)} lots, {len(nodes) + 8} nodes ({len(lead)} + {len(wing)} car), artwork {size} bytes")
    if "--preview" in sys.argv:
        out = sys.argv[sys.argv.index("--preview") + 1]
        q = lambda v: min(65535, int(v * 65536))
        tyrell = dict(shape=q(.33), detune=q(.22), bright=q(.46), reso=q(.28), ring=0, touch=q(.8), ribbon=q(.5),
                      values=["33", "3c", "920", "28"])
        preview(out, "tyrell", tyrell)
        preview(out, "tyrell-held", tyrell, held=True, car_x=-20, hold_q10=900)
        preview(out, "tyrell-page1", tyrell, page=1)
        zero = {k: 0 for k in ("shape", "detune", "bright", "reso", "ring", "touch", "ribbon")}
        preview(out, "all0", zero)
        full = {k: 65535 for k in zero}
        preview(out, "all100", full)
        preview(out, "ring100", dict(tyrell, ring=65535))


if __name__ == "__main__":
    main()
