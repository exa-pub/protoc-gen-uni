#!/usr/bin/env bash
# Runs the e2e golden tests of every generator version in testdata/checksums (tests/e2e/golden.sh)
# against one binary. Versions the binary does not contain are skipped. Used by CI on runners
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
fail=0
for template in "$root"/testdata/checksums/*/*/v*.buf.gen.yaml; do
	rel=${template#"$root/testdata/checksums/"}
	name=${rel%/*} version=${rel##*/}
	version=${version%.buf.gen.yaml}
	echo "== $name $version"
	"$root/tests/e2e/golden.sh" ${runner[@]+"${runner[@]}"} "$bin" "$name" "$version"
	case $? in 0 | 77) ;; *) fail=1 ;; esac
done
exit $fail
