/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* A port of the parts of com.google.protobuf.Descriptors (protobuf-java 3.25.8,
 * java/core/src/main/java/com/google/protobuf/Descriptors.java) that the JVM plugins use (ScalaPB):
 * building FileDescriptors from FileDescriptorProtos (translation, then cross-linking), the
 * descriptor pools with their validation, and default values (with TextFormat.parseInteger and
 * unescapeBytes from TextFormat.java). protocgen.CodeGenRequest builds the files of the request in
 * order, each from its already built dependencies.
 *
 * Errors are reported like CodeGenApp does for any exception: the response error is the stack
 * trace, of which only the first line (the exception) is reproduced. */
#include "protobuf_java.h"

#include <stdlib.h>
#include <string.h>

NORETURN COLD static void jvalidation_error(const char *symbol, const char *desc)
{
	fail("com.google.protobuf.Descriptors$DescriptorValidationException: %s: %s", symbol, desc);
}

/* A string field as Java sees it (see java_cstr). */
const char *jstr(const PbMsg *m, uint32_t num) { return java_cstr(pb_bytes(m, num)); }
const char *jstr_at(const PbMsg *m, uint32_t num, int i) { return java_cstr(pb_bytes_at(m, num, i)); }

int java_type_of(int type)
{
	switch (type) {
	case PT_INT32: case PT_UINT32: case PT_SINT32: case PT_FIXED32: case PT_SFIXED32: return JT_INT;
	case PT_INT64: case PT_UINT64: case PT_SINT64: case PT_FIXED64: case PT_SFIXED64: return JT_LONG;
	case PT_FLOAT: return JT_FLOAT;
	case PT_DOUBLE: return JT_DOUBLE;
	case PT_BOOL: return JT_BOOLEAN;
	case PT_STRING: return JT_STRING;
	case PT_BYTES: return JT_BYTE_STRING;
	case PT_ENUM: return JT_ENUM;
	default: return JT_MESSAGE; /* message, group */
	}
}

/* WireFormat.FieldType.isPackable: everything but string, group, message and bytes. */
bool jfield_is_packable(const JField *f)
{
	return jfield_is_repeated(f) && f->type != PT_STRING && f->type != PT_GROUP && f->type != PT_MESSAGE &&
	       f->type != PT_BYTES;
}

bool jfield_is_packed(const JField *f)
{
	if (!jfield_is_packable(f))
		return false;
	PbMsg *o = jfield_options(f);
	if (f->file->syntax == SYN_PROTO2)
		return jpb_bool(o, FO_PACKED, false);
	return !pb_has(o, FO_PACKED) || jpb_bool(o, FO_PACKED, false);
}

JField *jmsg_find_field_by_name(const JMsg *m, const char *name)
{
	for (int i = 0; i < m->fields.n; i++)
		if (str_eq(VEC_AT(m->fields, JField, i)->name, name))
			return VEC_AT(m->fields, JField, i);
	return NULL;
}

/* ---- pools ---- */

/* FNV-1a, extended by n bytes */
uint64_t jhash_add(uint64_t h, const char *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		h = (h ^ (unsigned char)p[i]) * 0x100000001b3u;
	return h;
}

/* The entry of the key p1 + p2 (two pieces, n1 + n2 bytes, hash h), or NULL. */
static JSymEntry *jsymtab_slot(const JSymTab *t, const char *p1, size_t n1, const char *p2, size_t n2, uint64_t h)
{
	if (!t->cap)
		return NULL;
	for (size_t i = (size_t)h & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
		JSymEntry *e = &t->e[i];
		if (!e->sym)
			return e;
		if (e->hash == h && e->len == n1 + n2 && !memcmp(e->key, p1, n1) && (!n2 || !memcmp(e->key + n1, p2, n2)))
			return e;
	}
}

JSym *jsymtab_get(const JSymTab *t, const char *p1, size_t n1, const char *p2, size_t n2, uint64_t h)
{
	JSymEntry *e = jsymtab_slot(t, p1, n1, p2, n2, h);
	return e ? e->sym : NULL;
}

void jsymtab_put(JSymTab *t, const char *key, size_t len, uint64_t h, JSym *sym)
{
	if (2 * (t->n + 1) > t->cap) {
		JSymTab g = {xalloc(sizeof(JSymEntry) * (t->cap ? 2 * t->cap : 16)), t->cap ? 2 * t->cap : 16, 0};
		for (size_t i = 0; i < t->cap; i++)
			if (t->e[i].sym)
				*jsymtab_slot(&g, t->e[i].key, t->e[i].len, NULL, 0, t->e[i].hash) = t->e[i], g.n++;
		*t = g;
	}
	JSymEntry *e = jsymtab_slot(t, key, len, NULL, 0, h);
	if (!e->sym)
		t->n++;
	*e = (JSymEntry){key, len, h, sym};
}

static bool jsym_is_type(const JSym *s) { return s->kind == SYM_MESSAGE || s->kind == SYM_ENUM; }
static bool jsym_is_aggregate(const JSym *s)
{
	return s->kind == SYM_MESSAGE || s->kind == SYM_ENUM || s->kind == SYM_PACKAGE || s->kind == SYM_SERVICE;
}

