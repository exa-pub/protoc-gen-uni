/* Layout of a node tree into the virtual source dprint-plugin-typescript formats: the text ts-poet
 * would have printed for the nodes (up to the width of the white space within a line), the tokens
 * swc_ecma_parser 0.139.0 would have captured from it, the comments with swc's leading/trailing
 * attachment, and every node's swc span. dprint's rules query these positions (line of a node,
 * previous and next token, comments around a node, blank lines), so they must be those of the
 * parsed text; the layout hints of the nodes (nl, nl_after_cm, hints, comments) carry what the
 * generator's text would have had.
 *
 * Spans follow swc: a node spans from its first token to the last token it consumed (statements
 * and type members include their `;`/`,`), with swc's exceptions: a parameter's binding pattern
 * and identifier extend over its type annotation, an enum member excludes its comma, a type
 * annotation starts at its `:` (or `=>`), ...
 *
 * swc_ecma_parser: Copyright (c) swc project developers, Apache-2.0 License.
 * dprint-swc-ext: Copyright (c) 2020-2023 David Sherret, MIT License. */

#define LAY_MAX_DEPTH 1000

static const char *const ts_keywords[] = {"await", "break", "case", "catch", "continue", "debugger", "default",
					  "do", "else", "finally", "for", "function", "if", "return", "switch",
					  "throw", "try", "var", "let", "const", "while", "with", "new", "this",
					  "super", "class", "extends", "export", "import", "yield", "in",
					  "instanceof", "typeof", "void", "delete"};

/* swc's Word classification of an identifier-like token */
static uint8_t word_class(const char *s, size_t n)
{
	if ((n == 4 && memcmp(s, "true", 4) == 0) || (n == 5 && memcmp(s, "false", 5) == 0) ||
	    (n == 4 && memcmp(s, "null", 4) == 0))
		return WORD_LITERAL;
	for (size_t i = 0; i < sizeof ts_keywords / sizeof ts_keywords[0]; i++)
		if (strlen(ts_keywords[i]) == n && memcmp(ts_keywords[i], s, n) == 0)
			return WORD_KEYWORD;
	return WORD_IDENT;
}

static const char *tk_text(int kind)
{
	switch (kind) {
	case TK_LBRACE: return "{";
	case TK_RBRACE: return "}";
	case TK_LPAREN: return "(";
	case TK_RPAREN: return ")";
	case TK_LBRACKET: return "[";
	case TK_RBRACKET: return "]";
	case TK_SEMI: return ";";
	case TK_COMMA: return ",";
	case TK_DOT: return ".";
	case TK_DOTDOTDOT: return "...";
	case TK_QUESTION: return "?";
	case TK_QUESTION_DOT: return "?.";
	case TK_COLON: return ":";
	case TK_ARROW: return "=>";
	case TK_AT: return "@";
	case TK_HASH: return "#";
	case TK_TILDE: return "~";
	case TK_BANG: return "!";
	case TK_PLUSPLUS: return "++";
	case TK_MINUSMINUS: return "--";
	case TK_ASSIGN: return "=";
	case TK_ADD_ASSIGN: return "+=";
	case TK_SUB_ASSIGN: return "-=";
	case TK_MUL_ASSIGN: return "*=";
	case TK_DIV_ASSIGN: return "/=";
	case TK_MOD_ASSIGN: return "%=";
	case TK_EXP_ASSIGN: return "**=";
	case TK_LSHIFT_ASSIGN: return "<<=";
	case TK_RSHIFT_ASSIGN: return ">>=";
	case TK_ZFRSHIFT_ASSIGN: return ">>>=";
	case TK_BITOR_ASSIGN: return "|=";
	case TK_BITXOR_ASSIGN: return "^=";
	case TK_BITAND_ASSIGN: return "&=";
	case TK_AND_ASSIGN: return "&&=";
	case TK_OR_ASSIGN: return "||=";
	case TK_NULLISH_ASSIGN: return "?\?=";
	case TK_EQEQ: return "==";
	case TK_NOTEQ: return "!=";
	case TK_EQEQEQ: return "===";
	case TK_NOTEQEQ: return "!==";
	case TK_LT: return "<";
	case TK_LTEQ: return "<=";
	case TK_GT: return ">";
	case TK_GTEQ: return ">=";
	case TK_LSHIFT: return "<<";
	case TK_RSHIFT: return ">>";
	case TK_ZFRSHIFT: return ">>>";
	case TK_ADD: return "+";
	case TK_SUB: return "-";
	case TK_MUL: return "*";
	case TK_DIV: return "/";
	case TK_MOD: return "%";
	case TK_BITOR: return "|";
	case TK_BITXOR: return "^";
	case TK_BITAND: return "&";
	case TK_EXP: return "**";
	case TK_LOGICAL_OR: return "||";
	case TK_LOGICAL_AND: return "&&";
	case TK_NULLISH: return "??";
	case TK_BACKQUOTE: return "`";
	case TK_DOLLAR_LBRACE: return "${";
	default: fail("dprint layout: token kind %d", kind);
	}
}

typedef struct {
	Buf text;
	Tok *toks;
	int nt, ct;
	Comment *cms;
	int nc, cc;
	int pending;	 /* separator before the next item: -1 none, NL_SAME (space), NL_LINE, NL_BLANK */
	uint32_t last_hi; /* end of the last token */
	Node **waiting;	 /* nodes whose span starts at the next token */
	int nwait, cwait;
	int depth;
	bool glue; /* the next token follows the previous one without white space */
} Lay;

static void lay_request(Lay *L, int nl)
{
	if (nl > L->pending)
		L->pending = nl;
}

static void lay_write_sep(Lay *L)
{
	if (L->text.n > 0) {
		if (L->pending == NL_BLANK)
			buf_adds(&L->text, "\n\n");
		else if (L->pending == NL_LINE)
			buf_addc(&L->text, '\n');
		else if (L->pending == NL_SAME)
			buf_addc(&L->text, ' ');
	}
	L->pending = -1;
}

static void lay_tok_n(Lay *L, int kind, const char *s, size_t n)
{
	if (L->text.n + n + 2 >= UINT32_MAX / 2)
		fail("dprint layout: file too large");
	if (L->glue)
		L->pending = -1;
	else
		lay_request(L, NL_SAME);
	L->glue = false;
	lay_write_sep(L);
	if (L->nt == L->ct) {
		int cap = L->ct ? L->ct * 2 : 1024;
		L->toks = xrealloc(L->toks, sizeof(Tok) * (size_t)L->ct, sizeof(Tok) * (size_t)cap);
		L->ct = cap;
	}
	uint32_t lo = (uint32_t)L->text.n;
	uint8_t word = kind == TK_WORD ? word_class(s, n) : 0;
	L->toks[L->nt++] = (Tok){lo, lo + (uint32_t)n, (uint16_t)kind, word, false};
	buf_add(&L->text, s, n);
	for (int i = 0; i < L->nwait; i++)
		L->waiting[i]->lo = lo;
	L->nwait = 0;
	L->last_hi = lo + (uint32_t)n;
}

static void lay_tok(Lay *L, int kind) { lay_tok_n(L, kind, tk_text(kind), strlen(tk_text(kind))); }
static void lay_word(Lay *L, const char *w) { lay_tok_n(L, TK_WORD, w, strlen(w)); }

