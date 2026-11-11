#!/usr/bin/env python3
"""Nexus's screen: a still isometric night city and a white flying car.

Writes ../panel.svg.fragment (the artwork), ../city.h (the tables display.cpp
reads) and ../tests/outlines.h (each outline's measures, for the host checks),
from one description, so they always agree. With --preview <dir> it also draws
a few states as SVG, through a mirror of display.cpp's frame logic, for looking
at the picture without building anything.

    python3 tools/make_panel.py [--preview <dir>]

Pure Python; nothing is compiled.

The city is one fixed grid of lots seen from the isometric camera, the same
every time. Each lot is a tower: one outline for its body (a box with up to two
setback tiers and sometimes a mast, its inner edges drawn as thin slits), six
windows that light one by one, and sometimes a neon strip, a blade sign, and on
a mast two rings and a light that blinks. Every part comes from a hash of the
lot's index. A tower is filled black only where it hides something behind it.

Three level lanes of small traffic cross the sky at different depths, drawn
between the rows they pass behind, and a searchlight sweeps from one roof. The
white car and its wingman fly level across in front of it all.
"""
import json
import math
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
ID = json.loads((HERE / "definition.json").read_text())["id"]
M32 = 0xFFFFFFFF
C30 = math.cos(math.pi / 6)
CELL = 64                         # the grid's pitch, world px
OX, OY = 240, 40                  # the camera
SLIT = 0.4                        # an inner edge: a slit this wide, drawn by its two sides
MOST_WINDOWS = 10                 # window nodes on a lot, at most
WINDOW_BYTES = 32150              # the artwork's share for windows
SMALLEST_SCALE = 614 / 1024       # display.cpp's smallest lot scale
BUDGET = 280                      # points of changed outlines a frame (display.cpp's budget)


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
    ("#B4B4B4", "#B4B4B4", 0.3),   # 1 a lit window, left face
    ("#828282", "#828282", 0.3),   # 2 a lit window, right face
    ("#F0F0F0", "#F0F0F0", 0.3),   # 3 a window flaring under touch
    ("#505050", "#505050", 0.3),   # 4..7 neon, dim to bright with reso
    ("#787878", "#787878", 0.3),
    ("#A0A0A0", "#A0A0A0", 0.3),
    ("#C8C8C8", "#C8C8C8", 0.3),
    ("#000000", "#464646", 0.8),   # 8..10 a mast's ring, faint to bright with ring
    ("#000000", "#7D7D7D", 0.8),
    ("#000000", "#C3C3C3", 0.8),
    ("#BEBEBE", "#BEBEBE", 0.3),   # 11 a lit sign
    ("#2E2E2E", "#2E2E2E", 0.3),   # 12 a mast light, off
    ("#F5F5F5", "#F5F5F5", 0.3),   # 13 a mast light, on
]
EDGE = ["#3C3C3C", "#444444", "#4E4E4E", "#585858", "#626262", "#6C6C6C", "#767676", "#808080"]  # by row, back to front
BLADE = ("#000000", "#5A5A5A", 0.5)
BEAM = ("#000000", "#3C3C3C", 0.4)
LANE_PAINT = ["#5C5C5C", "#8A8A8A", "#B8B8B8"]


class Node:
    def __init__(self, role, points, paint, fill=True, origin=None):
        self.role, self.points, self.paint, self.fill = role, points, paint, fill
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        if origin is None:
            origin = (round((min(xs) + max(xs)) / 2), round((min(ys) + max(ys)) / 2))
        self.ox, self.oy = origin


# ------------------------------------------------------------------ outline geometry
def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def add(a, b): return (a[0] + b[0], a[1] + b[1])
def mul(a, s): return (a[0] * s, a[1] * s)
def length(a): return math.hypot(a[0], a[1])
def unitv(a): n = length(a); return (a[0] / n, a[1] / n)
def cross(a, b): return a[0] * b[1] - a[1] * b[0]


def toward(a, b, d):
    """The point d px from a on the way to b."""
    return add(a, mul(unitv(sub(b, a)), d))


def left_normal(a, b):
    d = unitv(sub(b, a))
    return (d[1], -d[0])


def meet(p1, d1, p2, d2):
    """Where the line p1 + t d1 meets p2 + s d2."""
    den = cross(d1, d2)
    t = cross(sub(p2, p1), d2) / den
    return add(p1, mul(d1, t))


def tree_contour(points, edges, root, w):
    """The outline around a tree of thin lines, a slit w wide: out along one side of each
    branch and back along the other, a flat end at each tip. Returns the points from the
    root's one side to its other. edges: {vertex: [neighbours]}; the tour takes each
    vertex's branches in turn by angle, so the outline never crosses itself."""
    tour = []

    def walk(v, parent):
        nbrs = [n for n in edges[v] if n != parent]
        if parent is not None:
            back = math.atan2(*reversed(sub(points[parent], points[v])))
            nbrs.sort(key=lambda n: (math.atan2(*reversed(sub(points[n], points[v]))) - back) % (2 * math.pi))
        for n in nbrs:
            tour.append((v, n))
            walk(n, v)
            tour.append((n, v))
    walk(root, None)
    h = w / 2
    lines = []
    for a, b in tour:
        n = left_normal(points[a], points[b])
        lines.append((add(points[a], mul(n, h)), sub(points[b], points[a]), n))
    out = [lines[0][0]]
    for k in range(len(tour) - 1):
        (p1, d1, n1), (p2, d2, n2) = lines[k], lines[k + 1]
        if tour[k][0] == tour[k + 1][1] and tour[k][1] == tour[k + 1][0]:   # a tip: a flat end
            tip = points[tour[k][1]]
            out += [add(tip, mul(n1, h)), add(tip, mul(n2, h))]
        else:
            out.append(meet(p1, d1, p2, d2))
    p, d, n = lines[-1]
    out.append(add(p, d))
    return out


def segments_cross(a, b, c, d):
    def orient(p, q, r): return cross(sub(q, p), sub(r, p))
    o1, o2, o3, o4 = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
    return (o1 > 1e-9) != (o2 > 1e-9) and (o3 > 1e-9) != (o4 > 1e-9) and \
        abs(o1) > 1e-9 and abs(o2) > 1e-9 and abs(o3) > 1e-9 and abs(o4) > 1e-9


