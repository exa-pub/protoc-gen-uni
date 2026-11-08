/* Node construction API (dprint_typescript.h): builds the swc_ecma_ast-shaped trees swc_ecma_parser
 * 0.139.0 would produce for the TypeScript text a generator stands for, so that dprint's rules see
 * the same tree (optional chains wrapped like swc's parse_subscripts, arguments in ExprOrSpread,
 * function parameters in Param, type annotations in TsTypeAnn, ...). */

static Node **va_nodes(Node *first, va_list ap, int *n)
{
	int cap = 8;
	Node **v = xalloc(sizeof(Node *) * (size_t)cap);
	*n = 0;
	for (Node *x = first; x; x = va_arg(ap, Node *)) {
		if (*n == cap) {
			v = xrealloc(v, sizeof(Node *) * (size_t)cap, sizeof(Node *) * (size_t)cap * 2);
			cap *= 2;
		}
		v[(*n)++] = x;
	}
	return v;
}

Node *dts_new(int kind)
{
	Node *n = xalloc(sizeof *n);
	n->kind = (uint16_t)kind;
	return n;
}

Node *dts_leaf(int kind, Str text)
{
	Node *n = dts_new(kind);
	n->text = text;
	return n;
}

Node *dts_leafc(int kind, const char *text) { return dts_leaf(kind, str_c(text)); }

void dts_add(NodeList *l, Node *n)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 4;
		l->v = xrealloc(l->v, sizeof(Node *) * (size_t)l->cap, sizeof(Node *) * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = n;
}

static void cm_add(NodeComment **v, int *n, Str text, bool block, int nl)
{
	NodeComment *w = xalloc(sizeof(NodeComment) * (size_t)(*n + 1));
	if (*n)
		memcpy(w, *v, sizeof(NodeComment) * (size_t)*n);
	w[*n] = (NodeComment){text, block, (uint8_t)nl};
	*v = w;
	(*n)++;
}

void dts_comment(Node *n, Str text, bool block, int nl) { cm_add(&n->cm, &n->ncm, text, block, nl); }
void dts_trailing_comment(Node *n, Str text, bool block, int nl) { cm_add(&n->tcm, &n->ntcm, text, block, nl); }

Node *dts_nl(Node *n, int nl)
{
	n->nl = (uint8_t)nl;
	return n;
}

Node *dts_hint(Node *n, unsigned hints)
{
	n->hints |= (uint16_t)hints;
	return n;
}

static NodeList clone_list(NodeList l, void (*cb)(Node *, const Node *, void *), void *arg)
{
	NodeList r = {0};
	if (!l.n)
		return r;
	r.v = xalloc(sizeof(Node *) * (size_t)l.n);
	r.n = r.cap = l.n;
	for (int i = 0; i < l.n; i++)
		r.v[i] = dts_clone(l.v[i], cb, arg);
	return r;
}

Node *dts_clone(const Node *n, void (*cb)(Node *, const Node *, void *), void *arg)
{
	if (!n)
		return NULL;
	Node *c = xalloc(sizeof *c);
	*c = *n;
	c->parent = NULL;
	c->a = dts_clone(n->a, cb, arg);
	c->b = dts_clone(n->b, cb, arg);
	c->c = dts_clone(n->c, cb, arg);
	c->d = dts_clone(n->d, cb, arg);
	c->list = clone_list(n->list, cb, arg);
	c->list2 = clone_list(n->list2, cb, arg);
	c->list3 = clone_list(n->list3, cb, arg);
	if (cb)
		cb(c, n, arg);
	return c;
}

/* ---- expressions ---- */

Node *dts_id(const char *name) { return dts_leafc(N_IDENT, name); }
Node *dts_idn(Str name) { return dts_leaf(N_IDENT, name); }
Node *dts_str(Str raw) { return dts_leaf(N_STR, raw); }
Node *dts_num(const char *text) { return dts_leafc(N_NUMBER, text); }
Node *dts_bool(bool v) { return dts_leafc(N_BOOL, v ? "true" : "false"); }
Node *dts_null(void) { return dts_new(N_NULL); }
Node *dts_this(void) { return dts_new(N_THIS_EXPR); }