static void lay_comment(Lay *L, const NodeComment *c)
{
	lay_write_sep(L);
	if (L->nc == L->cc) {
		int cap = L->cc ? L->cc * 2 : 64;
		L->cms = xrealloc(L->cms, sizeof(Comment) * (size_t)L->cc, sizeof(Comment) * (size_t)cap);
		L->cc = cap;
	}
	uint32_t lo = (uint32_t)L->text.n;
	buf_add_str(&L->text, c->text);
	Comment *cm = &L->cms[L->nc++];
	memset(cm, 0, sizeof *cm);
	cm->lo = lo;
	cm->hi = (uint32_t)L->text.n;
	cm->block = c->block;
	L->pending = c->block ? NL_SAME : NL_LINE;
}

/* Starts a node: its line break (or dflt), its comments, and its span start at the next token. */
static void lay_begin(Lay *L, Node *n, int dflt)
{
	if (++L->depth > LAY_MAX_DEPTH)
		fail("dprint layout: nesting too deep");
	int nl = n->nl ? n->nl : dflt;
	if (nl)
		lay_request(L, nl);
	for (int i = 0; i < n->ncm; i++) {
		if (i > 0)
			lay_request(L, n->cm[i].nl ? n->cm[i].nl : NL_LINE);
		lay_comment(L, &n->cm[i]);
	}
	if (n->ncm)
		lay_request(L, n->nl_after_cm ? n->nl_after_cm : NL_LINE);
	if (L->nwait == L->cwait) {
		int cap = L->cwait ? L->cwait * 2 : 16;
		L->waiting = xrealloc(L->waiting, sizeof(Node *) * (size_t)L->cwait, sizeof(Node *) * (size_t)cap);
		L->cwait = cap;
	}
	L->waiting[L->nwait++] = n;
}

static void lay_end(Lay *L, Node *n)
{
	L->depth--;
	n->hi = L->last_hi;
	for (int i = 0; i < n->ntcm; i++) {
		lay_request(L, n->tcm[i].nl ? n->tcm[i].nl : NL_SAME);
		lay_comment(L, &n->tcm[i]);
	}
}

static void lay_text(Lay *L, Node *n, int kind)
{
	if (n->text.n == 0 && kind != TK_TEMPLATE)
		fail("dprint layout: node %d without text", n->kind);
	lay_tok_n(L, kind, n->text.p, n->text.n);
}

static void lay_node(Lay *L, Node *n);
static void lay_inner(Lay *L, Node *n);
static void lay_type(Lay *L, Node *n) { lay_node(L, n); }
static void lay_stmt(Lay *L, Node *n);

/* `;` of a statement unless the hint says it was left out */
static void lay_semi(Lay *L, Node *n)
{
	if (!(n->hints & H_NO_SEMI))
		lay_tok(L, TK_SEMI);
}

static void lay_close(Lay *L, Node *n, int kind)
{
	if (n->hints & H_CLOSE_NL)
		lay_request(L, NL_LINE);
	lay_tok(L, kind);
}

/* comma-separated list (NULL = hole) */
static void lay_list(Lay *L, Node *owner, NodeList *l, void (*fn)(Lay *, Node *))
{
	for (int i = 0; i < l->n; i++) {
		if (!l->v[i]) {
			lay_tok(L, TK_COMMA);
			continue;
		}
		fn(L, l->v[i]);
		if (i + 1 < l->n || (owner->hints & H_TRAILING_COMMA))
			lay_tok(L, TK_COMMA);
	}
}

static void lay_type_ann_tok(Lay *L, Node *ann, int tok)
{
	lay_begin(L, ann, 0);
	lay_tok(L, tok);
	lay_type(L, ann->a);
	lay_end(L, ann);
}

static void lay_type_ann(Lay *L, Node *ann) { lay_type_ann_tok(L, ann, TK_COLON); }

static void lay_type_params(Lay *L, Node *d)
{
	lay_begin(L, d, 0);
	lay_tok(L, TK_LT);
	lay_list(L, d, &d->list, lay_node);
	lay_close(L, d, TK_GT);
	lay_end(L, d);
}

/* Return type after `:` (or `=>`), possibly a type predicate (its TsTypeAnn has no colon). */
static void lay_return_type(Lay *L, Node *ann, int tok)
{
	lay_type_ann_tok(L, ann, tok);
}

/* ---- patterns and parameters ---- */

/* parse_binding_pat */
static void lay_pat(Lay *L, Node *n) { lay_node(L, n); }

/* parse_formal_param_pat: modifiers, pattern, `?`, type annotation (extends the pattern's and the
 * binding identifier's span), default value. */
static void lay_formal_param(Lay *L, Node *n)
{
	if (n->kind == N_PARAM) {
		lay_begin(L, n, 0);
		lay_formal_param(L, n->a);
		lay_end(L, n);
		return;
	}
	if (n->kind == N_TS_PARAM_PROP) {
		lay_begin(L, n, 0);
		if (n->accessibility == ACC_PUBLIC)
			lay_word(L, "public");
		else if (n->accessibility == ACC_PRIVATE)
			lay_word(L, "private");
		else if (n->accessibility == ACC_PROTECTED)
			lay_word(L, "protected");
		if (n->flags & F_OVERRIDE)
			lay_word(L, "override");
		if (n->flags & F_READONLY)
			lay_word(L, "readonly");
		lay_formal_param(L, n->a);
		lay_end(L, n);
		return;
	}
	Node *assign = NULL;
	if (n->kind == N_ASSIGN_PAT) {
		assign = n;
		lay_begin(L, assign, 0);
		n = n->a;
	}
	if (n->kind == N_REST_PAT) {
		lay_begin(L, n, 0);
		lay_tok(L, TK_DOTDOTDOT);
		lay_pat(L, n->a);
		if (n->b)
			lay_type_ann(L, n->b);
		lay_end(L, n);
	} else if (n->kind == N_BINDING_IDENT) {
		/* without a type annotation the spans end before `?` */
		lay_begin(L, n, 0);
		lay_begin(L, n->a, 0);
		lay_text(L, n->a, TK_WORD);
		if (!n->b) {
			lay_end(L, n->a);
			lay_end(L, n);
			if (n->a->flags & F_OPTIONAL)
				lay_tok(L, TK_QUESTION);
		} else {
			if (n->a->flags & F_OPTIONAL)
				lay_tok(L, TK_QUESTION);
			lay_type_ann(L, n->b);
			lay_end(L, n->a);
			lay_end(L, n);
		}
	} else {
		/* array or object pattern: the span extends over `?` and the type annotation only if
		 * there is one */
		lay_begin(L, n, 0);
		Node *ann = n->b;
		n->b = NULL;
		lay_inner(L, n);
		n->b = ann;
		if (!ann) {
			lay_end(L, n);
			if (n->flags & F_OPTIONAL)
				lay_tok(L, TK_QUESTION);
		} else {
			if (n->flags & F_OPTIONAL)
				lay_tok(L, TK_QUESTION);
			lay_type_ann(L, ann);
			lay_end(L, n);
		}
	}
	if (assign) {
		lay_tok(L, TK_ASSIGN);
		lay_node(L, assign->b);
		lay_end(L, assign);
	}
}

static void lay_params(Lay *L, Node *owner, NodeList *params)
{
	lay_tok(L, TK_LPAREN);
	lay_list(L, owner, params, lay_formal_param);
	lay_tok(L, TK_RPAREN);
}

/* parse_function_rest: type params, params, return type, body (or `;`) */
static void lay_function_rest(Lay *L, Node *f)
{
	if (f->b)
		lay_type_params(L, f->b);
	lay_params(L, f, &f->list);
	if (f->c)
		lay_return_type(L, f->c, TK_COLON);
	if (f->a)
		lay_stmt(L, f->a);
	else
		lay_semi(L, f);
}