enum { FILTER_TYPES_ONLY, FILTER_AGGREGATES_ONLY, FILTER_ALL };

static bool jsym_matches(const JSym *s, int filter)
{
	return filter == FILTER_ALL || (filter == FILTER_TYPES_ONLY && jsym_is_type(s)) ||
	       (filter == FILTER_AGGREGATES_ONLY && jsym_is_aggregate(s));
}

/* DescriptorPool.dependencies: the dependencies of a file and, after each one, its public
 * dependencies transitively (importPublicDependencies, a preorder walk that skips the files already
 * added). The walk runs at each use instead of storing the set per file, which would take memory
 * quadratic in the length of a chain of public imports; walks do not nest, so they share one
 * stack (reset per request by jpool_reset). */
typedef struct {
	JFile *f;
	int i;
} JPoolFrame;
static struct {
	JPoolFrame *v;
	int n, cap;
	unsigned epoch;
} jpool_walk;

/* protobuf-java 4.36.2 instead of 3.25.8 (jpool_use_v4) */
static bool jpool_v4;

void jpool_reset(void)
{
	memset(&jpool_walk, 0, sizeof jpool_walk);
	jpool_v4 = false;
}

void jpool_use_v4(void) { jpool_v4 = true; }

typedef struct {
	const JFile *f;
	int dep;
	unsigned epoch;
} JPoolIter;

static JPoolIter jpool_iter(const JFile *f)
{
	jpool_walk.n = 0;
	return (JPoolIter){f, 0, ++jpool_walk.epoch};
}

static JFile *jpool_visit(JPoolIter *it, JFile *d)
{
	if (d->pool_mark == it->epoch)
		return NULL;
	d->pool_mark = it->epoch;
	if (jpool_walk.n == jpool_walk.cap) {
		int nc = jpool_walk.cap ? 2 * jpool_walk.cap : 16;
		jpool_walk.v = xrealloc(jpool_walk.v, sizeof(JPoolFrame) * (size_t)jpool_walk.cap, sizeof(JPoolFrame) * (size_t)nc);
		jpool_walk.cap = nc;
	}
	jpool_walk.v[jpool_walk.n++] = (JPoolFrame){d, 0};
	return d;
}

static JFile *jpool_next(JPoolIter *it)
{
	for (;;) {
		JFile *d = NULL;
		if (jpool_walk.n) {
			JPoolFrame *top = &jpool_walk.v[jpool_walk.n - 1];
			if (top->i == top->f->public_deps.n) {
				jpool_walk.n--;
				continue;
			}
			d = jpool_visit(it, VEC_AT(top->f->public_deps, JFile, top->i++));
		} else {
			if (it->dep == it->f->deps.n)
				return NULL;
			d = jpool_visit(it, VEC_AT(it->f->deps, JFile, it->dep++));
		}
		if (d)
			return d;
	}
}

/* DescriptorPool.findSymbol of the name p1 + p2 with hash h. */
static JSym *jpool_find2(const JFile *f, const char *p1, size_t n1, const char *p2, size_t n2, uint64_t h, int filter)
{
	JSym *s = jsymtab_get(&f->syms, p1, n1, p2, n2, h);
	if (s && jsym_matches(s, filter))
		return s;
	JPoolIter it = jpool_iter(f);
	for (JFile *d; (d = jpool_next(&it));) {
		s = jsymtab_get(&d->syms, p1, n1, p2, n2, h);
		if (s && jsym_matches(s, filter))
			return s;
	}
	return NULL;
}

static JSym *jpool_find(const JFile *f, const char *full_name, int filter)
{
	size_t n = strlen(full_name);
	return jpool_find2(f, full_name, n, NULL, 0, jhash_add(JHASH0, full_name, n), filter);
}

/* DescriptorPool.lookupSymbol (without placeholders: allowUnknownDependencies is false). The scopes
 * tried are the prefixes of relative_to up to each of its dots, from the last one. */
static JSym *jpool_lookup(const JFile *f, const char *name, const char *relative_to, int filter)
{
	JSym *result;
	if (name[0] == '.') {
		result = jpool_find(f, name + 1, filter);
	} else {
		const char *dot = strchr(name, '.');
		size_t name_len = strlen(name), first_len = dot ? (size_t)(dot - name) : name_len;
		/* hashes of relative_to up to and including each dot */
		size_t rlen = strlen(relative_to), ndots = 0;
		for (size_t i = 0; i < rlen; i++)
			ndots += relative_to[i] == '.';
		size_t *dots = xalloc(sizeof(size_t) * (ndots + 1));
		uint64_t *hashes = xalloc(sizeof(uint64_t) * (ndots + 1));
		ndots = 0;
		uint64_t h = JHASH0;
		for (size_t i = 0; i < rlen; i++) {
			h = jhash_add(h, relative_to + i, 1);
			if (relative_to[i] == '.')
				dots[ndots] = i, hashes[ndots++] = h;
		}
		for (;;) {
			if (!ndots) {
				result = jpool_find(f, name, filter);
				break;
			}
			ndots--;
			size_t n1 = dots[ndots] + 1;
			result = jpool_find2(f, relative_to, n1, name, first_len, jhash_add(hashes[ndots], name, first_len),
					     FILTER_AGGREGATES_ONLY);
			if (result) {
				if (dot)
					result = jpool_find2(f, relative_to, n1, name, name_len, jhash_add(hashes[ndots], name, name_len),
							     filter);
				break;
			}
		}
	}
	if (!result)
		jvalidation_error(relative_to, xfmt("\"%s\" is not defined.", name));
	return result;
}