/* swc parse_subscripts: a member or call continuing an optional chain (or starting one with `?.`)
 * is wrapped in OptChainExpr; a call in a chain is an OptCall. */
static Node *opt_chain_wrap(Node *base, Node *obj, bool question_dot)
{
	if (!question_dot && obj->kind != N_OPT_CHAIN_EXPR)
		return base;
	if (base->kind == N_CALL_EXPR)
		base->kind = N_OPT_CALL;
	Node *oc = dts_new(N_OPT_CHAIN_EXPR);
	if (question_dot)
		oc->flags |= F_OPTIONAL;
	oc->a = base;
	return oc;
}

Node *dts_opt_member(Node *obj, const char *prop, bool optional)
{
	Node *m = dts_new(N_MEMBER_EXPR);
	m->a = obj;
	m->b = dts_id(prop);
	return opt_chain_wrap(m, obj, optional);
}

Node *dts_membern(Node *obj, Str prop)
{
	Node *m = dts_new(N_MEMBER_EXPR);
	m->a = obj;
	m->b = dts_idn(prop);
	return opt_chain_wrap(m, obj, false);
}

Node *dts_member(Node *obj, const char *prop) { return dts_opt_member(obj, prop, false); }

Node *dts_opt_index(Node *obj, Node *expr, bool optional)
{
	Node *m = dts_new(N_MEMBER_EXPR);
	m->a = obj;
	Node *c = dts_new(N_COMPUTED_PROP_NAME);
	c->a = expr;
	m->b = c;
	return opt_chain_wrap(m, obj, optional);
}

Node *dts_index(Node *obj, Node *expr) { return dts_opt_index(obj, expr, false); }

static Node *arg_wrap(Node *e)
{
	if (e->kind == N_EXPR_OR_SPREAD)
		return e;
	Node *w = dts_new(N_EXPR_OR_SPREAD);
	w->a = e;
	return w;
}

Node *dts_callv(Node *callee, Node *type_args, NodeList args)
{
	Node *c = dts_new(N_CALL_EXPR);
	c->a = callee;
	c->b = type_args;
	for (int i = 0; i < args.n; i++)
		dts_add(&c->list, arg_wrap(args.v[i]));
	return opt_chain_wrap(c, callee, false);
}

Node *dts_call(Node *callee, ...)
{
	va_list ap;
	va_start(ap, callee);
	int n;
	Node **v = va_nodes(va_arg(ap, Node *), ap, &n);
	va_end(ap);
	return dts_callv(callee, NULL, (NodeList){v, n, n});
}

Node *dts_new_expr(Node *callee, ...)
{
	va_list ap;
	va_start(ap, callee);
	int n;
	Node **v = va_nodes(va_arg(ap, Node *), ap, &n);
	va_end(ap);
	Node *e = dts_new(N_NEW_EXPR);
	e->a = callee;
	e->flags |= F_HAS_ARGS;
	for (int i = 0; i < n; i++)
		dts_add(&e->list, arg_wrap(v[i]));
	return e;
}

Node *dts_bin(int op, Node *a, Node *b)
{
	Node *n = dts_new(N_BIN_EXPR);
	n->op = (uint16_t)op;
	n->a = a;
	n->b = b;
	return n;
}

Node *dts_instanceof(Node *a, Node *b)
{
	Node *n = dts_bin(TK_WORD, a, b);
	n->flags |= F_INSTANCEOF;
	return n;
}

Node *dts_assign(int op, Node *a, Node *b)
{
	Node *n = dts_new(N_ASSIGN_EXPR);
	n->op = (uint16_t)op;
	n->a = a;
	n->b = b;
	return n;
}

Node *dts_unary(int op, Node *a)
{
	Node *n = dts_new(N_UNARY_EXPR);
	n->op = (uint16_t)op;
	n->a = a;
	return n;
}

Node *dts_update(int op, Node *a, bool prefix)
{
	Node *n = dts_new(N_UPDATE_EXPR);
	n->op = (uint16_t)op;
	n->a = a;
	if (prefix)
		n->flags |= F_PREFIX;
	return n;
}

