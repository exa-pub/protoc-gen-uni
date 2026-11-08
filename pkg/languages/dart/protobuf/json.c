/* GeneratedMessage.writeToJsonMap of package:protobuf (lib/src/protobuf/json/json.dart
 * writeToJsonMap; BSD-3-Clause, Copyright the Dart project authors) over pkg/pb messages. */
#include "dart_protobuf.h"

#include <math.h>
#include <string.h>

#include "dart_std.h"

static Jv *jv_new(JvKind kind)
{
	Jv *v = xalloc(sizeof *v);
	v->kind = kind;
	return v;
}

static void jv_add(Jv *c, const char *key, Jv *val)
{
	if (c->n == c->cap) {
		int cap = c->cap ? c->cap * 2 : 4;
		c->keys = xrealloc(c->keys, (size_t)c->cap * sizeof *c->keys, (size_t)cap * sizeof *c->keys);
		c->vals = xrealloc(c->vals, (size_t)c->cap * sizeof *c->vals, (size_t)cap * sizeof *c->vals);
		c->cap = cap;
	}
	c->keys[c->n] = key;
	c->vals[c->n++] = val;
}

static Jv *jv_str(const char *s, size_t n)
{
	Jv *v = jv_new(JV_STRING);
	v->s = (Str){s, n};
	return v;
}

static Jv *jv_num(const char *s)
{
	Jv *v = jv_new(JV_NUMBER);
	v->s = str_c(s);
	return v;
}

/* convertToMap of a double: NaN and the infinities as their names, integral values as ints. */
static Jv *jv_double(double d)
{
	if (isnan(d))
		return jv_str("NaN", 3);
	if (isinf(d))
		return d < 0 ? jv_str("-Infinity", 9) : jv_str("Infinity", 8);
	if (d == trunc(d)) {
		/* double.toInt() clamps to the int64 range (Dart VM) */
		long long i = d >= 9223372036854775807.0 ? INT64_MAX : d <= -9223372036854775808.0 ? INT64_MIN : (long long)d;
		return jv_num(xfmt("%lld", i));
	}
	return jv_num(dart_double_to_string(d));
}

static Jv *jv_value(const PbField *f, const PbVal *v)
{
	switch (f->type) {
	case PT_BOOL: {
		Jv *b = jv_new(JV_BOOL);
		b->b = v->u != 0;
		return b;
	}
	case PT_STRING: return jv_str(v->s.p, v->s.n);
	case PT_BYTES: {
		char *s = dart_base64_encode((const uint8_t *)v->s.p, v->s.n);
		return jv_str(s, strlen(s));
	}
	case PT_INT32: case PT_SINT32: case PT_ENUM: return jv_num(xfmt("%lld", (long long)(int32_t)v->u));
	case PT_SFIXED32: return jv_num(xfmt("%lld", (long long)(int32_t)(uint32_t)v->u));
	case PT_UINT32: case PT_FIXED32: return jv_num(xfmt("%lld", (long long)(uint32_t)v->u));
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: {
		char *s = xfmt("%lld", (long long)v->u);
		return jv_str(s, strlen(s));
	}
	case PT_UINT64: case PT_FIXED64: {
		Buf b = {0};
		char d[24];
		int n = 0;
		uint64_t x = v->u;
		do
			d[n++] = (char)('0' + x % 10);
		while ((x /= 10));
		while (n)
			buf_addc(&b, d[--n]);
		return jv_str(buf_str(&b), b.n);
	}
	case PT_DOUBLE: {
		double d;
		memcpy(&d, &v->u, sizeof d);
		return jv_double(d);
	}
	case PT_FLOAT: {
		uint32_t bits = (uint32_t)v->u;
		float fl;
		memcpy(&fl, &bits, sizeof fl);
		return jv_double((double)fl);
	}
	case PT_MESSAGE: return dpb_json_map(v->m);
	default: fail("protobuf: unknown field type %d", f->type);
	}
}

Jv *dpb_json_map(const PbMsg *m)
{
	Jv *map = jv_new(JV_MAP);
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		/* missing, repeated or an empty byte array */
		if (f->label == PL_OPT && f->type == PT_BYTES && s->v[0].s.n == 0)
			continue;
		const char *key = xfmt("%d", (int)f->num);
		if (f->label == PL_OPT) {
			jv_add(map, key, jv_value(f, &s->v[0]));
			continue;
		}
		Jv *list = jv_new(JV_LIST);
		for (int j = 0; j < s->n; j++)
			jv_add(list, NULL, jv_value(f, &s->v[j]));
		jv_add(map, key, list);
	}
	return map;
}
