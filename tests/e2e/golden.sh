#!/usr/bin/env bash
# End-to-end golden test of one generator version: buf runs the plugin binary as a local plugin on
# testdata/proto/<test> with the option groups of testdata/checksums/<name>/<version>.buf.gen.yaml,
# and the sha256 of every generated file is compared with testdata/checksums/<name>/<version>.sha256
# (the checksums of the upstream plugin output, see scripts/gen-testdata.sh).
#
#   tests/e2e/golden.sh [--runner CMD] BIN NAME VERSION [TEST...]
#
#   BIN       plugin binary (any path; made absolute)
#   NAME      <owner>/<plugin>, e.g. bufbuild/es
#   VERSION   e.g. v2.17.0
#   TEST      tests to run (default: all `inputs:` of the template)
#   --runner  command that runs BIN, e.g. "qemu-aarch64" for a cross-built binary
#
# Env: BUF (default: scripts/get-buf.sh), UNI_E2E_WORK (scratch dir, default: temporary),
# UNI_DIFF_LINES (diff lines shown per file, default 60).
#
# On a mismatch, if testdata/checksums/<name>/gen/<version>/ exists (the upstream output written by
# scripts/gen-testdata.sh), differing files are diffed against it.
#
# Exit status: 0 ok, 1 failure, 77 the binary does not contain NAME VERSION (`BIN --list`), 2 usage.
set -euo pipefail

runner=()
if [ "${1:-}" = --runner ]; then
	read -r -a runner <<<"$2"
	shift 2
fi
if [ $# -lt 3 ]; then
	echo "usage: $0 [--runner CMD] BIN NAME VERSION [TEST...]" >&2
	exit 2
fi
bin=$1 name=$2 version=$3
shift 3
case $bin in /*) ;; *) bin=$PWD/$bin ;; esac

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
d=testdata/checksums/$name
template=$d/$version.buf.gen.yaml
sums=$d/$version.sha256
gen=$d/gen/$version
lines=${UNI_DIFF_LINES:-60}
for f in "$template" "$sums"; do [ -f "$f" ] || { echo "$f: not found" >&2; exit 1; }; done
[ -x "$bin" ] || [ -f "$bin" ] || { echo "$bin: not found" >&2; exit 1; }

sha256() { if command -v sha256sum >/dev/null; then sha256sum -- "$@"; else shasum -a 256 -- "$@"; fi; }
# Paths passed to buf: native Windows paths under Git Bash/MSYS (buf is a Windows program there).
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }

# Skip generators the binary does not contain (`--list` prints "<name> <version>" lines; a binary
# without --list is assumed to contain everything).
if list=$(${runner[@]+"${runner[@]}"} "$bin" --list 2>/dev/null); then
	if ! printf '%s\n' "$list" | grep -qxF "$name $version"; then
		echo "skip: $name $version is not in $bin"
		exit 77
	fi
fi

if [ $# -eq 0 ]; then
	# shellcheck disable=SC2046 # test names are plain words
	set -- $(sed -nE 's|^[[:space:]]*-[[:space:]]*directory:[[:space:]]*testdata/proto/([A-Za-z0-9._-]+)[[:space:]]*$|\1|p' "$template")
	[ $# -gt 0 ] || { echo "$template: no tests in inputs:" >&2; exit 1; }
fi

BUF=${BUF:-$("$root/scripts/get-buf.sh")}
if [ -n "${UNI_E2E_WORK:-}" ]; then
	work=$UNI_E2E_WORK
	mkdir -p "$work"
else
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
fi

# The template with every `remote:` entry replaced by the local binary, uni-name/uni-version
# prepended to its options, and `strategy: all` (one request with all files, like remote plugins).
local_cmd=
for w in ${runner[@]+"${runner[@]}"} "$(native "$bin")"; do local_cmd="$local_cmd${local_cmd:+, }\"$w\""; done
tmpl=$work/$(echo "$name" | tr / _)_$version.buf.gen.yaml
awk -v local="[$local_cmd]" -v uni="uni-name=$name, uni-version=$version" '
	function flush() { if (pending) print ind "  opt: [" uni "]"; pending = 0 }
	/^[[:space:]]*-[[:space:]]*remote:/ {
		flush(); match($0, /^[[:space:]]*/); ind = substr($0, 1, RLENGTH)
		print ind "- local: " local; print ind "  strategy: all"; pending = 1; next
	}
	pending && /^[[:space:]]*opt:/ {
		if ($0 !~ /^[[:space:]]*opt:[[:space:]]*\[[^]]*[^][:space:]][^]]*\][[:space:]]*$/) {
			print FILENAME ": opt must be a non-empty one-line list: " $0 > "/dev/stderr"; exit 1
		}
		sub(/\[/, "[" uni ", "); print; pending = 0; next
	}
	pending && /^[^[:space:]]/ { flush() }
	{ print }
	END { flush() }
' "$template" >"$tmpl"

fail=0
for test in "$@"; do
	[ -d "testdata/proto/$test" ] || { echo "FAIL $test: testdata/proto/$test not found"; fail=1; continue; }
	out=$work/$test
	rm -rf "$out"
	if ! "$BUF" generate "testdata/proto/$test" --template "$(native "$tmpl")" -o "$(native "$out")" >"$work/$test.log" 2>&1; then
		echo "FAIL $test: buf generate failed:"
		sed 's/^/    /' "$work/$test.log"
		fail=1
		continue
	fi
	# "<sha>  <group>/<test>/<path>", sorted by path
	actual=$work/$test.actual expected=$work/$test.expected
	(
		[ -d "$out" ] || exit 0
		cd "$out"
		find . -type f | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r f; do
			sha256 "$f"
		done | awk -v t="$test" '{ p = substr($0, index($0, "  ") + 2); i = index(p, "/"); print $1 "  " substr(p, 1, i) t "/" substr(p, i + 1) }'
	) | LC_ALL=C sort -k2 >"$actual"
	awk -v t="$test" '{ split($2, a, "/"); if (a[2] == t) print }' "$sums" | LC_ALL=C sort -k2 >"$expected"
	if cmp -s "$expected" "$actual"; then
		echo "ok   $test ($(wc -l <"$actual" | tr -d ' ') files)"
		continue
	fi
	fail=1
	echo "FAIL $test:"
	awk 'NR == FNR { e[$2] = $1; next } { a[$2] = $1 }
		END {
			for (p in e) if (!(p in a)) print "missing " p; else if (e[p] != a[p]) print "differs " p
			for (p in a) if (!(p in e)) print "extra   " p
		}' "$expected" "$actual" | LC_ALL=C sort -k2 | while read -r kind p; do
		echo "    $kind $p"
		[ "$kind" = differs ] || continue
		group=${p%%/*} rest=${p#*/}
		rest=${rest#*/}
		if [ ! -f "$gen/$p" ]; then
			continue
		fi
		if [ "$(sha256 "$gen/$p" | cut -d' ' -f1)" != "$(awk -v p="$p" '$2 == p { print $1 }' "$expected")" ]; then
			echo "      $gen is stale (does not match $sums), rerun scripts/gen-testdata.sh"
			continue
		fi
		diff -u "$gen/$p" "$out/$group/$rest" | head -n "$lines" | sed 's/^/      /' || true
	done
	[ -d "$gen" ] || echo "    (run scripts/gen-testdata.sh $name $version to get diffs)"
done
exit $fail
