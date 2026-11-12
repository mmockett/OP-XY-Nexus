#!/usr/bin/env python3
"""Nexus's screen: a dense isometric city in greys, drawn like pixel art, with a
monorail train on an elevated track, an OP-XY advert tower, billboards, a
helipad, masts, a dish, a ball tower, rooftop fans and a road with tiny cars.

Writes ../panel.svg.fragment (the artwork), ../city.h (the tables display.cpp
reads) and ../tests/outlines.h (each node's outline measures, for the host
checks), from one description, so they always agree. With --preview <dir> it
also draws a few states as SVG, through a mirror of display.cpp's frame logic,
for looking at the picture without building anything.

    python3 tools/make_panel.py [--preview <dir>] [--compact nospace,implicit,relative]

--compact writes shorter path syntax the OP-XY hasn't been seen to load yet (path_d() says which), for a
build meant to test it; the artwork kept in the repository is written without it.

Pure Python with shapely 2.2.0 (a prebuilt wheel); nothing is compiled.

The city is still, so it is drawn once: every face is a filled outline, and
each is clipped to what is actually seen of it, back to front, so the screen
is filled about once over. What changes is a node: the sky and clouds, lit
windows, background lights, the advert's text and pattern, the billboards, the
hourglass's dots, neon edges, rings (a back half and a front half each, round
their mast or the ball), blinking lights, the dish, a flying car over the
helipad, the two-car train and the road's tiny cars. The fans' blades and the
advert's two lines are reshaping outlines (nodes 0..3) that display.cpp redraws.
The train and the cars are drawn after what is behind them and before what is in
front, so they pass behind the nearer towers.
"""
import json
import math
import pathlib
import sys

import shapely
import shapely.affinity
from shapely.geometry import Polygon, box as rect

HERE = pathlib.Path(__file__).resolve().parent.parent
DEFINITION = json.loads((HERE / "definition.json").read_text())
ID = DEFINITION["id"]
# The header's titles, as definition.json labels the controls: the page's by slot, the Shift row's by entry.
TITLES = [p["label"] for p in DEFINITION["parameters"]]
SHIFT_TITLES = [p["label"] for p in DEFINITION["shift"]]
assert len(TITLES) == 8 and len(SHIFT_TITLES) == 8
GRID = 0.25                       # every static outline's points sit on this grid, px
OX, OY = 64, 150                  # the camera: world (0, 0, 0) on screen
TOP, BOTTOM = 38, 220             # the stage, below the header
STAGE = rect(0, TOP, 480, BOTTOM)
BUDGET = 200                      # display.cpp's budget: points of changed outlines a frame
TRAIN_REACH = 320                 # px either way the train can be from home (for the depth order)
CRAFT_RISE = 30                   # px the craft rises at bite 100 (display.cpp's rise)
# The road's cars: lane (world x), way (1 up the road to the right, -1 down it toward the viewer), speed
# px a second along screen x, where it is drawn (world y), and where it is on its run at rest (px).
ROAD_CARS = [(218.9, 1, 14, -150, 40), (223.15, 1, 11, -150, 170), (227.4, -1, 12, -150, 90), (230.9, -1, 16, -150, 220)]
SAND = 2                          # the hourglass's dots: dark against its panel (12..15) at every reso
SUN_STRIPES = 4                   # the sun poster's reflection, in stripes that lowcut clears from the bottom up
SAND_DRIFT = 5                    # px each half of the hourglass moves at detune 100 (display.cpp's drift)
OCCLUDE = 100                     # px^2: a static outline this big hides what is behind it
HEADER_FILL = 480 * 38 + 8 * 60 * 20   # the header's black and its bars, px^2
ARTWORK_LIMIT = 65536             # the SDK's limit on panel.svg.fragment (tooling/preview.py)
# The reshaping outlines, nodes 0..3, each of PATH_POINTS points: the three fans' blades, which turn
# smoothly, and the advert's two lines, which sine lengthens. The header's bars follow (4..11), then the city.
PATHS, PATH_POINTS = 4, 9
FAN_PATHS, LINES_PATH = (0, 1, 2), 3
FIRST = PATHS + 8                 # the first of the city's nodes
FAN_HUB, FAN_SWEEP, FAN_TIP = 1.3, 1.1, 0.34   # a blade's root radius, and its root's and tips' angles back and on, rad
# The advert's lines, in its face's units: where they start, their rows and height, and their lengths at
# sine 0 and 100 (display.cpp's line_rows and line_length).
LINE_U, LINE_W, LINE_GAP, LINE_H, LINE_SPINE = 8, 72, 4.5, 0.5, 0.6
LINE_LENGTH = ((10, 32), (6, 20))

# ------------------------------------------------------------------ the greys
# Sixteen steps, black to near white: the palette, and every static fill uses them too.
TONES = [0, 18, 28, 38, 50, 62, 74, 86, 100, 114, 130, 148, 166, 186, 208, 236]
PAL_WIDTH = 0.5                   # the palette's stroke: a node painted from it grows 0.25 px a side


def grey(t):
    v = TONES[t]
    return "#%02X%02X%02X" % (v, v, v)


# Shades by distance: (top, left, right). Nearer is darker; the haze is light.
NEAR = (9, 6, 3)
MID = (9, 7, 4)
FAR = (10, 8, 5)
HAZE = (12, 11, 10)
SKY = 12                          # the sky's grey at bright 0
# As bright rises (by its square) the sky darkens toward NIGHT, and the clouds with it a grey lighter.
# Below HORIZON the sky darkens only to GLOW, the city's light low behind the haze towers. The haze
# towers keep their light greys, so they stay lighter than the sky behind them at every setting.
NIGHT, GLOW, HORIZON = 6, 9, 72
CLOUD = 8                         # the clouds' grey at bright 100, all of each cloud alike


def hash32(i, j, k):
    """display.cpp's hash: the same integers on both sides."""
    h = (i * 374761393 + j * 668265263 + k * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return h ^ (h >> 16)


def P(x, y, z):
    """The isometric camera, 2:1: world x runs down-right, y down-left, z up."""
    return (OX + x - y, OY + (x + y) / 2 - z)


def num(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def length(a): return math.hypot(a[0], a[1])
def cross(a, b): return a[0] * b[1] - a[1] * b[0]


def area(pts):
    return 0.5 * sum(cross(pts[k], pts[(k + 1) % len(pts)]) for k in range(len(pts)))


def start_long(pts):
    """Rotates an outline to close on its longest edge: a short closing edge draws a sliver."""
    best = max(range(len(pts)), key=lambda k: length(sub(pts[(k + 1) % len(pts)], pts[k])))
    return pts[best + 1:] + pts[:best + 1]


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


def turns(pts):
    out = []
    for k in range(len(pts)):
        a, b, c = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
        u, v = sub(a, b), sub(c, b)
        cosine = (u[0] * v[0] + u[1] * v[1]) / (length(u) * length(v))
        out.append(math.degrees(math.acos(max(-1, min(1, cosine)))))
    return out


def measures(pts):
    gaps = [length(sub(pts[(k + 1) % len(pts)], pts[k])) for k in range(len(pts))]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return dict(count=len(pts), gap=min(gaps), closing=gaps[-1], turn=min(turns(pts)), area=abs(area(pts)),
                box=(min(xs), max(xs), min(ys), max(ys)))


def check(pts, where, extent=None):
    """The device's drawing rules (OP-XY-MNMD docs/SCREEN.md; the SDK's docs/display.md)."""
    m = measures(pts)
    assert 3 <= m["count"] <= 64, f"{where}: {m['count']} points"
    assert m["gap"] >= 0.125, f"{where}: points {m['gap']:.3f} px apart"
    assert m["closing"] >= 0.25, f"{where}: closing edge {m['closing']:.3f} px"
    assert m["turn"] >= 2.5, f"{where}: a turn of {m['turn']:.2f} degrees"
    assert m["area"] > 0.2, f"{where}: no area"
    assert simple(pts), f"{where}: crosses itself"
    if extent:
        x0, x1, y0, y1 = m["box"]
        assert max(-x0, x1, -y0, y1) <= extent, f"{where}: a point past {extent} px from its origin"
    return m


# ------------------------------------------------------------------ the scene's parts
class Box:
    """An axis-aligned box in the world, and maps onto its three seen faces."""

    def __init__(self, x0, x1, y0, y1, z0, z1):
        assert x0 < x1 and y0 < y1 and z0 < z1
        self.x0, self.x1, self.y0, self.y1, self.z0, self.z1 = x0, x1, y0, y1, z0, z1

    def faces(self):
        x0, x1, y0, y1, z0, z1 = self.x0, self.x1, self.y0, self.y1, self.z0, self.z1
        top = [P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)]
        left = [P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), P(x0, y1, z0)]
        right = [P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0), P(x1, y0, z0)]
        return top, left, right

    # face coordinates: u across, w down from the top edge (v into the roof)
    def L(self, u, w): return P(self.x0 + u, self.y1, self.z1 - w)
    def R(self, u, w): return P(self.x1, self.y1 - u, self.z1 - w)
    def T(self, u, v): return P(self.x0 + u, self.y0 + v, self.z1)
    @property
    def a(self): return self.x1 - self.x0
    @property
    def b(self): return self.y1 - self.y0
    @property
    def h(self): return self.z1 - self.z0


def tower(fx, fy, a, b, y1, z0=0):
    """A box from where its top front corner is on screen, its width along x (a) and y (b),
    and the world y of its front face (which fixes its depth); it stands on the ground."""
    a, b = 2 * round(a / 2), 2 * round(b / 2)          # even sizes and an even x + y keep corners on whole pixels
    x1 = fx - OX + y1
    x1 += (x1 + y1) % 2
    z1 = (x1 + y1) // 2 + OY - fy
    return Box(x1 - a, x1, y1 - b, y1, min(z0, z1 - 200), z1)


FOUND = {}                        # the first pass's findings: how far down each probe is first and last seen
SEEN = {}                         # and kept for the second: (part, seed, column) -> px down its face
SPANDREL = 1.5                    # the face's grey between two rows of windows, px
PAIR_SLACK = 12                   # px apart two columns' feet may be and still be joined by a bridge


def span(key, w0):
    """Where a column of windows is drawn, (top, foot) px down its face: from just above where it is
    first seen (its top unless that is hidden) to where it is last seen, or None if it isn't."""
    if key not in SEEN:
        return None
    top = SEEN[("top",) + key]
    return (w0 if top - w0 < 2 else top - 1.25, SEEN[key])


JITTER = 0.0                      # a test of the output's steadiness: nudge every point this much


class Item:
    def __init__(self, kind, pts, tone=None, occ=False, width=0.0, node=None):
        # Points to 1/10000 px: the platforms' sines and cosines differ in their last bits, and an
        # outline that just touches another must touch, or not, the same way on every machine.
        pts = [(round(x + JITTER, 4), round(y - JITTER, 4)) for x, y in pts]
        self.kind, self.pts, self.tone, self.occ, self.width, self.node = kind, pts, tone, occ, width, node


class Solid:
    """Something with a place in the depth order: its box, and its parts, drawn in order."""

    def __init__(self, name, box, items=None):
        self.name, self.box, self.items = name, box, items or []

    def fill(self, pts, tone, occ=None, sky=None):
        """A static filled outline. Unless told, it hides what is behind it when it is big and
        solid enough that cutting it out of what is behind saves more fill than it costs."""
        if occ is None:
            a = abs(area(pts))
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            occ = a >= OCCLUDE and a >= 0.3 * (max(xs) - min(xs)) * (max(ys) - min(ys))
        self.items.append(Item("fill", list(pts), tone, occ))
        self.items[-1].sky = sky        # (greys at bright 0 and 100, and below the horizon): seen, nodes that darken

    def line(self, pts, tone, width):
        self.items.append(Item("line", list(pts), tone, width=width))

    def node(self, pts, **spec):
        self.items.append(Item("node", list(pts), node=spec))

    def faces(self, shades, occ=True, sides=(True, True, True)):
        self.faces_of(self.box, shades, occ, sides)

    def faces_of(self, box, shades, occ=True, sides=(True, True, True)):
        top, left, right = box.faces()
        for want, pts, t in zip(sides, (top, left, right), shades):
            if want:
                self.fill(pts, t, occ)


SOLIDS = []


def solid(name, box):
    s = Solid(name, box)
    SOLIDS.append(s)
    return s


# ------------------------------------------------------------------ shapes in face coordinates
def half(p):
    """A point on half pixels: shorter numbers in the artwork, a quarter pixel at most from the design."""
    return (round(p[0] * 2) / 2, round(p[1] * 2) / 2)


def quad(f, u, w, du, dw):
    return [half(f(u, w)), half(f(u + du, w)), half(f(u + du, w + dw)), half(f(u, w + dw))]


def column(f, u, w, du, dw, n, pitch, spine=0.5):
    """n dashes stacked down a face, joined by a thin spine at their left: one outline."""
    pts = []
    for k in range(n):
        wk = w + k * pitch
        pts += [f(u if k == 0 else u + spine, wk), f(u + du, wk), f(u + du, wk + dw)]
        pts.append(f(u + spine, wk + dw) if k < n - 1 else f(u, wk + dw))
    return pts


