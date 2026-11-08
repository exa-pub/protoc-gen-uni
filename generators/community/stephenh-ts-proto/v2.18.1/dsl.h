#define UNUSED __attribute__((unused))

/* Short names for building dprint-typescript nodes (dprint_typescript.h) and ts-poet codes
 * (ts_poet.h) in the port of ts-proto's templates. Lists are NULL-terminated. */

#define ID(s) dts_id(s)
#define IDN(s) dts_idn(s)
#define MEM(o, p) dts_member(o, p)
#define NUM(s) dts_num(s)
#define BIN(op, a, b) dts_bin(TK_##op, a, b)
#define SET(a, b) dts_assign(TK_ASSIGN, a, b)
#define ES(e) dts_expr_stmt(e)
#define RET(e) dts_return(e)
#define KW(k) dts_tkw(k)
#define NL(n, k) dts_nl(n, NL_##k)
#define BLANK(n) dts_nl(n, NL_BLANK)
#define HINT(n, h) dts_hint(n, h)
#define UNDEF() dts_id("undefined")

/* a string literal as ts-poet prints it (raw, with its quotes) */
static Node *str_raw(const char *raw) { return dts_str(str_c(raw)); }
#define SQ(s) str_raw("'" s "'")
#define DQ(s) str_raw("\"" s "\"")
static Node *sq(const char *s) { return str_raw(xcat("'", s, "'")); }
static Node *dq(const char *s) { return str_raw(xcat("\"", s, "\"")); }
/* an integer in an expression: `-1` is a unary minus (swc) */
static Node *numi(long long v)
{
	if (v < 0)
		return dts_unary(UNARY_MINUS, dts_num(xfmt("%lld", -(v + 1) + 1LL))); /* v is an int32 */
	return dts_num(xfmt("%lld", v));
}

static NodeList nl_of(Node *first, va_list ap)
{
	NodeList l = {0};
	for (Node *x = first; x; x = va_arg(ap, Node *))
		dts_add(&l, x);
	return l;
}

static NodeList L(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	return l;
}

static Node *block(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	Node *b = dts_block();
	b->list = l;
	return b;
}

static Node *blockl(NodeList l)
{
	Node *b = dts_block();
	b->list = l;
	return b;
}

static void addl(NodeList *dst, NodeList src)
{
	for (int i = 0; i < src.n; i++)
		dts_add(dst, src.v[i]);
}

static Node *call(Node *callee, ...)
{
	va_list ap;
	va_start(ap, callee);
	Node *first = va_arg(ap, Node *);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	return dts_callv(callee, NULL, l);
}

/* obj.method(args...) */
static Node *mcall(Node *obj, const char *method, ...)
{
	va_list ap;
	va_start(ap, method);
	Node *first = va_arg(ap, Node *);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	return dts_callv(dts_member(obj, method), NULL, l);
}

static Node *newx(Node *callee, ...)
{
	va_list ap;
	va_start(ap, callee);
	Node *first = va_arg(ap, Node *);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	Node *e = dts_new_expr(callee, NULL);
	for (int i = 0; i < l.n; i++) {
		Node *w = dts_new(N_EXPR_OR_SPREAD);
		w->a = l.v[i];
		dts_add(&e->list, w);
	}
	return e;
}

/* `if (test) { stmts }` */
UNUSED static Node *if_(Node *test, NodeList cons, Node *alt) { return dts_if(test, blockl(cons), alt); }

/* function parameter `name: type` (optional: `name?: type`) with an optional default */
static Node *param(const char *name, Node *type, bool optional, Node *def)
{
	return dts_param(dts_binding(name, type, optional), def);
}

/* a parameter of a method/call signature or function type (swc: TsFnParam, a pattern) */
static Node *sparam(const char *name, Node *type, bool optional) { return dts_binding(name, type, optional); }

/* method property `name<tparams>(params): ret { body }` */
static Node *method(const char *name, Node *tparams, NodeList params, Node *ret, Node *body)
{
	return dts_method_prop(dts_id(name), dts_function(params, tparams, ret, body, 0));
}

static Node *const_(const char *name, Node *type, Node *init) { return dts_var(VAR_CONST, dts_binding(name, type, false), init); }
static Node *let_(const char *name, Node *type, Node *init) { return dts_var(VAR_LET, dts_binding(name, type, false), init); }

static Node *tref(const char *name, ...)
{
	va_list ap;
	va_start(ap, name);
	Node *first = va_arg(ap, Node *);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	return dts_tref_node(dts_entity(name), l);
}

static Node *arrow(NodeList params, Node *ret, Node *body) { return dts_arrow(params, ret, body, 0); }

/* `(params) => body` with untyped parameters */
static Node *arrow1(const char *p, Node *body) { return dts_arrow(L(dts_binding(p, NULL, false), NULL), NULL, body, 0); }

static Node *obj(Node *first, ...)
{
	va_list ap;
	va_start(ap, first);
	NodeList l = nl_of(first, ap);
	va_end(ap);
	Node *o = dts_obj();
	o->list = l;
	return o;
}

static Node *kv(const char *key, Node *v) { return dts_kvc(key, v); }
/* shorthand property `{ key }` (swc: Prop::Shorthand, an Ident in the list) */
static Node *shorthand(const char *key) { return dts_id(key); }

static Node *fn_decl(const char *name, Node *tparams, NodeList params, Node *ret, Node *body)
{
	return dts_fn_decl(dts_id(name), dts_function(params, tparams, ret, body, 0));
}

/* `e as const` (swc: TsConstAssertion) */
static Node *as_const(Node *e)
{
	Node *n = dts_new(N_TS_CONST_ASSERTION);
	n->a = e;
	return n;
}
