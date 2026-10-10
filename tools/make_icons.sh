#!/bin/bash
# Renders the application icons from packaging/icons/icon.svg (run on the host: needs
# inkscape and ImageMagick; the results are committed, the builds only copy them).
#   icon.png  256 px (Linux, AppImage)   icon.ico  16 to 256 px (Windows)   icon.icns  16 to 1024 px (macOS)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/packaging/icons"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
for s in 16 24 32 48 64 128 256 1024; do
	inkscape "$DIR/icon.svg" --export-type=png --export-width=$s --export-height=$s --export-filename="$TMP/$s.png" >/dev/null 2>&1
done
cp "$TMP/256.png" "$DIR/icon.png"
# PNG-encoded entries (Windows Vista and later): a few kB instead of raw bitmaps
python3 - "$TMP" "$DIR/icon.ico" <<'PY'
import sys
from PIL import Image
t, out = sys.argv[1], sys.argv[2]
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [Image.open("%s/%d.png" % (t, s)).convert("RGBA") for s in sizes]
imgs[-1].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1], bitmap_format="png")
PY
python3 "$ROOT/tools/make_icns.py" "$TMP/1024.png" "$DIR/icon.icns"
ls -la "$DIR"
