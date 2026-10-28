#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  build-vita.sh — build the Vita packages with VitaSDK
#
#    ./build-vita.sh          # configure + build (intermediates in .build/vita)
#    ./build-vita.sh clean    # wipe .build/vita first
#
#  Every build produces, in BUILD/ (VERSION = VITA_VERSION in CMakeLists.txt):
#    lf2-vita-v<VERSION>-completo.vpk   standalone: program + the game as app0:/lf2
#                                       (nothing to copy; ~680 files, installs slowly)
#    lf2-vita-v<VERSION>-programa.vpk   program only (~5 MB): the update package, for
#                                       a Vita that has the game in ux0:data/lf2/
#    copiar-para-ux0-data/lf2/          that game folder (created once, if missing)
#  Packages of other versions are removed from BUILD/ (BUILD/anteriores is kept).
#
#  Needs the user's LF2 v2.0a: LF2_EXE=<lf2.exe> (default reference/decomp/lf2.exe),
#  embedded in the program at build time, and LF2_GAME=<game folder with data, bg,
#  sprite, bgm/*.ogg> (default reference/lf2-game). Python with capstone + pefile:
#  LF2_PYTHON=... (default python3). Requires $VITASDK with SDL2, SDL2_mixer and codecs.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

if [ -z "${VITASDK:-}" ]; then
  echo "error: VITASDK is not set." >&2
  echo "  export VITASDK=/usr/local/vitasdk" >&2
  exit 1
fi
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$ROOT/.build/vita"
OUT="$ROOT/BUILD"
GAME="${LF2_GAME:-$ROOT/reference/lf2-game}"

[ -d "$GAME/data" ] || { echo "error: game folder not found: $GAME (set LF2_GAME)" >&2; exit 1; }
[ "${1:-}" = "clean" ] && rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"
cd "$WORK"

ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
[ -n "${LF2_EXE:-}" ] && ARGS+=("-DLF2_EXE=$LF2_EXE")
[ -n "${LF2_PYTHON:-}" ] && ARGS+=("-DLF2_PYTHON=$LF2_PYTHON")

# CMake 4 refuses projects declaring cmake_minimum_required < 3.5, which
# VitaSDK's bundled vita.cmake still does. Harmless to override.
cmake "$ROOT" "${ARGS[@]}"
make -j"$(nproc 2>/dev/null || echo 4)"

VER=$(sed -n 's/.*VITA_VERSION *"0*\([0-9]*\.[0-9]*\)".*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
PROG="lf2-vita-v$VER-programa.vpk"
FULL="lf2-vita-v$VER-completo.vpk"

# older packages out of the way (only this version's pair stays in BUILD/)
find "$OUT" -maxdepth 1 -name 'lf2-vita-v*.vpk' ! -name "$PROG" ! -name "$FULL" -delete

cp -f "$WORK/lf2-vita.vpk" "$OUT/$PROG"

# the game files the port reads (no lf2.exe: it is in the eboot; no .wma: the
# music is the .ogg next to it; no recordings: Playback Recording is not used)
mapfile -t FILES < <(cd "$GAME" && find data bg sprite bgm -type f \
    ! -iname '*.wma' ! -iname 'temporary.txt' | LC_ALL=C sort)

PACK=(-s "$WORK/lf2-vita.vpk_param.sfo" -b "$WORK/lf2-vita.self")
for f in sce_sys/icon0.png sce_sys/livearea/contents/bg.png \
         sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/template.xml; do
  PACK+=(-a "$ROOT/$f=$f")
done
for f in "${FILES[@]}"; do PACK+=(-a "$GAME/$f=lf2/$f"); done
vita-pack-vpk "${PACK[@]}" "$OUT/$FULL"

DATA="$OUT/copiar-para-ux0-data/lf2"
if [ ! -d "$DATA/data" ]; then
  mkdir -p "$DATA"
  (cd "$GAME" && for f in "${FILES[@]}"; do mkdir -p "$DATA/$(dirname "$f")"; cp -p "$f" "$DATA/$f"; done)
fi

echo
echo "built: $OUT/$FULL   (standalone, ${#FILES[@]} game files)"
echo "       $OUT/$PROG   (program only; game in ux0:data/lf2/)"
