"""Per-frame object snapshot, identical for the recompiled build (driver) and
the Unicorn oracle, so trajectories can be compared line by line."""
import struct

GAME_STRUCT = 0x458B00
FIELDS = [("x", 0x10, "i"), ("y", 0x14, "i"), ("z", 0x18, "i"),
          ("vx", 0x40, "d"), ("vy", 0x48, "d"), ("vz", 0x50, "d"),
          ("rx", 0x58, "d"), ("ry", 0x60, "d"), ("rz", 0x68, "d"),
          ("frame", 0x70, "i"), ("facing", 0x80, "b"),
          ("fall", 0xB0, "i"), ("shaking", 0xB4, "i"), ("bdefend", 0xB8, "i"),
          ("hp", 0x2FC, "i"), ("dark_hp", 0x300, "i"), ("max_hp", 0x304, "i"),
          ("mp", 0x308, "i"), ("team", 0x364, "i")]
HEADER = "frame idx id type " + " ".join(f[0] for f in FIELDS)


def snapshot(read, frame):
    """read(addr, n) -> bytes. Returns text lines, one per live object."""
    ex = read(GAME_STRUCT + 4, 400)
    ptrs = read(GAME_STRUCT + 0x194, 1600)
    lines = []
    for i in range(400):
        if not ex[i]:
            continue
        a = struct.unpack_from("<I", ptrs, 4 * i)[0]
        raw = read(a, 0x370)
        d = struct.unpack_from("<I", raw, 0x368)[0]
        ident, typ = struct.unpack("<ii", read(d + 0x6F4, 8)) if d else (-1, -1)
        vals = []
        for _, off, fmt in FIELDS:
            v = struct.unpack_from("<" + fmt, raw, off)[0]
            vals.append(repr(v) if fmt == "d" else str(v))
        lines.append("%d %d %d %d %s" % (frame, i, ident, typ, " ".join(vals)))
    return lines
