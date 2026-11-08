/* Copyright (c) 2016 Timo Stamm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/runtime/src of protobuf-ts v2.11.1: lower-camel-case.ts,
 * reflection-info.ts (normalizeFieldInfo), reflection-create.ts, reflection-scalar-default.ts,
 * reflection-long-convert.ts, reflection-binary-reader.ts, binary-reader.ts, goog-varint.ts,
 * pb-long.ts (toString, toNumber), reflection-json-writer.ts, base64.ts (base64encode) and
 * enum-object.ts; RuntimeEnumBuilder is from packages/plugin/src/interpreter.ts. */
#include "runtime.h"

#include <string.h>

enum {
	T_DOUBLE = 1, T_FLOAT = 2, T_INT64 = 3, T_UINT64 = 4, T_INT32 = 5, T_FIXED64 = 6, T_FIXED32 = 7,
	T_BOOL = 8, T_STRING = 9, T_BYTES = 12, T_UINT32 = 13, T_SFIXED32 = 15, T_SFIXED64 = 16,
	T_SINT32 = 17, T_SINT64 = 18,
};
enum { WT_VARINT = 0, WT_BIT64 = 1, WT_LEN = 2, WT_SGROUP = 3, WT_EGROUP = 4, WT_BIT32 = 5 };

/* Nesting of messages and groups that is read before giving up. Upstream recurses on the JavaScript
 * stack, whose limit depends on the engine and its frame sizes, so the port does not reproduce it and
 * deeper input is unsupported. */
#define MAX_DEPTH 100
#define MAX_DEPTH_ERROR "unsupported by protoc-gen-uni: values nested deeper than 100 messages or groups"

/* ---- lower-camel-case.ts ---- */

const char *rt_lower_camel_case(const char *snake_case)
{
	bool cap_next = false;
	Buf b = {0};
	for (size_t i = 0; snake_case[i]; i++) {
		char next = snake_case[i];
		if (next == '_') {
			cap_next = true;
		} else if (next >= '0' && next <= '9') {
			buf_addc(&b, next);
			cap_next = true;
		} else if (cap_next) {
			buf_addc(&b, next >= 'a' && next <= 'z' ? (char)(next - 32) : next);
			cap_next = false;
		} else if (i == 0) {
			buf_addc(&b, next >= 'A' && next <= 'Z' ? (char)(next + 32) : next);
		} else {
			buf_addc(&b, next);
		}
	}
	return buf_str(&b);
}

/* ---- reflection-info.ts, message-type.ts ---- */

RtMessageType *rt_message_type(const char *type_name, RtPartialFieldInfo **fields, int n, JsVal *options,
			       const RtResolver *resolver)
{
	RtMessageType *t = xalloc(sizeof *t);
	t->type_name = type_name;
	t->n_fields = n;
	t->fields = xalloc(sizeof *t->fields * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		RtPartialFieldInfo *p = fields[i];
		RtFieldInfo *f = &p->f;
		if (!p->has_local_name)
			f->local_name = rt_lower_camel_case(f->name);
		if (!p->has_json_name)
			f->json_name = rt_lower_camel_case(f->name);
		if (!p->has_repeat)
			f->repeat = RT_REPEAT_NO;
		if (!p->has_opt)
			f->opt = f->repeat ? false : f->oneof ? false : f->kind == RT_MESSAGE;
		t->fields[i] = f;
	}
	t->options = options ? options : js_object();
	t->resolver = resolver;
	return t;
}

/* ---- pb-long.ts, reflection-long-convert.ts ---- */

static Str u64_decimal(uint64_t v, bool minus)
{
	char tmp[24];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	Buf b = {0};
	if (minus)
		buf_addc(&b, '-');
	while (n)
		buf_addc(&b, tmp[--n]);
	return buf_view(&b);
}

/* PbLong/PbULong.toString() */
static Str long_decimal(uint64_t bits, bool is_signed)
{
	if (is_signed && (int64_t)bits < 0)
		return u64_decimal(~bits + 1, true);
	return u64_decimal(bits, false);
}

