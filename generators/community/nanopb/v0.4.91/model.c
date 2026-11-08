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

/* The data nanopb_generator.py works on: Python values it mixes (Names, str), NanoPBOptions of
 * generator/proto/nanopb.proto with the protobuf message semantics the generator relies on
 * (HasField, CopyFrom, MergeFrom, field defaults), and a view of the descriptors of the request
 * (descriptor_pb2 as python-protobuf parses it: closed enums, defaults of unset fields). */

/* ---- Names (class Names) ---- */

/* A tuple of name parts; Names(('',)) is normalized to Names(()). */
typedef struct {
	const char **p;
	int n;
} Names;

static Names names_make(const char **p, int n)
{
	if (n == 1 && !*p[0])
		n = 0;
	return (Names){p, n};
}

static Names names_of(const char *s)
{
	const char **p = xalloc(sizeof *p);
	p[0] = s;
	return names_make(p, 1);
}

/* Names + str */
static Names names_add(Names a, const char *s)
{
	const char **p = xalloc(sizeof *p * (size_t)(a.n + 1));
	for (int i = 0; i < a.n; i++)
		p[i] = a.p[i];
	p[a.n] = s;
	return names_make(p, a.n + 1);
}

/* Names + Names */
static Names names_cat(Names a, Names b)
{
	const char **p = xalloc(sizeof *p * (size_t)(a.n + b.n + 1));
	for (int i = 0; i < a.n; i++)
		p[i] = a.p[i];
	for (int i = 0; i < b.n; i++)
		p[a.n + i] = b.p[i];
	return names_make(p, a.n + b.n);
}

/* Names(s.split(sep)) */
static Names names_split(const char *s, char sep)
{
	Vec v = str_split(s, sep);
	return names_make((const char **)v.v, v.n);
}

/* str(names): '_'.join(parts) */
static char *names_str(Names a)
{
	Buf b = {0};
	for (int i = 0; i < a.n; i++) {
		if (i)
			buf_addc(&b, '_');
		buf_adds(&b, a.p[i]);
	}
	return (char *)buf_str(&b);
}

/* A Python value that is None, a str or a Names. */
enum { PV_NONE, PV_STR, PV_NAMES };
typedef struct {
	int kind;
	const char *s;
	Names nm;
} PyVal;

static PyVal pv_str(const char *s) { return (PyVal){PV_STR, s, {0}}; }
static PyVal pv_names(Names n) { return (PyVal){PV_NAMES, NULL, n}; }

/* str(v) */
static const char *pv_text(PyVal v)
{
	return v.kind == PV_STR ? v.s : v.kind == PV_NAMES ? names_str(v.nm) : "None";
}

/* bool(v): Names has no __len__, so it is always true. */
static bool pv_truthy(PyVal v)
{
	return v.kind == PV_NAMES || (v.kind == PV_STR && *v.s);
}

/* ---- the output of the generator: a stderr log and Python exceptions ---- */

/* sys.stderr: kept for the error of a failing run (an uncaught exception exits with status 1
 * after everything written to stderr). */
static Buf nb_stderr;
static bool nb_stderr_truncated;

static void nb_log(const char *s)
{
	if (nb_stderr.n > (16u << 20)) {
		nb_stderr_truncated = true;
		return;
	}
	buf_adds(&nb_stderr, s);
}

/* ---- NanoPBOptions (generator/proto/nanopb.proto) ---- */

enum { FT_DEFAULT = 0, FT_CALLBACK = 1, FT_STATIC = 2, FT_IGNORE = 3, FT_POINTER = 4, FT_INLINE = 5 };
enum { M_NONE = 0, M_STRIP_PACKAGE = 1, M_FLATTEN = 2, M_PACKAGE_INITIALS = 3 };
enum { DS_AUTO = 0 };

