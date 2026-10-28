#!/usr/bin/env python3
"""Per-test summary of a scenario from an object trace (the oracle's, i.e.
the original lf2.exe): states reached, HP/MP change, knockback (speed, height,
distance), projectiles/objects created, transformations.

  python3 tools/scen/analyze.py <scen_dir> <game_dir> [trace=oracle_obj.txt]
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from datinfo import decode, frames  # noqa: E402

COLS = ["frame", "idx", "id", "type", "x", "y", "z", "vx", "vy", "vz", "rx", "ry", "rz",
        "fr", "facing", "fall", "shaking", "bdefend", "hp", "dark_hp", "max_hp", "mp", "team"]

STATUS = [("ice", range(200, 203)), ("fire", range(203, 207)), ("falling", range(180, 192)),
          ("lying", range(230, 232)), ("dizzy", range(226, 230)), ("injured", range(220, 226)),
          ("caught", range(130, 145)), ("catching", range(120, 124)), ("defend", range(110, 112)),
          ("guard_broken", range(112, 115))]


def load_trace(p):
    rows = {}
    for ln in open(p):
        t = ln.split()
        r = {}
        for k, v in zip(COLS, t):
            r[k] = float(v) if k in ("vx", "vy", "vz", "rx", "ry", "rz") else int(v)
        rows.setdefault(r["frame"], {})[r["idx"]] = r
    return rows


def id_names(game):
    m = {}
    txt = open(os.path.join(game, "data", "data.txt"), encoding="latin1").read()
    for i, f in re.findall(r"id:\s*(\d+)\s+type:\s*\d+\s+file:\s*(\S+)", txt):
        m[int(i)] = f.replace("data\\", "").replace(".dat", "")
    return m


_frames_cache = {}


def frame_name(game, names, ident, fr):
    if ident not in _frames_cache:
        try:
            _frames_cache[ident] = frames(decode(os.path.join(game, "data", names[ident] + ".dat")))
        except Exception:
            _frames_cache[ident] = {}
    return _frames_cache[ident].get(fr, {}).get("name", str(fr))


def summarize(rows, t0, t1, game, names):
    out = {}
    fighters = [0, 1]
    first = rows.get(t0, {})
    born = set()
    for f in range(t0, t1):
        for i, r in rows.get(f, {}).items():
            if i not in first and r["type"] == 3:
                born.add(r["id"])
    out["projectiles"] = sorted(names.get(i, str(i)) for i in born)
    for i in fighters:
        seq = [rows[f][i] for f in range(t0, t1) if f in rows and i in rows[f]]
        if not seq:
            continue
        st = []
        for name, rg in STATUS:
            if any(r["fr"] in rg for r in seq):
                st.append(name)
        ids = []
        for r in seq:
            n = names.get(r["id"], str(r["id"]))
            if not ids or ids[-1] != n:
                ids.append(n)
        air = [r for r in seq if r["y"] < 0]
        hit_idx = next((k for k, r in enumerate(seq) if r["fr"] in range(180, 192) or r["fr"] in range(220, 230)), None)
        kb = {}
        if hit_idx is not None:
            after = seq[hit_idx:]
            kb["max_abs_vx"] = round(max(abs(r["vx"]) for r in after), 3)
            kb["max_up_vy"] = round(-min(r["vy"] for r in after), 3)
            kb["max_height"] = -min(r["y"] for r in after)
            land = next((k for k, r in enumerate(after) if r["fr"] in (230, 231)), None)
            end = after[land] if land is not None else after[-1]
            kb["distance_x"] = round(end["rx"] - after[0]["rx"], 2)
            kb["frames_to_lying"] = land
        out[i] = {
            "fighter": " -> ".join(ids),
            "status": st,
            "hp": (seq[0]["hp"], min(r["hp"] for r in seq), seq[-1]["hp"]),
            "mp": (seq[0]["mp"], min(r["mp"] for r in seq)),
            "airborne_frames": len(air),
            "knockback": kb,
        }
    return out


def main():
    d, game = sys.argv[1], sys.argv[2]
    trace = sys.argv[3] if len(sys.argv) > 3 else "oracle_obj.txt"
    rows = load_trace(os.path.join(d, trace))
    names = id_names(game)
    m = json.load(open(os.path.join(d, "marks.json")))
    marks = m["marks"] + [[m["frames"] - 1, "end"]]
    res = []
    for (t0, label), (t1, _) in zip(marks, marks[1:]):
        res.append({"label": label, "from": t0, "to": t1, **{str(k): v for k, v in summarize(rows, t0, t1, game, names).items()}})
    json.dump(res, open(os.path.join(d, "analysis.json"), "w"), indent=1)
    for r in res:
        print(r["label"], r["from"], r.get("projectiles"))
        for k in ("0", "1"):
            if k in r:
                print("   #%s %s" % (k, r[k]))


if __name__ == "__main__":
    main()
