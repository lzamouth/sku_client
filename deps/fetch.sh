#!/bin/bash
# Download and unpack the third-party sources (pinned versions) into deps/src.
set -e
cd "$(dirname "$0")"

SDL_VER=3.4.16
CURL_VER=8.22.0
MBEDTLS_VER=3.6.7
ZLIB_VER=1.3.1
NUKLEAR_TAG=v4.13.3

get() { # url  file
    [ -f "dl/$2" ] || curl -fL --retry 3 -o "dl/$2" "$1"
}

get "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz" "SDL3-$SDL_VER.tar.gz"
get "https://github.com/curl/curl/releases/download/curl-${CURL_VER//./_}/curl-$CURL_VER.tar.xz" "curl-$CURL_VER.tar.xz"
get "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VER/mbedtls-$MBEDTLS_VER.tar.bz2" "mbedtls-$MBEDTLS_VER.tar.bz2"
get "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.xz" "zlib-$ZLIB_VER.tar.xz"
get "https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/$NUKLEAR_TAG/nuklear.h" "nuklear.h"
get "https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/$NUKLEAR_TAG/demo/sdl3_renderer/nuklear_sdl3_renderer.h" "nuklear_sdl3_renderer.h"
get "https://curl.se/ca/cacert.pem" "cacert.pem"
# static AppImage runtime (type 2): the AppImage is this file + a squashfs of the AppDir
mkdir -p tools
[ -f tools/runtime-x86_64 ] || curl -fL --retry 3 -o tools/runtime-x86_64 "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
chmod +x tools/runtime-x86_64
get "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2" "dejavu-fonts-ttf-2.37.tar.bz2"

mkdir -p src
[ -d "src/SDL3-$SDL_VER" ]       || tar xzf "dl/SDL3-$SDL_VER.tar.gz"       -C src
[ -d "src/curl-$CURL_VER" ]      || tar xJf "dl/curl-$CURL_VER.tar.xz"      -C src
[ -d "src/mbedtls-$MBEDTLS_VER" ]|| tar xjf "dl/mbedtls-$MBEDTLS_VER.tar.bz2" -C src
[ -d "src/zlib-$ZLIB_VER" ]      || tar xJf "dl/zlib-$ZLIB_VER.tar.xz"      -C src
ln -sfn "SDL3-$SDL_VER"       src/sdl
ln -sfn "curl-$CURL_VER"      src/curl
ln -sfn "mbedtls-$MBEDTLS_VER" src/mbedtls
ln -sfn "zlib-$ZLIB_VER"      src/zlib
mkdir -p src/nuklear && cp dl/nuklear.h dl/nuklear_sdl3_renderer.h src/nuklear/
mkdir -p src/fonts && tar xjf dl/dejavu-fonts-ttf-2.37.tar.bz2 -C src/fonts --strip-components=1 \
    dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf dejavu-fonts-ttf-2.37/LICENSE
ls -la dl src
