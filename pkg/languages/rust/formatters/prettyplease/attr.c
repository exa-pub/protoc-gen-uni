/* Port of prettyplease 0.2.37 src/attr.rs (Copyright David Tolnay, MIT OR Apache-2.0). */

static void p_path(Pp *p, const PpNode *path, int kind);
static void p_expr(Pp *p, const PpNode *e);
static void p_ident(Pp *p, const char *ident);
static void p_token_punct(Pp *p, char ch);
static void p_token_literal(Pp *p, const char *repr);

enum { PATH_SIMPLE, PATH_TYPE, PATH_EXPR };

static void p_attr(Pp *p, const PpNode *attr);

static void p_outer_attrs(Pp *p, const PpList *attrs)
{
	if (!attrs)
		return;
	for (int i = 0; i < attrs->n; i++)
		if (!(attrs->v[i]->flags & PP_F_INNER))
			p_attr(p, attrs->v[i]);
}

static void p_inner_attrs(Pp *p, const PpList *attrs)
{
	if (!attrs)
		return;
	for (int i = 0; i < attrs->n; i++)
		if (attrs->v[i]->flags & PP_F_INNER)
			p_attr(p, attrs->v[i]);
}

static bool attr_has_outer(const PpList *attrs)
{
	if (attrs)
		for (int i = 0; i < attrs->n; i++)
			if (!(attrs->v[i]->flags & PP_F_INNER))
				return true;
	return false;
}

static bool attr_has_inner(const PpList *attrs)
{
	if (attrs)
		for (int i = 0; i < attrs->n; i++)
			if (attrs->v[i]->flags & PP_F_INNER)
				return true;
	return false;
}

/* Path::is_ident */
static bool path_is_ident(const PpNode *path, const char *ident)
{
	return !(path->flags & PP_F_LEADING_COLON) && path->list.n == 1 && !path->list.v[0]->a &&
	       str_eq(path->list.v[0]->text, ident);
}

static bool value_of_attribute(const char *requested, const PpNode *attr, Str *out)
{
	if (attr->aux != PP_META_NAME_VALUE || !path_is_ident(attr->a, requested))
		return false;
	const PpNode *value = attr->b;
	if (value->kind != PP_E_LIT || (value->attrs && value->attrs->n))
		return false;
	const PpNode *lit = value->a;
	if (lit->aux != PP_LIT_STR)
		return false;
	*out = (Str){lit->a->text, lit->a->len};
	return true;
}

static bool str_contains(Str s, char c) { return memchr(s.p, c, s.n) != NULL; }

static Str trim_trailing_spaces(Str doc)
{
	while (doc.n && doc.p[doc.n - 1] == ' ')
		doc.n--;
	return doc;
}

static Str trim_interior_trailing_spaces(Str doc)
{
	bool found = false;
	for (size_t i = 0; i + 1 < doc.n; i++)
		if (doc.p[i] == ' ' && doc.p[i + 1] == '\n')
			found = true;
	if (!found)
		return doc;
	Buf b = {0};
	size_t start = 0;
	for (size_t i = 0; i <= doc.n; i++) {
		if (i == doc.n) {
			buf_add(&b, doc.p + start, i - start);
		} else if (doc.p[i] == '\n') {
			size_t end = i;
			while (end > start && doc.p[end - 1] == ' ')
				end--;
			buf_add(&b, doc.p + start, end - start);
			buf_addc(&b, '\n');
			start = i + 1;
		}
	}
	return buf_view(&b);
}

static bool can_be_block_comment(Str value)
{
	size_t depth = 0, i = 0, upper = value.n - 1;
	const char *bytes = value.p;
	while (i < upper) {
		if (bytes[i] == '/' && bytes[i + 1] == '*') {
			depth++;
			i += 2;
		} else if (bytes[i] == '*' && bytes[i + 1] == '/') {
			if (depth == 0)
				return false;
			depth--;
			i += 2;
		} else {
			i++;
		}
	}
	return depth == 0 && !(value.n && value.p[value.n - 1] == '/');
}

static void p_attr_tokens(Pp *p, const PpNode *group);

