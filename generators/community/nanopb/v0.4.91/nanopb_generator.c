/* Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution. */

/* nanopb/generator/nanopb_generator.py of nanopb 0.4.9.1: generation of fields, oneofs,
 * messages and whole .proto files (the classes Names, EncodedSize, ProtoElement, Enum, Field,
 * ExtensionRange, ExtensionField, OneOf, Message, MangleNames, ProtoFile). Python semantics the
 * output depends on are kept: integers are unbounded (__int128 here, overflow is an error),
 * strings are formatted with %-40s (code points), dicts keep insertion order. */

__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;

/* ---- Python helpers ---- */

/* str(int) */
static char *i128_str(i128 v)
{
	char tmp[48];
	int n = 0;
	bool neg = v < 0;
	u128 u = neg ? (u128)0 - (u128)v : (u128)v;
	do {
		tmp[n++] = (char)('0' + (int)(u % 10));
		u /= 10;
	} while (u);
	char *s = xalloc((size_t)n + 2);
	size_t k = 0;
	if (neg)
		s[k++] = '-';
	while (n)
		s[k++] = tmp[--n];
	return s;
}

#define I128_LIMIT ((i128)1 << 120)

static i128 chk(i128 v)
{
	if (v > I128_LIMIT || v < -I128_LIMIT)
		fail("unsupported by protoc-gen-uni: an encoded or struct size beyond 2**120");
	return v;
}

/* a * b, failing beyond the limit instead of overflowing */
static i128 mul_chk(i128 a, i128 b)
{
	i128 m = b < 0 ? -b : b;
	if (m && (a > I128_LIMIT / m || a < -I128_LIMIT / m))
		fail("unsupported by protoc-gen-uni: an encoded or struct size beyond 2**120");
	return a * b;
}

/* Python's floor modulo */
static i128 py_mod(i128 a, i128 b)
{
	i128 r = a % b;
	if (r && ((r < 0) != (b < 0)))
		r += b;
	return r;
}

/* Number of code points */
static size_t ulen(const char *s)
{
	size_t n = 0;
	for (; *s; s++)
		n += ((unsigned char)*s & 0xc0) != 0x80;
	return n;
}

/* '%-Ns' % s */
static void add_ljust(Buf *b, const char *s, size_t w)
{
	buf_adds(b, s);
	for (size_t n = ulen(s); n < w; n++)
		buf_addc(b, ' ');
}

static void fail_non_ascii(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s >= 0x80)
			fail("unsupported by protoc-gen-uni: non-ASCII characters in a name that is case-converted (-C)");
}

/* ---- naming styles (NamingStyle, NamingStyleC) ---- */

static bool c_style;

static bool is_up(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_low(char c) { return c >= 'a' && c <= 'z'; }

/* NamingStyleC.underscore */
static char *underscore(const char *w)
{
	fail_non_ascii(w);
	size_t n = strlen(w);
	Buf a = {0};
	/* re.sub(r"([A-Z]+)([A-Z][a-z])", r'\1_\2', word) */
	for (size_t i = 0; i < n;) {
		if (!is_up(w[i])) {
			buf_addc(&a, w[i++]);
			continue;
		}
		size_t j = i;
		while (j < n && is_up(w[j]))
			j++;
		if (j - i >= 2 && j < n && is_low(w[j])) {
			buf_add(&a, w + i, j - 1 - i);
			buf_addc(&a, '_');
			buf_addc(&a, w[j - 1]);
			buf_addc(&a, w[j]);
			i = j + 1;
		} else {
			buf_add(&a, w + i, j - i);
			i = j;
		}
	}
	const char *s = buf_str(&a);
	n = a.n;
	Buf b = {0};
	/* re.sub(r"([a-z\d])([A-Z])", r'\1_\2', word), replace("-", "_"), lower() */
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		bool pair = (is_low(c) || (c >= '0' && c <= '9')) && i + 1 < n && is_up(s[i + 1]);
		buf_addc(&b, c == '-' ? '_' : is_up(c) ? (char)(c + 32) : c);
		if (pair) {
			buf_addc(&b, '_');
			buf_addc(&b, (char)(s[i + 1] + 32));
			i++;
		}
	}
	return (char *)buf_str(&b);
}

static char *upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (is_low(*p))
			*p = (char)(*p - 32);
	return r;
}

static const char *ns_enum_name(const char *n) { return c_style ? underscore(n) : xcat("_", n); }
static const char *ns_struct_name(const char *n) { return c_style ? underscore(n) : xcat("_", n); }
static const char *ns_type_name(const char *n) { return c_style ? xcat(underscore(n), "_t") : n; }
static const char *ns_define_name(const char *n) { return c_style ? upper(underscore(n)) : n; }
static const char *ns_var_name(const char *n) { return c_style ? underscore(n) : n; }
static const char *ns_enum_entry(const char *n) { return c_style ? upper(underscore(n)) : n; }
static const char *ns_func_name(const char *n) { return c_style ? underscore(n) : n; }
static const char *ns_bytes_type(const char *sn, const char *n)
{
	return c_style ? xcat(underscore(sn), "_", underscore(n), "_t") : xcat(sn, "_", n, "_t");
}

/* ---- names_from_type_name, varint_max_size ---- */

static Names names_from_type_name(const char *type_name)
{
	if (!type_name[0])
		nb_raise(S_ABS_INDEX, "IndexError: string index out of range\n");
	if (type_name[0] != '.')
		nb_raise(S_ABS_NAME, "NotImplementedError: Lookup of non-absolute type names is not supported\n");
	return names_split(type_name + 1, '.');
}

static int varint_max_size(i128 v)
{
	if (v < 0)
		v = ((i128)1 << 64) - v;
	for (int i = 1; i < 11; i++)
		if ((v >> (i * 7)) == 0)
			return i;
	nb_raise(S_VARINT, xcat("ValueError: Value too large for varint: ", i128_str(v), "\n"));
}

/* ---- EncodedSize ---- */

/* An immutable list of strings, concatenated in O(1). */
typedef struct SList SList;
struct SList {
	size_t n;
	const char *s; /* a leaf */
	const SList *l, *r;
};

static size_t sl_len(const SList *a) { return a ? a->n : 0; }

static const SList *sl_leaf(const char *s)
{
	SList *l = xalloc(sizeof *l);
	l->n = 1;
	l->s = s;
	return l;
}

static const SList *sl_cat(const SList *a, const SList *b)
{
	if (!sl_len(a))
		return b;
	if (!sl_len(b))
		return a;
	if (a->n + b->n > 4000000)
		fail("unsupported by protoc-gen-uni: an encoded size with more than 4000000 symbols");
	SList *l = xalloc(sizeof *l);
	l->n = a->n + b->n;
	l->l = a;
	l->r = b;
	return l;
}

/* The elements in order (iteratively: lists can be deep). */
static const char **sl_items(const SList *a)
{
	size_t n = sl_len(a), k = 0;
	const char **v = xalloc(sizeof *v * (n + 1));
	if (!n)
		return v;
	const SList **stack = xalloc(sizeof *stack * (n + 1));
	size_t sp = 0;
	stack[sp++] = a;
	while (sp) {
		const SList *x = stack[--sp];
		if (x->s) {
			v[k++] = x->s;
			continue;
		}
		stack[sp++] = x->r;
		stack[sp++] = x->l;
	}
	return v;
}

typedef struct {
	i128 value;
	const SList *symbols, *declarations, *required_defines;
} ESize;

static ESize *es_new(i128 v)
{
	ESize *e = xalloc(sizeof *e);
	e->value = chk(v);
	return e;
}

/* EncodedSize(Names or str) */
static ESize *es_sym(const char *s)
{
	ESize *e = es_new(0);
	e->symbols = e->required_defines = sl_leaf(s);
	return e;
}

static ESize *es_add_int(const ESize *a, i128 v)
{
	ESize *e = xalloc(sizeof *e);
	*e = *a;
	e->value = chk(a->value + v);
	return e;
}

static ESize *es_add(const ESize *a, const ESize *b)
{
	ESize *e = es_new(a->value + b->value);
	e->symbols = sl_cat(a->symbols, b->symbols);
	e->declarations = sl_cat(a->declarations, b->declarations);
	e->required_defines = sl_cat(a->required_defines, b->required_defines);
	return e;
}

static ESize *es_mul(const ESize *a, i128 m)
{
	ESize *e = xalloc(sizeof *e);
	*e = *a;
	e->value = mul_chk(a->value, m);
	e->symbols = NULL;
	size_t n = sl_len(a->symbols);
	if (n) {
		const char **v = sl_items(a->symbols);
		const char *prefix = xcat(i128_str(m), "*");
		const SList *r = NULL;
		/* a balanced list */
		const SList **leaves = xalloc(sizeof *leaves * n);
		for (size_t i = 0; i < n; i++)
			leaves[i] = sl_leaf(xcat(prefix, v[i]));
		for (size_t w = n; w > 1; w = (w + 1) / 2)
			for (size_t i = 0; i < w; i += 2)
				leaves[i / 2] = i + 1 < w ? sl_cat(leaves[i], leaves[i + 1]) : leaves[i];
		r = leaves[0];
		e->symbols = r;
	}
	return e;
}

