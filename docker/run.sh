#!/bin/bash
# Run a command inside the Linux build container, as the calling user so that
# build outputs are not root-owned. Usage: docker/run.sh <command...>
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=sku-build-linux
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" -f "$HERE/docker/Dockerfile.linux" "$HERE/docker"
fi
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$HERE:/work" -w /work "$IMAGE" "$@"