Node *dts_cond(Node *test, Node *cons, Node *alt)
{
	Node *n = dts_new(N_COND_EXPR);
	n->a = test;
	n->b = cons;
	n->c = alt;
	return n;
}

static Node *wrap1(int kind, Node *a)
{
	Node *n = dts_new(kind);
	n->a = a;
	return n;
}

Node *dts_paren(Node *e) { return wrap1(N_PAREN_EXPR, e); }
Node *dts_nonnull(Node *e) { return wrap1(N_TS_NON_NULL_EXPR, e); }
Node *dts_await(Node *e) { return wrap1(N_AWAIT_EXPR, e); }
Node *dts_spread(Node *e) { return wrap1(N_SPREAD_ELEMENT, e); }

Node *dts_as(Node *e, Node *type)
{
	Node *n = wrap1(N_TS_AS_EXPR, e);
	n->b = type;
	return n;
}

Node *dts_yield(Node *e, bool delegate)
{
	Node *n = wrap1(N_YIELD_EXPR, e);
	if (delegate)
		n->flags |= F_DELEGATE;
	return n;
}

Node *dts_spread_arg(Node *e)
{
	Node *n = wrap1(N_EXPR_OR_SPREAD, e);
	n->flags |= F_SPREAD;
	return n;
}

Node *dts_arr(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	Node *a = dts_new(N_ARRAY_LIT);
	for (int i = 0; i < n; i++)
		dts_add(&a->list, arg_wrap(v[i]));
	return a;
}

Node *dts_obj(void) { return dts_new(N_OBJECT_LIT); }
void dts_obj_add(Node *obj, Node *prop) { dts_add(&obj->list, prop); }

Node *dts_kv(Node *key, Node *value)
{
	Node *n = dts_new(N_KEY_VALUE_PROP);
	n->a = key;
	n->b = value;
	return n;
}

Node *dts_kvc(const char *key, Node *value) { return dts_kv(dts_id(key), value); }

Node *dts_method_prop(Node *key, Node *function)
{
	Node *n = dts_new(N_METHOD_PROP);
	n->a = key;
	n->b = function;
	return n;
}

Node *dts_getter_prop(Node *key, Node *type, Node *body)
{
	Node *n = dts_new(N_GETTER_PROP);
	n->a = key;
	n->b = type ? dts_type_ann(type) : NULL;
	n->c = body;
	return n;
}

Node *dts_function(NodeList params, Node *type_params, Node *return_type, Node *body, unsigned flags)
{
	Node *f = dts_new(N_FUNCTION);
	f->list = params;
	f->b = type_params;
	f->c = return_type ? dts_type_ann(return_type) : NULL;
	f->a = body;
	f->flags = flags;
	return f;
}

Node *dts_fn_expr(Node *ident, Node *function)
{
	Node *n = dts_new(N_FN_EXPR);
	n->a = ident;
	n->b = function;
	return n;
}

Node *dts_arrow(NodeList params, Node *return_type, Node *body, unsigned flags)
{
	Node *n = dts_new(N_ARROW_EXPR);
	n->list = params;
	n->c = return_type ? dts_type_ann(return_type) : NULL;
	n->a = body;
	n->flags = flags;
	return n;
}

Node *dts_tpl(int n_exprs, const Str *quasis, Node *const *exprs)
{
	Node *t = dts_new(N_TPL);
	for (int i = 0; i <= n_exprs; i++) {
		dts_add(&t->list2, dts_leaf(N_TPL_ELEMENT, quasis[i]));
		if (i < n_exprs)
			dts_add(&t->list, exprs[i]);
	}
	return t;
}

Node *dts_tagged_tpl(Node *tag, Node *tpl)
{
	Node *n = dts_new(N_TAGGED_TPL);
	n->a = tag;
	n->c = tpl;
	return n;
}

Node *dts_seq(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	Node *s = dts_new(N_SEQ_EXPR);
	for (int i = 0; i < n; i++)
		dts_add(&s->list, v[i]);
	return s;
}