/* Field numbers of NanoPBOptions. */
enum {
	NO_MAX_SIZE = 1, NO_MAX_COUNT = 2, NO_TYPE = 3, NO_LONG_NAMES = 4, NO_PACKED_STRUCT = 5,
	NO_SKIP_MESSAGE = 6, NO_INT_SIZE = 7, NO_NO_UNIONS = 8, NO_MSGID = 9, NO_PACKED_ENUM = 10,
	NO_ANONYMOUS_ONEOF = 11, NO_PROTO3 = 12, NO_ENUM_TO_STRING = 13, NO_MAX_LENGTH = 14,
	NO_FIXED_LENGTH = 15, NO_FIXED_COUNT = 16, NO_MANGLE_NAMES = 17, NO_CALLBACK_DATATYPE = 18,
	NO_CALLBACK_FUNCTION = 19, NO_DESCRIPTORSIZE = 20, NO_PROTO3_SINGULAR_MSGS = 21,
	NO_SUBMSG_CALLBACK = 22, NO_DEFAULT_HAS = 23, NO_INCLUDE = 24, NO_PACKAGE = 25,
	NO_EXCLUDE = 26, NO_TYPE_OVERRIDE = 27, NO_SORT_BY_TAG = 28, NO_FALLBACK_TYPE = 29,
	NO_INITIALIZER = 30, NO_LABEL_OVERRIDE = 31, NO_ENUM_VALIDATE = 32,
	NO_DISCARD_UNUSED_AUTOMATIC_TYPES = 33, NO_ENUM_INTSIZE = 34, NO_DISCARD_DEPRECATED = 35,
	NO_LAST = 35,
};

/* The kinds of the NanoPBOptions fields, by number. */
enum { K_INT32 = 1, K_UINT32, K_BOOL, K_STRING, K_ENUM, K_REPSTR };
static const uint8_t no_kind[NO_LAST + 1] = {
	[NO_MAX_SIZE] = K_INT32, [NO_MAX_LENGTH] = K_INT32, [NO_MAX_COUNT] = K_INT32,
	[NO_INT_SIZE] = K_ENUM, [NO_ENUM_INTSIZE] = K_ENUM, [NO_TYPE] = K_ENUM,
	[NO_LONG_NAMES] = K_BOOL, [NO_PACKED_STRUCT] = K_BOOL, [NO_PACKED_ENUM] = K_BOOL,
	[NO_SKIP_MESSAGE] = K_BOOL, [NO_NO_UNIONS] = K_BOOL, [NO_MSGID] = K_UINT32,
	[NO_ANONYMOUS_ONEOF] = K_BOOL, [NO_PROTO3] = K_BOOL, [NO_PROTO3_SINGULAR_MSGS] = K_BOOL,
	[NO_ENUM_TO_STRING] = K_BOOL, [NO_ENUM_VALIDATE] = K_BOOL, [NO_FIXED_LENGTH] = K_BOOL,
	[NO_FIXED_COUNT] = K_BOOL, [NO_SUBMSG_CALLBACK] = K_BOOL, [NO_MANGLE_NAMES] = K_ENUM,
	[NO_CALLBACK_DATATYPE] = K_STRING, [NO_CALLBACK_FUNCTION] = K_STRING,
	[NO_DESCRIPTORSIZE] = K_ENUM, [NO_DEFAULT_HAS] = K_BOOL, [NO_INCLUDE] = K_REPSTR,
	[NO_EXCLUDE] = K_REPSTR, [NO_PACKAGE] = K_STRING, [NO_TYPE_OVERRIDE] = K_ENUM,
	[NO_LABEL_OVERRIDE] = K_ENUM, [NO_SORT_BY_TAG] = K_BOOL, [NO_FALLBACK_TYPE] = K_ENUM,
	[NO_INITIALIZER] = K_STRING, [NO_DISCARD_UNUSED_AUTOMATIC_TYPES] = K_BOOL,
	[NO_DISCARD_DEPRECATED] = K_BOOL,
};

/* A NanoPBOptions message: per field number whether it is set and its value (the default when
 * unset); repeated fields as string lists. */
typedef struct {
	uint64_t has;
	int64_t v[NO_LAST + 1];
	const char *s[NO_LAST + 1];
	Vec include, exclude;
} NbOpts;

static NbOpts *opts_new(void)
{
	NbOpts *o = xalloc(sizeof *o);
	o->v[NO_LONG_NAMES] = 1;
	o->v[NO_SORT_BY_TAG] = 1;
	o->v[NO_FALLBACK_TYPE] = FT_CALLBACK;
	o->v[NO_DISCARD_UNUSED_AUTOMATIC_TYPES] = 1;
	o->s[NO_CALLBACK_DATATYPE] = "pb_callback_t";
	o->s[NO_CALLBACK_FUNCTION] = "pb_default_field_callback";
	o->s[NO_PACKAGE] = "";
	o->s[NO_INITIALIZER] = "";
	return o;
}

