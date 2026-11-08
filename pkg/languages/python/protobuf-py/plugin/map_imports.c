/* Copyright (c) 2025-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* protobuf/plugin/_map_imports.py of protobuf-py v0.7.0. A glob becomes a regular expression
 * matched with re.fullmatch: "**" + "/" becomes ([^/]+/)*, "**" .*, "*" [^/]*, a trailing "/"
 * /.* and any other character itself. The expressions are matched here with a set of positions
 * over the tokens (no backtracking). Matching bytes equals matching code points: the classes are
 * closed under the stars. */
#include "internal.h"

#include <string.h>

enum { T_CHAR, T_STAR, T_ANY, T_SEGS }; /* c, [^/]*, .* (no newline), ([^/]+/)* */

typedef struct {
	int kind;
	char c;
} Tok;

struct MapImport {
	Tok *toks;
	int n;
	const char *target;
};

static const char OPTION_NAME[] = "map_imports";
static const char RUN_FRAME[] = PBPY_FRAME("_run.py", 174, "run", "map_imports=compile_map_imports(fw_opts.map_imports),")
	"                ~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^\n";

static bool target_ok(const char *t)
{
	/* [A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)* */
	for (;;) {
		if (!((*t >= 'A' && *t <= 'Z') || (*t >= 'a' && *t <= 'z') || *t == '_'))
			return false;
		t++;
		while ((*t >= 'A' && *t <= 'Z') || (*t >= 'a' && *t <= 'z') || (*t >= '0' && *t <= '9') || *t == '_')
			t++;
		if (!*t)
			return true;
		if (*t++ != '.')
			return false;
	}
}

COLD PbpyMapImports pbpy_compile_map_imports(const Vec *keys, const Vec *values)
{
	PbpyMapImports m = {keys->n, xalloc(sizeof(struct MapImport) * (size_t)(keys->n ? keys->n : 1))};
	for (int i = 0; i < keys->n; i++) {
		const char *pattern = keys->v[i], *target = values->v[i];
		if (!*pattern)
			pbpy_raise(xcat(RUN_FRAME, PBPY_FRAME("_map_imports.py", 30, "compile_map_imports", "raise ValueError(msg)")),
				"option '%s': pattern must not be empty", OPTION_NAME);
		char *normalized = str_trim_suffix(target, ".");
		if (*normalized && !target_ok(normalized))
			pbpy_raise(xcat(RUN_FRAME, PBPY_FRAME("_map_imports.py", 35, "compile_map_imports", "raise ValueError(msg)")),
				"option '%s': target '%s' must be a Python package path", OPTION_NAME, target);
		size_t n = strlen(pattern);
		Tok *toks = xalloc(sizeof *toks * (2 * n + 1));
		int nt = 0;
		for (size_t k = 0; k < n;) {
			char c = pattern[k];
			if (c == '*') {
				if (pattern[k + 1] == '*') {
					if (pattern[k + 2] == '/') {
						toks[nt++] = (Tok){T_SEGS, 0};
						k += 3;
						continue;
					}
					toks[nt++] = (Tok){T_ANY, 0};
					k += 2;
					continue;
				}
				toks[nt++] = (Tok){T_STAR, 0};
			} else if (c == '/' && k == n - 1) {
				toks[nt++] = (Tok){T_CHAR, '/'};
				toks[nt++] = (Tok){T_ANY, 0};
			} else {
				toks[nt++] = (Tok){T_CHAR, c};
			}
			k++;
		}
		m.v[i] = (struct MapImport){toks, nt, normalized};
	}
	return m;
}

/* re.fullmatch: a[t] = tokens [0, t) matched the input so far; b[t] = inside token t (T_SEGS)
 * after one or more non-slash characters of a segment. */
static bool fullmatch(const struct MapImport *mi, const char *s)
{
	int nt = mi->n;
	bool *a = xalloc((size_t)(nt + 1)), *b = xalloc((size_t)(nt + 1));
	bool *na = xalloc((size_t)(nt + 1)), *nb = xalloc((size_t)(nt + 1));
	a[0] = true;
	size_t len = strlen(s);
	for (size_t i = 0;; i++) {
		/* epsilon closure: the stars may match nothing */
		for (int t = 0; t < nt; t++)
			if (a[t] && mi->toks[t].kind != T_CHAR)
				a[t + 1] = true;
		if (i == len)
			return a[nt];
		char c = s[i];
		memset(na, 0, (size_t)(nt + 1));
		memset(nb, 0, (size_t)(nt + 1));
		bool any = false;
		for (int t = 0; t < nt; t++) {
			const Tok *k = &mi->toks[t];
			if (a[t]) {
				switch (k->kind) {
				case T_CHAR:
					if (c == k->c)
						na[t + 1] = any = true;
					break;
				case T_STAR:
					if (c != '/')
						na[t] = any = true;
					break;
				case T_ANY:
					if (c != '\n')
						na[t] = any = true;
					break;
				case T_SEGS:
					if (c != '/')
						nb[t] = any = true;
					break;
				}
			}
			if (b[t]) {
				if (c != '/')
					nb[t] = any = true;
				else
					na[t] = any = true;
			}
		}
		if (!any)
			return false;
		bool *x = a;
		a = na, na = x;
		x = b, b = nb, nb = x;
	}
}

const char *pbpy_map_import_target(const PbpyMapImports *m, const char *proto_name)
{
	for (int i = 0; i < m->n; i++)
		if (fullmatch(&m->v[i], proto_name))
			return m->v[i].target;
	return NULL;
}
