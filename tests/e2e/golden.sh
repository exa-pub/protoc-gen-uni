#!/usr/bin/env bash
# End-to-end golden test of one generator version: buf runs the plugin binary as a local plugin on
# testdata/proto/<test> with the option groups of testdata/checksums/<name>/<version>.buf.gen.yaml,
# and the sum of the output of each test is compared with its line `<sum>  <version>/<test>` in
# testdata/checksums/<name>/checksums.txt (the sums of the upstream plugin output; the sum and the
# expected errors of failing groups are defined in scripts/gen-testdata.sh and computed the same way
# here, see filesums, testsums, errtext and entry).
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
# On a mismatch, if testdata/gen/<name>/<version>/ exists (the upstream output written by
# scripts/gen-testdata.sh), the files are compared with it and differing files are diffed.
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
sums=$d/checksums.txt
gen=testdata/gen/$name/$version
lines=${UNI_DIFF_LINES:-60}
for f in "$template" "$sums"; do [ -f "$f" ] || { echo "$f: not found" >&2; exit 1; }; done
[ -x "$bin" ] || [ -f "$bin" ] || { echo "$bin: not found" >&2; exit 1; }

# sha256 of stdin, lowercase hex.
sha256() { if command -v sha256sum >/dev/null; then sha256sum; else shasum -a 256; fi | cut -c1-64; }
# "<sha256>  <group>/<path>" of the files DIR/<group>/<path>, sorted by "<group>/<path>" in byte order:
# the lines of the test sum (scripts/gen-testdata.sh), which is the sha256 of their text.
filesums() {
	(
		cd "$1" 2>/dev/null || exit 0
		find . -type f | sed 's|^\./||' | while IFS= read -r f; do
			printf '%s  %s\n' "$(sha256 <"$f")" "$f"
		done | LC_ALL=C sort -t ' ' -k3
	)
}
# The lines of the test sum: filesums of OUT and "<sha256>  <group>:error" of the error texts
# ERRORS/<group>, sorted by their third field.
testsums() {
	{
		filesums "$1"
		for f in "$2"/*; do
			if [ -f "$f" ]; then printf '%s  %s:error\n' "$(sha256 <"$f")" "${f##*/}"; fi
		done
	} | LC_ALL=C sort -t ' ' -k3
}
# The template with only its K-th plugin entry (1-based).
entry() {
	awk -v k="$1" '
		/^[^[:space:]#]/ { skip = 0 }
		/^[[:space:]]*-[[:space:]]*(remote|local):/ { n++; skip = (n != k) }
		!skip
	' "$2"
}
# The error text of a failed plugin named NAME from buf's stderr FILE; fails if FILE is not such an
# error (a crash, a non-zero exit status of the local plugin, any other failure).
errtext() {
	name=$1 awk '
		{ t = t $0 "\n" }
		END {
			n = ENVIRON["name"]
			p1 = "Failure: failed_precondition: plugin \"" n "\" failed: "
			p2 = "Failure: invalid_argument: plugin \"" n "\" exited with non-zero status "
			p3 = "Failure: plugin " n ": "
			if (index(t, p1) == 1) {
				t = substr(t, length(p1) + 1)
			} else if (index(t, p2) == 1) {
				t = substr(t, length(p2) + 1)
				i = index(t, "\n")
				if (substr(t, 1, i - 1) !~ /^[0-9]+$/) exit 1
				t = substr(t, i + 1)
				if (t != "" && index(t, "stderr:\n") != 1) exit 1
				t = substr(t, 9)
			} else if (index(t, p3) == 1) {
				t = substr(t, length(p3) + 1)
				if (t ~ /^(exit status [0-9]+|signal: [^\n]*)\n$/) exit 1
			} else {
				exit 1
			}
			sub(/\n+$/, "", t)
			printf "%s\n", t
		}
	' "$2"
}
# Paths passed to buf: native Windows paths under Git Bash/MSYS (buf is a Windows program there).
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }

# Skip generators the binary does not contain (`--list` prints "<name> <version>" lines; a binary
# without --list is assumed to contain everything).
if list=$(${runner[@]+"${runner[@]}"} "$bin" --list 2>/dev/null); then
	if ! grep -qxF "$name $version" <<<"$list"; then
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

# The template (stdin) with every `remote:` entry replaced by the local binary, uni-name/uni-version
# prepended to its options, and `strategy: all` (one request with all files, like remote plugins).
# buf names the local plugin by its command words joined by spaces (local_name, for errtext).
local_cmd='' local_name=''
for w in ${runner[@]+"${runner[@]}"} "$(native "$bin")"; do
	local_cmd="$local_cmd${local_cmd:+, }\"$w\""
	local_name="$local_name${local_name:+ }$w"
