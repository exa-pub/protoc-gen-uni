#!/usr/bin/env bash
# Downloads the pinned buf release binary for this host, verifies its sha256 and prints its path.
#
#   scripts/get-buf.sh [dir]        default dir: build/tools
#
# Used by the e2e tests (CMake calls it unless BUF is set). Needs curl.
set -euo pipefail

BUF_VERSION=v1.75.0
# sha256 of the release assets, from https://github.com/bufbuild/buf/releases/download/$BUF_VERSION/sha256.txt
sha256_of() {
	case $1 in
	buf-Darwin-arm64) echo f71b009ebd8530b12fd85b060c6c245e15d6e5110ccc8da48d8b6fa71e59330b ;;
	buf-Darwin-x86_64) echo ff6d0979143c661d59178a032c57be883b2ac03dc9bf7d00ffca222b0354bd46 ;;
	buf-Linux-aarch64) echo 00eb190aa82df17f393e05b04889cc60a0b13b3259d336e21a87d5f23ab4fd26 ;;
	buf-Linux-x86_64) echo fad1e38d6909bbb583a4da81d8c02a01046e2cd663bde7e337dd24277393764b ;;
	buf-Windows-x86_64.exe) echo de35b288e7962ac72aefee5048aff80551a4f0e6ac865a21f838693461ac8b8d ;;
	*) echo "no pinned buf binary for $1" >&2; return 1 ;;
	esac
}

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dir=${1:-$root/build/tools}

os=$(uname -s) arch=$(uname -m) ext=
case $os in
Linux) ;;
Darwin) [ "$arch" != aarch64 ] || arch=arm64 ;;
MINGW* | MSYS* | CYGWIN*) os=Windows ext=.exe ;;
esac
[ "$os" != Linux ] || [ "$arch" != arm64 ] || arch=aarch64
asset=buf-$os-$arch$ext
want=$(sha256_of "$asset")
bin=$dir/buf-$BUF_VERSION$ext

sum() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

if [ ! -x "$bin" ] || [ "$(sum "$bin")" != "$want" ]; then
	mkdir -p "$dir"
	curl -fsSL --retry 3 -o "$bin.tmp" "https://github.com/bufbuild/buf/releases/download/$BUF_VERSION/$asset"
	got=$(sum "$bin.tmp")
	[ "$got" = "$want" ] || { rm -f "$bin.tmp"; echo "$asset: sha256 $got, want $want" >&2; exit 1; }
	chmod +x "$bin.tmp" && mv "$bin.tmp" "$bin"
fi
echo "$bin"
