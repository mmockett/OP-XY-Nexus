# Temporary: the generator's merge log, to compare platforms.
import sys
sys.path.insert(0, sys.argv[1]); sys.path.insert(0, sys.path[0])
import make_panel as mp
import merge_logged
merge_logged.OUTFILE = sys.argv[2]
mp.merge = merge_logged.merge
mp.compose()