/* ---- patterns and parameters ---- */

Node *dts_bindingn(Str name, Node *type, bool optional)
{
	Node *b = dts_new(N_BINDING_IDENT);
	b->a = dts_idn(name);
	if (optional)
		b->a->flags |= F_OPTIONAL;
	b->b = type ? dts_type_ann(type) : NULL;
	return b;
}

Node *dts_binding(const char *name, Node *type, bool optional) { return dts_bindingn(str_c(name), type, optional); }

Node *dts_assign_pat(Node *pat, Node *value)
{
	Node *n = dts_new(N_ASSIGN_PAT);
	n->a = pat;
	n->b = value;
	return n;
}

Node *dts_param(Node *pat, Node *default_value)
{
	Node *p = dts_new(N_PARAM);
	p->a = default_value ? dts_assign_pat(pat, default_value) : pat;
	return p;
}

Node *dts_rest_pat(Node *pat, Node *type)
{
	Node *n = dts_new(N_REST_PAT);
	n->a = pat;
	n->b = type ? dts_type_ann(type) : NULL;
	return n;
}

Node *dts_param_prop(int accessibility, unsigned flags, Node *pat)
{
	Node *n = dts_new(N_TS_PARAM_PROP);
	n->accessibility = (uint8_t)accessibility;
	n->flags = flags;
	n->a = pat;
	return n;
}

/* ---- statements and declarations ---- */

Node *dts_module(void) { return dts_new(N_MODULE); }
Node *dts_expr_stmt(Node *e) { return wrap1(N_EXPR_STMT, e); }
Node *dts_return(Node *e) { return wrap1(N_RETURN_STMT, e); }
Node *dts_throw(Node *e) { return wrap1(N_THROW_STMT, e); }

Node *dts_if(Node *test, Node *cons, Node *alt)
{
	Node *n = dts_new(N_IF_STMT);
	n->a = test;
	n->b = cons;
	n->c = alt;
	return n;
}

Node *dts_block(void) { return dts_new(N_BLOCK_STMT); }
void dts_block_add(Node *block, Node *stmt) { dts_add(&block->list, stmt); }

Node *dts_declarator(Node *pat, Node *init)
{
	Node *d = dts_new(N_VAR_DECLARATOR);
	d->a = pat;
	d->b = init;
	return d;
}

Node *dts_var_decl(int kind)
{
	Node *v = dts_new(N_VAR_DECL);
	v->op = (uint16_t)kind;
	return v;
}

Node *dts_var(int kind, Node *pat, Node *init)
{
	Node *v = dts_var_decl(kind);
	dts_add(&v->list, dts_declarator(pat, init));
	return v;
}

Node *dts_for(Node *init, Node *test, Node *update, Node *body)
{
	Node *n = dts_new(N_FOR_STMT);
	n->a = init;
	n->b = test;
	n->c = update;
	n->d = body;
	return n;
}

Node *dts_for_of(Node *left, Node *right, Node *body, bool await)
{
	Node *n = dts_new(N_FOR_OF_STMT);
	n->a = left;
	n->b = right;
	n->d = body;
	if (await)
		n->flags |= F_AWAIT;
	return n;
}

Node *dts_for_in(Node *left, Node *right, Node *body)
{
	Node *n = dts_new(N_FOR_IN_STMT);
	n->a = left;
	n->b = right;
	n->d = body;
	return n;
}

Node *dts_while(Node *test, Node *body)
{
	Node *n = dts_new(N_WHILE_STMT);
	n->a = test;
	n->d = body;
	return n;
}

Node *dts_do_while(Node *body, Node *test)
{
	Node *n = dts_new(N_DO_WHILE_STMT);
	n->a = test;
	n->d = body;
	return n;
}

Node *dts_switch(Node *disc) { return wrap1(N_SWITCH_STMT, disc); }
Node *dts_case(Node *test) { return wrap1(N_SWITCH_CASE, test); }
Node *dts_break(void) { return dts_new(N_BREAK_STMT); }
Node *dts_continue(void) { return dts_new(N_CONTINUE_STMT); }

