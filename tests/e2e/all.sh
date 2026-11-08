#!/usr/bin/env bash
# Runs the e2e golden tests of every generator version in testdata/checksums (tests/e2e/golden.sh)
# against one binary, UNI_E2E_JOBS versions at a time (default: the number of CPUs), and prints
# their output in order. Versions the binary does not contain are skipped. Used by CI on runners
# without the CMake build tree (aarch64, macOS, Windows).
#
#   tests/e2e/all.sh [--runner CMD] BIN
set -uo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
runner=()
if [ "${1:-}" = --runner ]; then
	runner=(--runner "$2")
	shift 2
fi
[ $# -eq 1 ] || { echo "usage: $0 [--runner CMD] BIN" >&2; exit 2; }
bin=$1
export BUF=${BUF:-$("$root/scripts/get-buf.sh")} || exit 1
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

versions=()
for template in "$root"/testdata/checksums/*/*/v*.buf.gen.yaml; do
	rel=${template#"$root/testdata/checksums/"}
	versions+=("${rel%/*}" "$(basename "$rel" .buf.gen.yaml)")
done
# Each run writes OUT/<name>_<version>, plus <...>.fail when it fails (77: skipped).
# shellcheck disable=SC2016 # the script is expanded by the inner bash
printf '%s\n' "${versions[@]}" | xargs -P "${UNI_E2E_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}" -n 2 bash -c '
	name=${*: -2:1} version=${*: -1}
	log=$0/${name//\//_}_$version
	"${@:1:$#-2}" "$name" "$version" >"$log" 2>&1
	case $? in 0 | 77) ;; *) touch "$log.fail" ;; esac
' "$out" "$root/tests/e2e/golden.sh" ${runner[@]+"${runner[@]}"} "$bin"

fail=0
for ((i = 0; i < ${#versions[@]}; i += 2)); do
	log=$out/${versions[i]//\//_}_${versions[i + 1]}
	echo "== ${versions[i]} ${versions[i + 1]}"
	cat "$log"
	[ ! -e "$log.fail" ] || fail=1
done
exit $fail
