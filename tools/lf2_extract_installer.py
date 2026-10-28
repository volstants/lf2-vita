#!/usr/bin/env python3
"""Extract the official LF2 v2.0a installer (LF2_v2.0a.exe, Clickteam Install
Maker) WITHOUT running it.

    python3 tools/lf2_extract_installer.py LF2_v2.0a.exe OUT_DIR

Layout of the installer overlay (reverse-engineered for this file only):
    "wwgT)H" signature, then blocks  u16 id | u16 flags | u32 size | body
    compressed body = u32 unpacked size | u8 method (1 zlib, 2 bzip2) | stream
    block 0x143A  file list: u32 count, entries  u16 len | u16 type | ...
                  file entry (type 0): +6 u32 offset, +10 u32 packed,
                  +18 u32 size, +62 NUL-terminated relative path
    block 0x7F7F  data: u32 total, then per file  u8 method | stream

The official download is https://www.lf2.net/LF2_v2.0a.exe (freeware). The
extracted lf2.exe must hash to the project's reference SHA-256.
"""
import bz2
import hashlib
import pathlib
import struct
import sys
import zlib

LF2_SHA = "12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5"


def unpack(body):
    size, method = struct.unpack_from("<IB", body, 0)
    data = zlib.decompress(body[5:]) if method == 1 else bz2.decompress(body[5:])
    assert len(data) == size
    return data


def extract(installer: pathlib.Path, out: pathlib.Path):
    d = installer.read_bytes()
    o = d.find(b"wwgT)H")
    if o < 0:
        raise SystemExit("not a Clickteam installer")
    o += 6
    blocks = {}
    while o + 8 <= len(d):
        bid, flags, size = struct.unpack_from("<HHI", d, o)
        blocks[bid] = d[o + 8:o + 8 + size]
        o += 8 + size
    listing = unpack(blocks[0x143A])
    data = blocks[0x7F7F]
    p, written = 4, []
    while p < len(listing):
        n, kind = struct.unpack_from("<HH", listing, p)
        if n == 0:
            break
        e = listing[p:p + n]
        if kind == 0 and n >= 62:
            off, packed = struct.unpack_from("<II", e, 6)
            size = struct.unpack_from("<I", e, 18)[0]
            name = e[62:].split(b"\0")[0].decode("latin1")
            raw = data[4 + off:4 + off + packed]
            try:
                blob = {1: lambda b: zlib.decompress(b), 2: bz2.decompress,
                        0: lambda b: b}[raw[0]](raw[1:])
            except Exception:
                blob = None          # the bundled uninstaller is not needed
            if blob is not None and len(blob) == size:
                dst = out / name.replace("\\", "/")
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(blob)
                written.append(name)
        p += n
    exe = out / "lf2.exe"
    ok = exe.exists() and hashlib.sha256(exe.read_bytes()).hexdigest() == LF2_SHA
    return written, ok


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    files, ok = extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
    print("extracted %d files; lf2.exe %s" % (len(files), "matches" if ok else "DOES NOT match"))
