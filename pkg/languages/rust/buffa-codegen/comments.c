/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/comments.rs: the FQN-keyed comment map built from
 * SourceCodeInfo, and the doc attributes rendered from a comment (indented-block and fence
 * handling, AIP-192 cross-reference links, rustdoc escaping). */

/* format_comment: detached, leading and trailing comments, trimmed, joined by a blank line */
static char *format_comment(const PbMsg *loc)
{
	Vec parts = {0};
	int nd = pb_count(loc, LOC_LEADING_DETACHED);
	for (int i = 0; i < nd; i++)
		vec_push(&parts, (void *)pb_str_at(loc, LOC_LEADING_DETACHED, i));
	if (pb_has(loc, LOC_LEADING))
		vec_push(&parts, (void *)pb_str(loc, LOC_LEADING));
	if (pb_has(loc, LOC_TRAILING))
		vec_push(&parts, (void *)pb_str(loc, LOC_TRAILING));
	Buf b = {0};
	for (int i = 0; i < parts.n; i++) {
		Str s = str_c(parts.v[i]);
		while (s.n && s.p[0] == '\n')
			s.p++, s.n--;
		/* trim_end: Unicode whitespace */
		Str t = rust_trim(s);
		if (!t.n)
			continue;
		s.n = (size_t)(t.p + t.n - s.p);
		if (b.n)
			buf_adds(&b, "\n\n");
		buf_add_str(&b, s);
	}
	return b.n ? (char *)buf_str(&b) : NULL;
}

static char *path_key(const int32_t *p, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, "%d,", p[i]);
	return (char *)buf_str(&b);
}

static void put_comment(const Map *paths, const int32_t *p, int n, const char *fqn)
{
	const char *c = map_get(paths, path_key(p, n));
	if (c)
		map_put(&cx.comments, fqn, (void *)c);
}

static void collect_enum_comments(const Map *paths, int32_t *p, int n, const char *fqn, const PbMsg *e)
{
	put_comment(paths, p, n, fqn);
	int nv = pb_count(e, EDP_VALUE);
	for (int i = 0; i < nv; i++) {
		p[n] = 2;
		p[n + 1] = i;
		put_comment(paths, p, n + 2, xcat(fqn, ".", dname(pb_msg_at(e, EDP_VALUE, i))));
	}
}

static void collect_message_comments(const Map *paths, int32_t *p, int n, const char *fqn, const PbMsg *msg, int depth)
{
	if (depth > 100)
		fail("message nesting too deep");
	put_comment(paths, p, n, fqn);
	int k = n_fields(msg);
	for (int i = 0; i < k; i++) {
		p[n] = DP_FIELD;
		p[n + 1] = i;
		put_comment(paths, p, n + 2, xcat(fqn, ".", dname(field_at(msg, i))));
	}
	k = pb_count(msg, DP_ONEOF_DECL);
	for (int i = 0; i < k; i++) {
		p[n] = DP_ONEOF_DECL;
		p[n + 1] = i;
		put_comment(paths, p, n + 2, xcat(fqn, ".", dname(pb_msg_at(msg, DP_ONEOF_DECL, i))));
	}
	k = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < k; i++) {
		p[n] = DP_ENUM_TYPE;
		p[n + 1] = i;
		const PbMsg *e = pb_msg_at(msg, DP_ENUM_TYPE, i);
		collect_enum_comments(paths, p, n + 2, xcat(fqn, ".", dname(e)), e);
	}
	k = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < k; i++) {
		p[n] = DP_NESTED_TYPE;
		p[n + 1] = i;
		const PbMsg *m = pb_msg_at(msg, DP_NESTED_TYPE, i);
		collect_message_comments(paths, p, n + 2, xcat(fqn, ".", dname(m)), m, depth + 1);
	}
}

/* fqn_comments */
static void comments_collect(const PbMsg *file)
{
	const PbMsg *sci = pb_msg(file, FDP_SOURCE_CODE_INFO);
	int nloc = pb_count(sci, SCI_LOCATION);
	if (!nloc)
		return;
	Map paths = {0};
	for (int i = 0; i < nloc; i++) {
		const PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		char *c = format_comment(loc);
		if (!c)
			continue;
		int np = pb_count(loc, LOC_PATH);
		int32_t *p = xalloc(sizeof *p * (size_t)(np + 1));
		for (int j = 0; j < np; j++)
			p[j] = (int32_t)pb_int_at(loc, LOC_PATH, j);
		map_put(&paths, path_key(p, np), c);
	}
	if (!paths.n)
		return;
	const char *package = pb_str(file, FDP_PACKAGE);
	int32_t p[2 * 104 + 4];
	int n = pb_count(file, FDP_ENUM_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *e = pb_msg_at(file, FDP_ENUM_TYPE, i);
		p[0] = FDP_ENUM_TYPE;
		p[1] = i;
		collect_enum_comments(&paths, p, 2, join_fqn(package, dname(e)), e);
	}
	n = pb_count(file, FDP_MESSAGE_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *m = pb_msg_at(file, FDP_MESSAGE_TYPE, i);
		p[0] = FDP_MESSAGE_TYPE;
		p[1] = i;
		collect_message_comments(&paths, p, 2, join_fqn(package, dname(m)), m, 0);
	}
}