static void lay_prop_name(Lay *L, Node *n) { lay_node(L, n); }

/* ---- type members ---- */

static void lay_type_member_sep(Lay *L, Node *n)
{
	if (n->hints & H_SEP_COMMA)
		lay_tok(L, TK_COMMA);
	else
		lay_semi(L, n);
}

static void lay_type_member(Lay *L, Node *n, int dflt)
{
	lay_begin(L, n, dflt);
	switch (n->kind) {
	case N_TS_CALL_SIGNATURE_DECL:
	case N_TS_CONSTRUCT_SIGNATURE_DECL:
		if (n->kind == N_TS_CONSTRUCT_SIGNATURE_DECL)
			lay_word(L, "new");
		if (n->b)
			lay_type_params(L, n->b);
		lay_params(L, n, &n->list);
		if (n->c)
			lay_return_type(L, n->c, TK_COLON);
		break;
	case N_TS_INDEX_SIGNATURE: {
		if (n->flags & F_STATIC)
			lay_word(L, "static");
		if (n->flags & F_READONLY)
			lay_word(L, "readonly");
		lay_tok(L, TK_LBRACKET);
		Node *param = n->list.v[0];
		lay_begin(L, param, 0);
		lay_begin(L, param->a, 0);
		lay_text(L, param->a, TK_WORD);
		lay_type_ann(L, param->b);
		lay_end(L, param->a);
		lay_end(L, param);
		lay_tok(L, TK_RBRACKET);
		if (n->c)
			lay_type_ann(L, n->c);
		break;
	}
	case N_TS_GETTER_SIGNATURE:
	case N_TS_SETTER_SIGNATURE:
		lay_word(L, n->kind == N_TS_GETTER_SIGNATURE ? "get" : "set");
		lay_prop_name(L, n->a);
		lay_tok(L, TK_LPAREN);
		if (n->b)
			lay_formal_param(L, n->b);
		lay_tok(L, TK_RPAREN);
		if (n->c)
			lay_return_type(L, n->c, TK_COLON);
		break;
	case N_TS_METHOD_SIGNATURE:
		lay_prop_name(L, n->a);
		if (n->flags & F_OPTIONAL)
			lay_tok(L, TK_QUESTION);
		if (n->b)
			lay_type_params(L, n->b);
		lay_params(L, n, &n->list);
		if (n->c)
			lay_return_type(L, n->c, TK_COLON);
		break;
	case N_TS_PROPERTY_SIGNATURE:
		if (n->flags & F_READONLY)
			lay_word(L, "readonly");
		lay_prop_name(L, n->a);
		if (n->flags & F_OPTIONAL)
			lay_tok(L, TK_QUESTION);
		if (n->c)
			lay_type_ann(L, n->c);
		break;
	default:
		fail("dprint layout: node %d is not a type member", n->kind);
	}
	lay_type_member_sep(L, n);
	lay_end(L, n);
}

/* ---- templates ---- */

static void lay_tpl(Lay *L, Node *t)
{
	lay_begin(L, t, 0);
	lay_tok(L, TK_BACKQUOTE);
	for (int i = 0; i < t->list2.n; i++) {
		Node *q = t->list2.v[i];
		lay_begin(L, q, 0);
		/* a template chunk follows the previous token without a separator */
		L->glue = true;
		lay_tok_n(L, TK_TEMPLATE, q->text.p, q->text.n);
		lay_end(L, q);
		L->glue = true;
		if (i + 1 == t->list2.n) {
			q->flags |= F_TPL_TAIL;
			lay_tok(L, TK_BACKQUOTE);
			break;
		}
		if (i >= t->list.n)
			fail("dprint layout: template without expression");
		L->glue = true;
		lay_tok(L, TK_DOLLAR_LBRACE);
		lay_node(L, t->list.v[i]);
		lay_tok(L, TK_RBRACE);
		L->glue = true;
	}
	lay_end(L, t);
}

/* ---- classes ---- */

static void lay_class_member(Lay *L, Node *n)
{
	lay_begin(L, n, NL_LINE);
	if (n->flags & F_DECLARE)
		lay_word(L, "declare");
	if (n->accessibility == ACC_PUBLIC)
		lay_word(L, "public");
	else if (n->accessibility == ACC_PRIVATE)
		lay_word(L, "private");
	else if (n->accessibility == ACC_PROTECTED)
		lay_word(L, "protected");
	if (n->flags & F_STATIC)
		lay_word(L, "static");
	if (n->flags & F_ABSTRACT)
		lay_word(L, "abstract");
	if (n->flags & F_OVERRIDE)
		lay_word(L, "override");
	if (n->flags & F_READONLY)
		lay_word(L, "readonly");
	switch (n->kind) {
	case N_CONSTRUCTOR:
		lay_node(L, n->a);
		lay_params(L, n, &n->list);
		if (n->b)
			lay_stmt(L, n->b);
		else
			lay_semi(L, n);
		break;
	case N_CLASS_METHOD: {
		Node *f = n->b;
		lay_begin(L, f, 0);
		if (f->flags & F_ASYNC)
			lay_word(L, "async");
		if (f->flags & F_GENERATOR)
			lay_tok(L, TK_MUL);
		if (n->op == METHOD_GETTER)
			lay_word(L, "get");
		else if (n->op == METHOD_SETTER)
			lay_word(L, "set");
		lay_prop_name(L, n->a);
		if (n->flags & F_OPTIONAL)
			lay_tok(L, TK_QUESTION);
		lay_function_rest(L, f);
		lay_end(L, f);
		/* the function spans from the member start */
		f->lo = n->lo;
		break;
	}
	case N_CLASS_PROP:
		lay_prop_name(L, n->a);
		if (n->flags & F_OPTIONAL)
			lay_tok(L, TK_QUESTION);
		if (n->flags & F_DEFINITE)
			lay_tok(L, TK_BANG);
		if (n->c)
			lay_type_ann(L, n->c);
		if (n->b) {
			lay_tok(L, TK_ASSIGN);
			lay_node(L, n->b);
		}
		lay_semi(L, n);
		break;
	default:
		fail("dprint layout: node %d is not a class member", n->kind);
	}
	lay_end(L, n);
}

/* `class` keyword onwards; the N_CLASS node's span starts at `start_node` (the declaration or
 * `abstract`). */
static void lay_class(Lay *L, Node *c, Node *ident)
{
	lay_word(L, "class");
	if (ident)
		lay_node(L, ident);
	if (c->b)
		lay_type_params(L, c->b);
	if (c->a) {
		lay_word(L, "extends");
		lay_node(L, c->a);
		if (c->c)
			lay_type_params(L, c->c);
	}
	/* swc ends the list at a word `implements` (is_ts_list_terminator) and drops the rest as a
	 * second implements clause (TS1175, not an error for dprint): `implements implements {`
	 * becomes a class without implements */
	for (int i = 0; i < c->list3.n; i++) {
		Node *e = c->list3.v[i];
		if (e && e->kind == N_TS_EXPR_WITH_TYPE_ARGS && e->a && e->a->kind == N_IDENT &&
		    str_eqc(e->a->text, "implements")) {
			c->list3.n = i;
			break;
		}
	}
	if (c->list3.n) {
		lay_word(L, "implements");
		lay_list(L, c, &c->list3, lay_node);
	}
	lay_tok(L, TK_LBRACE);
	for (int i = 0; i < c->list.n; i++)
		lay_class_member(L, c->list.v[i]);
	if (c->list.n)
		lay_request(L, NL_LINE);
	lay_close(L, c, TK_RBRACE);
}

