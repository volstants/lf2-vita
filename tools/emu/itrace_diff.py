#!/usr/bin/env python3
"""First divergence between two instruction traces (rc_trace / itrace.py).

  python3 tools/emu/itrace_diff.py oracle_trace.bin recomp_trace.bin [--fp]

--fp also compares the x87 registers that are live (ST(0)..ST(depth-1) are not
tracked, so it compares ST(0) plus any physical register a later instruction
could reach through a stale slot: all eight).
"""
import math
import struct
import sys

REC = struct.Struct("<10I8d")
NAMES = ["pc", "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "top"]

# Functions the recompiled build runs natively (emit.py NATIVE): their bodies
# have no trace records there, so drop them from the oracle's trace too.
NATIVE_RANGES = [(0x4450D0, 0x44517B), (0x4148A0, 0x414A26)]


def load(p):
    b = open(p, "rb").read()
    recs = (REC.unpack_from(b, i) for i in range(0, len(b) - REC.size + 1, REC.size))
    return [r for r in recs if not any(lo <= r[0] < hi for lo, hi in NATIVE_RANGES)]


def same_fp(a, b):
    if math.isnan(a) and math.isnan(b):
        return True
    return a == b


def st(r, i):
    return r[10 + ((r[9] + i) & 7)]


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    mode = "all" if "--fp-all" in sys.argv else ("st0" if "--fp" in sys.argv else "")
    print("records", len(a), len(b))
    for i, (x, y) in enumerate(zip(a, b)):
        bad = [k for k in range(10) if x[k] != y[k]]
        if not bad and mode == "st0" and not same_fp(st(x, 0), st(y, 0)):
            bad.append("st0")
        if not bad and mode == "all" and not all(same_fp(x[10 + k], y[10 + k]) for k in range(8)):
            bad.append("fp")
        if bad:
            print("first divergence at record", i, bad)
            for j in range(max(0, i - 12), min(i + 3, len(a), len(b))):
                ra, rb = a[j], b[j]
                regs = " ".join("%s=%x/%x" % (NAMES[k], ra[k], rb[k])
                                for k in range(1, 10) if ra[k] != rb[k])
                print("%6d %08x | %s st0=%r/%r" % (j, ra[0], regs, st(ra, 0), st(rb, 0)))
            x, y = a[i], b[i]
            print("oracle ST:", [st(x, k) for k in range(8)])
            print("recomp ST:", [st(y, k) for k in range(8)])
            return
    print("no divergence in common prefix")


if __name__ == "__main__":
    main()
