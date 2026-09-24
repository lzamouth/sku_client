#!/bin/bash
# Publishes a version already packaged by release.sh:
#   1. the client's own update depot (xp_depot, zone "SkunkCrafts - Stable", folder sku_client)
#   2. the five downloads on the store CDN (S3 bucket mounted with s3fs)
#   3. the "urls to updater v4.txt" list on that CDN
#   4. checks: the five URLs answer 200, the depot announces the version
# Usage: docs/publish.sh <version> [--dry-run]     e.g. docs/publish.sh 4.0.0-beta6
# See docs/PUBLISH.md. Nothing here is secret: xp_depot reads its own credentials.
set -e
VER="$1"; DRY=""
[ -n "$VER" ] || { echo "usage: $0 <version> [--dry-run]" >&2; exit 1; }
[ "$2" = "--dry-run" ] && DRY="echo [dry-run]"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist/$VER"
ZONE=/hdd1/depots/skunkcrafts                 # holds xp_zone.toml
DEPOT_NAME=sku_client                         # folder inside the zone
S3_DIR=/remote/s3/aerobask                    # store CDN bucket (s3fs mount)
CDN_BASE=https://1228098873.rsc.cdn77.org     # what serves that bucket
URLS_FILE="$S3_DIR/urls to updater v4.txt"
FILES="SkunkcraftsUpdater-$VER-windows.zip SkunkcraftsUpdater-$VER-windows-setup.exe SkunkcraftsUpdater-$VER-macos.zip SkunkcraftsUpdater-$VER-linux.zip SkunkcraftsUpdater-$VER-x86_64.AppImage"

[ -d "$DIST/depot" ] || { echo "$DIST/depot not found: run ./release.sh $VER first" >&2; exit 1; }
for f in $FILES; do [ -f "$DIST/$f" ] || { echo "missing $DIST/$f" >&2; exit 1; }; done
[ "$(grep '^version|' "$DIST/depot/skunkcrafts_updater_config.txt" | cut -d'|' -f2)" = "$VER" ] || { echo "depot config is not at version $VER" >&2; exit 1; }
mountpoint -q "$S3_DIR" || { echo "$S3_DIR is not mounted" >&2; exit 1; }

echo "=== 1. client depot -> $ZONE/$DEPOT_NAME"
$DRY rm -rf "$ZONE/$DEPOT_NAME"
$DRY cp -r "$DIST/depot" "$ZONE/$DEPOT_NAME"
$DRY xp_depot "$ZONE" "$DEPOT_NAME" update

echo "=== 2. downloads -> $S3_DIR"
for f in $FILES; do $DRY cp "$DIST/$f" "$S3_DIR/"; done

echo "=== 3. $URLS_FILE"
if [ -z "$DRY" ]; then
	# every "SkunkcraftsUpdater-<old version>-<suffix>" becomes the new version; labels are kept
	sed -i -E "s/SkunkcraftsUpdater-[0-9][A-Za-z0-9.-]*-(windows\.zip|windows-setup\.exe|macos\.zip|linux\.zip|x86_64\.AppImage)/SkunkcraftsUpdater-$VER-\1/g" "$URLS_FILE"
	cat "$URLS_FILE"
else
	echo "[dry-run] rewrite versions in \"$URLS_FILE\""
fi

echo "=== 4. checks"
[ -n "$DRY" ] && exit 0
sleep 2
for f in $FILES; do
	code=$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$CDN_BASE/$f")
	echo "$code  $CDN_BASE/$f"
	[ "$code" = 200 ] || { echo "not served yet" >&2; exit 1; }
done
"$ROOT/build/linux/src/cli/sku" client | grep published
echo "=== done: $VER published"