static char *es_str(const ESize *e)
{
	if (!sl_len(e->symbols))
		return i128_str(e->value);
	Buf b = {0};
	buf_fmt(&b, "(%s + ", i128_str(e->value));
	const char **v = sl_items(e->symbols);
	for (size_t i = 0; i < e->symbols->n; i++)
		buf_fmt(&b, "%s%s", i ? " + " : "", v[i]);
	buf_addc(&b, ')');
	return (char *)buf_str(&b);
}

static i128 es_upperlimit(const ESize *e) { return sl_len(e->symbols) ? (((i128)1 << 32) - 1) : e->value; }

/* ---- ProtoElement ---- */

typedef struct {
	const char *path; /* the element path as a key, e.g. "4,0" */
	const Map *comments;
} ProtoElement;

static const char *path_add(const char *path, int a, int b)
{
	return *path ? xfmt("%s,%d,%d", path, a, b) : xfmt("%d,%d", a, b);
}

/* format_comment */
static void add_comment(Buf *out, const char *c)
{
	Str s = py_strip(str_c(c));
	Buf a = {0};
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] == '/' && i + 1 < s.n && s.p[i + 1] == '*') {
			buf_adds(&a, "/ *");
			i++;
		} else {
			buf_addc(&a, s.p[i]);
		}
	}
	buf_adds(out, "/* ");
	for (size_t i = 0; i < a.n; i++) {
		if (a.p[i] == '*' && i + 1 < a.n && a.p[i + 1] == '/') {
			buf_adds(out, "* /");
			i++;
		} else {
			buf_addc(out, a.p[i]);
		}
	}
	buf_adds(out, " */");
}

/* get_comments: leading and trailing comment ("" if none) of the element or a member. */
static void get_comments(const ProtoElement *pe, const char *member, bool leading_indent, const char **lead,
	const char **trail)
{
	*lead = *trail = "";
	if (!pe->comments)
		return;
	const char *path = !*member ? pe->path : !*pe->path ? member : xcat(pe->path, ",", member);
	const Location *loc = map_get(pe->comments, path);
	if (!loc)
		return;
	if (*loc->leading) {
		Buf b = {0};
		if (leading_indent)
			buf_adds(&b, "    ");
		add_comment(&b, loc->leading);
		*lead = buf_str(&b);
	}
	if (*loc->trailing) {
		Buf b = {0};
		add_comment(&b, loc->trailing);
		*trail = buf_str(&b);
	}
}

/* ---- the classes ---- */

typedef struct ProtoFile ProtoFile;
typedef struct Message Message;
typedef struct Enum Enum;
typedef struct Field Field;

enum { KIND_ENUM = 1, KIND_MESSAGE };

struct Enum {
	int kind;
	ProtoElement pe;
	NbOpts *options;
	Names names;
	int n;
	Names *vnames;
	int32_t *vnums;
	Names *longnames;
	bool packed;
	ProtoFile *protofile;
};

/* pbtype, allocation and rules values */
enum {
	PB_BOOL, PB_DOUBLE, PB_FIXED32, PB_FIXED64, PB_FLOAT, PB_INT32, PB_INT64, PB_SFIXED32, PB_SFIXED64,
	PB_SINT32, PB_SINT64, PB_UINT32, PB_UINT64, PB_ENUM, PB_UENUM, PB_STRING, PB_FIXED_LENGTH_BYTES,
	PB_BYTES, PB_MESSAGE, PB_MSG_W_CB, PB_EXTENSION, PB_ONEOF,
};
static const char *const pbtype_names[] = {
	"BOOL", "DOUBLE", "FIXED32", "FIXED64", "FLOAT", "INT32", "INT64", "SFIXED32", "SFIXED64",
	"SINT32", "SINT64", "UINT32", "UINT64", "ENUM", "UENUM", "STRING", "FIXED_LENGTH_BYTES",
	"BYTES", "MESSAGE", "MSG_W_CB", "EXTENSION", "oneof",
};
enum { AL_STATIC, AL_POINTER, AL_CALLBACK, AL_ONEOF };
static const char *const alloc_names[] = {"STATIC", "POINTER", "CALLBACK", "ONEOF"};
enum { R_REPEATED, R_FIXARRAY, R_REQUIRED, R_OPTIONAL, R_SINGULAR, R_ONEOF };
static const char *const rules_names[] = {"REPEATED", "FIXARRAY", "REQUIRED", "OPTIONAL", "SINGULAR", "ONEOF"};

static bool is_msg_type(int pbtype) { return pbtype == PB_MESSAGE || pbtype == PB_MSG_W_CB; }
static bool is_enum_type(int pbtype) { return pbtype == PB_ENUM || pbtype == PB_UENUM; }

enum { FK_FIELD, FK_EXTRANGE, FK_EXTFIELD, FK_ONEOF };

struct Field {
	int fk;
	ProtoElement pe;
	int64_t tag;
	Names struct_name;
	const char *union_name;
	const char *name;
	PyVal deflt;
	bool has_max_size, has_max_count, has_enc_size, has_data_item_size;
	int64_t max_size, max_count, data_item_size;
	i128 enc_size;
	const char *array_decl;
	PyVal ctype;
	const char *callback_datatype;
	bool math_include_required, default_has, anonymous;
	const char *initializer; /* NULL: None */
	int rules, allocation, pbtype;
	Names submsgname;
	/* ExtensionField */
	Names fullname;
	bool skip;
	Message *msg;
	/* OneOf */
	Vec fields;
	bool has_msg_cb, sort_by_tag, in_fields;
	/* names_key of the last ctype and default looked up by add_dependency */
	Names key_ctype_of, key_deflt_of;
	const char *key_ctype, *key_deflt;
};

struct Message {
	int kind;
	ProtoElement pe;
	Names name;
	Vec fields;
	MsgDesc *desc; /* the copy parse() made; NULL for an extension's message */
	bool math_include_required, packed, has_msgid;
	int64_t descriptorsize;
	uint32_t msgid;
	const char *callback_function;
	ProtoFile *protofile;
	/* memoized sizes (per ProtoFile.dependencies) */
	int memo_gen_d, memo_gen_e;
	i128 memo_d;
	const ESize *memo_e;
	bool memo_e_none;
	const char *memo_log_d;
	bool discarded;
	/* check_recursive: get_dependencies() (once), and the state in check number chk_gen */
	Vec chk_deps;
	bool chk_have;
	int chk_gen, chk_state, chk_height;
	long chk_work;
};

/* ---- dependencies: ProtoFile.dependencies minus the struct names removed on the way down ---- */

typedef struct {
	const Map *base;
	const char *removed[512];
	int nremoved;
	bool touched; /* a lookup hit a removed name */
	int depth;
	long work;
} Deps;

static void *deps_get(Deps *d, const char *key)
{
	void *v = map_get(d->base, key);
	if (!v)
		return NULL;
	for (int i = 0; i < d->nremoved; i++)
		if (str_eq(d->removed[i], key)) {
			d->touched = true;
			return NULL;
		}
	return v;
}

static int deps_gen;

static void deps_enter(Deps *d, const char *removed)
{
	if (d->nremoved >= 300 || ++d->work > 50000000)
		fail("unsupported by protoc-gen-uni: messages nested too deeply (upstream: RecursionError)");
	d->removed[d->nremoved++] = removed;
}

/* ---- Enum ---- */

static Enum *enum_new(Names names, const EnumDesc *desc, NbOpts *o, const char *path, const Map *comments)
{
	Enum *e = xalloc(sizeof *e);
	e->kind = KIND_ENUM;
	e->pe = (ProtoElement){path, comments};
	e->options = o;
	e->names = names;
	Names base = names_make(names.p, names.n ? names.n - 1 : 0);
	e->n = desc->n;
	e->vnames = xalloc(sizeof *e->vnames * (size_t)(e->n + 1));
	e->longnames = xalloc(sizeof *e->longnames * (size_t)(e->n + 1));
	e->vnums = desc->vnums;
	for (int i = 0; i < e->n; i++) {
		e->vnames[i] = names_add(opt(o, NO_LONG_NAMES) ? names : base, desc->vnames[i]);
		e->longnames[i] = names_add(names, desc->vnames[i]);
	}
	e->packed = opt(o, NO_PACKED_ENUM) != 0;
	return e;
}

static bool enum_has_negative(const Enum *e)
{
	for (int i = 0; i < e->n; i++)
		if (e->vnums[i] < 0)
			return true;
	return false;
}

static const char *intsize_ctype(int64_t is)
{
	switch (is) {
	case 8: return "uint8_t";
	case 16: return "uint16_t";
	case 32: return "uint32_t";
	case 64: return "uint64_t";
	}
	return NULL;
}

