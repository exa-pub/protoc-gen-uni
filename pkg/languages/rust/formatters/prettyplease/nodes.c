/* Node construction (prettyplease.h): the syn node kinds as PpNode values. Not a port; the shapes
 * follow syn 2.0.114. */

#include <stdarg.h>

PpNode *pp_node(int kind)
{
	PpNode *n = xalloc(sizeof *n);
	n->kind = (uint8_t)kind;
	return n;
}

static void list_push(PpList *l, PpNode *child)
{
	if (!child)
		fail("prettyplease: NULL node in a list");
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 4;
		l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v, (size_t)cap * sizeof *l->v);
		l->cap = cap;
	}
	l->v[l->n++] = child;
}

void pp_push(PpNode *parent, PpNode *child) { list_push(&parent->list, child); }

void pp_push_attr(PpNode *node, PpNode *attr)
{
	if (!node->attrs)
		node->attrs = xalloc(sizeof *node->attrs);
	list_push(node->attrs, attr);
}

static PpNode **generics_slot(PpNode *item)
{
	switch (item->kind) {
	case PP_ITEM_CONST: case PP_ITEM_ENUM: case PP_ITEM_STRUCT:
		return &item->a;
	case PP_ITEM_TRAIT: case PP_ITEM_TYPE: case PP_IMPL_TYPE:
		return &item->b;
	case PP_ITEM_IMPL: case PP_SIG: case PP_TRAIT_TYPE:
		return &item->c;
	case PP_ITEM_FN:
		return &item->a->c;
	default:
		fail("prettyplease: node kind %d has no generics", item->kind);
	}
}

void pp_set_generics(PpNode *item, PpNode *generics) { *generics_slot(item) = generics; }
PpNode *pp_generics_of(const PpNode *item) { return *generics_slot((PpNode *)item); }
void pp_set_vis(PpNode *item, int vis) { item->aux = (uint16_t)vis; }

static void push_va(PpNode *parent, PpNode *first, va_list ap)
{
	for (PpNode *n = first; n; n = va_arg(ap, PpNode *))
		pp_push(parent, n);
}

#define VA_PUSH(parent, first)                                                                     \
	do {                                                                                       \
		va_list ap;                                                                        \
		va_start(ap, first);                                                               \
		push_va(parent, first, ap);                                                        \
		va_end(ap);                                                                        \
	} while (0)

static PpNode *mk(int kind, PpNode *a, PpNode *b, PpNode *c)
{
	PpNode *n = pp_node(kind);
	n->a = a;
	n->b = b;
	n->c = c;
	return n;
}

static PpNode *mkt(int kind, const char *text, PpNode *a, PpNode *b)
{
	PpNode *n = mk(kind, a, b, NULL);
	n->text = text;
	return n;
}

/* ---- paths ---- */

PpNode *pp_path_push(PpNode *path, const char *ident, PpNode *args)
{
	pp_push(path, mkt(PP_SEGMENT, ident, args, NULL));
	return path;
}

PpNode *pp_path_ident(const char *ident)
{
	return pp_path_push(pp_node(PP_PATH), ident, NULL);
}

PpNode *pp_path(const char *s)
{
	PpNode *path = pp_node(PP_PATH);
	if (s[0] == ':' && s[1] == ':') {
		path->flags |= PP_F_LEADING_COLON;
		s += 2;
	}
	for (;;) {
		const char *e = strstr(s, "::");
		if (!e) {
			pp_path_push(path, s, NULL);
			return path;
		}
		pp_path_push(path, xstrndup(s, (size_t)(e - s)), NULL);
		s = e + 2;
	}
}

PpNode *pp_path_args(PpNode *path, PpNode *args)
{
	path->list.v[path->list.n - 1]->a = args;
	return path;
}

PpNode *pp_angle(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_ANGLE_ARGS);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_lifetime(const char *lifetime) { return mkt(PP_LIFETIME, lifetime, NULL, NULL); }

/* ---- types ---- */

