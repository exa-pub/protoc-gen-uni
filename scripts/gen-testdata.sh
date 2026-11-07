#!/usr/bin/env bash
# Regenerates golden test data for one generator from its upstream plugin.
#
#   scripts/gen-testdata.sh <owner>/<plugin> <version>     e.g. scripts/gen-testdata.sh bufbuild/es v2.17.0
#
# Files (d = testdata/checksums/<owner>/<plugin>):
#   $d/<version>.buf.gen.yaml   committed, input. Tests: `inputs:` entries `- directory: testdata/proto/<test>`.
#                               Option groups: one `- remote: buf.build/<owner>/<plugin>:<version>` plugin
#                               entry per group with `out: <group>`; no `strategy:` (not allowed for remote).
#   $d/<version>.sha256         committed, output: `<sha256>  <group>/<test>/<path>` of the upstream output,
#                               sorted by path; checked by the e2e tests (tests/e2e/golden.sh).
#   $d/gen/<version>/<group>/<test>/
#                               NOT committed (.gitignore): the upstream output; tests show diffs against
#                               it when it exists.
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
gen=$d/gen/$version
[ -f "$template" ] || { echo "$template: not found" >&2; exit 1; }

die() {
	echo "$template: $*" >&2
	exit 1
}

# --- validate the template --------------------------------------------------------------------------
grep -qE '^[[:space:]]*strategy:' "$template" && die "remove 'strategy:', it is not allowed for remote plugins"
mapfile -t groups < <(sed -nE 's/^[[:space:]]*-?[[:space:]]*out:[[:space:]]*([^[:space:]#]+).*/\1/p' "$template")
remotes=$(grep -cE '^[[:space:]]*-[[:space:]]*remote:' "$template" || true)
[ "${#groups[@]}" -gt 0 ] && [ "${#groups[@]}" -eq "$remotes" ] ||
	die "every plugin entry must be '- remote: buf.build/$name:$version' with 'out: <group>'"
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

# --- generate -------------------------------------------------------------------------------------
rm -rf "${gen:?}"
mkdir -p "$gen"

for test in "${tests[@]}"; do
	input=testdata/proto/$test
	[ -d "$input" ] || { echo "$input: not found" >&2; exit 1; }
	echo "$name $version: $test" >&2
	"$BUF" generate "$input" --template "$template" -o "$tmp/ref/$test"
	for g in "${groups[@]}"; do
		if [ -d "$tmp/ref/$test/$g" ]; then
			mkdir -p "$gen/$g"
			mv "$tmp/ref/$test/$g" "$gen/$g/$test"
		fi
	done
done

# "<sha256>  <group>/<test>/<path>", sorted by path (a test without output has no lines).
(cd "$gen" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r f; do sha256sum -- "$f"; done) |
	LC_ALL=C sort -k2 >"$d/$version.sha256"

echo "done: $d/$version.sha256 ($(wc -l <"$d/$version.sha256") files), upstream output: $gen" >&2