static JsVal *long_convert(uint64_t bits, bool is_signed, int long_type)
{
	switch (long_type) {
	case RT_LONG_BIGINT: return js_bigint(long_decimal(bits, is_signed));
	case RT_LONG_NUMBER: {
		/* toNumber(): hi * 2^32 + (lo >>> 0), where hi is signed for PbLong */
		double d = is_signed ? (double)(int64_t)bits : (double)bits;
		if (d > 9007199254740991.0 || d < -9007199254740991.0)
			fail("Error: cannot convert to safe number");
		return js_number(d);
	}
	default: return js_string(long_decimal(bits, is_signed));
	}
}

/* ---- reflection-scalar-default.ts ---- */

JsVal *rt_scalar_default(int type, int long_type)
{
	if (long_type == RT_LONG_UNSET)
		long_type = RT_LONG_STRING;
	switch (type) {
	case T_BOOL: return js_bool(false);
	case T_UINT64: case T_FIXED64: return long_convert(0, false, long_type);
	case T_INT64: case T_SFIXED64: case T_SINT64: return long_convert(0, true, long_type);
	case T_DOUBLE: case T_FLOAT: return js_number(0);
	case T_BYTES: return js_bytes((Str){"", 0});
	case T_STRING: return js_string((Str){"", 0});
	default: return js_number(0);
	}
}

/* ---- reflection-create.ts ---- */

JsVal *rt_create(const RtMessageType *t)
{
	JsVal *msg = js_object();
	for (int i = 0; i < t->n_fields; i++) {
		const RtFieldInfo *f = t->fields[i];
		Str name = str_c(f->local_name);
		if (f->opt)
			continue;
		if (f->oneof) {
			JsVal *g = js_object();
			js_obj_set(g, STR_LIT("oneofKind"), js_undefined());
			js_obj_set(msg, str_c(f->oneof), g);
		} else if (f->repeat) {
			js_obj_set(msg, name, js_array());
		} else {
			switch (f->kind) {
			case RT_SCALAR: js_obj_set(msg, name, rt_scalar_default(f->T, f->L)); break;
			case RT_ENUM: js_obj_set(msg, name, js_number(0)); break;
			case RT_MAP: js_obj_set(msg, name, js_object()); break;
			}
		}
	}
	return msg;
}

/* ---- binary-reader.ts, goog-varint.ts ---- */

typedef struct {
	const uint8_t *buf;
	size_t len, pos;
	int depth;
} Reader;

static void assert_bounds(Reader *r)
{
	if (r->pos > r->len)
		fail("RangeError: premature EOF");
}

/* this.buf[this.pos++]: undefined (0 in bit operations) past the end */
static uint32_t next_byte(Reader *r)
{
	size_t p = r->pos++;
	return p < r->len ? r->buf[p] : 0;
}

static uint32_t read_uint32(Reader *r)
{
	uint32_t b = next_byte(r), result = b & 0x7f;
	if (!(b & 0x80))
		goto done;
	b = next_byte(r);
	result |= (b & 0x7f) << 7;
	if (!(b & 0x80))
		goto done;
	b = next_byte(r);
	result |= (b & 0x7f) << 14;
	if (!(b & 0x80))
		goto done;
	b = next_byte(r);
	result |= (b & 0x7f) << 21;
	if (!(b & 0x80))
		goto done;
	b = next_byte(r);
	result |= (b & 0x0f) << 28;
	for (int read = 5; (b & 0x80) && read < 10; read++)
		b = next_byte(r);
	if (b & 0x80)
		fail("Error: invalid varint");
done:
	assert_bounds(r);
	return result;
}

static uint64_t read_varint64(Reader *r)
{
	uint32_t lo = 0, hi = 0;
	for (int shift = 0; shift < 28; shift += 7) {
		uint32_t b = next_byte(r);
		lo |= (b & 0x7f) << shift;
		if (!(b & 0x80))
			goto done;
	}
	uint32_t middle = next_byte(r);
	lo |= (middle & 0x0f) << 28;
	hi = (middle & 0x70) >> 4;
	if (!(middle & 0x80))
		goto done;
	for (int shift = 3; shift <= 31; shift += 7) {
		uint32_t b = next_byte(r);
		hi |= (b & 0x7f) << shift;
		if (!(b & 0x80))
			goto done;
	}
	fail("Error: invalid varint");
done:
	assert_bounds(r);
	return (uint64_t)hi << 32 | lo;
}

