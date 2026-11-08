/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* encoding/protojson of protobuf-go v1.36.12: marshalKnownValue, marshalStruct, marshalListValue
 * (well_known_types.go), marshalMap (encode.go) and the string and number writers of
 * internal/encoding/json/encode.go. */
#include "protojson.h"

#include "go_std.h"

#include <stdlib.h>
#include <string.h>

enum { V_NULL = 1, V_NUMBER = 2, V_STRING = 3, V_BOOL = 4, V_STRUCT = 5, V_LIST = 6 };

/* The recursion of the port runs on the C stack (Go's has no limit). */
#define MAX_DEPTH 2000

/* appendString: errors on invalid UTF-8 (proto3 strings were validated when decoded). */
static const char *append_string(Buf *b, Str in)
{
	buf_addc(b, '"');
	for (size_t i = 0; i < in.n;) {
		uint32_t r;
		int n = utf8_decode(in.p + i, in.n - i, &r);
		if (r == 0xFFFD && n == 1)
			return xcat(go_err_prefix(), "invalid UTF-8");
		if (r < ' ' || r == '"' || r == '\\') {
			buf_addc(b, '\\');
			switch (r) {
			case '"':
			case '\\':
				buf_addc(b, (char)r);
				break;
			case '\b':
				buf_addc(b, 'b');
				break;
			case '\f':
				buf_addc(b, 'f');
				break;
			case '\n':
				buf_addc(b, 'n');
				break;
			case '\r':
				buf_addc(b, 'r');
				break;
			case '\t':
				buf_addc(b, 't');
				break;
			default:
				buf_fmt(b, "u%04x", r);
			}
		} else {
			buf_add(b, in.p + i, (size_t)n);
		}
		i += (size_t)n;
	}
	buf_addc(b, '"');
	return NULL;
}

/* fmt %v of a float64 that is NaN or infinite */
static const char *special(double v) { return v != v ? "NaN" : v > 0 ? "+Inf" : "-Inf"; }

static const char *marshal_value(const PbMsg *v, Buf *b, int depth);

typedef struct {
	const PbMsg *m;
	int index;
} Entry;

static int cmp_key(const PbMsg *a, const PbMsg *c)
{
	Str ka = pb_bytes(a, 1), kc = pb_bytes(c, 1);
	size_t n = ka.n < kc.n ? ka.n : kc.n;
	int r = n ? memcmp(ka.p, kc.p, n) : 0;
	return r ? r : (ka.n > kc.n) - (ka.n < kc.n);
}

static int cmp_entry(const void *x, const void *y)
{
	const Entry *a = x, *c = y;
	int r = cmp_key(a->m, c->m);
	return r ? r : (a->index > c->index) - (a->index < c->index);
}

/* marshalMap with order.GenericKeyOrder: the entries sorted by key; of the entries with the same
 * key, the last one (a Go map holds it). */
static const char *marshal_struct(const PbMsg *s, Buf *b, int depth)
{
	const PbSlot *slot = pb_slot(s, 1);
	int n = slot ? slot->n : 0;
	Entry *e = xalloc(sizeof *e * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		e[i] = (Entry){slot->v[i].m, i};
	qsort(e, (size_t)n, sizeof *e, cmp_entry);
	buf_addc(b, '{');
	bool first = true;
	for (int i = 0; i < n; i++) {
		if (i + 1 < n && cmp_key(e[i].m, e[i + 1].m) == 0)
			continue;
		if (!first)
			buf_addc(b, ',');
		first = false;
		const char *err = append_string(b, pb_bytes(e[i].m, 1));
		if (!err) {
			buf_addc(b, ':');
			const PbMsg *val = pb_msg(e[i].m, 2);
			err = marshal_value(val ? val : pb_new(e[i].m->def->f[1].msg), b, depth + 1);
		}
		if (err)
			return err;
	}
	buf_addc(b, '}');
	return NULL;
}

static const char *marshal_value(const PbMsg *v, Buf *b, int depth)
{
	if (depth > MAX_DEPTH)
		return "unsupported by protoc-gen-uni: google.protobuf.Value nested too deeply";
	if (pb_has(v, V_NULL)) {
		buf_adds(b, "null");
	} else if (pb_has(v, V_NUMBER)) {
		uint64_t bits = pb_slot(v, V_NUMBER)->v[0].u;
		double f;
		memcpy(&f, &bits, sizeof f);
		if (f != f || f > 1.7976931348623157e308 || f < -1.7976931348623157e308)
			return xcat(go_err_prefix(), "google.protobuf.Value.number_value: invalid ", special(f), " value");
		go_std_json_float(b, f, 64);
	} else if (pb_has(v, V_STRING)) {
		return append_string(b, pb_bytes(v, V_STRING));
	} else if (pb_has(v, V_BOOL)) {
		buf_adds(b, pb_int(v, V_BOOL, 0) ? "true" : "false");
	} else if (pb_has(v, V_STRUCT)) {
		return marshal_struct(pb_msg(v, V_STRUCT), b, depth);
	} else if (pb_has(v, V_LIST)) {
		const PbMsg *l = pb_msg(v, V_LIST);
		buf_addc(b, '[');
		for (int i = 0; i < pb_count(l, 1); i++) {
			if (i)
				buf_addc(b, ',');
			const char *err = marshal_value(pb_msg_at(l, 1, i), b, depth + 1);
			if (err)
				return err;
		}
		buf_addc(b, ']');
	} else {
		return xcat(go_err_prefix(), "google.protobuf.Value: none of the oneof fields is set");
	}
	return NULL;
}

const char *protojson_marshal_value(const PbMsg *v, Buf *out)
{
	size_t start = out->n;
	const char *err = marshal_value(v, out, 0);
	if (err)
		out->n = start;
	return err;
}