def simple(pts):
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            if segments_cross(a, b, pts[j], pts[(j + 1) % n]):
                return False
    return True


def splice(ring, at, contour):
    """Replaces ring[at] with a slit's contour, whichever way round keeps it simple."""
    for c in (contour, list(reversed(contour))):
        trial = ring[:at] + c + ring[at + 1:]
        if simple(trial):
            return trial
    raise AssertionError("a slit crosses the outline")


def area(pts):
    return 0.5 * sum(cross(pts[k], pts[(k + 1) % len(pts)]) for k in range(len(pts)))


def start_long(pts):
    """Rotates an outline to close on its longest edge: a short closing edge draws a sliver."""
    best = max(range(len(pts)), key=lambda k: length(sub(pts[(k + 1) % len(pts)], pts[k])))
    return pts[best + 1:] + pts[:best + 1]


def inside(p, poly):
    c = False
    n = len(poly)
    for k in range(n):
        a, b = poly[k], poly[(k + 1) % n]
        if (a[1] > p[1]) != (b[1] > p[1]):
            x = a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if x > p[0]:
                c = not c
    return c


def overlap(p, q):
    """Whether two simple outlines overlap."""
    for i in range(len(p)):
        for j in range(len(q)):
            if segments_cross(p[i], p[(i + 1) % len(p)], q[j], q[(j + 1) % len(q)]):
                return True
    return inside(p[0], q) or inside(q[0], p)


def clip_area(pts, x0, y0, x1, y1):
    """The outline's area inside a box (Sutherland-Hodgman)."""
    def clip(poly, keep, cut):
        out = []
        for k in range(len(poly)):
            a, b = poly[k - 1], poly[k]
            if keep(b):
                if not keep(a):
                    out.append(cut(a, b))
                out.append(b)
            elif keep(a):
                out.append(cut(a, b))
        return out

    def at_x(x):
        return lambda a, b: (x, a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0]))

    def at_y(y):
        return lambda a, b: (a[0] + (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]), y)
    for keep, cut in ((lambda p: p[0] >= x0, at_x(x0)), (lambda p: p[0] <= x1, at_x(x1)),
                      (lambda p: p[1] >= y0, at_y(y0)), (lambda p: p[1] <= y1, at_y(y1))):
        pts = clip(pts, keep, cut)
        if not pts:
            return 0.0
    return abs(area(pts))


# ------------------------------------------------------------------ the lots
def lots():
    out = []
    for j in range(-6, 10):
        for i in range(-8, 12):
            bx, by = proj(i * CELL + CELL / 2, j * CELL + CELL / 2, 0)
            bx, by = OX + bx, OY + by
            if -40 <= bx <= 520 and 44 <= by <= 270:
                out.append((i, j, round(bx), round(by)))
    out.sort(key=lambda l: (l[0] + l[1], l[0]))       # back to front: the drawing order
    return [dict(t=t, i=i, j=j, x=x, y=y, row=i + j + 1, column=round((x + 40) / (CELL * C30)))
            for t, (i, j, x, y) in enumerate(out)]


def static(t):
    """A lot's fixed parts, from a hash of its index."""
    rs = lambda k: hash32(t, 0, 100 + k) / 2 ** 32
    s = {}
    a0 = 11 + round(5 * rs(0))
    h0 = round(30 + 44 * rs(1))
    r = rs(2)
    tiers = 0 if r < 0.3 else 1 if r < 0.75 else 2
    boxes = [(a0, h0)]
    a = a0
    for k in range(tiers):
        a = a - 3 - round(2 * rs(3 + k))
        boxes.append((a, round((12 + 12 * rs(5 + k)) if k == 0 else (7 + 7 * rs(7)))))
    s["mast"] = tiers > 0 and rs(8) < 0.42
    s["mh"] = round(16 + 12 * rs(9))
    s["neon"] = rs(10) < 0.55
    s["sign"] = not s["mast"] and rs(11) < 0.35
    # the whole outline stays within 62 px of its middle
    over = 2 * a0 + sum(h for _, h in boxes) + s["mh"] * s["mast"] - 124
    if over > 0:
        boxes[0] = (a0, h0 - over)
    s["boxes"] = boxes
    return s


def stack(s):
    """Each box's half width and its floor and roof heights."""
    out, z = [], 0
    for a, h in s["boxes"]:
        out.append((a, z, z + h))
        z += h
    return out


def building(s):
    """The building as one outline: its silhouette, with its inner edges as slits."""
    B = stack(s)
    P = lambda a, b, z: proj(a, b, z)
    n = len(B) - 1
    an, _, zt = B[n]
    ring = [P(-an, -an, zt), P(an, -an, zt)]
    xr, xl = {}, {}
    for k in range(n, 0, -1):
        a, z0, _ = B[k]
        ap = B[k - 1][0]
        xr[k] = (2 * a * C30, a - ap - z0)
        ring += [xr[k], P(ap, -ap, z0)]
    a0, _, z1 = B[0]
    ring += [P(a0, -a0, 0), P(a0, a0, 0), P(-a0, a0, 0), P(-a0, a0, z1)]
    for k in range(1, n + 1):
        a, z0, z1k = B[k]
        ap = B[k - 1][0]
        xl[k] = (-2 * a * C30, a - ap - z0)
        ring += [xl[k], P(-a, a, z1k)]
    top_at = ring[0]
    # the base box's inner edges: up the front corner and along the roof's two front edges
    F, Fb, L, R = P(a0, a0, z1), P(a0, a0, 0), P(-a0, a0, z1), P(a0, -a0, z1)
    pts = {"F": F, "Fb": Fb, "L": toward(L, F, 0.8), "R": toward(R, F, 0.8)}
    ring = splice(ring, ring.index(Fb), tree_contour(pts, {"Fb": ["F"], "F": ["Fb", "L", "R"], "L": ["F"], "R": ["F"]},
                                                  "Fb", SLIT))
    for k in range(1, n + 1):
        a, z0, zk = B[k]
        Lt, Ft, Rt = P(-a, a, zk), P(a, a, zk), P(a, -a, zk)
        Ftb, Ltb, Rtb = P(a, a, z0), P(-a, a, z0), P(a, -a, z0)
        tp = {"Lt": Lt, "Ft": Ft, "Rt": toward(Rt, Ft, 0.8), "Ftb": Ftb, "Ltb": Ltb, "Rtb": Rtb,
              "Xl": toward(xl[k], Ltb, 0.8), "Xr": toward(xr[k], Rtb, 0.8)}
        tree = {"Lt": ["Ft"], "Ft": ["Lt", "Rt", "Ftb"], "Rt": ["Ft"], "Ftb": ["Ft", "Ltb", "Rtb"],
                "Ltb": ["Ftb", "Xl"], "Rtb": ["Ftb", "Xr"], "Xl": ["Ltb"], "Xr": ["Rtb"]}
        ring = splice(ring, ring.index(Lt), tree_contour(tp, tree, "Lt", SLIT))
    if s["mast"]:
        # a mast on the topmost corner: a thin spire, part of the same outline
        i = ring.index(top_at)
        prev, nxt = ring[i - 1], ring[(i + 1) % len(ring)]
        mx, my = top_at
        wl = meet(prev, sub(top_at, prev), (mx - 0.9, my), (0, 1))
        wr = meet(top_at, sub(nxt, top_at), (mx + 0.9, my), (0, 1))
        spire = [wl, (mx - 0.4, my - s["mh"]), (mx + 0.4, my - s["mh"]), wr]
        ring = ring[:i] + spire + ring[i + 1:]
        s["mast_top"] = (mx, my - s["mh"])
        s["mast_foot"] = (mx, my)
    s["roof"] = top_at
    return start_long(ring)