Node *dts_try(Node *block, Node *catch_param, Node *catch_body, Node *finally_block)
{
	Node *n = dts_new(N_TRY_STMT);
	n->a = block;
	if (catch_body) {
		Node *c = dts_new(N_CATCH_CLAUSE);
		c->a = catch_param;
		c->b = catch_body;
		n->b = c;
	}
	n->c = finally_block;
	return n;
}

Node *dts_export(Node *decl) { return wrap1(N_EXPORT_DECL, decl); }
Node *dts_export_default(Node *e) { return wrap1(N_EXPORT_DEFAULT_EXPR, e); }

Node *dts_fn_decl(Node *ident, Node *function)
{
	Node *n = dts_new(N_FN_DECL);
	n->a = ident;
	n->b = function;
	return n;
}

Node *dts_class_decl(Node *ident, Node *class_)
{
	Node *n = dts_new(N_CLASS_DECL);
	n->a = ident;
	n->b = class_;
	return n;
}

Node *dts_class(Node *super_class, Node *type_params)
{
	Node *n = dts_new(N_CLASS);
	n->a = super_class;
	n->b = type_params;
	return n;
}

void dts_class_add(Node *class_, Node *member) { dts_add(&class_->list, member); }

Node *dts_class_method(Node *key, Node *function, unsigned flags, int accessibility, int kind)
{
	Node *n = dts_new(N_CLASS_METHOD);
	n->a = key;
	n->b = function;
	n->flags = flags;
	n->accessibility = (uint8_t)accessibility;
	n->op = (uint16_t)kind;
	return n;
}

Node *dts_class_prop(Node *key, Node *type, Node *value, unsigned flags, int accessibility)
{
	Node *n = dts_new(N_CLASS_PROP);
	n->a = key;
	n->c = type ? dts_type_ann(type) : NULL;
	n->b = value;
	n->flags = flags;
	n->accessibility = (uint8_t)accessibility;
	return n;
}

Node *dts_constructor(NodeList params, Node *body, int accessibility)
{
	Node *n = dts_new(N_CONSTRUCTOR);
	n->a = dts_id("constructor");
	n->list = params;
	n->b = body;
	n->accessibility = (uint8_t)accessibility;
	return n;
}

Node *dts_interface(Node *ident, Node *type_params)
{
	Node *n = dts_new(N_TS_INTERFACE_DECL);
	n->a = ident;
	n->b = type_params;
	n->c = dts_new(N_TS_INTERFACE_BODY);
	return n;
}

void dts_interface_add(Node *iface, Node *member) { dts_add(&iface->c->list, member); }

Node *dts_type_alias(Node *ident, Node *type_params, Node *type)
{
	Node *n = dts_new(N_TS_TYPE_ALIAS_DECL);
	n->a = ident;
	n->b = type_params;
	n->c = type;
	return n;
}

Node *dts_enum(Node *ident, bool is_const)
{
	Node *n = dts_new(N_TS_ENUM_DECL);
	n->a = ident;
	if (is_const)
		n->flags |= F_CONST;
	return n;
}

void dts_enum_add(Node *e, Node *key, Node *init)
{
	Node *m = dts_new(N_TS_ENUM_MEMBER);
	m->a = key;
	m->b = init;
	dts_add(&e->list, m);
}

Node *dts_namespace(Node *ident)
{
	Node *n = dts_new(N_TS_MODULE_DECL);
	n->flags |= F_NAMESPACE;
	n->a = ident;
	n->b = dts_new(N_TS_MODULE_BLOCK);
	return n;
}

Node *dts_import(Node *src, bool type_only)
{
	Node *n = dts_new(N_IMPORT_DECL);
	n->a = src;
	if (type_only)
		n->flags |= F_TYPE_ONLY;
	return n;
}

void dts_import_named(Node *imp, Node *imported, Node *local, bool type_only)
{
	Node *s = dts_new(N_IMPORT_NAMED_SPECIFIER);
	s->a = local;
	s->b = imported;
	if (type_only)
		s->flags |= F_TYPE_ONLY;
	dts_add(&imp->list, s);
}

