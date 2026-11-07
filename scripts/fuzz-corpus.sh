#!/usr/bin/env bash
# Builds the seed corpus of fuzz/fuzz_request.c: one file per (option group, test) of
# testdata/checksums/<name>/<version>.buf.gen.yaml, with the input format of the harness:
# "<parameter>\n<CodeGeneratorRequest>". The requests are recorded by running buf with the
# template's inputs and settings (managed mode etc.), its plugin list replaced by a local plugin
# that saves the request. Without NAME VERSION, the corpus of every template (protocolbuffers/go
# gets a few more option sets).
#
#   scripts/fuzz-corpus.sh [NAME VERSION [OUT]]    default OUT: build/fuzz/corpus
#
# Needs buf (scripts/get-buf.sh, BUF overrides) and network access for buf dependencies
# (es-validate; a test whose request cannot be built is skipped with a warning).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
if [ $# -lt 2 ]; then
	for t in testdata/checksums/*/*/v*.buf.gen.yaml; do
		v=$(basename "$t" .buf.gen.yaml) n=${t#testdata/checksums/}
		"$0" "${n%/*}" "$v" ${1:+"$1"}
	done
	exit 0
fi
name=$1 version=$2
out=${3:-$root/build/fuzz/corpus}
template=testdata/checksums/$name/$version.buf.gen.yaml
[ -f "$template" ] || { echo "$template: not found" >&2; exit 1; }
BUF=${BUF:-$("$root/scripts/get-buf.sh")}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The local plugin: saves the request to $1 and answers with an empty response that supports
# proto3 optional and editions 2023..2024 (supported_features=3, minimum_edition=EDITION_PROTO2,
# maximum_edition=EDITION_2024), so that buf accepts any input.
cat >"$tmp/dump" <<'SH'
#!/bin/sh
cat >"$1" && printf '\020\003\030\346\007\040\351\007'
SH
chmod +x "$tmp/dump"

# "<group>=<parameter>" for every plugin entry: `out:` and the `opt:` list joined with ",", as buf
# passes it.
groups=()
while IFS= read -r g; do groups+=("$g"); done < <(awk '
	function flush() { if (out != "") print out "=" opt; out = ""; opt = "" }
	/^[^[:space:]#]/ { if (inplugins) flush(); inplugins = ($0 ~ /^plugins:/); next }
	!inplugins { next }
	/^[[:space:]]*-[[:space:]]*(remote|local):/ { flush(); next }
	/^[[:space:]]*-?[[:space:]]*out:/ { sub(/^[[:space:]]*-?[[:space:]]*out:[[:space:]]*/, ""); sub(/[[:space:]#].*/, ""); out = $0; next }
	/^[[:space:]]*opt:/ {
		sub(/^[[:space:]]*opt:[[:space:]]*\[/, ""); sub(/\][[:space:]]*$/, "")
		n = split($0, a, ","); opt = ""
		for (i = 1; i <= n; i++) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", a[i]); opt = opt (i > 1 ? "," : "") a[i] }
	}
	END { if (inplugins) flush() }
' "$template")
if [ "$name" = protocolbuffers/go ]; then
	groups+=("annotate=paths=source_relative,annotate_code,default_api_level=API_HYBRID"
		"module=module=example.com/gen,annotate_code=true"
		"strip=experimental_strip_nonfunctional_codegen=true,default_api_level=API_OPAQUE"
		"apilevel=apilevelMgofeatures/open.proto=API_HYBRID,Mproto3/proto3.proto=example.com/x;xpb")
fi
[ "${#groups[@]}" -gt 0 ] || { echo "$template: no plugin entries" >&2; exit 1; }

# The template with its plugin list replaced by the dump plugin.
awk -v dump="$tmp/dump" -v req="$tmp/request.binpb" '
	/^[^[:space:]#]/ { skip = 0 }
	/^plugins:/ { print; print "  - local: [\"" dump "\", \"" req "\"]"; print "    strategy: all"; print "    out: ."; skip = 1; next }
	!skip { print }
' "$template" >"$tmp/dump.buf.gen.yaml"

mkdir -p "$out"
slug=${name//\//-}
n=0
for test in $(sed -nE 's|^[[:space:]]*-[[:space:]]*directory:[[:space:]]*testdata/proto/([A-Za-z0-9._-]+)[[:space:]]*$|\1|p' "$template"); do
	rm -f "$tmp/request.binpb"
	if ! "$BUF" generate "testdata/proto/$test" --template "$tmp/dump.buf.gen.yaml" -o "$tmp/gen" 2>"$tmp/err"; then
		echo "warning: $name $version: skipped $test: $(head -c 300 "$tmp/err")" >&2
		continue
	fi
	for g in "${groups[@]}"; do
		param="uni-name=$name,uni-version=$version,${g#*=}"
		{ printf '%s\n' "${param%,}"; cat "$tmp/request.binpb"; } >"$out/$slug-${g%%=*}-$test"
		n=$((n + 1))
	done
done
echo "$name $version: $n seeds in $out" >&2
