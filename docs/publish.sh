#!/bin/bash
# Publishes a version already packaged by release.sh, on the store CDN (S3 bucket mounted with s3fs):
#   1. the client's own update depot, <bucket>/updater/depot/: versioned packages, lists, changelog, guide, cfg
#      (lists generated here: path|crc32 and path|size; locked cfg first, unlocked cfg last, like xp_depot)
#   2. the five downloads under their fixed names in <bucket>/updater/, for permanent links, plus the user
#      guide and the developers' material: PUBLISHING.html / .md (from docs/PUBLISHING.md as it is now),
#      xp_depot.py (from $XP_DEPOT), and version.js, the version shown by the link pages
#   4. what https://updater.skunkcrafts.com/ serves at its root, staged in dist/site_updater/ then
#      uploaded there: the two link pages of docs/site/ with their stylesheet, script and logo, the
#      user and developers' guides and changelog.txt (the rest of its root names are redirected to
#      this CDN folder). The upload goes through xp_depot's own FTP code and the xp_zone.toml of
#      $SITE_ZONE, the zone of the private beta depots on that server: its credentials stay there,
#      unread and unprinted by this script
#   3. checks: the fixed URLs and the depot cfg answer 200, the depot announces the version
# Usage: docs/publish.sh <version> [--dry-run]     e.g. docs/publish.sh 4.0.0-beta8
#        docs/publish.sh --dev-guide [--dry-run]    the developers' material and version.js only
#                                                   (they change between versions)
# See docs/PUBLISH.md. Nothing here is secret: the bucket is mounted with its own credentials.
set -e
VER="$1"; DRY=""
[ -n "$VER" ] || { echo "usage: $0 <version>|--dev-guide [--dry-run]" >&2; exit 1; }
[ "$2" = "--dry-run" ] && DRY="echo [dry-run]"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist/$VER"
S3_DIR=/remote/s3/aerobask/updater            # store CDN bucket (s3fs mount), the updater's folder
CDN_BASE=https://1228098873.rsc.cdn77.org/updater   # what serves that folder
DEPOT_NAME=depot                              # sub-folder holding the client's update depot
DEPOT_URL="$CDN_BASE/$DEPOT_NAME"
SUFFIXES="windows.zip windows-setup.exe macos.zip linux.zip x86_64.AppImage"
XP_DEPOT="${XP_DEPOT:-$HOME/Development/Claude/toolchain/xp_depot}"   # the publishing tool, kept outside this repository
SITE_ZONE="${SITE_ZONE:-/hdd1/depots/nuxit}"   # xp_depot zone (folder with its xp_zone.toml) of updater.skunkcrafts.com
SITE_URL=https://updater.skunkcrafts.com

# The developers' guide, HTML (same converter as the user guide) and Markdown, xp_depot.py and
# version.js, under permanent names; then the folder for updater.skunkcrafts.com.
publish_dev_guide() {
	python3 "$ROOT/tools/md2html.py" "$ROOT/docs/PUBLISHING.md" "$ROOT/dist/PUBLISHING.html"
	$DRY cp "$ROOT/dist/PUBLISHING.html" "$ROOT/docs/PUBLISHING.md" "$S3_DIR/"
	if [ -f "$XP_DEPOT" ]; then $DRY cp "$XP_DEPOT" "$S3_DIR/xp_depot.py"; else echo "warning: $XP_DEPOT not found, xp_depot.py not published" >&2; fi
	# the version shown by the pages: the one being published, or the one the depot announces
	local v="$1"
	[ -n "$v" ] || v=$(grep '^version|' "$S3_DIR/$DEPOT_NAME/skunkcrafts_updater.cfg" 2>/dev/null | cut -d'|' -f2 | tr -d '\r')
	if [ -n "$v" ]; then
		if [ -n "$DRY" ]; then echo "[dry-run] version.js: $v"; else printf 'window.SKU_VERSION = "%s";\n' "$v" > "$S3_DIR/version.js"; fi
	fi
	stage_site
}

# What updater.skunkcrafts.com serves at its root: the pages, the guides and the changelog, as they are now.
stage_site() {
	local out="$ROOT/dist/site_updater"
	rm -rf "$out"; mkdir -p "$out"
	cp "$ROOT/docs/site/index.html" "$ROOT/docs/site/developers.html" "$ROOT/docs/site/site.css" "$ROOT/docs/site/site.js" "$out/"
	cp "$ROOT/packaging/icons/icon.svg" "$out/skunkcrafts-logo.svg"
	cp "$ROOT/docs/USER_GUIDE.md" "$ROOT/docs/PUBLISHING.md" "$out/"
	python3 "$ROOT/tools/md2html.py" "$ROOT/docs/USER_GUIDE.md" "$out/USER_GUIDE.html"
	cp "$ROOT/dist/PUBLISHING.html" "$out/"
	tr -d '\r' < "$ROOT/CHANGELOG.txt" > "$out/changelog.txt"
	upload_site
}