static void jpool_validate_name(const char *full_name, const char *name)
{
	if (!name[0])
		jvalidation_error(full_name, "Missing name.");
	for (int i = 0; name[i]; i++) {
		char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9' && i > 0))
			continue;
		jvalidation_error(full_name, xfmt("\"%s\" is not a valid identifier.", name));
	}
}

static void jpool_add_symbol(JFile *f, int kind, void *p, const char *full_name, const char *name)
{
	jpool_validate_name(full_name, name);
	size_t n = strlen(full_name);
	uint64_t h = jhash_add(JHASH0, full_name, n);
	JSym *old = jsymtab_get(&f->syms, full_name, n, NULL, 0, h);
	if (old) {
		if (old->file == f) {
			const char *dot = strrchr(full_name, '.');
			if (!dot)
				jvalidation_error(full_name, xfmt("\"%s\" is already defined.", full_name));
			jvalidation_error(full_name, xfmt("\"%s\" is already defined in \"%s\".", dot + 1,
							  xstrndup(full_name, (size_t)(dot - full_name))));
		}
		jvalidation_error(full_name, xfmt("\"%s\" is already defined in file \"%s\".", full_name, old->file->name));
	}
	JSym *s = xalloc(sizeof *s);
	s->kind = kind, s->p = p, s->file = f, s->name = name;
	jsymtab_put(&f->syms, full_name, n, h, s);
}

/* DescriptorPool.addPackage: recursive over the prefixes up to each dot, shortest first. */
static void jpool_add_package(JFile *pool, const char *full_name, JFile *file)
{
	uint64_t h = JHASH0;
	size_t start = 0; /* of the last component */
	for (size_t i = 0;; i++) {
		if (full_name[i] == '.' || !full_name[i]) {
			JSym *old = jsymtab_get(&pool->syms, full_name, i, NULL, 0, h);
			const char *name = NULL;
			if (!old || old->kind != SYM_PACKAGE)
				name = xstrndup(full_name + start, i - start);
			if (old && old->kind != SYM_PACKAGE)
				jvalidation_error(file->name,
						  xfmt("\"%s\" is already defined (as something other than a package) in file \"%s\".",
						       name, old->file->name));
			if (!old) {
				JSym *s = xalloc(sizeof *s);
				s->kind = SYM_PACKAGE, s->file = file, s->name = name;
				jsymtab_put(&pool->syms, full_name, i, h, s);
			}
			if (!full_name[i])
				break;
			start = i + 1;
		}
		h = jhash_add(h, full_name + i, 1);
	}
}

/* ---- translation ---- */

static const char *jfull_name(JFile *file, JMsg *parent, const char *name)
{
	if (parent)
		return xcat(parent->full_name, ".", name);
	if (file->package[0])
		return xcat(file->package, ".", name);
	return name;
}

static JEnum *jenum_new(PbMsg *proto, JFile *file, JMsg *parent, int index)
{
	JEnum *e = xalloc(sizeof *e);
	e->proto = proto, e->file = file, e->parent = parent, e->index = index;
	e->name = jstr(proto, EDP_NAME);
	e->full_name = jfull_name(file, parent, e->name);
	if (pb_count(proto, EDP_VALUE) == 0)
		jvalidation_error(e->full_name, "Enums must contain at least one value.");
	for (int i = 0; i < pb_count(proto, EDP_VALUE); i++) {
		JEnumValue *v = xalloc(sizeof *v);
		v->proto = pb_msg_at(proto, EDP_VALUE, i);
		v->name = jstr(v->proto, EVDP_NAME);
		v->number = (int32_t)pb_int(v->proto, EVDP_NUMBER, 0);
		v->index = i;
		v->type = e;
		v->full_name = xcat(e->full_name, ".", v->name);
		jpool_add_symbol(file, SYM_ENUM_VALUE, v, v->full_name, v->name);
		vec_push(&e->values, v);
	}
	jpool_add_symbol(file, SYM_ENUM, e, e->full_name, e->name);
	return e;
}

