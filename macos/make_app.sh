#!/bin/bash
# Build a macOS .app bundle from a universal binary, on Linux, and zip it
# (Info-ZIP keeps the executable bit; macOS Archive Utility restores it).
# Usage: macos/make_app.sh <binary> <AppName> <bundle-id> <version> <out-dir> [resource files...]
set -e
BIN="$1"; NAME="$2"; ID="$3"; VER="$4"; OUT="$5"; shift 5
APP="$OUT/$NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/$NAME"
chmod 755 "$APP/Contents/MacOS/$NAME"
ICON=""
for r in "$@"; do
	cp "$r" "$APP/Contents/Resources/"
	# an .icns among the resources is the application icon: without CFBundleIconFile
	# macOS ignores it and shows the generic icon (Finder, Dock, Launchpad)
	case "$r" in *.icns) ICON=$(basename "$r" .icns) ;; esac
done
ICON_KEY=""
[ -n "$ICON" ] && ICON_KEY="<key>CFBundleIconFile</key>           <string>$ICON</string>"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>               <string>$NAME</string>
	<key>CFBundleDisplayName</key>        <string>$NAME</string>
	<key>CFBundleExecutable</key>         <string>$NAME</string>
	$ICON_KEY
	<key>CFBundleIdentifier</key>         <string>$ID</string>
	<key>CFBundlePackageType</key>        <string>APPL</string>
	<key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
	<key>CFBundleShortVersionString</key> <string>$VER</string>
	<key>CFBundleVersion</key>            <string>$VER</string>
	<key>LSMinimumSystemVersion</key>     <string>10.15</string>
	<key>NSHighResolutionCapable</key>    <true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
	<key>CFBundleSupportedPlatforms</key> <array><string>MacOSX</string></array>
</dict>
</plist>
EOF
printf 'APPL????' > "$APP/Contents/PkgInfo"
( cd "$OUT" && rm -f "$NAME-macos.zip" && zip -qry "$NAME-macos.zip" "$NAME.app" )
echo "built $APP and $OUT/$NAME-macos.zip"