void dts_import_default(Node *imp, Node *local) { dts_add(&imp->list, wrap1(N_IMPORT_DEFAULT_SPECIFIER, local)); }
void dts_import_star(Node *imp, Node *local) { dts_add(&imp->list, wrap1(N_IMPORT_STAR_AS_SPECIFIER, local)); }

Node *dts_import_equals(Node *id, Node *src, bool type_only)
{
	Node *n = dts_new(N_TS_IMPORT_EQUALS_DECL);
	n->a = id;
	n->b = wrap1(N_TS_EXTERNAL_MODULE_REF, src);
	if (type_only)
		n->flags |= F_TYPE_ONLY;
	return n;
}

Node *dts_export_named(Node *src, bool type_only)
{
	Node *n = dts_new(N_NAMED_EXPORT);
	n->a = src;
	if (type_only)
		n->flags |= F_TYPE_ONLY;
	return n;
}

void dts_export_specifier(Node *exp, Node *orig, Node *exported)
{
	Node *s = dts_new(N_EXPORT_NAMED_SPECIFIER);
	s->a = orig;
	s->b = exported;
	dts_add(&exp->list, s);
}

void dts_export_namespace(Node *exp, Node *name) { dts_add(&exp->list, wrap1(N_EXPORT_NAMESPACE_SPECIFIER, name)); }

Node *dts_export_all(Node *src, bool type_only)
{
	Node *n = wrap1(N_EXPORT_ALL, src);
	if (type_only)
		n->flags |= F_TYPE_ONLY;
	return n;
}

/* ---- types ---- */

Node *dts_tkw(const char *keyword) { return dts_leafc(N_TS_KEYWORD_TYPE, keyword); }

Node *dts_entity(const char *dotted)
{
	Node *n = NULL;
	const char *s = dotted;
	for (;;) {
		const char *dot = strchr(s, '.');
		size_t len = dot ? (size_t)(dot - s) : strlen(s);
		Node *id = dts_idn((Str){s, len});
		if (!n) {
			n = id;
		} else {
			Node *q = dts_new(N_TS_QUALIFIED_NAME);
			q->a = n;
			q->b = id;
			n = q;
		}
		if (!dot)
			return n;
		s = dot + 1;
	}
}

Node *dts_targs(NodeList args)
{
	if (!args.n)
		return NULL;
	Node *n = dts_new(N_TS_TYPE_PARAM_INSTANTIATION);
	n->list = args;
	return n;
}

Node *dts_tref_node(Node *entity, NodeList args)
{
	Node *n = dts_new(N_TS_TYPE_REF);
	n->a = entity;
	n->b = dts_targs(args);
	return n;
}

Node *dts_trefn(Str name, NodeList args) { return dts_tref_node(dts_entity(xstrndup(name.p, name.n)), args); }

Node *dts_tref(const char *name, ...)
{
	va_list ap;
	va_start(ap, name);
	int n;
	Node **v = va_nodes(va_arg(ap, Node *), ap, &n);
	va_end(ap);
	return dts_tref_node(dts_entity(name), (NodeList){v, n, n});
}

Node *dts_tarray(Node *elem) { return wrap1(N_TS_ARRAY_TYPE, elem); }

Node *dts_tunionv(NodeList types)
{
	Node *n = dts_new(N_TS_UNION_TYPE);
	n->list = types;
	return n;
}

Node *dts_tunion(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	return dts_tunionv((NodeList){v, n, n});
}

Node *dts_tintersection(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	Node *t = dts_new(N_TS_INTERSECTION_TYPE);
	t->list = (NodeList){v, n, n};
	return t;
}

Node *dts_tlit(Node *literal) { return wrap1(N_TS_LIT_TYPE, literal); }
Node *dts_tparen(Node *t) { return wrap1(N_TS_PARENTHESIZED_TYPE, t); }

Node *dts_tfn(NodeList params, Node *type_params, Node *ret)
{
	Node *n = dts_new(N_TS_FN_TYPE);
	n->list = params;
	n->b = type_params;
	n->c = dts_type_ann(ret);
	return n;
}

