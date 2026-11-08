/* package:path 1.9.1 (https://github.com/dart-lang/core/tree/main/pkgs/path, lib/src/context.dart,
 * parsed_path.dart, style/posix.dart; BSD-3-Clause, Copyright the Dart project authors): a
 * `Context(style: Style.posix)`, whose current directory is ".". Paths are compared and split on
 * ASCII '/' and '.', so UTF-8 bytes behave like Dart's UTF-16 code units. */
#include "dart_path.h"

#include <string.h>

/* ParsedPath: root ("/" or NULL), parts and the separators around them (n + 1, "" or "/"). */
typedef struct {
	const char *root;
	const char **parts, **seps;
	int n, cap;
} DpPath;

static void dp_reserve(DpPath *p, int n)
{
	if (n + 1 <= p->cap)
		return;
	int cap = p->cap ? p->cap : 4;
	while (cap < n + 1)
		cap *= 2;
	p->parts = xrealloc(p->parts, sizeof *p->parts * (size_t)p->cap, sizeof *p->parts * (size_t)cap);
	p->seps = xrealloc(p->seps, sizeof *p->seps * (size_t)p->cap, sizeof *p->seps * (size_t)cap);
	p->cap = cap;
}

static DpPath dp_parse(const char *path)
{
	DpPath p = {0};
	if (*path == '/')
		p.root = "/", path++;
	dp_reserve(&p, 1);
	size_t start = 0, len = strlen(path);
	p.seps[0] = "";
	if (len && path[0] == '/')
		p.seps[0] = "/", start = 1;
	for (size_t i = start; i < len; i++)
		if (path[i] == '/') {
			dp_reserve(&p, p.n + 1);
			p.parts[p.n++] = xstrndup(path + start, i - start);
			p.seps[p.n] = "/";
			start = i + 1;
		}
	if (start < len) {
		dp_reserve(&p, p.n + 1);
		p.parts[p.n++] = xstrdup(path + start);
		p.seps[p.n] = "";
	}
	return p;
}

static char *dp_to_string(const DpPath *p)
{
	Buf b = {0};
	if (p->root)
		buf_adds(&b, p->root);
	for (int i = 0; i < p->n; i++) {
		buf_adds(&b, p->seps[i]);
		buf_adds(&b, p->parts[i]);
	}
	buf_adds(&b, p->seps[p->n]);
	return (char *)buf_str(&b);
}

static void dp_remove_trailing_separators(DpPath *p)
{
	while (p->n > 0 && !*p->parts[p->n - 1])
		p->n--;
	p->seps[p->n] = "";
}

/* ParsedPath.normalize (canonicalize false) */
static void dp_normalize(DpPath *p)
{
	int leading = 0, m = 0;
	const char **parts = xalloc(sizeof *parts * (size_t)(p->n + 1));
	for (int i = 0; i < p->n; i++) {
		const char *s = p->parts[i];
		if (!*s || str_eq(s, "."))
			continue;
		if (str_eq(s, "..")) {
			if (m > 0)
				m--;
			else
				leading++;
			continue;
		}
		parts[m++] = s;
	}
	DpPath r = {.root = p->root};
	int total = (p->root ? 0 : leading) + m;
	dp_reserve(&r, total + 1);
	if (!p->root)
		for (int i = 0; i < leading; i++)
			r.parts[r.n++] = "..";
	for (int i = 0; i < m; i++)
		r.parts[r.n++] = parts[i];
	if (r.n == 0 && !p->root)
		r.parts[r.n++] = ".";
	for (int i = 0; i <= r.n; i++)
		r.seps[i] = "/";
	r.seps[0] = ""; /* the posix root "/" needs no separator */
	dp_remove_trailing_separators(&r);
	*p = r;
}

/* ParsedPath._splitExtension()[0] */
static const char *dp_basename_without_extension(const DpPath *p)
{
	const char *file = NULL;
	for (int i = p->n - 1; i >= 0 && !file; i--)
		if (*p->parts[i])
			file = p->parts[i];
	if (!file)
		return "";
	if (str_eq(file, ".."))
		return "..";
	const char *dot = strrchr(file, '.');
	if (!dot || dot == file)
		return file;
	return xstrndup(file, (size_t)(dot - file));
}

