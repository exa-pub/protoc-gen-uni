/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/StringUtils.swift and Range+Extensions.swift. */

typedef struct {
	char *dir, *base, *suffix;
} PathParts;

/* splitPath(pathname:) */
static PathParts split_path(const char *pathname)
{
	Buf dir = {0}, base = {0}, suffix = {0};
	for (const char *p = pathname; *p; p++) {
		if (*p == '/') {
			buf_add(&dir, base.p, base.n);
			buf_add(&dir, suffix.p, suffix.n);
			buf_addc(&dir, '/');
			base.n = suffix.n = 0;
		} else if (*p == '.') {
			buf_add(&base, suffix.p, suffix.n);
			suffix.n = 0;
			buf_addc(&suffix, '.');
		} else {
			buf_addc(&suffix, *p);
		}
	}
	if (suffix.n && suffix.p[0] != '.') {
		buf_add(&base, suffix.p, suffix.n);
		suffix.n = 0;
	}
	PathParts r = {xstrndup(buf_str(&dir), dir.n), xstrndup(buf_str(&base), base.n), xstrndup(buf_str(&suffix), suffix.n)};
	return r;
}

/* escapedToDataLiteral: the C-escaped default value of a bytes field. */
static char *escaped_to_data_literal(Str s)
{
	if (!s.n)
		return "Data()";
	Buf out = {0};
	buf_adds(&out, "Data([");
	const char *separator = "";
	bool escape = false;
	int octal = 0, acc = 0;
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (octal > 0) {
			if (!(c >= 48 && c < 56))
				fail("invalid octal escape in a bytes default value");
			acc = (acc << 3) | (c - 48);
			if (--octal == 0) {
				buf_fmt(&out, "%s%d", separator, acc);
				separator = ", ";
			}
		} else if (escape) {
			switch (c) {
			case 110: buf_fmt(&out, "%s10", separator); separator = ", "; break;
			case 114: buf_fmt(&out, "%s13", separator); separator = ", "; break;
			case 116: buf_fmt(&out, "%s9", separator); separator = ", "; break;
			default:
				if (c >= 48 && c < 56) {
					octal = 2;
					acc = c - 48;
				} else {
					buf_fmt(&out, "%s%d", separator, c);
					separator = ", ";
				}
			}
			escape = false;
		} else if (c == 92) {
			escape = true;
		} else {
			buf_fmt(&out, "%s%d", separator, c);
			separator = ", ";
		}
	}
	buf_adds(&out, "])");
	return xstrdup(buf_str(&out));
}

/* stringToEscapedStringLiteral: per Unicode scalar; only NUL, other C0 controls, '"' and '\\'
 * are escaped, so bytes of multi-byte sequences pass through. */
static char *string_to_escaped_string_literal(Str s)
{
	if (!s.n)
		return "String()";
	Buf out = {0};
	buf_addc(&out, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (c == 0)
			buf_adds(&out, "\\0");
		else if (c < 32)
			buf_fmt(&out, "\\u{%c%c}", "0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15]);
		else if (c == 34)
			buf_adds(&out, "\\\"");
		else if (c == 92)
			buf_adds(&out, "\\\\");
		else
			buf_addc(&out, (char)c);
	}
	buf_addc(&out, '"');
	return xstrdup(buf_str(&out));
}

/* ---- Range+Extensions.swift ---- */

static char *range_case_expression(SwRange r)
{
	if ((int64_t)r.start == (int64_t)r.end - 1)
		return xfmt("%d", (int)r.start);
	return xfmt("%d..<%d", (int)r.start, (int)r.end);
}

static char *range_boolean_expression(SwRange r, const char *variable)
{
	if ((int64_t)r.start == (int64_t)r.end - 1)
		return xfmt("%d == %s", (int)r.start, variable);
	return xfmt("%d <= %s && %s < %d", (int)r.start, variable, variable, (int)r.end);
}

static char *ranges_case_expression(const SwRange *r, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, "%s%s", i ? ", " : "", range_case_expression(r[i]));
	return xstrdup(buf_str(&b));
}

static char *ranges_boolean_expression(const SwRange *r, int n, const char *variable)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, "%s(%s)", i ? " || " : "", range_boolean_expression(r[i], variable));
	return xstrdup(buf_str(&b));
}

/* sortAndMergeContinuous: a stable sort by lower bound (insertion sort keeps equal bounds in
 * order, like Swift's sort), then merging of touching neighbours from the end. */
static int sort_and_merge_continuous(SwRange *r, int n)
{
	for (int i = 1; i < n; i++) {
		SwRange x = r[i];
		int j = i;
		while (j > 0 && r[j - 1].start > x.start) {
			r[j] = r[j - 1];
			j--;
		}
		r[j] = x;
	}
	for (int i = n - 2; i >= 0; i--) {
		if (r[i].end == r[i + 1].start) {
			r[i].end = r[i + 1].end;
			memmove(&r[i + 1], &r[i + 2], sizeof(SwRange) * (size_t)(n - i - 2));
			n--;
		}
	}
	return n;
}