PpNode *pp_ty_path(PpNode *path) { return mk(PP_TY_PATH, NULL, path, NULL); }
PpNode *pp_ty(const char *path) { return pp_ty_path(pp_path(path)); }

PpNode *pp_ty_gen(const char *path, PpNode *first, ...)
{
	PpNode *args = pp_node(PP_ANGLE_ARGS);
	VA_PUSH(args, first);
	return pp_ty_path(pp_path_args(pp_path(path), args));
}

PpNode *pp_ty_qpath(PpNode *qself, int position, PpNode *path)
{
	PpNode *n = mk(PP_TY_PATH, qself, path, NULL);
	n->aux = (uint16_t)position;
	return n;
}

PpNode *pp_ty_ref(const char *lifetime, bool mut, PpNode *elem)
{
	PpNode *n = mkt(PP_TY_REF, lifetime, elem, NULL);
	n->flags = mut ? PP_F_MUT : 0;
	return n;
}

PpNode *pp_ty_ptr(bool mut, PpNode *elem)
{
	PpNode *n = mk(PP_TY_PTR, elem, NULL, NULL);
	n->flags = mut ? PP_F_MUT : 0;
	return n;
}

PpNode *pp_ty_slice(PpNode *elem) { return mk(PP_TY_SLICE, elem, NULL, NULL); }
PpNode *pp_ty_array(PpNode *elem, PpNode *len) { return mk(PP_TY_ARRAY, elem, len, NULL); }

PpNode *pp_ty_tuple(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_TY_TUPLE);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_ty_paren(PpNode *elem) { return mk(PP_TY_PAREN, elem, NULL, NULL); }
PpNode *pp_ty_impl(PpNode *bounds) { return mk(PP_TY_IMPL, bounds, NULL, NULL); }
PpNode *pp_ty_dyn(PpNode *bounds) { return mk(PP_TY_DYN, bounds, NULL, NULL); }
PpNode *pp_ty_never(void) { return pp_node(PP_TY_NEVER); }
PpNode *pp_ty_infer(void) { return pp_node(PP_TY_INFER); }

PpNode *pp_bounds(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_BOUNDS);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_trait_bound(PpNode *path, bool maybe)
{
	PpNode *n = mk(PP_TRAIT_BOUND, path, NULL, NULL);
	n->flags = maybe ? PP_F_MAYBE : 0;
	return n;
}

PpNode *pp_precise_capture(const char *first, ...)
{
	PpNode *n = pp_node(PP_PRECISE_CAPTURE);
	va_list ap;
	va_start(ap, first);
	for (const char *s = first; s; s = va_arg(ap, const char *))
		pp_push(n, mkt(*s == '\'' ? PP_LIFETIME : PP_IDENT, s, NULL, NULL));
	va_end(ap);
	return n;
}

/* ---- expressions ---- */

PpNode *pp_e_path(PpNode *path) { return mk(PP_E_PATH, NULL, path, NULL); }
PpNode *pp_e(const char *path) { return pp_e_path(pp_path(path)); }

PpNode *pp_e_qpath(PpNode *qself, int position, PpNode *path)
{
	PpNode *n = mk(PP_E_PATH, qself, path, NULL);
	n->aux = (uint16_t)position;
	return n;
}

PpNode *pp_e_lit(PpNode *lit) { return mk(PP_E_LIT, lit, NULL, NULL); }

PpNode *pp_e_call(PpNode *func, ...)
{
	PpNode *n = mk(PP_E_CALL, func, NULL, NULL);
	va_list ap;
	va_start(ap, func);
	PpNode *first = va_arg(ap, PpNode *);
	push_va(n, first, ap);
	va_end(ap);
	return n;
}

PpNode *pp_e_method(PpNode *receiver, const char *method, ...)
{
	PpNode *n = mkt(PP_E_METHOD, method, receiver, NULL);
	va_list ap;
	va_start(ap, method);
	PpNode *first = va_arg(ap, PpNode *);
	push_va(n, first, ap);
	va_end(ap);
	return n;
}