def row(f, u, w, du, dw, n, pitch, spine=0.5):
    """n blocks side by side along a face, joined by a thin spine along their foot: one outline."""
    pts = []
    for k in range(n):
        uk = u + k * pitch
        pts += [f(uk, w + dw if k == 0 else w + dw - spine), f(uk, w), f(uk + du, w)]
        pts.append(f(uk + du, w + dw - spine) if k < n - 1 else f(uk + du, w + dw))
    return pts


def ellipse(cx, cy, rx, ry, n=12, phase=0.0):
    return [(cx + rx * math.cos(2 * math.pi * (k + phase) / n), cy + ry * math.sin(2 * math.pi * (k + phase) / n))
            for k in range(n)]


def arc(cx, cy, rx, ry, n, back, inner=None, thick=0.5):
    """Half a level ring round (cx, cy) as a thin ribbon: its back half (above the centre on screen)
    or its front half, n segments, its inner edge `thick` px nearer the centre in height (with `inner`
    segments, fewer for the small rings: the band then thickens a little between them)."""
    sign = -1 if back else 1
    inner = inner or n
    outer = [(cx - rx * math.cos(math.pi * k / n), cy + sign * ry * math.sin(math.pi * k / n)) for k in range(n + 1)]
    under = [(cx - rx * math.cos(math.pi * k / inner), cy + sign * (ry * math.sin(math.pi * k / inner) - thick))
             for k in range(inner, -1, -1)]
    return outer + under