/* ---- rendering ---- */

static char *pad(const char *line)
{
	if (!*line || *line == ' ')
		return (char *)line;
	return xcat(" ", line);
}

/* indent_split: byte offset where `columns` of leading whitespace are reached, or -1 */
static long indent_split(const char *line, int columns)
{
	int col = 0;
	size_t i = 0;
	for (; line[i]; i++) {
		if (col >= columns)
			return (long)i;
		if (line[i] == ' ')
			col++;
		else if (line[i] == '\t')
			col += 4 - col % 4;
		else
			return -1;
	}
	return col >= columns ? (long)i : -1;
}

static bool is_indented(const char *line) { return indent_split(line, 4) >= 0; }

static char *strip_indent(const char *line)
{
	long off = indent_split(line, 4);
	if (off < 0)
		return (char *)line;
	const char *s = line + off;
	Str t = rust_trim(str_c(s)); /* trim_start */
	const char *q = t.p;
	if (has_prefix(q, "```"))
		return (char *)line;
	return (char *)s;
}

typedef struct {
	const char *indent;
	size_t indent_len;
	char ch;
	size_t len;
	const char *info;
} Fence;

static bool fence_marker(const char *line, Fence *f)
{
	if (is_indented(line))
		return false;
	size_t il = 0;
	while (line[il] == ' ')
		il++;
	const char *rest = line + il;
	char ch = *rest;
	if (ch != '`' && ch != '~')
		return false;
	size_t len = 0;
	while (rest[len] == ch)
		len++;
	const char *info = rest + len;
	if (len < 3 || (ch == '`' && strchr(info, '`')))
		return false;
	f->indent = line;
	f->indent_len = il;
	f->ch = ch;
	f->len = len;
	f->info = info;
	return true;
}

static bool fence_closes(const Fence *f, char ch, size_t len)
{
	if (f->ch != ch || f->len < len)
		return false;
	for (const char *p = f->info; *p; p++)
		if (*p != ' ' && *p != '\t')
			return false;
	return true;
}

static char *fence_info(const char *info)
{
	bool dropped = false, has_ignore = false;
	Vec toks = {0};
	const char *p = info;
	while (*p) {
		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;
		const char *b = p;
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			p++;
		if (p == b)
			continue;
		char *tok = xstrndup(b, (size_t)(p - b));
		if (has_prefix(tok, "ignore-"))
			dropped = true;
		else {
			vec_push(&toks, tok);
			has_ignore |= str_eq(tok, "ignore");
		}
	}
	if (!toks.n)
		return dropped ? "rust,ignore" : "text";
	if (!has_ignore)
		vec_push(&toks, "ignore");
	return str_join(&toks, ",");
}

static size_t find_backtick_closer(const char *s, size_t n, size_t from, size_t run)
{
	size_t i = from;
	while (i < n) {
		if (s[i] == '`') {
			size_t start = i;
			while (i < n && s[i] == '`')
				i++;
			if (i - start == run)
				return i;
		} else
			i++;
	}
	return 0;
}

static long find_inline_link_end(const char *s, size_t n, size_t start)
{
	size_t j = start + 1;
	while (j < n && s[j] != ']') {
		if (s[j] == '[')
			return -1;
		j++;
	}
	if (j + 1 >= n || s[j + 1] != '(')
		return -1;
	int depth = 1;
	for (size_t k = j + 2; k < n; k++) {
		if (s[k] == '(')
			depth++;
		else if (s[k] == ')' && --depth == 0)
			return (long)k;
	}
	return -1;
}

static bool find_ref_link(const char *s, size_t n, size_t start, Str *display, Str *target, size_t *end)
{
	size_t j = start + 1;
	while (j < n) {
		if (s[j] == '[')
			return false;
		if (s[j] == ']')
			break;
		j++;
	}
	if (j >= n || j + 1 >= n || s[j + 1] != '[')
		return false;
	size_t ro = j + 1, k = ro + 1;
	while (k < n && s[k] != ']') {
		if (s[k] == '[')
			return false;
		k++;
	}
	if (k >= n || (k + 1 < n && s[k + 1] == '('))
		return false;
	*display = (Str){s + start + 1, j - start - 1};
	*target = (Str){s + ro + 1, k - ro - 1};
	*end = k + 1;
	return true;
}