static JField *jfield_new(PbMsg *proto, JFile *file, JMsg *parent, int index, bool is_ext)
{
	JField *f = xalloc(sizeof *f);
	f->proto = proto, f->file = file, f->index = index, f->is_ext = is_ext;
	f->name = jstr(proto, FD_NAME);
	f->full_name = jfull_name(file, parent, f->name);
	f->number = (int32_t)pb_int(proto, FD_NUMBER, 0);
	f->label = (int)pb_int(proto, FD_LABEL, LABEL_OPTIONAL);
	f->has_type = pb_has(proto, FD_TYPE);
	f->type = (int)pb_int(proto, FD_TYPE, PT_DOUBLE);
	f->proto3_optional = jpb_bool(proto, FD_PROTO3_OPTIONAL, false);
	if (f->number <= 0)
		jvalidation_error(f->full_name, "Field numbers must be positive integers.");
	if (is_ext) {
		if (!pb_has(proto, FD_EXTENDEE))
			jvalidation_error(f->full_name, "FieldDescriptorProto.extendee not set for extension field.");
		f->ext_scope = parent;
		if (pb_has(proto, FD_ONEOF_INDEX))
			jvalidation_error(f->full_name, "FieldDescriptorProto.oneof_index set for extension field.");
	} else {
		if (pb_has(proto, FD_EXTENDEE))
			jvalidation_error(f->full_name, "FieldDescriptorProto.extendee set for non-extension field.");
		f->containing_type = parent;
		if (pb_has(proto, FD_ONEOF_INDEX)) {
			int64_t oi = pb_int(proto, FD_ONEOF_INDEX, 0);
			if (oi < 0 || oi >= parent->oneofs.n)
				jvalidation_error(f->full_name, xfmt("FieldDescriptorProto.oneof_index is out of range for type %s",
								     parent->name));
			f->oneof = VEC_AT(parent->oneofs, JOneof, (int)oi);
		}
	}
	jpool_add_symbol(file, SYM_FIELD, f, f->full_name, f->name);
	return f;
}

static JMsg *jmsg_new(PbMsg *proto, JFile *file, JMsg *parent, int index)
{
	JMsg *m = xalloc(sizeof *m);
	m->proto = proto, m->file = file, m->parent = parent, m->index = index;
	m->name = jstr(proto, DP_NAME);
	m->full_name = jfull_name(file, parent, m->name);
	for (int i = 0; i < pb_count(proto, DP_ONEOF_DECL); i++) {
		JOneof *o = xalloc(sizeof *o);
		o->proto = pb_msg_at(proto, DP_ONEOF_DECL, i);
		o->name = jstr(o->proto, ODP_NAME);
		o->full_name = jfull_name(file, m, o->name);
		o->containing = m;
		o->index = i;
		vec_push(&m->oneofs, o);
	}
	for (int i = 0; i < pb_count(proto, DP_NESTED_TYPE); i++)
		vec_push(&m->nested, jmsg_new(pb_msg_at(proto, DP_NESTED_TYPE, i), file, m, i));
	for (int i = 0; i < pb_count(proto, DP_ENUM_TYPE); i++)
		vec_push(&m->enums, jenum_new(pb_msg_at(proto, DP_ENUM_TYPE, i), file, m, i));
	for (int i = 0; i < pb_count(proto, DP_FIELD); i++)
		vec_push(&m->fields, jfield_new(pb_msg_at(proto, DP_FIELD, i), file, m, i, false));
	for (int i = 0; i < pb_count(proto, DP_EXTENSION); i++)
		vec_push(&m->exts, jfield_new(pb_msg_at(proto, DP_EXTENSION, i), file, m, i, true));
	for (int i = 0; i < m->fields.n; i++) {
		JField *f = VEC_AT(m->fields, JField, i);
		if (f->oneof)
			vec_push(&f->oneof->fields, f);
	}
	int synthetic = 0;
	for (int i = 0; i < m->oneofs.n; i++) {
		if (joneof_is_synthetic(VEC_AT(m->oneofs, JOneof, i)))
			synthetic++;
		else if (synthetic > 0)
			jvalidation_error(m->full_name, "Synthetic oneofs must come last.");
	}
	m->real_oneofs = m->oneofs.n - synthetic;
	jpool_add_symbol(file, SYM_MESSAGE, m, m->full_name, m->name);
	return m;
}

static JService *jservice_new(PbMsg *proto, JFile *file, int index)
{
	JService *s = xalloc(sizeof *s);
	s->proto = proto, s->file = file, s->index = index;
	s->name = jstr(proto, SDP_NAME);
	s->full_name = jfull_name(file, NULL, s->name);
	for (int i = 0; i < pb_count(proto, SDP_METHOD); i++) {
		JMethod *m = xalloc(sizeof *m);
		m->proto = pb_msg_at(proto, SDP_METHOD, i);
		m->name = jstr(m->proto, MDP_NAME);
		m->full_name = xcat(s->full_name, ".", m->name);
		m->service = s, m->file = file, m->index = i;
		jpool_add_symbol(file, SYM_METHOD, m, m->full_name, m->name);
		vec_push(&s->methods, m);
	}
	jpool_add_symbol(file, SYM_SERVICE, s, s->full_name, s->name);
	return s;
}

/* ---- default values ---- */

/* Long.parseLong / BigInteger(text, radix) for the digits of TextFormat.parseInteger: an optional
 * sign, then at least one digit of the radix. Returns false on a format error; *big is set when
 * the magnitude does not fit 64 bits. */
static bool jparse_digits(const char *s, int radix, bool *neg, uint64_t *mag, bool *big)
{
	*neg = false, *mag = 0, *big = false;
	if (*s == '-' || *s == '+')
		*neg = *s++ == '-';
	if (!*s)
		return false;
	for (; *s; s++) {
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			d = *s - 'A' + 10;
		else
			return false;
		if (d >= radix)
			return false;
		if (*mag > (UINT64_MAX - (uint64_t)d) / (uint64_t)radix)
			*big = true;
		*mag = *mag * (uint64_t)radix + (uint64_t)d;
	}
	return true;
}