static bool opts_has(const NbOpts *o, int num) { return (o->has >> num) & 1; }
static int64_t opt(const NbOpts *o, int num) { return o->v[num]; }

static void opts_set(NbOpts *o, int num, int64_t v)
{
	o->v[num] = v;
	o->has |= (uint64_t)1 << num;
}

static void opts_set_s(NbOpts *o, int num, const char *s)
{
	o->s[num] = s;
	o->has |= (uint64_t)1 << num;
}

static void vec_copy(Vec *dst, const Vec *src)
{
	Vec v = {0};
	for (int i = 0; i < src->n; i++)
		vec_push(&v, src->v[i]);
	*dst = v;
}

/* new = NanoPBOptions(); new.CopyFrom(o) */
static NbOpts *opts_copy(const NbOpts *o)
{
	NbOpts *c = xalloc(sizeof *c);
	*c = *o;
	vec_copy(&c->include, &o->include);
	vec_copy(&c->exclude, &o->exclude);
	return c;
}

/* dst.MergeFrom(src): set singular fields overwrite, repeated fields are appended. */
static void opts_merge(NbOpts *dst, const NbOpts *src)
{
	for (int num = 1; num <= NO_LAST; num++) {
		if (!opts_has(src, num))
			continue;
		dst->v[num] = src->v[num];
		dst->s[num] = src->s[num];
		dst->has |= (uint64_t)1 << num;
	}
	for (int i = 0; i < src->include.n; i++)
		vec_push(&dst->include, src->include.v[i]);
	for (int i = 0; i < src->exclude.n; i++)
		vec_push(&dst->exclude, src->exclude.v[i]);
}

/* Whether v is a value of the closed enum of field num. */
static bool enum_known(int num, int64_t v)
{
	switch (num) {
	case NO_TYPE: case NO_FALLBACK_TYPE: return v >= 0 && v <= 5;
	case NO_INT_SIZE: case NO_ENUM_INTSIZE: return v == 0 || v == 8 || v == 16 || v == 32 || v == 64;
	case NO_MANGLE_NAMES: return v >= 0 && v <= 3;
	case NO_DESCRIPTORSIZE: return v == 0 || v == 1 || v == 2 || v == 4 || v == 8;
	case NO_TYPE_OVERRIDE: return v >= 1 && v <= 18;
	default: return v >= 1 && v <= 3; /* NO_LABEL_OVERRIDE */
	}
}

/* A string of the request, as python-protobuf decodes it. */
static const char *req_text(Str s)
{
	if (s.n && memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: a NUL character in a name or string of the request");
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xfffd && w == 1)
			fail("google.protobuf.message.DecodeError: invalid UTF-8 in a string of the request");
		i += (size_t)w;
	}
	return s.p;
}

/* options.Extensions[nanopb_*] when the options have extension 1010 set, else NULL. */
static NbOpts *nanopb_ext(const PbMsg *options)
{
	if (!options)
		return NULL;
	PbReader r = pb_unknown_fields(options);
	PbRawField f;
	Buf payload = {0};
	bool found = false;
	while (pb_next_field(&r, &f))
		if (f.num == 1010 && f.wt == WT_LEN) {
			buf_add(&payload, f.val, f.vlen);
			found = true;
		}
	if (!found)
		return NULL;
	/* python-protobuf parses the extension: the last value of a field wins, repeated strings
	 * are appended, a value of the wrong wire type or an unknown enum value is an unknown
	 * field */
	NbOpts *o = opts_new();
	o->has = 0;
	PbReader pr = {(const uint8_t *)payload.p, (const uint8_t *)payload.p + payload.n};
	while (pb_next_field(&pr, &f)) {
		int num = f.num <= NO_LAST ? (int)f.num : 0, k = no_kind[num];
		if (!k || (f.wt == WT_LEN) != (k == K_STRING || k == K_REPSTR) || (f.wt != WT_LEN && f.wt != WT_VARINT))
			continue;
		Str sv = {(const char *)f.val, f.vlen};
		const char *text = k == K_STRING || k == K_REPSTR ? (req_text(sv), xstrndup(sv.p, sv.n)) : NULL;
		if (k == K_STRING)
			opts_set_s(o, num, text);
		else if (k == K_REPSTR)
			vec_push(num == NO_INCLUDE ? &o->include : &o->exclude, (void *)text);
		else if (k == K_UINT32)
			opts_set(o, num, (int64_t)(uint32_t)f.v);
		else if (k == K_BOOL)
			opts_set(o, num, f.v != 0);
		else if (k == K_INT32 || enum_known(num, (int32_t)f.v))
			opts_set(o, num, (int32_t)f.v);
	}
	if (pr.p != pr.end)
		fail("google.protobuf.message.DecodeError: Error parsing message");
	return o;
}

