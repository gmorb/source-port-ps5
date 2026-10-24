#!/usr/bin/env bash
# Configure + build nillerusr's source-engine for PS5 (objects/modules only).
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC/source-engine"
export CC=$PS5_CC CXX=$PS5_CXX AR=$PS5_AR
if [[ ${1:-} == configure || ! -f build/c4che/_cache.py ]]; then
    python3 waf configure -T release --prefix=/out --build-games=hl2 \
        --disable-warns --ps5 --use-ccache
fi
python3 waf build -k -j"$(nproc)"
