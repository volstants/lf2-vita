#!/usr/bin/env python3
"""Closed-loop driver for scenario tests on the recompiled lf2.exe.

Runs bin/lf2-recomp-test in pipe mode (LF2_PIPE=1): every frame the host
stops, the scenario reads game memory (objects, HP, positions) and decides
which Windows messages to post. Every message is logged as an events file,
so the exact same input can then be replayed on the Unicorn oracle (the
original lf2.exe) and on the recompiled build for a frame-by-frame compare.

Because the recompiled build is proven identical to the original frame by
frame, a controller that reads state is still deterministic: replaying its
log reproduces the same run on both.
"""
from __future__ import annotations

import os
import struct
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME_STRUCT = 0x458B00          # exists[400] at +4, object*[400] at +0x194

# object_t fields (confirmed against memory dumps of the running game)
OBJ = {
    "x": (0x10, "i"), "y": (0x14, "i"), "z": (0x18, "i"),
    "vx": (0x40, "d"), "vy": (0x48, "d"), "vz": (0x50, "d"),
    "rx": (0x58, "d"), "ry": (0x60, "d"), "rz": (0x68, "d"),
    "frame": (0x70, "i"), "facing": (0x80, "b"),
    "fall": (0xB0, "i"), "shaking": (0xB4, "i"), "bdefend": (0xB8, "i"),
    "hp": (0x2FC, "i"), "dark_hp": (0x300, "i"), "max_hp": (0x304, "i"), "mp": (0x308, "i"),
    "team": (0x364, "i"), "data": (0x368, "I"),
}
OBJ_SPAN = 0x370
DATA_ID, DATA_TYPE = 0x6F4, 0x6F8

# player 1 / player 2 keys (data\control.txt)
P1 = dict(up=0x68, down=0x62, left=0x64, right=0x66, attack=0x65, jump=0x60, defend=0x6B)
P2 = dict(up=0x57, down=0x58, left=0x41, right=0x44, attack=0x53, jump=0x09, defend=0xC0)
WM_KEYDOWN, WM_KEYUP = 0x100, 0x101
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x200, 0x201, 0x202
VK_F = {n: 0x6F + n for n in range(1, 13)}          # VK_F1 = 0x70


class Obj:
    def __init__(self, idx, addr, raw, ident, typ):
        self.idx, self.addr, self.id, self.type = idx, addr, ident, typ
        for k, (off, fmt) in OBJ.items():
            setattr(self, k, struct.unpack_from("<" + fmt, raw, off)[0])

    def __repr__(self):
        return ("#%d id%d t%d f%d hp%d mp%d x%d y%d z%d v(%.2f,%.2f,%.2f)" %
                (self.idx, self.id, self.type, self.frame, self.hp, self.mp,
                 self.x, self.y, self.z, self.vx, self.vy, self.vz))


class Game:
    def __init__(self, game_dir, out_dir, limit=20000, binary=None):
        os.makedirs(out_dir, exist_ok=True)
        self.out = out_dir
        self.log_path = os.path.join(out_dir, "events.txt")
        env = dict(os.environ, LF2_PIPE="1", LF2_NOHASH="1", LF2_PIPE_LOG=self.log_path)
        exe = binary or os.path.join(ROOT, "bin", "lf2-recomp-test")
        self.p = subprocess.Popen([exe, game_dir, str(limit), "-", out_dir], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, env=env, text=True, bufsize=1)
        self.frame = 0
        self.pending = {}           # frame -> [(msg, wp, lp)]
        self.held = set()
        self.trace = None           # objstate lines per finished frame
        self.marks = []             # (frame, label) for the report
        self._wait_frame()

    def record(self, path):
        import objstate
        self.trace = open(path, "w")
        self._objstate = objstate

    def mark(self, label):
        self.marks.append((self.frame, label))
        print("[%d] %s" % (self.frame, label), flush=True)

    # ── transport ───────────────────────────────────────────────────────────
    def _cmd(self, s):
        self.p.stdin.write(s + "\n")

    def _wait_frame(self):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise EOFError("host exited at frame %d" % self.frame)
            if line.startswith("F "):
                self.frame = int(line[2:])
                for m in self.pending.pop(self.frame, ()):
                    self._cmd("K %x %x %x" % m)
                if self.trace:
                    for ln in self._objstate.snapshot(self.read, self.frame - 1):
                        self.trace.write(ln + "\n")
                return

    def step(self, n=1):
        for _ in range(n):
            self._cmd("N")
            self.p.stdin.flush()
            self._wait_frame()

    def close(self):
        if self.trace:
            self.trace.close()
        try:
            self._cmd("Q")
            self.p.stdin.flush()
        except BrokenPipeError:
            pass
        self.p.wait()

    # ── memory ──────────────────────────────────────────────────────────────
    def read(self, addr, n):
        self._cmd("R %x %x" % (addr, n))
        self.p.stdin.flush()
        return bytes.fromhex(self.p.stdout.readline().strip())

    def r32(self, a):
        return struct.unpack("<i", self.read(a, 4))[0]

    def objects(self):
        ex = self.read(GAME_STRUCT + 4, 400)
        ptrs = self.read(GAME_STRUCT + 0x194, 1600)
        out = []
        for i in range(400):
            if not ex[i]:
                continue
            a = struct.unpack_from("<I", ptrs, 4 * i)[0]
            raw = self.read(a, OBJ_SPAN)
            d = struct.unpack_from("<I", raw, 0x368)[0]
            ident, typ = struct.unpack("<ii", self.read(d + DATA_ID, 8)) if d else (-1, -1)
            out.append(Obj(i, a, raw, ident, typ))
        return out

    def shot(self, name):
        self._cmd("S " + os.path.join(self.out, name))

    # ── input ───────────────────────────────────────────────────────────────
    def post(self, msg, wp, lp=0, at=None):
        at = self.frame if at is None else at
        if at <= self.frame:
            self._cmd("K %x %x %x" % (msg, wp, lp))
        else:
            self.pending.setdefault(at, []).append((msg, wp, lp))

    def down(self, vk):
        self.post(WM_KEYDOWN, vk)
        self.held.add(vk)

    def up(self, vk):
        self.post(WM_KEYUP, vk)
        self.held.discard(vk)

    def tap(self, vk, hold=2, after=4):
        """press, hold `hold` frames, release, then wait `after` frames"""
        self.down(vk)
        self.step(hold)
        self.up(vk)
        self.step(after)

    def seq(self, keys, hold=2, gap=1):
        """a command sequence like D>A: each key tapped in order"""
        for k in keys:
            self.down(k)
            self.step(hold)
            self.up(k)
            self.step(gap)

    def release_all(self):
        for vk in list(self.held):
            self.up(vk)

    def click(self, x, y, settle=5):
        lp = (y << 16) | x
        self.post(WM_MOUSEMOVE, 0, lp)
        self.step(settle)
        self.post(WM_LBUTTONDOWN, 1, lp)
        self.step(3)
        self.post(WM_LBUTTONUP, 0, lp)
        self.step(settle)

    def type_text(self, vks, hold=2, gap=2):
        for vk in vks:
            self.tap(vk, hold, gap)
