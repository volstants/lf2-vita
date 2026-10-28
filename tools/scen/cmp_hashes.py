#!/usr/bin/env python3
"""Compare per-frame hash logs ("frame N data D [heap H] top T") of the
recompiled build and the oracle.

  cmp_hashes.py recomp.log oracle.log [--frames F] [--heap-every K]

Without --frames (old behaviour, for quick looks) only the frames present in
both logs are compared. With --frames F the comparison is strict and fails
unless:
  * both logs have a "frame N" line with data and top for every N in 1..F;
  * every frame multiple of K (default 10) has a heap CRC in both logs;
  * the recompiled log ends with "end frame F" (the run reached the end).
Exit status 0 only when identical; the last line of output says why not.
"""
import argparse
import sys


def load(p):
    frames, end = {}, None
    for ln in open(p, errors="replace"):
        t = ln.split()
        if len(t) >= 6 and t[0] == "frame":
            try:
                frames[int(t[1])] = dict(zip(t[2::2], t[3::2]))
            except ValueError:
                pass
        elif len(t) >= 3 and t[0] == "end" and t[1] == "frame":
            try:
                end = int(t[2])
            except ValueError:
                pass
    return frames, end


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recomp")
    ap.add_argument("oracle")
    ap.add_argument("--frames", type=int, help="strict: require every frame 1..F")
    ap.add_argument("--heap-every", type=int, default=10)
    a = ap.parse_args()
    (x, xend), (y, _) = load(a.recomp), load(a.oracle)

    if a.frames is None:
        common = sorted(set(x) & set(y))
        if not common:
            print("FAILED: no frame in common")
            return 1
        frames = common
    else:
        frames = range(1, a.frames + 1)
        for name, log in (("recomp", x), ("oracle", y)):
            missing = [f for f in frames if f not in log]
            if missing:
                print("FAILED: %s log lacks %d of %d frames (first %d)"
                      % (name, len(missing), a.frames, missing[0]))
                return 1
        if xend != a.frames:
            print("FAILED: recomp run did not reach the end (end frame %s, expected %d)"
                  % (xend, a.frames))
            return 1

    heap_checked = 0
    for f in frames:
        p, q = x[f], y[f]
        for k in ("data", "top"):
            if k not in p or k not in q:
                print("FAILED: frame %d lacks %s" % (f, k))
                return 1
            if p[k] != q[k]:
                print("DIVERGE frame %d %s %s != %s" % (f, k, p[k], q[k]))
                return 1
        must_heap = a.frames is not None and a.heap_every > 0 and f % a.heap_every == 0
        if must_heap and ("heap" not in p or "heap" not in q):
            print("FAILED: frame %d lacks the heap CRC" % f)
            return 1
        if "heap" in p and "heap" in q:
            heap_checked += 1
            if p["heap"] != q["heap"]:
                print("DIVERGE frame %d heap %s != %s" % (f, p["heap"], q["heap"]))
                return 1
    last = frames[-1]
    print("identical: %d frames (data every frame, heap on %d frames), last %d%s"
          % (len(frames), heap_checked, last, ", run completed" if a.frames else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
