#!/usr/bin/env bash
# Converte o audio original para o formato que o Vita toca.
#   tools/lf2_prepare_audio.sh <pasta-do-jogo-original>
# Efeitos: data/*.wav sao copiados como estao (PCM 16 bits, 44,1 kHz).
# Musica: bgm/*.wma (WMA v2, 64 kb/s) -> assets/bgm/*.ogg (Vorbis q3).
# O SDL_mixer do VitaSDK nao decodifica WMA; OGG e' o formato nativo dele.
set -euo pipefail
SRC="${1:?pasta do LF2 original (com data/ e bgm/)}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v ffmpeg >/dev/null || { echo "ffmpeg nao encontrado" >&2; exit 1; }
mkdir -p "$ROOT/assets/bgm" "$ROOT/data"
cp -n "$SRC"/data/*.wav "$ROOT/data/"
for f in "$SRC"/bgm/*.wma; do
  b="$(basename "${f%.wma}")"
  ffmpeg -loglevel error -y -i "$f" -c:a libvorbis -q:a 3 "$ROOT/assets/bgm/$b.ogg"
done
echo "ok: $(ls "$ROOT"/data/*.wav | wc -l) wav, $(ls "$ROOT"/assets/bgm/*.ogg | wc -l) ogg"
