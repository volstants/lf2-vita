#!/usr/bin/env python3
"""Generate one scenario on the recompiled build (closed loop).

  python3 tools/scen/run.py s1_davis_firen <game_dir> <out_root>

Writes <out>/<name>/events.txt (replayable), recomp_obj.txt (object state per
frame), marks.json (test labels by frame) and screenshots.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from driver import Game  # noqa: E402
from scenarios import SCENARIOS  # noqa: E402


def main():
    name, game_dir, root = sys.argv[1], sys.argv[2], sys.argv[3]
    out = os.path.join(root, name)
    if os.path.isdir(out):
        for f in os.listdir(out):
            os.remove(os.path.join(out, f))
    g = Game(game_dir, out, limit=60000)
    g.record(os.path.join(out, "recomp_obj.txt"))
    try:
        SCENARIOS[name](g)
    finally:
        frames = g.frame
        g.close()
        json.dump({"frames": frames, "marks": g.marks}, open(os.path.join(out, "marks.json"), "w"), indent=1)
    print("%s: %d frames, %d tests" % (name, frames, len(g.marks)))


if __name__ == "__main__":
    main()