done
to_local() {
	awk -v src="$template" -v local="[$local_cmd]" -v uni="uni-name=$name, uni-version=$version" '
		function flush() { if (pending) print ind "  opt: [" uni "]"; pending = 0 }
		/^[[:space:]]*-[[:space:]]*remote:/ {
			flush(); match($0, /^[[:space:]]*/); ind = substr($0, 1, RLENGTH)
			print ind "- local: " local; print ind "  strategy: all"; pending = 1; next
		}
		pending && /^[[:space:]]*opt:/ {
			if ($0 !~ /^[[:space:]]*opt:[[:space:]]*\[[^]]*[^][:space:]][^]]*\][[:space:]]*$/) {
				print src ": opt must be a non-empty one-line list: " $0 > "/dev/stderr"; exit 1
			}
			sub(/\[/, "[" uni ", "); print; pending = 0; next
		}
		pending && /^[^[:space:]]/ { flush() }
		{ print }
		END { flush() }
	'
}
tmpl=$work/$(echo "$name" | tr / _)_$version.buf.gen.yaml
to_local <"$template" >"$tmpl"
ngroups=$(grep -cE '^[[:space:]]*-[[:space:]]*remote:' "$template" || true)

fail=0
for test in "$@"; do
	[ -d "testdata/proto/$test" ] || { echo "FAIL $test: testdata/proto/$test not found"; fail=1; continue; }
	out=$work/$test errors=$work/$test.errors
	rm -rf "$out" "$errors"
	if ! "$BUF" generate "testdata/proto/$test" --template "$(native "$tmpl")" -o "$(native "$out")" >"$work/$test.log" 2>&1; then
		# A plugin failed: generate each group alone (expected errors, scripts/gen-testdata.sh).
		rm -rf "$out"
		mkdir -p "$out" "$errors"
		k=0 ok=1
		while [ $k -lt "$ngroups" ]; do
			k=$((k + 1))
			entry "$k" "$template" | to_local >"$tmpl.$k.yaml"
			g=$(sed -nE 's/^[[:space:]]*-?[[:space:]]*out:[[:space:]]*([^[:space:]#]+).*/\1/p' "$tmpl.$k.yaml")
			if "$BUF" generate "testdata/proto/$test" --template "$(native "$tmpl.$k.yaml")" -o "$(native "$out")" >"$work/$test.log" 2>&1; then
				continue
			fi
			if ! errtext "$local_name" "$work/$test.log" >"$errors/$g"; then
				echo "FAIL $test: buf generate failed (group $g):"
				sed 's/^/    /' "$work/$test.log"
				ok=0
				break
			fi
		done
		[ $ok = 1 ] || { fail=1; continue; }
	fi
	expected=$(awk -v k="$version/$test" '$2 == k { print $1 }' "$sums")
	actual=$work/$test.actual
	testsums "$out" "$errors" >"$actual"
	sum=$(sha256 <"$actual")
	if [ -z "$expected" ]; then
		echo "FAIL $test: no line for $version/$test in $sums (run scripts/gen-testdata.sh $name $version)"
		fail=1
		continue
	fi
	if [ "$sum" = "$expected" ]; then
		nerr=$(grep -cE '^[0-9a-f]{64}  [A-Za-z0-9._-]+:error$' "$actual" || true)
		if [ "$nerr" = 0 ]; then
			echo "ok   $test ($(wc -l <"$actual" | tr -d ' ') files)"
		else
			echo "ok   $test ($(($(wc -l <"$actual") - nerr)) files, $nerr expected errors)"
		fi
		continue
	fi
	fail=1
	echo "FAIL $test: sum $sum, expected $expected ($sums)"
	if [ ! -d "$gen" ]; then
		for f in "$errors"/*; do
			if [ -f "$f" ]; then echo "    error ${f##*/}:"; sed 's/^/      /' "$f"; fi
		done
		echo "    (run scripts/gen-testdata.sh $name $version to get diffs against the upstream output)"
		continue
	fi
	# The upstream lines from $gen/<group>/<test>/<path>, or $gen/<group>/<test> for an error.
	upstream=$work/$test.upstream
	for g in "$gen"/*/; do
		g=${g%/}
		g=${g##*/}
		if [ -f "$gen/$g/$test" ]; then
			printf '%s  %s:error\n' "$(sha256 <"$gen/$g/$test")" "$g"
		else
			filesums "$gen/$g/$test" | awk -v g="$g" '{ i = index($0, "  "); print substr($0, 1, i + 1) g "/" substr($0, i + 2) }'
		fi
	done | LC_ALL=C sort -t ' ' -k3 >"$upstream"
	if [ "$(sha256 <"$upstream")" != "$expected" ]; then
		echo "    $gen is stale (does not match $sums), rerun scripts/gen-testdata.sh $name $version"
		continue
	fi
	awk 'NR == FNR { e[$2] = $1; next } { a[$2] = $1 }
		END {
			for (p in e) if (!(p in a)) print "missing " p; else if (e[p] != a[p]) print "differs " p
			for (p in a) if (!(p in e)) print "extra   " p
		}' "$upstream" "$actual" | LC_ALL=C sort -b -k2 | while read -r kind p; do
		echo "    $kind $p"
		case $kind:$p in
		differs:*:error) diff -u "$gen/${p%:error}/$test" "$errors/${p%:error}" ;;
		missing:*:error) sed 's/^/  /' "$gen/${p%:error}/$test" ;;
		extra:*:error) sed 's/^/  /' "$errors/${p%:error}" ;;
		differs:*) diff -u "$gen/${p%%/*}/$test/${p#*/}" "$out/$p" ;;
		*) ;;
		esac | head -n "$lines" | sed 's/^/      /' || true
	done
done
exit $fail
