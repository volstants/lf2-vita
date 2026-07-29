#!/usr/bin/env python3
"""Convert LF2's BMPs (many are RLE8-compressed, which Pillow can't read) to PNG.

Usage:  python3 tools/bmp2png.py assets NAME [NAME...]
        python3 tools/bmp2png.py assets @listfile     (skips existing PNGs)
Reads assets/NAME.bmp, writes assets/NAME.png preserving pixels 1:1 (the black
or magenta background is kept — the engine color-keys at load time).

Output is 8-bit PALETTE PNG, like the source BMPs. Every LF2 sheet has <= 256
colours, so this is lossless, and it more than halves the VPK (16.4 MB of sheets
became 6.9 MB) — which is what the install time on the Vita is proportional to.
"""
import struct, os, sys
from PIL import Image

def load_bmp(path):
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', f"{path}: not a BMP"
    dataOff, = struct.unpack('<I', d[10:14])
    hdrSz,   = struct.unpack('<I', d[14:18])
    w, h     = struct.unpack('<ii', d[18:26])
    bpp,     = struct.unpack('<H', d[28:30])
    comp,    = struct.unpack('<I', d[30:34])
    topdown = h < 0; h = abs(h)
    if bpp == 24 and comp == 0:
        stride = (w * 3 + 3) & ~3
        rows = []
        for y in range(h):
            row = d[dataOff + y*stride : dataOff + y*stride + w*3]
            rows.append(bytes(row[i+2:i+3] + row[i+1:i+2] + row[i:i+1]
                              for i in range(0, w*3, 3)))
        if not topdown: rows.reverse()
        return Image.frombytes('RGB', (w, h), b''.join(b''.join([r]) for r in rows))
    ncol, = struct.unpack('<I', d[46:50])
    if ncol == 0: ncol = 1 << bpp
    palOff = 14 + hdrSz
    pal = []
    for i in range(ncol):
        b, g, r = d[palOff+i*4], d[palOff+i*4+1], d[palOff+i*4+2]
        pal += [r, g, b]
    pal += [0, 0, 0] * (256 - ncol)
    idx = bytearray(w * h)                       # top-down index buffer
    if comp == 0 and bpp == 8:
        stride = (w + 3) & ~3
        for y in range(h):
            yy = y if topdown else h - 1 - y
            idx[yy*w:(yy+1)*w] = d[dataOff + y*stride : dataOff + y*stride + w]
    elif comp == 1 and bpp == 8:                 # RLE8, bottom-up rows
        i = dataOff; x = 0; y = 0
        while i < len(d) - 1:
            b0 = d[i]; b1 = d[i+1]; i += 2
            if b0 > 0:
                yy = y if topdown else h - 1 - y
                if 0 <= yy < h:
                    end = min(x + b0, w)
                    if x < w: idx[yy*w + x : yy*w + end] = bytes([b1]) * (end - x)
                x += b0
            else:
                if b1 == 0: x = 0; y += 1
                elif b1 == 1: break
                elif b1 == 2: x += d[i]; y += d[i+1]; i += 2
                else:
                    yy = y if topdown else h - 1 - y
                    take = d[i:i+b1]
                    if 0 <= yy < h and x < w:
                        end = min(x + b1, w)
                        idx[yy*w + x : yy*w + end] = take[:end - x]
                    x += b1; i += b1
                    if b1 % 2: i += 1            # word padding
    else:
        raise ValueError(f"{path}: bpp={bpp} comp={comp} unsupported")
    im = Image.frombytes('P', (w, h), bytes(idx))
    im.putpalette(pal)
    return im.convert('RGB')

def main():
    folder = sys.argv[1]
    names = []
    for a in sys.argv[2:]:
        if a.startswith('@'):
            names += [l.strip() for l in open(a[1:]) if l.strip()]
        else:
            names.append(a)
    fail = 0
    for n in names:
        src = os.path.join(folder, n + ".bmp")
        dst = os.path.join(folder, n + ".png")
        if os.path.exists(dst):
            print(f"skip {n}.png (existe)"); continue
        if not os.path.exists(src):
            print(f"MISS {n}.bmp"); fail += 1; continue
        try:
            im = load_bmp(src)
            # 8-bit palette out: the LF2 sheets never exceed 256 colours, so the
            # quantisation is exact, and the VPK ends up under half the size.
            if im.mode == "RGB":
                im = im.convert("P", palette=Image.ADAPTIVE, colors=256)
            im.save(dst, "PNG", optimize=True)
            print(f"ok   {n}.bmp {im.size[0]}x{im.size[1]} -> {n}.png")
        except Exception as e:
            print(f"FAIL {n}.bmp: {e}"); fail += 1
    sys.exit(1 if fail else 0)

if __name__ == "__main__":
    main()