def quad_left(x0, x1, y, z0, z1):     # a quad on a face of constant y (the left face)
    return [proj(x0, y, z1), proj(x1, y, z1), proj(x1, y, z0), proj(x0, y, z0)]


def quad_right(x, y0, y1, z0, z1):    # a quad on a face of constant x (the right face)
    return [proj(x, y0, z1), proj(x, y1, z1), proj(x, y1, z0), proj(x, y0, z0)]


def windows(t, s):
    """Candidate windows: lit runs on the base box's two faces and the first tier's, one at
    most a floor on each face (so they never touch), clear of the neon strip and the sign,
    in an order from the hash. Each is (face, outline): face 0 left, 1 right."""
    B = stack(s)
    out = []
    for b in range(min(2, len(B))):
        a, z0, z1 = B[b]
        floors = max(1, int((z1 - z0 - 6) // 5))
        for face in (0, 1):
            for f in range(floors):
                rs = lambda q: hash32(t, b * 64 + face * 32 + f, 200 + q) / 2 ** 32
                if rs(0) > (0.62 if b == 0 else 0.5):
                    continue
                z = z0 + 3 + f * 5
                w = 3.5 + rs(1) * (4.5 if b else 7)
                lo, hi = -a + 2, a - 2 - w
                if b == 0 and face == 0 and s["sign"]:
                    hi = min(hi, a - 7.5 - w)
                if b == 0 and face == 1 and s["neon"]:
                    lo = max(lo, -a + 4.5)
                if hi < lo:
                    continue
                x = lo + rs(2) * (hi - lo)
                if face == 0:
                    q = quad_left(x, x + w, a + 0.2, z, z + 2.4)
                else:
                    q = quad_right(a + 0.2, -x - w, -x, z, z + 2.4)
                out.append((hash32(t, len(out), 250), face, q))
    out.sort()
    return [(face, q) for _, face, q in out]


def neon(s):
    """A neon strip up the right face, by its right corner."""
    a, _, h = stack(s)[0]
    return quad_right(a + 0.2, -a + 1.4, -a + 3.2, 4, h - 4)


def sign(t, s):
    """A blade sign standing off the left face, and its lit panel."""
    a, _, h = stack(s)[0]
    x0 = a - 4.5
    z0 = round(h * (0.3 + 0.25 * hash32(t, 0, 120) / 2 ** 32))
    hs = min(h - z0 - 4, round(10 + 8 * hash32(t, 0, 121) / 2 ** 32))
    blade = [proj(x0, a, z0 + hs), proj(x0 + 1.6, a, z0 + hs), proj(x0 + 1.6, a + 7, z0 + hs),
             proj(x0 + 1.6, a + 7, z0), proj(x0, a + 7, z0), proj(x0, a + 7, z0 + hs)]
    glow = [proj(x0 + 1.6 + 0.2, a + 1.2, z0 + hs - 1.6), proj(x0 + 1.6 + 0.2, a + 5.8, z0 + hs - 1.6),
            proj(x0 + 1.6 + 0.2, a + 5.8, z0 + 1.6), proj(x0 + 1.6 + 0.2, a + 1.2, z0 + 1.6)]
    return start_long(blade), glow


def ellipse(cx, cy, rx, ry, n=12):
    return [(cx + rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n)) for k in range(n)]


def seen(lot, pts):
    """Whether an outline of a lot is ever on screen below the header, at any of its scales."""
    cx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
    cy = (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2
    return any(3 <= lot["x"] + s * cx <= 477 and 41 <= lot["y"] + s * cy <= 217 for s in (SMALLEST_SCALE, 1307 / 1024))


def lot_nodes(lot, count):
    t = lot["t"]
    s = static(t)
    n = [Node("body", building(s), ("#000000", EDGE[lot["row"]], 0.5))]
    for k, (face, q) in enumerate(lot["wins"][:count]):
        n.append(Node("win%d" % k, q, PALETTE[1 + face]))
    if s["neon"]:
        n.append(Node("neon", neon(s), PALETTE[5]))
    if s["sign"]:
        blade, glow = sign(t, s)
        n.append(Node("blade", blade, BLADE))
        n.append(Node("glow", glow, PALETTE[11]))
    if s["mast"]:
        mx, my = s["mast_top"]
        mh = s["mh"]
        n.append(Node("ring0", ellipse(mx, my + mh * 0.62, 7.4, 2.6), PALETTE[9], fill=False))
        n.append(Node("ring1", ellipse(mx, my + mh * 0.34, 5.6, 2.0), PALETTE[9], fill=False))
        n.append(Node("light", [(mx, my - 2.9), (mx + 1.5, my - 1.3), (mx, my + 0.3), (mx - 1.5, my - 1.3)], PALETTE[13]))
    return s, n


# ------------------------------------------------------------------ the car: an original wedge flyer, side on
PROFILE = [[0, 3], [34, 2], [41, 3.5], [45, 6], [43.5, 8.5], [32, 10.5], [26, 11.5], [9, 12], [2.5, 11], [0, 8]]
CANOPY = [[9, 11.8], [27, 11.4], [22.5, 17.5], [12, 18], [7.5, 15]]
POD = [[1, 1.5], [19, 1.5], [21.5, 5.5], [18, 9.5], [4, 9.5], [0, 6]]


def cproj(u, d, z):
    """The car's own axes on screen: u along its length (screen x, the way it flies), d across
    it toward the viewer, z up; the same isometric camera as the city's."""
    return proj((u + d) / math.sqrt(2), (d - u) / math.sqrt(2), z)


def hull(points):
    pts = sorted(set((round(p[0], 3), round(p[1], 3)) for p in points))
    def cr(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cr(lower[-2], lower[-1], p) <= 0: lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cr(upper[-2], upper[-1], p) <= 0: upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def extrusion(prof, x, d0, d1, sc):
    return [cproj(x + u * sc, dd, w * sc) for u, w in prof for dd in (d0, d1)]


def deck(prof, x, d0, d1, sc):
    """The upward faces of the body, as one strip: along the top of the profile at d0, back at d1."""
    ks = []
    for k in range(len(prof)):
        a, b = prof[k], prof[(k + 1) % len(prof)]
        nx, nz = b[1] - a[1], -(b[0] - a[0])
        if nx + nz > 0.01 and nz / math.hypot(nx, nz) > 0.55:
            ks.append(k)
    first, last = ks[0], ks[-1] + 1
    idx = list(range(first, last + 1))
    pts = [cproj(x + prof[k % len(prof)][0] * sc, d0, prof[k % len(prof)][1] * sc) for k in idx]
    pts += [cproj(x + prof[k % len(prof)][0] * sc, d1, prof[k % len(prof)][1] * sc) for k in reversed(idx)]
    return pts


def car_nodes(sc, lead):
    W = 11 * sc
    x = -22 * sc                                      # the car's reference: mid-body on the ground line
    n = []
    if lead:
        n.append(Node("farpod", hull(extrusion(POD, x + 3 * sc, -4 * sc, 0, sc)), ("#BEBEBE", "#000000", 0.5)))
    n.append(Node("hull", hull(extrusion(PROFILE, x, 0, W, sc)), ("#C8C8C8", "#000000", 0.5)))
    if lead:
        n.append(Node("deck", deck(PROFILE, x, 0, W, sc), ("#F2F2F2", "#000000", 0.4)))
    n.append(Node("side", [cproj(x + u * sc, W, w * sc) for u, w in PROFILE], ("#E2E2E2", "#000000", 0.5)))
    n.append(Node("glass", hull(extrusion(CANOPY, x, 2 * sc, W - 2 * sc, sc)), ("#303030", "#F2F2F2", 0.7)))
    if lead:
        n.append(Node("lamp", [cproj(x + 13 * sc, 4 * sc, 18.6 * sc), cproj(x + 20 * sc, 4 * sc, 18.4 * sc),
                               cproj(x + 20 * sc, W - 4 * sc, 18.4 * sc), cproj(x + 13 * sc, W - 4 * sc, 18.6 * sc)],
                      ("#FFFFFF", "#000000", 0.5)))
    n.append(Node("nearpod", hull(extrusion(POD, x + 3 * sc, W, W + 4 * sc, sc)), ("#D6D6D6", "#000000", 0.5)))
    # every part of a car turns about one origin, its middle, so they move and bank as one
    ox = round(sum(min(p[0] for p in q.points) + max(p[0] for p in q.points) for q in n) / (2 * len(n)))
    oy = round(sum(min(p[1] for p in q.points) + max(p[1] for p in q.points) for q in n) / (2 * len(n)))
    for q in n:
        q.ox, q.oy = ox, oy
        q.points = start_long(q.points)
    return n


# ------------------------------------------------------------------ the sky: traffic lanes and a searchlight
# Each lane: its height on screen, the row it passes in front of (it's drawn after that row's
# lots, so nearer towers hide it), its speed at full cruise (px a second, + rightward), its
# cars' scale and how many.
LANES = [dict(y=60, after=1, speed=-16, scale=0.75, cars=4),
         dict(y=96, after=3, speed=27, scale=0.9, cars=3),
         dict(y=148, after=5, speed=-42, scale=1.1, cars=3)]
LANE_LENGTH = 500                                     # a lane's loop, px: the screen and 10 px each side
LANE_LEFT = -10


def tiny_car(k, scale):
    """A small car: a level wedge, nose to the right."""
    pts = [(-3.6, -1.0), (2.0, -1.0), (3.8, 0.1), (2.0, 1.0), (-3.6, 1.0)]
    return [(x * scale, y * scale) for x, y in pts]


BEAM_LENGTH = 62                                      # px at unit scale; drawn at scale 2
BEAM_SCALE = 2048


def beam_node():
    return Node("beam", [(-0.6, 0), (0.6, 0), (4.6, -BEAM_LENGTH), (-4.6, -BEAM_LENGTH)], BEAM, fill=False, origin=(0, 0))


# ------------------------------------------------------------------ checks (the device's drawing rules)
def measures(node):
    """An outline's measures, from its points as the artwork writes them (relative to its origin)."""
    pts = [(float(num(p[0] - node.ox)), float(num(p[1] - node.oy))) for p in node.points]
    gaps = [length(sub(pts[(k + 1) % len(pts)], pts[k])) for k in range(len(pts))]
    turns = []
    for k in range(len(pts)):
        a, b, c = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
        u, v = sub(a, b), sub(c, b)
        cosine = (u[0] * v[0] + u[1] * v[1]) / (length(u) * length(v))
        turns.append(math.degrees(math.acos(max(-1, min(1, cosine)))))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return dict(count=len(pts), gap=min(gaps), closing=gaps[-1], turn=min(turns), area=abs(area(pts)),
                box=(min(xs), max(xs), min(ys), max(ys)), pts=pts)


def check(node, where, smallest=1.0):
    m = measures(node)
    assert 3 <= m["count"] <= 64, f"{where}: {m['count']} points"
    assert m["gap"] * smallest >= 0.125, f"{where}: points {m['gap'] * smallest:.3f} px apart at the smallest scale"
    assert m["closing"] * smallest >= 0.25, f"{where}: closing edge {m['closing'] * smallest:.3f} px"
    assert m["turn"] >= 2.5, f"{where}: a turn of {m['turn']:.2f} degrees"
    assert m["area"] > 0.5, f"{where}: no area"
    assert simple(m["pts"]), f"{where}: crosses itself"
    x0, x1, y0, y1 = m["box"]
    assert max(-x0, x1, -y0, y1) <= 63, f"{where}: a point past 63 px from its origin"


# ------------------------------------------------------------------ the whole scene
def scene():
    """Every node, numbered: the header's bars 0..7, then each lot's, the traffic, the beam
    and the cars. `order` is the artwork's drawing order, back to front."""
    L = lots()
    for lot in L:
        s = static(lot["t"])
        building(s)
        lot["wins"] = [w for w in windows(lot["t"], s) if seen(lot, w[1])][:MOST_WINDOWS]
    counts = window_counts(L)
    nodes = []
    rows = []
    for lot in L:
        s, ns = lot_nodes(lot, counts[lot["t"]])
        rows.append(dict(lot, first=8 + len(nodes), s=s, roles=[q.role for q in ns],
                         faces=[f for f, _ in lot["wins"][:counts[lot["t"]]]]))
        nodes += ns
    traffic = []
    for li, lane in enumerate(LANES):
        for c in range(lane["cars"]):
            pts = tiny_car(c, lane["scale"])
            if lane["speed"] < 0:
                pts = [(-x, y) for x, y in reversed(pts)]
            q = Node("car%d.%d" % (li, c), start_long(pts), (LANE_PAINT[li], LANE_PAINT[li], 0.3), origin=(0, 0))
            traffic.append((li, c, 8 + len(nodes)))
            nodes.append(q)
    # the searchlight: on the tallest roof without a mast in the back two rows
    cands = [r for r in rows if 4 <= r["row"] <= 5 and not r["s"]["mast"] and 120 <= r["x"] <= 360]
    src = max(cands, key=lambda r: sum(h for _, h in r["s"]["boxes"]))
    src["s"]["roof"] = (0, -sum(h for _, h in src["s"]["boxes"]))
    beam_index = 8 + len(nodes)
    nodes.append(beam_node())
    lead, wing = car_nodes(1.2, True), car_nodes(0.78, False)
    car_first = 8 + len(nodes)
    nodes += lead + wing
    # the drawing order
    order = []
    for r in rows:
        order += list(range(r["first"], r["first"] + len(r["roles"])))
        if r is src:
            order.append(beam_index)
        nxt = rows[rows.index(r) + 1] if r is not rows[-1] else None
        for li, lane in enumerate(LANES):
            if r["row"] == lane["after"] and (nxt is None or nxt["row"] != r["row"]):
                order += [k for (l2, _, k) in traffic if l2 == li]
    order += list(range(car_first, car_first + len(lead) + len(wing)))
    assert sorted(order) == list(range(8, 8 + len(nodes)))
    return dict(rows=rows, nodes=nodes, traffic=traffic, beam=beam_index, beam_lot=src, lead=lead, wing=wing,
                car_first=car_first, order=order)


def window_counts(L):
    """How many of its windows each lot draws: taken a window from each lot in turn, until
    the artwork's share for windows is spent."""
    counts = {lot["t"]: 0 for lot in L}
    spent = 0
    for k in range(MOST_WINDOWS):
        for lot in L:
            if k < len(lot["wins"]):
                q = lot["wins"][k][1]
                cost = 86 + sum(len(num(p[0])) + len(num(p[1])) + 3 for p in q)
                if spent + cost > WINDOW_BYTES:
                    return counts
                spent += cost
                counts[lot["t"]] += 1
    return counts


def lot_fills(sc):
    """Which bodies are filled: those that hide something drawn before them, at the
    largest scale any lot can have."""
    big = 1307 / 1024
    rows, nodes = sc["rows"], sc["nodes"]
    shapes = {}
    for r in rows:
        q = nodes[r["first"] - 8]
        shapes[r["t"]] = [(r["x"] + big * p[0], r["y"] + big * p[1]) for p in q.points]
    beam_reach = []
    src = sc["beam_lot"]
    bx, by = src["x"] + big * src["s"]["roof"][0], src["y"] + big * src["s"]["roof"][1]
    for k in range(-6, 7):
        a = math.radians(30 * k / 6)
        beam_reach.append((bx + 124 * math.sin(a), by - 124 * math.cos(a)))
    beam_poly = [(bx - 1, by), (bx + 1, by)] + list(reversed(beam_reach))
    out = {}
    for n, r in enumerate(rows):
        hides = False
        for before in rows[:n]:
            if overlap(shapes[r["t"]], shapes[before["t"]]):
                hides = True
                break
        for lane in LANES:
            if lane["after"] < r["row"]:
                band = [(-10, lane["y"] - 3), (490, lane["y"] - 3), (490, lane["y"] + 3), (-10, lane["y"] + 3)]
                hides = hides or overlap(shapes[r["t"]], band)
        if src["row"] < r["row"] or (src["row"] == r["row"] and src["t"] < r["t"]):
            hides = hides or overlap(shapes[r["t"]], beam_poly)
        out[r["t"]] = hides
    return out


def write_panel(sc):
    nodes = sc["nodes"]
    fills = lot_fills(sc)
    lot_of = {}
    for r in sc["rows"]:
        lot_of[r["first"]] = r
    out = [f'<g id="{ID}_panel">']
    for idx in sc["order"]:
        q = nodes[idx - 8]
        small = SMALLEST_SCALE if idx < sc["car_first"] and idx not in [k for _, _, k in sc["traffic"]] and idx != sc["beam"] else 1.0
        check(q, f"node {idx} ({q.role})", small)
        fill, stroke, width = q.paint
        if q.role == "body":
            q.fill = fills[lot_of[idx]["t"]]
        d = "M" + " L".join(f"{num(p[0] - q.ox)},{num(p[1] - q.oy)}" for p in q.points) + " Z"
        f = fill if q.fill else "none"
        out.append(f'<path id="{ID}_node_{idx}" d="{d}" fill="{f}" stroke="{stroke}" stroke-width="{num(width)}"/>')
    for k, (fill, stroke, width) in enumerate(PALETTE):
        out.append(f'<circle id="{ID}_palette_{k}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>')
    out.append(f'<text id="{ID}_text_16" transform="matrix(1 0 0 1 640 320)" fill="#BDBDBD" '
               f'font-family="UniversNextPro-Light" font-size="10px">-</text>')
    out.append(HEADER.replace("velvet_", ID + "_").strip())
    out.append("</g>")
    text = "\n".join(out) + "\n"
    assert len(text.encode()) <= 65536, f"artwork is {len(text.encode())} bytes, over 64 KiB"
    (HERE / "panel.svg.fragment").write_text(text)
    return len(text.encode())


def lot_points(r, nodes):
    return sum(len(nodes[r["first"] - 8 + k].points) for k in range(len(r["roles"])))


def write_tables(sc):
    rows, nodes = sc["rows"], sc["nodes"]
    lead, wing, car_first = sc["lead"], sc["wing"], sc["car_first"]
    n = len(nodes) + 8
    lead_box = car_box(lead)
    wing_box = car_box(wing)
    src = sc["beam_lot"]
    L = []
    L.append("// Generated by tools/make_panel.py from the same description as panel.svg.fragment: do not edit.")
    L.append("#pragma once")
    L.append('#include <opxy/types.hpp>')
    L.append("")
    L.append(f"namespace {ID} {{ namespace city {{")
    L.append(f"const opxy::u32 node_count = {n};")
    L.append(f"const opxy::u32 lot_count = {len(rows)};")
    L.append(f"const opxy::u32 most_windows = {MOST_WINDOWS};")
    L.append(f"const opxy::u32 car_first = {car_first};      // the lead car's {len(lead)} nodes, then the wingman's {len(wing)}")
    L.append(f"const opxy::u32 lead_nodes = {len(lead)}, wing_nodes = {len(wing)};")
    L.append(f"// Each car's reach from its origin, px (left, right): it's off screen past these.")
    L.append(f"const opxy::i32 lead_left = {lead_box[0]}, lead_right = {lead_box[1]}, wing_left = {wing_box[0]}, wing_right = {wing_box[1]};")
    L.append(f"const opxy::u32 beam = {sc['beam']}, beam_lot = {src['t']};   // the searchlight, and the lot it stands on")
    L.append(f"const opxy::i32 beam_x = {round(src['s']['roof'][0])}, beam_y = {round(src['s']['roof'][1])};   // its foot, from the lot's pivot at unit scale")
    L.append("// A lot: its pivot on screen (px), its first node (its body), its windows (the nodes after the")
    L.append("// body) and which face each is on (bit k set: window k is on the right face), its column (the")
    L.append("// skyline's phase), and whether it has a neon strip (one node after the windows), a blade sign")
    L.append("// (two after that) and a mast (two rings and a light, last).")
    L.append("struct Lot { short x, y; unsigned short first, faces; unsigned char windows, column, neon, sign, mast; };")
    L.append("const Lot lots[lot_count] = {")
    for r in rows:
        s = r["s"]
        wins = [q for q in r["roles"] if q.startswith("win")]
        faces = sum(1 << k for k in range(len(wins)) if r["faces"][k])
        L.append(f"    {{{r['x']}, {r['y']}, {r['first']}, {faces}, {len(wins)}, {r['column']}, {int(s['neon'])}, "
                 f"{int(s['sign'])}, {int(s['mast'])}}},")
    L.append("};")
    L.append("// The traffic: each lane's height (px), its speed as a share of the cruise (Q10), whether it")
    L.append("// runs rightward, its first node and its number of cars; each car's place along its lane's")
    L.append("// loop (px).")
    L.append(f"const opxy::u32 lane_count = {len(LANES)};")
    L.append(f"const opxy::i32 lane_length = {LANE_LENGTH}, lane_left = {LANE_LEFT};")
    L.append("struct Lane { short y, share; unsigned short rightward, first, cars; };")
    firsts = {}
    for li, c, k in sc["traffic"]:
        firsts.setdefault(li, k)
    L.append("const Lane lanes[lane_count] = {")
    for li, lane in enumerate(LANES):
        L.append(f"    {{{lane['y']}, {round(abs(lane['speed']) * 1024 / CRUISE)}, {int(lane['speed'] > 0)}, "
                 f"{firsts[li]}, {lane['cars']}}},")
    L.append("};")
    spots = []
    for li, c, k in sc["traffic"]:
        lane = LANES[li]
        base = c * LANE_LENGTH // lane["cars"]
        spots.append(str(base + (r16(li, c, 300) % (LANE_LENGTH // lane["cars"] // 2))))
    L.append(f"const short spots[{len(spots)}] = {{" + ", ".join(spots) + "};")
    L.append("// Each lot node's origin from its lot's pivot, px at unit scale (the others': 0).")
    lot_nodes_set = set()
    for r in rows:
        lot_nodes_set |= set(range(r["first"], r["first"] + len(r["roles"])))
    ox = ["0"] * 8 + [str(nodes[k - 8].ox if k in lot_nodes_set else 0) for k in range(8, n)]
    oy = ["0"] * 8 + [str(nodes[k - 8].oy if k in lot_nodes_set else 0) for k in range(8, n)]
    L.append("const short origin_x[node_count] = {" + ", ".join(ox) + "};")
    L.append("const short origin_y[node_count] = {" + ", ".join(oy) + "};")
    L.append("// Each node's points: what redrawing it costs.")
    pts = ["4"] * 8 + [str(len(q.points)) for q in nodes]
    L.append("const unsigned char points[node_count] = {" + ", ".join(pts) + "};")
    L.append("}}")
    (HERE / "city.h").write_text("\n".join(L) + "\n")


def car_box(nodes):
    left = min(min(p[0] - q.ox for p in q.points) for q in nodes)
    right = max(max(p[0] - q.ox for p in q.points) for q in nodes)
    return math.floor(left) - 2, math.ceil(right) + 2


def write_outlines(sc):
    """Each node's outline measures, for tests/screen.cpp's drawing-rule checks."""
    nodes = sc["nodes"]
    L = ["// Generated by tools/make_panel.py: each node's outline as the artwork draws it. Do not edit.",
         "// points, the shortest edge and the closing edge (Q10 px at unit scale), the sharpest turn",
         "// (tenths of a degree from straight back), whether it has area and a fill, and its box",
         "// about its origin (Q10 px).",
         "#pragma once",
         "struct Outline { int points, gap, closing, turn, area, filled, left, right, top, bottom; };",
         f"const Outline outlines[{len(nodes) + 8}] = {{"]
    L += ["    {4, 61440, 20480, 900, 1, 1, 0, 61440, 0, 20480},"] * 8      # the header's bars
    for q in nodes:
        m = measures(q)
        x0, x1, y0, y1 = m["box"]
        q10 = lambda v: int(math.floor(v * 1024)) if v < 0 else int(math.ceil(v * 1024))
        # rounded down a little, so the last bit of a platform's floating point never shows
        L.append(f"    {{{m['count']}, {int(m['gap'] * 1024 - 0.01)}, {int(m['closing'] * 1024 - 0.01)}, "
                 f"{int(m['turn'] * 10 - 0.01)}, "
                 f"{int(m['area'] > 0.5)}, {int(q.fill)}, {q10(x0)}, {q10(x1)}, {q10(y0)}, {q10(y1)}}},  // {q.role}")
    L.append("};")
    (HERE / "tests" / "outlines.h").write_text("\n".join(L) + "\n")


# ------------------------------------------------------------------ a mirror of display.cpp's frame, for previews
CRUISE = 110                                          # display.cpp's cruise, px a second


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


def sway(turn):
    x = turn & 0xFFFF
    x = x - 65536 if x >= 32768 else x
    return (x * (32768 - abs(x))) >> 18


def lot_state(r, c, held, hold, serial, clock):
    """display.cpp's desired(): a lot's scale code and what each of its parts shows."""
    t = r["t"]
    sky = wave16((r["column"] * 11916) & 0xFFFF, c["shape"])
    mix = (sky * 39322 + r16(t, 0, 1) * 26214) >> 16
    code = (mix * 63) >> 16
    rise = 154 + ((c["touch"] * 973) >> 16)
    wins = []
    for w in range(sum(1 for q in r["roles"] if q.startswith("win"))):
        if r16(t, w, 20) < c["bright"]:
            wins.append(1 + r["faces"][w])
        elif held and c["touch"] and r16(t * 7 + w, serial, 3) < (c["touch"] >> 1) and hold >= ((r16(t, w, 40) * rise) >> 16):
            wins.append(3)
        else:
            wins.append(0)
    neon = 3 + min(4, 1 + ((c["reso"] * 4) >> 16)) if r["s"]["neon"] else 0
    g = c["ring"]
    first = 0 if g == 0 else 1 + min(2, (g * 6) >> 16)
    second = 0 if g < 32768 else 1 + min(2, ((g - 32768) * 6) >> 16)
    sign_lit = r16(t, 0, 8) < c["bright"] + 9830
    light = ((clock + (r16(t, 0, 9) >> 6)) & 1023) < 280
    return dict(code=code, wins=wins, neon=neon, rings=(first, second), sign=sign_lit, light=light)


def frame_places(sc, c, run=None, held=False, hold=0, serial=1, clock=0, share=0, lanes=(0, 0, 0)):
    """The nodes display.cpp shows: index -> (x, y, scale, palette entry or None, turn degrees)."""
    rows, nodes = sc["rows"], sc["nodes"]
    place = {}
    scales = {}
    for r in rows:
        st = lot_state(r, c, held, hold, serial, clock)
        s = (614 + st["code"] * 11) / 1024
        scales[r["t"]] = s
        for k, role in enumerate(r["roles"]):
            idx = r["first"] + k
            q = nodes[idx - 8]
            pal = None
            if role.startswith("win"):
                w = st["wins"][int(role[3:])]
                if not w:
                    continue
                pal = w
            elif role == "neon":
                pal = st["neon"]
            elif role == "glow":
                if not st["sign"]:
                    continue
                pal = 11
            elif role.startswith("ring"):
                lv = st["rings"][int(role[4:])]
                if not lv:
                    continue
                pal = 7 + lv
            elif role == "light":
                pal = 13 if st["light"] else 12
            place[idx] = (r["x"] + s * q.ox, r["y"] + s * q.oy, s, pal, 0)
    # the searchlight
    src = sc["beam_lot"]
    s = scales[src["t"]]
    angle = (sway(clock * 9) * 300 >> 10) / 10
    place[sc["beam"]] = (src["x"] + s * round(src["s"]["roof"][0]), src["y"] + s * round(src["s"]["roof"][1]),
                         BEAM_SCALE / 1024, None, angle)
    # the traffic
    for li, cnum, idx in sc["traffic"]:
        lane = LANES[li]
        spot = int(sc_spots(sc)[sc["traffic"].index((li, cnum, idx))])
        p = (spot + lanes[li]) % LANE_LENGTH
        x = LANE_LEFT + p if lane["speed"] > 0 else LANE_LEFT + LANE_LENGTH - p
        place[idx] = (x, lane["y"], 1, None, 0)
    # the cars
    lx, ly, wx, wy, bank = car_place(sc, c, run, clock, share)
    lb, wb = car_box(sc["lead"]), car_box(sc["wing"])
    if lx + lb[1] > 0 and lx + lb[0] < 480:
        for k in range(len(sc["lead"])):
            place[sc["car_first"] + k] = (lx, ly, 1, None, -bank)
    if c["detune"] > 1966 and wx + wb[1] > 0 and wx + wb[0] < 480:
        for k in range(len(sc["wing"])):
            place[sc["car_first"] + len(sc["lead"]) + k] = (wx, wy, 1, None, -bank)
    return place


def sc_spots(sc):
    out = []
    for li, c, k in sc["traffic"]:
        lane = LANES[li]
        base = c * LANE_LENGTH // lane["cars"]
        out.append(base + (r16(li, c, 300) % (LANE_LENGTH // lane["cars"] // 2)))
    return out


def course(sc):
    """The lead car's way across, px: from just off the left to where the wingman is off the right, and a gap."""
    lb, wb = car_box(sc["lead"]), car_box(sc["wing"])
    left = -lb[1]
    exit_ = 480 + WING_BEHIND + WING_APART - wb[0]
    return left, exit_ + GAP - left


WING_BEHIND, WING_APART, GAP, HOME = 66, 16, 70, 300


def car_place(sc, c, run=None, clock=0, share=0):
    """display.cpp's draw_cars, in px."""
    left, _ = course(sc)
    x = HOME if run is None else left + run
    bob = (sway(clock * 31) * 3 // 2) * share / 1024 / 1024
    bank = (sway(clock * 31 + 16384) * 15 / 1024) * share / 1024 / 10
    weave = (c["chorus"] >> 6) * 5 * sway(clock * 58) / 1024 / 1024
    y = CAR_Y - c["ribbon"] * CAR_RISE / 65536 + bob + weave
    apart = c["detune"] * WING_APART / 65536
    return x, y, x - WING_BEHIND - apart, y + 16 + apart * 0.6 - 2 * weave, bank


CAR_Y, CAR_RISE = 132, 48


def preview(out_dir, name, sc, c, **kw):
    place = frame_places(sc, c, **kw)
    nodes = sc["nodes"]
    fills = lot_fills(sc)
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="480" height="220">',
           '<style>@font-face{font-family:UniversNextPro-Light;src:url(file:///Users/mmockett/Documents/GitHub/OP-XY/tools/render/font/UniversNextPro-Light.ttf)}</style>',
           '<rect width="480" height="220" fill="#000"/>']
    lot_of = {r["first"]: r for r in sc["rows"]}
    for idx in sc["order"]:
        if idx not in place:
            continue
        q = nodes[idx - 8]
        x, y, s, pal, turn = place[idx]
        fill, stroke, width = q.paint if pal is None else PALETTE[pal]
        filled = fills[lot_of[idx]["t"]] if q.role == "body" else q.fill
        if not filled:
            fill = "none"
        d = "M" + " L".join(f"{num(p[0] - q.ox)},{num(p[1] - q.oy)}" for p in q.points) + " Z"
        co, si = math.cos(math.radians(turn)) * s, math.sin(math.radians(turn)) * s
        svg.append(f'<path transform="matrix({co:.4f} {si:.4f} {-si:.4f} {co:.4f} {x:.2f} {y:.2f})" d="{d}" '
                   f'fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')
    svg.append('<rect width="480" height="38" fill="#000"/>')
    shades = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for k, g in enumerate(shades):
        svg.append(f'<rect x="{60 * k}" width="60" height="20" fill="{g}"/>')
    titles = ["shape", "detune", "bright", "reso"]
    grey = ["chorus", "ring", "touch", "ribbon"]
    values = c.get("values", ["", "", "", "", "", "", "", ""])
    for k in range(4):
        col = "#CBCBCB" if k < 2 else "#000"
        svg.append(f'<text x="{5 + 120 * k}" y="10" font-family="UniversNextPro-Light" font-size="10" fill="{col}">{titles[k]}</text>')
        svg.append(f'<text x="{65 + 120 * k}" y="17.4" font-family="UniversNextPro-Light" font-size="20" fill="{col}">{values[k]}</text>')
        svg.append(f'<text x="{5 + 120 * k}" y="33" font-family="UniversNextPro-Light" font-size="10" fill="#5A5A5A">{grey[k]}</text>')
        svg.append(f'<text x="{65 + 120 * k}" y="33" font-family="UniversNextPro-Light" font-size="13" fill="#707070">{values[4 + k]}</text>')
    svg.append("</svg>")
    pathlib.Path(out_dir).mkdir(parents=True, exist_ok=True)
    (pathlib.Path(out_dir) / f"{name}.svg").write_text("\n".join(svg))
    return place


def fill_area(sc, place):
    """The filled area on screen, overlaps counted, px^2: what the device pays for every frame."""
    nodes = sc["nodes"]
    fills = lot_fills(sc)
    lot_of = {r["first"]: r for r in sc["rows"]}
    total = 0.0
    for idx, (x, y, s, pal, turn) in place.items():
        q = nodes[idx - 8]
        filled = fills[lot_of[idx]["t"]] if q.role == "body" else q.fill
        if not filled:
            continue
        pts = [(x + s * (p[0] - q.ox), y + s * (p[1] - q.oy)) for p in q.points]
        total += clip_area(pts, 0, 38, 480, 220)
    return total


def previews(out, sc):
    q = lambda v: min(65535, int(v * 65536))
    tyrell = dict(shape=q(.33), detune=q(.22), bright=q(.46), reso=q(.28), chorus=q(.45), ring=0, touch=q(.8),
                  ribbon=q(.5), values=["33", "3c", "920", "28", "45", "0", "80", "0st"])
    zero = {k: 0 for k in ("shape", "detune", "bright", "reso", "chorus", "ring", "touch", "ribbon")}
    full = {k: 65535 for k in zero}
    states = [
        ("resting", tyrell, {}),
        ("flying", tyrell, dict(run=330, held=True, hold=900, clock=700, share=1024, lanes=(140, 260, 90))),
        ("all0", dict(zero, values=["0", "0c", "80", "0", "0", "0", "0", "-12st"]), {}),
        ("all100", dict(full, values=["100", "30c", "16k", "100", "100", "100", "100", "+12st"]),
         dict(run=420, held=True, hold=4000, clock=300, share=1024, lanes=(300, 40, 200))),
    ]
    for name, c, kw in states:
        place = preview(out, name, sc, c, **kw)
        print(f"{name}: {len(place) + 8} nodes on screen, fill {fill_area(sc, place):.0f} px^2 below the header")


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


def main():
    sc = scene()
    size = write_panel(sc)
    write_tables(sc)
    write_outlines(sc)
    rows, nodes = sc["rows"], sc["nodes"]
    # While flying, the cars, the traffic and the searchlight change every frame; the largest
    # lot must still fit in what's left, so the city can always restyle.
    moving = sum(len(q.points) for q in sc["lead"] + sc["wing"]) + \
        sum(len(nodes[k - 8].points) for _, _, k in sc["traffic"]) + len(nodes[sc["beam"] - 8].points)
    largest = max(lot_points(r, nodes) for r in rows)
    assert moving + largest <= BUDGET, f"{moving} moving points and a lot of {largest} are over the budget"
    masts = sum(r["s"]["mast"] for r in rows)
    print(f"{len(rows)} lots ({masts} masts), {len(nodes) + 8} nodes, artwork {size} bytes, "
          f"longest outline {max(len(q.points) for q in nodes)} points, "
          f"largest lot {largest} points, {moving} moving points")
    if "--preview" in sys.argv:
        previews(sys.argv[sys.argv.index("--preview") + 1], sc)


if __name__ == "__main__":
    main()
