#!/usr/bin/env bash
# Run a command inside the ff9-ds build container, e.g. `tools/docker.sh make`.
# The image is (re)built automatically when the Dockerfile changes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=ff9-ds-build
HASH=$(sha1sum "$ROOT/tools/docker/Dockerfile" | cut -c1-12)

if ! docker image inspect "$IMAGE:$HASH" >/dev/null 2>&1; then
    docker build -t "$IMAGE:$HASH" -t "$IMAGE:latest" -f "$ROOT/tools/docker/Dockerfile" "$ROOT/tools/docker"
fi

TTY=()
[ -t 0 ] && [ -t 1 ] && TTY=(-it)

# Cap memory so a runaway job cannot take down the host.
exec docker run --rm --init --memory=8g --memory-swap=8g "${TTY[@]}" -u "$(id -u):$(id -g)" \
    -v "$ROOT:/ff9ds" -w /ff9ds "$IMAGE:$HASH" "${@:-bash}"