static void read_tag(Reader *r, uint32_t *no, int *wt)
{
	uint32_t tag = read_uint32(r);
	*no = tag >> 3;
	*wt = (int)(tag & 7);
	if (*no == 0 || *wt > 5)
		fail("Error: illegal tag: field no %d wire type %d", (int)*no, *wt);
}

static void skip(Reader *r, int wt)
{
	switch (wt) {
	case WT_VARINT:
		while (next_byte(r) & 0x80)
			;
		break;
	case WT_BIT64: r->pos += 8; break;
	case WT_BIT32: r->pos += 4; break;
	case WT_LEN: {
		uint32_t n = read_uint32(r);
		r->pos += n;
		break;
	}
	case WT_SGROUP: {
		if (++r->depth > MAX_DEPTH)
			fail(MAX_DEPTH_ERROR);
		for (;;) {
			uint32_t no;
			int t;
			read_tag(r, &no, &t);
			if (t == WT_EGROUP)
				break;
			skip(r, t);
		}
		r->depth--;
		break;
	}
	default: fail("Error: cant skip wire type %d", wt);
	}
	assert_bounds(r);
}

static uint32_t read_fixed32(Reader *r)
{
	if (r->pos + 4 > r->len)
		fail("RangeError: Offset is outside the bounds of the DataView");
	const uint8_t *p = r->buf + r->pos;
	r->pos += 4;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static Str read_bytes(Reader *r)
{
	uint32_t n = read_uint32(r);
	size_t start = r->pos;
	r->pos += n;
	assert_bounds(r);
	return (Str){xstrndup((const char *)r->buf + start, n), n};
}

/* TextDecoder("utf-8", {fatal: true, ignoreBOM: true}).decode */
static Str read_string(Reader *r)
{
	Str s = read_bytes(r);
	for (size_t i = 0; i < s.n;) {
		uint8_t c = (uint8_t)s.p[i];
		size_t k = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
		bool ok = k && i + k <= s.n;
		uint32_t cp = k == 1 ? c : k == 2 ? c & 0x1f : k == 3 ? c & 0x0f : c & 0x07;
		for (size_t j = 1; ok && j < k; j++) {
			uint8_t d = (uint8_t)s.p[i + j];
			ok = (d & 0xc0) == 0x80;
			cp = cp << 6 | (d & 0x3f);
		}
		if (ok && ((k == 2 && cp < 0x80) || (k == 3 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) ||
			   (k == 4 && (cp < 0x10000 || cp > 0x10ffff))))
			ok = false;
		if (!ok)
			fail("TypeError: The encoded data was not valid for encoding utf-8");
		i += k;
	}
	return s;
}

static double bits_to_double(uint64_t u)
{
	double d;
	memcpy(&d, &u, sizeof d);
	return d;
}

static double bits_to_float(uint32_t u)
{
	float f;
	memcpy(&f, &u, sizeof f);
	return (double)f;
}

/* ReflectionBinaryReader.scalar */
static JsVal *read_scalar(Reader *r, int type, int long_type)
{
	switch (type) {
	case T_INT32: return js_number((double)(int32_t)read_uint32(r));
	case T_STRING: return js_string(read_string(r));
	case T_BOOL: return js_bool(read_varint64(r) != 0);
	case T_DOUBLE: {
		uint64_t lo = read_fixed32(r), hi = read_fixed32(r);
		return js_number(bits_to_double(hi << 32 | lo));
	}
	case T_FLOAT: return js_number(bits_to_float(read_fixed32(r)));
	case T_INT64: return long_convert(read_varint64(r), true, long_type);
	case T_UINT64: return long_convert(read_varint64(r), false, long_type);
	case T_FIXED64: case T_SFIXED64: {
		uint64_t lo = read_fixed32(r), hi = read_fixed32(r);
		return long_convert(hi << 32 | lo, type == T_SFIXED64, long_type);
	}
	case T_FIXED32: return js_number((double)read_fixed32(r));
	case T_BYTES: return js_bytes(read_bytes(r));
	case T_UINT32: return js_number((double)read_uint32(r));
	case T_SFIXED32: return js_number((double)(int32_t)read_fixed32(r));
	case T_SINT32: {
		uint32_t z = read_uint32(r);
		return js_number((double)(int32_t)((z >> 1) ^ (0u - (z & 1))));
	}
	case T_SINT64: {
		uint64_t z = read_varint64(r);
		return long_convert((z >> 1) ^ (0 - (z & 1)), true, long_type);
	}
	}
	fail("Error: unknown scalar type %d", type);
}

static void read_message(RtMessageType *t, Reader *r, JsVal *message, size_t length);

/* MessageType.internalBinaryRead(reader, length, options, target) */
static JsVal *internal_binary_read(RtMessageType *t, Reader *r, size_t length, JsVal *target)
{
	JsVal *message = target && target->t != JS_UNDEFINED ? target : rt_create(t);
	if (++r->depth > MAX_DEPTH)
		fail(MAX_DEPTH_ERROR);
	read_message(t, r, message, length);
	r->depth--;
	return message;
}

static RtMessageType *field_message(const RtMessageType *t, void *ref) { return t->resolver->message(t->resolver->ctx, ref); }

/* Map keys are property keys: numbers become their decimal string. */
static Str map_key_string(JsVal *key)
{
	if (key->t == JS_NUMBER)
		return str_c(js_number_to_string(key->num));
	return key->s;
}

static void map_entry(RtMessageType *t, RtFieldInfo *field, Reader *r, Str *key_out, JsVal **val_out)
{
	size_t length = read_uint32(r), end = r->pos + length;
	JsVal *key = NULL, *val = NULL;
	while (r->pos < end) {
		uint32_t no;
		int wt;
		read_tag(r, &no, &wt);
		switch (no) {
		case 1:
			if (field->K == T_BOOL)
				key = js_string(str_c(read_varint64(r) != 0 ? "true" : "false"));
			else
				key = read_scalar(r, field->K, RT_LONG_STRING);
			break;
		case 2:
			switch (field->V_kind) {
			case RT_SCALAR: val = read_scalar(r, field->V_T, field->V_L); break;
			case RT_ENUM: val = js_number((double)(int32_t)read_uint32(r)); break;
			case RT_MESSAGE: {
				size_t n = read_uint32(r);
				val = internal_binary_read(field_message(t, field->ref), r, n, NULL);
				break;
			}
			}
			break;
		default:
			fail("Error: Unknown field %d (wire type %d) in map entry for %s#%s", (int)no, wt, t->type_name,
			     field->name);
		}
	}
	if (!key) {
		JsVal *raw = rt_scalar_default(field->K, RT_LONG_UNSET);
		key = field->K == T_BOOL ? js_string(STR_LIT("false")) : raw;
	}
	if (!val) {
		switch (field->V_kind) {
		case RT_SCALAR: val = rt_scalar_default(field->V_T, field->V_L); break;
		case RT_ENUM: val = js_number(0); break;
		case RT_MESSAGE: val = rt_create(field_message(t, field->ref)); break;
		}
	}
	*key_out = map_key_string(key);
	*val_out = val;
}

/* ReflectionBinaryReader.read */
static void read_message(RtMessageType *t, Reader *r, JsVal *message, size_t length)
{
	if (!t->by_no) {
		t->by_no = xalloc(sizeof *t->by_no);
		for (int i = 0; i < t->n_fields; i++)
			map_put(t->by_no, xfmt("%d", t->fields[i]->no), t->fields[i]);
	}
	size_t end = r->pos + length;
	while (r->pos < end) {
		uint32_t no;
		int wt;
		read_tag(r, &no, &wt);
		RtFieldInfo *field = map_get(t->by_no, xfmt("%d", (int)no));
		if (!field) {
			skip(r, wt);
			continue;
		}
		JsVal *target = message;
		Str local = str_c(field->local_name);
		if (field->oneof) {
			Str oneof = str_c(field->oneof);
			target = js_obj_get(message, oneof);
			JsVal *kind = target && target->t == JS_OBJECT ? js_obj_get(target, STR_LIT("oneofKind")) : NULL;
			if (!kind || kind->t != JS_STRING || !str_equal(kind->s, local)) {
				target = js_object();
				js_obj_set(target, STR_LIT("oneofKind"), js_string(local));
				js_obj_set(message, oneof, target);
			}
		}
		switch (field->kind) {
		case RT_SCALAR:
		case RT_ENUM: {
			int type = field->kind == RT_ENUM ? T_INT32 : field->T;
			int lt = field->kind == RT_SCALAR ? field->L : RT_LONG_UNSET;
			if (field->repeat) {
				JsVal *arr = js_obj_get(target, local);
				if (!arr || arr->t != JS_ARRAY)
					fail("TypeError: cannot read properties of undefined (reading 'push')");
				if (wt == WT_LEN && type != T_STRING && type != T_BYTES) {
					size_t e = read_uint32(r) + r->pos;
					while (r->pos < e)
						vec_push(&arr->items, read_scalar(r, type, lt));
				} else {
					vec_push(&arr->items, read_scalar(r, type, lt));
				}
			} else {
				js_obj_set(target, local, read_scalar(r, type, lt));
			}
			break;
		}
		case RT_MESSAGE: {
			RtMessageType *mt = field_message(t, field->ref);
			if (field->repeat) {
				JsVal *arr = js_obj_get(target, local);
				if (!arr || arr->t != JS_ARRAY)
					fail("TypeError: cannot read properties of undefined (reading 'push')");
				size_t n = read_uint32(r);
				vec_push(&arr->items, internal_binary_read(mt, r, n, NULL));
			} else {
				size_t n = read_uint32(r);
				js_obj_set(target, local, internal_binary_read(mt, r, n, js_obj_get(target, local)));
			}
			break;
		}
		case RT_MAP: {
			Str key;
			JsVal *val;
			map_entry(t, field, r, &key, &val);
			JsVal *map = js_obj_get(target, local);
			if (!map || map->t != JS_OBJECT)
				fail("TypeError: cannot set properties of undefined");
			js_obj_set(map, key, val);
			break;
		}
		}
	}
}

JsVal *rt_from_binary(RtMessageType *t, Str bytes)
{
	Reader r = {(const uint8_t *)bytes.p, bytes.n, 0, 0};
	return internal_binary_read(t, &r, bytes.n, NULL);
}

/* ---- reflection-json-writer.ts ---- */

static RtEnumInfo *field_enum(const RtMessageType *t, void *ref) { return t->resolver->enm(t->resolver->ctx, ref); }

/* base64encode: standard alphabet with padding */
static Str base64_encode(Str b)
{
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	Buf o = {0};
	const uint8_t *p = (const uint8_t *)b.p;
	size_t i = 0;
	for (; i + 3 <= b.n; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
		char q[4] = {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], abc[v & 63]};
		buf_add(&o, q, 4);
	}
	if (b.n - i == 1) {
		uint32_t v = (uint32_t)p[i] << 16;
		char q[4] = {abc[v >> 18], abc[(v >> 12) & 63], '=', '='};
		buf_add(&o, q, 4);
	} else if (b.n - i == 2) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
		char q[4] = {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], '='};
		buf_add(&o, q, 4);
	}
	return buf_view(&o);
}

