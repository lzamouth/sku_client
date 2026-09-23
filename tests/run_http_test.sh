#!/bin/bash
# Serves tests/fixture/depot with python's http.server and runs test_http.
# Usage: run_http_test.sh <path-to-test_http-binary>
set -e
BIN="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
FIX="$HERE/fixture/depot"
SCRATCH="$(mktemp -d)"
PORT=$((20000 + RANDOM % 20000))
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$FIX" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -rf "$SCRATCH"' EXIT
for i in $(seq 1 50); do python3 -c "import urllib.request,sys; urllib.request.urlopen(sys.argv[1], timeout=1)" "http://127.0.0.1:$PORT/skunkcrafts_updater.cfg" >/dev/null 2>&1 && break; sleep 0.1; done
SKU_TEST_URL="http://127.0.0.1:$PORT" SKU_TEST_DIR="$SCRATCH" "$BIN"
