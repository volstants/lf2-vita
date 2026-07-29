#!/usr/bin/env bash
# Fetch the REAL SDL2 headers for host compile-checking main.cpp.
#
# No root needed: `apt-get download` only fetches the .deb, and `dpkg-deb -x`
# unpacks it anywhere. Use this instead of hand-written ABI stubs — a stub that
# gets SDL_Surface's layout or SDL_Event's size wrong does not fail to compile,
# it corrupts memory at runtime, and you end up debugging the harness.
#
#   ./tools/host_sdl.sh          # downloads to .sdlhost/ (gitignored)
#   make -f Makefile.host check-main
#
# Note on the include style: this project uses `#include <SDL2/SDL.h>`, which is
# what vitasdk expects. Ubuntu also ships SDL_config.h as a shim that pulls in
# <SDL2/_real_SDL_config.h> from the arch dir, so BOTH include roots are needed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/.sdlhost"

mkdir -p "$DEST"
cd "$DEST"

# ── Headers ──────────────────────────────────────────────────────────────────
if [ -d "root/usr/include/SDL2" ]; then
    echo "SDL2 headers already in $DEST"
else
    apt-get download libsdl2-dev libsdl2-image-dev
    for d in *.deb; do dpkg-deb -x "$d" root; done

    # The Vita header main.cpp includes; only sceKernelExitProcess is referenced.
    mkdir -p root/usr/include/psp2/kernel
    cat > root/usr/include/psp2/kernel/processmgr.h <<'EOF'
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void sceKernelExitProcess(int res);
#ifdef __cplusplus
}
#endif
EOF
    echo "SDL2 headers ready: $(ls root/usr/include/SDL2 | wc -l) files in $DEST"
fi

# ── Runtime libs for the headless harness ────────────────────────────────────
# libSDL2 is usually already on the system; libSDL2_image usually is not. The
# pysdl2-dll wheel ships both as plain .so files, but without the soname
# symlinks (libFoo.so.0) that the linker records, so we create those.
mkdir -p "$DEST/lib"
if ! ldconfig -p 2>/dev/null | grep -q libSDL2_image; then
    pip install -q pysdl2-dll --break-system-packages 2>/dev/null || pip install -q pysdl2-dll
fi
DLL="$(python3 - <<'EOF' 2>/dev/null || true
import os
try:
    import sdl2dll
    print(os.path.join(os.path.dirname(sdl2dll.__file__), "dll"))
except Exception:
    pass
EOF
)"
if [ -n "$DLL" ] && [ -d "$DLL" ]; then
    for f in "$DLL"/*.so; do
        b="$(basename "$f" .so)"
        ln -sf "$f" "$DEST/lib/$b.so"
        ln -sf "$f" "$DEST/lib/$b.so.0"
    done
    echo "SDL2 runtime libs linked into $DEST/lib"
fi
