/* Port of prettyplease 0.2.37 src/mac.rs and src/token.rs (Copyright David Tolnay, MIT OR
 * Apache-2.0), without the "verbatim" feature (buffa-codegen, tonic-build and connectrpc-codegen use
 * the default features): macro bodies print as token trees. `macro_rules!` definitions are not
 * ported (generated code has none); they fail. */

static void p_macro_rules_tokens(Pp *p, const PpList *stream, bool matcher);

/* ---- token.rs ---- */

static void p_ident(Pp *p, const char *ident) { p_word(p, ident); }

static void p_token_punct(Pp *p, char ch)
{
	static const char puncts[] = "!#$%&'*+,-./:;<=>?@^|~";
	const char *s = ch ? strchr(puncts, ch) : NULL;
	if (!s)
		fail("prettyplease: invalid punctuation %d", ch);
	p_wordn(p, s, 1);
}

static void p_token_literal(Pp *p, const char *repr) { p_word(p, repr); }

static void p_delimiter_open(Pp *p, int delimiter)
{
	static const char *const open[] = {"(", "{", "["};
	if (delimiter != PP_DELIM_NONE)
		p_word(p, open[delimiter]);
}

static void p_delimiter_close(Pp *p, int delimiter)
{
	static const char *const close[] = {")", "}", "]"};
	if (delimiter != PP_DELIM_NONE)
		p_word(p, close[delimiter]);
}

static void p_single_token(Pp *p, const PpNode *token, bool matcher)
{
	switch (token->kind) {
	case PP_TT_GROUP:
		p_delimiter_open(p, token->aux);
		if (token->list.n) {
			if (token->aux == PP_DELIM_BRACE)
				p_space(p);
			p_macro_rules_tokens(p, &token->list, matcher);
			if (token->aux == PP_DELIM_BRACE)
				p_space(p);
		}
		p_delimiter_close(p, token->aux);
		break;
	case PP_TT_IDENT:
		p_ident(p, token->text);
		break;
	case PP_TT_PUNCT:
		p_token_punct(p, (char)token->aux);
		break;
	default:
		p_token_literal(p, token->text);
	}
}

/* ---- mac.rs ---- */

static bool requires_semi(int delimiter) { return delimiter != PP_DELIM_BRACE; }

static void p_mac(Pp *p, const PpNode *mac, const char *ident, bool semicolon)
{
	if (ident && path_is_ident(mac->a, "macro_rules"))
		fail("prettyplease: macro_rules! is not supported");
	p_path(p, mac->a, PATH_SIMPLE);
	p_word(p, "!");
	if (ident) {
		p_nbsp(p);
		p_ident(p, ident);
	}
	static const char *const open[] = {"(", " {", "["}, *const close[] = {")", "}", "]"};
	int d = mac->aux;
	void (*delimiter_break)(Pp *) = d == PP_DELIM_BRACE ? p_hardbreak : p_zerobreak;
	p_word(p, open[d]);
	if (mac->list.n) {
		p_cbox(p, INDENT);
		delimiter_break(p);
		p_ibox(p, 0);
		p_macro_rules_tokens(p, &mac->list, false);
		p_end(p);
		delimiter_break(p);
		p_offset(p, -INDENT);
		p_end(p);
	}
	p_word(p, close[d]);
	if (semicolon)
		p_word(p, ";");
}

static bool is_keyword(const char *ident)
{
	static const char *const keywords[] = {
		"as", "async", "await", "box", "break", "const", "continue", "crate", "dyn", "else",
		"enum", "extern", "fn", "for", "if", "impl", "in", "let", "loop", "macro", "match", "mod",
		"move", "mut", "pub", "ref", "return", "static", "struct", "trait", "type", "unsafe", "use",
		"where", "while", "yield"};
	for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++)
		if (str_eq(ident, keywords[i]))
			return true;
	return false;
}

enum {
	MS_START, MS_DOLLAR, MS_DOLLAR_CRATE, MS_DOLLAR_IDENT, MS_DOLLAR_IDENT_COLON, MS_DOLLAR_PAREN,
	MS_DOLLAR_PAREN_SEP, MS_POUND, MS_POUND_BANG, MS_DOT, MS_COLON, MS_COLON2, MS_IDENT,
	MS_IDENT_BANG, MS_DELIM, MS_OTHER
};

