/* Unity build of the dprint-plugin-typescript 0.87.1 port; see README.md. Everything is static
 * except the API in dprint_typescript.h. */
#include "dprint_typescript.h"

#include <ctype.h>
#include <string.h>

#include "core/ir.h"
#include "ts/ast.h"

#include "core/print_items.c"
#include "core/printer.c"
#include "core/gen_separated_values.c"
#include "ts/source.c"
#include "ts/build.c"
#include "ts/layout.c"
#include "ts/generate.c"

void dprint_ts_format_module(Node *module, Buf *out)
{
	if (!module || module->kind != N_MODULE)
		fail("dprint: not a module");
	dprint_ids_reset();
	Source *src = layout_module(module);
	Items items = ts_generate(src);
	PrintOptions opts = {120, 2, false, "\n"};
	dprint_print(items, opts, out);
}

/* ---- dprint_ts_reserved_name ---- */

/* Which names make swc_ecma_parser 0.139.0 fail to parse a TypeScript module (strict mode), by
 * position. Only syntax_error!s and the diagnostics that dprint-plugin-typescript's
 * ensure_no_specific_syntax_errors (swc.rs) keeps count; the other diagnostics are ignored.
 *   R_HARD  keyword tokens that are no identifier anywhere (parse_ident: ExpectedIdent; type:
 *           parse_ts_non_array_type has no arm for them)
 *   R_TYPE  a type reference (`let` and `super` are keyword tokens without a type arm; `infer` is
 *           only valid in a conditional type)
 *   R_TYPEOP a type reference, except as the element of an array type (`keyof`, `readonly` and
 *           `unique` start a type operator, which `[]` completes as a tuple)
 *   R_MOD   a type reference followed by `{`, `[` or a name (eat_any_ts_modifier takes it for an
 *           accessibility modifier and then wants a name)
 *   R_EXPR  an expression (not IdentRef: is_reserved_word in a module)
 *   R_BARE  an expression other than the object of a member expression (`super.x` is valid)
 *   R_BIND  a binding name (parse_binding_ident; `enum` makes `const enum`)
 *   R_IMP   an imported binding without `as` (ReservedWordInImport: is_reserved_word)
 *   R_IMPAS an imported binding after `as` */
enum { R_HARD = 1, R_TYPE = 2, R_TYPEOP = 4, R_MOD = 8, R_EXPR = 16, R_BARE = 32, R_BIND = 64, R_IMP = 128, R_IMPAS = 256 };

static const struct {
	const char *word;
	unsigned short flags;
} swc_reserved[] = {
	{"case", R_HARD}, {"catch", R_HARD}, {"class", R_HARD}, {"const", R_HARD}, {"continue", R_HARD},
	{"debugger", R_HARD}, {"default", R_HARD}, {"delete", R_HARD}, {"do", R_HARD}, {"else", R_HARD},
	{"export", R_HARD}, {"extends", R_HARD}, {"finally", R_HARD}, {"for", R_HARD}, {"function", R_HARD},
	{"if", R_HARD}, {"import", R_HARD}, {"in", R_HARD}, {"instanceof", R_HARD}, {"new", R_HARD},
	{"return", R_HARD}, {"switch", R_HARD}, {"throw", R_HARD}, {"try", R_HARD}, {"typeof", R_HARD},
	{"var", R_HARD}, {"while", R_HARD}, {"with", R_HARD},
	{"await", R_EXPR | R_BARE | R_BIND | R_IMP | R_IMPAS},
	{"break", R_EXPR | R_BARE | R_BIND | R_IMP | R_IMPAS},
	{"void", R_EXPR | R_BARE | R_BIND | R_IMP | R_IMPAS},
	{"yield", R_EXPR | R_BARE | R_IMP},
	{"let", R_TYPE | R_IMP},
	{"null", R_BIND | R_IMP | R_IMPAS},
	{"true", R_BIND | R_IMP | R_IMPAS},
	{"false", R_BIND | R_IMP | R_IMPAS},
	{"this", R_IMP},
	{"super", R_TYPE | R_BARE | R_BIND | R_IMP | R_IMPAS},
	{"enum", R_EXPR | R_BARE | R_BIND | R_IMP},
	{"implements", R_EXPR | R_BARE | R_IMP},
	{"interface", R_EXPR | R_BARE | R_IMP},
	{"package", R_EXPR | R_BARE | R_IMP},
	{"private", R_MOD | R_EXPR | R_BARE | R_IMP},
	{"protected", R_MOD | R_EXPR | R_BARE | R_IMP},
	{"public", R_MOD | R_EXPR | R_BARE | R_IMP},
	{"infer", R_TYPE},
	{"keyof", R_TYPEOP},
	{"readonly", R_TYPEOP},
	{"unique", R_TYPEOP},
};

