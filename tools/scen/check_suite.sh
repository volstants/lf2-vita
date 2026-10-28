#!/bin/bash
# Acceptance suite (see tools/scen/check_suite.py for the rules).
#   tools/scen/check_suite.sh <game_dir> [jobs]
#   LF2_BIN=bin/other-build tools/scen/check_suite.sh <game_dir>
# Exit status 0 only if every scenario ran to the end with exit status 0 and
# all its hashes equal the original's.
set -u
[ $# -ge 1 ] || { echo "usage: check_suite.sh <game_dir> [jobs]"; exit 2; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
exec python3 "$ROOT/tools/scen/check_suite.py" "$1" --jobs "${2:-2}"
