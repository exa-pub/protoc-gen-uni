/* package:path (https://github.com/dart-lang/core/tree/main/pkgs/path, lib/src/context.dart,
 * parsed_path.dart; BSD-3-Clause, Copyright the Dart project authors) and Uri.file of dart:core
 * (sdk/lib/core/uri.dart), for the relative paths protoc_plugin passes. */
#include "dart_path.h"

#include <string.h>

/* ParsedPath of a relative url-style path: its non-root parts. */
static int dpath_parts(const char *p, const char ***out)
{
	Vec v = str_split(p, '/');
	*out = (const char **)v.v;
	return v.n;
}

/* ParsedPath.normalize of a relative path: drops "." and empty parts, resolves "..", "." if
 * nothing is left. */
static int dpath_normalize(const char **parts, int n, const char ***out)
{
	const char **r = xalloc((size_t)(n + 1) * sizeof *r);
	int m = 0, leading = 0;
	for (int i = 0; i < n; i++) {
		const char *s = parts[i];
		if (!*s || strcmp(s, ".") == 0)
			continue;
		if (strcmp(s, "..") == 0) {
			if (m > leading)
				m--;
			else
				r[m++] = s, leading++;
			continue;
		}
		r[m++] = s;
	}
	if (m == 0)
		r[m++] = ".";
	*out = r;
	return m;
}

static char *dpath_join(const char **parts, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		if (i)
			buf_addc(&b, '/');
		buf_adds(&b, parts[i]);
	}
	return (char *)buf_str(&b);
}

char *dpath_without_extension(const char *p)
{
	const char *slash = strrchr(p, '/');
	const char *file = slash ? slash + 1 : p;
	if (!*file || strcmp(file, "..") == 0)
		return xstrdup(p);
	const char *dot = strrchr(file, '.');
	if (!dot || dot == file)
		return xstrdup(p);
	return xstrndup(p, (size_t)(dot - p));
}

char *dpath_dirname(const char *p)
{
	const char **parts;
	int n = dpath_parts(p, &parts);
	while (n > 0 && !*parts[n - 1]) /* removeTrailingSeparators */
		n--;
	if (n <= 1)
		return xstrdup(".");
	n--;
	while (n > 0 && !*parts[n - 1])
		n--;
	return dpath_join(parts, n);
}

char *dpath_relative(const char *p, const char *from)
{
	const char **fp, **pp, **f, **t;
	int nfp = dpath_parts(from, &fp), npp = dpath_parts(p, &pp);
	int nf = dpath_normalize(fp, nfp, &f);
	int np = dpath_normalize(pp, npp, &t);
	if (strcmp(f[0], ".") == 0)
		return dpath_join(t, np);
	while (nf > 0 && np > 0 && strcmp(f[0], t[0]) == 0)
		f++, nf--, t++, np--;
	if (nf > 0 && strcmp(f[0], "..") == 0)
		return NULL;
	const char **r = xalloc((size_t)(nf + np + 1) * sizeof *r);
	int m = 0;
	for (int i = 0; i < nf; i++)
		r[m++] = "..";
	for (int i = 0; i < np; i++)
		r[m++] = t[i];
	if (m == 0)
		return xstrdup(".");
	if (m > 1 && strcmp(r[m - 1], ".") == 0)
		m--;
	return dpath_join(r, m);
}

/* _Uri._pathCharTable: unreserved, sub-delims, ':' and '@'. */
static bool dpath_path_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-._~!$&'()*+,;=:@", c);
}

char *dpath_file_uri(const char *p, bool *absolute)
{
	*absolute = p[0] == '/';
	Buf b = {0};
	for (const unsigned char *s = (const unsigned char *)p; *s; s++) {
		if (*s == '/' || dpath_path_char(*s))
			buf_addc(&b, (char)*s);
		else
			buf_fmt(&b, "%%%c%c", "0123456789ABCDEF"[*s >> 4], "0123456789ABCDEF"[*s & 15]);
	}
	return (char *)buf_str(&b);
}