/* ---- statements ---- */

static void lay_var_decl(Lay *L, Node *v, bool for_loop)
{
	if (v->flags & F_DECLARE)
		lay_word(L, "declare");
	lay_word(L, v->op == VAR_VAR ? "var" : v->op == VAR_LET ? "let" : "const");
	for (int i = 0; i < v->list.n; i++) {
		Node *d = v->list.v[i];
		lay_begin(L, d, 0);
		Node *ann = d->a->b;
		d->a->b = NULL;
		lay_pat(L, d->a);
		d->a->b = ann;
		if (d->flags & F_DEFINITE)
			lay_tok(L, TK_BANG);
		if (ann)
			lay_type_ann(L, ann);
		if (d->b) {
			lay_tok(L, TK_ASSIGN);
			lay_node(L, d->b);
		}
		lay_end(L, d);
		if (i + 1 < v->list.n)
			lay_tok(L, TK_COMMA);
	}
	if (!for_loop)
		lay_semi(L, v);
}

static void lay_block_body(Lay *L, Node *b, NodeList *stmts)
{
	lay_tok(L, TK_LBRACE);
	for (int i = 0; i < stmts->n; i++)
		lay_stmt(L, stmts->v[i]);
	if (stmts->n)
		lay_request(L, NL_LINE);
	lay_close(L, b, TK_RBRACE);
}

static void lay_decl(Lay *L, Node *n);
static void lay_decl_inner(Lay *L, Node *n);

static void lay_stmt_inner(Lay *L, Node *n)
{
	switch (n->kind) {
	case N_EXPR_STMT:
		lay_node(L, n->a);
		lay_semi(L, n);
		break;
	case N_BLOCK_STMT:
		lay_block_body(L, n, &n->list);
		break;
	case N_EMPTY_STMT:
		lay_tok(L, TK_SEMI);
		break;
	case N_IF_STMT:
		lay_word(L, "if");
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_tok(L, TK_RPAREN);
		lay_stmt(L, n->b);
		if (n->c) {
			if (n->hints & H_OP_NL)
				lay_request(L, NL_LINE);
			lay_word(L, "else");
			lay_stmt(L, n->c);
		}
		break;
	case N_RETURN_STMT:
	case N_THROW_STMT:
		lay_word(L, n->kind == N_RETURN_STMT ? "return" : "throw");
		if (n->a)
			lay_node(L, n->a);
		lay_semi(L, n);
		break;
	case N_BREAK_STMT:
	case N_CONTINUE_STMT:
		lay_word(L, n->kind == N_BREAK_STMT ? "break" : "continue");
		if (n->a)
			lay_node(L, n->a);
		lay_semi(L, n);
		break;
	case N_DEBUGGER_STMT:
		lay_word(L, "debugger");
		lay_semi(L, n);
		break;
	case N_WHILE_STMT:
		lay_word(L, "while");
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_tok(L, TK_RPAREN);
		lay_stmt(L, n->d);
		break;
	case N_DO_WHILE_STMT:
		lay_word(L, "do");
		lay_stmt(L, n->d);
		lay_word(L, "while");
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_tok(L, TK_RPAREN);
		lay_semi(L, n);
		break;
	case N_FOR_STMT:
	case N_FOR_OF_STMT:
	case N_FOR_IN_STMT:
		lay_word(L, "for");
		if (n->flags & F_AWAIT)
			lay_word(L, "await");
		lay_tok(L, TK_LPAREN);
		if (n->a) {
			if (n->a->kind == N_VAR_DECL) {
				lay_begin(L, n->a, 0);
				lay_var_decl(L, n->a, true);
				lay_end(L, n->a);
			} else {
				lay_node(L, n->a);
			}
		}
		if (n->kind == N_FOR_STMT) {
			lay_tok(L, TK_SEMI);
			if (n->b)
				lay_node(L, n->b);
			lay_tok(L, TK_SEMI);
			if (n->c)
				lay_node(L, n->c);
		} else {
			lay_word(L, n->kind == N_FOR_OF_STMT ? "of" : "in");
			lay_node(L, n->b);
		}
		lay_tok(L, TK_RPAREN);
		lay_stmt(L, n->d);
		break;
	case N_SWITCH_STMT:
		lay_word(L, "switch");
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_tok(L, TK_RPAREN);
		lay_tok(L, TK_LBRACE);
		for (int i = 0; i < n->list.n; i++) {
			Node *c = n->list.v[i];
			lay_begin(L, c, NL_LINE);
			if (c->a) {
				lay_word(L, "case");
				lay_node(L, c->a);
			} else {
				lay_word(L, "default");
			}
			lay_tok(L, TK_COLON);
			for (int k = 0; k < c->list.n; k++)
				lay_stmt(L, c->list.v[k]);
			lay_end(L, c);
		}
		if (n->list.n)
			lay_request(L, NL_LINE);
		lay_close(L, n, TK_RBRACE);
		break;
	case N_TRY_STMT:
		lay_word(L, "try");
		lay_stmt(L, n->a);
		if (n->b) {
			Node *c = n->b;
			lay_begin(L, c, 0);
			lay_word(L, "catch");
			if (c->a) {
				lay_tok(L, TK_LPAREN);
				Node *ann = c->a->b;
				c->a->b = NULL;
				lay_pat(L, c->a);
				c->a->b = ann;
				if (ann)
					lay_type_ann(L, ann);
				lay_tok(L, TK_RPAREN);
			}
			lay_stmt(L, c->b);
			lay_end(L, c);
		}
		if (n->c) {
			lay_word(L, "finally");
			lay_stmt(L, n->c);
		}
		break;
	case N_LABELED_STMT:
		lay_node(L, n->a);
		lay_tok(L, TK_COLON);
		lay_stmt(L, n->b);
		break;
	case N_TS_IMPORT_EQUALS_DECL:
		lay_word(L, "import");
		if (n->flags & F_TYPE_ONLY)
			lay_word(L, "type");
		lay_node(L, n->a);
		lay_tok(L, TK_ASSIGN);
		lay_node(L, n->b);
		lay_semi(L, n);
		break;
	case N_IMPORT_DECL: {
		lay_word(L, "import");
		if (n->flags & F_TYPE_ONLY)
			lay_word(L, "type");
		int i = 0;
		bool any = n->list.n > 0;
		if (i < n->list.n && n->list.v[i]->kind == N_IMPORT_DEFAULT_SPECIFIER) {
			Node *s = n->list.v[i++];
			lay_begin(L, s, 0);
			lay_node(L, s->a);
			lay_end(L, s);
			if (i < n->list.n)
				lay_tok(L, TK_COMMA);
		}
		if (i < n->list.n && n->list.v[i]->kind == N_IMPORT_STAR_AS_SPECIFIER) {
			Node *s = n->list.v[i++];
			lay_begin(L, s, 0);
			lay_tok(L, TK_MUL);
			lay_word(L, "as");
			lay_node(L, s->a);
			lay_end(L, s);
		} else if (i < n->list.n || (any && n->list.v[0]->kind != N_IMPORT_DEFAULT_SPECIFIER) ||
			   (n->hints & H_TRAILING_COMMA)) {
			lay_tok(L, TK_LBRACE);
			for (; i < n->list.n; i++) {
				Node *s = n->list.v[i];
				lay_begin(L, s, 0);
				if (s->flags & F_TYPE_ONLY)
					lay_word(L, "type");
				if (s->b) {
					lay_node(L, s->b);
					lay_word(L, "as");
				}
				lay_node(L, s->a);
				lay_end(L, s);
				if (i + 1 < n->list.n || (n->hints & H_TRAILING_COMMA))
					lay_tok(L, TK_COMMA);
			}
			lay_close(L, n, TK_RBRACE);
		}
		if (any || (n->hints & H_TRAILING_COMMA))
			lay_word(L, "from");
		lay_node(L, n->a);
		lay_semi(L, n);
		break;
	}
	case N_EXPORT_DECL:
		lay_word(L, "export");
		lay_decl(L, n->a);
		break;
	case N_EXPORT_DEFAULT_EXPR:
		lay_word(L, "export");
		lay_word(L, "default");
		lay_node(L, n->a);
		lay_semi(L, n);
		break;
	case N_EXPORT_ALL:
		lay_word(L, "export");
		if (n->flags & F_TYPE_ONLY)
			lay_word(L, "type");
		lay_tok(L, TK_MUL);
		lay_word(L, "from");
		lay_node(L, n->a);
		lay_semi(L, n);
		break;
	case N_NAMED_EXPORT: {
		lay_word(L, "export");
		if (n->flags & F_TYPE_ONLY)
			lay_word(L, "type");
		int first = 0;
		if (n->list.n && n->list.v[0]->kind == N_EXPORT_NAMESPACE_SPECIFIER) {
			Node *s = n->list.v[first++];
			lay_begin(L, s, 0);
			lay_tok(L, TK_MUL);
			lay_word(L, "as");
			lay_node(L, s->a);
			lay_end(L, s);
			if (first < n->list.n)
				lay_tok(L, TK_COMMA);
			else
				goto named_export_from;
		}
		lay_tok(L, TK_LBRACE);
		for (int i = first; i < n->list.n; i++) {
			Node *s = n->list.v[i];
			lay_begin(L, s, 0);
			if (s->flags & F_TYPE_ONLY)
				lay_word(L, "type");
			lay_node(L, s->a);
			if (s->b) {
				lay_word(L, "as");
				lay_node(L, s->b);
			}
			lay_end(L, s);
			if (i + 1 < n->list.n || (n->hints & H_TRAILING_COMMA))
				lay_tok(L, TK_COMMA);
		}
		lay_close(L, n, TK_RBRACE);
	named_export_from:
		if (n->a) {
			lay_word(L, "from");
			lay_node(L, n->a);
		}
		lay_semi(L, n);
		break;
	}
	default:
		lay_decl_inner(L, n);
		return;
	}
}

