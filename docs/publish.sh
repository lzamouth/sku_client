#!/bin/bash
# Publishes a version already packaged by release.sh, on the store CDN (S3 bucket mounted with s3fs):
#   1. the client's own update depot, <bucket>/sku_client/: versioned packages, lists, changelog, guide, cfg
#      (lists generated here: path|crc32 and path|size; locked cfg first, unlocked cfg last, like xp_depot)
#   2. the five downloads under their fixed names at the bucket root, for permanent links, plus the guide
#   3. checks: the fixed URLs and the depot cfg answer 200, the depot announces the version
# Usage: docs/publish.sh <version> [--dry-run]     e.g. docs/publish.sh 4.0.0-beta8
# See docs/PUBLISH.md. Nothing here is secret: the bucket is mounted with its own credentials.
set -e
VER="$1"; DRY=""
[ -n "$VER" ] || { echo "usage: $0 <version> [--dry-run]" >&2; exit 1; }
[ "$2" = "--dry-run" ] && DRY="echo [dry-run]"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist/$VER"
S3_DIR=/remote/s3/aerobask                    # store CDN bucket (s3fs mount)
CDN_BASE=https://1228098873.rsc.cdn77.org     # what serves that bucket
DEPOT_NAME=sku_client                         # sub-folder holding the client's update depot
DEPOT_URL="$CDN_BASE/$DEPOT_NAME"
SUFFIXES="windows.zip windows-setup.exe macos.zip linux.zip x86_64.AppImage"

[ -d "$DIST/depot" ] || { echo "$DIST/depot not found: run ./release.sh $VER first" >&2; exit 1; }
for s in $SUFFIXES; do [ -f "$DIST/SkunkcraftsUpdater-$VER-$s" ] || { echo "missing $DIST/SkunkcraftsUpdater-$VER-$s" >&2; exit 1; }; done
[ "$(grep '^version|' "$DIST/depot/skunkcrafts_updater_config.txt" | cut -d'|' -f2)" = "$VER" ] || { echo "depot config is not at version $VER" >&2; exit 1; }
grep -q "^module|$DEPOT_URL\$" "$DIST/depot/skunkcrafts_updater_config.txt" || { echo "depot config module is not $DEPOT_URL (rebuild with SKU_CLIENT_DEPOT_URL=$DEPOT_URL)" >&2; exit 1; }
mountpoint -q "$S3_DIR" || { echo "$S3_DIR is not mounted" >&2; exit 1; }

echo "=== 1. client depot -> $S3_DIR/$DEPOT_NAME  ($DEPOT_URL)"
DEP="$S3_DIR/$DEPOT_NAME"
$DRY mkdir -p "$DEP"
# lists: every file of the depot folder except the protocol files themselves
python3 - "$DIST/depot" "$DIST/depot" <<'PY'
import os, sys, zlib
src, out = sys.argv[1], sys.argv[2]
skip = {"skunkcrafts_updater_config.txt", "skunkcrafts_updater_whitelist.txt", "skunkcrafts_updater_sizeslist.txt", "skunkcrafts_updater_blacklist.txt"}
wl, sz = [], []
for name in sorted(os.listdir(src)):
	if name in skip or name.startswith("."): continue
	data = open(os.path.join(src, name), "rb").read()
	wl.append("%s|%d" % (name, zlib.crc32(data) & 0xFFFFFFFF)); sz.append("%s|%d" % (name, len(data)))
open(os.path.join(out, "skunkcrafts_updater_whitelist.txt"), "w").write("\n".join(wl) + "\n")
open(os.path.join(out, "skunkcrafts_updater_sizeslist.txt"), "w").write("\n".join(sz) + "\n")
open(os.path.join(out, "skunkcrafts_updater_blacklist.txt"), "w").write("")
print("   %d file(s) listed" % len(wl))
PY
if [ -z "$DRY" ]; then
	sed 's/^locked|.*/locked|true/' "$DIST/depot/skunkcrafts_updater_config.txt" > "$DEP/skunkcrafts_updater.cfg"   # lock first
	for f in "$DIST"/depot/*; do
		n="$(basename "$f")"
		case "$n" in skunkcrafts_updater_config.txt) ;; *) cp "$f" "$DEP/$n" ;; esac
	done
	sed 's/^locked|.*/locked|false/' "$DIST/depot/skunkcrafts_updater_config.txt" > "$DEP/skunkcrafts_updater.cfg"  # unlock last
else
	echo "[dry-run] lock, copy $(ls "$DIST/depot" | wc -l) file(s), unlock"
fi

echo "=== 2. fixed-name downloads -> $S3_DIR"
for s in $SUFFIXES; do $DRY cp "$DIST/SkunkcraftsUpdater-$VER-$s" "$S3_DIR/SkunkcraftsUpdater-$s"; done
$DRY cp "$DIST/depot/USER_GUIDE.html" "$DIST/depot/USER_GUIDE.md" "$S3_DIR/"
$DRY cp "$DIST/sku-cli-$VER.zip" "$S3_DIR/sku-cli.zip"

echo "=== 3. checks"
[ -n "$DRY" ] && exit 0
sleep 2
for s in $SUFFIXES; do
	code=$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$CDN_BASE/SkunkcraftsUpdater-$s")
	echo "$code  $CDN_BASE/SkunkcraftsUpdater-$s"
	[ "$code" = 200 ] || { echo "not served yet" >&2; exit 1; }
done
echo "$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$CDN_BASE/USER_GUIDE.html")  $CDN_BASE/USER_GUIDE.html"
echo "$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$DEPOT_URL/skunkcrafts_updater.cfg")  $DEPOT_URL/skunkcrafts_updater.cfg"
SKU_CLIENT_URL="$DEPOT_URL" "$ROOT/build/linux/src/cli/sku" client | grep -E "published|package"
echo "=== done: $VER published"
echo "Permanent links: $CDN_BASE/SkunkcraftsUpdater-<windows.zip|windows-setup.exe|macos.zip|linux.zip|x86_64.AppImage>, $CDN_BASE/USER_GUIDE.html"
