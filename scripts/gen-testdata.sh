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
# (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855).
#
# Expected errors: an option group may fail (the upstream plugin rejects an option or an input); its
# error text is then tested instead of files. buf aborts the whole run when a plugin fails, so a test
# whose run fails is generated again one group at a time (the template with only that group's plugin
# entry). A group that fails there contributes the line `<sha256 of E>  <group>:error` (no file path
# has this form) to the test sum, sorted with the file lines. E is the error the plugin returned
# (CodeGeneratorResponse.error, or the stderr of a plugin that exited with a non-zero status) without
# buf's prefix, i.e. buf's stderr after `Failure: failed_precondition: plugin "<plugin>" failed: `,
# `Failure: invalid_argument: plugin "<plugin>" exited with non-zero status <n>` + "\nstderr:\n"
# (remote plugins) or `Failure: plugin <plugin>: ` (local plugins; <plugin> is the command, its words
# joined by spaces), with trailing newlines removed and one "\n" appended; a port returns E as its
# CodeGeneratorResponse.error whichever way upstream reports it. Any other failure (network,
# input, a local plugin that crashes or exits with a non-zero status) is an error of this script and
# of the test. The upstream error text goes to testdata/gen/<owner>/<plugin>/<version>/<group>/<test>,
# a file instead of the output directory.
#
# tests/e2e/golden.sh implements the same definitions (filesums, testsums, errtext and entry there).
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

# buf generate TEMPLATE INPUT OUT, retried while buf.build rate-limits remote plugins
# (resource_exhausted) or the connection fails (canceled, unavailable: large inputs often get
# "connection reset by peer"). On failure, buf's stderr is in $tmp/stderr.
generate() {
	local i
	for i in 1 2 3 4 5 6 7 8 9 10; do
		rm -rf "$3"
		if "$BUF" generate "$2" --template "$1" -o "$3" 2>"$tmp/stderr"; then
			return 0
		fi
		if grep -q resource_exhausted "$tmp/stderr"; then
			echo "rate limited, retrying in $((i * 15)) s" >&2
		elif grep -qE '^Failure: (canceled|unavailable): ' "$tmp/stderr"; then
			echo "connection failed, retrying in $((i * 15)) s: $(head -n 1 "$tmp/stderr")" >&2
		else
			return 1
		fi
		sleep $((i * 15))
	done
	return 1
}

# The template with only its K-th plugin entry (1-based), see "Expected errors" above.
entry() {
	awk -v k="$1" '
		/^[^[:space:]#]/ { skip = 0 }
		/^[[:space:]]*-[[:space:]]*(remote|local):/ { n++; skip = (n != k) }
		!skip
	' "$2"
}

# The error text E of a failed plugin named NAME from buf's stderr FILE (see "Expected errors" above);
# fails if FILE is not such an error.
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

# The lines of the test sum: filesums of OUT and "<sha256>  <group>:error" of the error texts
# ERRORS/<group>, sorted by their third field.
testsums() {
	{
		filesums "$1"
		for f in "$2"/*; do
			if [ -f "$f" ]; then printf '%s  %s:error\n' "$(sha256sum <"$f" | cut -c1-64)" "${f##*/}"; fi
		done
	} | LC_ALL=C sort -t ' ' -k3
}

# checksums.txt lines sorted by version (sort -V), then test (byte order).
sortsums() {
	awk '{ i = index($2, "/"); print substr($2, 1, i - 1), substr($2, i + 1), $1 }' |
		LC_ALL=C sort -k1,1V -k2,2 | awk '{ print $3 "  " $1 "/" $2 }'
}

# --- generate -------------------------------------------------------------------------------------
rm -rf "${gen:?}"
mkdir -p "$gen"

nerr=0
for test in "${tests[@]}"; do
	input=testdata/proto/$test
	[ -d "$input" ] || { echo "$input: not found" >&2; exit 1; }
	echo "$name $version: $test" >&2
	out=$tmp/ref/$test errors=$tmp/errors/$test
	if ! generate "$template" "$input" "$out"; then
		# A plugin failed: generate each group alone (see "Expected errors" above).
		mkdir -p "$out" "$errors"
		for k in $(seq "${#groups[@]}"); do
			entry "$k" "$template" >"$tmp/entry.yaml"
			g=$(sed -nE 's/^[[:space:]]*-?[[:space:]]*out:[[:space:]]*([^[:space:]#]+).*/\1/p' "$tmp/entry.yaml")
			if generate "$tmp/entry.yaml" "$input" "$tmp/one"; then
				if [ -d "$tmp/one/$g" ]; then mv "$tmp/one/$g" "$out/$g"; fi
			elif errtext "buf.build/$name:$version" "$tmp/stderr" >"$errors/$g"; then
				echo "  $g: error: $(head -n 1 "$errors/$g")" >&2
				nerr=$((nerr + 1))
			else
				echo "$test, group $g:" >&2
				cat "$tmp/stderr" >&2
				exit 1
			fi
		done
	fi
	echo "$(testsums "$out" "$errors" | sha256sum | cut -c1-64)  $version/$test" >>"$tmp/sums"
	for g in "${groups[@]}"; do
		if [ -d "$out/$g" ]; then
			mkdir -p "$gen/$g"
			mv "$out/$g" "$gen/$g/$test"
		elif [ -f "$errors/$g" ]; then
			mkdir -p "$gen/$g"
			mv "$errors/$g" "$gen/$g/$test"
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

echo "done: $sums ($version: ${#tests[@]} tests, $(find "$gen" -type f | wc -l | tr -d ' ') files, $nerr expected errors), upstream output: $gen" >&2