static unsigned swc_reserved_flags(Str s)
{
	for (size_t i = 0; i < sizeof swc_reserved / sizeof swc_reserved[0]; i++)
		if (str_eqc(s, swc_reserved[i].word))
			return swc_reserved[i].flags;
	return 0;
}

/* property-name positions and the names swc's TypeScript parser reads as IdentName (the ids
 * of interfaces, type aliases, enums and namespaces, `export { x }`): any word is allowed there */
static bool is_property_name(const Node *parent, const Node *n)
{
	if (!parent)
		return false;
	switch (parent->kind) {
	case N_MEMBER_EXPR:
	case N_TS_QUALIFIED_NAME:
		return parent->b == n;
	case N_KEY_VALUE_PROP:
	case N_METHOD_PROP:
	case N_GETTER_PROP:
	case N_SETTER_PROP:
	case N_CLASS_METHOD:
	case N_CLASS_PROP:
	case N_TS_PROPERTY_SIGNATURE:
	case N_TS_METHOD_SIGNATURE:
	case N_TS_GETTER_SIGNATURE:
	case N_TS_SETTER_SIGNATURE:
	case N_TS_ENUM_MEMBER:
	case N_KEY_VALUE_PAT_PROP:
	case N_EXPORT_NAMESPACE_SPECIFIER:
	case N_TS_INTERFACE_DECL:
	case N_TS_TYPE_ALIAS_DECL:
	case N_TS_ENUM_DECL:
	case N_TS_MODULE_DECL:
		return parent->a == n;
	case N_EXPORT_NAMED_SPECIFIER:
		return true;
	case N_IMPORT_NAMED_SPECIFIER:
		return parent->b == n;
	default:
		return false;
	}
}

static bool is_last_of(const NodeList *l, const Node *n) { return l->n && l->v[l->n - 1] == n; }

/* Whether the source token after the type reference anc[top] is `{` or `[` (the generated text
 * puts no line break there): the element of an array type, or the end of the return type of a
 * function or getter with a body. A name never follows a type reference in generated code. */
static bool type_followed_by_brace(const Node *const *anc, int top)
{
	const Node *cur = anc[top];
	for (int i = top - 1; i >= 0; cur = anc[i--]) {
		const Node *p = anc[i];
		switch (p->kind) {
		case N_TS_ARRAY_TYPE:
			return true;
		case N_TS_UNION_TYPE:
		case N_TS_INTERSECTION_TYPE:
			if (!is_last_of(&p->list, cur))
				return false;
			break;
		case N_TS_TYPE_ANN:
			break;
		case N_FUNCTION:
			return p->c == cur && p->a;
		case N_GETTER_PROP:
			return p->b == cur && p->c;
		default:
			return false;
		}
	}
	return false;
}