/* TextFormat.parseInteger: false on NumberFormatException. */
static bool jparse_integer(const char *text, bool is_signed, bool is_long, int64_t *out)
{
	/* Long.parseLong and BigInteger take any Unicode digit (Character.digit) */
	for (const char *q = text; *q; q++)
		if ((unsigned char)*q >= 0x80)
			fail("protoc-gen-uni does not support non-ASCII characters in integer default values: \"%s\"", text);
	const char *p = text;
	bool negative = false;
	if (*p == '-') {
		if (!is_signed)
			return false;
		p++;
		negative = true;
	}
	int radix = 10;
	if (p[0] == '0' && p[1] == 'x') {
		p += 2;
		radix = 16;
	} else if (p[0] == '0') {
		radix = 8;
	}
	bool inner_neg, big;
	uint64_t mag;
	if (!jparse_digits(p, radix, &inner_neg, &mag, &big))
		return false;
	if (strlen(p) < 16) {
		/* Long.parseLong: within the long range */
		if (mag > (uint64_t)INT64_MAX + (inner_neg ? 1 : 0))
			return false;
		int64_t r = inner_neg ? (int64_t)(0 - mag) : (int64_t)mag;
		if (negative)
			r = (int64_t)(0 - (uint64_t)r);
		if (!is_long) {
			if (is_signed ? (r > INT32_MAX || r < INT32_MIN) : (r >= ((int64_t)1 << 32) || r < 0))
				return false;
		}
		*out = r;
		return true;
	}
	/* BigInteger: bitLength of the signed value */
	if (big)
		return false;
	bool neg = inner_neg != negative;
	if (mag == 0)
		neg = false;
	/* bitLength: for v >= 0 the bits of v, for v < 0 the bits of -v - 1 */
	uint64_t x = neg ? mag - 1 : mag;
	int bits = 0;
	while (x) {
		bits++;
		x >>= 1;
	}
	int limit = !is_long ? (is_signed ? 31 : 32) : (is_signed ? 63 : 64);
	if (bits > limit)
		return false;
	*out = neg ? (int64_t)(0 - mag) : (int64_t)mag;
	return true;
}

static bool jis_octal(char c) { return c >= '0' && c <= '7'; }
static bool jis_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int jdigit_value(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c - 'A' + 10;
}

static void jput_utf8(Buf *b, uint32_t cp)
{
	if (cp < 0x80) {
		buf_addc(b, (char)cp);
	} else if (cp < 0x800) {
		buf_addc(b, (char)(0xc0 | cp >> 6));
		buf_addc(b, (char)(0x80 | (cp & 0x3f)));
	} else if (cp < 0x10000) {
		buf_addc(b, (char)(0xe0 | cp >> 12));
		buf_addc(b, (char)(0x80 | ((cp >> 6) & 0x3f)));
		buf_addc(b, (char)(0x80 | (cp & 0x3f)));
	} else {
		buf_addc(b, (char)(0xf0 | cp >> 18));
		buf_addc(b, (char)(0x80 | ((cp >> 12) & 0x3f)));
		buf_addc(b, (char)(0x80 | ((cp >> 6) & 0x3f)));
		buf_addc(b, (char)(0x80 | (cp & 0x3f)));
	}
}

/* TextFormat.unescapeBytes; returns NULL (with *err) on InvalidEscapeSequenceException. */
static bool junescape_bytes(Str in, Str *out, const char **err)
{
	Buf r = {0};
	for (size_t i = 0; i < in.n; i++) {
		char c = in.p[i];
		if (c != '\\') {
			buf_addc(&r, c);
			continue;
		}
		if (i + 1 >= in.n) {
			*err = "Invalid escape sequence: '\\' at end of string.";
			return false;
		}
		c = in.p[++i];
		if (jis_octal(c)) {
			int code = jdigit_value(c);
			if (i + 1 < in.n && jis_octal(in.p[i + 1]))
				code = code * 8 + jdigit_value(in.p[++i]);
			if (i + 1 < in.n && jis_octal(in.p[i + 1]))
				code = code * 8 + jdigit_value(in.p[++i]);
			buf_addc(&r, (char)code);
			continue;
		}
		switch (c) {
		case 'a': buf_addc(&r, 0x07); break;
		case 'b': buf_addc(&r, '\b'); break;
		case 'f': buf_addc(&r, '\f'); break;
		case 'n': buf_addc(&r, '\n'); break;
		case 'r': buf_addc(&r, '\r'); break;
		case 't': buf_addc(&r, '\t'); break;
		case 'v': buf_addc(&r, 0x0b); break;
		case '\\': buf_addc(&r, '\\'); break;
		case '\'': buf_addc(&r, '\''); break;
		case '"': buf_addc(&r, '"'); break;
		case '?': buf_addc(&r, '?'); break;
		case 'x': {
			int code;
			if (i + 1 < in.n && jis_hex(in.p[i + 1])) {
				code = jdigit_value(in.p[++i]);
			} else {
				*err = "Invalid escape sequence: '\\x' with no digits";
				return false;
			}
			if (i + 1 < in.n && jis_hex(in.p[i + 1]))
				code = code * 16 + jdigit_value(in.p[++i]);
			buf_addc(&r, (char)code);
			break;
		}
		case 'u':
			i++;
			if (i + 3 < in.n && jis_hex(in.p[i]) && jis_hex(in.p[i + 1]) && jis_hex(in.p[i + 2]) &&
			    jis_hex(in.p[i + 3])) {
				uint32_t ch = (uint32_t)(jdigit_value(in.p[i]) << 12 | jdigit_value(in.p[i + 1]) << 8 |
							 jdigit_value(in.p[i + 2]) << 4 | jdigit_value(in.p[i + 3]));
				if (ch >= 0xd800 && ch <= 0xdfff) {
					*err = "Invalid escape sequence: '\\u' refers to a surrogate";
					return false;
				}
				jput_utf8(&r, ch);
				i += 3;
			} else {
				*err = "Invalid escape sequence: '\\u' with too few hex chars";
				return false;
			}
			break;
		case 'U': {
			i++;
			if (i + 7 >= in.n) {
				*err = "Invalid escape sequence: '\\U' with too few hex chars";
				return false;
			}
			uint32_t cp = 0;
			for (size_t k = i; k < i + 8; k++) {
				if (!jis_hex(in.p[k])) {
					*err = "Invalid escape sequence: '\\U' with too few hex chars";
					return false;
				}
				cp = cp << 4 | (uint32_t)jdigit_value(in.p[k]);
			}
			if (cp > 0x10ffff) {
				*err = xfmt("Invalid escape sequence: '\\U%s' is not a valid code point value", xstrndup(in.p + i, 8));
				return false;
			}
			if (cp >= 0xd800 && cp <= 0xdfff) {
				*err = xfmt("Invalid escape sequence: '\\U%s' refers to a surrogate code unit", xstrndup(in.p + i, 8));
				return false;
			}
			jput_utf8(&r, cp);
			i += 7;
			break;
		}
		default:
			*err = xfmt("Invalid escape sequence: '\\%c'", c);
			return false;
		}
	}
	*out = buf_view(&r);
	if (!out->p)
		*out = (Str){"", 0};
	return true;
}