static void enum_str(Buf *b, const Enum *e)
{
	const char *lead, *trail;
	get_comments(&e->pe, "", false, &lead, &trail);
	if (*lead)
		buf_fmt(b, "%s\n", lead);
	buf_fmt(b, "typedef enum %s", ns_enum_name(names_str(e->names)));
	const char *ct = intsize_ctype(opt(e->options, NO_ENUM_INTSIZE));
	if (ct)
		buf_fmt(b, ": %s", ct);
	buf_adds(b, " {");
	if (*trail)
		buf_fmt(b, " %s", trail);
	buf_adds(b, "\n");
	for (int i = 0; i < e->n; i++) {
		get_comments(&e->pe, xfmt("2,%d", i), true, &lead, &trail);
		if (*lead)
			buf_fmt(b, "%s\n", lead);
		buf_fmt(b, "    %s = %d%s", ns_enum_entry(names_str(e->vnames[i])), (int)e->vnums[i],
			i == e->n - 1 ? "" : ",");
		if (*trail)
			buf_fmt(b, " %s", trail);
		if (i < e->n - 1)
			buf_addc(b, '\n');
	}
	buf_adds(b, "\n}");
	if (e->packed)
		buf_adds(b, " pb_packed");
	buf_fmt(b, " %s;", ns_type_name(names_str(e->names)));
}

static int enum_encoded_size(const Enum *e)
{
	if (!e->n)
		fail("ValueError: max() iterable argument is empty");
	int m = 0;
	for (int i = 0; i < e->n; i++) {
		int s = varint_max_size(e->vnums[i]);
		if (s > m)
			m = s;
	}
	return m;
}

static PyVal mangle_unmangle(ProtoFile *pf, Names names);
static void reverse_put(ProtoFile *pf, const char *key, PyVal v);

/* A stable merge sort of pointers. */
static void stable_sort(void **v, int n, int (*cmp)(const void *, const void *))
{
	if (n < 2)
		return;
	void **tmp = xalloc(sizeof *tmp * (size_t)n);
	for (int w = 1; w < n; w *= 2) {
		for (int lo = 0; lo < n; lo += 2 * w) {
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				tmp[k++] = cmp(v[j], v[i]) < 0 ? v[j++] : v[i++];
			while (i < mid)
				tmp[k++] = v[i++];
			while (j < hi)
				tmp[k++] = v[j++];
		}
		memcpy(v, tmp, sizeof *v * (size_t)n);
	}
}

typedef struct {
	Names name;
	const char *s;
	int32_t num;
} EnumVal;

static int cmp_enumval(const void *a, const void *b)
{
	const EnumVal *x = a, *y = b;
	if (x->num != y->num)
		return x->num < y->num ? -1 : 1;
	return py_str_cmp(str_c(x->s), str_c(y->s));
}

static void enum_aux_defines(Buf *b, Enum *e)
{
	EnumVal **sv = xalloc(sizeof *sv * (size_t)(e->n + 1));
	for (int i = 0; i < e->n; i++) {
		sv[i] = xalloc(sizeof **sv);
		*sv[i] = (EnumVal){e->vnames[i], names_str(e->vnames[i]), e->vnums[i]};
	}
	stable_sort((void **)sv, e->n, cmp_enumval);
	if (!e->n)
		nb_raise(S_AUX_INDEX, "IndexError: list index out of range\n");
	ProtoFile *pf = e->protofile;
	PyVal un = mangle_unmangle(pf, e->names);
	const char *ns = names_str(e->names);
	static const char *const sfx[] = {"_MIN", "_MAX", "_ARRAYSIZE"};
	for (int k = 0; k < 3; k++) {
		const char *id = ns_define_name(xcat("_", ns, sfx[k]));
		const char *entry = ns_enum_entry(sv[k ? e->n - 1 : 0]->s);
		if (k < 2)
			buf_fmt(b, "#define %s %s\n", id, entry);
		else
			buf_fmt(b, "#define %s ((%s)(%s+1))\n", id, ns_type_name(ns), entry);
		if (pv_truthy(un))
			reverse_put(pf, id, pv_str(ns_define_name(xcat("_", pv_text(un), sfx[k]))));
	}
	if (!opt(e->options, NO_LONG_NAMES))
		for (int i = 0; i < e->n; i++)
			buf_fmt(b, "#define %s %s\n", ns_define_name(names_str(e->longnames[i])),
				ns_enum_entry(names_str(e->vnames[i])));
	if (opt(e->options, NO_ENUM_TO_STRING))
		buf_fmt(b, "const char *%s(%s v);\n", ns_func_name(xcat(ns, "_name")), ns_type_name(ns));
	if (opt(e->options, NO_ENUM_VALIDATE))
		buf_fmt(b, "bool %s(%s v);\n", ns_func_name(xcat(ns, "_valid")), ns_type_name(ns));
}

static void enum_to_string_definition(Buf *b, const Enum *e)
{
	if (!opt(e->options, NO_ENUM_TO_STRING))
		return;
	const char *ns = names_str(e->names);
	buf_fmt(b, "const char *%s(%s v) {\n", ns_func_name(xcat(ns, "_name")), ns_type_name(ns));
	buf_adds(b, "    switch (v) {\n");
	size_t skip = ulen(ns) + 1;
	for (int i = 0; i < e->n; i++) {
		/* str(strname)[len(str(self.names)) + 1:] */
		const char *s = names_str(e->longnames[i]);
		for (size_t k = 0; k < skip && *s; k++) {
			s++;
			while (((unsigned char)*s & 0xc0) == 0x80)
				s++;
		}
		buf_fmt(b, "        case %s: return \"%s\";\n", ns_enum_entry(names_str(e->vnames[i])), ns_enum_entry(s));
	}
	buf_adds(b, "    }\n    return \"unknown\";\n}\n");
}

static void enum_validate(Buf *b, const Enum *e)
{
	if (!opt(e->options, NO_ENUM_VALIDATE))
		return;
	const char *ns = names_str(e->names);
	buf_fmt(b, "bool %s(%s v) {\n", ns_func_name(xcat(ns, "_valid")), ns_type_name(ns));
	buf_adds(b, "    switch (v) {\n");
	for (int i = 0; i < e->n; i++)
		buf_fmt(b, "        case %s: return true;\n", ns_enum_entry(names_str(e->vnames[i])));
	buf_adds(b, "    }\n    return false;\n}\n");
}

/* ---- Field ---- */

typedef struct {
	const char *ctype;
	int pbtype, enc_size, data_size;
} DataType;

/* datatypes[type] and datatypes[(type, int_size)] */
static bool datatype(int type, int64_t int_size, DataType *out)
{
	static const DataType base[19] = {
		[PT_BOOL] = {"bool", PB_BOOL, 1, 4}, [PT_DOUBLE] = {"double", PB_DOUBLE, 8, 8},
		[PT_FIXED32] = {"uint32_t", PB_FIXED32, 4, 4}, [PT_FIXED64] = {"uint64_t", PB_FIXED64, 8, 8},
		[PT_FLOAT] = {"float", PB_FLOAT, 4, 4}, [PT_INT32] = {"int32_t", PB_INT32, 10, 4},
		[PT_INT64] = {"int64_t", PB_INT64, 10, 8}, [PT_SFIXED32] = {"int32_t", PB_SFIXED32, 4, 4},
		[PT_SFIXED64] = {"int64_t", PB_SFIXED64, 8, 8}, [PT_SINT32] = {"int32_t", PB_SINT32, 5, 4},
		[PT_SINT64] = {"int64_t", PB_SINT64, 10, 8}, [PT_UINT32] = {"uint32_t", PB_UINT32, 5, 4},
		[PT_UINT64] = {"uint64_t", PB_UINT64, 10, 8},
	};
	if (type < 1 || type > 18 || !base[type].ctype)
		return false;
	*out = base[type];
	int k = int_size == 8 ? 0 : int_size == 16 ? 1 : int_size == 32 ? 2 : int_size == 64 ? 3 : -1;
	if (k < 0)
		return true;
	static const char *const sct[4] = {"int8_t", "int16_t", "int32_t", "int64_t"};
	static const char *const uct[4] = {"uint8_t", "uint16_t", "uint32_t", "uint64_t"};
	static const int dsz[4] = {1, 2, 4, 8};
	static const int sint_enc[4] = {2, 3, 5, 10}, uint_enc[4] = {2, 3, 5, 10};
	switch (type) {
	case PT_INT32: *out = (DataType){sct[k], PB_INT32, 10, dsz[k]}; break;
	case PT_INT64: *out = (DataType){sct[k], PB_INT64, 10, dsz[k]}; break;
	case PT_SINT32: *out = (DataType){sct[k], PB_SINT32, sint_enc[k], dsz[k]}; break;
	case PT_SINT64: *out = (DataType){sct[k], PB_SINT64, sint_enc[k], dsz[k]}; break;
	case PT_UINT32: *out = (DataType){uct[k], PB_UINT32, uint_enc[k], dsz[k]}; break;
	case PT_UINT64: *out = (DataType){uct[k], PB_UINT64, uint_enc[k], dsz[k]}; break;
	}
	return true;
}

static const char *ctype_type_name(PyVal ct)
{
	return ct.kind == PV_NAMES ? ns_type_name(names_str(ct.nm)) : ct.s;
}