/* PbLong.from(value).isZero() for a long value of any LongType. */
static bool long_is_zero(const JsVal *v)
{
	if (v->t == JS_NUMBER)
		return v->num == 0;
	return v->s.n == 1 && v->s.p[0] == '0';
}

/* PbLong.from(value).toString() */
static Str long_to_string(const JsVal *v)
{
	if (v->t == JS_NUMBER)
		return str_c(js_number_to_string(v->num));
	return v->s;
}

/* ReflectionJsonWriter.scalar; NULL is undefined. */
static JsVal *json_scalar(int type, const JsVal *value, bool optional, bool emit_defaults)
{
	if (!value || value->t == JS_UNDEFINED)
		return NULL;
	bool ed = emit_defaults || optional;
	switch (type) {
	case T_INT32: case T_SFIXED32: case T_SINT32: case T_FIXED32: case T_UINT32:
		if (value->num == 0)
			return ed ? js_number(0) : NULL;
		return js_number(value->num);
	case T_FLOAT: case T_DOUBLE:
		if (value->num == 0)
			return ed ? js_number(0) : NULL;
		if (value->num != value->num)
			return js_string(STR_LIT("NaN"));
		if (value->num > 1.7976931348623157e308)
			return js_string(STR_LIT("Infinity"));
		if (value->num < -1.7976931348623157e308)
			return js_string(STR_LIT("-Infinity"));
		return js_number(value->num);
	case T_STRING:
		if (!value->s.n)
			return ed ? js_string((Str){"", 0}) : NULL;
		return js_string(value->s);
	case T_BOOL:
		if (!value->b)
			return ed ? js_bool(false) : NULL;
		return js_bool(true);
	case T_UINT64: case T_FIXED64: case T_INT64: case T_SFIXED64: case T_SINT64:
		if (long_is_zero(value) && !ed)
			return NULL;
		return js_string(long_to_string(value));
	case T_BYTES:
		if (!value->s.n)
			return ed ? js_string((Str){"", 0}) : NULL;
		return js_string(base64_encode(value->s));
	}
	return NULL;
}