# The staged folder, to the root of updater.skunkcrafts.com, with xp_depot's FTP code and zone file.
# xp_depot's own messages are not shown: on a failure they may name the account.
upload_site() {
	local out="$ROOT/dist/site_updater"
	if [ -n "$DRY" ]; then echo "[dry-run] upload $(ls "$out" | wc -l) file(s) to the root of $SITE_URL"; return 0; fi
	if [ ! -f "$XP_DEPOT" ] || [ ! -f "$SITE_ZONE/xp_zone.toml" ]; then
		echo "warning: $XP_DEPOT or $SITE_ZONE/xp_zone.toml missing: copy $out to the root of $SITE_URL by hand" >&2
		return 0
	fi
	echo "=== site -> $SITE_URL"
	python3 - "$XP_DEPOT" "$SITE_ZONE" "$out" <<'PY'
import contextlib, importlib.machinery, importlib.util, io, os, sys, types
tool, zone_dir, out = sys.argv[1:4]
names = sorted(os.listdir(out))
loader = importlib.machinery.SourceFileLoader("xp_depot", tool)
xp = importlib.util.module_from_spec(importlib.util.spec_from_loader("xp_depot", loader))
noise = io.StringIO()
try:
	with contextlib.redirect_stdout(noise), contextlib.redirect_stderr(noise):
		loader.exec_module(xp)
		backend = xp.make_backend(xp.read_zone_file(zone_dir), "", types.SimpleNamespace(jobs=1, insecure=False))
		try:
			for name in names:
				backend.put_file(os.path.join(out, name), name)
		finally:
			backend.close()
except BaseException:
	sys.exit("   upload failed (xp_depot's message hidden, it may name the account): try xp_depot %s <depot> status" % zone_dir)
print("   %d file(s) uploaded" % len(names))
PY
	for f in "$out"/*; do
		curl -s --max-time 30 "$SITE_URL/$(basename "$f")" | cmp -s - "$f" || { echo "   $SITE_URL/$(basename "$f") differs from $f" >&2; return 1; }
	done
	echo "   $SITE_URL serves them all"
}
check_url() {
	code=$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$1")
	echo "$code  $1"
}

if [ "$VER" = "--dev-guide" ]; then
	mountpoint -q "$(dirname "$S3_DIR")" || { echo "$(dirname "$S3_DIR") is not mounted" >&2; exit 1; }
	mkdir -p "$ROOT/dist"
	echo "=== developers' guide -> $S3_DIR"
	publish_dev_guide
	[ -n "$DRY" ] && exit 0
	sleep 2
	for f in PUBLISHING.html PUBLISHING.md xp_depot.py version.js; do check_url "$CDN_BASE/$f"; done
	echo "Permanent links: https://updater.skunkcrafts.com/ (users), https://updater.skunkcrafts.com/developers.html, $CDN_BASE/PUBLISHING.html, $CDN_BASE/xp_depot.py"
	exit 0
fi

[ -d "$DIST/depot" ] || { echo "$DIST/depot not found: run ./release.sh $VER first" >&2; exit 1; }
for s in $SUFFIXES; do [ -f "$DIST/SkunkcraftsUpdater-$VER-$s" ] || { echo "missing $DIST/SkunkcraftsUpdater-$VER-$s" >&2; exit 1; }; done
[ "$(grep '^version|' "$DIST/depot/skunkcrafts_updater_config.txt" | cut -d'|' -f2)" = "$VER" ] || { echo "depot config is not at version $VER" >&2; exit 1; }
grep -q "^module|$DEPOT_URL\$" "$DIST/depot/skunkcrafts_updater_config.txt" || { echo "depot config module is not $DEPOT_URL (rebuild with SKU_CLIENT_DEPOT_URL=$DEPOT_URL)" >&2; exit 1; }
mountpoint -q "$(dirname "$S3_DIR")" || { echo "$(dirname "$S3_DIR") is not mounted" >&2; exit 1; }
mkdir -p "$S3_DIR"

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

echo "=== 2. fixed-name downloads -> $S3_DIR (permanent links)"
for s in $SUFFIXES; do $DRY cp "$DIST/SkunkcraftsUpdater-$VER-$s" "$S3_DIR/SkunkcraftsUpdater-$s"; done
$DRY cp "$DIST/depot/USER_GUIDE.html" "$DIST/depot/USER_GUIDE.md" "$S3_DIR/"
publish_dev_guide "$VER"
$DRY cp "$DIST/sku-cli-$VER.zip" "$S3_DIR/sku-cli.zip"

echo "=== 3. checks"
[ -n "$DRY" ] && exit 0
sleep 2
for s in $SUFFIXES; do
	code=$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$CDN_BASE/SkunkcraftsUpdater-$s")
	echo "$code  $CDN_BASE/SkunkcraftsUpdater-$s"
	[ "$code" = 200 ] || { echo "not served yet" >&2; exit 1; }
done
check_url "$CDN_BASE/USER_GUIDE.html"
check_url "$CDN_BASE/PUBLISHING.html"
check_url "$CDN_BASE/version.js"
echo "$(curl -sIL --max-time 30 -o /dev/null -w '%{http_code}' "$DEPOT_URL/skunkcrafts_updater.cfg")  $DEPOT_URL/skunkcrafts_updater.cfg"
SKU_CLIENT_URL="$DEPOT_URL" "$ROOT/build/linux/src/cli/sku" client | grep -E "published|package"
echo "=== done: $VER published"
echo "Permanent links: $CDN_BASE/SkunkcraftsUpdater-<windows.zip|windows-setup.exe|macos.zip|linux.zip|x86_64.AppImage>, $CDN_BASE/USER_GUIDE.html, $CDN_BASE/PUBLISHING.html"
[ -f "$S3_DIR/urls to updater v4.txt" ] && echo "Links file (unchanged, the names are permanent): $S3_DIR/urls to updater v4.txt"
