/* black/comments.py: comments in prefixes (list_comments, generate_comments, make_comment). */

/* make_comment(content) */
static char *make_comment(Str content)
{
	content = py_rstrip(content);
	if (!content.n)
		return "#";
	if (content.p[0] == '#')
		content.p++, content.n--;
	Buf b = {0};
	buf_addc(&b, '#');
	/* NON_BREAKING_SPACE U+00A0 not followed by "type:" after lstrip */
	if (content.n >= 2 && (unsigned char)content.p[0] == 0xc2 && (unsigned char)content.p[1] == 0xa0) {
		Str rest = py_lstrip(content);
		if (!str_has_prefix(rest, STR_LIT("type:"))) {
			content.p += 2, content.n -= 2;
			buf_addc(&b, ' ');
			buf_add_str(&b, content);
			return (char *)buf_str(&b);
		}
	}
	if (content.n && !strchr(" !:#'", content.p[0]))
		buf_addc(&b, ' ');
	buf_add_str(&b, content);
	return (char *)buf_str(&b);
}

/* list_comments(prefix, is_endmarker=...) */
static int list_comments(const char *prefix, bool is_endmarker, ProtoComment **out)
{
	*out = NULL;
	if (!*prefix || !strchr(prefix, '#'))
		return 0;
	Buf res = {0};
	size_t consumed = 0;
	int nlines = 0, ignored_lines = 0, index = 0;
	bool form_feed = false;
	const char *p = prefix;
	for (;; index++) {
		/* re.split("\r?\n", prefix) */
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		size_t full = len;
		if (nl && len && p[len - 1] == '\r')
			len--;
		Str full_line = {p, len};
		consumed += py_len(full_line.p, full_line.n) + 1;
		/* re.match(r"^(\s*)(\S.*|)$", full_line) */
		Str line = py_lstrip(full_line);
		if (!line.n) {
			nlines++;
			if (memchr(full_line.p, '\f', full_line.n))
				form_feed = true;
		}
		if (!line.n || line.p[0] != '#') {
			if (line.n && line.p[line.n - 1] == '\\')
				ignored_lines++;
		} else {
			ProtoComment pc = {
				.type = index == ignored_lines && !is_endmarker ? BK_COMMENT : BK_STANDALONE_COMMENT,
				.value = make_comment(line),
				.newlines = nlines,
				.consumed = consumed,
				.form_feed = form_feed,
			};
			buf_add(&res, &pc, sizeof pc);
			form_feed = false;
			nlines = 0;
		}
		if (!nl)
			break;
		p += full + 1;
	}
	*out = (ProtoComment *)res.p;
	return (int)(res.n / sizeof(ProtoComment));
}

/* make_simple_prefix(nl_count, form_feed) */
static char *simple_prefix(int nl, bool form_feed)
{
	Buf b = {0};
	if (form_feed) {
		for (int i = 0; i < nl - 1; i++)
			buf_addc(&b, '\n');
		buf_adds(&b, "\f\n");
	} else {
		for (int i = 0; i < nl; i++)
			buf_addc(&b, '\n');
	}
	return (char *)buf_str(&b);
}

/* normalize_trailing_prefix(leaf, total_consumed); consumed counts code points. */
static void normalize_trailing_prefix(BkNode *leaf, size_t total_consumed)
{
	const char *s = leaf->prefix;
	size_t n = strlen(s), off = 0;
	for (size_t c = 0; c < total_consumed && off < n; c++) {
		off++;
		while (off < n && ((unsigned char)s[off] & 0xc0) == 0x80)
			off++;
	}
	const char *rem = s + off;
	if (strchr(rem, '\\')) {
		leaf->prefix = "";
		return;
	}
	int nl = 0;
	for (const char *q = rem; *q; q++)
		nl += *q == '\n';
	size_t rl = strlen(rem);
	bool ff = strchr(rem, '\f') && rl && rem[rl - 1] == '\n';
	leaf->prefix = nl == 0 && !ff ? "" : simple_prefix(nl, ff);
}

static int generate_comments(BkNode *leaf, BkNode ***out)
{
	ProtoComment *pcs;
	int n = list_comments(leaf->prefix, leaf->type == BK_ENDMARKER, &pcs);
	size_t total = 0;
	BkNode **res = n ? xalloc(sizeof *res * (size_t)n) : NULL;
	for (int i = 0; i < n; i++) {
		total = pcs[i].consumed;
		BkNode *c = new_leaf(pcs[i].type, pcs[i].value);
		c->prefix = simple_prefix(pcs[i].newlines, pcs[i].form_feed);
		c->src = leaf;
		res[i] = c;
	}
	normalize_trailing_prefix(leaf, total);
	*out = res;
	return n;
}

/* FMT_OFF and _contains_fmt_skip_comment (FMT_SKIP): the comments that convert_one_fmt_off_pair acts on */
static bool is_fmt_directive(const char *c)
{
	static const char *const fmt[] = {"# fmt: off", "# fmt:off", "# yapf: disable"};
	for (size_t i = 0; i < sizeof fmt / sizeof *fmt; i++)
		if (!strcmp(c, fmt[i]))
			return true;
	return false;
}

static bool is_fmt_skip(Str s)
{
	return str_eqc(s, "# fmt: skip") || str_eqc(s, "# fmt:skip");
}

static bool contains_fmt_skip(const char *line)
{
	Str s = str_c(line);
	if (is_fmt_skip(s))
		return true;
	/* "# " + comment.strip() for comment in line.split("# ")[1:] */
	for (const char *p = strstr(line, "# "); p; p = strstr(p, "# ")) {
		p += 2;
		const char *e = strstr(p, "# ");
		Str part = py_strip((Str){p, e ? (size_t)(e - p) : strlen(p)});
		Buf b = {0};
		buf_adds(&b, "# ");
		buf_add_str(&b, part);
		if (is_fmt_skip(buf_view(&b)))
			return true;
	}
	/* "# " + comment.strip() for comment in line.strip("# ").split(";") */
	size_t a = 0, z = s.n;
	while (a < z && (s.p[a] == '#' || s.p[a] == ' '))
		a++;
	while (z > a && (s.p[z - 1] == '#' || s.p[z - 1] == ' '))
		z--;
	for (size_t i = a; i <= z;) {
		size_t j = i;
		while (j < z && s.p[j] != ';')
			j++;
		Buf b = {0};
		buf_adds(&b, "# ");
		buf_add_str(&b, py_strip((Str){s.p + i, j - i}));
		if (is_fmt_skip(buf_view(&b)))
			return true;
		i = j + 1;
	}
	return false;
}

bool bk_comment_ok(Str comment)
{
	for (size_t i = 0; i < comment.n; i++)
		if (comment.p[i] == 0 || comment.p[i] == '\r' || comment.p[i] == '\f' || comment.p[i] == '\n')
			return false;
	char *c = make_comment(comment);
	return !is_fmt_directive(c) && !contains_fmt_skip(c);
}

bool bk_docstring_ok(Str text)
{
	size_t i = 0;
	while (i < text.n) {
		if (text.p[i] == 0)
			return false;
		if (text.p[i] == '\\') {
			i += 2;
			continue;
		}
		if (i + 3 <= text.n && !memcmp(text.p + i, "\"\"\"", 3))
			return false;
		i++;
	}
	return i == text.n;
}
