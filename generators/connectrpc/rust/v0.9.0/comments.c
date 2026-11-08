/* Copyright 2026 The Connect Authors. Licensed under the Apache License, Version 2.0.
 *
 * Port of connectrpc-codegen 0.9.0 src/comments.rs: sanitization of proto source comments for
 * rustdoc emission (a hardened fork of buffa-codegen's comment sanitizer). Byte-oriented like
 * upstream; non-ASCII chars are copied unchanged. */

/* str::trim_start (Unicode white space) */
static Str trim_start(Str s)
{
	while (s.n) {
		uint32_t c;
		int w = utf8_decode(s.p, s.n, &c);
		if (!rust_is_whitespace(c))
			break;
		s.p += w, s.n -= (size_t)w;
	}
	return s;
}

static bool sv_prefix(Str s, const char *p) { return str_has_prefix(s, str_c(p)); }

/* An indented-code-block line: 4+ spaces or a tab (CommonMark). */
static bool is_indented(Str line) { return sv_prefix(line, "    ") || sv_prefix(line, "\t"); }

/* strip_indent */
static Str strip_indent(Str line)
{
	Str stripped = line;
	if (sv_prefix(line, "    "))
		stripped = (Str){line.p + 4, line.n - 4};
	else if (sv_prefix(line, "\t"))
		stripped = (Str){line.p + 1, line.n - 1};
	if (sv_prefix(trim_start(stripped), "```"))
		return line;
	return stripped;
}

/* A fenced-code-block marker line (struct Fence). */
typedef struct {
	Str indent;
	char ch;
	size_t len;
	Str info;
} Fence;

/* Fence::closes: same character, a run at least as long, only spaces or tabs after it. */
static bool fence_closes(const Fence *f, char ch, size_t len)
{
	if (f->ch != ch || f->len < len)
		return false;
	for (size_t i = 0; i < f->info.n; i++)
		if (f->info.p[i] != ' ' && f->info.p[i] != '\t')
			return false;
	return true;
}

/* fence_marker */
static bool fence_marker(Str line, Fence *out)
{
	if (is_indented(line))
		return false;
	size_t indent = 0;
	while (indent < line.n && line.p[indent] == ' ')
		indent++;
	if (indent == line.n)
		return false;
	char ch = line.p[indent];
	if (ch != '`' && ch != '~')
		return false;
	size_t end = indent;
	while (end < line.n && line.p[end] == ch)
		end++;
	Str info = {line.p + end, line.n - end};
	size_t len = end - indent;
	if (len < 3 || (ch == '`' && memchr(info.p, '`', info.n)))
		return false;
	out->indent = (Str){line.p, indent};
	out->ch = ch;
	out->len = len;
	out->info = info;
	return true;
}

/* fence_info: the info string that makes the fence inert for rustdoc. */
static void fence_info(Buf *b, Str info)
{
	bool dropped_target_ignore = false, has_ignore = false;
	Vec tokens = {0};
	size_t i = 0;
	while (i <= info.n) {
		size_t j = i;
		while (j < info.n && info.p[j] != ',' && info.p[j] != ' ' && info.p[j] != '\t')
			j++;
		if (j > i) {
			Str *tok = xalloc(sizeof *tok);
			*tok = (Str){info.p + i, j - i};
			if (sv_prefix(*tok, "ignore-")) {
				dropped_target_ignore = true;
			} else {
				vec_push(&tokens, tok);
				has_ignore |= str_eqc(*tok, "ignore");
			}
		}
		i = j + 1;
	}
	if (!tokens.n) {
		buf_adds(b, dropped_target_ignore ? "rust,ignore" : "text");
		return;
	}
	for (int k = 0; k < tokens.n; k++) {
		if (k)
			buf_addc(b, ',');
		buf_add_str(b, *(Str *)tokens.v[k]);
	}
	if (!has_ignore)
		buf_adds(b, ",ignore");
}

/* find_backtick_closer */
static size_t find_backtick_closer(Str s, size_t from, size_t run_len)
{
	size_t i = from;
	while (i < s.n) {
		if (s.p[i] == '`') {
			size_t start = i;
			while (i < s.n && s.p[i] == '`')
				i++;
			if (i - start == run_len)
				return i;
		} else {
			i++;
		}
	}
	return 0;
}

/* find_inline_link_end: the index of the closing ')' of a complete [text](url), or 0. */
static size_t find_inline_link_end(Str s, size_t start)
{
	size_t j = start + 1;
	while (j < s.n && s.p[j] != ']') {
		if (s.p[j] == '[')
			return 0;
		j++;
	}
	if (j + 1 >= s.n || s.p[j + 1] != '(')
		return 0;
	int depth = 1;
	for (size_t k = j + 2; k < s.n; k++) {
		if (s.p[k] == '(') {
			depth++;
		} else if (s.p[k] == ')') {
			if (--depth == 0)
				return k;
		}
	}
	return 0;
}

static bool has_url_scheme(Str s, size_t at)
{
	Str rest = {s.p + at, s.n - at};
	return sv_prefix(rest, "http://") || sv_prefix(rest, "https://");
}