/* Java's FloatingDecimal grammar for Float.valueOf / Double.valueOf (decimal and hexadecimal
 * forms, "NaN", "Infinity", optional f/F/d/D suffix, surrounding whitespace trimmed). Returns the
 * text for strtod/strtof, or NULL on NumberFormatException. */
static const char *jfloat_text(const char *s)
{
	size_t n = strlen(s);
	while (n && (unsigned char)s[n - 1] <= ' ')
		n--;
	while (n && (unsigned char)*s <= ' ')
		s++, n--;
	char *t = xstrndup(s, n);
	const char *p = t;
	if (*p == '+' || *p == '-')
		p++;
	if (str_eq(p, "NaN"))
		return "nan";
	if (str_eq(p, "Infinity"))
		return t[0] == '-' ? "-inf" : "inf";
	bool hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
	if (hex)
		p += 2;
	int digits = 0;
	while (hex ? jis_hex(*p) : (*p >= '0' && *p <= '9'))
		p++, digits++;
	if (*p == '.') {
		p++;
		while (hex ? jis_hex(*p) : (*p >= '0' && *p <= '9'))
			p++, digits++;
	}
	if (!digits)
		return NULL;
	if (hex ? (*p == 'p' || *p == 'P') : (*p == 'e' || *p == 'E')) {
		p++;
		if (*p == '+' || *p == '-')
			p++;
		if (!(*p >= '0' && *p <= '9'))
			return NULL;
		while (*p >= '0' && *p <= '9')
			p++;
	} else if (hex) {
		return NULL; /* the binary exponent is required */
	}
	char *end = (char *)p;
	if (*p == 'f' || *p == 'F' || *p == 'd' || *p == 'D')
		p++;
	if (*p)
		return NULL;
	*end = 0;
	return t;
}