static void field_init(Field *f, Names struct_name, FieldDesc *desc, NbOpts *fo, const char *path, const Map *comments)
{
	f->fk = FK_FIELD;
	f->pe = (ProtoElement){path, comments};
	f->tag = desc->number;
	f->struct_name = struct_name;
	f->name = desc->name;
	f->array_decl = "";
	f->callback_datatype = fo->s[NO_CALLBACK_DATATYPE];
	f->sort_by_tag = opt(fo, NO_SORT_BY_TAG) != 0;
	if (opt(fo, NO_TYPE) == FT_INLINE) {
		opts_set(fo, NO_TYPE, FT_STATIC);
		opts_set(fo, NO_FIXED_LENGTH, 1);
	}
	if (opts_has(fo, NO_MAX_SIZE)) {
		f->has_max_size = true;
		f->max_size = opt(fo, NO_MAX_SIZE);
	}
	f->initializer = opts_has(fo, NO_INITIALIZER) ? fo->s[NO_INITIALIZER] : NULL;
	f->default_has = opt(fo, NO_DEFAULT_HAS) != 0;
	if (desc->type == PT_STRING && opts_has(fo, NO_MAX_LENGTH)) {
		f->has_max_size = true;
		f->max_size = opt(fo, NO_MAX_LENGTH) + 1;
	}
	if (opts_has(fo, NO_MAX_COUNT)) {
		f->has_max_count = true;
		f->max_count = opt(fo, NO_MAX_COUNT);
	}
	if (desc->has_default_value)
		f->deflt = pv_str(desc->default_value);
	if (opts_has(fo, NO_LABEL_OVERRIDE))
		desc->label = (int32_t)opt(fo, NO_LABEL_OVERRIDE);
	bool can_be_static = true;
	if (desc->label == LABEL_REPEATED) {
		f->rules = R_REPEATED;
		if (!f->has_max_count) {
			can_be_static = false;
		} else {
			f->array_decl = xfmt("[%lld]", (long long)f->max_count);
			if (opt(fo, NO_FIXED_COUNT))
				f->rules = R_FIXARRAY;
		}
	} else if (desc->label == LABEL_REQUIRED) {
		f->rules = R_REQUIRED;
	} else if (opt(fo, NO_PROTO3)) {
		if (desc->type == PT_MESSAGE && !opt(fo, NO_PROTO3_SINGULAR_MSGS))
			f->rules = R_OPTIONAL;
		else if (desc->proto3_optional)
			f->rules = R_OPTIONAL;
		else
			f->rules = R_SINGULAR;
	} else {
		f->rules = R_OPTIONAL; /* LABEL_OPTIONAL: the label is a closed enum */
	}
	if ((desc->type == PT_STRING || desc->type == PT_BYTES) && !f->has_max_size)
		can_be_static = false;
	if (opt(fo, NO_TYPE) == FT_DEFAULT)
		opts_set(fo, NO_TYPE, can_be_static ? FT_STATIC : opt(fo, NO_FALLBACK_TYPE));
	if (opt(fo, NO_TYPE) == FT_STATIC && !can_be_static)
		nb_raise(S_STATIC, xfmt("Exception: Field '%s' is defined as static, but max_size or max_count is not given.\n",
			f->name));
	if (opt(fo, NO_FIXED_COUNT) && !f->has_max_count)
		nb_raise(S_FIXCOUNT, xfmt("Exception: Field '%s' is defined as fixed count, but max_count is not given.\n",
			f->name));
	switch (opt(fo, NO_TYPE)) {
	case FT_STATIC: f->allocation = AL_STATIC; break;
	case FT_POINTER: f->allocation = AL_POINTER; break;
	case FT_CALLBACK: f->allocation = AL_CALLBACK; break;
	default: nb_raise(S_NOTIMPL_ALLOC, xfmt("NotImplementedError: %d\n", (int)opt(fo, NO_TYPE)));
	}
	if (opts_has(fo, NO_TYPE_OVERRIDE))
		desc->type = (int32_t)opt(fo, NO_TYPE_OVERRIDE);
	DataType dt;
	if (datatype(desc->type, opt(fo, NO_INT_SIZE), &dt)) {
		f->ctype = pv_str(dt.ctype);
		f->pbtype = dt.pbtype;
		f->has_enc_size = f->has_data_item_size = true;
		f->enc_size = dt.enc_size;
		f->data_item_size = dt.data_size;
	} else if (desc->type == PT_ENUM) {
		f->pbtype = PB_ENUM;
		f->has_data_item_size = true;
		f->data_item_size = 4;
		TB_PUSH(F_FIELD_ENUM_NAME);
		f->ctype = pv_names(names_from_type_name(desc->type_name));
		TB_POP();
		if (f->deflt.kind != PV_NONE)
			f->deflt = pv_names(names_add(f->ctype.nm, f->deflt.s));
	} else if (desc->type == PT_STRING) {
		f->pbtype = PB_STRING;
		f->ctype = pv_str("char");
		if (f->allocation == AL_STATIC) {
			if (!f->has_max_size)
				nb_raise(S_ARRAY_DECL, "TypeError: %d format: a real number is required, not NoneType\n");
			f->array_decl = xfmt("%s[%lld]", f->array_decl, (long long)f->max_size);
			f->has_enc_size = true;
			f->enc_size = (i128)varint_max_size(f->max_size) + f->max_size - 1;
		}
	} else if (desc->type == PT_BYTES) {
		if (opt(fo, NO_FIXED_LENGTH)) {
			f->pbtype = PB_FIXED_LENGTH_BYTES;
			if (!f->has_max_size)
				nb_raise(S_FIXLEN, xfmt("Exception: Field '%s' is defined as fixed length, but max_size is not given.\n",
					f->name));
			f->has_enc_size = true;
			f->enc_size = (i128)varint_max_size(f->max_size) + f->max_size;
			f->ctype = pv_str("pb_byte_t");
			f->array_decl = xfmt("%s[%lld]", f->array_decl, (long long)f->max_size);
		} else {
			f->pbtype = PB_BYTES;
			f->ctype = pv_str("pb_bytes_array_t");
			if (f->allocation == AL_STATIC) {
				if (!f->has_max_size)
					nb_raise(S_BYTES_NONE, "TypeError: '<' not supported between instances of 'NoneType' and 'int'\n");
				f->ctype = pv_str(ns_bytes_type(names_str(struct_name), f->name));
				f->has_enc_size = true;
				f->enc_size = (i128)varint_max_size(f->max_size) + f->max_size;
			}
		}
	} else if (desc->type == PT_MESSAGE) {
		f->pbtype = PB_MESSAGE;
		TB_PUSH(F_FIELD_MSG_NAME);
		f->submsgname = names_from_type_name(desc->type_name);
		TB_POP();
		f->ctype = pv_names(f->submsgname);
		if (opt(fo, NO_SUBMSG_CALLBACK) && f->allocation == AL_STATIC)
			f->pbtype = PB_MSG_W_CB;
	} else {
		nb_raise(S_NOTIMPL_TYPE, xfmt("NotImplementedError: %d\n", (int)desc->type));
	}
	if (pv_truthy(f->deflt) && (f->pbtype == PB_FLOAT || f->pbtype == PB_DOUBLE) &&
		(strstr(f->deflt.s, "inf") || strstr(f->deflt.s, "nan")))
		f->math_include_required = true;
}

static const char *field_var_name(const Field *f) { return ns_var_name(f->name); }

static void field_str(Buf *out, const Field *f);

static void oneof_str(Buf *b, const Field *o)
{
	if (!o->fields.n)
		return;
	const char *vn = ns_var_name(o->name);
	if (o->has_msg_cb)
		buf_fmt(b, "    pb_callback_t cb_%s;\n", vn);
	buf_fmt(b, "    pb_size_t which_%s;\n", vn);
	buf_adds(b, "    union {\n");
	for (int i = 0; i < o->fields.n; i++) {
		Buf fs = {0};
		field_str(&fs, VEC_AT(o->fields, Field, i));
		buf_adds(b, "    ");
		for (size_t k = 0; k < fs.n; k++) {
			buf_addc(b, fs.p[k]);
			if (fs.p[k] == '\n')
				buf_adds(b, "    ");
		}
		buf_addc(b, '\n');
	}
	if (o->anonymous)
		buf_adds(b, "    };");
	else
		buf_fmt(b, "    } %s;", vn);
}

