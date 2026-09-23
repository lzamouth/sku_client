#!/bin/bash
# Verify the delivery constraints on a built binary.
# Usage: check.sh linux|windows|macos <binary>
set -e
TARGET="$1"; BIN="$2"
OSX=/usr/local/osxcross/bin
echo "== $BIN"; ls -l "$BIN" | awk '{print $5 " bytes"}'; file "$BIN"
case "$TARGET" in
linux)
    echo "-- max GLIBC symbol version (must be <= 2.35):"
    objdump -T "$BIN" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1
    echo "-- NEEDED shared libraries:"
    objdump -p "$BIN" | awk '/NEEDED/ {print "   " $2}'
    ;;
windows)
    echo "-- imported DLLs (must all be Windows system DLLs):"
    x86_64-w64-mingw32-objdump -p "$BIN" | awk '/DLL Name/ {print "   " $3}'
    ;;
macos)
    echo "-- architectures:"; $OSX/lipo -info "$BIN"
    for arch in x86_64 arm64; do
        echo "-- $arch: min OS / SDK"
        $OSX/x86_64-apple-darwin20.4-otool -arch $arch -l "$BIN" | grep -A3 LC_BUILD_VERSION | grep -E 'minos|sdk'
    done
    echo "-- linked dylibs:"
    $OSX/x86_64-apple-darwin20.4-otool -arch arm64 -L "$BIN" | tail -n +2 | awk '{print "   " $1}'
    ;;
esac
