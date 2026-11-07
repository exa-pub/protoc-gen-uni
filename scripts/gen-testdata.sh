#!/usr/bin/env bash
# Regenerates golden test data for one generator from its upstream plugin.
#
#   scripts/gen-testdata.sh <owner>/<plugin> <version>     e.g. scripts/gen-testdata.sh bufbuild/es v2.17.0
#
# Files (d = testdata/checksums/<owner>/<plugin>):
#   $d/<version>.buf.gen.yaml   committed, input. Tests: `inputs:` entries `- directory: testdata/proto/<test>`.
#                               Option groups: one `- remote: buf.build/<owner>/<plugin>:<version>` plugin
#                               entry per group with `out: <group>`; no `strategy:` (not allowed for remote).
#   $d/checksums.txt            committed, output: one line `<sum>  <version>/<test>` per version and test,
#                               sorted by version (sort -V), then test (byte order); this script replaces
#                               the lines of <version>. Checked by the e2e tests (tests/e2e/golden.sh).
#   testdata/gen/<owner>/<plugin>/<version>/<group>/<test>/
#                               NOT committed (.gitignore): the upstream output; tests show diffs against
#                               it when it exists.
#
# The sum of a test covers the output of all its option groups: for every output file the line
# `<sha256 of the file>  <group>/<path>` (lowercase hex, two spaces, <path> relative to the group's
# `out:` directory), the lines sorted by `<group>/<path>` in byte order and each ended by "\n"; <sum>
# is the sha256 of that text. A test without output has the sum of the empty text
# (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855). tests/e2e/golden.sh implements
# the same definition (filesums there).
#
# Needs: network, sha256sum. Env: BUF — buf binary to use (default: scripts/get-buf.sh, the pinned
# version); BUF_TOKEN — buf.build token (remote plugins are rate limited).
set -euo pipefail

if [ $# -ne 2 ]; then
	echo "usage: $0 <owner>/<plugin> <version>" >&2
	exit 2
fi
name=$1 version=$2

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
d=testdata/checksums/$name
template=$d/$version.buf.gen.yaml
gen=testdata/gen/$name/$version
[ -f "$template" ] || { echo "$template: not found" >&2; exit 1; }

die() {
	echo "$template: $*" >&2
	exit 1
}

# --- validate the template --------------------------------------------------------------------------
grep -qE '^[[:space:]]*strategy:' "$template" && die "remove 'strategy:', it is not allowed for remote plugins"
mapfile -t groups < <(sed -nE 's/^[[:space:]]*-?[[:space:]]*out:[[:space:]]*([^[:space:]#]+).*/\1/p' "$template")
remotes=$(grep -cE '^[[:space:]]*-[[:space:]]*remote:' "$template" || true)
if [ "${#groups[@]}" -eq 0 ] || [ "${#groups[@]}" -ne "$remotes" ]; then
	die "every plugin entry must be '- remote: buf.build/$name:$version' with 'out: <group>'"
fi
for g in "${groups[@]}"; do
	[[ "$g" =~ ^[A-Za-z0-9._-]+$ ]] || die "out: $g must be a plain directory name"
done
[ -z "$(printf '%s\n' "${groups[@]}" | sort | uniq -d)" ] || die "duplicate out directories"
grep -E '^[[:space:]]*-[[:space:]]*remote:' "$template" | grep -vqF "buf.build/$name:$version" &&
	die "all remote plugins must be buf.build/$name:$version"
mapfile -t tests < <(sed -nE 's|^[[:space:]]*-[[:space:]]*directory:[[:space:]]*testdata/proto/([A-Za-z0-9._-]+)[[:space:]]*$|\1|p' "$template")
[ "${#tests[@]}" -gt 0 ] || die "no tests: add 'inputs:' entries '- directory: testdata/proto/<test>'"
[ "$(grep -cE '^[[:space:]]*-[[:space:]]*[a-z_]+:' <(sed -n '/^inputs:/,/^[^[:space:]-]/p' "$template"))" -eq "${#tests[@]}" ] ||
	die "every 'inputs:' entry must be '- directory: testdata/proto/<test>'"

BUF=${BUF:-$("$root/scripts/get-buf.sh")}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# buf generate, retried while buf.build rate-limits remote plugins (resource_exhausted).
generate() {
	local i
	for i in 1 2 3 4 5 6 7 8 9 10; do
		rm -rf "$2"
		if "$BUF" generate "$1" --template "$template" -o "$2" 2>"$tmp/err"; then
			return 0
		fi
		grep -q resource_exhausted "$tmp/err" || { cat "$tmp/err" >&2; return 1; }
		echo "rate limited, retrying in $((i * 15)) s" >&2
		sleep $((i * 15))
	done
	cat "$tmp/err" >&2
	return 1
}

# "<sha256>  <group>/<path>" of the files DIR/<group>/<path>, sorted by "<group>/<path>" in byte order
# (the lines of the test sum, see above; same as filesums in tests/e2e/golden.sh).
filesums() {
	(
		cd "$1" 2>/dev/null || exit 0
		find . -type f | sed 's|^\./||' | while IFS= read -r f; do
			printf '%s  %s\n' "$(sha256sum <"$f" | cut -c1-64)" "$f"
		done | LC_ALL=C sort -t ' ' -k3
	)
}

# checksums.txt lines sorted by version (sort -V), then test (byte order).
sortsums() {
	awk '{ i = index($2, "/"); print substr($2, 1, i - 1), substr($2, i + 1), $1 }' |
		LC_ALL=C sort -k1,1V -k2,2 | awk '{ print $3 "  " $1 "/" $2 }'
}

# --- generate -------------------------------------------------------------------------------------
rm -rf "${gen:?}"
mkdir -p "$gen"

for test in "${tests[@]}"; do
	input=testdata/proto/$test
	[ -d "$input" ] || { echo "$input: not found" >&2; exit 1; }
	echo "$name $version: $test" >&2
	generate "$input" "$tmp/ref/$test"
	echo "$(filesums "$tmp/ref/$test" | sha256sum | cut -c1-64)  $version/$test" >>"$tmp/sums"
	for g in "${groups[@]}"; do
		if [ -d "$tmp/ref/$test/$g" ]; then
			mkdir -p "$gen/$g"
			mv "$tmp/ref/$test/$g" "$gen/$g/$test"
		fi
	done
done

# Replace the lines of this version in checksums.txt.
sums=$d/checksums.txt
{
	if [ -f "$sums" ]; then awk -v v="$version/" 'index($2, v) != 1' "$sums"; fi
	cat "$tmp/sums"
} | sortsums >"$sums.tmp"
mv "$sums.tmp" "$sums"

echo "done: $sums ($version: ${#tests[@]} tests, $(find "$gen" -type f | wc -l | tr -d ' ') files), upstream output: $gen" >&2
