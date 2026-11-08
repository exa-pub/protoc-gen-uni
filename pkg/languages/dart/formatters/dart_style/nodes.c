/* The node API of the dart_style port (dart_style.h): constructors for the unresolved AST of
 * package:analyzer 14 and the linearizer that places its tokens.
 *
 * Not a port of package:analyzer. The shapes are those the analyzer's parser produces for the
 * same source text (parseString without resolution): `a.b` of two identifiers is a
 * PrefixedIdentifier and any other `x.b` a PropertyAccess, `m(...)`, `x.m(...)` and
 * `prefix.Type(...)` are MethodInvocations, `[prefix.]Type<T>.name(...)` and `new`/`const`
 * invocations InstanceCreationExpressions, cascade sections have a null target and the cascade
 * operator, formal parameters carry their default value in a FormalParameterDefaultClause and
 * switch cases are pattern cases with constant patterns. */

static const struct {
	const char *s;
	Keyword kw;
} ds_keywords[] = {
	{"abstract", KW_ABSTRACT}, {"as", KW_AS}, {"assert", KW_ASSERT}, {"async", KW_ASYNC}, {"await", KW_AWAIT},
	{"base", KW_BASE}, {"break", KW_BREAK}, {"case", KW_CASE}, {"catch", KW_CATCH}, {"class", KW_CLASS},
	{"const", KW_CONST}, {"continue", KW_CONTINUE}, {"covariant", KW_COVARIANT}, {"default", KW_DEFAULT},
	{"deferred", KW_DEFERRED}, {"do", KW_DO}, {"dynamic", KW_DYNAMIC}, {"else", KW_ELSE}, {"enum", KW_ENUM},
	{"export", KW_EXPORT}, {"extends", KW_EXTENDS}, {"extension", KW_EXTENSION}, {"external", KW_EXTERNAL},
	{"factory", KW_FACTORY}, {"false", KW_FALSE}, {"final", KW_FINAL}, {"finally", KW_FINALLY}, {"for", KW_FOR},
	{"Function", KW_FUNCTION}, {"get", KW_GET}, {"hide", KW_HIDE}, {"if", KW_IF}, {"implements", KW_IMPLEMENTS},
	{"import", KW_IMPORT}, {"in", KW_IN}, {"interface", KW_INTERFACE}, {"is", KW_IS}, {"late", KW_LATE},
	{"library", KW_LIBRARY}, {"mixin", KW_MIXIN}, {"native", KW_NATIVE}, {"new", KW_NEW}, {"null", KW_NULL},
	{"of", KW_OF}, {"on", KW_ON}, {"operator", KW_OPERATOR}, {"part", KW_PART}, {"required", KW_REQUIRED},
	{"rethrow", KW_RETHROW}, {"return", KW_RETURN}, {"sealed", KW_SEALED}, {"set", KW_SET}, {"show", KW_SHOW},
	{"static", KW_STATIC}, {"super", KW_SUPER}, {"switch", KW_SWITCH}, {"sync", KW_SYNC}, {"this", KW_THIS},
	{"throw", KW_THROW}, {"true", KW_TRUE}, {"try", KW_TRY}, {"type", KW_TYPE}, {"typedef", KW_TYPEDEF},
	{"var", KW_VAR}, {"void", KW_VOID}, {"when", KW_WHEN}, {"while", KW_WHILE}, {"with", KW_WITH},
	{"yield", KW_YIELD},
};

static Keyword ds_keyword(const char *s)
{
	for (size_t i = 0; i < sizeof ds_keywords / sizeof *ds_keywords; i++)
		if (strcmp(ds_keywords[i].s, s) == 0)
			return ds_keywords[i].kw;
	return KW_NONE;
}

void ds_push(NodeList *l, Node *n)
{
	if (!n)
		return;
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 4;
		l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v, (size_t)cap * sizeof *l->v);
		l->cap = cap;
	}
	l->v[l->n++] = n;
}

static NodeList ds_vlist(Node *first, va_list ap)
{
	NodeList l = {0};
	for (Node *n = first; n; n = va_arg(ap, Node *))
		ds_push(&l, n);
	return l;
}

/* ---- tokens ---- */

Token *ds_t(TokenType type, const char *lexeme)
{
	Token *t = xalloc(sizeof *t);
	t->type = type;
	t->lexeme = lexeme;
	t->len = (int)strlen(lexeme);
	t->offset = -1;
	if (type == TT_IDENTIFIER)
		t->kw = ds_keyword(lexeme);
	return t;
}

Token *ds_k(Keyword kw)
{
	for (size_t i = 0; i < sizeof ds_keywords / sizeof *ds_keywords; i++)
		if (ds_keywords[i].kw == kw) {
			Token *t = ds_t(TT_IDENTIFIER, ds_keywords[i].s);
			return t;
		}
	fail("dart_style: unknown keyword %d", (int)kw);
}

Token *ds_name(const char *s) { return ds_t(TT_IDENTIFIER, s); }

static Token *ds_p(TokenType type)
{
	static const char *const lex[TT_COUNT] = {
		[TT_OPEN_PAREN] = "(", [TT_CLOSE_PAREN] = ")", [TT_OPEN_SQUARE_BRACKET] = "[",
		[TT_CLOSE_SQUARE_BRACKET] = "]", [TT_OPEN_CURLY_BRACKET] = "{", [TT_CLOSE_CURLY_BRACKET] = "}",
		[TT_COMMA] = ",", [TT_SEMICOLON] = ";", [TT_COLON] = ":", [TT_PERIOD] = ".", [TT_PERIOD_PERIOD] = "..",
		[TT_QUESTION] = "?", [TT_AT] = "@", [TT_FUNCTION] = "=>", [TT_EQ] = "=", [TT_LT] = "<", [TT_GT] = ">",
		[TT_STAR] = "*", [TT_STRING_INTERPOLATION_IDENTIFIER] = "$",
	};
	return ds_t(type, lex[type]);
}