static void field_str(Buf *out, const Field *f)
{
	if (f->fk == FK_ONEOF) {
		oneof_str(out, f);
		return;
	}
	if (f->fk == FK_EXTRANGE) {
		buf_adds(out, "    pb_extension_t *extensions;");
		return;
	}
	Buf b = {0};
	const char *vn = field_var_name(f);
	const char *tn = ctype_type_name(f->ctype);
	bool strbytes = f->pbtype == PB_STRING || f->pbtype == PB_BYTES;
	if (f->allocation == AL_POINTER) {
		if (f->rules == R_REPEATED) {
			if (f->pbtype == PB_MSG_W_CB)
				buf_fmt(&b, "    pb_callback_t cb_%s;\n", vn);
			buf_fmt(&b, "    pb_size_t %s_count;\n", vn);
		}
		if (f->rules == R_FIXARRAY && strbytes)
			buf_fmt(&b, "    %s* (*%s)%s;", tn, vn, f->array_decl);
		else if (f->pbtype == PB_FIXED_LENGTH_BYTES || f->rules == R_FIXARRAY)
			buf_fmt(&b, "    %s (*%s)%s;", tn, vn, f->array_decl);
		else if (f->rules == R_REPEATED && strbytes)
			buf_fmt(&b, "    %s **%s;", tn, vn);
		else if (is_msg_type(f->pbtype))
			buf_fmt(&b, "    struct %s *%s;", ns_struct_name(pv_text(f->ctype)), vn);
		else
			buf_fmt(&b, "    %s *%s;", tn, vn);
	} else if (f->allocation == AL_CALLBACK) {
		buf_fmt(&b, "    %s %s;", f->callback_datatype, vn);
	} else {
		if (f->pbtype == PB_MSG_W_CB && (f->rules == R_OPTIONAL || f->rules == R_REPEATED))
			buf_fmt(&b, "    pb_callback_t cb_%s;\n", vn);
		if (f->rules == R_OPTIONAL)
			buf_fmt(&b, "    bool has_%s;\n", vn);
		else if (f->rules == R_REPEATED)
			buf_fmt(&b, "    pb_size_t %s_count;\n", vn);
		buf_fmt(&b, "    %s %s%s;", tn, vn, f->array_decl);
	}
	const char *lead, *trail;
	get_comments(&f->pe, "", true, &lead, &trail);
	if (*lead)
		buf_fmt(out, "%s\n", lead);
	buf_add(out, b.p, b.n);
	if (*trail)
		buf_fmt(out, " %s", trail);
}

static void field_types(Buf *b, const Field *f)
{
	if (f->fk == FK_ONEOF) {
		for (int i = 0; i < f->fields.n; i++)
			field_types(b, VEC_AT(f->fields, Field, i));
		return;
	}
	if (f->fk == FK_EXTRANGE)
		return;
	if (f->pbtype == PB_BYTES && f->allocation == AL_STATIC)
		buf_fmt(b, "typedef PB_BYTES_ARRAY_T(%lld) %s;\n", (long long)f->max_size, ns_var_name(pv_text(f->ctype)));
}

/* get_dependencies: appends type names to v. */
static void field_deps(const Field *f, Vec *v)
{
	if (f->fk == FK_ONEOF) {
		for (int i = 0; i < f->fields.n; i++)
			field_deps(VEC_AT(f->fields, Field, i), v);
		return;
	}
	if (f->allocation == AL_STATIC || (f->allocation == AL_POINTER && f->rules == R_FIXARRAY))
		vec_push(v, (void *)pv_text(f->ctype));
}

static bool field_has_callbacks(const Field *f)
{
	if (f->fk == FK_ONEOF) {
		for (int i = 0; i < f->fields.n; i++)
			if (field_has_callbacks(VEC_AT(f->fields, Field, i)))
				return true;
		return false;
	}
	return f->allocation == AL_CALLBACK;
}

static bool field_requires_custom_callback(const Field *f)
{
	if (f->fk == FK_ONEOF) {
		for (int i = 0; i < f->fields.n; i++)
			if (field_requires_custom_callback(VEC_AT(f->fields, Field, i)))
				return true;
		return false;
	}
	if (f->fk == FK_EXTRANGE)
		return false;
	return f->allocation == AL_CALLBACK && !str_eq(f->callback_datatype, "pb_callback_t");
}

/* codecs.escape_encode(s.encode('utf-8'))[0] */
static void escape_encode(Buf *b, const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '\'' || c == '\\')
			buf_fmt(b, "\\%c", c);
		else if (c == '\t')
			buf_adds(b, "\\t");
		else if (c == '\n')
			buf_adds(b, "\\n");
		else if (c == '\r')
			buf_adds(b, "\\r");
		else if (c < ' ' || c >= 0x7f)
			buf_fmt(b, "\\x%02x", c);
		else
			buf_addc(b, (char)c);
	}
}

static void hex_list(Buf *b, Str data)
{
	for (size_t i = 0; i < data.n; i++)
		buf_fmt(b, "%s0x%02x", i ? "," : "", (unsigned char)data.p[i]);
}

static Str escape_decode_at(const char *s, const char *site);

/* Bytes of repeated initializers so far: together they stay below 64 MiB. */
static size_t init_bytes;

static const char *repeat_join(const char *item, int64_t count, const char *sep)
{
	Buf b = {0};
	if (count > 0) {
		size_t need = (size_t)count * (strlen(item) + strlen(sep));
		if (need > (64u << 20) || init_bytes + need > (64u << 20))
			fail("unsupported by protoc-gen-uni: initializers larger than 64 MiB (max_count %lld)", (long long)count);
		init_bytes += need;
	}
	for (int64_t i = 0; i < count; i++) {
		if (i)
			buf_adds(&b, sep);
		buf_adds(&b, item);
	}
	return buf_str(&b);
}

static const char *field_initializer(const Field *f, bool null_init, bool inner_only);

static const char *oneof_initializer(const Field *o, bool null_init)
{
	TB_PUSH(o->has_msg_cb ? F_ONEOF_GETINIT_CB : F_ONEOF_GETINIT);
	const char *first = field_initializer(VEC_AT(o->fields, Field, 0), null_init, false);
	TB_POP();
	return o->has_msg_cb ? xcat("{{NULL}, NULL}, 0, {", first, "}") : xcat("0, {", first, "}");
}

static const char *field_initializer(const Field *f, bool null_init, bool inner_only)
{
	if (f->fk == FK_ONEOF)
		return oneof_initializer(f, null_init);
	const char *inner;
	if (f->initializer) {
		inner = f->initializer;
	} else if (is_msg_type(f->pbtype)) {
		inner = ns_define_name(xcat(pv_text(f->ctype), null_init ? "_init_zero" : "_init_default"));
	} else if (f->deflt.kind == PV_NONE || null_init) {
		switch (f->pbtype) {
		case PB_STRING: inner = "\"\""; break;
		case PB_BYTES: inner = "{0, {0}}"; break;
		case PB_FIXED_LENGTH_BYTES: inner = "{0}"; break;
		case PB_ENUM: case PB_UENUM: inner = xcat("_", ns_define_name(pv_text(f->ctype)), "_MIN"); break;
		default: inner = "0";
		}
	} else {
		const char *d = pv_text(f->deflt);
		Buf b = {0};
		switch (f->pbtype) {
		case PB_STRING:
			buf_addc(&b, '"');
			escape_encode(&b, d);
			buf_addc(&b, '"');
			break;
		case PB_BYTES:
		case PB_FIXED_LENGTH_BYTES: {
			Str data = escape_decode_at(d, f->pbtype == PB_BYTES ? S_INIT_BYTES : S_INIT_FIXED_BYTES);
			if (!data.n) {
				buf_adds(&b, f->pbtype == PB_BYTES ? "{0, {0}}" : "{0}");
			} else {
				if (f->pbtype == PB_BYTES)
					buf_fmt(&b, "{%d, {", (int)data.n);
				else
					buf_addc(&b, '{');
				hex_list(&b, data);
				buf_adds(&b, f->pbtype == PB_BYTES ? "}}" : "}");
			}
			break;
		}
		case PB_FIXED32: case PB_UINT32: buf_fmt(&b, "%su", d); break;
		case PB_FIXED64: case PB_UINT64: buf_fmt(&b, "%sull", d); break;
		case PB_SFIXED64: case PB_INT64: buf_fmt(&b, "%sll", d); break;
		case PB_FLOAT: case PB_DOUBLE:
			if (strstr(d, "inf"))
				buf_adds(&b, str_replace_all(d, "inf", "INFINITY"));
			else if (strstr(d, "nan"))
				buf_adds(&b, str_replace_all(d, "nan", "NAN"));
			else if (!strchr(d, '.') && f->pbtype == PB_FLOAT)
				buf_fmt(&b, "%s.0f", d);
			else if (f->pbtype == PB_FLOAT)
				buf_fmt(&b, "%sf", d);
			else
				buf_adds(&b, d);
			break;
		case PB_ENUM: case PB_UENUM: buf_adds(&b, ns_enum_entry(d)); break;
		default: buf_adds(&b, d);
		}
		inner = buf_str(&b);
	}
	if (inner_only)
		return inner;
	const char *outer = NULL;
	if (f->allocation == AL_STATIC) {
		if (f->rules == R_REPEATED)
			outer = xcat("0, {", repeat_join(inner, f->max_count, ", "), "}");
		else if (f->rules == R_FIXARRAY)
			outer = xcat("{", repeat_join(inner, f->max_count, ", "), "}");
		else if (f->rules == R_OPTIONAL)
			outer = xcat(null_init || !f->default_has ? "false, " : "true, ", inner);
		else
			outer = inner;
	} else if (f->allocation == AL_POINTER) {
		outer = f->rules == R_REPEATED ? "0, NULL" : "NULL";
	} else if (f->allocation == AL_CALLBACK) {
		if (f->pbtype == PB_EXTENSION)
			outer = "NULL";
		else if (str_eq(f->callback_datatype, "pb_callback_t"))
			outer = "{{NULL}, NULL}";
		else if (f->initializer)
			outer = inner;
		else {
			Str s = py_strip(str_c(f->callback_datatype));
			outer = s.n && s.p[s.n - 1] == '*' ? "NULL" : "{0}";
		}
	}
	if (f->pbtype == PB_MSG_W_CB && (f->rules == R_REPEATED || f->rules == R_OPTIONAL))
		outer = xcat("{{NULL}, NULL}, ", outer ? outer : "None");
	return outer ? outer : "None";
}

