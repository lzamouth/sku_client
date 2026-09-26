#!/bin/bash
# Configure and build the whole project for one target.
# Usage: build.sh linux|windows|macos [extra cmake -D options...]
#   e.g. build.sh windows -DSKU_BUILD_SPIKE=ON
# Linux must run in the container: docker/run.sh ./build.sh linux
set -e
TARGET="$1"; shift || true
ROOT="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$ROOT/deps/install/$TARGET"
OUT="$ROOT/build/$TARGET"

case "$TARGET" in
linux)   [ -f /.dockerenv ] || { echo "run inside the container: docker/run.sh ./build.sh linux" >&2; exit 1; }; TOOLCHAIN="" ;;
windows) TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake" ;;
macos)   TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$ROOT/cmake/toolchains/osxcross-universal.cmake" ;;
*) echo "usage: $0 linux|windows|macos [cmake options]" >&2; exit 1 ;;
esac

# development builds are stamped "<last changelog version>-dev.<commit>" unless a version is given
VERSION_OPT=""
case " $* " in *" -DSKU_VERSION="*) ;; *)
    VERSION_OPT="-DSKU_VERSION=$(head -1 "$ROOT/CHANGELOG.txt" | tr -d '\r')-dev.$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local)" ;;
esac

cmake -S "$ROOT" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DSKU_DEPS_PREFIX="$PREFIX" $TOOLCHAIN $VERSION_OPT "$@"
cmake --build "$OUT" -j "$(nproc)"
echo "=== built: $OUT"