PpNode *pp_e_method_tf(PpNode *receiver, const char *method, PpNode *turbofish, ...)
{
	PpNode *n = mkt(PP_E_METHOD, method, receiver, turbofish);
	va_list ap;
	va_start(ap, turbofish);
	PpNode *first = va_arg(ap, PpNode *);
	push_va(n, first, ap);
	va_end(ap);
	return n;
}

PpNode *pp_e_field(PpNode *base, const char *member) { return mkt(PP_E_FIELD, member, base, NULL); }

PpNode *pp_e_ref(bool mut, PpNode *e)
{
	PpNode *n = mk(PP_E_REF, e, NULL, NULL);
	n->flags = mut ? PP_F_MUT : 0;
	return n;
}

PpNode *pp_e_unary(const char *op, PpNode *e) { return mkt(PP_E_UNARY, op, e, NULL); }
PpNode *pp_e_binary(PpNode *l, const char *op, PpNode *r) { return mkt(PP_E_BINARY, op, l, r); }
PpNode *pp_e_assign(PpNode *l, PpNode *r) { return mk(PP_E_ASSIGN, l, r, NULL); }
PpNode *pp_e_cast(PpNode *e, PpNode *ty) { return mk(PP_E_CAST, e, ty, NULL); }
PpNode *pp_e_try(PpNode *e) { return mk(PP_E_TRY, e, NULL, NULL); }
PpNode *pp_e_await(PpNode *e) { return mk(PP_E_AWAIT, e, NULL, NULL); }
PpNode *pp_e_index(PpNode *e, PpNode *index) { return mk(PP_E_INDEX, e, index, NULL); }
PpNode *pp_e_paren(PpNode *e) { return mk(PP_E_PAREN, e, NULL, NULL); }

PpNode *pp_e_tuple(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_E_TUPLE);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_e_array(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_E_ARRAY);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_e_repeat(PpNode *e, PpNode *len) { return mk(PP_E_REPEAT, e, len, NULL); }

PpNode *pp_e_range(PpNode *start, PpNode *end, bool closed)
{
	PpNode *n = mk(PP_E_RANGE, start, end, NULL);
	n->flags = closed ? PP_F_CLOSED : 0;
	return n;
}

PpNode *pp_e_block(PpNode *block) { return mk(PP_E_BLOCK, block, NULL, NULL); }
PpNode *pp_e_unsafe(PpNode *block) { return mk(PP_E_UNSAFE, block, NULL, NULL); }

PpNode *pp_e_async(PpNode *block, bool move)
{
	PpNode *n = mk(PP_E_ASYNC, block, NULL, NULL);
	n->flags = move ? PP_F_MOVE : 0;
	return n;
}

PpNode *pp_e_if(PpNode *cond, PpNode *then_block, PpNode *else_branch)
{
	return mk(PP_E_IF, cond, then_block, else_branch);
}

PpNode *pp_e_let(PpNode *pat, PpNode *e) { return mk(PP_E_LET, pat, e, NULL); }
PpNode *pp_e_match(PpNode *e) { return mk(PP_E_MATCH, e, NULL, NULL); }
PpNode *pp_arm(PpNode *pat, PpNode *guard, PpNode *body) { return mk(PP_ARM, pat, guard, body); }

PpNode *pp_e_closure(PpNode *body, bool move)
{
	PpNode *n = mk(PP_E_CLOSURE, body, NULL, NULL);
	n->flags = move ? PP_F_MOVE : 0;
	return n;
}

PpNode *pp_e_struct(PpNode *path, PpNode *rest) { return mk(PP_E_STRUCT, NULL, path, rest); }

PpNode *pp_field_value(const char *member, PpNode *e)
{
	PpNode *n = mkt(PP_FIELD_VALUE, member, e, NULL);
	n->flags = e ? 0 : PP_F_SHORTHAND;
	return n;
}

PpNode *pp_e_return(PpNode *e) { return mk(PP_E_RETURN, e, NULL, NULL); }
PpNode *pp_e_break(const char *label, PpNode *e) { return mkt(PP_E_BREAK, label, e, NULL); }
PpNode *pp_e_continue(const char *label) { return mkt(PP_E_CONTINUE, label, NULL, NULL); }

