#!/bin/bash
# Builds the end-to-end fixture, serves the depot and runs test_addon once
# per protocol level. Usage: run_addon_test.sh <path-to-test_addon-binary>
set -e
BIN="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
PORT=$((20000 + RANDOM % 20000))
trap 'kill $SRV 2>/dev/null; rm -rf "$WORK"' EXIT
python3 "$HERE/make_fixture.py" "$WORK" "$PORT" >/dev/null
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$WORK/depot" >/dev/null 2>&1 &
SRV=$!
for i in $(seq 1 50); do python3 -c "import urllib.request,sys; urllib.request.urlopen(sys.argv[1], timeout=1)" "http://127.0.0.1:$PORT/level1/skunkcrafts_updater.cfg" >/dev/null 2>&1 && break; sleep 0.1; done
SKU_TEST_XP="$WORK/xp" SKU_TEST_URL="http://127.0.0.1:$PORT" SKU_TEST_LEVEL=level1 "$BIN"
# fresh fixture for the level-2 depot
rm -rf "$WORK/xp"
python3 "$HERE/make_fixture.py" "$WORK" "$PORT" >/dev/null
SKU_TEST_XP="$WORK/xp" SKU_TEST_URL="http://127.0.0.1:$PORT" SKU_TEST_LEVEL=level2 "$BIN"
