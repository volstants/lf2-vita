#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  build-vita.sh — build lf2-vita.vpk with VitaSDK
#
#    ./build-vita.sh          # configure + build into build/
#    ./build-vita.sh clean    # wipe build/ first
#
#  Requires $VITASDK to point at a VitaSDK install, plus these vdpm packages:
#      vdpm SDL2 SDL2_image libpng libwebp zlib libjpeg-turbo
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

if [ -z "${VITASDK:-}" ]; then
  echo "error: VITASDK is not set." >&2
  echo "  export VITASDK=/usr/local/vitasdk" >&2
  exit 1
fi
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

[ "${1:-}" = "clean" ] && rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

# CMake 4 refuses projects declaring cmake_minimum_required < 3.5, which
# VitaSDK's bundled vita.cmake still does. Harmless to override.
cmake "$ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

make -j"$(nproc 2>/dev/null || echo 4)"

echo
echo "built: $BUILD/lf2-vita.vpk"
echo "install on the Vita with VitaShell, or:  curl -T lf2-vita.vpk ftp://<vita-ip>:1337/ux0:/"
