#!/usr/bin/env bash
# Link the statically combined engine into a native PS5 folder app.
# Mirrors ps5-opengl's tools/build-native-test-app.sh (same runtime, heap,
# allocator wrapping and AGC import stubs), with our own objects and metadata.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/env.sh"
unset CC CXX AR
# The committed assets/sce_sys/param.json is the source of truth for the title ID,
# name and contentVersion (the homebrew catalog reads it from the repository).
param_src=$here/../assets/sce_sys/param.json
if [[ -f $param_src ]]; then
    TITLE_ID=${TITLE_ID:-$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['titleId'])" "$param_src")}
fi
TITLE_ID=${TITLE_ID:-PPSA20040}
TITLE_NAME=${TITLE_NAME:-Source Port (alpha)}
static=${STATIC_DIR:-$here/../build-static}
gl=$SRC/ps5-opengl
sdl=$gl/build/sdl-native/sdk
template=$PS5_NATIVE_APP_TEMPLATE
sdk=$PS5_PAYLOAD_SDK
app=$here/../build-app/$TITLE_ID
rt=$(clang-18 --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a

mkdir -p "$app"
cp "$template/Makefile" "$app/Makefile"
for d in assets runtime sce_sys tooling tools; do
    mkdir -p "$app/$d"; cp -a "$template/$d/." "$app/$d/"
done
# Our launcher art and music override the template's.
if [[ -d "$here/../assets/sce_sys" ]]; then
    cp -a "$here/../assets/sce_sys/." "$app/sce_sys/"
fi
# Heap: the template's default is unlimited; ps5-opengl pins 256 MiB for the
# GPU-side module heap, we keep its value (CPU heap is app_heap.c below).
heap_source="$app/tooling/native/sce_module_writer.cpp"
sed -i 's/write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());/write_u64(result.data, result.heap_size, 0x10000000ULL);/' "$heap_source"
grep -q "0x10000000ULL" "$heap_source"
link_script="$app/tools/build.sh"
OVERLAY_WRAPS=""
for f in open fopen freopen stat lstat access mkdir unlink remove rmdir chmod rename realpath opendir readdir closedir chdir getcwd SDL_GL_SwapWindow recvfrom exit _Exit abort SDL_SetThreadPriority sceAudioOutOutput sceAudioOutOpen pthread_mutex_init pthread_mutex_destroy pthread_cond_init pthread_cond_destroy pthread_mutexattr_init pthread_mutexattr_destroy; do
    OVERLAY_WRAPS+="--wrap=$f "
done
if ! grep -q -- "--wrap=malloc" "$link_script"; then
    sed -i "s/--eh-frame-hdr \\\\/--eh-frame-hdr --error-limit=0 --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size ${OVERLAY_WRAPS}\\\\/" "$link_script"
fi
cp "$template/tooling/native/ps5-pie.ld" "$app/tooling/native/ps5-pie-base.ld"
cp "$gl/native-app/ps5-pie.ld" "$app/tooling/native/ps5-pie.ld"
cp "$gl/native-app/app-symbols.map" "$app/tooling/native/app-symbols.map"
mkdir -p "$app/assets/fonts"
cp "$gl/third_party/imgui/misc/fonts/DroidSans.ttf" "$gl/third_party/imgui/misc/fonts/Cousine-Regular.ttf" "$app/assets/fonts/"
rm -rf -- "$app/src" "$app/include" "$app/vendor"
mkdir -p "$app/src" "$app/include" "$app/vendor"
cp "$gl/native-app/runtime_shims.c" "$app/src/runtime_shims.c"
cp "$here/runtime/zmr_app_heap.c" "$app/src/app_heap.c"   # ps5-opengl native-app/app_heap.c + optional lock
cp "$here/runtime/zmr_platform.c" "$here/runtime/zmr_libc_compat.c" "$here/runtime/zmr_fs_overlay.c" "$here/runtime/zmr_memdiag.c" "$here/runtime/zmr_hid.c" "$here/runtime/zmr_commands.c" "$here/runtime/zmr_audio.c" "$app/src/"

# Metadata
if [[ -f $param_src ]]; then
    cp "$param_src" "$app/sce_sys/param.json"
else
cp "$gl/native-app/param.json" "$app/sce_sys/param.json"
python3 "$gl/tools/native-display-metadata.py" "$app/sce_sys/param.json" --fps 60
python3 - "$app/sce_sys/param.json" "$TITLE_ID" "$TITLE_NAME" <<'PY'
import json, sys
from pathlib import Path
p = Path(sys.argv[1]); m = json.loads(p.read_text())
tid, name = sys.argv[2], sys.argv[3]
m["titleId"] = tid
m["conceptId"] = tid[4:]
m["contentId"] = f"UP9000-{tid}_00-ZOMBIEMASTERPS50"
m["localizedParameters"]["en-US"]["titleName"] = name
m["downloadDataSize"] = 256  # MiB (value validated by ps5-opengl) of writable /download0 (configs, saves, logs)
p.write_text(json.dumps(m, indent=2) + "\n")
PY
fi

pb=$PACBREW/lib
group="$app/vendor/libzmr_group.a"
{
    printf 'SEARCH_DIR("%s")\n' "$sdk/target/lib"
    printf 'GROUP (\n'
    for f in "$static/zmr_engine.o" "$sdl/lib/libSDL2.a" "$PS5_OPENGL_PREFIX/lib/libPS5OpenGLCore33.a" \
             "$pb/libfontconfig.a" "$pb/libexpat.a" "$pb/libfreetype.a" "$pb/libpng16.a" \
             "$pb/libjpeg.a" "$pb/libbz2.a" "$pb/libz.a" "$pb/libopenal.a" "$pb/libiconv.a" \
             "$pb/libcurl.a" "$pb/libssl.a" "$pb/libcrypto.a" "$pb/libpsl.a" "$pb/libzstd.a" \
             "$sdk/target/lib/libunwind.a" "$sdk/target/lib/libc++abi.a" "$sdk/target/lib/libc++.a" "$rt"; do
        test -s "$f" || { echo "missing $f" >&2; exit 2; }
        printf '  "%s"\n' "$f"
    done
    printf ')\n'
} > "$group"
printf 'APP_STATIC_ARCHIVES = vendor/libzmr_group.a\nTITLE_ID = %s\n' "$TITLE_ID" > "$app/.env"

if [[ ! -x "$app/.deps/native/ps5-payload-sdk/bin/prospero-lld" ]]; then
    mkdir -p "$app/.deps"; cp -a "$template/.deps/native" "$app/.deps/native"
fi
app_sdk="$app/.deps/native/ps5-payload-sdk"
mkdir -p "$app/build/native-imports"
for stub in agc_link_stub:libSceAgc agc_driver_link_stub:libSceAgcDriver; do
    src=${stub%%:*}; lib=${stub##*:}
    PS5_PAYLOAD_SDK="$app_sdk" sh "$app/tooling/prospero-clang18" -std=c11 -O2 -fPIC \
        -c "$gl/native-app/$src.c" -o "$app/build/native-imports/$src.o"
    "$app_sdk/bin/prospero-lld" --shared -soname "$lib.prx" -o "$app_sdk/target/lib/$lib.so" \
        "$app/build/native-imports/$src.o"
done
make -C "$app" --no-print-directory app
test -s "$app/dist/$TITLE_ID/eboot.bin"
echo "App folder: $app/dist/$TITLE_ID"