COLD char *dpath_posix_dirname(const char *path)
{
	DpPath p = dp_parse(path);
	dp_remove_trailing_separators(&p);
	if (p.n <= 1)
		return xstrdup(p.root ? p.root : ".");
	p.n--;
	dp_remove_trailing_separators(&p);
	return dp_to_string(&p);
}

COLD char *dpath_posix_basename_without_extension(const char *path)
{
	DpPath p = dp_parse(path);
	return xstrdup(dp_basename_without_extension(&p));
}

COLD char *dpath_posix_without_extension(const char *path)
{
	DpPath p = dp_parse(path);
	for (int i = p.n - 1; i >= 0; i--)
		if (*p.parts[i]) {
			p.parts[i] = dp_basename_without_extension(&p);
			break;
		}
	return dp_to_string(&p);
}

COLD char *dpath_posix_join(const char *a, const char *b)
{
	Buf buf = {0};
	bool needs_separator = false;
	const char *parts[2] = {a, b};
	for (int i = 0; i < 2; i++) {
		const char *part = parts[i];
		if (!*part)
			continue;
		if (*part == '/') {
			buf.n = 0;
			buf_adds(&buf, part);
		} else {
			if (needs_separator)
				buf_addc(&buf, '/');
			buf_adds(&buf, part);
		}
		needs_separator = part[strlen(part) - 1] != '/';
	}
	return (char *)buf_str(&buf);
}

/* Context._needsNormalization */
static bool dp_needs_normalization(const char *path)
{
	size_t start = 0, n = strlen(path);
	int prev2 = -1, prev = -1;
	if (n && path[0] == '/')
		start = 1, prev = '/';
	for (size_t i = start; i < n; i++) {
		int c = (unsigned char)path[i];
		if (c == '/') {
			if (prev == '/')
				return true;
			if (prev == '.' && (prev2 == -1 || prev2 == '.' || prev2 == '/'))
				return true;
		}
		prev2 = prev;
		prev = c;
	}
	if (prev == -1 || prev == '/')
		return true;
	return prev == '.' && (prev2 == -1 || prev2 == '/' || prev2 == '.');
}

COLD char *dpath_posix_normalize(const char *path)
{
	if (!dp_needs_normalization(path))
		return xstrdup(path);
	DpPath p = dp_parse(path);
	dp_normalize(&p);
	return dp_to_string(&p);
}

/* Context.absolute(part1) with the current directory "." */
static char *dp_absolute(const char *path) { return *path == '/' ? xstrdup(path) : dpath_posix_join(".", path); }

COLD char *dpath_posix_relative(const char *path, const char *from, const char **err)
{
	from = dp_absolute(from);
	if (*from != '/' && *path == '/')
		return dpath_posix_normalize(path);
	if (*path != '/')
		path = dp_absolute(path);
	if (*path != '/' && *from == '/') {
		*err = xfmt("PathException: Unable to find a path to \"%s\" from \"%s\".", path, from);
		return NULL;
	}
	DpPath f = dp_parse(from), p = dp_parse(path);
	dp_normalize(&f);
	dp_normalize(&p);
	if (f.n > 0 && str_eq(f.parts[0], "."))
		return dp_to_string(&p);
	if ((f.root == NULL) != (p.root == NULL))
		return dp_to_string(&p);
	int common = 0;
	while (common < f.n && common < p.n && str_eq(f.parts[common], p.parts[common]))
		common++;
	if (common < f.n && str_eq(f.parts[common], "..")) {
		*err = xfmt("PathException: Unable to find a path to \"%s\" from \"%s\".", path, from);
		return NULL;
	}
	/* drop the common parts, then prepend ".." for each remaining part of from */
	int ups = f.n - common;
	DpPath r = {0};
	dp_reserve(&r, ups + p.n - common + 1);
	r.seps[0] = "";
	for (int i = 0; i < ups; i++) {
		r.parts[r.n++] = "..";
		r.seps[r.n] = "/";
	}
	for (int i = common; i < p.n; i++) {
		r.parts[r.n] = p.parts[i];
		r.seps[++r.n] = p.seps[i + 1];
	}
	if (r.n == 0)
		return xstrdup(".");
	if (r.n > 1 && str_eq(r.parts[r.n - 1], ".")) {
		r.n--;
		r.seps[r.n] = "";
	}
	dp_remove_trailing_separators(&r);
	return dp_to_string(&r);
}