/* ReflectionJsonWriter.enum */
static JsVal *json_enum(const RtEnumInfo *type, const JsVal *value, bool optional, bool emit_defaults)
{
	if (str_eq(type->type_name, "google.protobuf.NullValue"))
		return !emit_defaults && !optional ? NULL : js_null();
	if (!value || value->t == JS_UNDEFINED)
		return NULL;
	if (value->num == 0 && !emit_defaults && !optional)
		return NULL;
	const char *name = rt_enum_name(type->obj, (int32_t)value->num);
	if (!name)
		return js_number(value->num);
	if (type->shared_prefix)
		return js_string(str_c(xcat(type->shared_prefix, name)));
	return js_string(str_c(name));
}

static JsVal *json_message(const RtMessageType *t, const JsVal *value)
{
	if (!value || value->t == JS_UNDEFINED)
		return NULL;
	return rt_to_json(t, value);
}

static JsVal *json_field(const RtMessageType *t, const RtFieldInfo *field, const JsVal *value, bool emit_defaults)
{
	if (field->kind == RT_MAP) {
		JsVal *obj = js_object();
		Str *keys;
		int n = value && value->t == JS_OBJECT ? js_obj_keys(value, &keys) : 0;
		for (int i = 0; i < n; i++) {
			JsVal *v = js_obj_get(value, keys[i]), *j = NULL;
			switch (field->V_kind) {
			case RT_SCALAR: j = json_scalar(field->V_T, v, false, true); break;
			case RT_MESSAGE: j = json_message(field_message(t, field->ref), v); break;
			case RT_ENUM: j = json_enum(field_enum(t, field->ref), v, false, true); break;
			}
			if (j)
				js_obj_set(obj, keys[i], j);
		}
		Str *k2;
		return emit_defaults || js_obj_keys(obj, &k2) > 0 ? obj : NULL;
	}
	if (field->repeat) {
		JsVal *arr = js_array();
		int n = value && value->t == JS_ARRAY ? value->items.n : 0;
		for (int i = 0; i < n; i++) {
			JsVal *v = value->items.v[i], *j = NULL;
			switch (field->kind) {
			case RT_SCALAR: j = json_scalar(field->T, v, field->opt, true); break;
			case RT_ENUM: j = json_enum(field_enum(t, field->ref), v, field->opt, true); break;
			case RT_MESSAGE: j = json_message(field_message(t, field->ref), v); break;
			}
			if (j)
				vec_push(&arr->items, j);
		}
		return emit_defaults || arr->items.n > 0 ? arr : NULL;
	}
	switch (field->kind) {
	case RT_SCALAR: return json_scalar(field->T, value, field->opt, emit_defaults);
	case RT_ENUM: return json_enum(field_enum(t, field->ref), value, field->opt, emit_defaults);
	case RT_MESSAGE: return json_message(field_message(t, field->ref), value);
	}
	return NULL;
}

