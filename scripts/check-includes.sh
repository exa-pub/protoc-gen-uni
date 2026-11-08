#!/usr/bin/env bash
# The binary may use only the C library: every `#include <...>` in the C sources must be a header
# from this list (C11, the POSIX/Windows I/O bits used by pkg/core, and libFuzzer/test-only
# headers). Project headers are included with "...". Fails listing the offending lines.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
allowed='assert.h ctype.h errno.h float.h inttypes.h limits.h math.h setjmp.h stdalign.h stdarg.h
stdbool.h stddef.h stdint.h stdio.h stdlib.h stdnoreturn.h string.h
unistd.h fcntl.h io.h sys/types.h sys/stat.h'
# (files deleted or moved in the working tree but still in the index are skipped)
bad=$(git ls-files --cached --others --exclude-standard -- '*.c' '*.h' ':!:ai.tmp/**' |
	while IFS= read -r f; do if [ -f "$f" ]; then printf '%s\n' "$f"; fi; done |
	xargs -r grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*<' |
	while IFS= read -r line; do
		h=$(sed -E 's/.*<([^>]+)>.*/\1/' <<<"$line")
		grep -qxF "$h" <(tr ' ' '\n' <<<"$allowed") || echo "$line"
	done)
if [ -n "$bad" ]; then
	echo "headers outside the allowed C library set (scripts/check-includes.sh):" >&2
	echo "$bad" >&2
	exit 1
fi