PpNode *pp_e_for(PpNode *pat, PpNode *e, PpNode *body) { return mk(PP_E_FOR, pat, e, body); }
PpNode *pp_e_while(PpNode *cond, PpNode *body) { return mk(PP_E_WHILE, cond, body, NULL); }
PpNode *pp_e_loop(PpNode *body) { return mk(PP_E_LOOP, body, NULL, NULL); }
PpNode *pp_e_macro(PpNode *mac) { return mk(PP_E_MACRO, mac, NULL, NULL); }
PpNode *pp_e_infer(void) { return pp_node(PP_E_INFER); }

/* ---- literals ---- */

PpNode *pp_lit(int kind, const char *repr)
{
	PpNode *n = mkt(PP_LIT, repr, NULL, NULL);
	n->aux = (uint16_t)kind;
	return n;
}

PpNode *pp_lit_str_repr(const char *repr, Str value)
{
	PpNode *n = pp_lit(PP_LIT_STR, repr);
	PpNode *v = mkt(PP_STRVAL, value.p, NULL, NULL);
	if (value.n > UINT32_MAX)
		fail("prettyplease: string literal too long");
	v->len = (uint32_t)value.n;
	n->a = v;
	return n;
}

PpNode *pp_e_lit_int(const char *repr) { return pp_e_lit(pp_lit(PP_LIT_INT, repr)); }
PpNode *pp_e_lit_bool(bool v) { return pp_e_lit(pp_lit(PP_LIT_BOOL, v ? "true" : "false")); }

/* ---- statements ---- */

PpNode *pp_block(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_BLOCK);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_s_local(PpNode *pat, PpNode *init) { return mk(PP_S_LOCAL, pat, init, NULL); }

PpNode *pp_s_expr(PpNode *e, bool semi)
{
	if (e->kind == PP_E_MACRO && (semi || e->a->aux == PP_DELIM_BRACE)) {
		PpNode *s = mk(PP_S_MACRO, e->a, NULL, NULL);
		s->attrs = e->attrs;
		s->flags = semi ? PP_F_SEMI : 0;
		return s;
	}
	PpNode *s = mk(PP_S_EXPR, e, NULL, NULL);
	s->flags = semi ? PP_F_SEMI : 0;
	return s;
}

PpNode *pp_s_item(PpNode *item) { return mk(PP_S_ITEM, item, NULL, NULL); }

/* ---- patterns ---- */

PpNode *pp_p_ident(const char *ident, bool ref, bool mut)
{
	PpNode *n = mkt(PP_P_IDENT, ident, NULL, NULL);
	n->flags = (ref ? PP_F_REF : 0) | (mut ? PP_F_MUT : 0);
	return n;
}

PpNode *pp_p_wild(void) { return pp_node(PP_P_WILD); }
PpNode *pp_p_rest(void) { return pp_node(PP_P_REST); }
PpNode *pp_p_lit(PpNode *e) { return mk(PP_P_LIT, e, NULL, NULL); }
PpNode *pp_p_path(PpNode *path) { return mk(PP_P_PATH, NULL, path, NULL); }

PpNode *pp_p_tuple_struct(PpNode *path, ...)
{
	PpNode *n = mk(PP_P_TUPLE_STRUCT, NULL, path, NULL);
	va_list ap;
	va_start(ap, path);
	PpNode *first = va_arg(ap, PpNode *);
	push_va(n, first, ap);
	va_end(ap);
	return n;
}

PpNode *pp_p_tuple(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_P_TUPLE);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_p_struct(PpNode *path, bool rest)
{
	PpNode *n = mk(PP_P_STRUCT, NULL, path, NULL);
	n->flags = rest ? PP_F_REST : 0;
	return n;
}