JsVal *rt_to_json(const RtMessageType *t, const JsVal *message)
{
	JsVal *json = js_object();
	for (int i = 0; i < t->n_fields; i++) {
		const RtFieldInfo *field = t->fields[i];
		Str local = str_c(field->local_name), json_name = str_c(field->json_name);
		if (!field->oneof) {
			JsVal *j = json_field(t, field, js_obj_get(message, local), false);
			if (j)
				js_obj_set(json, json_name, j);
			continue;
		}
		JsVal *group = js_obj_get(message, str_c(field->oneof));
		JsVal *kind = group && group->t == JS_OBJECT ? js_obj_get(group, STR_LIT("oneofKind")) : NULL;
		if (!kind || kind->t != JS_STRING || !str_equal(kind->s, local))
			continue;
		bool ed = field->kind == RT_SCALAR || field->kind == RT_ENUM;
		JsVal *j = json_field(t, field, js_obj_get(group, local), ed);
		if (!j)
			fail("Error: assertion failed");
		js_obj_set(json, json_name, j);
	}
	return json;
}

/* ---- enum-object.ts, RuntimeEnumBuilder (interpreter.ts) ---- */

/* parseInt(k) for the keys of an enum object (names and decimal numbers); false for NaN. */
static bool parse_int(const char *k, double *out)
{
	const char *p = k;
	while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
		p++;
	bool minus = *p == '-';
	if (*p == '-' || *p == '+')
		p++;
	int base = 10;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	double v = 0;
	int digits = 0;
	for (;; p++, digits++) {
		int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
		if (d >= base)
			break;
		v = v * base + d;
	}
	if (!digits)
		return false;
	*out = minus ? -v : v;
	return true;
}

