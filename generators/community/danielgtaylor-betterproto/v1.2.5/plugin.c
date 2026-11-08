/* Copyright (c) 2019 Daniel G. Taylor
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

/* Port of betterproto/plugin.py and betterproto/casing.py of betterproto 1.2.5
 * (https://github.com/danielgtaylor/python-betterproto/tree/v1.2.5): generate_code. The Jinja2
 * template betterproto/templates/template.py.j2 is rendered as black nodes (black.h) whose
 * prefixes hold the blank lines and comment lines of the rendered text. Python exceptions are
 * fail() calls with the exception text; input that upstream accepts but this port cannot
 * reproduce fails with "unsupported by protoc-gen-uni: ...". */

/* ---- input checks ---- */

static bool valid_utf8(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xfffd && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

/* A string of the request as Python sees it (upb returns invalid UTF-8 as bytes). */
static const char *text(Str s, const char *what)
{
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: NUL byte in %s", what);
	if (!valid_utf8(s))
		fail("unsupported by protoc-gen-uni: invalid UTF-8 in %s", what);
	return s.p;
}

static bool is_ident(const char *s)
{
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_'))
		return false;
	for (s++; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
			return false;
	return true;
}

/* Names in the output must be ASCII identifiers (as protoc and buf guarantee): the Python code
 * that upstream generates from others is not reproduced. */
static const char *ident(Str s, const char *what)
{
	const char *v = text(s, what);
	if (!is_ident(v))
		fail("unsupported by protoc-gen-uni: %s %s is not an ASCII identifier", what, py_repr_str(s));
	return v;
}

/* Dot-separated identifiers (empty allowed when may_be_empty). */
static const char *dotted(Str s, const char *what, bool may_be_empty)
{
	const char *v = text(s, what);
	if (!*v && may_be_empty)
		return v;
	Vec parts = str_split(v, '.');
	for (int i = 0; i < parts.n; i++)
		if (!is_ident(VEC_AT(parts, char, i)))
			fail("unsupported by protoc-gen-uni: %s %s is not made of ASCII identifiers", what, py_repr_str(s));
	return v;
}

/* Words the PY37 grammar of black (async keywords) does not accept as names. */
static bool is_keyword(const char *s)
{
	static const char *const kw[] = {"and", "as", "assert", "async", "await", "break", "class", "continue", "def",
					 "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
					 "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
					 "return", "try", "while", "with", "yield"};
	for (size_t i = 0; i < sizeof kw / sizeof *kw; i++)
		if (!strcmp(s, kw[i]))
			return true;
	return false;
}

/* A name in the rendered source that black cannot parse when it is a keyword. */
static const char *name_ok(const char *s)
{
	if (is_keyword(s))
		fail("black.parsing.InvalidInput: Cannot parse for target version Python 3.7: the keyword '%s' is "
		     "used as a name",
		     s);
	return s;
}

static void dotted_ok(const char *s)
{
	Vec parts = str_split(s, '.');
	for (int i = 0; i < parts.n; i++)
		name_ok(VEC_AT(parts, char, i));
}

/* len() of a str */
static int cp_len(Str s)
{
	int k = 0;
	for (size_t i = 0; i < s.n; i++)
		k += ((unsigned char)s.p[i] & 0xc0) != 0x80;
	return k;
}

/* ---- casing.py ---- */

static char *safe_snake_case(const char *value)
{
	static const char *const kw[] = {"and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
					 "else", "except", "finally", "for", "from", "global", "if", "import", "in",
					 "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
					 "while", "with", "yield"};
	char *v = sc_snakecase(value);
	for (size_t i = 0; i < sizeof kw / sizeof *kw; i++)
		if (!strcmp(v, kw[i]))
			return xcat(v, "_");
	return v;
}

/* ---- types: the strings of py_type/get_ref_type, kept as trees to build their nodes ---- */

typedef enum { TY_NAME, TY_STR, TY_SUB } TyKind;
typedef struct Ty {
	TyKind k;
	const char *s; /* NAME: a.b; STR: the text between the quotes; SUB: the subscripted name */
	struct Ty *args[2];
	int n;
} Ty;

static Ty *ty_name(const char *s)
{
	Ty *t = xalloc(sizeof *t);
	t->k = TY_NAME;
	t->s = s;
	return t;
}

static Ty *ty_quoted(const char *s)
{
	Ty *t = ty_name(s);
	t->k = TY_STR;
	return t;
}

static Ty *ty_sub(const char *base, Ty *a, Ty *b)
{
	Ty *t = ty_name(base);
	t->k = TY_SUB;
	t->args[0] = a;
	t->args[1] = b;
	t->n = b ? 2 : 1;
	return t;
}

static void ty_render(const Ty *t, Buf *b)
{
	switch (t->k) {
	case TY_NAME: buf_adds(b, t->s); return;
	case TY_STR: buf_fmt(b, "\"%s\"", t->s); return;
	case TY_SUB:
		buf_adds(b, t->s);
		buf_addc(b, '[');
		for (int i = 0; i < t->n; i++) {
			if (i)
				buf_adds(b, ", ");
			ty_render(t->args[i], b);
		}
		buf_addc(b, ']');
		return;
	}
}

static const char *ty_str(const Ty *t)
{
	Buf b = {0};
	ty_render(t, &b);
	return buf_str(&b);
}

/* .strip('"') */
static Ty *ty_strip_quotes(Ty *t) { return t->k == TY_STR ? ty_name(t->s) : t; }

static BkNode *ty_node(const Ty *t)
{
	switch (t->k) {
	case TY_NAME:
		if (!*t->s)
			fail("black.parsing.InvalidInput: Cannot parse for target version Python 3.7: an empty type name");
		dotted_ok(t->s);
		return bk_dotted(t->s);
	case TY_STR: return bk_string(xfmt("\"%s\"", t->s));
	case TY_SUB: {
		BkNode *args[2];
		for (int i = 0; i < t->n; i++)
			args[i] = ty_node(t->args[i]);
		return bk_subscript(bk_dotted(t->s), t->n, args);
	}
	}
	return NULL;
}

/* ---- generate_code state ---- */

typedef struct {
	int kind; /* 0: no comment, 1: "# " lines, 2: docstring */
	const char *text; /* the rendered comment (pad included), as get_comment returns it */
	const char *pad;
	const char *doc; /* kind 2: the string literal """...""" */
} Comment;

typedef struct {
	const char *py_name;
	int64_t number;
	Comment comment;
	const char *field_type;
	const char *field_wraps;
	const char *map_types[2];
	Ty *type;
	const char *zero;
	const char *one_of;
} Prop;

typedef struct {
	const char *name, *py_name;
	Comment comment;
	Prop *props;
	int nprops;
} MsgData;

typedef struct {
	const char *name;
	int64_t value;
	Comment comment;
} Entry;

typedef struct {
	const char *py_name;
	Comment comment;
	Entry *entries;
	int n;
} EnumData;

typedef struct {
	const char *py_name;
	Comment comment;
	const char *route;
	Ty *input, *output;
	MsgData *input_message;
	bool server_streaming;
} Method;

typedef struct {
	const char *py_name;
	Comment comment;
	Method *methods;
	int n;
} Service;

typedef struct {
	const char *module; /* "" for "from . import x" */
	int dots;
	const char *name;
} Import;

typedef struct {
	const char *filename;
	const char *package;
	Vec files;  /* file names */
	Vec protos; /* PbMsg * */
	Map imports, datetime_imports, typing_imports; /* sets */
	Vec import_list, datetime_list, typing_list;
	Vec messages, enums, services;
	Map message_by_name; /* the first message of output["messages"] with a name */
} Output;

static void set_add(Map *m, Vec *order, const char *s, void *val)
{
	if (!map_has(m, s)) {
		map_put(m, s, val);
		vec_push(order, (void *)s);
	}
}

/* ---- get_comment ---- */

/* The leading comments of the locations of a file by path (the first location with non-empty
 * leading_comments wins, like the linear search of get_comment). */
typedef struct {
	Map by_path;
} Comments;

static char *path_key(const int64_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, "%lld,", (long long)path[i]);
	return (char *)buf_str(&b);
}

static COLD void comments_init(Comments *c, const PbMsg *file)
{
	PbMsg *sci = pb_msg(file, FDP_SOURCE_CODE_INFO);
	int n = pb_count(sci, SCI_LOCATION);
	for (int i = 0; i < n; i++) {
		PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		Str lead = pb_bytes(loc, LOC_LEADING);
		if (!lead.n)
			continue;
		int np = pb_count(loc, LOC_PATH);
		int64_t *path = xalloc(sizeof *path * (size_t)(np + 1));
		for (int j = 0; j < np; j++)
			path[j] = (int32_t)pb_int_at(loc, LOC_PATH, j);
		char *key = path_key(path, np);
		if (!map_has(&c->by_path, key)) {
			Str *s = xalloc(sizeof *s);
			*s = lead;
			map_put(&c->by_path, key, s);
		}
	}
}

static COLD Comment get_comment(Comments *c, const int64_t *path, int n, int indent)
{
	Comment res = {0};
	Str *lead = map_get(&c->by_path, path_key(path, n));
	if (!lead)
		return res;
	text(*lead, "comment");
	char *pad = xalloc((size_t)indent + 1);
	memset(pad, ' ', (size_t)indent);
	res.pad = pad;
	/* sci.leading_comments.strip().replace("\n", "") */
	Str s = py_strip(*lead);
	char *joined = xalloc(s.n + 1);
	size_t nj = 0;
	for (size_t i = 0; i < s.n; i++)
		if (s.p[i] != '\n')
			joined[nj++] = s.p[i];
	Str *lines;
	int nl = py_textwrap_wrap((Str){joined, nj}, 79 - indent, &lines);
	if (path[n - 2] == 2 && (n < 4 ? (fail("IndexError: list index out of range"), true) : path[n - 4] != 6)) {
		/* This is a field */
		Buf b = {0};
		buf_adds(&b, pad);
		buf_adds(&b, "# ");
		for (int i = 0; i < nl; i++) {
			if (i)
				buf_fmt(&b, "\n%s# ", pad);
			buf_add_str(&b, lines[i]);
			Buf line = {0};
			buf_adds(&line, "# ");
			buf_add_str(&line, lines[i]);
			if (!bk_comment_ok(buf_view(&line)))
				fail("unsupported by protoc-gen-uni: comment %s turns black formatting off",
				     py_repr_str(buf_view(&line)));
		}
		res.kind = 1;
		res.text = buf_str(&b);
		return res;
	}
	/* This is a message, enum, service, or method */
	Buf content = {0};
	if (nl == 1 && cp_len(lines[0]) < 79 - indent - 6) {
		Str l = lines[0];
		while (l.n && l.p[0] == '"')
			l.p++, l.n--;
		while (l.n && l.p[l.n - 1] == '"')
			l.n--;
		buf_add_str(&content, l);
	} else {
		buf_fmt(&content, "\n%s", pad);
		for (int i = 0; i < nl; i++) {
			if (i)
				buf_fmt(&content, "\n%s", pad);
			buf_add_str(&content, lines[i]);
		}
		buf_fmt(&content, "\n%s", pad);
	}
	if (!bk_docstring_ok(buf_view(&content)))
		fail("unsupported by protoc-gen-uni: comment with '\"\"\"' or a trailing backslash in a docstring");
	res.kind = 2;
	res.doc = xfmt("\"\"\"%s\"\"\"", buf_str(&content));
	res.text = xcat(pad, res.doc);
	return res;
}

/* ---- py_type, get_ref_type ---- */

static const char *const WRAPPERS[][2] = {
	{"google.protobuf.DoubleValue", "float"}, {"google.protobuf.FloatValue", "float"},
	{"google.protobuf.Int64Value", "int"},    {"google.protobuf.UInt64Value", "int"},
	{"google.protobuf.Int32Value", "int"},    {"google.protobuf.UInt32Value", "int"},
	{"google.protobuf.BoolValue", "bool"},    {"google.protobuf.StringValue", "str"},
	{"google.protobuf.BytesValue", "bytes"},
};

/* str.lstrip(chars) */
static const char *lstrip_chars(const char *s, const char *chars)
{
	while (*s && strchr(chars, *s))
		s++;
	return s;
}

static Ty *get_ref_type(const char *package, Output *o, const char *type_name, bool unwrap)
{
	type_name = dotted(str_c(lstrip_chars(type_name, ".")), "type name", true);
	const char *wrapped = NULL, *wrapper_class = NULL;
	for (size_t i = 0; i < sizeof WRAPPERS / sizeof *WRAPPERS; i++)
		if (!strcmp(type_name, WRAPPERS[i][0])) {
			wrapped = WRAPPERS[i][1];
			wrapper_class = WRAPPERS[i][0] + strlen("google.protobuf.");
		}
	if (unwrap) {
		if (wrapped)
			return ty_sub("Optional", ty_name(wrapped), NULL);
		if (!strcmp(type_name, "google.protobuf.Duration"))
			return ty_name("timedelta");
		if (!strcmp(type_name, "google.protobuf.Timestamp"))
			return ty_name("datetime");
	} else if (wrapped) {
		const char *imp = xfmt("from google.protobuf.wrappers_pb2 import %s", wrapper_class);
		Import *im = xalloc(sizeof *im);
		*im = (Import){"google.protobuf.wrappers_pb2", 0, wrapper_class};
		set_add(&o->imports, &o->import_list, imp, im);
		return ty_name(wrapper_class);
	}
	Ty *result = NULL;
	if (has_prefix(type_name, package)) {
		/* type_name.lstrip(package).lstrip(".").split(".") */
		const char *rest = lstrip_chars(lstrip_chars(type_name, package), ".");
		Vec parts = str_split(rest, '.');
		const char *p0 = VEC_AT(parts, char, 0);
		if (parts.n > 1 && !*p0)
			fail("IndexError: string index out of range");
		if (parts.n == 1 || (parts.n > 1 && !(p0[0] >= 'a' && p0[0] <= 'z'))) {
			/* parts[0][0] == parts[0][0].upper() */
			Buf cased = {0};
			for (int i = 0; i < parts.n; i++)
				buf_adds(&cased, sc_pascalcase(VEC_AT(parts, char, i)));
			result = ty_quoted(buf_str(&cased));
		}
	}
	if (!result && strchr(type_name, '.')) {
		Vec parts = str_split(type_name, '.');
		const char *last = sc_pascalcase(VEC_AT(parts, char, parts.n - 1));
		Buf mod = {0};
		for (int i = 0; i < parts.n - 2; i++) {
			if (i)
				buf_addc(&mod, '.');
			buf_adds(&mod, VEC_AT(parts, char, i));
		}
		const char *name = VEC_AT(parts, char, parts.n - 2);
		const char *module = buf_str(&mod);
		Import *im = xalloc(sizeof *im);
		*im = (Import){module, 1, name};
		set_add(&o->imports, &o->import_list, xfmt("from .%s import %s", module, name), im);
		return ty_name(xfmt("%s.%s", name, last));
	}
	return result ? result : ty_name(type_name);
}

static Ty *py_type(const char *package, Output *o, const PbMsg *f)
{
	int64_t t = pb_int(f, FD_TYPE, PT_DOUBLE);
	switch (t) {
	case 1: case 2: case 6: case 7: case 15: case 16: return ty_name("float");
	case 3: case 4: case 5: case 13: case 17: case 18: return ty_name("int");
	case 8: return ty_name("bool");
	case 9: return ty_name("str");
	case 11: case 14: return get_ref_type(package, o, text(pb_bytes(f, FD_TYPE_NAME), "type name"), true);
	case 12: return ty_name("bytes");
	}
	fail("NotImplementedError: Unknown type %lld", (long long)t);
}

static const char *get_py_zero(int64_t type_num)
{
	switch (type_num) {
	case 8: return "False";
	case 9: return "\"\"";
	case 11: return "None";
	case 12: return "b\"\"";
	}
	return "0";
}

static const char *const TYPE_NAMES[] = {
	NULL, "TYPE_DOUBLE", "TYPE_FLOAT", "TYPE_INT64", "TYPE_UINT64", "TYPE_INT32", "TYPE_FIXED64",
	"TYPE_FIXED32", "TYPE_BOOL", "TYPE_STRING", "TYPE_GROUP", "TYPE_MESSAGE", "TYPE_BYTES",
	"TYPE_UINT32", "TYPE_ENUM", "TYPE_SFIXED32", "TYPE_SFIXED64", "TYPE_SINT32", "TYPE_SINT64",
};

/* FieldDescriptorProto.Type.Name(t): upb keeps unknown values of the closed enum as unknown
 * fields, so t is always a known value. */
static const char *type_enum_name(int64_t t)
{
	if (t < 1 || t > 18)
		fail("ValueError: Enum Type has no name defined for value %lld", (long long)t);
	return TYPE_NAMES[t];
}

static char *ascii_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	return r;
}