PpNode *pp_field_pat(const char *member, PpNode *pat)
{
	PpNode *n = mkt(PP_FIELD_PAT, member, pat ? pat : pp_p_ident(member, false, false), NULL);
	n->flags = pat ? 0 : PP_F_SHORTHAND;
	return n;
}

PpNode *pp_p_or(PpNode *first, ...)
{
	PpNode *n = pp_node(PP_P_OR);
	VA_PUSH(n, first);
	return n;
}

PpNode *pp_p_ref(bool mut, PpNode *pat)
{
	PpNode *n = mk(PP_P_REF, pat, NULL, NULL);
	n->flags = mut ? PP_F_MUT : 0;
	return n;
}

PpNode *pp_p_type(PpNode *pat, PpNode *ty) { return mk(PP_P_TYPE, pat, ty, NULL); }
PpNode *pp_p_macro(PpNode *mac) { return mk(PP_P_MACRO, mac, NULL, NULL); }

PpNode *pp_p_range(PpNode *lo, PpNode *hi, bool closed)
{
	PpNode *n = mk(PP_P_RANGE, lo, hi, NULL);
	n->flags = closed ? PP_F_CLOSED : 0;
	return n;
}

/* ---- attributes ---- */

static PpNode *attr(int meta, PpNode *path, bool inner)
{
	PpNode *n = mk(PP_ATTR, path, NULL, NULL);
	n->aux = (uint16_t)meta;
	n->flags = inner ? PP_F_INNER : 0;
	return n;
}

PpNode *pp_attr_path(PpNode *path, bool inner) { return attr(PP_META_PATH, path, inner); }

PpNode *pp_attr_list(PpNode *path, int delimiter, bool inner)
{
	PpNode *n = attr(PP_META_LIST, path, inner);
	n->flags |= (uint8_t)(delimiter << 4);
	return n;
}

PpNode *pp_attr_name_value(PpNode *path, PpNode *value, bool inner)
{
	PpNode *n = attr(PP_META_NAME_VALUE, path, inner);
	n->b = value;
	return n;
}

/* ---- token trees ---- */

PpNode *pp_macro(PpNode *path, int delimiter)
{
	PpNode *n = mk(PP_MACRO, path, NULL, NULL);
	n->aux = (uint16_t)delimiter;
	return n;
}

PpNode *pp_tt_ident(const char *ident) { return mkt(PP_TT_IDENT, ident, NULL, NULL); }

PpNode *pp_tt_punct(char ch, bool joint)
{
	PpNode *n = pp_node(PP_TT_PUNCT);
	n->aux = (uint16_t)(unsigned char)ch;
	n->flags = joint ? PP_F_JOINT : 0;
	return n;
}

PpNode *pp_tt_lit(const char *repr) { return mkt(PP_TT_LIT, repr, NULL, NULL); }

PpNode *pp_tt_group(int delimiter)
{
	PpNode *n = pp_node(PP_TT_GROUP);
	n->aux = (uint16_t)delimiter;
	return n;
}

void pp_tt_push_op(PpNode *group, const char *op)
{
	for (const char *c = op; *c; c++)
		pp_push(group, pp_tt_punct(*c, c[1] != 0));
}

void pp_tt_push_path(PpNode *group, const char *path)
{
	for (;;) {
		if (path[0] == ':' && path[1] == ':') {
			pp_tt_push_op(group, "::");
			path += 2;
			continue;
		}
		const char *e = strstr(path, "::");
		if (!e) {
			pp_push(group, pp_tt_ident(path));
			return;
		}
		pp_push(group, pp_tt_ident(xstrndup(path, (size_t)(e - path))));
		path = e;
	}
}