static void field_tags(Buf *b, const Field *f)
{
	if (f->fk == FK_ONEOF) {
		for (int i = 0; i < f->fields.n; i++)
			field_tags(b, VEC_AT(f->fields, Field, i));
		return;
	}
	if (f->fk == FK_EXTRANGE)
		return;
	const char *id = f->fk == FK_EXTFIELD ? ns_define_name(xcat(names_str(f->fullname), "_tag"))
		: ns_define_name(xcat(names_str(f->struct_name), "_", f->name, "_tag"));
	buf_adds(b, "#define ");
	add_ljust(b, id, 40);
	buf_fmt(b, " %lld\n", (long long)f->tag);
}

static const char *macro_x = "X", *macro_a = "a";

static void field_list(Buf *b, const Field *f)
{
	const char *name = field_var_name(f);
	if (f->rules == R_ONEOF) {
		const char *un = ns_var_name(f->union_name);
		name = f->anonymous ? xfmt("(%s,%s,%s)", un, name, name) : xfmt("(%s,%s,%s.%s)", un, name, un, name);
	}
	buf_fmt(b, "%s(%s, ", macro_x, macro_a);
	add_ljust(b, xcat(alloc_names[f->allocation], ","), 9);
	buf_addc(b, ' ');
	add_ljust(b, xcat(rules_names[f->rules], ","), 9);
	buf_addc(b, ' ');
	add_ljust(b, xcat(pbtype_names[f->pbtype], ","), 9);
	buf_addc(b, ' ');
	add_ljust(b, xcat(name, ","), 16);
	char *t = xfmt("%lld", (long long)f->tag);
	for (size_t n = strlen(t); n < 3; n++)
		buf_addc(b, ' ');
	buf_fmt(b, " %s)", t);
}

static i128 msg_data_size(Message *m, Deps *d);
static const ESize *msg_encoded_size(Message *m, Deps *d);

static i128 field_data_size(const Field *f, Deps *d)
{
	if (f->fk == FK_ONEOF) {
		i128 m = 0;
		for (int i = 0; i < f->fields.n; i++) {
			i128 s = field_data_size(VEC_AT(f->fields, Field, i), d);
			if (!i || s > m)
				m = s;
		}
		return m;
	}
	i128 size, align;
	if (f->allocation == AL_POINTER || f->pbtype == PB_EXTENSION) {
		size = 8;
		align = 8;
	} else if (f->allocation == AL_CALLBACK) {
		size = 16;
		align = 8;
	} else if (is_msg_type(f->pbtype)) {
		align = 8;
		const char *sub = names_str(f->submsgname);
		Message *dep = deps_get(d, sub);
		if (dep) {
			if (dep->kind != KIND_MESSAGE)
				fail("AttributeError: 'Enum' object has no attribute 'data_size'");
			deps_enter(d, names_str(f->struct_name));
			size = msg_data_size(dep, d);
			d->nremoved--;
		} else {
			size = 256;
			nb_log(xfmt("Could not determine size for submessage %s, using default %d\n", sub, 256));
		}
		if (f->pbtype == PB_MSG_W_CB)
			size += 16;
	} else if (f->pbtype == PB_STRING || f->pbtype == PB_FIXED_LENGTH_BYTES) {
		size = f->max_size;
		align = 4;
	} else if (f->pbtype == PB_BYTES) {
		size = (i128)f->max_size + 4;
		align = 4;
	} else if (f->has_data_item_size) {
		size = f->data_item_size;
		align = f->data_item_size >= 8 ? 8 : 4;
	} else {
		fail("Exception: Unhandled field type: %s", pbtype_names[f->pbtype]);
	}
	if ((f->rules == R_REPEATED || f->rules == R_FIXARRAY) && f->allocation == AL_STATIC)
		size = mul_chk(size, f->max_count);
	if (f->rules != R_REQUIRED && f->rules != R_SINGULAR)
		size += 4;
	if (py_mod(size, align))
		size += align - py_mod(size, align);
	return size;
}

static const ESize *oneof_encoded_size(const Field *o, Deps *d, bool *none);

/* Field.encoded_size: NULL is None. */
static const ESize *field_encoded_size(const Field *f, Deps *d)
{
	if (f->fk == FK_ONEOF) {
		bool none;
		const ESize *e = oneof_encoded_size(f, d, &none);
		return none ? NULL : e;
	}
	if (f->fk == FK_EXTRANGE)
		return es_new(0);
	if (f->allocation != AL_STATIC)
		return NULL;
	const ESize *enc = NULL;
	if (is_msg_type(f->pbtype)) {
		const char *sub = names_str(f->submsgname);
		Message *submsg = deps_get(d, sub);
		if (submsg) {
			if (submsg->kind != KIND_MESSAGE)
				fail("TypeError: Enum.encoded_size() takes 1 positional argument but 2 were given");
			const char *sn = names_str(f->struct_name);
			deps_enter(d, sn);
			enc = msg_encoded_size(submsg, d);
			d->nremoved--;
			Message *my = deps_get(d, sn);
			bool external = !my || submsg->protofile != my->protofile;
			if (enc && sl_len(enc->symbols) && external)
				enc = NULL;
			if (enc)
				enc = es_add_int(enc, varint_max_size(es_upperlimit(enc)));
			else if (!external)
				return NULL;
		}
		if (!enc)
			enc = es_add_int(es_sym(names_str(names_add(f->submsgname, "size"))), 5);
	} else if (is_enum_type(f->pbtype)) {
		Enum *et = deps_get(d, pv_text(f->ctype));
		if (et) {
			if (et->kind != KIND_ENUM)
				fail("TypeError: Message.encoded_size() missing 1 required positional argument: 'dependencies'");
			enc = es_new(enum_encoded_size(et));
		} else {
			enc = es_new(10);
		}
	} else if (!f->has_enc_size) {
		fail("RuntimeError: Could not determine encoded size for %s.%s", names_str(f->struct_name), f->name);
	} else {
		enc = es_new(f->enc_size);
	}
	enc = es_add_int(enc, varint_max_size((i128)f->tag * 8));
	if (f->rules == R_REPEATED || f->rules == R_FIXARRAY) {
		enc = es_mul(enc, f->max_count);
		if (f->max_count == 1)
			enc = es_add_int(enc, 1);
	}
	return enc;
}

static const ESize *oneof_encoded_size(const Field *o, Deps *d, bool *none)
{
	i128 largest = 0;
	*none = false;
	int nd = 0;
	int64_t *keys = xalloc(sizeof *keys * (size_t)(o->fields.n + 1));
	const ESize **vals = xalloc(sizeof *vals * (size_t)(o->fields.n + 1));
	for (int i = 0; i < o->fields.n; i++) {
		const Field *f = VEC_AT(o->fields, Field, i);
		const ESize *s = field_encoded_size(f, d);
		if (!s) {
			*none = true;
			return NULL;
		}
		if (sl_len(s->symbols)) {
			int k = 0;
			while (k < nd && keys[k] != f->tag)
				k++;
			if (k == nd)
				nd++;
			keys[k] = f->tag;
			vals[k] = s;
		} else if (s->value > largest) {
			largest = s->value;
		}
	}
	if (!nd)
		return es_new(largest);
	if (largest > 0) {
		int k = 0;
		while (k < nd && keys[k] != 0)
			k++;
		if (k == nd)
			nd++;
		keys[k] = 0;
		vals[k] = es_new(largest);
	}
	if (nd == 1)
		return vals[0];
	const char *un = xfmt("%s_%s_size_union", names_str(o->struct_name), o->name);
	Buf b = {0};
	buf_fmt(&b, "union %s {", un);
	const SList *req = NULL;
	for (int k = 0; k < nd; k++) {
		buf_fmt(&b, "%schar f%lld[%s];", k ? " " : "", (long long)keys[k], es_str(vals[k]));
		req = sl_cat(req, vals[k]->required_defines);
	}
	buf_adds(&b, "};\n");
	ESize *e = es_new(0);
	e->symbols = sl_leaf(xfmt("sizeof(union %s)", un));
	e->declarations = sl_leaf(buf_str(&b));
	e->required_defines = req;
	return e;
}

/* ---- ExtensionRange, ExtensionField, OneOf ---- */