static char *ascii_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return r;
}

/* ---- traverse ---- */

typedef struct {
	const char *package;
	Output *o;
	Comments *comments;
} Ctx;

static void process_message(Ctx *c, const PbMsg *item, const char *name, const int64_t *path, int np)
{
	Output *o = c->o;
	MsgData *m = xalloc(sizeof *m);
	m->name = name;
	m->py_name = sc_pascalcase(name);
	m->comment = get_comment(c->comments, path, np, 4);
	int nf = pb_count(item, DP_FIELD);
	m->props = xalloc(sizeof *m->props * (size_t)(nf + 1));
	int64_t *fpath = xalloc(sizeof *fpath * (size_t)(np + 3));
	memcpy(fpath, path, sizeof *fpath * (size_t)np);
	for (int i = 0; i < nf; i++) {
		PbMsg *f = pb_msg_at(item, DP_FIELD, i);
		const char *fname = ident(pb_bytes(f, FD_NAME), "field name");
		int64_t ftype = pb_int(f, FD_TYPE, PT_DOUBLE);
		Ty *t = py_type(c->package, o, f);
		const char *zero = get_py_zero(ftype);
		const char *field_type = ascii_lower(type_enum_name(ftype)) + 5;
		const char *field_wraps = "";
		const char *tn = text(pb_bytes(f, FD_TYPE_NAME), "type name");
		if (has_prefix(tn, ".google.protobuf") && has_suffix(tn, "Value")) {
			dotted(str_c(lstrip_chars(tn, ".")), "type name", false);
			const char *dot = strrchr(tn, '.');
			const char *w = xstrndup(dot + 1, strlen(dot + 1) - 5);
			field_wraps = xfmt("betterproto.TYPE_%s", ascii_upper(w));
		}
		const char *map_types[2] = {NULL, NULL};
		if (ftype == 11) {
			const char *dot = strrchr(tn, '.');
			const char *message_type = ascii_lower(dot ? dot + 1 : tn);
			Buf me = {0};
			for (const char *q = fname; *q; q++)
				if (*q != '_')
					buf_addc(&me, *q);
			const char *map_entry = xcat(ascii_lower(buf_str(&me)), "entry");
			if (!strcmp(message_type, map_entry)) {
				int nn = pb_count(item, DP_NESTED_TYPE);
				for (int j = 0; j < nn; j++) {
					PbMsg *nested = pb_msg_at(item, DP_NESTED_TYPE, j);
					const char *nname = ident(pb_bytes(nested, DP_NAME), "message name");
					Buf nb = {0};
					for (const char *q = nname; *q; q++)
						if (*q != '_')
							buf_addc(&nb, *q);
					if (strcmp(ascii_lower(buf_str(&nb)), map_entry))
						continue;
					if (!pb_int(pb_msg(nested, DP_OPTIONS), 7, 0))
						continue;
					if (pb_count(nested, DP_FIELD) < 2)
						fail("IndexError: list index out of range");
					PbMsg *kf = pb_msg_at(nested, DP_FIELD, 0), *vf = pb_msg_at(nested, DP_FIELD, 1);
					Ty *k = py_type(c->package, o, kf);
					Ty *v = py_type(c->package, o, vf);
					t = ty_sub("Dict", k, v);
					field_type = "map";
					map_types[0] = type_enum_name(pb_int(kf, FD_TYPE, PT_DOUBLE));
					map_types[1] = type_enum_name(pb_int(vf, FD_TYPE, PT_DOUBLE));
					set_add(&o->typing_imports, &o->typing_list, "Dict", (void *)1);
				}
			}
		}
		if (pb_int(f, FD_LABEL, LABEL_OPTIONAL) == LABEL_REPEATED && strcmp(field_type, "map")) {
			t = ty_sub("List", t, NULL);
			zero = "[]";
			set_add(&o->typing_imports, &o->typing_list, "List", (void *)1);
		}
		const char *one_of = "";
		if (pb_has(f, FD_ONEOF_INDEX)) {
			int64_t idx = (int32_t)pb_int(f, FD_ONEOF_INDEX, 0);
			int no = pb_count(item, DP_ONEOF_DECL);
			if (idx < 0)
				idx += no;
			if (idx < 0 || idx >= no)
				fail("IndexError: list index out of range");
			one_of = text(pb_bytes(pb_msg_at(item, DP_ONEOF_DECL, (int)idx), ODP_NAME), "oneof name");
			for (const char *q = one_of; *q; q++)
				if (*q == '"' || *q == '\\' || (unsigned char)*q < 0x20 || (unsigned char)*q >= 0x7f)
					fail("unsupported by protoc-gen-uni: oneof name %s", py_repr_str(str_c(one_of)));
		}
		const char *ts = ty_str(t);
		if (strstr(ts, "Optional["))
			set_add(&o->typing_imports, &o->typing_list, "Optional", (void *)1);
		if (strstr(ts, "timedelta"))
			set_add(&o->datetime_imports, &o->datetime_list, "timedelta", (void *)1);
		else if (strstr(ts, "datetime"))
			set_add(&o->datetime_imports, &o->datetime_list, "datetime", (void *)1);
		fpath[np] = 2;
		fpath[np + 1] = i;
		Prop *p = &m->props[m->nprops++];
		p->py_name = safe_snake_case(fname);
		p->number = (int32_t)pb_int(f, FD_NUMBER, 0);
		p->comment = get_comment(c->comments, fpath, np + 2, 4);
		p->field_type = field_type;
		p->field_wraps = field_wraps;
		p->map_types[0] = map_types[0];
		p->map_types[1] = map_types[1];
		p->type = t;
		p->zero = zero;
		p->one_of = one_of;
	}
	vec_push(&o->messages, m);
	if (!map_has(&o->message_by_name, name))
		map_put(&o->message_by_name, name, m);
}