Node *dts_ttype_lit(void) { return dts_new(N_TS_TYPE_LIT); }
void dts_ttype_lit_add(Node *lit, Node *member) { dts_add(&lit->list, member); }

Node *dts_prop_sig(Node *key, Node *type, bool optional, bool readonly)
{
	Node *n = dts_new(N_TS_PROPERTY_SIGNATURE);
	n->a = key;
	n->c = type ? dts_type_ann(type) : NULL;
	if (optional)
		n->flags |= F_OPTIONAL;
	if (readonly)
		n->flags |= F_READONLY;
	return n;
}

Node *dts_method_sig(Node *key, Node *type_params, NodeList params, Node *ret, bool optional)
{
	/* swc reads a member `new(...)` or `new<...>(...)` as a construct signature */
	bool construct = !optional && key->kind == N_IDENT && str_eqc(key->text, "new");
	Node *n = dts_new(construct ? N_TS_CONSTRUCT_SIGNATURE_DECL : N_TS_METHOD_SIGNATURE);
	n->a = construct ? NULL : key;
	n->b = type_params;
	n->list = params;
	n->c = ret ? dts_type_ann(ret) : NULL;
	if (optional)
		n->flags |= F_OPTIONAL;
	return n;
}

Node *dts_index_sig(const char *param, Node *param_type, Node *type, bool readonly)
{
	Node *n = dts_new(N_TS_INDEX_SIGNATURE);
	dts_add(&n->list, dts_binding(param, param_type, false));
	n->c = type ? dts_type_ann(type) : NULL;
	if (readonly)
		n->flags |= F_READONLY;
	return n;
}

Node *dts_tparam(const char *name, Node *constraint, Node *default_type)
{
	Node *n = dts_new(N_TS_TYPE_PARAM);
	n->a = dts_id(name);
	n->b = constraint;
	n->c = default_type;
	return n;
}

Node *dts_tparams(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	Node *d = dts_new(N_TS_TYPE_PARAM_DECL);
	d->list = (NodeList){v, n, n};
	return d;
}

Node *dts_tcond(Node *check, Node *extends, Node *t, Node *f)
{
	Node *n = dts_new(N_TS_CONDITIONAL_TYPE);
	n->a = check;
	n->b = extends;
	n->c = t;
	n->d = f;
	return n;
}

Node *dts_tindexed(Node *obj, Node *index)
{
	Node *n = dts_new(N_TS_INDEXED_ACCESS_TYPE);
	n->a = obj;
	n->b = index;
	return n;
}

Node *dts_tkeyof(Node *t)
{
	Node *n = wrap1(N_TS_TYPE_OPERATOR, t);
	n->op = TYPEOP_KEYOF;
	return n;
}

Node *dts_treadonly(Node *t)
{
	Node *n = wrap1(N_TS_TYPE_OPERATOR, t);
	n->op = TYPEOP_READONLY;
	return n;
}

Node *dts_tinfer(const char *name) { return wrap1(N_TS_INFER_TYPE, dts_tparam(name, NULL, NULL)); }
Node *dts_tquery(Node *entity) { return wrap1(N_TS_TYPE_QUERY, entity); }

Node *dts_tmapped(const char *param, Node *in, Node *as, Node *type, int readonly, int optional)
{
	Node *n = dts_new(N_TS_MAPPED_TYPE);
	n->a = dts_tparam(param, in, NULL);
	n->b = as;
	n->c = type;
	n->mapped_readonly = (uint8_t)readonly;
	n->mapped_optional = (uint8_t)optional;
	return n;
}

Node *dts_ttuple(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	int n;
	Node **v = va_nodes(first, ap, &n);
	va_end(ap);
	Node *t = dts_new(N_TS_TUPLE_TYPE);
	for (int i = 0; i < n; i++) {
		Node *e = dts_new(N_TS_TUPLE_ELEMENT);
		e->b = v[i];
		dts_add(&t->list, e);
	}
	return t;
}

Node *dts_type_ann(Node *type) { return wrap1(N_TS_TYPE_ANN, type); }