static void lay_decl_inner(Lay *L, Node *n)
{
	switch (n->kind) {
	case N_VAR_DECL:
		lay_var_decl(L, n, false);
		break;
	case N_FN_DECL: {
		Node *f = n->b;
		lay_begin(L, f, 0);
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		if (f->flags & F_ASYNC)
			lay_word(L, "async");
		lay_word(L, "function");
		if (f->flags & F_GENERATOR)
			lay_tok(L, TK_MUL);
		lay_node(L, n->a);
		lay_function_rest(L, f);
		lay_end(L, f);
		break;
	}
	case N_CLASS_DECL: {
		Node *c = n->b;
		lay_begin(L, c, 0);
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		if (c->flags & F_ABSTRACT)
			lay_word(L, "abstract");
		lay_class(L, c, n->a);
		lay_end(L, c);
		break;
	}
	case N_TS_INTERFACE_DECL: {
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		lay_word(L, "interface");
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		if (n->list.n) {
			lay_word(L, "extends");
			lay_list(L, n, &n->list, lay_node);
		}
		Node *body = n->c;
		lay_begin(L, body, 0);
		lay_tok(L, TK_LBRACE);
		for (int i = 0; i < body->list.n; i++)
			lay_type_member(L, body->list.v[i], NL_LINE);
		if (body->list.n)
			lay_request(L, NL_LINE);
		lay_close(L, body, TK_RBRACE);
		lay_end(L, body);
		break;
	}
	case N_TS_TYPE_ALIAS_DECL:
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		lay_word(L, "type");
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		if (n->hints & H_OP_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_ASSIGN);
		lay_type(L, n->c);
		lay_semi(L, n);
		break;
	case N_TS_ENUM_DECL:
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		if (n->flags & F_CONST)
			lay_word(L, "const");
		lay_word(L, "enum");
		lay_node(L, n->a);
		lay_tok(L, TK_LBRACE);
		for (int i = 0; i < n->list.n; i++) {
			Node *m = n->list.v[i];
			lay_begin(L, m, NL_LINE);
			lay_node(L, m->a);
			if (m->b) {
				lay_tok(L, TK_ASSIGN);
				lay_node(L, m->b);
			}
			lay_end(L, m);
			if (i + 1 < n->list.n || (n->hints & H_TRAILING_COMMA))
				lay_tok(L, TK_COMMA);
		}
		if (n->list.n)
			lay_request(L, NL_LINE);
		lay_close(L, n, TK_RBRACE);
		break;
	case N_TS_MODULE_DECL: {
		if (n->flags & F_DECLARE)
			lay_word(L, "declare");
		lay_word(L, (n->flags & F_NAMESPACE) ? "namespace" : "module");
		lay_node(L, n->a);
		Node *body = n->b;
		lay_begin(L, body, 0);
		lay_block_body(L, body, &body->list);
		lay_end(L, body);
		break;
	}
	default:
		fail("dprint layout: node %d is not a statement", n->kind);
	}
}

static void lay_decl(Lay *L, Node *n)
{
	lay_begin(L, n, 0);
	lay_decl_inner(L, n);
	lay_end(L, n);
}

static void lay_stmt(Lay *L, Node *n)
{
	lay_begin(L, n, NL_LINE);
	lay_stmt_inner(L, n);
	lay_end(L, n);
}

/* ---- expressions and types ---- */

static void lay_member(Lay *L, Node *m, bool optional)
{
	lay_node(L, m->a);
	if (m->hints & H_OP_NL)
		lay_request(L, NL_LINE);
	if (optional)
		lay_tok(L, TK_QUESTION_DOT);
	if (m->b->kind == N_COMPUTED_PROP_NAME) {
		lay_node(L, m->b);
	} else {
		if (!optional)
			lay_tok(L, TK_DOT);
		lay_node(L, m->b);
	}
}

static void lay_call(Lay *L, Node *c, bool optional)
{
	lay_node(L, c->a);
	if (optional)
		lay_tok(L, TK_QUESTION_DOT);
	if (c->b)
		lay_type_params(L, c->b);
	lay_tok(L, TK_LPAREN);
	lay_list(L, c, &c->list, lay_node);
	lay_close(L, c, TK_RPAREN);
}

static void lay_union_or_intersection(Lay *L, Node *n, int op)
{
	if (n->hints & H_LEADING_OP)
		lay_tok(L, op);
	for (int i = 0; i < n->list.n; i++) {
		if (i > 0) {
			if (n->list.v[i]->hints & H_OP_NL)
				lay_request(L, NL_LINE);
			lay_tok(L, op);
		}
		lay_type(L, n->list.v[i]);
	}
}

