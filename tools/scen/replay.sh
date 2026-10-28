#!/bin/bash
# Replay one generated scenario on the original lf2.exe (Unicorn oracle) and on
# the recompiled build, then compare frame hashes, object traces and memory.
#   tools/scen/replay.sh <scen_dir> <game_dir>
# Exit status 0 only if every step ran and every comparison is identical:
# oracle and recompiled runs exit 0, strict hash comparison (all frames, heap
# every 10, run completed), object traces identical (recomp_obj.txt comes from
# tools/scen/run.py) and the final memory dumps equal. Output: compare.txt.
set -u
[ $# -eq 2 ] || { echo "usage: replay.sh <scen_dir> <game_dir>"; exit 2; }
D=$1; GAME=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
FAIL=0
fail() { echo "FAILED: $*"; FAIL=1; }

F=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['frames']-1)" "$D/marks.json") \
    || { echo "FAILED: cannot read $D/marks.json"; exit 1; }
MEM=$(printf %04d "$F")

python3 "$ROOT/tools/emu/oracle.py" --frames "$F" --events "$D/events.txt" --hashes --hash-every 10 \
    --objtrace "$D/oracle_obj.txt" --dump "$F" --out "$D" > "$D/oracle_hash.log" 2>&1
ORC=$?
LF2_HASH_EVERY=10 "$ROOT/bin/lf2-recomp-test" "$GAME" "$F" "$D/events.txt" "$D" "$F" > "$D/recomp_hash.log" 2>&1
RC=$?

{
  echo "scenario $(basename "$D") frames $F"
  [ $ORC -eq 0 ] || fail "oracle exit status $ORC"
  [ $RC -eq 0 ] || fail "recomp exit status $RC"
  python3 "$ROOT/tools/scen/cmp_hashes.py" "$D/recomp_hash.log" "$D/oracle_hash.log" --frames "$F" \
      || FAIL=1
  python3 - "$D" "$F" <<'P' || FAIL=1
import os, sys
d, F = sys.argv[1], int(sys.argv[2])
for name in ("recomp_obj.txt", "oracle_obj.txt"):
    if not os.path.isfile(os.path.join(d, name)):
        print("FAILED: object trace %s missing" % name)
        sys.exit(1)
def load(p):
    rows = {}
    for ln in open(p):
        f = int(ln.split(None, 1)[0])
        if 1 <= f <= F:
            rows.setdefault(f, []).append(ln.rstrip())
    return rows
a, b = load(d + "/recomp_obj.txt"), load(d + "/oracle_obj.txt")
fr = sorted(set(a) | set(b))
bad = [f for f in fr if a.get(f) != b.get(f)]
n = sum(len(v) for v in b.values())
if not fr:
    print("FAILED: object traces are empty")
    sys.exit(1)
print("object trace: %d frames, %d object-rows; %s" % (len(fr), n,
      "identical" if not bad else "FIRST DIFF frame %d" % bad[0]))
sys.exit(1 if bad else 0)
P
  if [ ! -f "$D/recomp_mem_$MEM.bin" ] || [ ! -f "$D/oracle_mem_$MEM.bin" ]; then
    fail "final memory dump of frame $F missing"
  elif cmp -s "$D/recomp_mem_$MEM.bin" "$D/oracle_mem_$MEM.bin"; then
    echo "final memory frame $F: identical"
  else
    fail "final memory frame $F: DIFFERENT"
  fi
  [ $FAIL -eq 0 ] && echo "REPLAY: identical" || echo "REPLAY: FAILED"
} > "$D/compare.txt"
cat "$D/compare.txt"
grep -q "^REPLAY: identical$" "$D/compare.txt"
