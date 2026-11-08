/* Node.js `path` (posix), lib/path.js of Node.js v22: normalize, resolve, relative, dirname and
 * basename, as used by code generators written for Node.js (ts-poet's import paths).
 *
 * Node.js: Copyright Joyent, Inc. and other Node contributors, MIT License.
 *
 * The working directory of `resolve` is "/": generators only resolve relative paths that do not
 * climb above it, where the result does not depend on the working directory. */
#include "es_std.h"

#include <string.h>

/* normalizeString(path, allowAboveRoot, "/", isPosixPathSeparator) */
static char *normalize_string(const char *path, bool allow_above_root)
{
	Buf res = {0};
	size_t n = strlen(path);
	size_t last_segment_length = 0;
	long last_slash = -1;
	int dots = 0;
	char code = 0;
	for (size_t i = 0; i <= n; ++i) {
		if (i < n)
			code = path[i];
		else if (code == '/')
			break;
		else
			code = '/';
		if (code == '/') {
			if (last_slash == (long)i - 1 || dots == 1) {
				/* NOOP */
			} else if (dots == 2) {
				if (res.n < 2 || last_segment_length != 2 || res.p[res.n - 1] != '.' || res.p[res.n - 2] != '.') {
					if (res.n > 2) {
						char *ls = NULL;
						for (size_t k = res.n; k-- > 0;)
							if (res.p[k] == '/') {
								ls = res.p + k;
								break;
							}
						if (!ls) {
							res.n = 0;
							last_segment_length = 0;
						} else {
							res.n = (size_t)(ls - res.p);
							res.p[res.n] = 0;
							/* res.length - 1 - res.lastIndexOf("/") */
							long k = (long)res.n - 1;
							while (k >= 0 && res.p[k] != '/')
								k--;
							last_segment_length = (size_t)((long)res.n - 1 - k);
						}
						last_slash = (long)i;
						dots = 0;
						continue;
					} else if (res.n != 0) {
						res.n = 0;
						last_segment_length = 0;
						last_slash = (long)i;
						dots = 0;
						continue;
					}
				}
				if (allow_above_root) {
					buf_adds(&res, res.n > 0 ? "/.." : "..");
					last_segment_length = 2;
				}
			} else {
				if (res.n > 0)
					buf_addc(&res, '/');
				buf_add(&res, path + last_slash + 1, i - (size_t)(last_slash + 1));
				last_segment_length = i - (size_t)last_slash - 1;
			}
			last_slash = (long)i;
			dots = 0;
		} else if (code == '.' && dots != -1) {
			++dots;
		} else {
			dots = -1;
		}
	}
	return xstrndup(res.p ? res.p : "", res.n);
}

char *node_path_normalize(const char *path)
{
	size_t n = strlen(path);
	if (n == 0)
		return xstrdup(".");
	bool absolute = path[0] == '/';
	bool trailing = path[n - 1] == '/';
	char *p = normalize_string(path, !absolute);
	if (!*p) {
		if (absolute)
			return xstrdup("/");
		return xstrdup(trailing ? "./" : ".");
	}
	if (trailing)
		p = xcat(p, "/");
	return absolute ? xcat("/", p) : p;
}

char *node_path_resolve(const char *path)
{
	/* resolve(path) with process.cwd() = "/" */
	char *resolved = path[0] == '/' ? xcat(path, "/") : *path ? xcat("/", path, "/") : xstrdup("//");
	return xcat("/", normalize_string(resolved, false));
}

char *node_path_relative(const char *from, const char *to)
{
	if (str_eq(from, to))
		return xstrdup("");
	from = node_path_resolve(from);
	to = node_path_resolve(to);
	if (str_eq(from, to))
		return xstrdup("");
	size_t from_start = 1, from_end = strlen(from), from_len = from_end - from_start;
	size_t to_start = 1, to_len = strlen(to) - to_start;
	size_t length = from_len < to_len ? from_len : to_len;
	long last_common_sep = -1;
	size_t i = 0;
	for (; i < length; i++) {
		char c = from[from_start + i];
		if (c != to[to_start + i])
			break;
		else if (c == '/')
			last_common_sep = (long)i;
	}
	if (i == length) {
		if (to_len > length) {
			if (to[to_start + i] == '/')
				return xstrdup(to + to_start + i + 1);
			if (i == 0)
				return xstrdup(to + to_start + i);
		} else if (from_len > length) {
			if (from[from_start + i] == '/')
				last_common_sep = (long)i;
			else if (i == 0)
				last_common_sep = 0;
		}
	}
	Buf out = {0};
	for (i = from_start + (size_t)(last_common_sep + 1); i <= from_end; ++i)
		if (i == from_end || from[i] == '/')
			buf_adds(&out, out.n == 0 ? ".." : "/..");
	buf_adds(&out, to + to_start + last_common_sep);
	return xstrdup(buf_str(&out));
}

char *node_path_dirname(const char *path)
{
	size_t n = strlen(path);
	if (n == 0)
		return xstrdup(".");
	bool has_root = path[0] == '/';
	long end = -1;
	bool matched_slash = true;
	for (size_t i = n - 1; i >= 1; --i) {
		if (path[i] == '/') {
			if (!matched_slash) {
				end = (long)i;
				break;
			}
		} else {
			matched_slash = false;
		}
	}
	if (end == -1)
		return xstrdup(has_root ? "/" : ".");
	if (has_root && end == 1)
		return xstrdup("//");
	return xstrndup(path, (size_t)end);
}

char *node_path_basename(const char *path, const char *suffix)
{
	size_t n = strlen(path);
	long start = 0, end = -1;
	bool matched_slash = true;
	size_t sn = suffix ? strlen(suffix) : 0;
	if (sn > 0 && sn <= n) {
		if (str_eq(suffix, path))
			return xstrdup("");
		long ext_idx = (long)sn - 1;
		long first_non_slash_end = -1;
		for (long i = (long)n - 1; i >= 0; --i) {
			char c = path[i];
			if (c == '/') {
				if (!matched_slash) {
					start = i + 1;
					break;
				}
			} else {
				if (first_non_slash_end == -1) {
					matched_slash = false;
					first_non_slash_end = i + 1;
				}
				if (ext_idx >= 0) {
					if (c == suffix[ext_idx]) {
						if (--ext_idx == -1)
							end = i;
					} else {
						ext_idx = -1;
						end = first_non_slash_end;
					}
				}
			}
		}
		if (start == end)
			end = first_non_slash_end;
		else if (end == -1)
			end = (long)n;
		return xstrndup(path + start, (size_t)(end - start));
	}
	for (long i = (long)n - 1; i >= 0; --i) {
		if (path[i] == '/') {
			if (!matched_slash) {
				start = i + 1;
				break;
			}
		} else if (end == -1) {
			matched_slash = false;
			end = i + 1;
		}
	}
	if (end == -1)
		return xstrdup("");
	return xstrndup(path + start, (size_t)(end - start));
}