static void lay_inner(Lay *L, Node *n)
{
	switch (n->kind) {
	case N_IDENT:
		lay_text(L, n, TK_WORD);
		break;
	case N_PRIVATE_NAME:
		lay_tok(L, TK_HASH);
		lay_node(L, n->a);
		break;
	case N_THIS_EXPR:
		lay_word(L, "this");
		break;
	case N_SUPER:
		lay_word(L, "super");
		break;
	case N_NULL:
		lay_word(L, "null");
		break;
	case N_BOOL:
		lay_text(L, n, TK_WORD);
		break;
	case N_STR:
		lay_text(L, n, TK_STR);
		break;
	case N_NUMBER:
		lay_text(L, n, TK_NUM);
		break;
	case N_BIGINT:
		lay_text(L, n, TK_BIGINT);
		break;
	case N_REGEX:
		lay_text(L, n, TK_REGEX);
		break;
	case N_ARRAY_LIT:
		lay_tok(L, TK_LBRACKET);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_RBRACKET);
		break;
	case N_OBJECT_LIT:
		lay_tok(L, TK_LBRACE);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_RBRACE);
		break;
	case N_KEY_VALUE_PROP:
		lay_prop_name(L, n->a);
		lay_tok(L, TK_COLON);
		lay_node(L, n->b);
		break;
	case N_ASSIGN_PROP:
		lay_node(L, n->a);
		lay_tok(L, TK_ASSIGN);
		lay_node(L, n->b);
		break;
	case N_METHOD_PROP: {
		Node *f = n->b;
		lay_begin(L, f, 0);
		if (f->flags & F_ASYNC)
			lay_word(L, "async");
		if (f->flags & F_GENERATOR)
			lay_tok(L, TK_MUL);
		lay_prop_name(L, n->a);
		lay_function_rest(L, f);
		lay_end(L, f);
		break;
	}
	case N_GETTER_PROP:
		lay_word(L, "get");
		lay_prop_name(L, n->a);
		lay_tok(L, TK_LPAREN);
		lay_tok(L, TK_RPAREN);
		if (n->b)
			lay_type_ann(L, n->b);
		lay_stmt(L, n->c);
		break;
	case N_SETTER_PROP:
		lay_word(L, "set");
		lay_prop_name(L, n->a);
		lay_tok(L, TK_LPAREN);
		lay_formal_param(L, n->b);
		lay_tok(L, TK_RPAREN);
		lay_stmt(L, n->c);
		break;
	case N_SPREAD_ELEMENT:
		lay_tok(L, TK_DOTDOTDOT);
		lay_node(L, n->a);
		break;
	case N_EXPR_OR_SPREAD:
		if (n->flags & F_SPREAD)
			lay_tok(L, TK_DOTDOTDOT);
		lay_node(L, n->a);
		break;
	case N_FN_EXPR: {
		Node *f = n->b;
		lay_begin(L, f, 0);
		if (f->flags & F_ASYNC)
			lay_word(L, "async");
		lay_word(L, "function");
		if (f->flags & F_GENERATOR)
			lay_tok(L, TK_MUL);
		if (n->a)
			lay_node(L, n->a);
		lay_function_rest(L, f);
		lay_end(L, f);
		break;
	}
	case N_ARROW_EXPR:
		if (n->flags & F_ASYNC)
			lay_word(L, "async");
		if (n->hints & H_NO_PARENS) {
			lay_node(L, n->list.v[0]);
		} else {
			if (n->b)
				lay_type_params(L, n->b);
			lay_tok(L, TK_LPAREN);
			lay_list(L, n, &n->list, lay_formal_param);
			lay_tok(L, TK_RPAREN);
			if (n->c)
				lay_return_type(L, n->c, TK_COLON);
		}
		if (n->hints & H_OP_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_ARROW);
		if (n->a->kind == N_BLOCK_STMT) {
			lay_begin(L, n->a, 0);
			lay_block_body(L, n->a, &n->a->list);
			lay_end(L, n->a);
		} else {
			lay_node(L, n->a);
		}
		break;
	case N_CLASS_EXPR: {
		Node *c = n->b;
		lay_begin(L, c, 0);
		lay_class(L, c, n->a);
		lay_end(L, c);
		break;
	}
	case N_UNARY_EXPR:
		switch (n->op) {
		case UNARY_MINUS: lay_tok(L, TK_SUB); break;
		case UNARY_PLUS: lay_tok(L, TK_ADD); break;
		case UNARY_BANG: lay_tok(L, TK_BANG); break;
		case UNARY_TILDE: lay_tok(L, TK_TILDE); break;
		case UNARY_TYPEOF: lay_word(L, "typeof"); break;
		case UNARY_VOID: lay_word(L, "void"); break;
		case UNARY_DELETE: lay_word(L, "delete"); break;
		default: fail("dprint layout: unary operator %d", n->op);
		}
		lay_node(L, n->a);
		break;
	case N_UPDATE_EXPR:
		if (n->flags & F_PREFIX)
			lay_tok(L, n->op);
		lay_node(L, n->a);
		if (!(n->flags & F_PREFIX))
			lay_tok(L, n->op);
		break;
	case N_BIN_EXPR:
		lay_node(L, n->a);
		if (n->hints & H_OP_NL)
			lay_request(L, NL_LINE);
		if (n->op == TK_WORD)
			lay_word(L, (n->flags & F_INSTANCEOF) ? "instanceof" : "in");
		else
			lay_tok(L, n->op);
		lay_node(L, n->b);
		break;
	case N_ASSIGN_EXPR:
		lay_node(L, n->a);
		lay_tok(L, n->op);
		lay_node(L, n->b);
		break;
	case N_MEMBER_EXPR:
		lay_member(L, n, false);
		break;
	case N_OPT_CHAIN_EXPR: {
		Node *b = n->a;
		bool opt = (n->flags & F_OPTIONAL) != 0;
		lay_begin(L, b, 0);
		if (b->kind == N_OPT_CALL)
			lay_call(L, b, opt);
		else
			lay_member(L, b, opt);
		lay_end(L, b);
		break;
	}
	case N_COND_EXPR:
		lay_node(L, n->a);
		if (n->hints & H_OP_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_QUESTION);
		lay_node(L, n->b);
		if (n->hints & H_OP2_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_COLON);
		lay_node(L, n->c);
		break;
	case N_CALL_EXPR:
		lay_call(L, n, false);
		break;
	case N_NEW_EXPR:
		lay_word(L, "new");
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		if (n->flags & F_HAS_ARGS) {
			lay_tok(L, TK_LPAREN);
			lay_list(L, n, &n->list, lay_node);
			lay_close(L, n, TK_RPAREN);
		}
		break;
	case N_SEQ_EXPR:
		lay_list(L, n, &n->list, lay_node);
		break;
	case N_TPL:
		/* lay_tpl begins the node itself */
		break;
	case N_TAGGED_TPL:
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		lay_node(L, n->c);
		break;
	case N_PAREN_EXPR:
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_close(L, n, TK_RPAREN);
		break;
	case N_AWAIT_EXPR:
		lay_word(L, "await");
		lay_node(L, n->a);
		break;
	case N_YIELD_EXPR:
		lay_word(L, "yield");
		if (n->flags & F_DELEGATE)
			lay_tok(L, TK_MUL);
		if (n->a)
			lay_node(L, n->a);
		break;
	case N_TS_AS_EXPR:
	case N_TS_SATISFIES_EXPR:
		lay_node(L, n->a);
		lay_word(L, n->kind == N_TS_AS_EXPR ? "as" : "satisfies");
		lay_type(L, n->b);
		break;
	case N_TS_CONST_ASSERTION:
		lay_node(L, n->a);
		lay_word(L, "as");
		lay_word(L, "const");
		break;
	case N_TS_NON_NULL_EXPR:
		lay_node(L, n->a);
		lay_tok(L, TK_BANG);
		break;
	case N_TS_TYPE_ASSERTION:
		lay_tok(L, TK_LT);
		lay_type(L, n->b);
		lay_tok(L, TK_GT);
		lay_node(L, n->a);
		break;
	case N_COMPUTED_PROP_NAME:
		lay_tok(L, TK_LBRACKET);
		lay_node(L, n->a);
		lay_tok(L, TK_RBRACKET);
		break;
	case N_TS_EXPR_WITH_TYPE_ARGS:
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		break;
	/* patterns (outside parameters) */
	case N_BINDING_IDENT:
		lay_node(L, n->a);
		if (n->a->flags & F_OPTIONAL)
			lay_tok(L, TK_QUESTION);
		if (n->b)
			lay_type_ann(L, n->b);
		break;
	case N_ARRAY_PAT:
		lay_tok(L, TK_LBRACKET);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_RBRACKET);
		if (n->b)
			lay_type_ann(L, n->b);
		break;
	case N_OBJECT_PAT:
		lay_tok(L, TK_LBRACE);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_RBRACE);
		if (n->b)
			lay_type_ann(L, n->b);
		break;
	case N_ASSIGN_PAT:
		lay_node(L, n->a);
		lay_tok(L, TK_ASSIGN);
		lay_node(L, n->b);
		break;
	case N_REST_PAT:
		lay_tok(L, TK_DOTDOTDOT);
		lay_node(L, n->a);
		if (n->b)
			lay_type_ann(L, n->b);
		break;
	case N_KEY_VALUE_PAT_PROP:
		lay_prop_name(L, n->a);
		lay_tok(L, TK_COLON);
		lay_node(L, n->b);
		break;
	case N_ASSIGN_PAT_PROP:
		lay_node(L, n->a);
		if (n->b) {
			lay_tok(L, TK_ASSIGN);
			lay_node(L, n->b);
		}
		break;
	/* types */
	case N_TS_TYPE_ANN:
		lay_tok(L, TK_COLON);
		lay_type(L, n->a);
		break;
	case N_TS_KEYWORD_TYPE:
		lay_text(L, n, TK_WORD);
		break;
	case N_TS_THIS_TYPE:
		lay_word(L, "this");
		break;
	case N_TS_TYPE_REF:
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		break;
	case N_TS_QUALIFIED_NAME:
		lay_node(L, n->a);
		lay_tok(L, TK_DOT);
		lay_node(L, n->b);
		break;
	case N_TS_ARRAY_TYPE:
		lay_type(L, n->a);
		lay_tok(L, TK_LBRACKET);
		lay_tok(L, TK_RBRACKET);
		break;
	case N_TS_INDEXED_ACCESS_TYPE:
		lay_type(L, n->a);
		lay_tok(L, TK_LBRACKET);
		lay_type(L, n->b);
		lay_tok(L, TK_RBRACKET);
		break;
	case N_TS_UNION_TYPE:
		lay_union_or_intersection(L, n, TK_BITOR);
		break;
	case N_TS_INTERSECTION_TYPE:
		lay_union_or_intersection(L, n, TK_BITAND);
		break;
	case N_TS_FN_TYPE:
	case N_TS_CONSTRUCTOR_TYPE:
		if (n->kind == N_TS_CONSTRUCTOR_TYPE) {
			if (n->flags & F_ABSTRACT)
				lay_word(L, "abstract");
			lay_word(L, "new");
		}
		if (n->b)
			lay_type_params(L, n->b);
		lay_params(L, n, &n->list);
		lay_return_type(L, n->c, TK_ARROW);
		break;
	case N_TS_TYPE_LIT:
		lay_tok(L, TK_LBRACE);
		for (int i = 0; i < n->list.n; i++)
			lay_type_member(L, n->list.v[i], 0);
		lay_close(L, n, TK_RBRACE);
		break;
	case N_TS_TUPLE_TYPE:
		lay_tok(L, TK_LBRACKET);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_RBRACKET);
		break;
	case N_TS_TUPLE_ELEMENT:
		if (n->a) {
			Node *label = n->a;
			lay_begin(L, label, 0);
			lay_node(L, label->a);
			if (label->a->flags & F_OPTIONAL)
				lay_tok(L, TK_QUESTION);
			lay_end(L, label);
			lay_tok(L, TK_COLON);
		}
		lay_type(L, n->b);
		break;
	case N_TS_LIT_TYPE: {
		Node *v = n->a;
		if (v->kind == N_TPL) {
			lay_node(L, v);
		} else if (v->kind == N_NUMBER && v->text.n > 1 && v->text.p[0] == '-') {
			lay_begin(L, v, 0);
			lay_tok(L, TK_SUB);
			L->glue = true; /* the literal's text is "-1" */
			lay_tok_n(L, TK_NUM, v->text.p + 1, v->text.n - 1);
			lay_end(L, v);
		} else {
			lay_node(L, v);
		}
		break;
	}
	case N_TS_TYPE_OPERATOR:
		lay_word(L, n->op == TYPEOP_KEYOF ? "keyof" : n->op == TYPEOP_UNIQUE ? "unique" : "readonly");
		lay_type(L, n->a);
		break;
	case N_TS_CONDITIONAL_TYPE:
		lay_type(L, n->a);
		lay_word(L, "extends");
		lay_type(L, n->b);
		if (n->hints & H_OP_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_QUESTION);
		lay_type(L, n->c);
		if (n->hints & H_OP2_NL)
			lay_request(L, NL_LINE);
		lay_tok(L, TK_COLON);
		lay_type(L, n->d);
		break;
	case N_TS_INFER_TYPE:
		lay_word(L, "infer");
		lay_node(L, n->a);
		break;
	case N_TS_TYPE_PARAM:
		if (n->flags & F_CONST)
			lay_word(L, "const");
		lay_node(L, n->a);
		if (n->b) {
			lay_word(L, "extends");
			lay_type(L, n->b);
		}
		if (n->c) {
			lay_tok(L, TK_ASSIGN);
			lay_type(L, n->c);
		}
		break;
	case N_TS_MAPPED_TYPE: {
		lay_tok(L, TK_LBRACE);
		if (n->mapped_readonly == MAPPED_PLUS)
			lay_tok(L, TK_ADD);
		else if (n->mapped_readonly == MAPPED_MINUS)
			lay_tok(L, TK_SUB);
		if (n->mapped_readonly)
			lay_word(L, "readonly");
		lay_tok(L, TK_LBRACKET);
		Node *tp = n->a;
		lay_begin(L, tp, 0);
		lay_node(L, tp->a);
		lay_word(L, "in");
		lay_type(L, tp->b);
		lay_end(L, tp);
		if (n->b) {
			lay_word(L, "as");
			lay_type(L, n->b);
		}
		lay_tok(L, TK_RBRACKET);
		if (n->mapped_optional == MAPPED_PLUS)
			lay_tok(L, TK_ADD);
		else if (n->mapped_optional == MAPPED_MINUS)
			lay_tok(L, TK_SUB);
		if (n->mapped_optional)
			lay_tok(L, TK_QUESTION);
		if (n->c) {
			lay_tok(L, TK_COLON);
			lay_type(L, n->c);
		}
		if (n->hints & H_MAPPED_SEMI)
			lay_tok(L, TK_SEMI);
		lay_close(L, n, TK_RBRACE);
		break;
	}
	case N_TS_PARENTHESIZED_TYPE:
		lay_tok(L, TK_LPAREN);
		lay_type(L, n->a);
		lay_close(L, n, TK_RPAREN);
		break;
	case N_TS_EXTERNAL_MODULE_REF:
		lay_word(L, "require");
		lay_tok(L, TK_LPAREN);
		lay_node(L, n->a);
		lay_tok(L, TK_RPAREN);
		break;
	case N_TS_TYPE_QUERY:
		lay_word(L, "typeof");
		lay_node(L, n->a);
		if (n->b)
			lay_type_params(L, n->b);
		break;
	case N_TS_TYPE_PARAM_DECL:
	case N_TS_TYPE_PARAM_INSTANTIATION:
		lay_tok(L, TK_LT);
		lay_list(L, n, &n->list, lay_node);
		lay_close(L, n, TK_GT);
		break;
	case N_TS_TYPE_PREDICATE:
		if (n->flags & F_ASSERTS)
			lay_word(L, "asserts");
		lay_node(L, n->a);
		if (n->b) {
			lay_word(L, "is");
			lay_begin(L, n->b, 0);
			lay_type(L, n->b->a);
			lay_end(L, n->b);
		}
		break;
	case N_TS_REST_TYPE:
		lay_tok(L, TK_DOTDOTDOT);
		lay_type(L, n->a);
		break;
	case N_TS_OPTIONAL_TYPE:
		lay_type(L, n->a);
		lay_tok(L, TK_QUESTION);
		break;
	default:
		lay_stmt_inner(L, n);
		break;
	}
}