Token *ds_comment(Node *n, const char *text)
{
	Token *c = ds_t(TT_SINGLE_LINE_COMMENT, text);
	Token **p = &n->comments;
	while (*p)
		p = &(*p)->next;
	*p = c;
	return c;
}

Token *ds_comma(Node *n)
{
	if (!n->commaTok)
		n->commaTok = ds_p(TT_COMMA);
	return n->commaTok;
}

Node *ds_nl(Node *n, int nl, int sp)
{
	n->wsSet = true;
	n->nl = (int8_t)nl;
	n->sp = (int8_t)sp;
	return n;
}

Token *ds_tnl(Token *t, int nl, int sp)
{
	t->wsSet = true;
	t->nl = (int8_t)nl;
	t->sp = (int8_t)sp;
	return t;
}

static Node *ds_node(NodeKind kind)
{
	Node *n = xalloc(sizeof *n);
	n->kind = kind;
	return n;
}

/* ---- expressions ---- */

Node *ds_id(const char *name)
{
	Node *n = ds_node(N_SimpleIdentifier);
	n->name = ds_name(name);
	return n;
}

Node *ds_prop(Node *target, const char *name)
{
	Node *n = ds_node(target->kind == N_SimpleIdentifier ? N_PrefixedIdentifier : N_PropertyAccess);
	n->target = target;
	if (n->kind == N_PrefixedIdentifier)
		n->period = ds_p(TT_PERIOD);
	else
		n->op = ds_p(TT_PERIOD);
	n->ident = ds_id(name);
	return n;
}

Node *ds_path(const char *first, ...)
{
	va_list ap;
	va_start(ap, first);
	Node *n = ds_id(first);
	for (const char *s = va_arg(ap, const char *); s; s = va_arg(ap, const char *))
		n = ds_prop(n, s);
	va_end(ap);
	return n;
}

Node *ds_call(Node *target, const char *name, Node *typeArgs, Node *args)
{
	Node *n = ds_node(N_MethodInvocation);
	n->target = target;
	if (target)
		n->op = ds_p(TT_PERIOD);
	n->ident = ds_id(name);
	n->typeArgs = typeArgs;
	n->args = args;
	return n;
}

Node *ds_argl(NodeList l)
{
	Node *n = ds_node(N_ArgumentList);
	n->lp = ds_p(TT_OPEN_PAREN);
	n->list = l;
	n->rp = ds_p(TT_CLOSE_PAREN);
	return n;
}

Node *ds_args(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	NodeList l = ds_vlist(first, ap);
	va_end(ap);
	return ds_argl(l);
}

Node *ds_named(const char *name, Node *expr)
{
	Node *n = ds_node(N_NamedArgument);
	n->name = ds_name(name);
	n->colon = ds_p(TT_COLON);
	n->expr = expr;
	return n;
}

Node *ds_new(Keyword kw, Node *type, const char *ctor, Node *args)
{
	Node *n = ds_node(N_InstanceCreationExpression);
	if (kw != KW_NONE)
		n->kw = ds_k(kw);
	Node *cn = ds_node(N_ConstructorName);
	cn->type = type;
	if (ctor) {
		cn->period = ds_p(TT_PERIOD);
		cn->ident = ds_id(ctor);
	}
	n->ctorName = cn;
	n->args = args;
	return n;
}

Node *ds_lit(NodeKind kind, TokenType type, const char *lexeme)
{
	Node *n = ds_node(kind);
	n->tok = ds_t(type, lexeme);
	return n;
}

Node *ds_int(long long v) { return v < 0 ? ds_prefix(TT_MINUS, "-", ds_int(-v)) : ds_lit(N_IntegerLiteral, TT_INT, xfmt("%lld", v)); }

Node *ds_str(const char *lexeme) { return ds_lit(N_SimpleStringLiteral, TT_STRING, lexeme); }

Node *ds_adjacent(NodeList strings)
{
	Node *n = ds_node(N_AdjacentStrings);
	n->list = strings;
	return n;
}

Node *ds_interp(const char *s0, const char *ident, const char *s1)
{
	Node *n = ds_node(N_StringInterpolation);
	Node *a = ds_node(N_InterpolationString);
	a->tok = ds_t(TT_STRING, s0);
	ds_push(&n->list, a);
	Node *e = ds_node(N_InterpolationExpression);
	e->lb = ds_p(TT_STRING_INTERPOLATION_IDENTIFIER);
	e->expr = ds_id(ident);
	ds_push(&n->list, e);
	Node *b = ds_node(N_InterpolationString);
	b->tok = ds_t(TT_STRING, s1);
	ds_push(&n->list, b);
	return n;
}

Node *ds_cond(Node *c, Node *then, Node *els)
{
	Node *n = ds_node(N_ConditionalExpression);
	n->expr = c;
	n->question = ds_p(TT_QUESTION);
	n->expr2 = then;
	n->colon = ds_p(TT_COLON);
	n->expr3 = els;
	return n;
}

Node *ds_bin(Node *l, TokenType op, const char *lexeme, Node *r)
{
	Node *n = ds_node(N_BinaryExpression);
	n->expr = l;
	n->op = ds_t(op, lexeme);
	n->expr2 = r;
	return n;
}

Node *ds_prefix(TokenType op, const char *lexeme, Node *e)
{
	Node *n = ds_node(N_PrefixExpression);
	n->op = ds_t(op, lexeme);
	n->expr = e;
	return n;
}

Node *ds_postfix(Node *e, TokenType op, const char *lexeme)
{
	Node *n = ds_node(N_PostfixExpression);
	n->expr = e;
	n->op = ds_t(op, lexeme);
	return n;
}

Node *ds_assign(Node *l, TokenType op, const char *lexeme, Node *r)
{
	Node *n = ds_node(N_AssignmentExpression);
	n->expr = l;
	n->op = ds_t(op, lexeme);
	n->expr2 = r;
	return n;
}