static void p_attr(Pp *p, const PpNode *attr)
{
	bool inner = attr->flags & PP_F_INNER;
	Str doc;
	if (value_of_attribute("doc", attr, &doc)) {
		if (!str_contains(doc, '\n') && (inner || !(doc.n && doc.p[0] == '/'))) {
			doc = trim_trailing_spaces(doc);
			p_word(p, inner ? "//!" : "///");
			p_wordn(p, doc.p, doc.n);
			p_hardbreak(p);
			return;
		} else if (can_be_block_comment(doc) &&
			   (inner || !(doc.n && (doc.p[0] == '*' || doc.p[0] == '/')))) {
			doc = trim_interior_trailing_spaces(doc);
			p_word(p, inner ? "/*!" : "/**");
			p_wordn(p, doc.p, doc.n);
			p_word(p, "*/");
			p_hardbreak(p);
			return;
		}
	} else if (value_of_attribute("comment", attr, &doc)) {
		if (!str_contains(doc, '\n')) {
			doc = trim_trailing_spaces(doc);
			p_word(p, "//");
			p_wordn(p, doc.p, doc.n);
			p_hardbreak(p);
			return;
		} else if (can_be_block_comment(doc) && !(doc.n && (doc.p[0] == '*' || doc.p[0] == '!'))) {
			doc = trim_interior_trailing_spaces(doc);
			p_word(p, "/*");
			p_wordn(p, doc.p, doc.n);
			p_word(p, "*/");
			p_hardbreak(p);
			return;
		}
	}

	p_word(p, inner ? "#!" : "#");
	p_word(p, "[");
	switch (attr->aux) {
	case PP_META_PATH:
		p_path(p, attr->a, PATH_SIMPLE);
		break;
	case PP_META_LIST: {
		p_path(p, attr->a, PATH_SIMPLE);
		/* Group::new(delimiter, tokens) as a one-token stream */
		PpNode group = {0};
		group.kind = PP_TT_GROUP;
		group.aux = attr->flags >> 4;
		group.list = attr->list;
		PpNode outer = {0};
		outer.kind = PP_TT_GROUP;
		outer.aux = PP_DELIM_NONE;
		PpNode *one[1] = {&group};
		outer.list = (PpList){one, 1, 1};
		p_attr_tokens(p, &outer);
		break;
	}
	default:
		p_path(p, attr->a, PATH_SIMPLE);
		p_word(p, " = ");
		p_expr(p, attr->b);
	}
	p_word(p, "]");
	p_space(p);
}

enum { ST_WORD, ST_PUNCT, ST_TRAILING_COMMA };

/* attr_tokens: `stack` holds (token iterator, delimiter); the outermost level is the token stream
 * itself (Delimiter::None). */
static void p_attr_tokens(Pp *p, const PpNode *tokens)
{
	typedef struct {
		const PpNode *group;
		int i;
	} Frame;
	Frame stack[256];
	int depth = 0;
	stack[depth++] = (Frame){tokens, 0};
	void (*space)(Pp *) = p_nbsp;
	int state = ST_WORD;

	while (depth) {
		Frame *top = &stack[depth - 1];
		const PpNode *tt = top->i < top->group->list.n ? top->group->list.v[top->i++] : NULL;
		if (!tt) {
			switch (top->group->aux) {
			case PP_DELIM_PAREN:
				if (state != ST_TRAILING_COMMA)
					p_zerobreak(p);
				p_offset(p, -INDENT);
				p_end(p);
				p_word(p, ")");
				state = ST_PUNCT;
				break;
			case PP_DELIM_BRACE:
				p_word(p, "}");
				state = ST_PUNCT;
				break;
			case PP_DELIM_BRACKET:
				p_word(p, "]");
				state = ST_PUNCT;
				break;
			}
			depth--;
			if (!depth)
				space = p_nbsp;
			continue;
		}
		switch (tt->kind) {
		case PP_TT_IDENT:
			if (state == ST_WORD)
				space(p);
			p_ident(p, tt->text);
			state = ST_WORD;
			break;
		case PP_TT_PUNCT: {
			char ch = (char)tt->aux;
			if (state == ST_WORD && ch == '=')
				p_nbsp(p);
			if (ch == ',' && top->i >= top->group->list.n) {
				p_trailing_comma(p, true);
				state = ST_TRAILING_COMMA;
			} else {
				p_token_punct(p, ch);
				if (ch == '=')
					p_nbsp(p);
				else if (ch == ',')
					space(p);
				state = ST_PUNCT;
			}
			break;
		}
		case PP_TT_LIT:
			if (state == ST_WORD)
				space(p);
			p_token_literal(p, tt->text);
			state = ST_WORD;
			break;
		case PP_TT_GROUP:
			switch (tt->aux) {
			case PP_DELIM_PAREN:
				p_word(p, "(");
				p_cbox(p, INDENT);
				p_zerobreak(p);
				state = ST_PUNCT;
				break;
			case PP_DELIM_BRACE:
				p_word(p, "{");
				state = ST_PUNCT;
				break;
			case PP_DELIM_BRACKET:
				p_word(p, "[");
				state = ST_PUNCT;
				break;
			}
			/* Upstream has no limit, but its output grows with the square of the depth (one
			 * indented line per level). */
			if (depth == (int)(sizeof stack / sizeof *stack))
				fail("unsupported by protoc-gen-uni: attribute tokens nested more than 255 levels deep");
			stack[depth++] = (Frame){tt, 0};
			space = p_space;
			break;
		}
	}
}
