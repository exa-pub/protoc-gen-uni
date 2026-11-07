#!/usr/bin/env bash
# Checks that the generators of a binary (`BIN --list`), generators/, the README files and
# testdata/checksums/ are consistent:
#   - names are <owner>/<plugin> as on buf.build, versions vX.Y.Z[-pre]; the list is sorted by
#     name, then version, without duplicates;
#   - every generator has generators/<name>/<version>/generator.c, a version entry in
#     generators/<name>/README.md, testdata/checksums/<name>/<version>.{buf.gen.yaml,sha256} and
#     the test inputs (testdata/proto/<test>) of its template;
#   - every version directory and every template or checksum file belongs to a generator.
#
#   tests/e2e/check-registry.sh [--runner CMD] BIN
set -euo pipefail

runner=()
if [ "${1:-}" = --runner ]; then
	read -r -a runner <<<"$2"
	shift 2
fi
[ $# -eq 1 ] || { echo "usage: $0 [--runner CMD] BIN" >&2; exit 2; }
bin=$1
case $bin in /*) ;; *) bin=$PWD/$bin ;; esac
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

fail=0
err() { echo "$*"; fail=1; }

list=$(${runner[@]+"${runner[@]}"} "$bin" --list | tr -d '\r')
[ -n "$list" ] || err "$bin --list: no generators"
printf '%s\n' "$list" | LC_ALL=C sort -c -u -k1,1 -k2,2V 2>/dev/null ||
	err "generators must be sorted by name, then version, without duplicates:"$'\n'"$list"

known=
while read -r name version; do
	key=$name/$version
	known=$known$key$'\n'
	[[ $name =~ ^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$ ]] || err "$key: name must be <owner>/<plugin> as on buf.build"
	[[ $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]] || err "$key: version must be vX.Y.Z"
	[ -f "generators/$key/generator.c" ] || err "$key: generators/$key/generator.c not found"
	readme=generators/$name/README.md
	if [ ! -f "$readme" ]; then
		err "$key: $readme not found"
	elif ! grep -qE "^\| *$version *\|" "$readme"; then
		err "$key: $readme has no row for $version in its versions table"
	fi
	t=testdata/checksums/$key
	for f in "$t.buf.gen.yaml" "$t.sha256"; do [ -f "$f" ] || err "$key: $f not found"; done
	[ -f "$t.buf.gen.yaml" ] || continue
	for test in $(sed -nE 's|^[[:space:]]*-[[:space:]]*directory:[[:space:]]*testdata/proto/([A-Za-z0-9._-]+)[[:space:]]*$|\1|p' "$t.buf.gen.yaml"); do
		[ -d "testdata/proto/$test" ] || err "$key: testdata/proto/$test not found"
	done
done <<<"$list"

shopt -s nullglob
for p in generators/*/*/v*/ testdata/checksums/*/*/v*.buf.gen.yaml testdata/checksums/*/*/v*.sha256; do
	key=${p%/}
	key=${key#generators/}
	key=${key#testdata/checksums/}
	key=${key%.buf.gen.yaml}
	key=${key%.sha256}
	printf '%s' "$known" | grep -qxF "$key" || err "$p: $key is not a generator of $bin (generators/registry.c)"
done

[ $fail -eq 0 ] && echo "ok   $(printf '%s\n' "$list" | wc -l | tr -d ' ') generators"
exit $fail
