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
    # the commit, without git when building in the container: follow .git/HEAD by hand
    REV="${SKU_GIT_REV:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null)}"
    if [ -z "$REV" ] && [ -f "$ROOT/.git/HEAD" ]; then
        REF="$(sed -n 's/^ref: //p' "$ROOT/.git/HEAD")"
        if [ -n "$REF" ] && [ -f "$ROOT/.git/$REF" ]; then REV="$(cut -c1-7 "$ROOT/.git/$REF")"
        elif [ -n "$REF" ] && [ -f "$ROOT/.git/packed-refs" ]; then REV="$(grep " $REF\$" "$ROOT/.git/packed-refs" | cut -c1-7)"
        else REV="$(cut -c1-7 "$ROOT/.git/HEAD")"; fi
    fi
    VERSION_OPT="-DSKU_VERSION=$(head -1 "$ROOT/CHANGELOG.txt" | tr -d '\r')-dev.${REV:-local}" ;;
esac

cmake -S "$ROOT" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DSKU_DEPS_PREFIX="$PREFIX" $TOOLCHAIN $VERSION_OPT "$@"
cmake --build "$OUT" -j "$(nproc)"
echo "=== built: $OUT"
