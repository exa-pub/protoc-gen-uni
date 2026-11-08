#!/usr/bin/env bash
# Size budget: what each generator adds to the static linux-x86_64 binary, checked against
# testdata/size-budget.txt.
#
#   scripts/size.sh [<build dir prefix>]        (default build/size, inside the repository)
#
# In the toolchain image (scripts/docker.sh; already inside it: `scripts/size.sh --in-image ...`) it
# builds protoc-gen-uni with the linux-x86_64 preset, once per selection, in
# <prefix>/<owner>_<plugin>_<version>/ and <prefix>/all/ (kept, so a second run only rebuilds what
# changed):
#   - the baseline, -DUNI_GENERATORS=bufbuild/es/v2.17.0: UNI_GENERATORS cannot be empty, so the
#     smallest real selection carries what every binary has (pkg/core, pkg/pb, pkg/uni, libc);
#   - every other generator version directory together with the baseline; its delta is the size of
#     that binary minus the baseline, i.e. what the generator adds to a binary (its own code and
#     the libraries it does not share with the baseline);
#   - all generators (the released binary).
# It prints "generator  bytes  delta  budget" and fails when a delta exceeds its budget, a generator
# has no budget line or a budget line names no generator.
#
# testdata/size-budget.txt: lines "<owner>/<plugin>/<version>  <max delta bytes>" and
# "total  <max bytes of the full binary>"; lines starting with # are comments. The baseline's line
# limits its whole binary (its delta over nothing). A budget is set from a measurement + 10%,
# rounded up to 1 KiB, and raised deliberately, in the change that needs it. UNI_SIZE_JOBS sets the
# build parallelism (empty or unset: all CPUs), also when the script re-execs itself in the image.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
baseline=bufbuild/es/v2.17.0
budget_file=testdata/size-budget.txt

in_image=0
if [ "${1:-}" = --in-image ]; then
	in_image=1
	shift
fi
[ $# -le 1 ] || { echo "usage: $0 [--in-image] [<build dir prefix>]" >&2; exit 2; }
prefix=${1:-build/size}
case $prefix in /*) ;; *) prefix=$PWD/$prefix ;; esac
cd "$root"
if [ $in_image -eq 0 ]; then
	# docker.sh passes only a few variables into the container; pass UNI_SIZE_JOBS explicitly.
	exec scripts/docker.sh env UNI_SIZE_JOBS="${UNI_SIZE_JOBS:-}" scripts/size.sh --in-image "$prefix"
fi

jobs=${UNI_SIZE_JOBS:-$(nproc)}
case $jobs in '' | *[!0-9]* | 0) echo "UNI_SIZE_JOBS must be a positive integer: $jobs" >&2; exit 2 ;; esac

# Generator version directories, as generators/CMakeLists.txt discovers them.
keys=$(for f in generators/*/*/v*/CMakeLists.txt; do
	k=${f#generators/}
	echo "${k%/CMakeLists.txt}"
done | LC_ALL=C sort -t/ -k1,2 -k3V)
printf '%s\n' "$keys" | grep -qxF "$baseline" || { echo "baseline $baseline not found" >&2; exit 1; }

errors=()
err() { errors+=("error: $*"); }

# build <dir> <UNI_GENERATORS>: prints the binary size, or nothing when the build fails.
build() {
	local dir=$prefix/$1 log=$prefix/$1.log
	mkdir -p "$prefix"
	if cmake --preset linux-x86_64 -B "$dir" -DUNI_GENERATORS="$2" -DUNI_VERSION=dev >"$log" 2>&1 &&
		cmake --build "$dir" --target protoc-gen-uni -j "$jobs" >>"$log" 2>&1; then
		stat -c %s "$dir/protoc-gen-uni"
	else
		echo "error: build of UNI_GENERATORS=$2 failed ($log):" >&2
		tail -n 30 "$log" >&2
	fi
}

declare -A size budget
for k in $keys; do
	sel=$baseline
	[ "$k" = "$baseline" ] || sel=$baseline,$k
	size[$k]=$(build "${k//\//_}" "$sel")
done
size[total]=$(build all all)

if [ -f "$budget_file" ]; then
	while read -r name max rest; do
		case $name in '' | '#'*) continue ;; esac
		if [ -n "$rest" ] || ! [[ $max =~ ^[0-9]+$ ]]; then
			err "$budget_file: bad line: $name $max $rest"
		elif [ -n "${budget[$name]:-}" ]; then
			err "$budget_file: duplicate line for $name"
		elif [ "$name" != total ] && ! printf '%s\n' "$keys" | grep -qxF "$name"; then
			err "$budget_file: $name is not a generator (generators/$name/CMakeLists.txt not found)"
		else
			budget[$name]=$max
		fi
	done <"$budget_file"
else
	err "$budget_file not found"
fi

base=${size[$baseline]:-}
row() { printf '%-44s %10s %10s %10s  %s\n' "$@"; }
row generator bytes delta budget ""
for k in $keys total; do
	bytes=${size[$k]:-} max=${budget[$k]:-} delta='' note=''
	[ -z "$bytes" ] || [ -z "$base" ] || delta=$((bytes - base))
	checked=$delta
	case $k in
	"$baseline") delta=$bytes checked=$bytes note="(baseline: its whole binary)" ;;
	total) checked=$bytes note="(full binary: bytes)" ;;
	esac
	if [ -z "$checked" ]; then
		err "$k: not measured (build failed)"
		note="FAILED $note"
	elif [ -z "$max" ]; then
		err "$k: no line in $budget_file (add '$k  <bytes>': the measurement + 10%, rounded up to 1 KiB)"
		note="NO BUDGET $note"
	elif [ "$checked" -gt "$max" ]; then
		err "$k: $checked bytes exceed the budget of $max by $((checked - max))"
		note="OVER $note"
	fi
	row "$k" "${bytes:--}" "${delta:--}" "${max:--}" "$note"
done

if [ ${#errors[@]} -ne 0 ]; then
	printf '%s\n' "${errors[@]}"
	echo "a deliberate increase raises $budget_file in the same change"
	exit 1
fi
echo "ok   size budget"
