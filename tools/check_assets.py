#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
#  check_assets.py — spritesheet integrity scan (run via `make -f Makefile.host
#  check-assets`). Meant to run whenever new character .bmp enter the repo, so a
#  damaged sheet is caught by AUDIT, not by a visual bug three sessions later.
#
#  The lesson that motivated this: "decodifica sem erro" ≠ "conteúdo correto".
#  So this does NOT stop at "does it decode" — it classifies each sheet:
#
#    OK              decodes clean, content fills the header height
#    glitch-final    RLE stream errors at the very end, but every row still has
#                    content (benign — ImageMagick recovers it; reconvert to PNG)
#    TRUNCADO        content stops before the header height → real pixel loss
#    curto           shorter than 560 px by design (fewer pics) — not a defect
#    ILEGIVEL        no recoverable image at all
#
#  Requires ImageMagick (`convert`) — PIL mis-decodes some LF2 RLE8 silently — and
#  Pillow for sampling. Degrades to a warning if either is missing.
# ─────────────────────────────────────────────────────────────────────────────
import os, sys, glob, struct, subprocess, tempfile

ASSETS = "assets"
BG = {(0, 0, 0), (255, 0, 128)}          # black / magenta transparent keys

def have(cmd):
    return subprocess.run(["which", cmd], capture_output=True).returncode == 0

def bmp_header(path):
    d = open(path, "rb").read(26)
    w, h = struct.unpack("<ii", d[18:26])
    return w, abs(h)

def rows_with_content(im):
    from PIL import Image
    W, H = im.size
    last = -1
    for r in range(H // 80):
        crop = im.crop((0, r * 80, W, r * 80 + 80))
        nb = sum(n for c, n in _counter(crop).items() if c not in BG)
        if nb > 200:
            last = r
    return last, H // 80

def _counter(im):
    from collections import Counter
    return Counter(im.convert("RGB").getdata())

def classify(path):
    from PIL import Image
    w, h = bmp_header(path)
    tmp = os.path.join(tempfile.gettempdir(), "ck_" + os.path.basename(path) + ".png")
    res = subprocess.run(["convert", path, tmp], capture_output=True, text=True)
    decode_err = bool(res.stderr.strip())
    if not os.path.exists(tmp):
        return "ILEGIVEL", f"{w}x{h}", ""
    im = Image.open(tmp)
    last, nrows = rows_with_content(im)
    short = h < 560
    if last < 0:
        return "ILEGIVEL", f"{w}x{h}", "sem conteudo"
    if last < nrows - 1:                       # content stops early → real loss
        return "TRUNCADO", f"{w}x{h}", f"conteudo ate linha {last}/{nrows-1}"
    if decode_err:
        return "glitch-final", f"{w}x{h}", "stream RLE erra no fim, conteudo integro"
    return ("curto" if short else "OK"), f"{w}x{h}", ("menos pics (por design)" if short else "")

def main():
    if not have("convert"):
        print("check-assets: ImageMagick (`convert`) ausente — pulei o scan.\n"
              "  instale (ex.: sudo apt install imagemagick) para habilitar.")
        return 0
    try:
        import PIL  # noqa
    except ImportError:
        print("check-assets: Pillow ausente — pulei o scan (pip install pillow).")
        return 0

    sheets = sorted(f for f in glob.glob(os.path.join(ASSETS, "*_[012].bmp"))
                    if "mirror" not in f)
    problems = 0
    print(f"{'FOLHA':22s} {'STATUS':13s} {'DIMS':10s} OBS")
    for p in sheets:
        status, dims, obs = classify(p)
        if status in ("OK", "curto"):
            continue                          # only print anomalies
        problems += 1
        print(f"{os.path.basename(p):22s} {status:13s} {dims:10s} {obs}")
    print(f"--- {len(sheets)} folhas escaneadas; {problems} anomalias "
          f"(OK/curto omitidos). 'glitch-final' = benigno; 'TRUNCADO'/'ILEGIVEL' = perda real. ---")
    # Non-zero exit only for REAL loss, so CI can gate on it without failing on benign glitches.
    return 1 if any(classify(p)[0] in ("TRUNCADO", "ILEGIVEL") for p in sheets) else 0

if __name__ == "__main__":
    sys.exit(main())