static Field *extrange_new(Names struct_name, int32_t start)
{
	Field *f = xalloc(sizeof *f);
	f->fk = FK_EXTRANGE;
	f->tag = start;
	f->struct_name = struct_name;
	f->name = "extensions";
	f->pbtype = PB_EXTENSION;
	f->rules = R_OPTIONAL;
	f->allocation = AL_CALLBACK;
	f->ctype = pv_str("pb_extension_t");
	f->array_decl = "";
	f->has_max_size = f->has_max_count = f->has_data_item_size = true;
	f->callback_datatype = "pb_extension_t*";
	return f;
}

static Message *message_new(Names names, MsgDesc *desc, NbOpts *mo, const char *path, const Map *comments);

static Field *extfield_new(Names fullname, FieldDesc *desc, NbOpts *fo)
{
	Field *f = xalloc(sizeof *f);
	f->fullname = fullname;
	TB_PUSH(F_EXT_EXTENDEE);
	names_from_type_name(desc->extendee);
	TB_POP();
	TB_PUSH(F_EXT_FIELD);
	field_init(f, names_add(fullname, "extmsg"), desc, fo, "", NULL);
	TB_POP();
	f->fk = FK_EXTFIELD;
	if (f->rules != R_OPTIONAL) {
		f->skip = true;
	} else {
		f->rules = R_REQUIRED;
		f->msg = message_new(names_add(fullname, "extmsg"), NULL, fo, "", NULL);
		vec_push(&f->msg->fields, f);
	}
	return f;
}

static Field *oneof_new(Names struct_name, const char *name, NbOpts *o)
{
	Field *f = xalloc(sizeof *f);
	f->fk = FK_ONEOF;
	f->struct_name = struct_name;
	f->name = name;
	f->ctype = pv_str("union");
	f->pbtype = PB_ONEOF;
	f->allocation = AL_ONEOF;
	f->rules = R_ONEOF;
	f->anonymous = opt(o, NO_ANONYMOUS_ONEOF) != 0;
	f->sort_by_tag = opt(o, NO_SORT_BY_TAG) != 0;
	return f;
}

static int cmp_tag(const void *a, const void *b)
{
	const Field *x = a, *y = b;
	return x->tag < y->tag ? -1 : x->tag > y->tag;
}

/* ---- Message ---- */

static NbOpts *get_nanopb_suboptions(NbOpts *ext, const NbOpts *options, bool proto3_file)
{
	NbOpts *o = opts_copy(options);
	if (proto3_file)
		opts_set(o, NO_PROTO3, 1);
	if (ext)
		opts_merge(o, ext);
	return o;
}

static void load_fields(Message *m, MsgDesc *desc, NbOpts *mo)
{
	int no = desc->noneofs;
	bool *no_union = xalloc((size_t)no + 1);
	Map extra_no_unions = {0}; /* out of range oneof indexes */
	Field **oneofs = xalloc(sizeof *oneofs * (size_t)(no + 1));
	for (int i = 0; i < no; i++) {
		NbOpts *oo = get_nanopb_suboptions(desc->nanopb, mo, false);
		if (opt(oo, NO_NO_UNIONS))
			no_union[i] = true;
		else if (opt(oo, NO_TYPE) != FT_IGNORE)
			oneofs[i] = oneof_new(m->name, desc->oneofs[i], oo);
	}
	for (int index = 0; index < desc->nfields; index++) {
		FieldDesc *fd = &desc->fields[index];
		NbOpts *fo = get_nanopb_suboptions(fd->nanopb, mo, false);
		if (opt(fo, NO_TYPE) == FT_IGNORE)
			continue;
		if (opt(fo, NO_DISCARD_DEPRECATED) && fd->deprecated)
			continue;
		if (opt(fo, NO_DESCRIPTORSIZE) > m->descriptorsize)
			m->descriptorsize = opt(fo, NO_DESCRIPTORSIZE);
		Field *f = xalloc(sizeof *f);
		TB_PUSH(F_LOAD_FIELD);
		field_init(f, m->name, fd, fo, path_add(m->pe.path, 2, index), m->pe.comments);
		TB_POP();
		if (fd->has_oneof_index) {
			int32_t oi = fd->oneof_index;
			bool in_range = oi >= 0 && oi < no;
			if (fd->proto3_optional) {
				if (in_range)
					no_union[oi] = true;
				else
					map_put(&extra_no_unions, xfmt("%d", (int)oi), (void *)1);
			}
			bool is_no_union = in_range ? no_union[oi] : extra_no_unions.n && map_has(&extra_no_unions, xfmt("%d", (int)oi));
			if (is_no_union) {
				vec_push(&m->fields, f);
			} else if (in_range && oneofs[oi]) {
				Field *o = oneofs[oi];
				f->union_name = o->name;
				f->rules = R_ONEOF;
				f->anonymous = o->anonymous;
				vec_push(&o->fields, f);
				if (f->pbtype == PB_MSG_W_CB)
					o->has_msg_cb = true;
				if (!o->in_fields) {
					o->in_fields = true;
					vec_push(&m->fields, o);
				}
			}
		} else {
			vec_push(&m->fields, f);
		}
		if (f->math_include_required)
			m->math_include_required = true;
	}
	for (int i = 0; i < no; i++) {
		Field *o = oneofs[i];
		if (!o || !o->fields.n)
			continue;
		if (o->sort_by_tag)
			stable_sort(o->fields.v, o->fields.n, cmp_tag);
		o->tag = VEC_AT(o->fields, Field, 0)->tag;
		for (int k = 0; k < o->fields.n; k++)
			if (VEC_AT(o->fields, Field, k)->tag < o->tag)
				o->tag = VEC_AT(o->fields, Field, k)->tag;
	}
	if (desc->nranges > 0) {
		NbOpts *fo = get_nanopb_suboptions(desc->nanopb, mo, false);
		int32_t start = desc->range_start[0];
		for (int i = 1; i < desc->nranges; i++)
			if (desc->range_start[i] < start)
				start = desc->range_start[i];
		if (opt(fo, NO_TYPE) != FT_IGNORE)
			vec_push(&m->fields, extrange_new(m->name, start));
	}
	if (opt(mo, NO_SORT_BY_TAG))
		stable_sort(m->fields.v, m->fields.n, cmp_tag);
}

static Message *message_new(Names names, MsgDesc *desc, NbOpts *mo, const char *path, const Map *comments)
{
	Message *m = xalloc(sizeof *m);
	m->kind = KIND_MESSAGE;
	m->pe = (ProtoElement){path, comments};
	m->name = names;
	m->desc = desc;
	m->packed = opt(mo, NO_PACKED_STRUCT) != 0;
	m->descriptorsize = opt(mo, NO_DESCRIPTORSIZE);
	if (opt(mo, NO_MSGID)) {
		m->has_msgid = true;
		m->msgid = (uint32_t)opt(mo, NO_MSGID);
	}
	if (desc) {
		TB_PUSH(F_MESSAGE_LOAD);
		load_fields(m, desc, mo);
		TB_POP();
	}
	m->callback_function = mo->s[NO_CALLBACK_FUNCTION];
	if (!opts_has(mo, NO_CALLBACK_FUNCTION))
		for (int i = 0; i < m->fields.n; i++)
			if (field_requires_custom_callback(VEC_AT(m->fields, Field, i))) {
				m->callback_function = xcat(names_str(names), "_callback");
				break;
			}
	return m;
}

static void message_deps(const Message *m, Vec *v)
{
	for (int i = 0; i < m->fields.n; i++)
		field_deps(VEC_AT(m->fields, Field, i), v);
}

static void message_str(Buf *out, const Message *m)
{
	const char *lead, *trail;
	get_comments(&m->pe, "", false, &lead, &trail);
	const char *ns = names_str(m->name);
	Buf b = {0};
	if (*lead)
		buf_fmt(&b, "%s\n", lead);
	buf_fmt(&b, "typedef struct %s {", ns_struct_name(ns));
	if (*trail)
		buf_fmt(&b, " %s", trail);
	buf_addc(&b, '\n');
	if (!m->fields.n)
		buf_adds(&b, "    char dummy_field;");
	for (int i = 0; i < m->fields.n; i++) {
		if (i)
			buf_addc(&b, '\n');
		field_str(&b, VEC_AT(m->fields, Field, i));
	}
	if (protoc_insertion_points)
		buf_fmt(&b, "\n/* @@protoc_insertion_point(struct:%s) */", ns);
	buf_adds(&b, "\n}");
	if (m->packed)
		buf_adds(&b, " pb_packed");
	buf_fmt(&b, " %s;", ns_type_name(ns));
	if (m->packed)
		buf_adds(out, "PB_PACKED_STRUCT_START\n");
	buf_add(out, b.p, b.n);
	if (m->packed)
		buf_adds(out, "\nPB_PACKED_STRUCT_END");
	buf_addc(out, '\n');
}

static void message_types(Buf *b, const Message *m)
{
	for (int i = 0; i < m->fields.n; i++)
		field_types(b, VEC_AT(m->fields, Field, i));
}

static const char *message_initializer(const Message *m, bool null_init)
{
	if (!m->fields.n)
		return "{0}";
	Buf b = {0};
	buf_addc(&b, '{');
	for (int i = 0; i < m->fields.n; i++) {
		if (i)
			buf_adds(&b, ", ");
		TB_PUSH(F_MSG_GETINIT);
		buf_adds(&b, field_initializer(VEC_AT(m->fields, Field, i), null_init, false));
		TB_POP();
	}
	buf_addc(&b, '}');
	return buf_str(&b);
}

