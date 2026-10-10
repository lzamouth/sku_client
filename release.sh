#!/bin/bash
# Builds the three targets with a version number and assembles the deliverables in dist/<version>/,
# one download per platform and install mode (plus the CLI):
#   SkunkcraftsUpdater-<v>-windows.zip       SkunkcraftsUpdater.exe, to drop anywhere (X-Plane folder or not)
#   SkunkcraftsUpdater-<v>-windows-setup.exe NSIS installer
#   SkunkcraftsUpdater-<v>-macos.zip         SkunkcraftsUpdater.app, to drop anywhere
#   SkunkcraftsUpdater-<v>-linux.zip         SkunkcraftsUpdater_lin, to drop anywhere
#   SkunkcraftsUpdater-<v>-x86_64.AppImage   Linux, integrates with the desktop menu
#   sku-cli-<v>.zip                          command-line client for the three OS
#   depot/                                   the same packages under fixed names + config + changelog: the
#                                            client's own update depot, to push with xp_depot (CHANGELOG.txt
#                                            must start with the version line, like an aircraft changelog)
# Usage: release.sh <version>   (e.g. 4.0.0 or 4.0.0-beta1)
set -e
VER="$1"
[ -n "$VER" ] || { echo "usage: $0 <version>" >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$ROOT/dist/$VER"
STAGE="$DIST/stage"
OSX=/usr/local/osxcross/bin
OTOOL=$OSX/x86_64-apple-darwin20.4-otool
# depot the clients check for their own updates (the client follows the module announced there if it moves)
CLIENT_URL="${SKU_CLIENT_DEPOT_URL:-https://1228098873.rsc.cdn77.org/updater/depot}"
[ "$(head -1 "$ROOT/CHANGELOG.txt" | tr -d '\r')" = "$VER" ] || { echo "CHANGELOG.txt must start with the line \"$VER\" (the aircraft-style bare version header)" >&2; exit 1; }

echo "=== building $VER"
"$ROOT/docker/run.sh" ./build.sh linux   -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" -DSKU_CLIENT_DEPOT_URL="$CLIENT_URL" > "$ROOT/build/release-linux.log" 2>&1
"$ROOT/build.sh" windows -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" -DSKU_CLIENT_DEPOT_URL="$CLIENT_URL" > "$ROOT/build/release-windows.log" 2>&1
"$ROOT/build.sh" macos   -DSKU_BUILD_APP=ON -DSKU_VERSION="$VER" -DSKU_CLIENT_DEPOT_URL="$CLIENT_URL" > "$ROOT/build/release-macos.log" 2>&1
"$ROOT/deps/licenses.sh" > /dev/null

rm -rf "$DIST"; mkdir -p "$STAGE"
sed "s/@VERSION@/$VER/" "$ROOT/packaging/README.txt" > "$STAGE/README.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.txt" "$ROOT/docs/USER_GUIDE.md" "$STAGE/"
python3 "$ROOT/tools/md2html.py" "$ROOT/docs/USER_GUIDE.md" "$STAGE/USER_GUIDE.html"
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

echo "=== standalone zips (Windows, Linux; the macOS one comes from make_app.sh)"
( cd "$STAGE" && zip -qr "$DIST/SkunkcraftsUpdater-$VER-windows.zip" SkunkcraftsUpdater.exe README.txt USER_GUIDE.html THIRD_PARTY_NOTICES.txt )
( cd "$STAGE" && zip -qr "$DIST/SkunkcraftsUpdater-$VER-linux.zip" SkunkcraftsUpdater_lin README.txt USER_GUIDE.html THIRD_PARTY_NOTICES.txt )

echo "=== Windows installer"
makensis -V2 -DVERSION="$VER" -DSTAGE="$STAGE" -DOUT="$DIST/SkunkcraftsUpdater-$VER-windows-setup.exe" "$ROOT/packaging/windows/installer.nsi"

echo "=== AppImage (static type-2 runtime + squashfs, no appimagetool needed)"
APPDIR="$DIST/AppDir"
rm -rf "$APPDIR"; mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$STAGE/SkunkcraftsUpdater_lin" "$APPDIR/usr/bin/"
cp "$ROOT/build/linux/src/cli/sku" "$APPDIR/usr/bin/" && strip "$APPDIR/usr/bin/sku"
cp "$ROOT/packaging/linux/SkunkcraftsUpdater.desktop" "$APPDIR/"
cp "$ROOT/packaging/icons/icon.png" "$APPDIR/skunkcrafts-updater.png"
cp "$ROOT/packaging/icons/icon.png" "$APPDIR/.DirIcon"   # required by libappimage (AppImageLauncher integration)
cp "$ROOT/packaging/icons/icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/skunkcrafts-updater.png"
cp "$STAGE/README.txt" "$STAGE/USER_GUIDE.html" "$STAGE/THIRD_PARTY_NOTICES.txt" "$APPDIR/"
printf '#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\nexec "$HERE/usr/bin/SkunkcraftsUpdater_lin" "$@"\n' > "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
# gzip squashfs: read by our static runtime (zlib/zstd) AND by libappimage / AppImageLauncher (gzip/xz), which
# zstd is not; -comp zstd would make "integrate and run" fail on users' desktops.
# mksquashfs lives in the build container; paths are the same inside (/work = repo root)
REL_APPDIR="${APPDIR#$ROOT/}"
"$ROOT/docker/run.sh" mksquashfs "$REL_APPDIR" "$REL_APPDIR.squashfs" -root-owned -noappend -comp gzip -Xcompression-level 9 -b 1M -quiet
cat "$ROOT/deps/tools/runtime-x86_64" "$APPDIR.squashfs" > "$DIST/SkunkcraftsUpdater-$VER-x86_64.AppImage"
chmod +x "$DIST/SkunkcraftsUpdater-$VER-x86_64.AppImage"
rm -rf "$APPDIR" "$APPDIR.squashfs"

echo "=== CLI zip"
mkdir -p "$STAGE/cli/linux" "$STAGE/cli/windows" "$STAGE/cli/macos"
cp "$ROOT/build/linux/src/cli/sku" "$STAGE/cli/linux/" && strip "$STAGE/cli/linux/sku"
cp "$STAGE/sku.exe" "$STAGE/cli/windows/"
cp "$ROOT/build/macos/src/cli/sku" "$STAGE/cli/macos/"   # never strip: it would break the ad-hoc arm64 signature
cp "$STAGE/README.txt" "$STAGE/USER_GUIDE.html" "$STAGE/THIRD_PARTY_NOTICES.txt" "$STAGE/cli/"
( cd "$STAGE" && zip -qry "$DIST/sku-cli-$VER.zip" cli )

echo "=== client depot: dist/$VER/depot (versioned package names), published by docs/publish.sh"
DEPOT="$DIST/depot"
mkdir -p "$DEPOT"
for f in SkunkcraftsUpdater-$VER-windows-setup.exe SkunkcraftsUpdater-$VER-windows.zip SkunkcraftsUpdater-$VER-macos.zip \
         SkunkcraftsUpdater-$VER-linux.zip SkunkcraftsUpdater-$VER-x86_64.AppImage sku-cli-$VER.zip; do cp "$DIST/$f" "$DEPOT/"; done
tr -d '\r' < "$ROOT/CHANGELOG.txt" > "$DEPOT/changelog.txt"
cp "$STAGE/README.txt" "$DEPOT/readme.txt"
cp "$STAGE/USER_GUIDE.html" "$STAGE/USER_GUIDE.md" "$DEPOT/"
printf 'zone|custom\nmodule|%s\nname|SkunkCrafts Updater\nversion|%s\nlocked|false\n' "$CLIENT_URL" "$VER" > "$DEPOT/skunkcrafts_updater_config.txt"

rm -rf "$STAGE"
echo "=== done"
ls -la "$DIST"
