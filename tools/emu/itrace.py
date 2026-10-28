#!/usr/bin/env python3
"""Instruction-level trace of one frame on the Unicorn oracle.

Writes the same record format as rc_trace() in src/recomp/rt_core.cpp
(pc, eax, ecx, edx, ebx, esp, ebp, esi, edi, fpu top, 8 physical x87
registers as doubles) so
`itrace_diff.py` can find the first instruction where the recompiled build
and the original lf2.exe disagree.

  python3 tools/emu/itrace.py --events ev.txt --frame 1024 --out oracle_trace.bin
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unicorn  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

from oracle import Oracle  # noqa: E402

TEXT_LO, TEXT_HI = 0x401000, 0x44D000
REGS = [X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX,
        X.UC_X86_REG_ESP, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI]


def f80_to_double(mant, exp):
    sign = -1.0 if exp & 0x8000 else 1.0
    e = exp & 0x7FFF
    if e == 0 and mant == 0:
        return 0.0 * sign
    if e == 0x7FFF:
        return sign * float("inf") if (mant << 1) & ((1 << 64) - 1) == 0 else float("nan")
    try:
        return sign * float(mant) * 2.0 ** (e - 16383 - 63)
    except OverflowError:
        return sign * float("inf")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--events")
    ap.add_argument("--frame", type=int, required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    o = Oracle()
    if args.events:
        o.load_events(args.events)
    o.boot()
    o.run(args.frame - 1)
    out = open(args.out, "wb")
    u = o.u
    pack = struct.Struct("<10I8d").pack

    def hook(uc, addr, size, _):
        if o.frame != args.frame:
            return
        r = [uc.reg_read(x) for x in REGS]
        fsw = uc.reg_read(X.UC_X86_REG_FPSW)
        top = (fsw >> 11) & 7
        # UC_X86_REG_FPn are the physical registers (ST(i) = FP[(top+i)&7])
        fp = [f80_to_double(*uc.reg_read(X.UC_X86_REG_FP0 + i)) for i in range(8)]
        out.write(pack(addr, *r, top, *fp))

    u.hook_add(unicorn.UC_HOOK_CODE, hook, begin=TEXT_LO, end=TEXT_HI - 1)
    u.ctl_flush_tb()   # hooks only reach code translated after they were added
    o.run(args.frame)
    out.close()
    print("traced frame", args.frame, "records", os.path.getsize(args.out) // 104)


if __name__ == "__main__":
    main()
