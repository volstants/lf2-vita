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
#
# COPY, don't symlink into site-packages. This used to link straight at the
# wheel's install path, which on a sandboxed host is under a per-session
# directory: the next session found .sdlhost/lib full of dangling links and
# `make harness` died with "cannot find -lSDL2-2.0" while the headers — the only
# thing the Makefile checked — were still there. A local copy costs ~10 MB in a
# gitignored folder and survives.
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
if [ -n "$DLL" ] && [ -n "${DLL:-}" ] && [ -d "$DLL" ]; then
    # UNLINK EACH DESTINATION FIRST. `cp -f` does NOT overwrite a dangling
    # symlink: it refuses with "not writing through dangling symlink" and, under
    # `set -e`, kills the script before a single lib is copied. That is precisely
    # the state this script is called to REPAIR — the Makefile guard fires
    # because .sdlhost/lib is full of dead links into a previous session's pip
    # prefix — so the recovery path has to work from the broken state, not only
    # from the empty one. `rm -f` removes the LINK (it never follows it), which
    # is what we want; `cp --remove-destination` would do the same but is a GNU
    # extension. Note `rm -f` succeeds silently on a dangling link but NOT on a
    # read-only mount, hence the explicit check below.
    for f in "$DLL"/*.so "$DLL"/*.so.*; do
        [ -e "$f" ] || continue
        dst="$DEST/lib/$(basename "$f")"
        rm -f "$dst" 2>/dev/null || true
        if [ -L "$dst" ] || { [ -e "$dst" ] && [ ! -w "$dst" ]; }; then
            echo "host_sdl.sh: cannot replace $dst (stale link on a read-only or" >&2
            echo "             restricted mount). Delete .sdlhost/lib by hand and" >&2
            echo "             re-run, or move the repo off that mount." >&2
            exit 1
        fi
        cp -f "$f" "$dst"
    done
    # soname aliases (libFoo.so → libFoo.so.0), relative so the folder is movable
    ( cd "$DEST/lib" && for f in *.so; do rm -f "${f%.so}.so.0"; ln -sf "$f" "${f%.so}.so.0"; done )
    echo "SDL2 runtime libs copied into $DEST/lib"
fi

# Fail loudly rather than leaving a half-built .sdlhost the Makefile will trust.
for want in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0; do
    [ -r "$DEST/lib/$want" ] || { echo "host_sdl.sh: $want missing/unreadable" >&2; exit 1; }
done
