import shapely, math
from shapely.geometry import Polygon
from make_panel import *
import make_panel as mp
def merge(out):
    LOG=[]
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
            for bi in t2.query(poly[a]):
                b = live[int(bi)]
                if b == a or not alive[b] or not alive[a] or out[a]["tone"] != out[b]["tone"]:
                    continue
                # they must share an edge: under half a pixel isn't one (0.49, not 0.5: an edge of exactly half a
                # pixel comes out a hair either side of it on different machines)
                LOG.append(("len", a, b, repr(poly[a].intersection(poly[b]).length), repr(poly[a].boundary.intersection(poly[b].boundary).length)))
                if poly[a].intersection(poly[b]).length < 0.49 and poly[a].boundary.intersection(poly[b].boundary).length < 0.49:
                    continue
                if path(a, b) or path(b, a):
                    LOG.append(("path", a, b))
                    continue
                u = shapely.union_all([poly[a], poly[b]], grid_size=GRID)
                if u.geom_type != "Polygon" or u.interiors:
                    LOG.append(("shape", a, b, u.geom_type))
                    continue
                pts = tidy(list(u.exterior.coords)[:-1])
                if pts is None or len(pts) > 60:
                    LOG.append(("tidy", a, b, None if pts is None else len(pts), [list(map(repr,c)) for c in u.exterior.coords]))
                    continue
                LOG.append(("merge", a, b, len(pts)))
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
    import json; json.dump(LOG, open(OUTFILE, "w"))
    return final
