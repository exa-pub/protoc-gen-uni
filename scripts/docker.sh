#!/usr/bin/env bash
# Runs a command inside the toolchain image (docker/toolchain.Dockerfile) with the repository
# mounted at the same path and the host uid/gid, so build trees are usable from the host too.
#
#   scripts/docker.sh cmake --workflow --preset linux-x86_64
#   scripts/docker.sh bash
#
# The image is tagged with a hash of the Dockerfile and built locally on first use (~30 minutes:
# GCC is compiled for every target). UNI_TOOLCHAIN_IMAGE selects a registry image instead, which is
# pulled, never built (CI: ghcr.io, pushed by .github/workflows/toolchain.yml).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dockerfile=$root/docker/toolchain.Dockerfile
hash=$( (sha256sum "$dockerfile" 2>/dev/null || shasum -a 256 "$dockerfile") | cut -c1-12)
image=${UNI_TOOLCHAIN_IMAGE:-protoc-gen-uni-toolchain:$hash}
if ! docker image inspect "$image" >/dev/null 2>&1; then
	if [ -n "${UNI_TOOLCHAIN_IMAGE:-}" ]; then
		docker pull "$image" >&2
	else
		echo "building $image (once)..." >&2
		docker build -t "$image" -f "$dockerfile" "$root/docker" >&2
	fi
fi
tty=()
[ -t 0 ] && [ -t 1 ] && tty=(-t)
mkdir -p "$root/build/.home"
exec docker run --rm -i "${tty[@]}" -u "$(id -u):$(id -g)" -e HOME="$root/build/.home" \
	-e BUF_TOKEN -e UNI_DIFF_LINES -v "$root:$root" -w "$PWD" "$image" "$@"
