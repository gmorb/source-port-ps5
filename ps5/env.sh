# Shared build environment for the Source Port PS5 build.
# Override SRC to point at the folder holding the upstream checkouts (see docs/BUILDING.md).
_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export SRC=${SRC:-$(cd "$_here/../.." && pwd)/src}
export PS5_NATIVE_APP_TEMPLATE=$SRC/ps5-native-app-boilerplate
export PS5_PAYLOAD_SDK=$PS5_NATIVE_APP_TEMPLATE/.deps/native/ps5-payload-sdk
export PACBREW=$PS5_NATIVE_APP_TEMPLATE/.deps/pacbrew/v0.40.2/sysroot/user/homebrew
export PS5_OPENGL_PREFIX=${PS5_OPENGL_PREFIX:-$SRC/ps5-opengl/build/sdk/ps5-opengl-gl46}
export PATH=$PS5_PAYLOAD_SDK/bin:$PATH
export PKG_CONFIG_LIBDIR=$PACBREW/lib/pkgconfig:$PACBREW/libdata/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$PS5_NATIVE_APP_TEMPLATE/.deps/pacbrew/v0.40.2/sysroot
# waf picks compilers from these; set only for engine builds (see build-engine.sh)
export PS5_CC=prospero-clang PS5_CXX=prospero-clang++ PS5_AR=prospero-ar
export PATH=$_here/bin:$PATH PKG_CONFIG=$_here/bin/pkg-config
export PS5_COMPAT_INCLUDE=$_here/include