static char *escape_angle_brackets(Str s)
{
	Buf b = {0};
	size_t i = 0;
	while (i < s.n) {
		if (s.p[i] == '`') {
			size_t start = i;
			while (i < s.n && s.p[i] == '`')
				i++;
			size_t close = find_backtick_closer(s.p, s.n, i, i - start);
			if (close) {
				buf_add(&b, s.p + start, close - start);
				i = close;
			} else
				buf_add(&b, s.p + start, i - start);
		} else if (s.p[i] == '<') {
			buf_adds(&b, "\\<");
			i++;
		} else if (s.p[i] == '>') {
			buf_adds(&b, "\\>");
			i++;
		} else
			buf_addc(&b, s.p[i++]);
	}
	return (char *)buf_str(&b);
}

static char *escape_path_for_link(const char *path)
{
	Vec segs = {0};
	const char *p = path;
	for (;;) {
		const char *e = strstr(p, "::");
		char *seg = e ? xstrndup(p, (size_t)(e - p)) : (char *)p;
		vec_push(&segs, escape_mod_ident(seg));
		if (!e)
			break;
		p = e + 2;
	}
	return str_join(&segs, "::");
}

static const char *rust_parent_module(const char *path)
{
	const char *cut = NULL;
	for (const char *p = strstr(path, "::"); p; p = strstr(p + 2, "::"))
		cut = p;
	return cut ? xstrndup(path, (size_t)(cut - path)) : "";
}

static const char *last_segment(const char *path)
{
	const char *cut = NULL;
	for (const char *p = strstr(path, "::"); p; p = strstr(p + 2, "::"))
		cut = p;
	return cut ? cut + 2 : path;
}

static const char *resolve_type_fqn(const char *ref, const char *scope)
{
	const char *p = map_get(&cx.type_map, xcat(".", ref));
	if (p || !*scope)
		return p;
	char *sc = xstrdup(scope);
	for (;;) {
		p = map_get(&cx.type_map, xcat(".", sc, ".", ref));
		if (p)
			return p;
		char *dot = strrchr(sc, '.');
		if (!dot)
			return NULL;
		*dot = 0;
	}
}

static char *resolve_proto_ref(Str display, Str target, const char *scope)
{
	Str eff = rust_trim(target.n ? target : display);
	if (!eff.n)
		return NULL;
	const char *rp = resolve_type_fqn(xstrndup(eff.p, eff.n), scope);
	if (!rp || is_abs_path(rp))
		return NULL;
	char *d = escape_angle_brackets(display);
	const char *last = last_segment(rp);
	Str dt = rust_trim(display);
	if (str_eqc(dt, last) && !is_rust_keyword(last)) {
		const char *sp = map_get(&cx.type_map, xcat(".", scope));
		if (sp && str_eq(rust_parent_module(sp), rust_parent_module(rp)))
			return xcat("[", d, "]");
	}
	return xcat("[", d, "](crate::", escape_path_for_link(rp), ")");
}

static long find_autolink_end(const char *s, size_t n, size_t start)
{
	const char *rest = s + start + 1;
	size_t rn = n - start - 1;
	if (!((rn >= 7 && !strncmp(rest, "http://", 7)) || (rn >= 8 && !strncmp(rest, "https://", 8))))
		return -1;
	const char *gt = memchr(rest, '>', rn);
	return gt ? (long)(gt - s) : -1;
}

static long find_bare_url_end(const char *s, size_t n, size_t start)
{
	size_t rn = n - start;
	if (!((rn >= 7 && !strncmp(s + start, "http://", 7)) || (rn >= 8 && !strncmp(s + start, "https://", 8))))
		return -1;
	size_t j = start;
	while (j < n && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\f' || s[j] == '\r') && s[j] != ')')
		j++;
	return (long)j;
}

