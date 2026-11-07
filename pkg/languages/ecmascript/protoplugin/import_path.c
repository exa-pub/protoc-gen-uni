/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/protoplugin/src/import-path.ts and map-imports.ts. Upstream compiles the
 * patterns of rewrite_imports (starToRegExp) and map_imports (globToRegExp) to regular
 * expressions; they are matched directly here. */
#include "internal.h"

#include <string.h>

/* /^\.{1,2}\// */
bool is_relative_path(const char *p) { return has_prefix(p, "./") || has_prefix(p, "../"); }

/* Matches s against a pattern like the regular expression upstream builds from it:
 *   "**" + "/"      ([^\/]+\/)*   any number of non-empty "segment/"
 *   "**"            .*            glob only: anything but a line feed
 *   "*"             [^\/]*        anything within a segment
 *   "/" at the end  \/.*          glob only: everything in the directory
 * Everything else matches literally. The pattern is compiled to tokens and simulated as an NFA,
 * so matching takes linear time like the regular expression (no backtracking). */
enum { T_LIT, T_STAR, T_ANY, T_SEG_START, T_SEG_IN };
typedef struct {
	uint8_t kind;
	char c;
} Tok;

static int compile_pattern(const char *p, bool glob, Tok *t)
{
	int n = 0;
	while (*p) {
		if (p[0] == '*' && p[1] == '*' && p[2] == '/') {
			t[n++] = (Tok){T_SEG_START, 0};
			t[n++] = (Tok){T_SEG_IN, 0};
			p += 3;
		} else if (glob && p[0] == '*' && p[1] == '*') {
			t[n++] = (Tok){T_ANY, 0};
			p += 2;
		} else if (p[0] == '*') {
			t[n++] = (Tok){T_STAR, 0};
			p++;
		} else if (glob && p[0] == '/' && !p[1]) {
			t[n++] = (Tok){T_LIT, '/'};
			t[n++] = (Tok){T_ANY, 0};
			p++;
		} else {
			t[n++] = (Tok){T_LIT, *p++};
		}
	}
	return n;
}

/* Adds the states reachable without input; all such edges go forward. */
static void closure(const Tok *t, int n, bool *set)
{
	for (int i = 0; i < n; i++) {
		if (!set[i])
			continue;
		if (t[i].kind == T_STAR || t[i].kind == T_ANY)
			set[i + 1] = true;
		else if (t[i].kind == T_SEG_START)
			set[i + 2] = true;
	}
}

bool import_pattern_match(const char *pattern, const char *s, bool glob)
{
	Tok *t = xalloc(sizeof(Tok) * (2 * strlen(pattern) + 1));
	int n = compile_pattern(pattern, glob, t);
	bool *cur = xalloc((size_t)n + 1), *next = xalloc((size_t)n + 1);
	cur[0] = true;
	closure(t, n, cur);
	for (; *s; s++) {
		char c = *s;
		memset(next, 0, (size_t)n + 1);
		bool any = false;
		for (int i = 0; i < n; i++) {
			if (!cur[i])
				continue;
			int to = -1;
			switch (t[i].kind) {
			case T_LIT: to = c == t[i].c ? i + 1 : -1; break;
			case T_STAR: to = c != '/' ? i : -1; break;
			case T_ANY: to = c != '\n' ? i : -1; break;
			case T_SEG_START: to = c != '/' ? i + 1 : -1; break;
			case T_SEG_IN: to = c != '/' ? i : i - 1; break;
			}
			if (to >= 0)
				next[to] = any = true;
		}
		if (!any)
			return false;
		closure(t, n, next);
		bool *tmp = cur;
		cur = next, next = tmp;
	}
	return cur[n];
}

/* Applies rewrite_imports to an import path, and changes the .js extension of relative import
 * paths to the import_extension. */
const char *rewrite_import_path(const Schema *sc, const char *path)
{
	for (int i = 0; i < sc->opts.rewrite_imports.n; i++) {
		const PatternTarget *ri = sc->opts.rewrite_imports.v[i];
		if (import_pattern_match(ri->pattern, path, false)) {
			if (is_relative_path(path)) {
				const char *rest = path + (path[1] == '/' ? 1 : 2); /* "/..." */
				path = xcat(str_trim_suffix(ri->target, "/"), rest);
			} else {
				path = ri->target;
			}
			break;
		}
	}
	if (is_relative_path(path) && has_suffix(path, ".js")) {
		const char *ext = sc->opts.import_extension;
		if (str_eq(ext, "none"))
			return xstrndup(path, strlen(path) - 3);
		return xcat(xstrndup(path, strlen(path) - 2), ext);
	}
	return path;
}

/* Derives an ECMAScript module import path from a file path: `foo/bar.ts` becomes
 * `./foo/bar.js`. */
const char *derive_import_path(const char *filename)
{
	const char *p = filename;
	size_t n = strlen(p);
	/* /\.(js|ts|d.ts)$/: the leftmost match wins, so ".d.ts" before ".ts" (the second dot is
	 * unescaped upstream and matches any character but a line feed) */
	if (n >= 5 && p[n - 5] == '.' && p[n - 4] == 'd' && p[n - 3] != '\n' && p[n - 2] == 't' && p[n - 1] == 's')
		p = xcat(xstrndup(p, n - 5), ".js");
	else if (has_suffix(p, ".js") || has_suffix(p, ".ts"))
		p = xcat(xstrndup(p, n - 3), ".js");
	return is_relative_path(p) ? p : xcat("./", p);
}

static Vec non_empty_segments(const char *s)
{
	Vec all = str_split(s, '/'), out = {0};
	for (int i = 0; i < all.n; i++)
		if (*(char *)all.v[i])
			vec_push(&out, all.v[i]);
	return out;
}

/* Makes an import path relative to the file importing it. */
const char *make_import_path_relative(const char *importer, const char *path)
{
	if (!is_relative_path(path))
		return path; /* We don't touch absolute imports, like @bufbuild/protobuf */
	Vec a = non_empty_segments(has_prefix(importer, "./") ? importer + 2 : importer);
	if (a.n > 0)
		a.n--;
	Vec b = non_empty_segments(has_prefix(path, "./") ? path + 2 : path);
	int m = 0;
	while (m < a.n && m < b.n && str_eq(a.v[m], b.v[m]))
		m++;
	Vec parts = {0};
	for (int i = m; i < a.n; i++)
		vec_push(&parts, "..");
	for (int i = m; i < b.n; i++)
		vec_push(&parts, b.v[i]);
	const char *c = str_join(&parts, "/");
	return is_relative_path(c) ? c : xcat("./", c);
}