static void lay_node(Lay *L, Node *n)
{
	if (!n)
		fail("dprint layout: missing node");
	if (n->kind == N_TPL) {
		lay_tpl(L, n);
		return;
	}
	lay_begin(L, n, 0);
	lay_inner(L, n);
	lay_end(L, n);
}

static void set_parents(Node *n, Node *parent);

static void set_parents_list(NodeList *l, Node *parent)
{
	for (int i = 0; i < l->n; i++)
		if (l->v[i])
			set_parents(l->v[i], parent);
}

static void set_parents(Node *n, Node *parent)
{
	n->parent = parent;
	if (n->a)
		set_parents(n->a, n);
	if (n->b)
		set_parents(n->b, n);
	if (n->c)
		set_parents(n->c, n);
	if (n->d)
		set_parents(n->d, n);
	set_parents_list(&n->list, n);
	set_parents_list(&n->list2, n);
	set_parents_list(&n->list3, n);
}

/* ---- comment attachment (swc lexer state.rs: the comments buffer rules) ---- */

static bool tk_is_binop(uint16_t k)
{
	switch (k) {
	case TK_EQEQ:
	case TK_NOTEQ:
	case TK_EQEQEQ:
	case TK_NOTEQEQ:
	case TK_LT:
	case TK_LTEQ:
	case TK_GT:
	case TK_GTEQ:
	case TK_LSHIFT:
	case TK_RSHIFT:
	case TK_ZFRSHIFT:
	case TK_ADD:
	case TK_SUB:
	case TK_MUL:
	case TK_DIV:
	case TK_MOD:
	case TK_BITOR:
	case TK_BITXOR:
	case TK_BITAND:
	case TK_EXP:
	case TK_LOGICAL_OR:
	case TK_LOGICAL_AND:
	case TK_NULLISH:
		return true;
	default:
		return false;
	}
}

