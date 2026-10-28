#!/usr/bin/env python3
"""Acceptance suite: replay every scenario of tests/scen/ on the recompiled
build and compare, strictly, with the hashes recorded from the original
lf2.exe on the Unicorn oracle (oracle_hash.log, kept in the repo).

  tools/scen/check_suite.py <game_dir> [--jobs N] [--bin PATH] [--out DIR]

The scenarios are the ones listed in tests/scen/suite.txt (required: a listed
scenario whose events.txt, marks.json or oracle_hash.log is missing fails the
suite before anything runs). A scenario passes only if:
  * the program exits with status 0;
  * its log has every frame 1..F (.data CRC every frame, heap CRC every 10)
    and "end frame F";
  * every hash equals the original's (cmp_hashes.py --frames F).
The suite passes only if every listed scenario passes. Exit status: 0 pass, 1 fail, 2 usage/setup error.
Paths may contain spaces (arguments are never re-split by a shell).
"""
import argparse
import concurrent.futures as cf
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run_one(scen, game, binary, out_root):
    name = scen.name
    frames = json.load(open(scen / "marks.json"))["frames"] - 1
    out = out_root / name
    out.mkdir(parents=True, exist_ok=True)
    log = out / "recomp_hash.log"
    env = dict(os.environ, LF2_HASH_EVERY="10")
    t0 = time.time()
    with open(log, "w") as fh:
        rc = subprocess.run([str(binary), str(game), str(frames), str(scen / "events.txt"), str(out)],
                            stdout=fh, stderr=subprocess.STDOUT, env=env).returncode
    secs = time.time() - t0
    if rc != 0:
        return name, frames, secs, False, "FAILED: program exit status %d" % rc
    cmp = subprocess.run([sys.executable, str(ROOT / "tools/scen/cmp_hashes.py"), str(log),
                          str(scen / "oracle_hash.log"), "--frames", str(frames)],
                         capture_output=True, text=True)
    lines = (cmp.stdout.strip() or cmp.stderr.strip() or "FAILED: no output").splitlines()
    return name, frames, secs, cmp.returncode == 0, lines[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--bin", default=os.environ.get("LF2_BIN", str(ROOT / "bin/lf2-recomp-test")))
    ap.add_argument("--out", default=os.environ.get("LF2_SUITE_OUT", str(ROOT / ".build/suite")))
    a = ap.parse_args()
    game, binary, out = pathlib.Path(a.game_dir), pathlib.Path(a.bin), pathlib.Path(a.out)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print("missing %s (make -f Makefile.recomp bin/lf2-recomp-test)" % binary)
        return 2
    if not (game / "data").is_dir():
        print("not a game folder: %s" % game)
        return 2
    manifest = ROOT / "tests/scen/suite.txt"
    if not manifest.is_file():
        print("SUITE: FAILED (missing %s)" % manifest)
        return 1
    names = [ln.split("#", 1)[0].strip() for ln in manifest.read_text().splitlines()]
    names = [n for n in names if n]
    if not names:
        print("SUITE: FAILED (no scenario listed in %s)" % manifest)
        return 1
    scens, problems = [], []
    for n in names:
        d = ROOT / "tests/scen" / n
        missing = [f for f in ("events.txt", "marks.json", "oracle_hash.log") if not (d / f).is_file()]
        if missing:
            problems.append("%s: missing %s" % (n, ", ".join(missing)))
        else:
            scens.append(d)
    if problems:
        print("\n".join(problems))
        print("SUITE: FAILED (%d of %d required scenarios incomplete; nothing was run)"
              % (len(problems), len(names)))
        return 1
    out.mkdir(parents=True, exist_ok=True)
    with cf.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        results = sorted(ex.map(lambda s: run_one(s, game, binary, out), scens))
    lines = ["%-18s %6d frames %7.1f s  %s" % (n, f, s, msg) for n, f, s, ok, msg in results]
    passed = sum(1 for r in results if r[3])
    verdict = ("SUITE: all identical to the original (%d/%d scenarios)" % (passed, len(results))
               if passed == len(results) else
               "SUITE: FAILED (%d of %d scenarios did not pass)" % (len(results) - passed, len(results)))
    text = "\n".join(lines + [verdict])
    print(text)
    (out / "summary.txt").write_text(text + "\n")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