def disc(f, cu, cv, r, n=12):
    """A circle lying in a face's plane (u, v), as its outline on screen."""
    return [f(cu + r * math.cos(2 * math.pi * k / n), cv + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def strokes(lines, width):
    """A glyph from its strokes (face units), as one outline; a hole gets a slit at its foot."""
    from shapely.geometry import LineString
    g = shapely.union_all([LineString(l).buffer(width / 2, cap_style="square", join_style="mitre") for l in lines])
    g = shapely.set_precision(g, 0.05)
    for hole in list(g.interiors):
        hx = (min(p[0] for p in hole.coords) + max(p[0] for p in hole.coords)) / 2
        hy = max(p[1] for p in hole.coords)
        g = g.difference(rect(hx - 0.2, hy - 0.5, hx + 0.2, hy + width + 1))
    g = shapely.simplify(g, 0.01)
    return [(round(u, 2), round(v, 2)) for u, v in list(g.exterior.coords)[:-1]]


def blades(reach, turn):
    """A fan's three blades at a turn (0..1), as one outline about its centre on screen: each blade a
    triangle from its root near the hub to two tips, in the roof's plane (u, v -> u - v, (u + v) / 2)."""
    pts = []
    for blade in range(3):
        a0 = 2 * math.pi * (turn + blade / 3)
        for r, da in ((FAN_HUB, -FAN_SWEEP), (reach, -FAN_TIP), (reach, FAN_TIP)):
            u, v = r * math.cos(a0 + da), r * math.sin(a0 + da)
            pts.append((u - v, (u + v) / 2))
    return pts


def lines_outline(sine):
    """The advert's two lines for a sine amount (0..1), in its face's units: a bracket, the two bars
    joined by a thin spine at their left."""
    (a0, a1), (b0, b1) = LINE_LENGTH
    l1, l2 = a0 + (a1 - a0) * sine, b0 + (b1 - b0) * sine
    u, w, g, h, sp = LINE_U, LINE_W, LINE_GAP, LINE_H, LINE_SPINE
    return [(u, w), (u + l1, w), (u + l1, w + h), (u + sp, w + h), (u + sp, w + g), (u + l2, w + g),
            (u + l2, w + g + h), (u, w + g + h), (u, w + (g + h) / 2)]


GLYPHS = {   # 6 wide, 9 tall, strokes 1.6 wide (face units)
    "O": [[(0.8, 0.8), (5.2, 0.8), (5.2, 8.2), (0.8, 8.2), (0.8, 0.8)]],
    "P": [[(0.8, 9), (0.8, 0.8), (5.2, 0.8), (5.2, 4.6), (0.8, 4.6)]],
    "-": [[(0, 4.3), (4, 4.3)]],
    "X": [[(0.8, 0.8), (5.2, 8.2)], [(5.2, 0.8), (0.8, 8.2)]],
    "Y": [[(0.8, 0.8), (3, 4.6)], [(5.2, 0.8), (3, 4.6)], [(3, 4.6), (3, 8.2)]],
}
GLYPH_WIDTH = {"O": 6, "P": 6, "-": 4, "X": 6, "Y": 6}


# ------------------------------------------------------------------ the city
def comb_down(f, u, w, du, dw, n, pitch, spine=0.6):
    """n bars side by side, hanging from a thin spine along their tops: one outline."""
    pts = []
    for k in range(n):
        uk = u + k * pitch
        pts += [f(uk, w if k == 0 else w + spine), f(uk, w + dw), f(uk + du, w + dw)]
        pts.append(f(uk + du, w + spine) if k < n - 1 else f(uk + du, w))
    return pts


class City:
    """The scene's builder: solids, and the nodes on them."""

    def __init__(self):
        SOLIDS.clear()
        self.masts, self.lights, self.fans, self.rings = [], [], [], []

    # windows: tall ones, two floors high, in columns down a face. Each column (or two) is a lit strip,
    # a node of 4 (or 8) points, and the face's own grey is laid over a thin band between each two rows
    # (SPANDREL), so one strip shows as a column of windows. A strip hides the face behind it, so only
    # those bands are filled twice. Each column runs as far down its face as it is seen: a first pass
    # draws them all to the screen's foot and finds where each column (and each gap between two) is
    # first and last seen; the second runs each strip from just above the first to just past the
    # last, out of sight, and joins two columns that end within PAIR_SLACK of each other by a bridge
    # under their feet, out of sight too.
    def grid(self, s, f, u0, w0, cols, du, pu, pw, face, lit, seed):
        dw = pw - SPANDREL                         # a window's height
        a = du - 0.5                               # a strip's width: its paint's stroke adds the rest
        face_at = f
        f = lambda u, w: tuple(round(v * 2) / 2 for v in face_at(u, w))   # on half pixels: shorter numbers
        if not SEEN:
            for c in range(cols):
                u = u0 + c * pu
                for key, uu, ww in ((("c", seed, c), u, a), (("g", seed, c), u + a, pu - a)):
                    if key[0] == "g" and c + 1 == cols:
                        continue
                    w = w0
                    while f(uu, w)[1] < BOTTOM + 2 and w < w0 + 400:
                        w += 0.5
                    s.node(quad(f, uu, w0, ww, w - w0), kind="window", low=1, high=lit, tone=lit,
                           probe=[(key, [(w0 + 0.5 * k, quad(f, uu, w0 + 0.5 * k, ww, 0.5))
                                         for k in range(int((w - w0) * 2))])],
                           grid=(f, uu, w0, a, dw, pw))      # for window_stats()
            # and the mask's spine, either side: where it joins each band between rows to the next
            for side, us in (("l", u0 - 1.4), ("r", u0 + (cols - 1) * pu + du + 0.5 - 0.7)):
                n = int((BOTTOM - f(us, w0)[1]) // pw) + 2
                s.node(quad(f, us, w0 + dw, 0.7, n * pw), kind="window", low=1, high=lit, tone=lit,
                       probe=[((side, seed, r), [(0, quad(f, us, w0 + dw + r * pw, 0.7, pw))]) for r in range(n)])
            return
        runs = [span(("c", seed, c), w0) for c in range(cols)]
        if not any(runs):
            return
        # a node within 63 px of its origin: strips longer than this are cut into chunks under a band
        longest = 118 - (pu + du) / 2

        def node(pts, key):
            s.node(pts, kind="window", low=max(face - 1, 1), high=lit, param=hash32(*key, 5) & 255, tone=lit,
                   occ=True)

        def chunks(k, top, end, last=None):
            """Column k's strip from top to end, cut at the bands between rows into nodes short enough;
            returns where the last one starts, or draws it too when `last` is None."""
            w = top
            while w + longest < end + 1.0:
                r = int((w + longest - dw - w0) // pw)      # the last row whose window ends inside this chunk
                cut = w0 + r * pw + dw - 0.5
                if cut <= w + 1:
                    break
                node(quad(f, u0 + k * pu, w, a, cut - w), (seed, k + 100 * (r + 1)))
                w = w0 + (r + 1) * pw
            if last is None:
                node(quad(f, u0 + k * pu, w, a, end - w), (seed, k + 1000 + int(w)))
            return w

        c = 0
        while c < cols:
            if not runs[c]:
                c += 1
                continue
            top, foot = runs[c]
            gap = SEEN.get(("g", seed, c))
            pair = c + 1 < cols and runs[c + 1] and runs[c + 1][0] == top and abs(runs[c + 1][1] - foot) <= PAIR_SLACK
            if pair:
                bottom = max(foot, runs[c + 1][1], gap or 0) + 0.25   # the bridge's top, out of sight
                w = max(chunks(c, top, bottom, last=True), chunks(c + 1, top, bottom, last=True))
                w = min(w, bottom - 1)
                u = u0 + c * pu
                b0, b1 = u + pu, u + pu + a
                g0, g1 = bottom, bottom + 0.75
                node([f(u, w), f(u + a, w), f(u + a, g0), f(b0, g0), f(b0, w), f(b1, w), f(b1, g1), f(u, g1)],
                     (seed, c))
                c += 2
            else:
                chunks(c, top, foot + 1.5)
                c += 1
        deepest = max(r[1] for r in runs if r)
        gaps = sum(1 for r in range(1, 200) if w0 + (r - 1) * pw + dw < deepest)
        width = (cols - 1) * pu + du
        # the mask: runs of bands joined by a spine on whichever side is seen between them, at most 15
        # bands (60 points) to an outline; a band no spine reaches is an outline of its own
        r0 = 0
        while r0 < gaps:
            side = next((d for d in ("l", "r") if SEEN.get((d, seed, r0)) is not None), "l")
            n = 1
            while r0 + n < gaps and n < 15 and SEEN.get((side, seed, r0 + n - 1)) is not None:
                n += 1
            pts = column(lambda u, w: (u, w), u0 - 1.4, w0 + dw + r0 * pw, width + 1.9, SPANDREL, n, pw, spine=0.7)
            if side == "r":                        # the same comb, its spine on the right
                pts = [(2 * u0 - 2.8 + width + 1.9 - u, w) for u, w in pts]
            s.fill([f(u, w) for u, w in pts], face, occ=False)
            r0 += n

    def band(self, s, f, u0, w0, n, du, dw, pu, face, lit, seed, per=4):
        """A row of windows across a face, lit a run of `per` at a time."""
        k = 0
        while k < n:
            m = min(per, n - k)
            u = u0 + k * pu
            s.node(quad(f, u, w0, (m - 1) * pu + du - 0.5, dw - 0.5), kind="window",
                   low=max(face - 1, 1), high=lit, param=hash32(seed, k, 5) & 255, tone=lit)
            k += m
        s.fill(comb_down(f, u0 + du, w0 - 1.4, pu - du, dw + 1.9, n - 1, pu, spine=0.7), face, occ=False)

    def building(self, name, fx, fy, a, b, y1, shades=MID, parapet=2, inner=None):
        bx = tower(fx, fy, a, b, y1)
        s = solid(name, bx)
        s.faces(shades)
        if parapet:
            p = parapet
            s.fill([bx.T(p, p), bx.T(bx.a - p, p), bx.T(bx.a - p, bx.b - p), bx.T(p, bx.b - p)],
                   inner if inner is not None else shades[0] - 3)
        return s, bx

    def cornice(self, s, bx, w, tone):
        """A ledge round a tower's two seen faces, just under its roof: a thin ribbon, stroked."""
        s.line([half(p) for p in (bx.L(0, w), bx.L(bx.a, w), bx.R(bx.b, w), bx.R(bx.b, w + 0.5), bx.L(bx.a, w + 0.5),
                                  bx.L(0, w + 0.5))], tone, 0.5)

    def box_on(self, name, base, u, v, a, b, h, shades=(9, 6, 3), lid=None):
        """A box standing on another's roof (vents, plant rooms)."""
        bx = Box(base.x0 + u, base.x0 + u + a, base.y0 + v, base.y0 + v + b, base.z1, base.z1 + h)
        s = solid(name, bx)
        s.faces(shades)
        if lid is not None:
            s.fill([bx.T(1, 1), bx.T(a - 1, 1), bx.T(a - 1, b - 1), bx.T(1, b - 1)], lid)
        return s, bx

    def ring(self, owner, cx, cy, rx, ry, n, inner, slot, back):
        """A ring round a mast or the ball: its back half drawn before them, its front half after."""
        owner.node(arc(cx, cy, rx, ry, n, back, inner), kind="ring", param=slot, low=7, high=14, tone=11,
                   outline=True)

    def mast(self, owner, x, y, h, rings=True, width=1.2, tone=2, phase=0):
        hoops = [(y - h * 0.62, 5.5, 1.8, 0), (y - h * 0.82, 4.0, 1.4, 1)] if rings else []
        for cy, rx, ry, slot in hoops:
            self.ring(owner, x, cy, rx, ry, 4, 2, slot, True)
        owner.fill([(x - width, y + 0.5), (x - 0.4, y - h), (x + 0.4, y - h), (x + width, y + 0.5)], tone)
        owner.fill([(x - width - 0.8, y - h * 0.4), (x + width + 0.8, y - h * 0.4),
                    (x + width + 0.8, y - h * 0.4 + 1.2), (x - width - 0.8, y - h * 0.4 + 1.2)], tone)
        for cy, rx, ry, slot in hoops:
            self.ring(owner, x, cy, rx, ry, 4, 2, slot, False)
        self.lights.append((owner, x, y - h - 0.6, 1.3, phase))

    def fan(self, owner, bx, cu, cv, r):
        """An extractor fan on a roof: its rim and dark well, three blades as one reshaping outline that
        display.cpp turns in the roof's plane, and a hub."""
        owner.fill(disc(bx.T, cu, cv, r + 1.4, 12), 9)
        owner.fill(disc(bx.T, cu, cv, r + 0.4, 12), 1)
        k = len(self.fans)
        cx, cy = bx.T(cu, cv)
        origin = (math.floor(cx), math.floor(cy))
        centre = (cx - origin[0], cy - origin[1])
        pts = [(x + cx, y + cy) for x, y in blades(r - 0.6, 0.0)]
        owner.node(pts, kind="path", path=FAN_PATHS[k], tone=9, outline=True, pivot=origin, exact=True,
                   fan=(centre, r - 0.6), reach=disc(bx.T, cu, cv, r, 16))
        owner.fill(disc(bx.T, cu, cv, 1.3, 8), 10)
        self.fans.append(k)


def city():
    """Every solid of the scene; order() sorts them back to front."""
    C = City()
    g = C.grid

    # --- the sky, clouds and the haze far behind
    sky = solid("sky", Box(-3000, -2990, -3000, -2990, 0, 1))
    sky.fill([(0, TOP), (480, TOP), (480, BOTTOM), (0, BOTTOM)], SKY, occ=True, sky=(SKY, NIGHT, GLOW))
    for k, pts in enumerate([ellipse(360, 58, 34, 30, 16), ellipse(318, 46, 20, 12, 12), ellipse(398, 44, 22, 14, 12),
                             ellipse(30, 44, 26, 9, 12), ellipse(150, 42, 18, 7, 12)]):
        c = solid(f"cloud{k}", Box(-2900 + 2 * k, -2899 + 2 * k, -2900, -2899, 0, 1))
        c.fill(pts, 13, occ=True, sky=(13, CLOUD, None))
    glows = []
    haze = [  # (fx, fy, a, b, spire)
        (16, 76, 18, 12, 0), (52, 56, 12, 10, 0), (126, 56, 12, 10, 1), (152, 70, 14, 10, 0), (260, 46, 18, 16, 0),
        (300, 62, 12, 10, 0), (334, 86, 16, 12, 0), (392, 84, 14, 12, 1), (474, 56, 16, 12, 0), (238, 98, 14, 10, 0),
        (96, 98, 12, 10, 0), (430, 104, 12, 10, 0),
    ]
    for k, (fx, fy, a, b, spire) in enumerate(haze):
        bx = tower(fx, fy, a, b, -1200 + k * 12)
        h = solid(f"haze{k}", bx)
        h.faces(HAZE)
        if spire:
            for step in range(2):
                sb = Box(bx.x0 + 2 + 2 * step, bx.x1 - 2 - 2 * step, bx.y0 + 2 + 2 * step, bx.y1 - 2 - 2 * step,
                         bx.z1 + 6 * step, bx.z1 + 6 * step + 6)
                h.faces_of(sb, HAZE)
            mx, my = bx.T(a / 2, b / 2)
            h.fill([(mx - 1.2, my - 11), (mx - 0.3, my - 26), (mx + 0.3, my - 26), (mx + 1.2, my - 11)], 11)
        h.fill(quad(bx.L, 2, 7, a - 4, 1.5), 12)
        for r in range(2):
            h.node(quad(bx.L, 2, 15 + r * 8, a - 4.5, 1), kind="glow", low=11, high=14,
                   param=hash32(k, r, 9) & 255, tone=14)
        if k % 2:
            glows.append((h, quad(bx.L, 3 + (k * 5) % max(1, int(a) - 6), 11 + (k % 3) * 8, 1.6, 1.6), k))
        else:
            glows.append((h, quad(bx.R, 2 + (k * 3) % max(1, int(b) - 5), 9 + ((k + 1) % 3) * 9, 1.6, 1.6), k + 40))
    for h, pts, seed in glows:
        h.node(pts, kind="glow", low=11, high=15, param=hash32(seed, 7, 9) & 255, tone=15)

    # --- behind the track
    # B: the tall dark tower left of centre, with a plant room on its roof and lit strips
    s, bx = C.building("B", 88, 50, 29, 16, -60, (9, 6, 3))
    C.box_on("B0", bx, 6, 3, 14, 10, 8, lid=5)
    s.node(quad(bx.L, 7.25, 14, 2, 46), kind="window", low=4, high=14, param=40, tone=14)
    s.node(quad(bx.R, 6.25, 12, 2, 40), kind="window", low=2, high=13, param=170, tone=13)
    s.fill(quad(bx.L, 18, 8, 1, 70), 8)
    # haze-side: C, a lower tower with a dark latticed roof
    s, bx = C.building("C", 130, 70, 17, 13, -30, MID, inner=4)
    for k in range(3):
        s.fill([bx.T(3 + k * 4, 3), bx.T(4 + k * 4, 3), bx.T(4 + k * 4, 10), bx.T(3 + k * 4, 10)], 7)
    g(s, bx.L, 3, 8, 2, 3.5, 7, 10, 7, 14, 320)
    # Ls: a light face behind the track, under it
    s, bx = C.building("Ls", 106, 122, 20, 12, -12, (11, 10, 6), parapet=0)
    s.fill(quad(bx.L, 3, 6, 1.2, 80), 9)
    # D: the big tower behind the advert, columns of wide windows
    s, bx = C.building("D", 188, 40, 36, 22, -90, (9, 7, 3), parapet=0)
    s.fill(quad(bx.L, 0, 0, bx.a, 1.6), 12)
    g(s, bx.L, 5, 8, 3, 5, 10.5, 11, 7, 14, 340)
    # U: a dark block under the track, in front of D's foot
    s, bx = C.building("U", 192, 158, 30, 14, -14, (8, 5, 2), parapet=0)
    g(s, bx.L, 3, 6, 3, 4, 9.5, 12, 5, 14, 360)
    # E: a dark tower beyond the advert tower
    s, bx = C.building("E", 312, 46, 24, 22, -150, (9, 6, 3))
    C.cornice(s, bx, 3, 10)
    g(s, bx.L, 3, 7, 3, 3.5, 7, 11, 6, 14, 420)
    g(s, bx.R, 3, 7, 2, 3.5, 8, 11, 3, 13, 440)

    # The advert tower: OP-XY, a pattern and a dash on its left face; bands and windows on its right
    tw = tower(240, 72, 47, 43, -4)
    t = solid("advert", tw)
    t.faces((8, 6, 3))
    t.fill([tw.T(2, 2), tw.T(45, 2), tw.T(45, 41), tw.T(2, 41)], 5)
    C.box_on("adv0", tw, 10, 8, 16, 14, 6, lid=4)
    C.box_on("adv1", tw, 30, 24, 9, 10, 4)
    t.fill(quad(tw.L, 2.5, 4, 42, 84), 5, occ=True)                     # the advert's screen
    for k in range(3):
        t.fill(quad(tw.R, 0, 8 + k * 7, 43, 3), 4)
        t.fill(quad(tw.R, 0, 11 + k * 7, 43, 1), 7)
    g(t, tw.R, 8, 34, 3, 3, 7, 12, 3, 13, 500)
    u = 6.5
    for ch in "OP-XY":
        pts = [tw.L(u + p[0], 10 + p[1]) for p in strokes(GLYPHS[ch], 1.6)]
        t.node(pts, kind="glyph", low=12, high=15, tone=13)
        u += GLYPH_WIDTH[ch] + 1.4
    # its two lines, as long as sine is high: one reshaping outline
    lx, ly = tw.L(LINE_U, LINE_W)
    t.node([tw.L(u, w) for u, w in lines_outline(0)], kind="path", path=LINES_PATH, low=12, high=15, tone=13,
           outline=True, pivot=(math.floor(lx), math.floor(ly)), exact=True, line=(lx - math.floor(lx), ly - math.floor(ly)),
           sweep=[(0, 0)], spread=0.6, reach=[tw.L(u, w) for u, w in lines_outline(1)])
    # the patterns, one shown: saw as ramps, square as a checker, pulse as thin bars
    saw = []
    for k in range(3):
        u0 = 9 + k * 9.6
        saw += [tw.L(u0 + (0.4 if k else 0), 64 if k == 0 else 63), tw.L(u0 + 9.2, 30)]
        saw += [tw.L(u0 + 9.2, 63)] if k < 2 else [tw.L(u0 + 9.2, 64)]
    t.node(saw, kind="pattern", param=1, low=12, high=15, tone=13)
    for r in range(5):
        for c in range(4):
            if (r + c) % 2 == 0:
                t.node(quad(tw.L, 9 + c * 7.2, 29 + r * 7, 6.4, 6.2), kind="pattern", param=2, low=12, high=15, tone=13)
    t.node(row(tw.L, 9.5, 29, 1.5, 35, 6, 5.2), kind="pattern", param=3, low=12, high=15, tone=13)
    t.node([tw.L(46.3, 3), tw.L(47, 3), tw.L(47, 98), tw.L(46.3, 98)], kind="neon", low=5, high=15, tone=12)
    for k, (uu, vv, hh) in enumerate([(8, 6, 44), (36, 4, 50), (40, 30, 36)]):
        C.mast(t, *tw.T(uu, vv), hh, rings=k != 1, phase=k * 80)

    # small blocks right of the advert tower
    s, bx = C.building("F", 302, 100, 22, 26, -110, FAR)
    C.box_on("F0", bx, 3, 4, 8, 7, 6, lid=6)
    C.box_on("F1", bx, 13, 12, 7, 9, 4)
    C.box_on("F2", bx, 3, 15, 7, 7, 9, lid=6)
    s, bx = C.building("V", 300, 150, 22, 14, -24, MID)
    g(s, bx.L, 3, 6, 2, 4, 8, 12, 7, 14, 380)
    C.box_on("V0", bx, 4, 3, 7, 6, 5)

    # The ball tower: a lattice stem, a ball with a band, a spire
    bb = tower(364, 108, 16, 12, -150)
    s = solid("ballbase", bb)
    s.faces(FAR)
    sx_, sy_ = bb.T(8, 6)
    ball = solid("ball", Box(bb.x0 + 6, bb.x0 + 10, bb.y0 + 4, bb.y0 + 8, bb.z1, bb.z1 + 50))
    bcx, bcy = sx_, sy_ - 42
    hoops = [(12, 3.6, 0), (15.5, 4.6, 1)]          # the rings round the ball: their back halves first
    for rx, ry, slot in hoops:
        C.ring(ball, bcx, bcy, rx, ry, 6, 6, slot, True)
    ball.fill([(sx_ - 3, sy_ + 1), (sx_ - 2, sy_ - 34), (sx_ + 2, sy_ - 34), (sx_ + 3, sy_ + 1)], 4)
    ball.fill([(sx_ + 0.6, sy_ + 0.5), (sx_ + 0.6, sy_ - 34), (sx_ + 2, sy_ - 34), (sx_ + 3, sy_ + 0.5)], 2)
    for k in range(4):
        ball.fill([(sx_ - 2.6, sy_ - 6 - 7 * k), (sx_ + 2.6, sy_ - 6 - 7 * k), (sx_ + 2.6, sy_ - 5 - 7 * k),
                   (sx_ - 2.6, sy_ - 5 - 7 * k)], 6)
    ball.fill(ellipse(bcx, bcy, 8, 8, 16), 4)
    ball.fill(ellipse(bcx - 2, bcy - 2.2, 4.6, 4.6, 12), 7)
    ball.fill(ellipse(bcx - 3, bcy - 3.4, 1.6, 1.6, 8), 10)
    ball.fill([(bcx - 8, bcy - 0.5), (bcx + 8, bcy - 0.5), (bcx + 8, bcy + 0.7), (bcx - 8, bcy + 0.7)], 2)
    C.mast(ball, bcx, bcy - 7.6, 20, rings=False, width=1.0, tone=3, phase=150)
    for rx, ry, slot in hoops:                      # and their front halves over the ball
        C.ring(ball, bcx, bcy, rx, ry, 6, 6, slot, False)

    # The helipad: a wide block; its pad, the H in a circle, corner lights; the craft
    hp = tower(352, 140, 47, 46, -80)
    s = solid("helipad", hp)
    s.faces((9, 7, 4))
    s.fill([hp.T(4, 4), hp.T(43, 4), hp.T(43, 42), hp.T(4, 42)], 6)
    s.line([hp.T(7, 7), hp.T(40, 7), hp.T(40, 39), hp.T(7, 39)], 11, 0.5)
    s.line(disc(hp.T, 23.5, 23, 10, 18), 11, 0.6)
    H = [(-3.6, -3), (-2.4, -3), (-2.4, -0.6), (2.4, -0.6), (2.4, -3), (3.6, -3), (3.6, 3), (2.4, 3), (2.4, 0.6),
         (-2.4, 0.6), (-2.4, 3), (-3.6, 3)]
    s.fill([hp.T(23.5 + du, 23 + dv) for du, dv in H], 11)
    for uu, vv in [(7, 7), (40, 7), (40, 39), (7, 39)]:
        s.fill([hp.T(uu - 1.5, vv - 0.4), hp.T(uu + 1.5, vv - 0.4), hp.T(uu + 1.5, vv + 0.4), hp.T(uu - 1.5, vv + 0.4)], 11)
    for k, (uu, vv) in enumerate([(4, 4), (43, 4), (43, 42), (4, 42)]):
        C.lights.append((s, *hp.T(uu, vv), 1.1, 40 + k * 64))
    s.fill(quad(hp.L, 0, 3, 47, 1.2), 10)
    g(s, hp.L, 4, 8, 4, 4.5, 10, 12, 7, 14, 600)
    g(s, hp.R, 4, 8, 4, 4, 10, 12, 4, 13, 640)
    hcx, hcy = hp.T(23.5, 23)

    # The hourglass tower: a dot-matrix billboard on its left face, windows on its right
    hg = tower(437, 68, 40, 18, -60)
    s = solid("hourglass", hg)
    s.faces((9, 6, 2))
    s.fill([hg.T(2, 2), hg.T(38, 2), hg.T(38, 16), hg.T(2, 16)], 5)
    C.box_on("hg0", hg, 6, 4, 10, 8, 6, lid=6)
    C.box_on("hg1", hg, 22, 6, 8, 7, 4)
    s.fill(quad(hg.L, 3, 7, 34, 78), 4, occ=True)                       # the billboard's frame
    s.node(quad(hg.L, 4.25, 8.25, 31.5, 75.5), kind="panel", low=12, high=15, tone=14, occ=True)
    s.node([hg.L(9, 17), hg.L(32, 17), hg.L(20.5, 45)], kind="sand", param=0, tone=SAND, keep=True,
           sweep=[(0, 0), (0, -SAND_DRIFT)])
    s.node([hg.L(20.5, 47), hg.L(32, 75), hg.L(9, 75)], kind="sand", param=1, tone=SAND, keep=True,
           sweep=[(0, 0), (0, SAND_DRIFT)])
    # the dot matrix: the panel's grey laid over the gaps between dots, rows and columns
    s.node(column(hg.L, 5.5, 10.75, 28, 1.25, 12, 3, spine=0.6), kind="panel", low=12, high=15, tone=14)
    s.node(column(hg.L, 5.5, 46.75, 28, 1.25, 12, 3, spine=0.6), kind="panel", low=12, high=15, tone=14)
    s.node(comb_down(hg.L, 7.75, 8.75, 1.25, 75, 9, 3, spine=0.6), kind="panel", low=12, high=15, tone=14)
    g(s, hg.R, 4, 8, 2, 4, 7, 12, 2, 13, 700)
    s.node([hg.L(39.3, 3), hg.L(40, 3), hg.L(40, 96), hg.L(39.3, 96)], kind="neon", low=5, high=15, tone=12)
    # far right: a tall tower with a grid of windows
    s, bx = C.building("R2", 474, 40, 18, 22, -200, (9, 7, 3), parapet=0)
    C.cornice(s, bx, 2.5, 11)
    g(s, bx.L, 2.5, 6, 2, 3.5, 7, 10, 7, 14, 760)
    g(s, bx.R, 2.5, 6, 3, 3.5, 6.5, 10, 3, 13, 780)
    # N: a tower in front of the helipad's foot, lit windows
    s, bx = C.building("N", 338, 140, 24, 14, -16, MID)
    g(s, bx.L, 3, 6, 3, 4, 7.5, 12, 7, 14, 800)
    g(s, bx.R, 3, 6, 2, 3, 6, 12, 4, 13, 830)
    # M: a dark tower behind the second track
    s, bx = C.building("M", 402, 130, 22, 16, -84, NEAR)
    C.cornice(s, bx, 2.5, 10)
    g(s, bx.L, 3, 6, 3, 3, 6.5, 12, 6, 14, 860)
    g(s, bx.R, 3, 6, 2, 3, 6.5, 12, 3, 13, 880)

    # --- the second track: a double viaduct, lower right, running up-right behind the main one
    tr2 = Box(214, 232, -300, -16, 33, 39)
    s = solid("track2", tr2)
    top2, left2, right2 = tr2.faces()
    s.fill(top2, 4, occ=True)
    s.fill(right2, 2, occ=True)
    s.fill(left2, 3, occ=True)
    for v in (2.5, 6.5, 11, 15):
        s.fill([P(214 + v, -300, 39), P(214 + v + 0.8, -300, 39), P(214 + v + 0.8, -16, 39), P(214 + v, -16, 39)], 8)
    s.fill([P(232, -300, 36.6), P(232, -16, 36.6), P(232, -16, 35.6), P(232, -300, 35.6)], 5)
    for k, yy in enumerate((-80, -160, -240)):
        p = solid(f"pier2{k}", Box(216, 230, yy - 4, yy + 4, 0, 33))
        p.faces((7, 5, 2), sides=(False, True, True))
    # the road's cars, tiny, two lanes each way; drawn on the road, under what is in front of it
    road = solid("road", Box(214, 232, -300, -16, 39, 40))
    for k, (lane, way, speed, home, phase) in enumerate(ROAD_CARS):
        pts = [P(lane + du, home + dv, 39.2) for du, dv in ((0.6, 1.5), (0.6, -1.5), (-0.6, -1.5), (-0.6, 1.5))]
        road.node(pts, kind="car", param=k, tone=14 if way < 0 else 11, keep=True, way=way, speed=speed,
                  phase=phase, home=home, sweep=[(-320, 160), (320, -160)])

    # --- the main track: deck, rails and piers, along x
    beam = solid("beam", Box(-160, 300, 3, 9, 54, 62))
    beam.faces((3, 2, 2), sides=(False, True, False))
    deck = Box(-160, 300, 0, 12, 62, 70)
    s = solid("track", deck)
    top, left, right = deck.faces()
    s.fill(top, 3, occ=True)
    s.fill(left, 5, occ=True)
    # the rails and the deck's lip: thin ribbons, stroked, so they cost no fill
    for v, t_ in ((2.25, 8), (9.25, 8), (5.75, 2)):
        s.line([P(-160, v, 70), P(300, v, 70), P(300, v + 0.5, 70), P(-160, v + 0.5, 70)], t_, 0.5)
    s.line([P(-160, 12, 69.75), P(300, 12, 69.75), P(300, 12, 69.25), P(-160, 12, 69.25)], 9, 0.5)
    s.fill([P(-160, 12, 64), P(300, 12, 64), P(300, 12, 62), P(-160, 12, 62)], 3)
    # the deck's side in segments: a joint midway between each two piers, joined along the dark foot
    s.fill(row(deck.L, 119.7, 1.5, 0.6, 6, 4, 80, spine=0.6), 3, occ=False)
    for k, x in enumerate((-80, 0, 80, 160, 240)):
        p = solid(f"pier{k}", Box(x, x + 8, 4, 8, 0, 54))
        p.faces((7, 5, 2), sides=(False, True, True))
    # the train: two cars on the deck, nodes, drawn between the track and what is in front of it
    train = solid("train", Box(-160, 300, 1, 11, 70, 84))
    car_len, gap = 46, 3
    lead_x1 = 142                                   # home: the lead car's front, world x
    for car in (1, 0):                              # the rear car first: the lead car is nearer and in front
        x1 = lead_x1 - car * (car_len + gap)
        cb = Box(x1 - car_len, x1, 2, 10, 70.5, 83)
        top_, left_, right_ = cb.faces()
        parts = [("roof", top_, 14), ("side", left_, 10), ("end", right_, 6),
                 ("band", quad(cb.L, 3, 2.6, car_len - 6, 3.6), 3),
                 ("skirt", quad(cb.L, 0, 9.8, car_len, 2.7), 4)]
        if car == 0:
            parts += [("screen", quad(cb.R, 0.8, 1.6, 5.4, 4.6), 2), ("lamp", quad(cb.R, 1.2, 8.4, 1.8, 1.2), 15)]
        parts.append(("doors", row(cb.L, 9, 2.2, 1.6, 7, 3, 14, spine=0.6), 8))   # joined by a sill along their feet
        # every part of a car about its front corner, so it can run far off the screen's left
        front = P(x1, 10, 83)
        for role, pts, tone in parts:
            train.node(pts, kind="train", role=f"car{car}.{role}", tone=tone, keep=True,
                       pivot=(round(front[0]), round(front[1])),
                       sweep=[(-TRAIN_REACH, -TRAIN_REACH / 2), (TRAIN_REACH, TRAIN_REACH / 2)])

    # --- in front of the track
    # A: the tall tower at the left: a slatted roof, slats down its face, an antenna
    s, bx = C.building("A", 44, 84, 52, 18, 50, (8, 6, 3), inner=4)
    for k in range(4):
        s.fill([bx.T(8, 4 + k * 3), bx.T(44, 4 + k * 3), bx.T(44, 5.2 + k * 3), bx.T(8, 5.2 + k * 3)], 2)
    s.fill(column(bx.L, 8, 10, 12, 1.6, 12, 4.5, spine=1), 2, occ=False)
    g(s, bx.R, 3, 8, 2, 2.4, 7, 12, 3, 13, 900)
    C.mast(s, *bx.T(10, 9), 40, phase=20)
    # S: the sun billboard's block
    sb = tower(74, 112, 38, 12, 66)
    s = solid("sun", sb)
    s.faces((9, 6, 3))
    s.fill([sb.T(2, 2), sb.T(36, 2), sb.T(36, 10), sb.T(2, 10)], 6)
    s.fill(quad(sb.L, 2.5, 4, 33, 72), 4, occ=True)
    s.node(quad(sb.L, 3.75, 5.25, 30.5, 69.5), kind="panel", low=12, high=15, tone=14, occ=True)
    # a sunrise over water: the sun's half disc on the horizon, and its reflection below in stripes
    # cut to the mirrored half disc, narrowing as they go down; lowcut clears them from the bottom up
    r, hz = 12, 34
    sun = [sb.L(19 + r * math.cos(math.pi * k / 10), hz - r * math.sin(math.pi * k / 10)) for k in range(11)]
    s.fill(sun, 8)
    s.fill(quad(sb.L, 4.5, hz + 0.5, 29, 0.7), 8)                                 # the horizon
    for k in range(SUN_STRIPES):
        top = hz + 2.2 + k * 2.7                                                  # the k-th from the horizon
        bottom = top + 1.4 - k * 0.15
        wide = math.sqrt(r * r - ((top + bottom) / 2 - hz) ** 2)                  # the mirrored disc's half width there
        s.node([sb.L(19 - wide, top), sb.L(19 + wide, top), sb.L(19 + wide, bottom), sb.L(19 - wide, bottom)], kind="stripe", param=SUN_STRIPES - 1 - k,
               tone=8, keep=True)
    s.node([sb.L(37.3, 3), sb.L(38, 3), sb.L(38, 76), sb.L(37.3, 76)], kind="neon", low=4, high=15, tone=12)
    C.lights.append((s, *sb.T(1, 11), 1.0, 90))
    # L: the low block at the bottom left: fans on its roof, a lit band on its face
    lb = tower(40, 190, 45, 20, 92)
    s = solid("fans", lb)
    s.faces((7, 5, 2))
    s.fill([lb.T(2, 2), lb.T(43, 2), lb.T(43, 18), lb.T(2, 18)], 5)
    s.fill(quad(lb.L, 0, 6, 45, 6.5), 3)
    C.band(s, lb.L, 2, 7, 9, 3.4, 4.5, 4.8, 3, 14, 940)
    C.fan(s, lb, 14, 10, 6.5)
    C.box_on("L0", lb, 30, 6, 9, 8, 6, lid=5)
    # Dd: the long low block with the dish and a mast
    dd = tower(150, 230, 96, 30, 132)
    s = solid("dish", dd)
    s.faces((8, 5, 2))
    s.fill([dd.T(2, 2), dd.T(94, 2), dd.T(94, 28), dd.T(2, 28)], 5)
    for k, (u0, v0, a0, b0, h0) in enumerate([(40, 6, 12, 10, 8), (60, 14, 9, 8, 6), (76, 4, 12, 12, 9)]):
        C.box_on(f"Dd{k}", dd, u0, v0, a0, b0, h0, lid=4 + k % 2)
    s.fill([dd.T(8, 16), dd.T(20, 16), dd.T(20, 26), dd.T(8, 26)], 2)
    s.fill([dd.T(10, 18), dd.T(18, 18), dd.T(18, 24), dd.T(10, 24)], 4)
    dx, dy = dd.T(28, 10)
    C.mast(s, *dd.T(68, 6), 56, phase=200)
    # Bc: the block at the bottom centre: a plant room with vents
    bc = tower(292, 236, 80, 30, 132)
    s = solid("Bc", bc)
    s.faces((8, 5, 2))
    s.fill([bc.T(2, 2), bc.T(78, 2), bc.T(78, 28), bc.T(2, 28)], 5)
    r, rb = C.box_on("Bc0", bc, 34, 6, 18, 14, 11, lid=5)
    for k in range(3):
        r.fill(quad(rb.L, 2 + k * 5.5, 3, 3, 5), 2)
    for k in range(3):
        s.fill([bc.T(8 + k * 7, 18), bc.T(13 + k * 7, 18), bc.T(13 + k * 7, 25), bc.T(8 + k * 7, 25)], 2)
    # Rf: the block at the right: a wide base with lit floors, a box above with fans and marks
    rf = tower(462, 196, 54, 30, -12)
    s = solid("Rf", rf)
    s.faces((8, 5, 2))
    s.fill(quad(rf.L, 0, 0, rf.a, 2), 9)
    C.band(s, rf.L, 4, 8, 6, 5.5, 4, 7.5, 5, 14, 980, per=3)
    C.band(s, rf.L, 4, 16, 6, 5.5, 4, 7.5, 5, 14, 990, per=3)
    s2, ru = C.box_on("Rfu", rf, 6, 4, 44, 24, 26, shades=(10, 8, 4))
    s2.fill([ru.T(2, 2), ru.T(42, 2), ru.T(42, 22), ru.T(2, 22)], 6)
    for k in range(3):
        s2.fill(quad(ru.L, 9 + k * 10, 8, 2.4, 8), 2)
        s2.fill(quad(ru.L, 11.4 + k * 10, 13.6, 2.4, 2.4), 2)
    s2.fill(quad(ru.L, 0, 21, 44, 1.6), 5)
    C.fan(s2, ru, 13, 12, 7)
    C.fan(s2, ru, 30, 12, 7)
    C.lights.append((s2, *ru.T(43, 1), 1.0, 230))

    # masts, rings and lights; the dish; the craft
    for owner, pts, param in C.rings:
        owner.node(pts, kind="ring", param=param, low=7, high=14, tone=11, outline=True)
    for owner, x, y, rr, phase in C.lights:
        owner.node([(x, y - rr), (x + rr, y), (x, y + rr), (x - rr, y)], kind="light", low=4, high=15,
                   param=phase % 256, tone=15)
    owner = solid("dishitself", Box(dd.x0 + 24, dd.x0 + 32, dd.y0 + 6, dd.y0 + 14, dd.z1, dd.z1 + 16))
    owner.fill([(dx - 1.5, dy + 1), (dx - 0.8, dy - 7), (dx + 0.8, dy - 7), (dx + 1.5, dy + 1)], 3)
    owner.fill([(dx - 4, dy + 1.5), (dx + 4, dy + 1.5), (dx + 4, dy - 0.5), (dx - 4, dy - 0.5)], 4)
    tilt = 0.5
    dpts = [(dx + 8 * math.cos(a) * math.cos(tilt) - 6 * math.sin(a) * math.sin(tilt),
             dy - 11 + 8 * math.cos(a) * math.sin(tilt) + 6 * math.sin(a) * math.cos(tilt))
            for a in [2 * math.pi * k / 16 for k in range(16)]]
    owner.node(dpts, kind="dish", role="disc", tone=12, keep=True, pivot=(round(dx), round(dy - 11)), spread=2)
    owner.node([(dx - 0.5, dy - 11), (dx + 4.5, dy - 16.5), (dx + 5.5, dy - 16), (dx + 0.5, dy - 10.4)],
               kind="dish", role="feed", tone=4, keep=True, pivot=(round(dx), round(dy - 11)), spread=2)
    owner.node(ellipse(dx, dy - 11, 4.4, 3.4, 12, 0.5), kind="ring", param=0, low=6, high=10, tone=8, outline=True)
    owner.node(ellipse(dx, dy - 11, 1.6, 1.3, 8, 0.5), kind="ring", param=1, low=6, high=10, tone=8, outline=True)

    craft = solid("craft", Box(hp.x0 + 10, hp.x0 + 36, hp.y0 + 10, hp.y0 + 36, hp.z1, hp.z1 + 40))
    cx, cy = hcx, hcy
    craft.node(ellipse(cx, cy, 6, 2.6, 12), kind="shadow", tone=4, keep=True)
    up = [(0, 2), (0, -CRAFT_RISE - 2)]
    # the craft: a flying car, its nose to the right: two thruster pods on a keel under its hull, a
    # dark canopy, a fin with a beacon, a headlight, and its underside in shadow
    def part(role, pts, tone, **spec):
        craft.node([(cx + x, cy + y) for x, y in pts], kind="craft", role=role, tone=tone, keep=not spec,
                   sweep=up, pivot=(round(cx), round(cy - 4)), **spec)      # one pivot: the parts move as one
    part("thrusters", [(-8.6, -2.7), (-4.8, -2.7), (-4.8, -2.1), (4.8, -2.1), (4.8, -2.7), (8.4, -2.7), (8.9, -1.95),
                       (8.4, -1.2), (4.8, -1.2), (4.8, -1.6), (-4.8, -1.6), (-4.8, -1.2), (-8.6, -1.2), (-9.1, -1.95)], 5)
    part("hull", [(-8.2, -2.5), (-8.7, -4.1), (-7.8, -5.0), (-7.2, -6.7), (-6.1, -6.7), (-5.4, -5.0), (4.6, -4.9),
                  (8.2, -4.2), (9.8, -3.2), (9.3, -2.6), (7.6, -2.4)], 12)
    part("underside", [(-8.0, -3.15), (7.9, -3.05), (9.3, -2.6), (7.6, -2.4), (-8.2, -2.5)], 4)
    part("canopy", [(-3.4, -4.95), (-1.8, -6.9), (2.0, -7.2), (4.0, -6.0), (4.9, -4.9)], 3)
    part("headlight", [(8.9, -3.85), (9.8, -3.45), (8.9, -3.05), (8.3, -3.45)], 15)
    part("beacon", [(-6.6, -7.9), (-5.9, -7.2), (-6.6, -6.5), (-7.3, -7.2)], 15, low=6, high=15, param=1)
    return SOLIDS


# ------------------------------------------------------------------ the depth order and the clipping
def screen_poly(pts):
    p = Polygon(pts)
    return p if p.is_valid else p.buffer(0)


def surface(g):
    """Only the parts of a clipped region with area: a clip can leave a stray edge or point."""
    if g.geom_type == "GeometryCollection":
        return shapely.union_all([q for q in g.geoms if q.geom_type in ("Polygon", "MultiPolygon")])
    return g


def behind(a, b):
    """Whether box a is wholly behind box b: separated along some axis, toward the viewer."""
    return a.x1 <= b.x0 or a.y1 <= b.y0 or a.z1 <= b.z0


def order(solids):
    """The solids back to front: a topological sort of who is behind whom where they overlap on screen."""
    n = len(solids)
    hull = []
    for s in solids:
        g = shapely.union_all([reach(dict(kind="node", item=it)) if it.kind == "node" else screen_poly(it.pts)
                               for it in s.items])
        hull.append(g)
    after = {k: set() for k in range(n)}
    incoming = [0] * n
    for i in range(n):
        for j in range(i + 1, n):
            if not hull[i].intersects(hull[j]):
                continue
            a, b = solids[i].box, solids[j].box
            ab, ba = behind(a, b), behind(b, a)
            if not (ab or ba): print("INTERSECT", solids[i].name, solids[j].name); continue
            if ab and ba:
                continue
            lo, hi = (i, j) if ab else (j, i)
            after[lo].add(hi)
            incoming[hi] += 1
    ready = sorted(k for k in range(n) if incoming[k] == 0)
    out = []
    while ready:
        k = ready.pop(0)
        out.append(solids[k])
        for m in sorted(after[k]):
            incoming[m] -= 1
            if incoming[m] == 0:
                ready.append(m)
                ready.sort()
    assert len(out) == n, "a cycle in the depth order"
    return out


def pieces(g):
    """A clipped region as simple outlines on the grid: holes cut away with a vertical line
    through each, long outlines halved, slivers dropped."""
    if g.is_empty:
        return []
    if g.geom_type in ("MultiPolygon", "GeometryCollection"):
        return [p for q in g.geoms for p in pieces(q)]
    if g.geom_type != "Polygon":
        return []
    if g.area < 0.6:
        return []
    if g.interiors:
        hole = g.interiors[0]
        xs = [p[0] for p in hole.coords]
        cut = round((min(xs) + max(xs)) / 2 / GRID) * GRID
        if not (min(xs) < cut < max(xs)):
            cut = (min(xs) + max(xs)) / 2
        x0, y0, x1, y1 = g.bounds
        return (pieces(g.intersection(rect(x0 - 1, y0 - 1, cut, y1 + 1), grid_size=GRID)) +
                pieces(g.intersection(rect(cut, y0 - 1, x1 + 1, y1 + 1), grid_size=GRID)))
    pts = tidy(list(g.exterior.coords)[:-1])
    if pts is None:
        return []
    if len(pts) > 60:
        x0, y0, x1, y1 = g.bounds
        if x1 - x0 >= y1 - y0:
            cut = round((x0 + x1) / 2 / GRID) * GRID
            halves = (rect(x0 - 1, y0 - 1, cut, y1 + 1), rect(cut, y0 - 1, x1 + 1, y1 + 1))
        else:
            cut = round((y0 + y1) / 2 / GRID) * GRID
            halves = (rect(x0 - 1, y0 - 1, x1 + 1, cut), rect(x0 - 1, cut, x1 + 1, y1 + 1))
        return [p for h in halves for p in pieces(g.intersection(h, grid_size=GRID))]
    return [pts]


def tidy(pts):
    """Drops repeated and collinear points and spikes (turns within 2.5 degrees of straight back)."""
    pts = [(round(x / GRID) * GRID, round(y / GRID) * GRID) for x, y in pts]
    changed = True
    while changed and len(pts) >= 3:
        changed = False
        for k in range(len(pts)):
            a, b, c = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
            if b == a or b == c or abs(cross(sub(b, a), sub(c, b))) < 1e-9:
                del pts[k]
                changed = True
                break
            u, v = sub(a, b), sub(c, b)
            cosine = (u[0] * v[0] + u[1] * v[1]) / (length(u) * length(v))
            if cosine > math.cos(math.radians(2.6)):
                del pts[k]
                changed = True
                break
    if len(pts) < 3 or abs(area(pts)) < 0.6 or not simple(pts):
        return None
    pts = start_long(pts)
    try:
        check(pts, "piece")
    except AssertionError:
        return None
    return pts


def compose():
    """The artwork's paths and nodes, in drawing order: static outlines clipped front to back."""
    solids = order(city())
    flat = [(s, it) for s in solids for it in s.items]
    cover = Polygon()
    out = []
    dropped = 0.0
    for s, it in reversed(flat):
        if it.kind == "node":
            if it.node.get("probe"):    # the first pass: which rows of this column's windows show
                seen = screen_poly(it.pts).intersection(STAGE)
                if not cover.is_empty:
                    seen = seen.difference(cover)
                it.node["seen"] = seen
                for key, slices in it.node["probe"]:
                    for w, q in slices:
                        # (not at a simple fraction: an edge through a slice's middle must not round either way by machine)
                        if seen.intersection(Polygon(q)).area >= 0.37 * abs(area(q)):
                            FOUND[key] = max(FOUND.get(key, 0), w + 0.5)
                            FOUND[("top",) + key] = min(FOUND.get(("top",) + key, w), w)
            out.append(dict(kind="node", item=it, solid=s))
            if it.node.get("occ"):      # a node always shown, always in place: it hides what is behind it
                g = screen_poly(it.pts).buffer(-0.3, join_style="mitre")
                cover = shapely.union_all([cover, g.intersection(STAGE, grid_size=GRID)], grid_size=GRID)
            continue
        g = screen_poly(it.pts)
        vis = surface(g.intersection(STAGE, grid_size=GRID))
        if not cover.is_empty:
            vis = vis.difference(cover, grid_size=GRID)
        if getattr(it, "sky", None):
            # nodes that darken: above the horizon to the night's grey, below it to the glow's; each
            # within 63 px of its origin
            day, night, glow = it.sky
            bands = ((TOP, HORIZON, night), (HORIZON, BOTTOM, glow)) if glow else ((TOP, BOTTOM, night),)
            for y0, y1, end in bands:
                band = []
                for q in pieces(vis.intersection(rect(0, y0, 480, y1), grid_size=GRID)):
                    if abs(area(q)) < 4:        # a sliver at the header's edge: not worth a node
                        continue
                    xs, ys = [p[0] for p in q], [p[1] for p in q]
                    if max(xs) - min(xs) <= 120 and max(ys) - min(ys) <= 120:
                        band.append(q)
                    else:       # too big for a node: cut into cells
                        band += [c for x0 in range(0, 480, 100) for c in
                                 pieces(Polygon(q).intersection(rect(x0, y0, x0 + 100, y1), grid_size=GRID))]
                for q in band:
                        out.append(dict(kind="node", item=Item("node", q, node=dict(kind="sky", low=day, high=end,
                                                                                   tone=day)), solid=s))
            if it.occ:
                cover = shapely.union_all([cover, g.intersection(STAGE, grid_size=GRID)], grid_size=GRID)
            continue
        ps = pieces(vis)
        dropped += max(0.0, vis.area - sum(abs(area(p)) for p in ps))
        for p in reversed(ps):
            out.append(dict(kind=it.kind, pts=p, tone=it.tone, width=it.width))
        if it.occ:
            cover = shapely.union_all([cover, g.intersection(STAGE, grid_size=GRID)], grid_size=GRID)
    out.reverse()
    return solids, merge(out), dropped


def reach(e):
    """Where an element can ever draw: a node over its whole travel, a line along its stroke."""
    if e["kind"] == "node":
        it = e["item"]
        base = screen_poly(it.pts)
        if "reach" in it.node:
            base = shapely.union_all([base, screen_poly(it.node["reach"])]).convex_hull
        moves = it.node.get("sweep", [(0, 0)])
        g = shapely.union_all([shapely.affinity.translate(base, dx, dy) for dx, dy in moves]).convex_hull
        return g.buffer(it.node.get("spread", 0.6))
    if e["kind"] == "line":
        return Polygon(e["pts"]).exterior.buffer(e["width"] / 2 + 0.1)
    return Polygon(e["pts"])


def merge(out):
    """Joins touching static outlines of the same grey into one, where nothing drawn between
    them in the order overlaps either, then puts everything back in an order that keeps every
    overlap the right way round. Fewer, longer outlines: fewer bytes, the same picture."""
    from shapely.strtree import STRtree
    n = len(out)
    shapes = [reach(e) for e in out]
    tree = STRtree(shapes)
    succ = [set() for _ in range(n)]
    pred = [set() for _ in range(n)]
    for i in range(n):
        for j in tree.query(shapes[i]):
            j = int(j)
            if j <= i:
                continue
            if shapes[i].intersection(shapes[j]).area > 0.02:
                succ[i].add(j)
                pred[j].add(i)
    alive = [True] * n
    poly = [Polygon(e["pts"]) if e["kind"] == "fill" else None for e in out]

    def path(a, b):
        """Whether b can be reached from a along the order's edges."""
        seen, stack = {a}, [a]
        while stack:
            k = stack.pop()
            for m in succ[k]:
                if m == b:
                    return True
                if m not in seen:
                    seen.add(m)
                    stack.append(m)
        return False

    changed = True
    while changed:
        changed = False
        live = [k for k in range(n) if alive[k] and poly[k] is not None]
        t2 = STRtree([poly[k] for k in live])
        for ai, a in enumerate(live):
            if not alive[a]:
                continue
            # in index order: the tree's own order of equal boxes differs between C++ libraries
            for bi in sorted(int(x) for x in t2.query(poly[a])):
                b = live[int(bi)]
                if b == a or not alive[b] or not alive[a] or out[a]["tone"] != out[b]["tone"]:
                    continue
                # they must share an edge: under half a pixel isn't one (0.49, not 0.5: an edge of exactly half a
                # pixel comes out a hair either side of it on different machines)
                if poly[a].intersection(poly[b]).length < 0.49 and poly[a].boundary.intersection(poly[b].boundary).length < 0.49:
                    continue
                if path(a, b) or path(b, a):
                    continue
                u = shapely.union_all([poly[a], poly[b]], grid_size=GRID)
                if u.geom_type != "Polygon" or u.interiors:
                    continue
                pts = tidy(list(u.exterior.coords)[:-1])
                if pts is None or len(pts) > 60:
                    continue
                poly[a] = Polygon(pts)
                out[a] = dict(out[a], pts=pts)
                alive[b] = False
                for m in succ[b]:
                    pred[m].discard(b)
                    if m != a:
                        succ[a].add(m)
                        pred[m].add(a)
                for m in pred[b]:
                    succ[m].discard(b)
                    if m != a:
                        pred[a].add(m)
                        succ[m].add(a)
                succ[a].discard(a)
                pred[a].discard(a)
                changed = True
    # back in order: a topological sort, keeping the original order where free
    import heapq
    count = {k: len([m for m in pred[k] if alive[m]]) for k in range(n) if alive[k]}
    ready = [k for k, c in count.items() if c == 0]
    heapq.heapify(ready)
    final = []
    while ready:
        k = heapq.heappop(ready)
        final.append(out[k])
        for m in succ[k]:
            if alive[m]:
                count[m] -= 1
                if count[m] == 0:
                    heapq.heappush(ready, m)
    assert len(final) == sum(alive), "the merged order has a cycle"
    return final


# ------------------------------------------------------------------ nodes
MOVING = ("light", "dish", "craft", "shadow", "train", "car")
KINDS = ["fixed", "window", "glow", "glyph", "pattern", "panel", "sand", "neon", "ring", "sky", "stripe", "light",
         "dish", "craft", "shadow", "train", "car"]


class Node:
    def __init__(self, item):
        spec = item.node
        self.kind = spec["kind"]
        self.role = spec.get("role", self.kind)
        self.param = spec.get("param", 0)
        self.low, self.high = spec.get("low", 0), spec.get("high", 0)
        self.tone = spec.get("tone", 8)
        self.keep = spec.get("keep", False)
        self.outline = spec.get("outline", False)
        pts = item.pts
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        self.ox, self.oy = spec.get("pivot", (round((min(xs) + max(xs)) / 2), round((min(ys) + max(ys)) / 2)))
        self.path = spec.get("path")
        self.spec = spec
        if spec.get("exact"):          # a reshaping outline: its points as display.cpp writes them, in order
            self.rel = [(round(x - self.ox, 2), round(y - self.oy, 2)) for x, y in pts]
        else:
            self.rel = [(round((x - self.ox) / GRID) * GRID, round((y - self.oy) / GRID) * GRID) for x, y in pts]
            self.rel = start_long(self.rel)
        self.abs = [(x + self.ox, y + self.oy) for x, y in self.rel]

    @property
    def fill(self):
        return not self.outline


def build():
    # the first pass finds how far down each column of windows shows; the second draws them so
    SEEN.clear()
    FOUND.clear()
    compose()
    SEEN.update(FOUND)
    solids, out, dropped = compose()
    nodes = []
    for e in out:
        if e["kind"] == "node":
            n = Node(e["item"])
            e["node"] = n
            nodes.append(n)
    # node numbers: the reshaping outlines 0..3, the header's bars 4..11, then the city's by kind, so
    # display.cpp walks them in groups
    paths = sorted((n for n in nodes if n.path is not None), key=lambda n: n.path)
    assert [n.path for n in paths] == list(range(PATHS)), "a reshaping outline is missing"
    for n in paths:
        n.index = n.path
        assert len(n.rel) == PATH_POINTS and len(set(n.rel)) == PATH_POINTS, f"path {n.path}: {len(n.rel)} points"
    rank = {k: i for i, k in enumerate(KINDS)}
    numbered = sorted((n for n in nodes if n.path is None), key=lambda n: rank[n.kind])
    for i, n in enumerate(numbered):
        n.index = FIRST + i
    return dict(solids=solids, out=out, nodes=numbered, paths=paths, dropped=dropped)


# ------------------------------------------------------------------ writing
# The artwork's path syntax: "M x,y L x,y ... Z", no space before the Z. Artwork written so has run on
# the OP-XY (Tetragon's Galileo, nodes and still outlines alike, in the firmware from overnight-002 on).
# Shorter forms save bytes but none has been seen on the device yet, so each is opt-in, for a build
# meant to test it (--compact, comma-separated):
#   nospace    no space before each L (every path)
#   implicit   the points after the first without an L, implicit lineto (still outlines only)
#   relative   relative commands, m, l and z: each point from the one before (still outlines only)
# The nodes always keep exactly M, L ... and Z: the SDK binds them so (docs/display.md, "Artwork").
COMPACT = set()


def path_d(pts, still=False):
    if not COMPACT:
        return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in pts) + "Z"
    lineto = "" if still and "implicit" in COMPACT else "L"
    gap = "" if "nospace" in COMPACT and lineto else " "
    if still and "relative" in COMPACT:
        h = [(round(float(num(x)) * 100), round(float(num(y)) * 100)) for x, y in pts]
        steps = [h[0]] + [(b[0] - a[0], b[1] - a[1]) for a, b in zip(h, h[1:])]
        text = [f"{num(x / 100)},{num(y / 100)}" for x, y in steps]
        return "m" + (gap + lineto.lower()).join(text) + "z"
    return "M" + (gap + lineto).join(f"{num(x)},{num(y)}" for x, y in pts) + "Z"


def write_panel(sc):
    out = [f'<g id="{ID}_panel">']
    fill_static = 0.0
    for e in sc["out"]:
        if e["kind"] == "fill":
            check(e["pts"], "a static outline")
            out.append(f'<path d="{path_d(e["pts"], still=True)}" fill="{grey(e["tone"])}"/>')
            fill_static += abs(area(e["pts"]))
        elif e["kind"] == "line":
            check(e["pts"], "a static line")
            out.append(f'<path d="{path_d(e["pts"], still=True)}" fill="none" stroke="{grey(e["tone"])}" stroke-width="{num(e["width"])}"/>')
        else:
            n = e["node"]
            check(n.rel, f"node {n.index} ({n.role})", 63)
            t = grey(n.tone)
            if n.path is not None:
                paint = f'fill="none" stroke="{t}" stroke-width="{num(PAL_WIDTH)}"'
            elif n.outline:
                paint = f'fill="none" stroke="{t}" stroke-width="{num(PAL_WIDTH)}"'
            else:
                # a fill alone: kept, it is the still artwork's paint; repainted, the palette lends the
                # fill (and its stroke), so an authored stroke would only be bytes
                paint = f'fill="{t}"'
            out.append(f'<path id="{ID}_node_{n.index}" d="{path_d(n.rel)}" {paint}/>')
    for k in range(16):
        out.append(f'<circle id="{ID}_palette_{k}" cx="640" cy="0" r="1" fill="{grey(k)}" stroke="{grey(k)}" '
                   f'stroke-width="{num(PAL_WIDTH)}"/>')
    out.append(f'<text id="{ID}_text_16" transform="matrix(1 0 0 1 640 320)" fill="#BDBDBD" '
               f'font-family="UniversNextPro-Light" font-size="10px">-</text>')
    header = HEADER
    for k in range(8):
        header = header.replace(f'velvet_node_{k}"', f'{ID}_node_{PATHS + k}"')
    out.append(header.replace("velvet_", ID + "_").strip())
    out.append("</g>")
    text = "\n".join(out) + "\n"
    size = len(text.encode())
    if size > ARTWORK_LIMIT: print(f"OVER: artwork is {size} bytes, over {ARTWORK_LIMIT}")
    (HERE / "panel.svg.fragment").write_text(text)
    return size, fill_static


def train_course(sc):
    """Where the train can be seen: how far before and after its home (px along screen x) it is
    wholly hidden, behind what is drawn after it, under the header or off the screen."""
    out = sc["out"]
    first = min(k for k, e in enumerate(out) if e["kind"] == "node" and e["node"].kind == "train")
    front = shapely.union_all([Polygon(e["pts"]) for e in out[first:] if e["kind"] == "fill"] +
                              [rect(-10, -10, 490, TOP)], grid_size=GRID)
    train = shapely.union_all([Polygon(n.abs) for n in sc["nodes"] if n.kind == "train"]).buffer(0.3)

    def seen(d):
        g = shapely.affinity.translate(train, d, d / 2).intersection(STAGE)
        return g.difference(front).area > 0.25
    assert seen(0), "the train is hidden at home"
    lo = 0
    while seen(lo - 1):
        lo -= 1
    hi = 0
    while seen(hi + 1):
        hi += 1
    for n in sc["nodes"]:
        if n.kind == "train":
            for d in (lo - 1, hi + 1):
                assert -64 <= n.ox + d <= 640 and -64 <= n.oy + d / 2 <= 320, "the train runs past where a node can be"
    return lo - 1, hi + 1


def check_paths(sc):
    """The reshaping outlines keep the device's drawing rules at every pose display.cpp gives them: each
    fan at every turn, the advert's lines at every step of sine."""
    for k in sc["paths"]:
        if k.path in FAN_PATHS:
            (fx, fy), reach = k.spec["fan"]
            for a in range(1024):
                check([(fx + x, fy + y) for x, y in blades(reach, a / 1024)], f"fan {k.path} at {a}/1024", 63)
        else:
            lx, ly = k.spec["line"]
            for step in range(101):
                check([(lx + u - LINE_U, ly + (u - LINE_U) / 2 + w - LINE_W) for u, w in lines_outline(step / 100)],
                      f"the advert's lines at sine {step}", 63)


def road_course(sc):
    """Each road car's run, px along screen x from where it is drawn: from where it is wholly hidden
    (behind what is drawn after it, off the screen) to where it is hidden again, and never past the
    road's ends."""
    out = sc["out"]
    for n in sc["nodes"]:
        if n.kind != "car":
            continue
        at = next(k for k, e in enumerate(out) if e["kind"] == "node" and e["node"] is n)
        front = shapely.union_all([Polygon(e["pts"]) for e in out[at:] if e["kind"] == "fill"] +
                                  [rect(-10, -10, 490, TOP)], grid_size=GRID)
        car = Polygon(n.abs).buffer(0.3)
        home = n.spec["home"]
        lo_road, hi_road = math.ceil(home + 17.5), math.floor(home + 298.5)   # d moves it to world y home - d

        def seen(d):
            g = shapely.affinity.translate(car, d, -d / 2).intersection(STAGE)
            return g.difference(front).area > 0.1
        shown = [d for d in range(lo_road, hi_road + 1) if seen(d)]
        assert shown, "a road car is never seen"
        lo, hi = min(shown) - 1, max(shown) + 1
        assert lo >= lo_road and hi <= hi_road, f"a road car is seen at the road's end ({lo}, {hi} of {lo_road}, {hi_road})"
        assert -64 <= n.ox + lo and n.ox + hi <= 640 and -64 <= n.oy - hi / 2 and n.oy - lo / 2 <= 320
        n.spec["course"] = (lo, hi)


def window_stats():
    """The windows: how many show (at least half seen), and how much of the seen height of each column
    of windows the strips cover (each drawn over what span() keeps of it)."""
    SEEN.clear()
    FOUND.clear()
    _, out, _ = compose()
    SEEN.update(FOUND)
    shown, seen_height, covered = 0, 0.0, 0.0
    for e in out:
        spec = e["item"].node if e["kind"] == "node" else {}
        if "grid" not in spec or spec["probe"][0][0][0] != "c":
            continue
        f, u, w0, a, dw, pw = spec["grid"]
        seen = spec["seen"]
        for r in range(200):
            q = quad(f, u, w0 + r * pw, a, dw - 0.5)
            if q[0][1] > BOTTOM:
                break
            if seen.intersection(Polygon(q)).area >= 0.53 * abs(area(q)):
                shown += 1
        key = spec["probe"][0][0]
        kept = [span(key, w0)] if key in SEEN else []
        for w, q in spec["probe"][0][1]:
            if seen.intersection(Polygon(q)).area >= 0.37 * abs(area(q)):
                seen_height += 0.5
                covered += 0.5 if any(top <= w and w + 0.5 <= foot + 1.5 for top, foot in kept) else 0
    SEEN.clear()
    FOUND.clear()
    return shown, seen_height, covered


def write_tables(sc):
    nodes, paths = sc["nodes"], sc["paths"]
    n = len(nodes) + FIRST
    start, end = sc["course"]
    restyle_end = min(k.index for k in nodes if k.kind in MOVING)

    def table(values_of, path=lambda k: 0, bar=0):
        return ", ".join([str(path(k)) for k in paths] + [str(bar)] * 8 + [str(values_of(k)) for k in nodes])
    q10 = lambda v: int(round(v * 1024))
    fans = [k for k in paths if k.path in FAN_PATHS]
    line = paths[LINES_PATH]
    cars = [k for k in nodes if k.kind == "car"]
    L = ["// Generated by tools/make_panel.py from the same description as panel.svg.fragment: do not edit.",
         "#pragma once",
         "#include <opxy/types.hpp>",
         "",
         f"namespace {ID} {{ namespace city {{",
         f"const opxy::u32 node_count = {n};",
         f"// Nodes 0..{PATHS - 1} are reshaping outlines of {PATH_POINTS} points: the fans' blades ({', '.join(map(str, FAN_PATHS))}) and the",
         f"// advert's two lines ({LINES_PATH}). The header's bars are {PATHS}..{PATHS + 7}, then the city's nodes, those that restyle",
         "// first, then those that move (display.cpp's restyled() and moved() say what each does).",
         f"const opxy::u32 paths = {PATHS}, path_points = {PATH_POINTS}, fans = {len(fans)}, lines = {LINES_PATH}, bars = {PATHS}, first = {FIRST};",
         "enum Kind : unsigned char { " + ", ".join(KINDS) + " };",
         f"const opxy::u32 restyle_end = {restyle_end};   // nodes first up to this restyle; the rest move",
         "const unsigned char kind[node_count] = {" + table(lambda k: KINDS.index(k.kind)) + "};",
         "// Each node's place at rest on screen, px: its outline is drawn about it.",
         "const short home_x[node_count] = {" + table(lambda k: k.ox, lambda k: k.ox) + "};",
         "const short home_y[node_count] = {" + table(lambda k: k.oy, lambda k: k.oy) + "};",
         "// Its palette greys, dark and bright (0..15), and a number its kind reads: a window's or a",
         "// light's rank or phase, a pattern's wave, a ring's place, a road car's lane.",
         "const unsigned char low[node_count] = {" + table(lambda k: k.low, lambda k: k.low) + "};",
         "const unsigned char high[node_count] = {" + table(lambda k: k.high, lambda k: k.high) + "};",
         "const unsigned char param[node_count] = {" + table(lambda k: k.param) + "};",
         "// Its points: what redrawing it costs.",
         "const unsigned char points[node_count] = {" + table(lambda k: len(k.rel), lambda k: PATH_POINTS, 4) + "};",
         "// The fans: each one's centre from its node, Q10 px, and its blades' reach in its roof's units, Q10;",
         f"// a blade's root is {FAN_HUB} units out, {FAN_SWEEP} rad back, and its tips {FAN_TIP} rad either side (Q16 turns below).",
         "const opxy::i32 fan_x[fans] = {" + ", ".join(str(q10(k.spec["fan"][0][0])) for k in fans) + "};",
         "const opxy::i32 fan_y[fans] = {" + ", ".join(str(q10(k.spec["fan"][0][1])) for k in fans) + "};",
         "const opxy::i32 fan_reach[fans] = {" + ", ".join(str(q10(k.spec["fan"][1])) for k in fans) + "};",
         f"const opxy::i32 fan_hub = {q10(FAN_HUB)}, fan_root = {round(FAN_SWEEP / (2 * math.pi) * 65536)}, fan_tip = {round(FAN_TIP / (2 * math.pi) * 65536)};",
         "// A quarter of a sine, Q14, in 64 steps: display.cpp turns the fans' blades with it.",
         "const short quarter_sine[65] = {" + ", ".join(str(round(16384 * math.sin(math.pi / 2 * i / 64))) for i in range(65)) + "};",
         "// The advert's lines: where they start from their node, Q10 px; their rows, height and spine in its",
         "// face's units, Q10; and each line's length at sine 0 and 100.",
         f"const opxy::i32 line_x = {q10(line.spec['line'][0])}, line_y = {q10(line.spec['line'][1])};",
         f"const opxy::i32 line_gap = {q10(LINE_GAP)}, line_height = {q10(LINE_H)}, line_spine = {q10(LINE_SPINE)};",
         "const opxy::i32 line_length[2][2] = {" + ", ".join("{%d, %d}" % (q10(a), q10(b)) for a, b in LINE_LENGTH) + "};",
         "// The train's run, px along screen x from home (it moves half that down): wholly hidden at",
         "// both ends, behind the nearer towers or off the screen.",
         f"const opxy::i32 train_start = {start}, train_end = {end};",
         f"// The sun poster's reflection: {SUN_STRIPES} stripes, numbered from the bottom (param).",
         f"const opxy::u32 sun_stripes = {SUN_STRIPES};",
         "// The road cars, in node order from the first: each one's run, px along screen x from home (it",
         "// moves half that up), wholly hidden at both ends; its way (1 up the road to the right, -1 down",
         "// it), its speed in px a second, and where it is on its run at rest.",
         f"const opxy::u32 cars = {len(cars)};",
         "const opxy::i32 car_start[cars] = {" + ", ".join(str(c.spec["course"][0]) for c in cars) + "};",
         "const opxy::i32 car_end[cars] = {" + ", ".join(str(c.spec["course"][1]) for c in cars) + "};",
         "const opxy::i32 car_way[cars] = {" + ", ".join(str(c.spec["way"]) for c in cars) + "};",
         "const opxy::i32 car_speed[cars] = {" + ", ".join(str(c.spec["speed"]) for c in cars) + "};",
         "const opxy::i32 car_phase[cars] = {" + ", ".join(str(c.spec["phase"]) for c in cars) + "};",
         "// The still artwork's fill below the header and the header's own, px^2: the device fills",
         "// these every frame.",
         f"const opxy::i32 static_fill = {round(sc['fill'])}, header_fill = {HEADER_FILL};",
         "// The windows: how many show (at least half seen), and how much of the height seen of each",
         "// column of windows they cover, per mille: down to where its face is last seen.",
         f"const opxy::u32 windows_shown = {sc['windows'][0]}, window_cover = {round(1000 * sc['windows'][2] / sc['windows'][1])};",
         "// The header's titles, definition.json's labels (the header lowercases them): the page's",
         "// controls by slot, then the Shift row's by entry.",
         "const char* const titles[8] = {" + ", ".join(json.dumps(t) for t in TITLES) + "};",
         "const char* const shift_titles[8] = {" + ", ".join(json.dumps(t) for t in SHIFT_TITLES) + "};",
         "}}"]
    (HERE / "city.h").write_text("\n".join(L) + "\n")


def write_outlines(sc):
    """Each node's outline measures, for tests/screen.cpp's drawing-rule and fill checks."""
    L = ["// Generated by tools/make_panel.py: each node's outline as the artwork draws it. Do not edit.",
         "// points, the shortest edge and the closing edge (Q10 px at unit scale), the sharpest turn",
         "// (tenths of a degree from straight back), its area (1/16 px^2), whether it has a fill, and",
         "// its box about its origin (Q10 px).",
         "#pragma once",
         "struct Outline { int points, gap, closing, turn, area, filled, left, right, top, bottom; };",
         f"const Outline outlines[{len(sc['nodes']) + FIRST}] = {{"]
    q10 = lambda v: int(math.floor(v * 1024)) if v < 0 else int(math.ceil(v * 1024))

    def entry(k):
        m = measures(k.rel)
        x0, x1, y0, y1 = m["box"]
        return (f"    {{{m['count']}, {int(m['gap'] * 1024 - 0.01)}, {int(m['closing'] * 1024 - 0.01)}, "
                f"{int(m['turn'] * 10 - 0.01)}, {max(1, round(m['area'] * 16))}, {int(k.fill and k.path is None)}, "
                f"{q10(x0)}, {q10(x1)}, {q10(y0)}, {q10(y1)}}},  // {k.role}")
    L += [entry(k) + " (as authored: display.cpp reshapes it)" for k in sc["paths"]]
    L += ["    {4, 61440, 20480, 900, 19200, 1, 0, 61440, 0, 20480},"] * 8      # the header's bars
    for k in sc["nodes"]:
        m = measures(k.rel)
        x0, x1, y0, y1 = m["box"]
        L.append(f"    {{{m['count']}, {int(m['gap'] * 1024 - 0.01)}, {int(m['closing'] * 1024 - 0.01)}, "
                 f"{int(m['turn'] * 10 - 0.01)}, {max(1, round(m['area'] * 16))}, {int(k.fill)}, "
                 f"{q10(x0)}, {q10(x1)}, {q10(y0)}, {q10(y1)}}},  // {k.role}")
    L.append("};")
    (HERE / "tests" / "outlines.h").write_text("\n".join(L) + "\n")


# ------------------------------------------------------------------ a mirror of display.cpp's frame, for previews
SLOW, FAST = 20, 90                   # display.cpp's train speeds, px a second


def r16(i, j, k):
    return hash32(i & 0xFFFFFFFF, j & 0xFFFFFFFF, k) >> 16


def sway(turn):
    x = turn & 0xFFFF
    x = x - 65536 if x >= 32768 else x
    return (x * (32768 - abs(x))) >> 18


def between(lo, hi, amount):
    return min(hi, lo + ((amount * (hi - lo + 1)) >> 16))


def pattern_of(shape):
    return 1 if shape < 19661 else 2 if shape < 55706 else 3


def ring_level(g, slot):
    if slot == 0:
        return 0 if g == 0 else 1 + min(2, (g * 6) >> 16)
    return 0 if g < 32768 else 1 + min(2, ((g - 32768) * 6) >> 16)


def toward(lo, hi, amount):
    return between(lo, hi, amount) if hi >= lo else lo - between(0, lo - hi, amount)


ROAD_GAP = 30                         # display.cpp's road_gap


def mirror(sc, c, held=False, hold=0, serial=1, clock=0, d=0, spin=0, road=None):
    """display.cpp's restyled() and moved(): each node shown -> (x, y, scale, turn degrees, grey or None);
    the reshaping outlines -> their points about their node and grey."""
    out = {}
    pulse = 0
    if held:
        t = (clock * 3 // 2) & 1023
        tri = t if t < 512 else 1023 - t
        pulse = (tri * (c["swell"] >> 8) * 3) >> 17
    for k in sc["nodes"]:
        i, p, lo, hi = k.index, k.param, k.low, k.high
        x, y, sc_, turn, shade = k.ox, k.oy, 1.0, 0.0, None
        kind = k.kind
        if kind == "window":
            spread = 154 + ((c["swell"] * 973) >> 16)
            if (p << 8) < c["bright"]:
                shade = hi
            elif held and c["swell"] and r16(i, serial, 3) < (c["swell"] >> 1) and hold >= ((r16(i, 0, 40) * spread) >> 16):
                shade = 15
            else:
                shade = lo
        elif kind == "glow":
            shade = hi if (p << 8) < c["bright"] else lo
        elif kind in ("glyph", "panel", "neon"):
            shade = between(lo, hi, c["reso"])
        elif kind == "pattern":
            if p != pattern_of(c["shape"]):
                continue
            shade = min(15, between(lo, hi, c["reso"]) + pulse)
        elif kind == "sand":
            dd = ((c["detune"] * SAND_DRIFT * 2) >> 16) / 2
            y += dd if p else -dd
        elif kind == "sky":
            shade = toward(lo, hi, (c["bright"] * c["bright"]) >> 16)
        elif kind == "ring":
            level = ring_level(c["ring"], p)
            if not level:
                continue
            shade = lo + (level - 1) * (hi - lo) // 2
        elif kind == "light":
            shade = hi if ((clock + p * 4) & 1023) < 230 else lo
        elif kind == "stripe":
            step = (c.get("lowcut", 0) * 100 + 32768) >> 16
            if step * (SUN_STRIPES + 1) > (p + 1) * 100:
                continue
        elif kind == "dish":
            turn = ((sway(clock * 5) * 8) >> 10)
        elif kind in ("craft", "shadow"):
            up = (c["bite"] * CRAFT_RISE) >> 16
            bob = ((sway(clock * 41) + 512) >> 10) if held and up else 0
            if kind == "shadow":
                sc_ = (1024 - up * 10) / 1024
            else:
                y += -up + bob
                if p:
                    shade = hi if ((clock + 300) & 1023) < 120 else lo
        elif kind == "train":
            start, end = sc["course"]
            if not (start <= d <= end):
                continue
            x += d
            y += d / 2
        elif kind == "car":
            s = k.spec
            lo_, hi_ = s["course"]
            run = (road[p] if road else s["phase"]) % (hi_ - lo_ + ROAD_GAP)
            if run > hi_ - lo_:
                continue
            dd = lo_ + run if s["way"] > 0 else hi_ - run
            x += dd
            y -= dd / 2
        out[i] = (x, y, sc_, turn, shade)
    for k in sc["paths"]:
        if k.path in FAN_PATHS:
            (fx, fy), reach = k.spec["fan"]
            pts = [(fx + x, fy + y) for x, y in blades(reach, ((spin + k.path * 9000) & 0xFFFF) / 65536)]
            out[k.index] = (k.ox, k.oy, pts, 9)
        else:
            lx, ly = k.spec["line"]
            step = (c.get("sine", 0) * 100 + 32768) >> 16
            pts = [(lx + u - LINE_U, ly + (u - LINE_U) / 2 + w - LINE_W) for u, w in lines_outline(step / 100)]
            out[k.index] = (k.ox, k.oy, pts, between(k.low, k.high, c["reso"]))
    return out


def preview(out_dir, name, sc, c, **kw):
    place = mirror(sc, c, **kw)
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="480" height="220">',
           '<style>@font-face{font-family:UniversNextPro-Light;src:url(file:///Users/mmockett/Documents/GitHub/OP-XY/tools/render/font/UniversNextPro-Light.ttf)}</style>',
           '<rect width="480" height="220" fill="#000"/>']
    fill = 0.0
    for e in sc["out"]:
        if e["kind"] == "fill":
            svg.append(f'<path d="{path_d(e["pts"])}" fill="{grey(e["tone"])}"/>')
        elif e["kind"] == "line":
            svg.append(f'<path d="{path_d(e["pts"])}" fill="none" stroke="{grey(e["tone"])}" stroke-width="{num(e["width"])}"/>')
        else:
            k = e["node"]
            if k.index not in place:
                continue
            if k.path is not None:
                x, y, pts, shade = place[k.index]
                svg.append(f'<path transform="translate({x} {y})" d="{path_d(pts)}" fill="none" stroke="{grey(shade)}" '
                           f'stroke-width="{PAL_WIDTH}"/>')
                continue
            x, y, s, turn, shade = place[k.index]
            t = grey(k.tone if shade is None else shade)
            width = 0 if k.keep and shade is None else PAL_WIDTH
            f = "none" if k.outline else t
            if not k.outline:
                fill += measures(k.rel)["area"] * s * s
            co, si = math.cos(math.radians(turn)) * s, math.sin(math.radians(turn)) * s
            svg.append(f'<path transform="matrix({co:.4f} {si:.4f} {-si:.4f} {co:.4f} {x:.2f} {y:.2f})" '
                       f'd="{path_d(k.rel)}" fill="{f}" stroke="{t}" stroke-width="{width}"/>')
    svg.append('<rect width="480" height="38" fill="#000"/>')
    shades = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for k, g_ in enumerate(shades):
        svg.append(f'<rect x="{60 * k}" width="60" height="20" fill="{g_}"/>')
    # The header at rest on page 1: the bright row is page 1's controls, or with Shift held the
    # Shift entries under them; the dim row is page 2's.
    bright = SHIFT_TITLES[:4] if c.get("shift") else TITLES[:4]
    values = c["values"]
    top = values[8:12] if c.get("shift") else values[:4]
    for k in range(4):
        col = "#CBCBCB" if k < 2 else "#000"
        svg.append(f'<text x="{5 + 120 * k}" y="10" font-family="UniversNextPro-Light" font-size="10" fill="{col}">{bright[k].lower()}</text>')
        svg.append(f'<text x="{65 + 120 * k}" y="17.4" font-family="UniversNextPro-Light" font-size="20" fill="{col}">{top[k]}</text>')
        svg.append(f'<text x="{5 + 120 * k}" y="33" font-family="UniversNextPro-Light" font-size="10" fill="#5A5A5A">{TITLES[4 + k].lower()}</text>')
        svg.append(f'<text x="{65 + 120 * k}" y="33" font-family="UniversNextPro-Light" font-size="13" fill="#707070">{values[4 + k]}</text>')
    svg.append("</svg>")
    pathlib.Path(out_dir).mkdir(parents=True, exist_ok=True)
    (pathlib.Path(out_dir) / f"{name}.svg").write_text("\n".join(svg))
    return fill


def captions(page, shift):
    """display.cpp's captions and shift_captions, for Q16 controls: page's eight, then the Shift row's."""
    amount = lambda v: str(((v << 15 >> 10) * 100 + (1 << 20)) >> 21)

    def exp2_fraction(x):
        return 65536 + ((x * 45426) >> 16) + ((((x * x) >> 16) * 20110) >> 16)

    def times_exp2(x, octaves):
        return ((x * exp2_fraction(octaves & 0xFFFF)) << (octaves >> 16)) >> 16

    def hertz(h):
        if h < 1000:
            return str(h)
        if h < 9950:
            t = (h + 50) // 100
            return f"{t // 10}.{t % 10}k"
        return f"{(h + 500) // 1000}k"

    def power15(d):
        root = 0
        for bit in (1 << b for b in range(15, -1, -1)):
            if (((root | bit) * (root | bit)) >> 16) <= d:
                root |= bit
        return (d * root) >> 16

    out = [amount(v) for v in page] + [amount(v) for v in shift]
    out[1] = f"{(power15(page[1]) * 30 + 32768) >> 16}c"
    out[2] = hertz(times_exp2(80, (page[2] * 7644) // 1000))
    out[9] = ["16'", "8'", "4'", "2'"][(shift[1] * 4) >> 16]
    cut = (shift[2] * 100 + 32768) >> 16
    out[10] = hertz(times_exp2(20, cut * 4354)) if cut else "off"
    out[13] = ["0.5", "1", "1.5", "2", "2.6", "3", "3.5", "4"][(shift[5] * 8) >> 16]
    return out


def previews(out, sc):
    """Tyrell at rest and in flight, with Shift held, and at bright 0, 25, 50, 75 and 100; every control at
    100 at rest and in flight, and at 0; ring and bite up, sine up."""
    q = lambda v: min(65535, int(v * 65536))
    names = ("shape", "detune", "bright", "reso", "chorus", "ring", "swell", "bite")
    tyrell_page = [q(v) for v in (0, .22, .46, .28, .45, 0, .8, 0)]
    tyrell_shift = [q(v) for v in (0, .375, 0, .06, .5, .3125, .4, .25)]

    def setting(page, row, **kw):
        return dict(zip(names, page), sine=row[0], lowcut=row[2], values=captions(page, row), **kw)

    def changed(page=None, row=None, **controls):
        page, row = list(page or tyrell_page), list(row or tyrell_shift)
        for name, v in controls.items():
            if name == "sine":
                row[0] = q(v)
            elif name == "lowcut":
                row[2] = q(v)
            else:
                page[names.index(name)] = q(v)
        return setting(page, row)
    tyrell = setting(tyrell_page, tyrell_shift)
    full = setting([65535] * 8, [65535] * 8)
    start, end = sc["course"]
    cars = [n for n in sc["nodes"] if n.kind == "car"]
    # in flight, each car where it shows: in the gaps between the towers in front of the road
    at = (-66, 30, -75, 26)
    road = [d - n.spec["course"][0] if n.spec["way"] > 0 else n.spec["course"][1] - d for n, d in zip(cars, at)]
    flying = dict(held=True, hold=900, clock=700, d=start + (end - start) * 2 // 5, spin=5000, road=road)
    states = [("resting", tyrell, {}),
              ("flying", tyrell, flying),
              ("all100-resting", full, {}),
              ("all100-flying", full, dict(held=True, hold=4000, clock=300, d=start + (end - start) * 3 // 4,
                                           spin=20000, road=[r + 60 for r in road])),
              ("shift", setting(tyrell_page, tyrell_shift, shift=True), {}),
              ("all0", setting([0] * 8, [0] * 8), {}),
              ("ring100", changed(ring=1), {}),
              ("craft", changed(bite=.6, ring=1), dict(held=True, clock=400, spin=9000, road=road)),
              ("sine100", changed(sine=1), {})]
    states += [(f"bright{v:03d}", changed(bright=v / 100), {}) for v in (0, 25, 50, 75, 100)]
    states += [(f"lowcut{v:03d}", changed(lowcut=v / 100), {}) for v in (0, 25, 50, 75, 100)]
    states += [(f"reso{v:03d}", changed(reso=v / 100), {}) for v in (0, 100)]
    for name, c, kw in states:
        f = preview(out, name, sc, c, **kw)
        print(f"{name}: node fill {f:.0f} px^2")


def main():
    if "--compact" in sys.argv:
        COMPACT.update(sys.argv[sys.argv.index("--compact") + 1].split(","))
        assert COMPACT <= {"nospace", "implicit", "relative"}, f"unknown forms: {COMPACT}"
        print(f"compact path syntax, for a test build only (not yet seen on the device): {', '.join(sorted(COMPACT))}")
    sc = build()
    sc["windows"] = window_stats()
    size, fill_static = write_panel(sc)
    sc["fill"] = fill_static
    sc["course"] = train_course(sc)
    road_course(sc)
    check_paths(sc)
    write_tables(sc)
    write_outlines(sc)
    # definition.json declares the frame's nodes: keep its count with the artwork's
    d = HERE / "definition.json"
    text = d.read_text()
    import re
    text = re.sub(r'"nodes": \d+', f'"nodes": {len(sc["nodes"]) + FIRST}', text, count=1)
    text = re.sub(r'"paths": \d+', f'"paths": {PATHS}', text, count=1)
    d.write_text(re.sub(r'"points": \d+', f'"points": {PATH_POINTS}', text, count=1))
    nodes = sc["nodes"]
    statics = sum(1 for e in sc["out"] if e["kind"] != "node")
    moving = sum(len(k.rel) for k in nodes if k.kind in MOVING)
    largest = max(len(k.rel) for k in nodes if k.kind not in MOVING)
    assert moving + largest <= BUDGET + 60, f"{moving} moving points and a node of {largest}"
    print(f"{len(nodes) + 8} nodes, {statics} static outlines, artwork {size} bytes of {ARTWORK_LIMIT}, "
          f"still fill {fill_static:.0f} px^2 ({fill_static / (480 * 182):.2f} of the stage, "
          f"{(fill_static + HEADER_FILL) / (480 * 182):.2f} with the header), train {sc['course']}")
    by = {}
    for k in nodes:
        by.setdefault(k.kind, [0, 0])
        by[k.kind][0] += 1
        by[k.kind][1] += len(k.rel)
    print(" ".join(f"{k}:{v[0]}n/{v[1]}p" for k, v in by.items()))
    shown, seen, has = sc["windows"]
    print(f"windows: {shown} shown; {100 * has / seen:.1f}% of each column's seen height")
    if "--preview" in sys.argv:
        previews(sys.argv[sys.argv.index("--preview") + 1], sc)


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


if __name__ == "__main__":
    main()