static void jfield_default(JField *f)
{
	PbMsg *proto = f->proto;
	if (!pb_has(proto, FD_DEFAULT_VALUE)) {
		if (jfield_is_repeated(f))
			return;
		if (f->type == PT_ENUM)
			f->def_e = VEC_AT(f->enum_type->values, JEnumValue, 0);
		else if (f->type == PT_STRING || f->type == PT_BYTES)
			f->def_s = (Str){"", 0};
		return;
	}
	if (jfield_is_repeated(f))
		jvalidation_error(f->full_name, "Repeated fields cannot have default values.");
	/* getDefaultValue(): the decoded string; string and bytes defaults may contain U+0000 */
	Str dvs = java_string(pb_bytes(proto, FD_DEFAULT_VALUE));
	const char *dv = f->type == PT_STRING || f->type == PT_BYTES ? "" : java_cstr(dvs);
	bool ok = true;
	switch (f->type) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32:
		ok = jparse_integer(dv, true, false, &f->def_i);
		break;
	case PT_UINT32: case PT_FIXED32:
		ok = jparse_integer(dv, false, false, &f->def_i);
		f->def_i = (int32_t)(uint32_t)f->def_i;
		break;
	case PT_INT64: case PT_SINT64: case PT_SFIXED64:
		ok = jparse_integer(dv, true, true, &f->def_i);
		break;
	case PT_UINT64: case PT_FIXED64:
		ok = jparse_integer(dv, false, true, &f->def_i);
		break;
	case PT_FLOAT: case PT_DOUBLE: {
		const char *t = str_eq(dv, "inf") ? "inf" : str_eq(dv, "-inf") ? "-inf" : str_eq(dv, "nan") ? "nan" : NULL;
		if (!t)
			t = jfloat_text(dv);
		if (!t) {
			ok = false;
			break;
		}
		if (f->type == PT_FLOAT)
			f->def_d = (double)strtof(t, NULL);
		else
			f->def_d = strtod(t, NULL);
		break;
	}
	case PT_BOOL: {
		/* Boolean.valueOf: "true" in any case */
		const char *s = dv;
		f->def_i = strlen(s) == 4 && (s[0] | 0x20) == 't' && (s[1] | 0x20) == 'r' && (s[2] | 0x20) == 'u' &&
			   (s[3] | 0x20) == 'e';
		break;
	}
	case PT_STRING:
		f->def_s = dvs;
		break;
	case PT_BYTES: {
		const char *err = NULL;
		if (!junescape_bytes(dvs, &f->def_s, &err))
			jvalidation_error(f->full_name, xfmt("Couldn't parse default value: %s", err));
		break;
	}
	case PT_ENUM:
		for (int i = 0; i < f->enum_type->values.n; i++) {
			JEnumValue *v = VEC_AT(f->enum_type->values, JEnumValue, i);
			if (str_eq(v->name, dv)) {
				f->def_e = v;
				break;
			}
		}
		if (!f->def_e)
			jvalidation_error(f->full_name, xfmt("Unknown enum default value: \"%s\"", dv));
		break;
	default:
		jvalidation_error(f->full_name, "Message type had default value.");
	}
	if (!ok)
		jvalidation_error(f->full_name, xfmt("Could not parse default value: \"%s\"", dv));
}

/* ---- cross-linking ---- */

static bool jmsg_is_extension_number(const JMsg *m, int number)
{
	for (int i = 0; i < pb_count(m->proto, DP_EXTENSION_RANGE); i++) {
		PbMsg *r = pb_msg_at(m->proto, DP_EXTENSION_RANGE, i);
		if (pb_int(r, ER_START, 0) <= number && number < pb_int(r, ER_END, 0))
			return true;
	}
	return false;
}

static void jfield_cross_link(JField *f)
{
	PbMsg *proto = f->proto;
	if (pb_has(proto, FD_EXTENDEE)) {
		JSym *s = jpool_lookup(f->file, jstr(proto, FD_EXTENDEE), f->full_name, FILTER_TYPES_ONLY);
		if (s->kind != SYM_MESSAGE)
			jvalidation_error(f->full_name, xfmt("\"%s\" is not a message type.", jstr(proto, FD_EXTENDEE)));
		f->containing_type = s->p;
		if (!jmsg_is_extension_number(f->containing_type, f->number))
			jvalidation_error(f->full_name, xfmt("\"%s\" does not declare %d as an extension number.",
							     f->containing_type->full_name, f->number));
	}
	if (pb_has(proto, FD_TYPE_NAME)) {
		JSym *s = jpool_lookup(f->file, jstr(proto, FD_TYPE_NAME), f->full_name, FILTER_TYPES_ONLY);
		if (!f->has_type) {
			if (s->kind == SYM_MESSAGE)
				f->type = PT_MESSAGE;
			else if (s->kind == SYM_ENUM)
				f->type = PT_ENUM;
			else
				jvalidation_error(f->full_name, xfmt("\"%s\" is not a type.", jstr(proto, FD_TYPE_NAME)));
			f->has_type = true;
		}
		if (jfield_java_type(f) == JT_MESSAGE) {
			if (s->kind != SYM_MESSAGE)
				jvalidation_error(f->full_name, xfmt("\"%s\" is not a message type.", jstr(proto, FD_TYPE_NAME)));
			f->msg_type = s->p;
			if (pb_has(proto, FD_DEFAULT_VALUE))
				jvalidation_error(f->full_name, "Messages can't have default values.");
		} else if (jfield_java_type(f) == JT_ENUM) {
			if (s->kind != SYM_ENUM)
				jvalidation_error(f->full_name, xfmt("\"%s\" is not an enum type.", jstr(proto, FD_TYPE_NAME)));
			f->enum_type = s->p;
		} else {
			jvalidation_error(f->full_name, "Field with primitive type has type_name.");
		}
	} else if (jfield_java_type(f) == JT_MESSAGE || jfield_java_type(f) == JT_ENUM) {
		jvalidation_error(f->full_name, "Field with message or enum type missing type_name.");
	}
	if (jpb_bool(jfield_options(f), FO_PACKED, false) && !jfield_is_packable(f))
		jvalidation_error(f->full_name, "[packed = true] can only be specified for repeated primitive fields.");
	jfield_default(f);
	/* 4.x checks extensions of MessageSets in validateFeatures (features.c) */
	if (!jpool_v4 && f->containing_type && jpb_bool(pb_msg(f->containing_type->proto, DP_OPTIONS), 1, false)) {
		if (f->is_ext) {
			if (!jfield_is_optional(f) || f->type != PT_MESSAGE)
				jvalidation_error(f->full_name, "Extensions of MessageSets must be optional messages.");
		} else {
			jvalidation_error(f->full_name, "MessageSets cannot have fields, only extensions.");
		}
	}
}

