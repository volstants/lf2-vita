"""Binary-derived vectors for the sound queue (00416fb0, 00417090, 00419e60).

Runs only those three routines in Unicorn on synthetic state. The DirectSound
buffers are fake COM objects whose SetPan/SetVolume/Stop/SetCurrentPosition/
Play slots point at recorder stubs, so no OS call exists to be made.
Output: reference/round-audio-2026-09-23/audio-vectors.csv
"""
import csv
import pathlib
import random
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lf2emu import Machine  # noqa: E402

OUT = pathlib.Path(__file__).resolve().parents[2] / "reference/round-audio-2026-09-23"
CAM = 0x450BC4          # camera x used by 00416fb0/00417090
MASTER = 0x44D000       # DAT_0044d000
DS_OBJ = 0x44EECC       # IDirectSound*; the flush is skipped when it is null
LOADED_REQ, LOADED_L, LOADED_R, LOADED_BUF = 0x457588, 0x457BC8, 0x452170, 0x452948
ENGINE_REQ, ENGINE_L, ENGINE_R, ENGINE_BUF = 0x453E10, 0x4527E8, 0x4554C8, 0x451DB0


def main():
    m = Machine()
    calls = []

    def method(name, nargs):
        def h(a):
            calls.append((name, a[0]) + tuple(a.i(i) for i in range(1, nargs)))
            return 0
        return m.new_stub(name, h, nargs)

    vt = m.alloc(4 * 21)
    for idx, name, n in [(12, "Play", 4), (13, "SetCurrentPosition", 2), (15, "SetVolume", 2),
                         (16, "SetPan", 2), (18, "Stop", 1), (2, "Release", 1)]:
        m.w32(vt + 4 * idx, method(name, n))
    bufs = {}

    def buffer(slot_addr):
        o = m.alloc(16)
        m.w32(o, vt)
        m.w32(slot_addr, o)
        bufs[o] = slot_addr
        return o

    m.w32(DS_OBJ, m.alloc(16))
    for i in range(400):
        buffer(LOADED_BUF + 4 * i)
    for i in range(80):
        buffer(ENGINE_BUF + 4 * i)

    rng = random.Random(20260923)
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for case in range(3000):
        master = rng.choice([100, 100, 100, 0, 1, 50, 73, 99])
        m.w32(MASTER, master)
        cam = rng.choice([0, 0, 120, 800, 1500])
        m.w32(CAM, cam)
        reqs = []
        for _ in range(rng.randint(1, 4)):
            kind = rng.randint(0, 1)
            sid = rng.choice([0, 1, 2, 3, 17, 399]) if kind == 0 else rng.randint(0, 17)
            x = cam + rng.randint(-700, 1500)
            reqs.append((kind, sid, x))
            m.call(0x416FB0 if kind == 0 else 0x417090, (x & 0xFFFFFFFF, sid))
        calls.clear()
        m.call(0x419E60)
        played = []
        cur = {}
        for c in calls:
            slot = bufs[c[1]]
            kind = 0 if slot < ENGINE_BUF or slot >= LOADED_BUF else 1
            sid = (slot - (LOADED_BUF if kind == 0 else ENGINE_BUF)) // 4
            if c[0] == "SetPan":
                cur[(kind, sid)] = {"pan": c[2]}
            elif c[0] == "SetVolume":
                cur[(kind, sid)]["vol"] = c[2]
            elif c[0] == "Play":
                played.append((kind, sid, cur[(kind, sid)]["vol"], cur[(kind, sid)]["pan"]))
        rows.append({"case": case, "master": master, "cam": cam,
                     "requests": ";".join("%d:%d:%d" % r for r in reqs),
                     "played": ";".join("%d:%d:%d:%d" % p for p in played)})
    with open(OUT / "audio-vectors.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print("cases", len(rows), "played", sum(1 for r in rows if r["played"]))


if __name__ == "__main__":
    main()