static bool tt_ident_char(char c) { return c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'); }

static const char *tt_src(PpNode *group, const char *s, char close)
{
	static const char *const ops[] = {"<<=", ">>=", "...", "..=", "::", "->", "=>", "==", "!=", "<=", ">=", "&&", "||",
					  "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "<<", ">>", "..", NULL};
	while (*s) {
		char c = *s;
		if (c == ' ' || c == '\n' || c == '\t') {
			s++;
		} else if (c == close) {
			return s + 1;
		} else if (c == '(' || c == '[' || c == '{') {
			PpNode *g = pp_tt_group(c == '(' ? PP_DELIM_PAREN : c == '[' ? PP_DELIM_BRACKET : PP_DELIM_BRACE);
			s = tt_src(g, s + 1, c == '(' ? ')' : c == '[' ? ']' : '}');
			pp_push(group, g);
		} else if (c == '"') {
			const char *b = s++;
			while (*s && *s != '"')
				s += *s == '\\' && s[1] ? 2 : 1;
			if (*s)
				s++;
			pp_push(group, pp_tt_lit(xstrndup(b, (size_t)(s - b))));
		} else if (c >= '0' && c <= '9') {
			const char *b = s;
			while (tt_ident_char(*s) || (*s == '.' && s[1] >= '0' && s[1] <= '9'))
				s++;
			pp_push(group, pp_tt_lit(xstrndup(b, (size_t)(s - b))));
		} else if (c == '\'' && tt_ident_char(s[1])) {
			pp_push(group, pp_tt_punct('\'', true));
			s++;
		} else if (tt_ident_char(c)) {
			const char *b = s;
			if (c == 'r' && s[1] == '#')
				s += 2;
			while (tt_ident_char(*s))
				s++;
			pp_push(group, pp_tt_ident(xstrndup(b, (size_t)(s - b))));
		} else {
			size_t n = 1;
			for (int i = 0; ops[i]; i++)
				if (!strncmp(s, ops[i], strlen(ops[i]))) {
					n = strlen(ops[i]);
					break;
				}
			for (size_t i = 0; i < n; i++)
				pp_push(group, pp_tt_punct(s[i], i + 1 < n));
			s += n;
		}
	}
	return s;
}

void pp_tt_push_src(PpNode *group, const char *src) { tt_src(group, src, 0); }

/* ---- items ---- */

PpNode *pp_file(void) { return pp_node(PP_FILE); }

static PpNode *vis_item(int kind, int vis, const char *ident)
{
	PpNode *n = mkt(kind, ident, NULL, NULL);
	n->aux = (uint16_t)vis;
	return n;
}

PpNode *pp_item_const(int vis, const char *ident, PpNode *ty, PpNode *e)
{
	PpNode *n = vis_item(PP_ITEM_CONST, vis, ident);
	n->b = ty;
	n->c = e;
	return n;
}

PpNode *pp_item_static(int vis, bool mut, const char *ident, PpNode *ty, PpNode *e)
{
	PpNode *n = vis_item(PP_ITEM_STATIC, vis, ident);
	n->a = ty;
	n->b = e;
	n->flags = mut ? PP_F_MUT : 0;
	return n;
}

PpNode *pp_item_struct(int vis, const char *ident, int fields_kind)
{
	PpNode *n = vis_item(PP_ITEM_STRUCT, vis, ident);
	n->flags = (uint8_t)fields_kind;
	return n;
}

PpNode *pp_field(int vis, const char *ident, PpNode *ty)
{
	PpNode *n = vis_item(PP_FIELD, vis, ident);
	n->a = ty;
	return n;
}

PpNode *pp_item_enum(int vis, const char *ident) { return vis_item(PP_ITEM_ENUM, vis, ident); }

PpNode *pp_variant(const char *ident, int fields_kind, PpNode *discriminant)
{
	PpNode *n = mkt(PP_VARIANT, ident, discriminant, NULL);
	n->flags = (uint8_t)fields_kind;
	return n;
}

PpNode *pp_item_impl(PpNode *trait_path, PpNode *self_ty) { return mk(PP_ITEM_IMPL, trait_path, self_ty, NULL); }

PpNode *pp_item_trait(int vis, const char *ident, PpNode *supertraits)
{
	PpNode *n = vis_item(PP_ITEM_TRAIT, vis, ident);
	n->a = supertraits;
	return n;
}

PpNode *pp_sig(const char *ident, PpNode *ret) { return mkt(PP_SIG, ident, ret, NULL); }

PpNode *pp_receiver(bool ref, bool mut, const char *lifetime)
{
	PpNode *n = mkt(PP_RECEIVER, lifetime, NULL, NULL);
	n->flags = (ref ? PP_F_REF : 0) | (mut ? PP_F_MUT : 0);
	return n;
}

PpNode *pp_item_fn(int vis, PpNode *sig, PpNode *block)
{
	PpNode *n = mk(PP_ITEM_FN, sig, block, NULL);
	n->aux = (uint16_t)vis;
	return n;
}

PpNode *pp_item_type(int vis, const char *ident, PpNode *ty)
{
	PpNode *n = vis_item(PP_ITEM_TYPE, vis, ident);
	n->a = ty;
	return n;
}

PpNode *pp_impl_type(int vis, const char *ident, PpNode *ty)
{
	PpNode *n = vis_item(PP_IMPL_TYPE, vis, ident);
	n->a = ty;
	return n;
}

PpNode *pp_trait_type(const char *ident, PpNode *bounds, PpNode *def)
{
	return mkt(PP_TRAIT_TYPE, ident, bounds, def);
}

PpNode *pp_item_macro(PpNode *mac, const char *ident) { return mkt(PP_ITEM_MACRO, ident, mac, NULL); }

PpNode *pp_item_mod(int vis, const char *ident, bool content)
{
	PpNode *n = vis_item(PP_ITEM_MOD, vis, ident);
	n->flags = content ? PP_F_CONTENT : 0;
	return n;
}

PpNode *pp_item_use(int vis, PpNode *tree, bool leading_colon)
{
	PpNode *n = mk(PP_ITEM_USE, tree, NULL, NULL);
	n->aux = (uint16_t)vis;
	n->flags = leading_colon ? PP_F_LEADING_COLON : 0;
	return n;
}

PpNode *pp_use_path(const char *ident, PpNode *tree) { return mkt(PP_USE_PATH, ident, tree, NULL); }
PpNode *pp_use_name(const char *ident) { return mkt(PP_USE_NAME, ident, NULL, NULL); }

PpNode *pp_use_rename(const char *ident, const char *rename)
{
	return mkt(PP_USE_RENAME, ident, mkt(PP_IDENT, rename, NULL, NULL), NULL);
}

PpNode *pp_use_glob(void) { return pp_node(PP_USE_GLOB); }
PpNode *pp_use_group(void) { return pp_node(PP_USE_GROUP); }

PpNode *pp_use_tree(const char *s)
{
	const char *e = strstr(s, "::");
	if (e)
		return pp_use_path(xstrndup(s, (size_t)(e - s)), pp_use_tree(e + 2));
	if (str_eq(s, "*"))
		return pp_use_glob();
	const char *as = strstr(s, " as ");
	if (as)
		return pp_use_rename(xstrndup(s, (size_t)(as - s)), as + 4);
	return pp_use_name(s);
}

/* ---- generics ---- */

PpNode *pp_generics(void) { return pp_node(PP_GENERICS); }
PpNode *pp_lifetime_param(const char *lifetime) { return mkt(PP_LIFETIME_PARAM, lifetime, NULL, NULL); }

PpNode *pp_type_param(const char *ident, PpNode *bounds, PpNode *def)
{
	return mkt(PP_TYPE_PARAM, ident, bounds, def);
}

PpNode *pp_where(void) { return pp_node(PP_WHERE); }
PpNode *pp_pred_type(PpNode *ty, PpNode *bounds) { return mk(PP_PRED_TYPE, ty, bounds, NULL); }

/* ---- printing ---- */

char *pp_unparse(const PpNode *file)
{
	Pp p;
	pp_init(&p);
	p_file(&p, file);
	return pp_eof(&p);
}

char *pp_unparse_item(const PpNode *item)
{
	PpNode file = {0};
	file.kind = PP_FILE;
	PpNode *one[1] = {(PpNode *)item};
	file.list = (PpList){one, 1, 1};
	return pp_unparse(&file);
}