/* ---- descriptors ---- */

typedef struct {
	const char *name, *type_name, *extendee, *default_value;
	int32_t number, label, type, oneof_index;
	bool has_default_value, has_oneof_index, proto3_optional, deprecated;
	NbOpts *nanopb;
} FieldDesc;

typedef struct {
	const char *name;
	int n;
	const char **vnames;
	int32_t *vnums;
	NbOpts *nanopb;
} EnumDesc;

typedef struct MsgDesc MsgDesc;
struct MsgDesc {
	const char *name;
	FieldDesc *fields;
	int nfields;
	MsgDesc *nested;
	int nnested;
	EnumDesc *enums;
	int nenums;
	int32_t *range_start;
	int nranges;
	FieldDesc *exts;
	int nexts;
	const char **oneofs;
	int noneofs;
	bool map_entry, deprecated;
	NbOpts *nanopb;
};

typedef struct {
	const char *key; /* the path, e.g. "4,0,2,1" */
	const char *leading, *trailing;
} Location;

typedef struct {
	const char *name, *package, *syntax;
	const char **deps;
	int ndeps;
	MsgDesc *msgs;
	int nmsgs;
	EnumDesc *enums;
	int nenums;
	FieldDesc *exts;
	int nexts;
	NbOpts *nanopb;
	Map comments; /* path -> Location: the last location with comments */
} FileDesc;

static const char *pbs(const PbMsg *m, uint32_t num) { return req_text(pb_bytes(m, num)); }

static void read_field(FieldDesc *f, const PbMsg *m)
{
	f->name = pbs(m, FD_NAME);
	f->type_name = pbs(m, FD_TYPE_NAME);
	f->extendee = pbs(m, FD_EXTENDEE);
	f->default_value = pbs(m, FD_DEFAULT_VALUE);
	f->has_default_value = pb_has(m, FD_DEFAULT_VALUE);
	f->number = (int32_t)pb_int(m, FD_NUMBER, 0);
	f->label = (int32_t)pb_int(m, FD_LABEL, LABEL_OPTIONAL);
	f->type = (int32_t)pb_int(m, FD_TYPE, PT_DOUBLE);
	f->oneof_index = (int32_t)pb_int(m, FD_ONEOF_INDEX, 0);
	f->has_oneof_index = pb_has(m, FD_ONEOF_INDEX);
	f->proto3_optional = pb_int(m, FD_PROTO3_OPTIONAL, 0) != 0;
	PbMsg *o = pb_msg(m, FD_OPTIONS);
	f->deprecated = pb_int(o, FO_DEPRECATED, 0) != 0;
	f->nanopb = nanopb_ext(o);
}

static void read_enum(EnumDesc *e, const PbMsg *m)
{
	e->name = pbs(m, EDP_NAME);
	e->n = pb_count(m, EDP_VALUE);
	e->vnames = xalloc(sizeof *e->vnames * (size_t)(e->n + 1));
	e->vnums = xalloc(sizeof *e->vnums * (size_t)(e->n + 1));
	for (int i = 0; i < e->n; i++) {
		PbMsg *v = pb_msg_at(m, EDP_VALUE, i);
		e->vnames[i] = pbs(v, EVDP_NAME);
		e->vnums[i] = (int32_t)pb_int(v, EVDP_NUMBER, 0);
	}
	e->nanopb = nanopb_ext(pb_msg(m, EDP_OPTIONS));
}

static FieldDesc *read_fields(const PbMsg *m, uint32_t num, int *n)
{
	*n = pb_count(m, num);
	FieldDesc *f = xalloc(sizeof *f * (size_t)(*n + 1));
	for (int i = 0; i < *n; i++)
		read_field(&f[i], pb_msg_at(m, num, i));
	return f;
}

