#!/bin/bash
# Builds the three targets with a version number and assembles the deliverables in dist/<version>/:
#   SkunkcraftsUpdater-<v>.zip               the three GUI clients (drop-in-X-Plane mode) + README + notices
#   SkunkcraftsUpdater-<v>-windows-setup.exe NSIS installer
#   SkunkcraftsUpdater-<v>-macos.zip         SkunkcraftsUpdater.app
#   SkunkcraftsUpdater-<v>-x86_64.AppImage   Linux
#   sku-cli-<v>.zip                          command-line client for the three OS
# Usage: release.sh <version>   (e.g. 4.0.0 or 4.0.0-beta1)
set -e
VER="$1"
[ -n "$VER" ] || { echo "usage: $0 <version>" >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$ROOT/dist/$VER"
STAGE="$DIST/stage"
OSX=/usr/local/osxcross/bin
OTOOL=$OSX/x86_64-apple-darwin20.4-otool

echo "=== building $VER"
"$ROOT/docker/run.sh" ./build.sh linux   -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" > "$ROOT/build/release-linux.log" 2>&1
"$ROOT/build.sh" windows -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" > "$ROOT/build/release-windows.log" 2>&1
"$ROOT/build.sh" macos   -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" > "$ROOT/build/release-macos.log" 2>&1
"$ROOT/deps/licenses.sh" > /dev/null

rm -rf "$DIST"; mkdir -p "$STAGE"
sed "s/@VERSION@/$VER/" "$ROOT/packaging/README.txt" > "$STAGE/README.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.txt" "$STAGE/"
cp "$ROOT/packaging/icons/icon.ico" "$STAGE/"
cp "$ROOT/build/linux/src/app/SkunkcraftsUpdater_lin" "$STAGE/"
cp "$ROOT/build/windows/src/app/SkunkcraftsUpdater.exe" "$STAGE/"
cp "$ROOT/build/windows/src/cli/sku.exe" "$STAGE/"
x86_64-w64-mingw32-strip "$STAGE/SkunkcraftsUpdater.exe" "$STAGE/sku.exe"
strip "$STAGE/SkunkcraftsUpdater_lin"

echo "=== macOS bundle"
"$ROOT/macos/make_app.sh" "$ROOT/build/macos/src/app/SkunkcraftsUpdater" SkunkcraftsUpdater com.skunkcrafts.updater "$VER" "$STAGE" "$ROOT/packaging/icons/icon.icns"
mv "$STAGE/SkunkcraftsUpdater-macos.zip" "$DIST/SkunkcraftsUpdater-$VER-macos.zip"
# safety net: the arm64 slice must carry its ad-hoc signature
[ "$($OTOOL -arch arm64 -l "$STAGE/SkunkcraftsUpdater.app/Contents/MacOS/SkunkcraftsUpdater" | grep -c LC_CODE_SIGNATURE)" = 1 ] || { echo "arm64 slice is not signed" >&2; exit 1; }

echo "=== zip (drop-in-X-Plane mode, three OS)"
( cd "$STAGE" && zip -qry "$DIST/SkunkcraftsUpdater-$VER.zip" SkunkcraftsUpdater_lin SkunkcraftsUpdater.exe SkunkcraftsUpdater.app README.txt THIRD_PARTY_NOTICES.txt )

echo "=== Windows installer"
makensis -V2 -DVERSION="$VER" -DSTAGE="$STAGE" -DOUT="$DIST/SkunkcraftsUpdater-$VER-windows-setup.exe" "$ROOT/packaging/windows/installer.nsi"

echo "=== AppImage (static type-2 runtime + squashfs, no appimagetool needed)"
APPDIR="$DIST/AppDir"
rm -rf "$APPDIR"; mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/icons/hicolor/128x128/apps"
cp "$STAGE/SkunkcraftsUpdater_lin" "$APPDIR/usr/bin/"
cp "$ROOT/build/linux/src/cli/sku" "$APPDIR/usr/bin/" && strip "$APPDIR/usr/bin/sku"
cp "$ROOT/packaging/linux/SkunkcraftsUpdater.desktop" "$APPDIR/"
cp "$ROOT/packaging/icons/icon.png" "$APPDIR/skunkcrafts-updater.png"
cp "$ROOT/packaging/icons/icon.png" "$APPDIR/usr/share/icons/hicolor/128x128/apps/skunkcrafts-updater.png"
cp "$STAGE/README.txt" "$STAGE/THIRD_PARTY_NOTICES.txt" "$APPDIR/"
printf '#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\nexec "$HERE/usr/bin/SkunkcraftsUpdater_lin" "$@"\n' > "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
# mksquashfs lives in the build container; paths are the same inside (/work = repo root)
REL_APPDIR="${APPDIR#$ROOT/}"
"$ROOT/docker/run.sh" mksquashfs "$REL_APPDIR" "$REL_APPDIR.squashfs" -root-owned -noappend -comp zstd -Xcompression-level 19 -b 1M -quiet
cat "$ROOT/deps/tools/runtime-x86_64" "$APPDIR.squashfs" > "$DIST/SkunkcraftsUpdater-$VER-x86_64.AppImage"
chmod +x "$DIST/SkunkcraftsUpdater-$VER-x86_64.AppImage"
rm -rf "$APPDIR" "$APPDIR.squashfs"

echo "=== CLI zip"
mkdir -p "$STAGE/cli/linux" "$STAGE/cli/windows" "$STAGE/cli/macos"
cp "$ROOT/build/linux/src/cli/sku" "$STAGE/cli/linux/" && strip "$STAGE/cli/linux/sku"
cp "$STAGE/sku.exe" "$STAGE/cli/windows/"
cp "$ROOT/build/macos/src/cli/sku" "$STAGE/cli/macos/"   # never strip: it would break the ad-hoc arm64 signature
cp "$STAGE/README.txt" "$STAGE/THIRD_PARTY_NOTICES.txt" "$STAGE/cli/"
( cd "$STAGE" && zip -qry "$DIST/sku-cli-$VER.zip" cli )

rm -rf "$STAGE"
echo "=== done"
ls -la "$DIST"