/* find_autolink_end: the index of the '>' of <http(s)://...>, or 0. */
static size_t find_autolink_end(Str s, size_t start)
{
	if (!has_url_scheme(s, start + 1))
		return 0;
	const char *gt = memchr(s.p + start + 1, '>', s.n - start - 1);
	return gt ? (size_t)(gt - s.p) : 0;
}

static bool is_ascii_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

/* find_bare_url_end: the past-the-end index of a bare http(s):// URL, or 0. */
static size_t find_bare_url_end(Str s, size_t start)
{
	if (!has_url_scheme(s, start))
		return 0;
	size_t j = start;
	while (j < s.n && !is_ascii_whitespace(s.p[j]) && s.p[j] != ')' && s.p[j] != '<' && s.p[j] != '>')
		j++;
	while (j > start && strchr(".,;:!?", s.p[j - 1]) && s.p[j - 1])
		j--;
	return j;
}

/* sanitize_line: escapes one prose line for rustdoc. */
static void sanitize_line(Buf *out, Str line)
{
	size_t i = 0;
	while (i < line.n) {
		char b = line.p[i];
		if (b == '`') {
			size_t run_start = i;
			while (i < line.n && line.p[i] == '`')
				i++;
			size_t close_end = find_backtick_closer(line, i, i - run_start);
			if (close_end) {
				buf_add(out, line.p + run_start, close_end - run_start);
				i = close_end;
			} else {
				buf_add(out, line.p + run_start, i - run_start);
			}
			continue;
		}
		size_t end;
		switch (b) {
		case '\\':
			/* the author escapes the next char: the pair passes through (bytes of a multi-byte
			 * char are copied one by one, as the default case does) */
			buf_addc(out, '\\');
			i++;
			if (i < line.n)
				buf_addc(out, line.p[i++]);
			break;
		case '[':
			if ((end = find_inline_link_end(line, i))) {
				buf_add(out, line.p + i, end + 1 - i);
				i = end + 1;
			} else {
				buf_adds(out, "\\[");
				i++;
			}
			break;
		case ']':
			buf_adds(out, "\\]");
			i++;
			break;
		case '<':
			if ((end = find_autolink_end(line, i))) {
				buf_add(out, line.p + i, end + 1 - i);
				i = end + 1;
			} else {
				buf_adds(out, "\\<");
				i++;
			}
			break;
		case '>':
			buf_adds(out, "\\>");
			i++;
			break;
		case 'h':
			if ((end = find_bare_url_end(line, i))) {
				buf_addc(out, '<');
				buf_add(out, line.p + i, end - i);
				buf_addc(out, '>');
				i = end;
			} else {
				buf_addc(out, 'h');
				i++;
			}
			break;
		default:
			buf_addc(out, b);
			i++;
		}
	}
}

/* sanitize_comment: user fences pass through (made inert), indented blocks become ```text
 * fences, prose is escaped. Lines are joined with "\n". */
static char *sanitize_comment(Str text)
{
	Str *raw;
	int n = rust_lines(text, &raw);
	Vec lines = {0};
	bool in_code_block = false;
	bool open = false;
	char open_ch = 0;
	size_t open_len = 0;
	for (int idx = 0; idx < n; idx++) {
		Str line = raw[idx];
		Fence f;
		if (open) {
			if (fence_marker(line, &f) && fence_closes(&f, open_ch, open_len))
				open = false;
			vec_push(&lines, xstrndup(line.p, line.n));
			continue;
		}
		if (in_code_block) {
			if (is_indented(line)) {
				Str s = strip_indent(line);
				vec_push(&lines, xstrndup(s.p, s.n));
				continue;
			}
			if (!line.n) {
				bool next_is_indented = false;
				for (int k = idx + 1; k < n; k++)
					if (raw[k].n) {
						next_is_indented = is_indented(raw[k]);
						break;
					}
				if (next_is_indented) {
					vec_push(&lines, "");
					continue;
				}
			}
			vec_push(&lines, "```");
			in_code_block = false;
		}
		if (fence_marker(line, &f)) {
			open = true;
			open_ch = f.ch;
			open_len = f.len;
			Buf b = {0};
			buf_add_str(&b, f.indent);
			for (size_t k = 0; k < f.len; k++)
				buf_addc(&b, f.ch);
			fence_info(&b, f.info);
			vec_push(&lines, (char *)buf_str(&b));
		} else if (is_indented(line)) {
			vec_push(&lines, "```text");
			in_code_block = true;
			Str s = strip_indent(line);
			vec_push(&lines, xstrndup(s.p, s.n));
		} else {
			Buf b = {0};
			sanitize_line(&b, line);
			vec_push(&lines, (char *)buf_str(&b));
		}
	}
	if (in_code_block)
		vec_push(&lines, "```");
	if (open) {
		Buf b = {0};
		for (size_t k = 0; k < open_len; k++)
			buf_addc(&b, open_ch);
		vec_push(&lines, (char *)buf_str(&b));
	}
	return str_join(&lines, "\n");
}