static int count_required_fields(const Message *m)
{
	int c = 0;
	for (int i = 0; i < m->fields.n; i++) {
		const Field *f = VEC_AT(m->fields, Field, i);
		if (f->fk != FK_ONEOF && f->rules == R_REQUIRED)
			c++;
	}
	return c;
}

/* all_fields: the fields with the members of oneofs */
static Vec all_fields(const Message *m)
{
	Vec v = {0};
	for (int i = 0; i < m->fields.n; i++) {
		Field *f = VEC_AT(m->fields, Field, i);
		if (f->fk == FK_ONEOF)
			for (int k = 0; k < f->fields.n; k++)
				vec_push(&v, f->fields.v[k]);
		else
			vec_push(&v, f);
	}
	return v;
}

static Str message_default_value(Message *m, Deps *d);

static void fields_declaration(Buf *b, Message *m, Deps *d)
{
	Vec all = all_fields(m);
	macro_x = "X";
	macro_a = "a";
	for (bool again = true; again;) {
		again = false;
		for (int i = 0; i < all.n; i++)
			if (str_eq(VEC_AT(all, Field, i)->name, macro_x)) {
				macro_x = xcat(macro_x, "_");
				again = true;
				break;
			}
	}
	for (bool again = true; again;) {
		again = false;
		for (int i = 0; i < all.n; i++)
			if (str_eq(VEC_AT(all, Field, i)->name, macro_a)) {
				macro_a = xcat(macro_a, "_");
				again = true;
				break;
			}
	}
	Vec sorted = {0};
	vec_copy(&sorted, &all);
	stable_sort(sorted.v, sorted.n, cmp_tag);
	const char *ns = names_str(m->name);
	const char *dn = ns_define_name(ns);
	buf_fmt(b, "#define %s_FIELDLIST(%s, %s) \\\n", dn, macro_x, macro_a);
	for (int i = 0; i < sorted.n; i++) {
		if (i)
			buf_adds(b, " \\\n");
		field_list(b, VEC_AT(sorted, Field, i));
	}
	buf_addc(b, '\n');
	bool has_cb = false;
	for (int i = 0; i < m->fields.n; i++)
		if (field_has_callbacks(VEC_AT(m->fields, Field, i)))
			has_cb = true;
	if (has_cb) {
		if (!str_eq(m->callback_function, "pb_default_field_callback"))
			buf_fmt(b, "extern bool %s(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field);\n",
				m->callback_function);
		buf_fmt(b, "#define %s_CALLBACK %s\n", dn, m->callback_function);
	} else {
		buf_fmt(b, "#define %s_CALLBACK NULL\n", dn);
	}
	TB_PUSH(F_DECL_DEFVAL);
	Str defval = message_default_value(m, d);
	TB_POP();
	if (defval.n) {
		buf_fmt(b, "#define %s_DEFAULT (const pb_byte_t*)\"", dn);
		for (size_t i = 0; i < defval.n; i++)
			buf_fmt(b, "\\x%02x", (unsigned char)defval.p[i]);
		buf_adds(b, "\\x00\"\n");
	} else {
		buf_fmt(b, "#define %s_DEFAULT NULL\n", dn);
	}
	for (int i = 0; i < sorted.n; i++) {
		const Field *f = VEC_AT(sorted, Field, i);
		if (!is_msg_type(f->pbtype))
			continue;
		if (f->rules == R_ONEOF)
			buf_fmt(b, "#define %s_%s_%s_MSGTYPE %s\n", ns_type_name(ns), ns_var_name(f->union_name),
				ns_var_name(f->name), ctype_type_name(f->ctype));
		else
			buf_fmt(b, "#define %s_%s_MSGTYPE %s\n", ns_type_name(ns), ns_var_name(f->name),
				ctype_type_name(f->ctype));
	}
}

static void enumtype_defines(Buf *b, const Message *m)
{
	Vec all = all_fields(m);
	const char *ns = names_str(m->name);
	for (int i = 0; i < all.n; i++) {
		const Field *f = VEC_AT(all, Field, i);
		if (!is_enum_type(f->pbtype))
			continue;
		if (f->rules == R_ONEOF)
			buf_fmt(b, "#define %s_%s_%s_ENUMTYPE %s\n", ns_type_name(ns), ns_var_name(f->union_name),
				ns_var_name(f->name), ctype_type_name(f->ctype));
		else
			buf_fmt(b, "#define %s_%s_ENUMTYPE %s\n", ns_type_name(ns), ns_var_name(f->name),
				ctype_type_name(f->ctype));
	}
}

static int count_all_fields(const Message *m)
{
	int c = 0;
	for (int i = 0; i < m->fields.n; i++) {
		const Field *f = VEC_AT(m->fields, Field, i);
		c += f->fk == FK_ONEOF ? f->fields.n : 1;
	}
	return c;
}

static void cpp_lookup(Buf *b, const Message *m, const Map *local_defines)
{
	const char *ns = names_str(m->name);
	buf_fmt(b, "template <>\nstruct MessageDescriptor<%s> {\n", ns);
	buf_fmt(b, "    static PB_INLINE_CONSTEXPR const pb_size_t fields_array_length = %d;\n", count_all_fields(m));
	const char *sd = xcat(ns, "_size");
	if (map_has(local_defines, sd))
		buf_fmt(b, "    static PB_INLINE_CONSTEXPR const pb_size_t size = %s;\n", sd);
	buf_fmt(b, "    static inline const pb_msgdesc_t* fields() {\n        return &%s_msg;\n    }\n", ns);
	buf_fmt(b, "    static inline bool has_msgid() {\n        return %s;\n    }\n", m->has_msgid ? "true" : "false");
	buf_fmt(b, "    static inline uint32_t msgid() {\n        return %lld;\n    }\n};",
		(long long)(m->has_msgid ? m->msgid : 0));
}

static i128 msg_data_size(Message *m, Deps *d)
{
	if (m->memo_gen_d == deps_gen) {
		nb_log(m->memo_log_d);
		return m->memo_d;
	}
	bool touched = d->touched;
	d->touched = false;
	size_t log0 = nb_stderr.n;
	i128 s = 0;
	for (int i = 0; i < m->fields.n; i++)
		s = chk(s + field_data_size(VEC_AT(m->fields, Field, i), d));
	if (!d->touched) {
		m->memo_gen_d = deps_gen;
		m->memo_d = s;
		m->memo_log_d = xstrndup(nb_stderr.p ? nb_stderr.p + log0 : "", nb_stderr.n - log0);
	}
	d->touched = d->touched || touched;
	return s;
}

static const ESize *msg_encoded_size(Message *m, Deps *d)
{
	if (m->memo_gen_e == deps_gen)
		return m->memo_e_none ? NULL : m->memo_e;
	bool touched = d->touched;
	d->touched = false;
	const ESize *size = es_new(0);
	bool none = false;
	for (int i = 0; i < m->fields.n; i++) {
		const ESize *fs = field_encoded_size(VEC_AT(m->fields, Field, i), d);
		if (!fs) {
			none = true;
			break;
		}
		size = es_add(size, fs);
	}
	if (!d->touched) {
		m->memo_gen_e = deps_gen;
		m->memo_e = size;
		m->memo_e_none = none;
	}
	d->touched = d->touched || touched;
	return none ? NULL : size;
}

static int64_t required_descriptor_width(Message *m, Deps *d)
{
	if (m->descriptorsize != DS_AUTO)
		return m->descriptorsize;
	if (!m->fields.n)
		return 1;
	Vec all = all_fields(m);
	int64_t max_tag = 0, max_array = 0;
	i128 max_data = 0;
	for (int i = 0; i < all.n; i++) {
		const Field *f = VEC_AT(all, Field, i);
		if (!i || f->tag > max_tag)
			max_tag = f->tag;
		int64_t c = f->has_max_count ? f->max_count : 0;
		if (!i || c > max_array)
			max_array = c;
	}
	i128 max_offset = msg_data_size(m, d);
	for (int i = 0; i < all.n; i++) {
		i128 s = field_data_size(VEC_AT(all, Field, i), d);
		if (!i || s > max_data)
			max_data = s;
	}
	if (max_array > 0xFFFF)
		return 8;
	if (max_tag > 0x3FF || max_offset > 0xFFFF || max_array > 0x0FFF || max_data > 0x0FFF)
		return 4;
	if (max_tag > 0x3F || max_offset > 0xFF)
		return 2;
	return 1;
}

static void fields_definition(Buf *b, Message *m, Deps *d)
{
	int64_t w = required_descriptor_width(m, d);
	const char *ns = names_str(m->name);
	buf_fmt(b, "PB_BIND(%s, %s, ", ns_define_name(ns), ns_type_name(ns));
	if (w == 1)
		buf_adds(b, "AUTO");
	else
		buf_fmt(b, "%lld", (long long)w);
	buf_adds(b, ")\n");
}