static void process_enum(Ctx *c, const PbMsg *item, const char *name, const int64_t *path, int np)
{
	EnumData *e = xalloc(sizeof *e);
	e->py_name = sc_pascalcase(name);
	e->comment = get_comment(c->comments, path, np, 4);
	int nv = pb_count(item, EDP_VALUE);
	e->entries = xalloc(sizeof *e->entries * (size_t)(nv + 1));
	int64_t *vpath = xalloc(sizeof *vpath * (size_t)(np + 3));
	memcpy(vpath, path, sizeof *vpath * (size_t)np);
	for (int i = 0; i < nv; i++) {
		PbMsg *v = pb_msg_at(item, EDP_VALUE, i);
		vpath[np] = 2;
		vpath[np + 1] = i;
		e->entries[e->n++] = (Entry){
			ident(pb_bytes(v, EVDP_NAME), "enum value name"),
			(int32_t)pb_int(v, EVDP_NUMBER, 0),
			get_comment(c->comments, vpath, np + 2, 4),
		};
	}
	vec_push(&c->o->enums, e);
}

/* _traverse(path, items, prefix) over messages, with the consumer loop of generate_code. */
static void traverse_messages(Ctx *c, const PbMsg *parent, uint32_t field, const int64_t *path, int np,
			      const char *prefix, int depth)
{
	if (depth > 100)
		fail("RecursionError: maximum recursion depth exceeded");
	int n = pb_count(parent, field);
	int64_t *ipath = xalloc(sizeof *ipath * (size_t)(np + 3));
	memcpy(ipath, path, sizeof *ipath * (size_t)np);
	for (int i = 0; i < n; i++) {
		PbMsg *item = pb_msg_at(parent, field, i);
		const char *name = xcat(prefix, ident(pb_bytes(item, DP_NAME), "message name"));
		ipath[np] = i;
		if (!pb_int(pb_msg(item, DP_OPTIONS), 7, 0)) /* map_entry */
			process_message(c, item, name, ipath, np + 1);
		/* the enums of the message: path + [i, 4] */
		int ne = pb_count(item, DP_ENUM_TYPE);
		ipath[np + 1] = 4;
		for (int j = 0; j < ne; j++) {
			PbMsg *en = pb_msg_at(item, DP_ENUM_TYPE, j);
			process_enum(c, en, xcat(name, ident(pb_bytes(en, EDP_NAME), "enum name")), ipath, np + 2);
		}
		if (pb_count(item, DP_NESTED_TYPE)) {
			ipath[np + 1] = 3;
			traverse_messages(c, item, DP_NESTED_TYPE, ipath, np + 2, name, depth + 1);
		}
	}
}