Node *ds_as(Node *e, Node *type)
{
	Node *n = ds_node(N_AsExpression);
	n->expr = e;
	n->op = ds_k(KW_AS);
	n->type = type;
	return n;
}

Node *ds_index(Node *target, Node *index)
{
	Node *n = ds_node(N_IndexExpression);
	n->target = target;
	n->lb = ds_p(TT_OPEN_SQUARE_BRACKET);
	n->expr = index;
	n->rb = ds_p(TT_CLOSE_SQUARE_BRACKET);
	return n;
}

Node *ds_paren(Node *e)
{
	Node *n = ds_node(N_ParenthesizedExpression);
	n->lp = ds_p(TT_OPEN_PAREN);
	n->expr = e;
	n->rp = ds_p(TT_CLOSE_PAREN);
	return n;
}

Node *ds_await(Node *e)
{
	Node *n = ds_node(N_AwaitExpression);
	n->kw = ds_k(KW_AWAIT);
	n->expr = e;
	return n;
}

Node *ds_throw(Node *e)
{
	Node *n = ds_node(N_ThrowExpression);
	n->kw = ds_k(KW_THROW);
	n->expr = e;
	return n;
}

Node *ds_this(void)
{
	Node *n = ds_node(N_ThisExpression);
	n->kw = ds_k(KW_THIS);
	return n;
}

Node *ds_super(void)
{
	Node *n = ds_node(N_SuperExpression);
	n->kw = ds_k(KW_SUPER);
	return n;
}

Node *ds_cascade(Node *target, NodeList sections)
{
	Node *n = ds_node(N_CascadeExpression);
	n->target = target;
	n->list = sections;
	return n;
}

Node *ds_csec_call(const char *name, Node *typeArgs, Node *args)
{
	Node *n = ds_call(NULL, name, typeArgs, args);
	n->op = ds_p(TT_PERIOD_PERIOD);
	return n;
}

Node *ds_csec_assign(const char *name, Node *rhs)
{
	Node *pa = ds_node(N_PropertyAccess);
	pa->op = ds_p(TT_PERIOD_PERIOD);
	pa->ident = ds_id(name);
	return ds_assign(pa, TT_EQ, "=", rhs);
}

Node *ds_list(Token *constKw, Node *typeArgs, NodeList elements)
{
	Node *n = ds_node(N_ListLiteral);
	n->kw = constKw;
	n->typeArgs = typeArgs;
	n->lb = ds_p(TT_OPEN_SQUARE_BRACKET);
	n->list = elements;
	n->rb = ds_p(TT_CLOSE_SQUARE_BRACKET);
	return n;
}

Node *ds_map(Token *constKw, Node *typeArgs, NodeList elements)
{
	Node *n = ds_node(N_SetOrMapLiteral);
	n->kw = constKw;
	n->typeArgs = typeArgs;
	n->lb = ds_p(TT_OPEN_CURLY_BRACKET);
	n->list = elements;
	n->rb = ds_p(TT_CLOSE_CURLY_BRACKET);
	return n;
}

Node *ds_entry(Node *key, Node *value)
{
	Node *n = ds_node(N_MapLiteralEntry);
	n->expr = key;
	n->op = ds_p(TT_COLON);
	n->expr2 = value;
	return n;
}

Node *ds_fnexpr(Node *params, Node *body)
{
	Node *n = ds_node(N_FunctionExpression);
	n->params = params;
	n->body = body;
	if (body->kind == N_ExpressionFunctionBody)
		body->semi = NULL;
	return n;
}

/* ---- types ---- */

Node *ds_type(const char *prefix, const char *name, Node *typeArgs, bool nullable)
{
	Node *n = ds_node(N_NamedType);
	if (prefix) {
		n->prefixName = ds_name(prefix);
		n->prefixPeriod = ds_p(TT_PERIOD);
	}
	n->name = ds_name(name);
	n->typeArgs = typeArgs;
	if (nullable)
		n->question = ds_p(TT_QUESTION);
	return n;
}

Node *ds_targs(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	Node *n = ds_node(N_TypeArgumentList);
	n->lb = ds_p(TT_LT);
	n->list = ds_vlist(first, ap);
	n->rb = ds_p(TT_GT);
	va_end(ap);
	return n;
}

Node *ds_fntype(Node *returnType, Node *params)
{
	Node *n = ds_node(N_GenericFunctionType);
	n->type = returnType;
	n->kw = ds_k(KW_FUNCTION);
	n->params = params;
	return n;
}

/* ---- parameters ---- */

Node *ds_params(NodeList positional, NodeList optional, bool named)
{
	Node *n = ds_node(N_FormalParameterList);
	n->lp = ds_p(TT_OPEN_PAREN);
	n->list = positional;
	if (optional.n) {
		n->lb = ds_p(named ? TT_OPEN_CURLY_BRACKET : TT_OPEN_SQUARE_BRACKET);
		for (int i = 0; i < optional.n; i++) {
			optional.v[i]->flags |= named ? NF_NAMED : NF_OPTIONAL_POSITIONAL;
			ds_push(&n->list, optional.v[i]);
		}
		n->rb = ds_p(named ? TT_CLOSE_CURLY_BRACKET : TT_CLOSE_SQUARE_BRACKET);
	}
	n->rp = ds_p(TT_CLOSE_PAREN);
	return n;
}

Node *ds_param(Node *type, const char *name, Node *defaultValue)
{
	Node *n = ds_node(N_RegularFormalParameter);
	n->type = type;
	if (name)
		n->name = ds_name(name);
	if (defaultValue) {
		Node *d = ds_node(N_FormalParameterDefaultClause);
		d->op = ds_p(TT_EQ);
		d->expr = defaultValue;
		n->defaultClause = d;
	}
	return n;
}

