#!/usr/bin/env python3
"""Extrai os bitmaps de UI embutidos no .rsrc do lf2.exe.

Uso:  python3 tools/rsrc_extract.py reference/decomp/lf2.exe assets

Por que existe: a arte de menu/HUD do original (CHARMENU, BATTLEMODE,
MENU_BACK1-13, SCORE_BOARD, cursores, ENDING) nao esta em sprite\\sys\\ — mora
como RT_BITMAP dentro do executavel. Sao 64 recursos, ~3 MB, 8bpp indexado.
Isso as torna extraiveis SEM a instalacao do jogo, so' com o .exe.

O recurso RT_BITMAP e' um DIB *sem* o BITMAPFILEHEADER de 14 bytes — o Windows
o reconstroi ao carregar. Escrever o blob cru num .bmp produz arquivo invalido;
este script reconstroi o header, calculando o offset dos pixels a partir do
tamanho do BITMAPINFOHEADER e do numero de cores da paleta.

COPYRIGHT: a arte e' (c) Marti Wong / Starsky Wong, como os sprites. A saida vai
para assets/, que o .gitignore exclui. Nao commitar.
"""
import struct, sys, os

def sections(d):
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vsz, va, rsz, ra = struct.unpack_from("<IIII", d, o + 8)
        out.append((name, va, rsz, ra))
    return out

def walk(d, base_raw, off, path, out):
    """Percorre a arvore de 3 niveis do diretorio de recursos (tipo/nome/lang)."""
    n_named, n_id = struct.unpack_from("<HH", d, off + 12)
    for i in range(n_named + n_id):
        e = off + 16 + i * 8
        nameoff, dataoff = struct.unpack_from("<II", d, e)
        if nameoff & 0x80000000:                      # entrada nomeada (string)
            no = base_raw + (nameoff & 0x7fffffff)
            ln = struct.unpack_from("<H", d, no)[0]
            nm = d[no + 2:no + 2 + ln * 2].decode("utf-16-le")
        else:
            nm = str(nameoff)                          # entrada por ID numerico
        if dataoff & 0x80000000:                       # ainda e' diretorio
            walk(d, base_raw, base_raw + (dataoff & 0x7fffffff), path + [nm], out)
        else:                                          # folha: DataEntry
            do = base_raw + dataoff
            rva, size = struct.unpack_from("<II", d, do)
            out.append((path + [nm], rva, size))

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    exe, dest = sys.argv[1], sys.argv[2]
    d = open(exe, "rb").read()
    rsrc = [s for s in sections(d) if s[0] == ".rsrc"]
    if not rsrc:
        print("sem secao .rsrc"); return 1
    _, base_va, _, base_raw = rsrc[0]
    res = []
    walk(d, base_raw, base_raw, [], res)

    os.makedirs(dest, exist_ok=True)
    n = skipped = 0
    for path, rva, size in res:
        if path[0] != "2":            # RT_BITMAP == 2; ignora cursor/manifest/etc
            skipped += 1; continue
        off = rva - base_va + base_raw
        dib = d[off:off + size]
        if len(dib) < 40:
            skipped += 1; continue
        hdrsz = struct.unpack_from("<I", dib, 0)[0]
        bpp   = struct.unpack_from("<H", dib, 14)[0]
        used  = struct.unpack_from("<I", dib, 32)[0]
        ncol  = used or ((1 << bpp) if bpp <= 8 else 0)
        off_bits = 14 + hdrsz + ncol * 4
        fh = b"BM" + struct.pack("<IHHI", 14 + len(dib), 0, 0, off_bits)
        name = path[1] if len(path) > 1 else path[0]
        safe = "".join(c if c.isalnum() or c in "_-" else "_" for c in name)
        open(os.path.join(dest, f"ui_{safe}.bmp"), "wb").write(fh + dib)
        n += 1
    print(f"{n} bitmaps extraidos para {dest}/ (ui_*.bmp), {skipped} recursos ignorados")
    print("Converta para PNG com: python3 tools/bmp2png.py", dest,
          "$(cd", dest, "&& ls ui_*.bmp | sed 's/\\.bmp$//')")
    return 0

if __name__ == "__main__":
    sys.exit(main())