static void p_macro_rules_tokens(Pp *p, const PpList *stream, bool matcher)
{
	int state = MS_START;
	bool previous_is_joint = true;
	for (int i = 0; i < stream->n; i++) {
		const PpNode *t = stream->v[i];
		bool needs_space;
		int next;
		int k = t->kind;
		char ch = k == PP_TT_PUNCT ? (char)t->aux : 0;
		bool joint = k == PP_TT_PUNCT && (t->flags & PP_F_JOINT);
		int delim = k == PP_TT_GROUP ? t->aux : -1;
		bool paren_or_bracket = delim == PP_DELIM_PAREN || delim == PP_DELIM_BRACKET;
#define R(sp, st) (needs_space = (sp), next = (st))
		if (state == MS_DOLLAR && k == PP_TT_IDENT && matcher)
			R(false, MS_DOLLAR_IDENT);
		else if (state == MS_DOLLAR && k == PP_TT_IDENT && str_eq(t->text, "crate"))
			R(false, MS_DOLLAR_CRATE);
		else if (state == MS_DOLLAR && k == PP_TT_IDENT)
			R(false, MS_OTHER);
		else if (state == MS_DOLLAR_IDENT && ch == ':' && !joint)
			R(false, MS_DOLLAR_IDENT_COLON);
		else if (state == MS_DOLLAR_IDENT_COLON && k == PP_TT_IDENT)
			R(false, MS_OTHER);
		else if (state == MS_DOLLAR_PAREN && (ch == '+' || ch == '*' || ch == '?') && !joint)
			R(false, MS_OTHER);
		else if (state == MS_DOLLAR_PAREN && (k == PP_TT_IDENT || k == PP_TT_LIT))
			R(false, MS_DOLLAR_PAREN_SEP);
		else if (state == MS_DOLLAR_PAREN && k == PP_TT_PUNCT && joint)
			R(false, MS_DOLLAR_PAREN);
		else if (state == MS_DOLLAR_PAREN && k == PP_TT_PUNCT)
			R(false, MS_DOLLAR_PAREN_SEP);
		else if (state == MS_DOLLAR_PAREN_SEP && (ch == '+' || ch == '*'))
			R(false, MS_OTHER);
		else if (state == MS_POUND && ch == '!')
			R(false, MS_POUND_BANG);
		else if (state == MS_DOLLAR && delim == PP_DELIM_PAREN)
			R(false, MS_DOLLAR_PAREN);
		else if ((state == MS_POUND || state == MS_POUND_BANG) && delim == PP_DELIM_BRACKET)
			R(false, MS_OTHER);
		else if (state == MS_IDENT && paren_or_bracket)
			R(false, MS_DELIM);
		else if (state == MS_IDENT && ch == '!' && !joint)
			R(false, MS_IDENT_BANG);
		else if (state == MS_IDENT_BANG && paren_or_bracket)
			R(false, MS_OTHER);
		else if (state == MS_COLON && ch == ':')
			R(false, MS_COLON2);
		else if (paren_or_bracket)
			R(true, MS_DELIM);
		else if (delim == PP_DELIM_BRACE || delim == PP_DELIM_NONE)
			R(true, MS_OTHER);
		else if (k == PP_TT_IDENT && !is_keyword(t->text))
			R(state != MS_DOT && state != MS_COLON2, MS_IDENT);
		else if (k == PP_TT_LIT && has_suffix(t->text, "."))
			R(state != MS_DOT, MS_OTHER);
		else if (k == PP_TT_LIT)
			R(state != MS_DOT, MS_IDENT);
		else if (ch == ',' || ch == ';')
			R(false, MS_OTHER);
		else if (ch == '.' && !matcher)
			R(state != MS_IDENT && state != MS_DELIM, MS_DOT);
		else if (ch == ':' && joint)
			R(state != MS_IDENT && state != MS_DOLLAR_CRATE, MS_COLON);
		else if (ch == '$')
			R(true, MS_DOLLAR);
		else if (ch == '#')
			R(true, MS_POUND);
		else
			R(true, MS_OTHER);
#undef R
		if (!previous_is_joint) {
			if (needs_space)
				p_space(p);
			else if (ch == '.')
				p_zerobreak(p);
		}
		previous_is_joint = joint || ch == '$';
		p_single_token(p, t, matcher);
		state = next;
	}
}