Node *ds_fparam(Keyword thisOrSuper, const char *name)
{
	Node *n = ds_node(thisOrSuper == KW_THIS ? N_FieldFormalParameter : N_SuperFormalParameter);
	n->kw2 = ds_k(thisOrSuper);
	n->period = ds_p(TT_PERIOD);
	n->name = ds_name(name);
	return n;
}

/* ---- statements ---- */

Node *ds_block(NodeList statements)
{
	Node *n = ds_node(N_Block);
	n->lb = ds_p(TT_OPEN_CURLY_BRACKET);
	n->list = statements;
	n->rb = ds_p(TT_CLOSE_CURLY_BRACKET);
	return n;
}

Node *ds_expr_stmt(Node *e)
{
	Node *n = ds_node(N_ExpressionStatement);
	n->expr = e;
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_return(Node *e)
{
	Node *n = ds_node(N_ReturnStatement);
	n->kw = ds_k(KW_RETURN);
	n->expr = e;
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_if(Node *cond, Node *then)
{
	Node *n = ds_node(N_IfStatement);
	n->kw = ds_k(KW_IF);
	n->lp = ds_p(TT_OPEN_PAREN);
	n->expr = cond;
	n->rp = ds_p(TT_CLOSE_PAREN);
	n->stmt = then;
	return n;
}

static Node *ds_vars(Keyword kw, Node *type, const char *name, Node *init)
{
	Node *l = ds_node(N_VariableDeclarationList);
	if (kw != KW_NONE)
		l->kw = ds_k(kw);
	l->type = type;
	Node *v = ds_node(N_VariableDeclaration);
	v->name = ds_name(name);
	if (init) {
		v->op = ds_p(TT_EQ);
		v->expr = init;
	}
	ds_push(&l->list, v);
	return l;
}

Node *ds_var_stmt(Keyword kw, Node *type, const char *name, Node *init)
{
	Node *n = ds_node(N_VariableDeclarationStatement);
	n->vars = ds_vars(kw, type, name, init);
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_switch(Node *e, NodeList members)
{
	Node *n = ds_node(N_SwitchStatement);
	n->kw = ds_k(KW_SWITCH);
	n->lp = ds_p(TT_OPEN_PAREN);
	n->expr = e;
	n->rp = ds_p(TT_CLOSE_PAREN);
	n->lb = ds_p(TT_OPEN_CURLY_BRACKET);
	n->list = members;
	n->rb = ds_p(TT_CLOSE_CURLY_BRACKET);
	return n;
}

Node *ds_case(Node *e, NodeList statements)
{
	Node *n = ds_node(N_SwitchPatternCase);
	n->kw = ds_k(KW_CASE);
	Node *cp = ds_node(N_ConstantPattern);
	cp->expr = e;
	n->expr = cp;
	n->colon = ds_p(TT_COLON);
	n->list = statements;
	return n;
}

Node *ds_default(NodeList statements)
{
	Node *n = ds_node(N_SwitchDefault);
	n->kw = ds_k(KW_DEFAULT);
	n->colon = ds_p(TT_COLON);
	n->list = statements;
	return n;
}

Node *ds_yield(bool star, Node *e)
{
	Node *n = ds_node(N_YieldStatement);
	n->kw = ds_k(KW_YIELD);
	if (star)
		n->star = ds_p(TT_STAR);
	n->expr = e;
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

/* ---- function bodies ---- */

Node *ds_arrow(Node *e)
{
	Node *n = ds_node(N_ExpressionFunctionBody);
	n->op = ds_p(TT_FUNCTION);
	n->expr = e;
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_bbody(Keyword async, bool star, Node *block)
{
	Node *n = ds_node(N_BlockFunctionBody);
	if (async != KW_NONE)
		n->kw = ds_k(async);
	if (star)
		n->star = ds_p(TT_STAR);
	n->body = block;
	return n;
}

Node *ds_empty_body(void)
{
	Node *n = ds_node(N_EmptyFunctionBody);
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

/* ---- declarations ---- */

Node *ds_annot(Node *name, Node *args)
{
	Node *n = ds_node(N_Annotation);
	n->op = ds_p(TT_AT);
	n->ident = name;
	n->args = args;
	return n;
}

Node *ds_unit(NodeList directives, NodeList declarations)
{
	Node *n = ds_node(N_CompilationUnit);
	n->list = directives;
	n->list2 = declarations;
	n->name = ds_t(TT_EOF, ""); /* the EOF token */
	return n;
}

Node *ds_import(const char *uri, const char *prefix)
{
	Node *n = ds_node(N_ImportDirective);
	n->kw = ds_k(KW_IMPORT);
	n->expr = ds_str(uri);
	if (prefix) {
		n->op = ds_k(KW_AS);
		n->ident = ds_id(prefix);
	}
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_export(const char *uri, NodeList show)
{
	Node *n = ds_node(N_ExportDirective);
	n->kw = ds_k(KW_EXPORT);
	n->expr = ds_str(uri);
	if (show.n) {
		Node *c = ds_node(N_ShowCombinator);
		c->kw = ds_k(KW_SHOW);
		c->list = show;
		ds_push(&n->list, c);
	}
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

static Node *ds_typelist_clause(NodeKind kind, Keyword kw, NodeList types)
{
	Node *n = ds_node(kind);
	n->kw = ds_k(kw);
	n->list = types;
	return n;
}

Node *ds_class(NodeList meta, bool isAbstract, const char *name, Node *superclass, NodeList mixins, NodeList members)
{
	Node *n = ds_node(N_ClassDeclaration);
	n->meta = meta;
	if (isAbstract)
		n->m_abstract = ds_k(KW_ABSTRACT);
	n->kw = ds_k(KW_CLASS);
	Node *np = ds_node(N_NameWithTypeParameters);
	np->name = ds_name(name);
	n->namePart = np;
	if (superclass) {
		Node *e = ds_node(N_ExtendsClause);
		e->kw = ds_k(KW_EXTENDS);
		e->type = superclass;
		n->clause1 = e;
	}
	if (mixins.n)
		n->clause2 = ds_typelist_clause(N_WithClause, KW_WITH, mixins);
	Node *b = ds_node(N_BlockClassBody);
	b->lb = ds_p(TT_OPEN_CURLY_BRACKET);
	b->list = members;
	b->rb = ds_p(TT_CLOSE_CURLY_BRACKET);
	n->body = b;
	return n;
}

Node *ds_enum(NodeList meta, const char *name, NodeList constants)
{
	Node *n = ds_node(N_EnumDeclaration);
	n->meta = meta;
	n->kw = ds_k(KW_ENUM);
	Node *np = ds_node(N_NameWithTypeParameters);
	np->name = ds_name(name);
	n->namePart = np;
	Node *b = ds_node(N_BlockEnumBody);
	b->lb = ds_p(TT_OPEN_CURLY_BRACKET);
	b->list = constants;
	b->rb = ds_p(TT_CLOSE_CURLY_BRACKET);
	n->body = b;
	return n;
}

Node *ds_enum_const(const char *name)
{
	Node *n = ds_node(N_EnumConstantDeclaration);
	n->name = ds_name(name);
	return n;
}

Node *ds_field(NodeList meta, bool isStatic, Keyword kw, Node *type, const char *name, Node *init)
{
	Node *n = ds_node(N_FieldDeclaration);
	n->meta = meta;
	if (isStatic)
		n->m_static = ds_k(KW_STATIC);
	n->vars = ds_vars(kw, type, name, init);
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_toplevel(NodeList meta, Keyword kw, Node *type, const char *name, Node *init)
{
	Node *n = ds_node(N_TopLevelVariableDeclaration);
	n->meta = meta;
	n->vars = ds_vars(kw, type, name, init);
	n->semi = ds_p(TT_SEMICOLON);
	return n;
}

Node *ds_method(NodeList meta, Keyword modifier, Node *returnType, Keyword property, const char *name, Node *params,
		Node *body)
{
	Node *n = ds_node(N_MethodDeclaration);
	n->meta = meta;
	if (modifier != KW_NONE)
		n->m_static = ds_k(modifier);
	n->type = returnType;
	if (property != KW_NONE)
		n->m_property = ds_k(property);
	n->name = ds_name(name);
	n->params = params;
	n->body = body;
	return n;
}

Node *ds_ctor(NodeList meta, Keyword constOrFactory, const char *className, const char *name, Node *params,
	      Node *body)
{
	Node *n = ds_node(N_ConstructorDeclaration);
	n->meta = meta;
	if (constOrFactory == KW_CONST)
		n->m_const = ds_k(KW_CONST);
	else if (constOrFactory == KW_FACTORY)
		n->m_factory = ds_k(KW_FACTORY);
	n->ident = ds_id(className);
	if (name) {
		n->period = ds_p(TT_PERIOD);
		n->name = ds_name(name);
	}
	n->params = params;
	n->body = body;
	return n;
}

/* ---- linearizer ----
 * Walks the tree in source order (the order of the analyzer's childEntities), appends every token
 * to the stream and writes the source text: pending whitespace (set by the parent: none, a space
 * or a newline at the body indentation plus a delta) is written before the next token unless the
 * node or the token overrides it. */

typedef struct {
	Buf *out;
	int len16;
	int indent;   /* indentation of the current body */
	int pnl, psp; /* pending whitespace */
	Token *tail;
	Token *pcomments; /* pending comments of the nodes being entered */
	int depth;
} Lin;

enum { LIN_MAX_DEPTH = 2000 };

static void lin_text(Lin *L, const char *s, size_t n)
{
	buf_add(L->out, s, n);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if ((c & 0xC0) != 0x80)
			L->len16 += c >= 0xF0 ? 2 : 1;
	}
}

static void lin_ws(Lin *L, int nl, int sp)
{
	if (L->out->n == 0)
		return;
	if (nl > 0) {
		for (int i = 0; i < nl; i++)
			lin_text(L, "\n", 1);
		sp += L->indent;
	}
	for (int i = 0; i < sp; i++)
		lin_text(L, " ", 1);
}

static void lin_place(Lin *L, Token *t)
{
	t->offset = (int)L->out->n;
	t->off16 = L->len16;
	lin_text(L, t->lexeme, (size_t)t->len);
	if (t->trail)
		lin_text(L, t->trail, strlen(t->trail));
}

static void lin_tok(Lin *L, Token *t)
{
	if (!t)
		return;
	if (L->pcomments) {
		Token *last = L->pcomments;
		while (last->next)
			last = last->next;
		last->next = t->precedingComments;
		t->precedingComments = L->pcomments;
		L->pcomments = NULL;
	}
	int nl = L->pnl, sp = L->psp;
	for (Token *c = t->precedingComments; c; c = c->next) {
		lin_ws(L, c->nl, c->sp);
		lin_place(L, c);
	}
	if (t->wsSet) {
		nl = t->nl;
		sp = t->sp;
	}
	lin_ws(L, nl, sp);
	lin_place(L, t);
	t->previous = L->tail;
	L->tail->next = t;
	L->tail = t;
	L->pnl = L->psp = 0;
}

static void lin_sp(Lin *L) { L->pnl = 0, L->psp = 1; }
static void lin_line(Lin *L, int sp) { L->pnl = 1, L->psp = sp; }

static void lin_node(Lin *L, Node *n);

static void lin_sp_node(Lin *L, Node *n)
{
	if (!n)
		return;
	lin_sp(L);
	lin_node(L, n);
}

static void lin_sp_tok(Lin *L, Token *t)
{
	if (!t)
		return;
	lin_sp(L);
	lin_tok(L, t);
}

/* A comma-separated list. vertical: one element per line at indent + 2, then the closing token on
 * its own line at indent. */
static void lin_list(Lin *L, NodeList *l, bool vertical, bool trailing)
{
	if (vertical)
		L->indent += 2;
	for (int i = 0; i < l->n; i++) {
		Node *e = l->v[i];
		if (vertical)
			lin_line(L, 0);
		else if (i > 0)
			lin_sp(L);
		lin_node(L, e);
		if (i + 1 < l->n || trailing || e->commaTok)
			lin_tok(L, ds_comma(e));
	}
	if (vertical) {
		L->indent -= 2;
		lin_line(L, 0);
	}
}

static void lin_meta(Lin *L, NodeList *meta)
{
	for (int i = 0; i < meta->n; i++) {
		lin_node(L, meta->v[i]);
		lin_line(L, 0);
	}
}

/* Members of a body: one per line at indent + 2, the closing bracket on its own line. */
static void lin_body(Lin *L, Token *lb, NodeList *members, Token *rb)
{
	lin_tok(L, lb);
	L->indent += 2;
	for (int i = 0; i < members->n; i++) {
		lin_line(L, 0);
		lin_node(L, members->v[i]);
	}
	L->indent -= 2;
	lin_line(L, 0);
	lin_tok(L, rb);
}

static void lin_children(Lin *L, Node *n)
{
	switch (n->kind) {
	case N_SimpleIdentifier:
		lin_tok(L, n->name);
		break;
	case N_PrefixedIdentifier:
		lin_node(L, n->target);
		lin_tok(L, n->period);
		lin_node(L, n->ident);
		break;
	case N_PropertyAccess:
		lin_node(L, n->target);
		lin_tok(L, n->op);
		lin_node(L, n->ident);
		break;
	case N_MethodInvocation:
		lin_node(L, n->target);
		lin_tok(L, n->op);
		lin_node(L, n->ident);
		lin_node(L, n->typeArgs);
		lin_node(L, n->args);
		break;
	case N_InstanceCreationExpression:
		lin_tok(L, n->kw);
		if (n->kw)
			lin_sp(L);
		lin_node(L, n->ctorName);
		lin_node(L, n->args);
		break;
	case N_ConstructorName:
		lin_node(L, n->type);
		lin_tok(L, n->period);
		lin_node(L, n->ident);
		break;
	case N_IndexExpression:
		lin_node(L, n->target);
		lin_tok(L, n->op);
		lin_tok(L, n->lb);
		lin_node(L, n->expr);
		lin_tok(L, n->rb);
		break;
	case N_ArgumentList:
		lin_tok(L, n->lp);
		lin_list(L, &n->list, n->flags & DS_VERTICAL, n->flags & DS_TRAILING_COMMA);
		lin_tok(L, n->rp);
		break;
	case N_NamedArgument:
		lin_tok(L, n->name);
		lin_tok(L, n->colon);
		lin_sp_node(L, n->expr);
		break;
	case N_IntegerLiteral: case N_DoubleLiteral: case N_BooleanLiteral: case N_NullLiteral:
	case N_SimpleStringLiteral: case N_InterpolationString:
		lin_tok(L, n->tok);
		break;
	case N_AdjacentStrings:
		for (int i = 0; i < n->list.n; i++) {
			if (i > 0)
				lin_sp(L);
			lin_node(L, n->list.v[i]);
		}
		break;
	case N_StringInterpolation:
		for (int i = 0; i < n->list.n; i++)
			lin_node(L, n->list.v[i]);
		break;
	case N_InterpolationExpression:
		lin_tok(L, n->lb);
		lin_node(L, n->expr);
		lin_tok(L, n->rb);
		break;
	case N_ConditionalExpression:
		lin_node(L, n->expr);
		lin_sp_tok(L, n->question);
		lin_sp_node(L, n->expr2);
		lin_sp_tok(L, n->colon);
		lin_sp_node(L, n->expr3);
		break;
	case N_BinaryExpression: case N_AssignmentExpression:
		lin_node(L, n->expr);
		lin_sp_tok(L, n->op);
		lin_sp_node(L, n->expr2);
		break;
	case N_PrefixExpression:
		lin_tok(L, n->op);
		lin_node(L, n->expr);
		break;
	case N_PostfixExpression:
		lin_node(L, n->expr);
		lin_tok(L, n->op);
		break;
	case N_AsExpression:
		lin_node(L, n->expr);
		lin_sp_tok(L, n->op);
		lin_sp_node(L, n->type);
		break;
	case N_ParenthesizedExpression:
		lin_tok(L, n->lp);
		lin_node(L, n->expr);
		lin_tok(L, n->rp);
		break;
	case N_AwaitExpression: case N_ThrowExpression:
		lin_tok(L, n->kw);
		lin_sp_node(L, n->expr);
		break;
	case N_ThisExpression: case N_SuperExpression:
		lin_tok(L, n->kw);
		break;
	case N_CascadeExpression:
		lin_node(L, n->target);
		for (int i = 0; i < n->list.n; i++)
			lin_node(L, n->list.v[i]);
		break;
	case N_ListLiteral: case N_SetOrMapLiteral:
		lin_tok(L, n->kw);
		if (n->kw)
			lin_sp(L);
		lin_node(L, n->typeArgs);
		lin_tok(L, n->lb);
		lin_list(L, &n->list, n->flags & DS_VERTICAL, n->flags & DS_TRAILING_COMMA);
		lin_tok(L, n->rb);
		break;
	case N_MapLiteralEntry:
		lin_node(L, n->expr);
		lin_tok(L, n->op);
		lin_sp_node(L, n->expr2);
		break;
	case N_FunctionExpression:
		lin_node(L, n->params);
		lin_sp_node(L, n->body);
		break;
	case N_NamedType:
		lin_tok(L, n->prefixName);
		lin_tok(L, n->prefixPeriod);
		lin_tok(L, n->name);
		lin_node(L, n->typeArgs);
		lin_tok(L, n->question);
		break;
	case N_TypeArgumentList:
		lin_tok(L, n->lb);
		lin_list(L, &n->list, false, false);
		lin_tok(L, n->rb);
		break;
	case N_GenericFunctionType:
		lin_node(L, n->type);
		lin_sp_tok(L, n->kw);
		lin_node(L, n->params);
		lin_tok(L, n->question);
		break;
	case N_FormalParameterList: {
		bool vertical = n->flags & DS_VERTICAL;
		lin_tok(L, n->lp);
		int npos = 0;
		while (npos < n->list.n && !(n->list.v[npos]->flags & (NF_NAMED | NF_OPTIONAL_POSITIONAL)))
			npos++;
		NodeList pos = {n->list.v, npos, npos}, opt = {n->list.v ? n->list.v + npos : NULL, n->list.n - npos, n->list.n - npos};
		lin_list(L, &pos, false, n->lb ? npos > 0 : (n->flags & DS_TRAILING_COMMA) != 0);
		if (n->lb) {
			if (npos > 0)
				lin_sp(L);
			lin_tok(L, n->lb);
			lin_list(L, &opt, vertical, vertical || (n->flags & DS_TRAILING_COMMA));
			lin_tok(L, n->rb);
		}
		lin_tok(L, n->rp);
		break;
	}
	case N_RegularFormalParameter: case N_FieldFormalParameter: case N_SuperFormalParameter:
		for (int i = 0; i < n->meta.n; i++) {
			lin_node(L, n->meta.v[i]);
			lin_sp(L);
		}
		if (n->type) {
			lin_node(L, n->type);
			if (n->name || n->kw2)
				lin_sp(L);
		}
		lin_tok(L, n->kw2);
		lin_tok(L, n->period);
		lin_tok(L, n->name);
		if (n->defaultClause)
			lin_sp_node(L, n->defaultClause);
		break;
	case N_FormalParameterDefaultClause:
		lin_tok(L, n->op);
		lin_sp_node(L, n->expr);
		break;
	case N_Block:
		lin_body(L, n->lb, &n->list, n->rb);
		break;
	case N_ExpressionStatement:
		lin_node(L, n->expr);
		lin_tok(L, n->semi);
		break;
	case N_ReturnStatement:
		lin_tok(L, n->kw);
		lin_sp_node(L, n->expr);
		lin_tok(L, n->semi);
		break;
	case N_IfStatement:
		lin_tok(L, n->kw);
		lin_sp_tok(L, n->lp);
		lin_node(L, n->expr);
		lin_tok(L, n->rp);
		lin_sp_node(L, n->stmt);
		break;
	case N_VariableDeclarationStatement:
		lin_node(L, n->vars);
		lin_tok(L, n->semi);
		break;
	case N_VariableDeclarationList:
		lin_tok(L, n->kw);
		if (n->kw)
			lin_sp(L);
		if (n->type) {
			lin_node(L, n->type);
			lin_sp(L);
		}
		lin_list(L, &n->list, false, false);
		break;
	case N_VariableDeclaration:
		lin_tok(L, n->name);
		lin_sp_tok(L, n->op);
		lin_sp_node(L, n->expr);
		break;
	case N_SwitchStatement:
		lin_tok(L, n->kw);
		lin_sp_tok(L, n->lp);
		lin_node(L, n->expr);
		lin_tok(L, n->rp);
		lin_sp(L);
		lin_body(L, n->lb, &n->list, n->rb);
		break;
	case N_SwitchPatternCase: case N_SwitchDefault:
		lin_tok(L, n->kw);
		if (n->expr)
			lin_sp_node(L, n->expr);
		lin_tok(L, n->colon);
		for (int i = 0; i < n->list.n; i++)
			lin_sp_node(L, n->list.v[i]);
		break;
	case N_ConstantPattern:
		lin_node(L, n->expr);
		break;
	case N_YieldStatement:
		lin_tok(L, n->kw);
		lin_tok(L, n->star);
		lin_sp_node(L, n->expr);
		lin_tok(L, n->semi);
		break;
	case N_ExpressionFunctionBody:
		lin_tok(L, n->kw);
		lin_tok(L, n->star);
		if (n->kw)
			lin_sp(L);
		lin_tok(L, n->op);
		lin_sp_node(L, n->expr);
		lin_tok(L, n->semi);
		break;
	case N_BlockFunctionBody:
		lin_tok(L, n->kw);
		lin_tok(L, n->star);
		if (n->kw)
			lin_sp(L);
		lin_node(L, n->body);
		break;
	case N_EmptyFunctionBody:
		lin_tok(L, n->semi);
		break;
	case N_Annotation:
		lin_tok(L, n->op);
		lin_node(L, n->ident);
		lin_node(L, n->args);
		break;
	case N_CompilationUnit: {
		bool first = true;
		NodeList *lists[2] = {&n->list, &n->list2};
		for (int k = 0; k < 2; k++)
			for (int i = 0; i < lists[k]->n; i++) {
				if (!first)
					lin_line(L, 0);
				first = false;
				lin_node(L, lists[k]->v[i]);
			}
		lin_line(L, 0);
		lin_tok(L, n->name);
		break;
	}
	case N_ImportDirective: case N_ExportDirective:
		lin_meta(L, &n->meta);
		lin_tok(L, n->kw);
		lin_sp_node(L, n->expr);
		lin_sp_tok(L, n->op);
		lin_sp_node(L, n->ident);
		for (int i = 0; i < n->list.n; i++)
			lin_sp_node(L, n->list.v[i]);
		lin_tok(L, n->semi);
		break;
	case N_ShowCombinator: case N_WithClause: case N_ImplementsClause:
		lin_tok(L, n->kw);
		lin_sp(L);
		lin_list(L, &n->list, false, false);
		break;
	case N_ClassDeclaration:
		lin_meta(L, &n->meta);
		if (n->m_abstract) {
			lin_tok(L, n->m_abstract);
			lin_sp(L);
		}
		lin_tok(L, n->kw);
		lin_sp_node(L, n->namePart);
		lin_sp_node(L, n->clause1);
		lin_sp_node(L, n->clause2);
		lin_sp_node(L, n->body);
		break;
	case N_NameWithTypeParameters:
		lin_tok(L, n->name);
		break;
	case N_ExtendsClause:
		lin_tok(L, n->kw);
		lin_sp_node(L, n->type);
		break;
	case N_BlockClassBody: case N_BlockEnumBody:
		if (n->kind == N_BlockEnumBody) {
			lin_tok(L, n->lb);
			lin_list(L, &n->list, true, false);
			lin_tok(L, n->rb);
		} else {
			lin_body(L, n->lb, &n->list, n->rb);
		}
		break;
	case N_EnumDeclaration:
		lin_meta(L, &n->meta);
		lin_tok(L, n->kw);
		lin_sp_node(L, n->namePart);
		lin_sp_node(L, n->body);
		break;
	case N_EnumConstantDeclaration:
		lin_meta(L, &n->meta);
		lin_tok(L, n->name);
		break;
	case N_FieldDeclaration: case N_TopLevelVariableDeclaration:
		lin_meta(L, &n->meta);
		if (n->m_static) {
			lin_tok(L, n->m_static);
			lin_sp(L);
		}
		lin_node(L, n->vars);
		lin_tok(L, n->semi);
		break;
	case N_MethodDeclaration:
		lin_meta(L, &n->meta);
		if (n->m_static) {
			lin_tok(L, n->m_static);
			lin_sp(L);
		}
		if (n->type) {
			lin_node(L, n->type);
			lin_sp(L);
		}
		if (n->m_property) {
			lin_tok(L, n->m_property);
			lin_sp(L);
		}
		lin_tok(L, n->name);
		lin_node(L, n->params);
		if (n->body->kind != N_EmptyFunctionBody)
			lin_sp(L);
		lin_node(L, n->body);
		break;
	case N_ConstructorDeclaration:
		lin_meta(L, &n->meta);
		if (n->m_const) {
			lin_tok(L, n->m_const);
			lin_sp(L);
		}
		if (n->m_factory) {
			lin_tok(L, n->m_factory);
			lin_sp(L);
		}
		lin_node(L, n->ident);
		lin_tok(L, n->period);
		lin_tok(L, n->name);
		lin_node(L, n->params);
		if (n->body->kind != N_EmptyFunctionBody)
			lin_sp(L);
		lin_node(L, n->body);
		break;
	default:
		fail("dart_style: the linearizer does not support AST node %d", (int)n->kind);
	}
}

static void lin_node(Lin *L, Node *n)
{
	if (!n)
		return;
	if (++L->depth > LIN_MAX_DEPTH)
		fail("dart_style: the syntax tree is nested too deeply");
	if (n->wsSet) {
		L->pnl = n->nl;
		L->psp = n->sp;
	}
	if (n->comments) {
		/* The node's layout goes to its first comment without a layout of its own (or to its
		 * first token); the comments after it and the token go on their own lines. */
		bool used = false;
		int after = 0;
		for (Token *c = n->comments; c; c = c->next) {
			if (!c->wsSet) {
				ds_tnl(c, used ? 1 : L->pnl, used ? after : L->psp);
				if (!used && c == n->comments)
					after = c->nl > 0 ? c->sp : 0;
				used = true;
			} else if (c == n->comments) {
				after = c->nl > 0 ? c->sp : 0;
			}
		}
		if (used) {
			L->pnl = 1;
			L->psp = after;
		}
		Token **p = &L->pcomments;
		while (*p)
			p = &(*p)->next;
		*p = n->comments;
		n->comments = NULL;
	}
	Token *before = L->tail;
	lin_children(L, n);
	n->begin = before->next;
	n->end = L->tail;
	L->depth--;
}

static void ds_set_parents(Node *n, int depth);

static void ds_set_parents_list(Node *parent, NodeList *l, int depth)
{
	for (int i = 0; i < l->n; i++) {
		l->v[i]->parent = parent;
		ds_set_parents(l->v[i], depth);
	}
}

static void ds_set_parents(Node *n, int depth)
{
	if (depth > LIN_MAX_DEPTH)
		fail("dart_style: the syntax tree is nested too deeply");
	Node **children[] = {&n->target, &n->ident, &n->ident2, &n->expr, &n->expr2, &n->expr3, &n->type,
			     &n->typeArgs, &n->typeParams, &n->params, &n->args, &n->body, &n->body2, &n->stmt,
			     &n->stmt2, &n->vars, &n->parts, &n->suffix, &n->defaultClause, &n->namePart,
			     &n->clause1, &n->clause2, &n->clause3, &n->ctorName, &n->func, &n->redirect, &n->decl,
			     &n->enumArgs};
	for (size_t i = 0; i < sizeof children / sizeof *children; i++) {
		Node *c = *children[i];
		if (c && c != n) {
			c->parent = n;
			ds_set_parents(c, depth + 1);
		}
	}
	ds_set_parents_list(n, &n->list, depth + 1);
	ds_set_parents_list(n, &n->list2, depth + 1);
	ds_set_parents_list(n, &n->meta, depth + 1);
}

void ds_linearize(Node *unit, Buf *text)
{
	Lin L = {.out = text};
	Token *head = ds_t(TT_EOF, "");
	head->offset = -1;
	L.tail = head;
	text->n = 0;
	lin_node(&L, unit);
	Token *eof = unit->name;
	eof->next = eof;
	unit->begin = head->next;
	unit->end = eof;
	ds_set_parents(unit, 0);
}
