#!/usr/bin/env bash
# Regenerates golden test data for one generator from its upstream plugin.
#
#   scripts/gen-testdata.sh <owner>/<plugin> <version>     e.g. scripts/gen-testdata.sh bufbuild/es v2.17.0
#
# Files (d = testdata/checksums/<owner>/<plugin>):
#   $d/<version>.buf.gen.yaml   committed, input. Tests: `inputs:` entries `- directory: testdata/proto/<test>`.
#                               Option groups: one `- remote: buf.build/<owner>/<plugin>:<version>` plugin
#                               entry per group with `out: <group>`; no `strategy:` (not allowed for remote).
#   $d/<version>.tar.xz         committed, output (built by pack_archive below):
#                                 <test>.request.binpb   the CodeGeneratorRequest buf sent, parameter cleared
#                                                        (identical for all groups)
#                                 <group>/parameter.txt  the parameter buf sent for the group
#                                 <group>/<test>.sha256  sha256sum of the upstream output, paths relative
#                                                        to the test directory
#   $d/gen/<version>/<group>/<test>/
#                               NOT committed (.gitignore): the upstream output; tests show diffs against
#                               it when it exists.
#
# The request is captured by running the same template with every `remote:` replaced by
# internal/cmd/protoc-gen-dump (strategy: all), so the golden test replays exactly the upstream input.
#
# Needs: go, xz, GNU tar, network. Env: BUF — buf binary to use (default: buf $BUF_VERSION built via
# `go install`, does not touch go.mod); BUF_TOKEN — buf.build token (remote plugins are rate limited).
set -euo pipefail

BUF_VERSION=v1.75.0

if [ $# -ne 2 ]; then
	echo "usage: $0 <owner>/<plugin> <version>" >&2
	exit 2
fi
name=$1 version=$2

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
d=testdata/checksums/$name
template=$d/$version.buf.gen.yaml
archive=$d/$version.tar.xz
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

# --- tools ----------------------------------------------------------------------------------------
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if [ -z "${BUF:-}" ]; then
	echo "building buf $BUF_VERSION..." >&2
	GOBIN=$tmp/bin GOFLAGS=-mod=mod go install "github.com/bufbuild/buf/cmd/buf@$BUF_VERSION"
	BUF=$tmp/bin/buf
fi
go build -o "$tmp/bin/protoc-gen-dump" ./internal/cmd/protoc-gen-dump

# Same template, but every remote plugin replaced by the request dumper.
sed -E "s#^([[:space:]]*)-([[:space:]]*)remote:.*#\1-\2local: $tmp/bin/protoc-gen-dump\n\1 \2strategy: all#" \
	"$template" >"$tmp/dump.gen.yaml"

# --- generate -------------------------------------------------------------------------------------
pack=$tmp/pack # archive contents
rm -rf "${gen:?}"
for g in "${groups[@]}"; do
	mkdir -p "$pack/$g" "$gen/$g"
done

for test in "${tests[@]}"; do
	input=testdata/proto/$test
	[ -d "$input" ] || { echo "$input: not found" >&2; exit 1; }
	echo "$name $version: $test" >&2
	"$BUF" generate "$input" --template "$template" -o "$tmp/ref/$test"
	"$BUF" generate "$input" --template "$tmp/dump.gen.yaml" -o "$tmp/req/$test"
	for g in "${groups[@]}"; do
		if [ -d "$tmp/ref/$test/$g" ]; then
			mv "$tmp/ref/$test/$g" "$gen/$g/$test"
		else
			mkdir -p "$gen/$g/$test" # plugin produced no files
		fi
		(cd "$gen/$g/$test" && find . -type f | LC_ALL=C sort | sed 's|^\./||' |
			while IFS= read -r f; do sha256sum -- "$f"; done) >"$pack/$g/$test.sha256"

		req=$tmp/req/$test/$g
		if [ -f "$pack/$test.request.binpb" ]; then
			cmp -s "$req/request.binpb" "$pack/$test.request.binpb" ||
				{ echo "$test: requests differ between groups (not only in parameter)" >&2; exit 1; }
		else
			mv "$req/request.binpb" "$pack/$test.request.binpb"
		fi
		if [ -f "$pack/$g/parameter.txt" ]; then
			cmp -s "$req/parameter.txt" "$pack/$g/parameter.txt" ||
				{ echo "$g: parameter differs between tests" >&2; exit 1; }
		else
			mv "$req/parameter.txt" "$pack/$g/parameter.txt"
		fi
	done
done

# --- pack -----------------------------------------------------------------------------------------
# Reproducible: sorted names, fixed mtime/owner, single-threaded xz → the same inputs give the same bytes.
pack_archive() {
	(cd "$1" && find . -type f | LC_ALL=C sort | sed 's|^\./||' |
		tar --format=ustar --mtime=@0 --owner=0 --group=0 --numeric-owner --mode=a+rX,u+w,go-w \
			--no-recursion -cf - -T -) | xz -9 -T1 --check=crc64 >"$2"
}
pack_archive "$pack" "$tmp/archive.tar.xz"
mv "$tmp/archive.tar.xz" "$archive"

echo "done: $archive ($(du -h "$archive" | cut -f1)), upstream output: $gen" >&2
