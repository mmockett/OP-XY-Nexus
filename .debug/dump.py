# Temporary: the generator's merge decisions, to compare platforms.
import json, sys
sys.path.insert(0, sys.argv[1])
import make_panel as mp
import shapely
from shapely.geometry import Polygon
from shapely.strtree import STRtree
log = {"edges": [], "pairs": []}
def merge(out):
    n = len(out)
    shapes = [mp.reach(e) for e in out]
    tree = STRtree(shapes)
    for i in range(n):
        for j in tree.query(shapes[i]):
            j = int(j)
            if j > i:
                a = shapes[i].intersection(shapes[j]).area
                if a > 0.02: log["edges"].append((i, j))
    poly = [Polygon(e["pts"]) if e["kind"] == "fill" else None for e in out]
    live = [k for k in range(n) if poly[k] is not None]
    t2 = STRtree([poly[k] for k in live])
    for ai, a in enumerate(live):
        for bi in t2.query(poly[a]):
            b = live[int(bi)]
            if b <= a or out[a]["tone"] != out[b]["tone"]: continue
            l1 = poly[a].intersection(poly[b]).length
            l2 = poly[a].boundary.intersection(poly[b].boundary).length
            u = shapely.union_all([poly[a], poly[b]], grid_size=mp.GRID)
            pts = mp.tidy(list(u.exterior.coords)[:-1]) if u.geom_type == "Polygon" and not u.interiors else None
            log["pairs"].append((a, b, repr(l1), repr(l2), u.geom_type, None if pts is None else len(pts)))
    return orig(out)
orig = mp.merge
mp.merge = merge
mp.compose()
json.dump(log, open(sys.argv[2], "w"))