int jfield_cmp_number(const void *a, const void *b)
{
	int x = ((const JField *)a)->number, y = ((const JField *)b)->number;
	return x < y ? -1 : x > y;
}

static void jmsg_cross_link(JMsg *m)
{
	for (int i = 0; i < m->nested.n; i++)
		jmsg_cross_link(VEC_AT(m->nested, JMsg, i));
	for (int i = 0; i < m->fields.n; i++)
		jfield_cross_link(VEC_AT(m->fields, JField, i));
	int n = m->fields.n;
	m->by_number = xalloc(sizeof(JField *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		m->by_number[i] = VEC_AT(m->fields, JField, i);
	java_sort((void **)m->by_number, n, jfield_cmp_number);
	for (int i = 0; i + 1 < n; i++) {
		JField *old = m->by_number[i], *f = m->by_number[i + 1];
		if (old->number == f->number)
			jvalidation_error(f->full_name, xfmt("Field number %d has already been used in \"%s\" by field \"%s\".",
							     f->number, f->containing_type->full_name, old->name));
	}
	for (int i = 0; i < m->exts.n; i++)
		jfield_cross_link(VEC_AT(m->exts, JField, i));
}

/* FileDescriptor.buildFrom(proto, deps). byname: the files built so far. */
JFile *jfile_build(PbMsg *proto, Map *byname)
{
	JFile *f = xalloc(sizeof *f);
	f->proto = proto;
	f->name = jstr(proto, FDP_NAME);
	f->package = jstr(proto, FDP_PACKAGE);
	const char *syntax = jstr(proto, FDP_SYNTAX);
	f->syntax = str_eq(syntax, "proto3") ? SYN_PROTO3 : str_eq(syntax, "editions") ? SYN_EDITIONS : SYN_PROTO2;
	for (int i = 0; i < pb_count(proto, FDP_DEPENDENCY); i++) {
		const char *dn = jstr_at(proto, FDP_DEPENDENCY, i);
		JFile *d = map_get(byname, dn);
		if (!d)
			fail("java.util.NoSuchElementException: key not found: %s", dn);
		vec_push(&f->deps, d);
	}
	/* DescriptorPool: the dependencies and their public dependencies, with their packages */
	JPoolIter it = jpool_iter(f);
	for (JFile *d; (d = jpool_next(&it));)
		jpool_add_package(f, d->package, d);
	for (int i = 0; i < pb_count(proto, FDP_PUBLIC_DEPENDENCY); i++) {
		int64_t idx = pb_int_at(proto, FDP_PUBLIC_DEPENDENCY, i);
		if (idx < 0 || idx >= pb_count(proto, FDP_DEPENDENCY))
			jvalidation_error(f->name, "Invalid public dependency index.");
		/* nameToFileMap.get(proto.getDependency(index)): the dependencies were looked up by these
		 * names, so this is always found */
		vec_push(&f->public_deps, f->deps.v[idx]);
	}
	jpool_add_package(f, f->package, f);
	for (int i = 0; i < pb_count(proto, FDP_MESSAGE_TYPE); i++)
		vec_push(&f->msgs, jmsg_new(pb_msg_at(proto, FDP_MESSAGE_TYPE, i), f, NULL, i));
	for (int i = 0; i < pb_count(proto, FDP_ENUM_TYPE); i++)
		vec_push(&f->enums, jenum_new(pb_msg_at(proto, FDP_ENUM_TYPE, i), f, NULL, i));
	for (int i = 0; i < pb_count(proto, FDP_SERVICE); i++)
		vec_push(&f->services, jservice_new(pb_msg_at(proto, FDP_SERVICE, i), f, i));
	for (int i = 0; i < pb_count(proto, FDP_EXTENSION); i++)
		vec_push(&f->exts, jfield_new(pb_msg_at(proto, FDP_EXTENSION, i), f, NULL, i, true));
	/* crossLink */
	for (int i = 0; i < f->msgs.n; i++)
		jmsg_cross_link(VEC_AT(f->msgs, JMsg, i));
	for (int i = 0; i < f->services.n; i++) {
		JService *s = VEC_AT(f->services, JService, i);
		for (int j = 0; j < s->methods.n; j++) {
			JMethod *m = VEC_AT(s->methods, JMethod, j);
			JSym *in = jpool_lookup(f, jstr(m->proto, MDP_INPUT_TYPE), m->full_name, FILTER_TYPES_ONLY);
			if (in->kind != SYM_MESSAGE)
				jvalidation_error(m->full_name, xfmt("\"%s\" is not a message type.", jstr(m->proto, MDP_INPUT_TYPE)));
			m->input = in->p;
			JSym *out = jpool_lookup(f, jstr(m->proto, MDP_OUTPUT_TYPE), m->full_name, FILTER_TYPES_ONLY);
			if (out->kind != SYM_MESSAGE)
				jvalidation_error(m->full_name, xfmt("\"%s\" is not a message type.", jstr(m->proto, MDP_OUTPUT_TYPE)));
			m->output = out->p;
		}
	}
	for (int i = 0; i < f->exts.n; i++)
		jfield_cross_link(VEC_AT(f->exts, JField, i));
	if (jpool_v4)
		jfile_resolve_features(f);
	return f;
}
