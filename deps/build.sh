#!/bin/bash
# Build the third-party libraries (static) for one target.
# Usage: deps/build.sh linux|windows|macos
# Output: deps/install/<target>/{include,lib}
# Linux must be run inside the Ubuntu 22.04 container: docker/run.sh deps/build.sh linux
set -e
TARGET="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/deps/src"
PREFIX="$ROOT/deps/install/$TARGET"
BUILD="$ROOT/deps/build/$TARGET"
JOBS=$(nproc)
mkdir -p "$PREFIX" "$BUILD"

case "$TARGET" in
linux)
    if [ ! -f /.dockerenv ]; then echo "linux deps must be built in the container: docker/run.sh deps/build.sh linux" >&2; exit 1; fi
    TOOLCHAIN=""
    TLS_OPTS="-DCURL_USE_MBEDTLS=ON"
    ;;
windows)
    TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake"
    TLS_OPTS="-DCURL_USE_SCHANNEL=ON"
    ;;
macos)
    TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$ROOT/cmake/toolchains/osxcross-universal.cmake"
    TLS_OPTS="-DCURL_USE_MBEDTLS=ON -DCURL_USE_SECTRANSP=OFF -DUSE_APPLE_IDN=OFF"
    ;;
*) echo "usage: $0 linux|windows|macos" >&2; exit 1 ;;
esac

COMMON="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX -DSKU_DEPS_PREFIX=$PREFIX $TOOLCHAIN"

cm() { # name  srcdir  options...
    local name="$1" src="$2"; shift 2
    echo "=== $name ($TARGET)"
    cmake -S "$src" -B "$BUILD/$name" $COMMON "$@" > "$BUILD/$name.configure.log" 2>&1 || { tail -40 "$BUILD/$name.configure.log"; exit 1; }
    cmake --build "$BUILD/$name" -j "$JOBS" > "$BUILD/$name.build.log" 2>&1 || { tail -40 "$BUILD/$name.build.log"; exit 1; }
    cmake --install "$BUILD/$name" > "$BUILD/$name.install.log" 2>&1
}

# --- zlib (static only; its CMake always builds a shared lib too, so use its own build files)
echo "=== zlib ($TARGET)"
rm -rf "$BUILD/zlib"; cp -r "$SRC/zlib/" "$BUILD/zlib"
if [ "$TARGET" = windows ]; then
    make -C "$BUILD/zlib" -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- \
        BINARY_PATH="$PREFIX/bin" INCLUDE_PATH="$PREFIX/include" LIBRARY_PATH="$PREFIX/lib" install > "$BUILD/zlib.build.log" 2>&1 || { tail -20 "$BUILD/zlib.build.log"; exit 1; }
else
    if [ "$TARGET" = macos ]; then
        ZCC=/usr/local/osxcross/bin/o64-clang
        ZFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.15"
        ZAR=/usr/local/osxcross/bin/x86_64-apple-darwin20.4-ar
    else
        ZCC=cc; ZFLAGS="-O2"; ZAR=ar
    fi
    (cd "$BUILD/zlib" && CC=$ZCC CFLAGS="$ZFLAGS" LDFLAGS="$ZFLAGS" AR="$ZAR" ./configure --static --prefix="$PREFIX" && make -j"$JOBS" install) > "$BUILD/zlib.build.log" 2>&1 || { tail -20 "$BUILD/zlib.build.log"; exit 1; }
fi

# --- mbedTLS (Linux and macOS only; Windows uses Schannel)
# Used from several threads at once (parallel downloads and checks): TLS 1.3 goes through PSA Crypto,
# whose key store is global and needs the threading layer, or concurrent handshakes corrupt the heap
# (4.2.5 crash on macOS). Set in the configuration header itself, so that curl, built next against the
# installed headers, sees the same structures.
if [ "$TARGET" != windows ]; then
    for opt in MBEDTLS_THREADING_C MBEDTLS_THREADING_PTHREAD; do
        python3 "$SRC/mbedtls/scripts/config.py" -f "$SRC/mbedtls/include/mbedtls/mbedtls_config.h" set $opt
    done
    rm -rf "$BUILD/mbedtls" "$BUILD/curl"
    cm mbedtls "$SRC/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DMBEDTLS_FATAL_WARNINGS=OFF \
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DGEN_FILES=OFF
fi

# --- curl (static, HTTP/HTTPS only)
cm curl "$SRC/curl" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DHTTP_ONLY=ON -DCURL_DISABLE_LDAP=ON -DENABLE_UNIX_SOCKETS=OFF \
    -DCURL_USE_LIBPSL=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_GSSAPI=OFF \
    -DCURL_ZLIB=ON -DZLIB_ROOT="$PREFIX" -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF \
    -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_CA_FALLBACK=OFF \
    -DCURL_ENABLE_SSL=ON $TLS_OPTS

# --- SDL3 (static; video, events, render, dialogs; no audio/joystick/etc.)
cm sdl "$SRC/sdl" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
    -DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_CAMERA=OFF -DSDL_POWER=OFF -DSDL_HIDAPI=OFF -DSDL_GPU=OFF \
    -DSDL_VIDEO=ON -DSDL_RENDER=ON -DSDL_DIALOG=ON

echo "=== done: $PREFIX"
ls "$PREFIX/lib"