/* Whether swc rejects the identifier anc[top] (with the ancestors anc[0..top-1]) */
static bool swc_rejects(const Node *const *anc, int top)
{
	const Node *n = anc[top];
	const Node *parent = top ? anc[top - 1] : NULL;
	if (is_property_name(parent, n))
		return false;
	unsigned f = swc_reserved_flags(n->text);
	if (!f)
		return false;
	bool hard = f & R_HARD;
	switch (parent ? parent->kind : N_INVALID) {
	case N_TS_TYPE_REF:
	case N_TS_QUALIFIED_NAME: {
		int t = top - 1; /* the type reference */
		while (t > 0 && anc[t]->kind == N_TS_QUALIFIED_NAME)
			t--;
		const Node *tp = t ? anc[t - 1] : NULL;
		if (hard)
			/* `x as const` is a const assertion */
			return !(str_eqc(n->text, "const") && tp && tp->kind == N_TS_AS_EXPR && tp->b == anc[t]);
		if (f & R_TYPE)
			return true;
		if (f & R_TYPEOP)
			return !(tp && tp->kind == N_TS_ARRAY_TYPE);
		if ((f & R_MOD) && parent->kind == N_TS_TYPE_REF && !(parent->b && parent->b->list.n))
			return type_followed_by_brace(anc, top - 1);
		return false;
	}
	case N_TS_TYPE_QUERY: /* parse_ts_entity_name(allow_reserved_words) after `typeof` */
		return str_eqc(n->text, "import");
	case N_TS_EXPR_WITH_TYPE_ARGS:
		/* heritage clause (parse_ident_name): a class's implements list ends at `extends` (then
		 * parse_super_class fails at `{`) and at `implements` (then a second implements clause
		 * follows, which fails at `<`; without type arguments see lay_class) */
		if (str_eqc(n->text, "extends"))
			return true;
		return str_eqc(n->text, "implements") && parent->b && parent->b->list.n && top >= 2 &&
		       anc[top - 2]->kind == N_CLASS;
	case N_IMPORT_NAMED_SPECIFIER:
		return hard || (f & (parent->b ? R_IMPAS : R_IMP));
	case N_IMPORT_DEFAULT_SPECIFIER:
	case N_IMPORT_STAR_AS_SPECIFIER:
		return hard || (f & R_IMPAS);
	case N_BINDING_IDENT:
	case N_FN_DECL:
	case N_CLASS_DECL:
	case N_FN_EXPR:
	case N_CLASS_EXPR:
	case N_TS_TYPE_PARAM:
		return hard || (f & R_BIND);
	case N_MEMBER_EXPR:
		return hard || (f & R_EXPR);
	case N_KEY_VALUE_PROP: /* `{ k: await }` parses (an await expression without an argument) */
		return hard || ((f & R_BARE) && !str_eqc(n->text, "await"));
	default:
		return hard || (f & R_BARE);
	}
}

static const char *reserved_rec(const Node **anc, int top)
{
	const Node *n = anc[top];
	if (n->kind == N_IDENT && swc_rejects(anc, top))
		return xstrndup(n->text.p, n->text.n);
	if (top >= 10000)
		fail("dprint: tree too deep");
	const Node *kids[4] = {n->a, n->b, n->c, n->d};
	for (int i = 0; i < 4; i++) {
		if (!kids[i])
			continue;
		anc[top + 1] = kids[i];
		const char *r = reserved_rec(anc, top + 1);
		if (r)
			return r;
	}
	const NodeList *ls[3] = {&n->list, &n->list2, &n->list3};
	for (int k = 0; k < 3; k++)
		for (int i = 0; i < ls[k]->n; i++) {
			if (!ls[k]->v[i])
				continue;
			anc[top + 1] = ls[k]->v[i];
			const char *r = reserved_rec(anc, top + 1);
			if (r)
				return r;
		}
	return NULL;
}

const char *dprint_ts_reserved_name(const Node *module)
{
	if (!module)
		return NULL;
	static const Node *anc[10001];
	anc[0] = module;
	return reserved_rec(anc, 0);
}

/* ---- dprint_ts_dump ---- */

static COLD void dump_rec(const Node *n, Buf *out, int depth, const char *slot)
{
	for (int i = 0; i < depth; i++)
		buf_adds(out, "  ");
	if (!n) {
		buf_fmt(out, "%s: -\n", slot);
		return;
	}
	buf_fmt(out, "%s: k%d op%d f%x h%x nl%d/%d", slot, n->kind, n->op, (unsigned)n->flags, n->hints, n->nl, n->nl_after_cm);
	if (n->text.n) {
		buf_adds(out, " '");
		buf_add_str(out, n->text);
		buf_adds(out, "'");
	}
	for (int i = 0; i < n->ncm; i++)
		buf_fmt(out, " cm[%d,%d]", n->cm[i].block, n->cm[i].nl);
	for (int i = 0; i < n->ntcm; i++)
		buf_fmt(out, " tcm[%d,%d]", n->tcm[i].block, n->tcm[i].nl);
	buf_addc(out, '\n');
	const Node *kids[4] = {n->a, n->b, n->c, n->d};
	static const char *const names[4] = {"a", "b", "c", "d"};
	for (int i = 0; i < 4; i++)
		if (kids[i])
			dump_rec(kids[i], out, depth + 1, names[i]);
	const NodeList *ls[3] = {&n->list, &n->list2, &n->list3};
	static const char *const lnames[3] = {"l", "l2", "l3"};
	for (int k = 0; k < 3; k++)
		for (int i = 0; i < ls[k]->n; i++)
			dump_rec(ls[k]->v[i], out, depth + 1, lnames[k]);
}

COLD void dprint_ts_dump(const Node *n, Buf *out) { dump_rec(n, out, 0, "root"); }