static EnumDesc *read_enums(const PbMsg *m, uint32_t num, int *n)
{
	*n = pb_count(m, num);
	EnumDesc *e = xalloc(sizeof *e * (size_t)(*n + 1));
	for (int i = 0; i < *n; i++)
		read_enum(&e[i], pb_msg_at(m, num, i));
	return e;
}

enum { MO_MAP_ENTRY = 7, MO_DEPRECATED = 3 };

static void read_message(MsgDesc *d, const PbMsg *m)
{
	d->name = pbs(m, DP_NAME);
	d->fields = read_fields(m, DP_FIELD, &d->nfields);
	d->nnested = pb_count(m, DP_NESTED_TYPE);
	d->nested = xalloc(sizeof *d->nested * (size_t)(d->nnested + 1));
	for (int i = 0; i < d->nnested; i++)
		read_message(&d->nested[i], pb_msg_at(m, DP_NESTED_TYPE, i));
	d->enums = read_enums(m, DP_ENUM_TYPE, &d->nenums);
	d->nranges = pb_count(m, DP_EXTENSION_RANGE);
	d->range_start = xalloc(sizeof *d->range_start * (size_t)(d->nranges + 1));
	for (int i = 0; i < d->nranges; i++)
		d->range_start[i] = (int32_t)pb_int(pb_msg_at(m, DP_EXTENSION_RANGE, i), ER_START, 0);
	d->exts = read_fields(m, DP_EXTENSION, &d->nexts);
	d->noneofs = pb_count(m, DP_ONEOF_DECL);
	d->oneofs = xalloc(sizeof *d->oneofs * (size_t)(d->noneofs + 1));
	for (int i = 0; i < d->noneofs; i++)
		d->oneofs[i] = pbs(pb_msg_at(m, DP_ONEOF_DECL, i), ODP_NAME);
	PbMsg *o = pb_msg(m, DP_OPTIONS);
	d->map_entry = pb_int(o, MO_MAP_ENTRY, 0) != 0;
	d->deprecated = pb_int(o, MO_DEPRECATED, 0) != 0;
	d->nanopb = nanopb_ext(o);
}

static FileDesc *read_file(const PbMsg *m)
{
	FileDesc *f = xalloc(sizeof *f);
	f->name = pbs(m, FDP_NAME);
	f->package = pbs(m, FDP_PACKAGE);
	f->syntax = pbs(m, FDP_SYNTAX);
	f->ndeps = pb_count(m, FDP_DEPENDENCY);
	f->deps = xalloc(sizeof *f->deps * (size_t)(f->ndeps + 1));
	for (int i = 0; i < f->ndeps; i++)
		f->deps[i] = req_text(pb_bytes_at(m, FDP_DEPENDENCY, i));
	f->nmsgs = pb_count(m, FDP_MESSAGE_TYPE);
	f->msgs = xalloc(sizeof *f->msgs * (size_t)(f->nmsgs + 1));
	for (int i = 0; i < f->nmsgs; i++)
		read_message(&f->msgs[i], pb_msg_at(m, FDP_MESSAGE_TYPE, i));
	f->enums = read_enums(m, FDP_ENUM_TYPE, &f->nenums);
	f->exts = read_fields(m, FDP_EXTENSION, &f->nexts);
	f->nanopb = nanopb_ext(pb_msg(m, FDP_OPTIONS));
	PbMsg *sci = pb_msg(m, FDP_SOURCE_CODE_INFO);
	for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
		PbMsg *l = pb_msg_at(sci, SCI_LOCATION, i);
		const char *lead = pbs(l, LOC_LEADING), *trail = pbs(l, LOC_TRAILING);
		bool detached = pb_count(l, LOC_LEADING_DETACHED) > 0;
		for (int k = 0; k < pb_count(l, LOC_LEADING_DETACHED); k++)
			req_text(pb_bytes_at(l, LOC_LEADING_DETACHED, k));
		if (!*lead && !*trail && !detached)
			continue;
		Buf key = {0};
		const PbSlot *s = pb_slot(l, LOC_PATH);
		for (int k = 0; s && k < s->n; k++)
			buf_fmt(&key, "%s%d", k ? "," : "", (int)(int32_t)s->v[k].u);
		Location *loc = xalloc(sizeof *loc);
		loc->key = buf_str(&key);
		loc->leading = lead;
		loc->trailing = trail;
		map_put(&f->comments, loc->key, loc);
	}
	return f;
}