static bool tk_can_have_trailing_line_comment(const Tok *t) { return !tk_is_binop(t->kind); }

static bool tk_can_have_trailing_comment(const Tok *t)
{
	switch (t->kind) {
	case TK_SEMI:
	case TK_LBRACE:
	case TK_NUM:
	case TK_STR:
	case TK_DOLLAR_LBRACE:
	case TK_REGEX:
	case TK_BIGINT:
	case TK_RBRACE:
		return true;
	case TK_WORD:
		return t->word == WORD_IDENT;
	default:
		return false;
	}
}

static bool range_has_line_break(const char *s, uint32_t lo, uint32_t hi)
{
	for (uint32_t p = lo; p < hi; p++)
		if (s[p] == '\n')
			return true;
	return false;
}

/* Attaches every comment as leading (at the next token's start) or trailing (at the previous
 * token's end) like swc's lexer with its comments buffer. */
static void ts_attach_comments(const char *src, uint32_t n, const Tok *toks, int ntoks, Comment *comments, int ncomments)
{
	int ci = 0;
	for (int ti = 0; ti <= ntoks; ti++) {
		const Tok *prev = ti > 0 ? &toks[ti - 1] : NULL;
		uint32_t gap_lo = prev ? prev->hi : 0;
		uint32_t gap_hi = ti < ntoks ? toks[ti].lo : n;
		bool had_line_break = prev == NULL; /* is_first */
		uint32_t scan = gap_lo;
		for (; ci < ncomments && comments[ci].lo < gap_hi; ci++) {
			Comment *c = &comments[ci];
			if (range_has_line_break(src, scan, c->lo))
				had_line_break = true;
			bool is_for_next;
			if (!c->block) {
				is_for_next = had_line_break || (prev && !tk_can_have_trailing_line_comment(prev));
				/* a line comment ends at a line break */
				if (c->hi < n)
					had_line_break = true;
			} else {
				is_for_next = had_line_break || !prev || !tk_can_have_trailing_comment(prev);
				uint32_t p = c->hi;
				while (p < n && (src[p] == ' ' || src[p] == '\n'))
					p++;
				if (p < n && src[p] == ';')
					is_for_next = false;
				if (range_has_line_break(src, c->lo, c->hi))
					had_line_break = true;
			}
			scan = c->hi;
			if (is_for_next) {
				if (ti < ntoks) {
					c->leading = true;
					c->pos = toks[ti].lo;
				} else if (!prev) {
					c->leading = true;
					c->pos = 0;
				} else {
					c->leading = false;
					c->pos = prev->hi;
				}
			} else {
				c->leading = false;
				c->pos = prev ? prev->hi : 0;
			}
		}
	}
}

/* Lays out the module: fills the source (text, tokens with an EOF sentinel, comments and their
 * indexes) and the spans and parents of all nodes. */
static Source *layout_module(Node *module)
{
	Lay *L = xalloc(sizeof *L);
	L->pending = -1;
	lay_begin(L, module, 0);
	for (int i = 0; i < module->list.n; i++)
		lay_stmt(L, module->list.v[i]);
	L->depth--;
	for (int i = 0; i < module->ntcm; i++) {
		lay_request(L, module->tcm[i].nl ? module->tcm[i].nl : NL_LINE);
		lay_comment(L, &module->tcm[i]);
	}
	if (L->nt == 0) {
		module->lo = 0;
		module->hi = 0;
	} else {
		module->hi = L->last_hi;
	}
	L->nwait = 0;
	set_parents(module, NULL);

	Source *s = source_new(L->text.p ? L->text.p : "", (uint32_t)L->text.n);
	Tok *t = xalloc(sizeof(Tok) * (size_t)(L->nt + 1));
	if (L->nt)
		memcpy(t, L->toks, sizeof(Tok) * (size_t)L->nt);
	t[L->nt] = (Tok){s->n, s->n, TK_EOF, 0, false};
	s->toks = t;
	s->ntoks = L->nt;
	s->comments = L->cms;
	s->ncomments = L->nc;
	s->module = module;
	ts_attach_comments(s->text, s->n, s->toks, s->ntoks, s->comments, s->ncomments);
	source_index(s);
	return s;
}
