/* black/strings.py: docstring indentation and string width. Strings are UTF-8; Python indexes
 * them by code point. */

static size_t py_len(const char *s, size_t n)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		k += ((unsigned char)s[i] & 0xc0) != 0x80;
	return k;
}

/* str_width: len() for ASCII. Non-ASCII text only occurs in lines without brackets (black.h),
 * whose output does not depend on their width; it counts one column per code point. */
static int str_width(const char *s, size_t n) { return (int)py_len(s, n); }

/* Byte offset after the leading Python whitespace (str.lstrip) of s[0:n). */
static size_t lstrip_len(const char *s, size_t n)
{
	Str r = py_lstrip((Str){s, n});
	return (size_t)(r.p - s);
}

/* lines_with_leading_tabs_expanded(s) */
static int leading_tabs_expanded(Str s, Str **out)
{
	Str *lines;
	int n = py_splitlines(s, &lines);
	Str *res = xalloc(sizeof *res * (size_t)(n + 1));
	int k = 0;
	for (int i = 0; i < n; i++) {
		Str line = lines[i];
		size_t pl = lstrip_len(line.p, line.n);
		if (pl == line.n || pl == 0) {
			res[k++] = line;
			continue;
		}
		/* line[:prefix_length].expandtabs() + stripped_line */
		Buf b = {0};
		size_t col = 0;
		for (size_t j = 0; j < pl;) {
			uint32_t r;
			int w = utf8_decode(line.p + j, pl - j, &r);
			if (r == '\t') {
				size_t sp = 8 - col % 8;
				for (size_t q = 0; q < sp; q++)
					buf_addc(&b, ' ');
				col += sp;
			} else {
				buf_add(&b, line.p + j, (size_t)w);
				col = (r == '\n' || r == '\r') ? 0 : col + 1;
			}
			j += (size_t)w;
		}
		buf_add(&b, line.p + pl, line.n - pl);
		res[k++] = buf_view(&b);
	}
	if (s.n && s.p[s.n - 1] == '\n')
		res[k++] = (Str){"", 0};
	*out = res;
	return k;
}

/* fix_docstring(docstring, prefix) */
static char *fix_docstring(const char *s, size_t n, const char *prefix)
{
	if (!n)
		return "";
	Str *lines;
	int nl = leading_tabs_expanded((Str){s, n}, &lines);
	size_t indent = SIZE_MAX;
	for (int i = 1; i < nl; i++) {
		size_t pl = lstrip_len(lines[i].p, lines[i].n);
		if (pl < lines[i].n) {
			size_t cp = py_len(lines[i].p, pl);
			if (cp < indent)
				indent = cp;
		}
	}
	Buf b = {0};
	if (nl)
		buf_add_str(&b, py_strip(lines[0]));
	if (indent < SIZE_MAX) {
		int last_line_idx = nl - 2;
		for (int i = 1; i < nl; i++) {
			Str line = lines[i];
			/* line[indent:] by code points */
			size_t off = 0;
			for (size_t c = 0; c < indent && off < line.n; c++) {
				off++;
				while (off < line.n && ((unsigned char)line.p[off] & 0xc0) == 0x80)
					off++;
			}
			Str stripped = py_rstrip((Str){line.p + off, line.n - off});
			buf_addc(&b, '\n');
			if (stripped.n || i - 1 == last_line_idx) {
				buf_adds(&b, prefix);
				buf_add_str(&b, stripped);
			}
		}
	}
	return (char *)buf_str(&b);
}
