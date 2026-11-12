# Temporary: the generator's stages, to compare platforms.
import json, sys
sys.path.insert(0, sys.argv[1])
import make_panel as mp
orig = mp.merge
stages = {}
def merge(out):
    stages["premerge"] = [(e["kind"], e.get("tone"), e["pts"] if e["kind"] != "node" else e["item"].pts) for e in out]
    shapes = [mp.reach(e) for e in out]
    stages["reach_area"] = [round(s.area, 9) for s in shapes]
    r = orig(out)
    stages["merged"] = [(e["kind"], e.get("tone"), e["pts"] if e["kind"] != "node" else e["item"].pts) for e in r]
    return r
mp.merge = merge
solids = mp.order(mp.city())
stages["order"] = [s.name for s in solids]
mp.compose()
json.dump(stages, open(sys.argv[2], "w"))
