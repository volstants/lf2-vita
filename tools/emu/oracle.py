"""Drive the original game loop headlessly.

    python3 tools/emu/oracle.py --frames 300 --shot out.png

Boot = CRT static initialisers + WinMain (0x43cf40). WinMain's message loop
calls the per-frame routine 0x43e9a0 whenever enough virtual time has passed;
a code hook on that address gives us one callback per original frame, where
scripted keys are applied and state is sampled.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import struct
import zlib
import sys

import numpy as np
import unicorn
from unicorn import x86_const as X

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lf2emu  # noqa: E402
from lf2emu import StopEmu  # noqa: E402
from runtime import LF2Machine  # noqa: E402

GAME_DIR = pathlib.Path("/home/claude/lf2game")
KEYTABLE = 0x455378          # 256 bytes of 'd'/'u' written by the WndProc
FRAME_FN = 0x43E9A0
WINMAIN = 0x43CF40
INITTERM_E = (0x4472D8, 0x4472E8)
INITTERM = (0x4472C0, 0x4472D4)


class Oracle(LF2Machine):
    def __init__(self, game_dir=GAME_DIR):
        super().__init__(game_dir)
        self.frame_limit = 0
        self.on_frame = None          # callback(oracle) before each frame
        self.after_frame = None       # callback(oracle) after each frame
        self._in_frame = False
        self.u.hook_add(unicorn.UC_HOOK_CODE, self._frame_hook, begin=FRAME_FN, end=FRAME_FN)
        self.keys = set()
        self.events = {}

    def _frame_hook(self, uc, addr, size, user):
        # Same protocol as src/recomp/host_test.cpp beginFrame(): report the
        # finished frame, stop at the limit, then deliver this frame's events.
        if self.frame > 0:
            if self.after_frame:
                self.after_frame(self)
        if self.frame >= self.frame_limit:
            uc.emu_stop()
            return
        self.frame += 1
        for m in self.events.get(self.frame, ()):
            self.post(*m)
        if self.on_frame:
            self.on_frame(self)

    def state_hashes(self):
        data = bytes(self.u.mem_read(0x44D000, 0x459524 - 0x44D000))
        heap = bytes(self.u.mem_read(lf2emu.HEAP_BASE, self.heap - lf2emu.HEAP_BASE))
        return zlib.crc32(data), zlib.crc32(heap)

    def load_events(self, path):
        for line in open(path):
            p = line.split()
            if len(p) == 4:
                self.events.setdefault(int(p[0]), []).append(
                    (int(p[1], 16), int(p[2], 16), int(p[3], 16)))

    def apply_keys(self):
        table = bytearray(b"u" * 256)
        for vk in self.keys:
            table[vk] = ord("d")
        self.write(KEYTABLE, bytes(table))

    def boot(self):
        for lo, hi in (INITTERM_E, INITTERM):
            for p in range(lo, hi, 4):
                fn = self.r32(p)
                if fn:
                    self.call(fn)

    def run(self, frames):
        """Run (or continue) WinMain until `frames` frames have executed."""
        self.frame_limit = frames
        u = self.u
        if not getattr(self, "_started", False):
            self._started = True
            sp = lf2emu.STACK_TOP - 0x1000
            sp -= 4; self.w32(sp, 0x400000)
            sp -= 4; self.w32(sp, lf2emu.MAGIC_RET)
            u.reg_write(X.UC_X86_REG_ESP, sp)
            start = WINMAIN
        else:
            start = u.reg_read(X.UC_X86_REG_EIP)
        self.bad = None
        try:
            u.emu_start(start, lf2emu.MAGIC_RET)
        except unicorn.UcError as e:
            raise RuntimeError("fault %s at %08x bad=%r" % (
                e, u.reg_read(X.UC_X86_REG_EIP), self.bad))
        if self.bad:
            raise RuntimeError("stopped: %r" % (self.bad,))

    def screenshot(self, path):
        from PIL import Image
        s = self.back or self.primary
        a = s.pix
        rgb = np.stack([(a >> 16) & 255, (a >> 8) & 255, a & 255], axis=2).astype(np.uint8)
        Image.fromarray(rgb).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--shot")
    ap.add_argument("--events")
    ap.add_argument("--hashes", action="store_true",
                    help="print per-frame state hashes (same format as lf2-recomp-test)")
    ap.add_argument("--dump", default="", help="comma list of frames to dump memory")
    ap.add_argument("--out", default="/tmp")
    ap.add_argument("--hash-every", type=int, default=1,
                    help="heap CRC only every N frames (the .data CRC is every frame)")
    ap.add_argument("--objtrace", help="write per-frame object state (tools/scen/objstate.py)")
    args = ap.parse_args()
    o = Oracle()
    if args.events:
        o.load_events(args.events)
    dumps = {int(x) for x in args.dump.split(",") if x}

    objtrace = open(args.objtrace, "w") if args.objtrace else None
    if objtrace:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scen"))
        import objstate

    def after(o):
        if args.hashes:
            d = zlib.crc32(bytes(o.u.mem_read(0x44D000, 0x459524 - 0x44D000)))
            if o.frame % args.hash_every == 0:
                h = zlib.crc32(bytes(o.u.mem_read(lf2emu.HEAP_BASE, o.heap - lf2emu.HEAP_BASE)))
                print("frame %d data %08x heap %08x top %08x" % (o.frame, d, h, o.heap), flush=True)
            else:
                print("frame %d data %08x top %08x" % (o.frame, d, o.heap), flush=True)
        if objtrace:
            rd = lambda a, n: bytes(o.u.mem_read(a, n))
            for line in objstate.snapshot(rd, o.frame):
                objtrace.write(line + "\n")
        if o.frame in dumps:
            with open("%s/oracle_mem_%04d.bin" % (args.out, o.frame), "wb") as f:
                f.write(bytes(o.u.mem_read(0x44D000, 0x459524 - 0x44D000)))
                f.write(bytes(o.u.mem_read(lf2emu.HEAP_BASE, o.heap - lf2emu.HEAP_BASE)))
            o.screenshot("%s/oracle_%04d.png" % (args.out, o.frame))
    o.after_frame = after
    o.boot()
    o.run(args.frames)
    if objtrace:
        objtrace.close()
    print("frames", o.frame, "time", o.now)
    for m in o.messages[:20]:
        print(m)
    print("texts", o.texts[-10:])
    print("sounds", o.sounds[-10:])
    if args.shot:
        o.screenshot(args.shot)


if __name__ == "__main__":
    main()