static COLD void process_file(Output *o, const PbMsg *file)
{
	Comments comments = {0};
	comments_init(&comments, file);
	Ctx c = {o->package, o, &comments};
	int64_t path[2] = {5, 0};
	int ne = pb_count(file, FDP_ENUM_TYPE);
	for (int i = 0; i < ne; i++) {
		PbMsg *en = pb_msg_at(file, FDP_ENUM_TYPE, i);
		path[1] = i;
		process_enum(&c, en, ident(pb_bytes(en, EDP_NAME), "enum name"), path, 2);
	}
	int64_t mpath[1] = {4};
	traverse_messages(&c, file, FDP_MESSAGE_TYPE, mpath, 1, "", 0);

	int ns = pb_count(file, FDP_SERVICE);
	for (int i = 0; i < ns; i++) {
		PbMsg *svc = pb_msg_at(file, FDP_SERVICE, i);
		const char *sname = ident(pb_bytes(svc, SDP_NAME), "service name");
		Service *s = xalloc(sizeof *s);
		s->py_name = xcat(sc_pascalcase(sname), "Stub");
		int64_t spath[4] = {6, i, 2, 0};
		s->comment = get_comment(&comments, spath, 2, 4);
		int nm = pb_count(svc, SDP_METHOD);
		s->methods = xalloc(sizeof *s->methods * (size_t)(nm + 1));
		for (int j = 0; j < nm; j++) {
			PbMsg *m = pb_msg_at(svc, SDP_METHOD, j);
			if (pb_int(m, MDP_CLIENT_STREAMING, 0))
				fail("NotImplementedError: Client streaming not yet supported");
			const char *mname = ident(pb_bytes(m, MDP_NAME), "method name");
			const char *in_raw = text(pb_bytes(m, MDP_INPUT_TYPE), "input type");
			Ty *input_type = ty_strip_quotes(get_ref_type(o->package, o, in_raw, true));
			MsgData *input_message = NULL;
			if ((input_message = map_get(&o->message_by_name, ty_str(input_type)))) {
				for (int k = 0; k < input_message->nprops; k++)
					if (!strcmp(input_message->props[k].zero, "None"))
						set_add(&o->typing_imports, &o->typing_list, "Optional", (void *)1);
			}
			Method *me = &s->methods[s->n++];
			me->py_name = sc_snakecase(mname);
			spath[2] = 2;
			spath[3] = j;
			me->comment = get_comment(&comments, spath, 4, 8);
			me->route = xfmt("/%s.%s/%s", o->package, sname, mname);
			me->input = ty_strip_quotes(get_ref_type(o->package, o, in_raw, true));
			me->input_message = input_message;
			me->output = ty_strip_quotes(
				get_ref_type(o->package, o, text(pb_bytes(m, MDP_OUTPUT_TYPE), "output type"), false));
			me->server_streaming = pb_int(m, MDP_SERVER_STREAMING, 0) != 0;
			if (me->server_streaming)
				set_add(&o->typing_imports, &o->typing_list, "AsyncGenerator", (void *)1);
		}
		vec_push(&o->services, s);
	}
}
