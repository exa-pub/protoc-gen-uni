#!/usr/bin/env bash
# Checks that the generators of a binary (`BIN --list`), generators/, the README files and
# testdata/checksums/ are consistent:
#   - names are <owner>/<plugin> as on buf.build, versions vX.Y[.Z][-pre]; the list is sorted by
#     name, then version, without duplicates;
#   - every generator has generators/<name>/<version>/{CMakeLists.txt,generator.c}, a version entry in
#     generators/<name>/README.md, testdata/checksums/<name>/<version>.buf.gen.yaml and the test
#     inputs (testdata/proto/<test>) of its template;
#   - every testdata/checksums/<name>/checksums.txt has well-formed lines `<sum>  <version>/<test>`
#     (scripts/gen-testdata.sh), sorted by version, then test, exactly one per test of each template
#     <version>.buf.gen.yaml and none for other versions or tests;
#   - every version directory belongs to a generator; templates without a version directory are test
#     data prepared for a port that does not exist yet (reported as pending).
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
# The tests of a template: its `- directory: testdata/proto/<test>` inputs.
tests() { sed -nE 's|^[[:space:]]*-[[:space:]]*directory:[[:space:]]*testdata/proto/([A-Za-z0-9._-]+)[[:space:]]*$|\1|p' "$1"; }

list=$(${runner[@]+"${runner[@]}"} "$bin" --list | tr -d '\r')
[ -n "$list" ] || err "$bin --list: no generators"
printf '%s\n' "$list" | LC_ALL=C sort -c -u -k1,1 -k2,2V 2>/dev/null ||
	err "generators must be sorted by name, then version, without duplicates:"$'\n'"$list"

known=
pending=
while read -r name version; do
	key=$name/$version
	known=$known$key$'\n'
	[[ $name =~ ^[a-z0-9][a-z0-9-]*/[a-z0-9][a-z0-9-]*$ ]] || err "$key: name must be <owner>/<plugin> as on buf.build"
	[[ $version =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?(-[0-9A-Za-z.-]+)?$ ]] || err "$key: version must be vX.Y[.Z]"
	for f in CMakeLists.txt generator.c; do [ -f "generators/$key/$f" ] || err "$key: generators/$key/$f not found"; done
	readme=generators/$name/README.md
	if [ ! -f "$readme" ]; then
		err "$key: $readme not found"
	elif ! grep -qE "^\| *$version *\|" "$readme"; then
		err "$key: $readme has no row for $version in its versions table"
	fi
	t=testdata/checksums/$key
	for f in "$t.buf.gen.yaml" "testdata/checksums/$name/checksums.txt"; do [ -f "$f" ] || err "$key: $f not found"; done
	[ -f "$t.buf.gen.yaml" ] || continue
	for test in $(tests "$t.buf.gen.yaml"); do
		[ -d "testdata/proto/$test" ] || err "$key: testdata/proto/$test not found"
	done
done <<<"$list"

# checksums.txt: the lines expected from the templates ("<version>/<test>", sorted like
# scripts/gen-testdata.sh writes them) against the lines present.
for sums in testdata/checksums/*/*/checksums.txt; do
	d=${sums%/checksums.txt}
	bad=$(grep -nvE '^[0-9a-f]{64}  v[^/[:space:]]+/[A-Za-z0-9._-]+$' "$sums" || true)
	[ -z "$bad" ] || { err "$sums: lines must be '<sha256>  <version>/<test>':"$'\n'"$bad"; continue; }
	sorted=$(awk '{ i = index($2, "/"); print substr($2, 1, i - 1), substr($2, i + 1), $1 }' "$sums" |
		LC_ALL=C sort -k1,1V -k2,2 | awk '{ print $3 "  " $1 "/" $2 }')
	[ "$sorted" = "$(cat "$sums")" ] || err "$sums: lines must be sorted by version (sort -V), then test"
	want=$(for t in "$d"/v*.buf.gen.yaml; do
		[ -f "$t" ] || continue
		v=${t##*/}
		v=${v%.buf.gen.yaml}
		for test in $(tests "$t"); do echo "$v/$test"; done
	done | LC_ALL=C sort)
	have=$(awk '{ print $2 }' "$sums" | LC_ALL=C sort)
	dup=$(printf '%s\n' "$have" | uniq -d)
	[ -z "$dup" ] || err "$sums: duplicate lines for $(printf '%s' "$dup" | tr '\n' ' ')"
	missing=$(LC_ALL=C comm -23 <(printf '%s\n' "$want") <(printf '%s\n' "$have" | uniq) | grep . || true)
	[ -z "$missing" ] || err "$sums: no line for $(printf '%s' "$missing" | tr '\n' ' ') (run scripts/gen-testdata.sh)"
	extra=$(LC_ALL=C comm -13 <(printf '%s\n' "$want") <(printf '%s\n' "$have" | uniq) | grep . || true)
	[ -z "$extra" ] || err "$sums: lines without a template test: $(printf '%s' "$extra" | tr '\n' ' ')"
done

for p in testdata/checksums/*/*/v*.sha256; do
	if [ -f "$p" ]; then err "$p: per-file checksums are replaced by checksums.txt (scripts/gen-testdata.sh)"; fi
done
shopt -s nullglob
for p in generators/*/*/v*/ testdata/checksums/*/*/v*.buf.gen.yaml; do
	key=${p%/}
	key=${key#generators/}
	key=${key#testdata/checksums/}
	key=${key%.buf.gen.yaml}
	printf '%s' "$known" | grep -qxF "$key" && continue
	case $p in
	testdata/*) [ -d "generators/$key" ] || { pending=$pending$key$'\n'; continue; } ;;
	esac
	err "$p: $key is not a generator of $bin (missing generators/$key/CMakeLists.txt?)"
done
pending=$(printf '%s' "$pending" | LC_ALL=C sort -u)
[ -z "$pending" ] || echo "pending (test data without a port): $(printf '%s' "$pending" | tr '\n' ' ')"

[ $fail -eq 0 ] && echo "ok   $(printf '%s\n' "$list" | wc -l | tr -d ' ') generators"
exit $fail
