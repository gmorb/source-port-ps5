#!/usr/bin/env bash
# Full pipeline: engine objects -> static modules -> native PS5 folder app.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
source "$here/env.sh"
static=$root/build-static

[[ ${SKIP_ENGINE:-0} == 1 ]] || "$here/build-engine.sh"
[[ ${SKIP_COLLECT:-0} == 1 ]] || python3 "$here/collect-links.py" "$root/build-links.json"
python3 "$here/staticlink.py" "$root/build-links.json" "$static"

flags=(-O2 -march=znver2 -fno-omit-frame-pointer)
prospero-clang++ "${flags[@]}" -c "$here/runtime/ps5_dl.cpp" -o "$static/ps5_dl.o"
prospero-clang++ "${flags[@]}" -c "$here/runtime/ps5_main.cpp" -o "$static/ps5_main.o"
prospero-clang++ "${flags[@]}" -I"$PS5_OPENGL_PREFIX/include" -c "$here/runtime/ps5_gltrace.cpp" -o "$static/ps5_gltrace.o"
prospero-clang "${flags[@]}" -c "$static/ps5_static_table.c" -o "$static/ps5_static_table.o"
# Static constructors run in link order. Shared libraries are initialized
# before the modules that depend on them, so link the "global" libraries first.
first=(tier0 steam_api vstdlib togl)
mods=()
for m in "${first[@]}"; do mods+=("$static/modules/$m.o"); done
for f in "$static"/modules/*.o; do
    m=$(basename "$f" .o)
    [[ " ${first[*]} " == *" $m "* ]] || mods+=("$f")
done
ld.lld -r -o "$static/zmr_engine.o" "${mods[@]}" "$static/ps5_main.o" "$static/ps5_gltrace.o" "$static/ps5_dl.o" \
    "$static/ps5_static_table.o"
"$here/package-app.sh"