static char *sanitize_line(const char *line, const char *scope)
{
	size_t n = strlen(line), i = 0;
	Buf out = {0};
	while (i < n) {
		char b = line[i];
		if (b == '`') {
			size_t start = i;
			while (i < n && line[i] == '`')
				i++;
			size_t close = find_backtick_closer(line, n, i, i - start);
			if (close) {
				buf_add(&out, line + start, close - start);
				i = close;
			} else
				buf_add(&out, line + start, i - start);
			continue;
		}
		long end;
		Str disp, tgt;
		size_t rend;
		switch (b) {
		case '\\':
			buf_addc(&out, '\\');
			i++;
			if (i < n) {
				uint32_t r;
				int w = utf8_decode(line + i, n - i, &r);
				buf_add(&out, line + i, (size_t)w);
				i += (size_t)w;
			}
			break;
		case '[':
			if ((end = find_inline_link_end(line, n, i)) >= 0) {
				buf_add(&out, line + i, (size_t)end - i + 1);
				i = (size_t)end + 1;
			} else if (find_ref_link(line, n, i, &disp, &tgt, &rend)) {
				char *res = resolve_proto_ref(disp, tgt, scope);
				if (res)
					buf_adds(&out, res);
				else {
					buf_adds(&out, "\\[");
					buf_adds(&out, escape_angle_brackets(disp));
					buf_adds(&out, "\\]\\[");
					buf_adds(&out, escape_angle_brackets(tgt));
					buf_adds(&out, "\\]");
				}
				i = rend;
			} else {
				buf_adds(&out, "\\[");
				i++;
			}
			break;
		case ']':
			buf_adds(&out, "\\]");
			i++;
			break;
		case '<':
			if ((end = find_autolink_end(line, n, i)) >= 0) {
				buf_add(&out, line + i, (size_t)end - i + 1);
				i = (size_t)end + 1;
			} else {
				buf_adds(&out, "\\<");
				i++;
			}
			break;
		case '>':
			buf_adds(&out, "\\>");
			i++;
			break;
		case 'h':
			if ((end = find_bare_url_end(line, n, i)) >= 0) {
				buf_addc(&out, '<');
				buf_add(&out, line + i, (size_t)end - i);
				buf_addc(&out, '>');
				i = (size_t)end;
			} else {
				buf_addc(&out, 'h');
				i++;
			}
			break;
		default:
			buf_addc(&out, b);
			i++;
		}
	}
	return (char *)buf_str(&out);
}

/* doc_lines_impl */
static void doc_lines(Vec *attrs, const char *text, const char *scope)
{
	Str *raw;
	int nraw = rust_lines(str_c(text), &raw);
	Vec lines = {0};
	bool in_code = false, open = false;
	char open_ch = 0;
	size_t open_len = 0;
	for (int idx = 0; idx < nraw; idx++) {
		char *line = xstrndup(raw[idx].p, raw[idx].n);
		Fence f;
		if (open) {
			if (fence_marker(line, &f) && fence_closes(&f, open_ch, open_len))
				open = false;
			vec_push(&lines, pad(line));
			continue;
		}
		if (in_code) {
			if (is_indented(line)) {
				vec_push(&lines, pad(strip_indent(line)));
				continue;
			}
			if (!*line) {
				bool next_indented = false;
				for (int k = idx + 1; k < nraw; k++)
					if (raw[k].n) {
						next_indented = is_indented(xstrndup(raw[k].p, raw[k].n));
						break;
					}
				if (next_indented) {
					vec_push(&lines, "");
					continue;
				}
			}
			vec_push(&lines, " ```");
			in_code = false;
		}
		if (fence_marker(line, &f)) {
			open = true;
			open_ch = f.ch;
			open_len = f.len;
			Buf b = {0};
			buf_add(&b, f.indent, f.indent_len);
			for (size_t k = 0; k < f.len; k++)
				buf_addc(&b, f.ch);
			buf_adds(&b, fence_info(f.info));
			vec_push(&lines, pad(buf_str(&b)));
		} else if (is_indented(line)) {
			vec_push(&lines, " ```text");
			in_code = true;
			vec_push(&lines, pad(strip_indent(line)));
		} else if (!*line) {
			vec_push(&lines, "");
		} else
			vec_push(&lines, pad(sanitize_line(line, scope)));
	}
	if (in_code)
		vec_push(&lines, " ```");
	if (open) {
		Buf b = {0};
		buf_addc(&b, ' ');
		for (size_t k = 0; k < open_len; k++)
			buf_addc(&b, open_ch);
		vec_push(&lines, (void *)buf_str(&b));
	}
	for (int i = 0; i < lines.n; i++)
		vec_push(attrs, doc_attr(lines.v[i]));
}

/* doc_attrs_resolved */
static void doc_attrs(Vec *attrs, const char *comment, const char *scope_fqn)
{
	if (comment)
		doc_lines(attrs, comment, scope_fqn);
}

/* doc_attrs_with_tag_resolved */
static void doc_attrs_with_tag(Vec *attrs, const char *comment, const char *tag, const char *scope_fqn)
{
	if (comment) {
		doc_lines(attrs, comment, scope_fqn);
		vec_push(attrs, doc_attr(""));
	}
	doc_lines(attrs, tag, scope_fqn);
}