const char *rt_enum_name(const RtEnumObject *o, int32_t number)
{
	intptr_t i = (intptr_t)map_get(&o->by_number, xfmt("%d", (int)number));
	return i ? o->names[i - 1] : NULL;
}

static int name_index(const RtEnumObject *o, const char *name) { return (int)(intptr_t)map_get(&o->by_name, name) - 1; }

/* isEnumObject over {[number]: name, [name]: number}; "__proto__" adds no property. */
static bool is_enum_object(const RtEnumObject *o)
{
	if (!rt_enum_name(o, 0))
		return false;
	for (int i = 0; i < o->n; i++) {
		/* number keys */
		const char *nam = rt_enum_name(o, o->numbers[i]);
		if (!nam)
			return false;
		int j = str_eq(nam, "__proto__") ? -1 : name_index(o, nam);
		if (j < 0 || o->numbers[j] != o->numbers[i])
			return false;
		/* name keys */
		if (str_eq(o->names[i], "__proto__"))
			continue;
		double num;
		if (parse_int(o->names[i], &num)) {
			if (num != (double)(int32_t)num)
				return false;
			const char *n2 = rt_enum_name(o, (int32_t)num);
			if (!n2)
				return false;
			int k = str_eq(n2, "__proto__") ? -1 : name_index(o, n2);
			if (k < 0 || o->numbers[k] != (int32_t)num)
				return false;
		}
	}
	return true;
}

RtEnumObject *rt_enum_object(const char **names, const int32_t *numbers, int n)
{
	RtEnumObject *o = xalloc(sizeof *o);
	for (int i = 0; i < n; i++) {
		if (map_has(&o->by_name, names[i]))
			fail("Error: duplicate names");
		map_put(&o->by_name, names[i], (void *)(intptr_t)(i + 1));
		map_put(&o->by_number, xfmt("%d", (int)numbers[i]), (void *)(intptr_t)(i + 1));
	}
	o->names = xalloc(sizeof *o->names * (size_t)(n + 1));
	o->numbers = xalloc(sizeof *o->numbers * (size_t)(n + 1));
	memcpy(o->names, names, sizeof *names * (size_t)n);
	memcpy(o->numbers, numbers, sizeof *numbers * (size_t)n);
	o->n = n;
	if (!is_enum_object(o))
		fail("Error: not a typescript enum object");
	return o;
}
