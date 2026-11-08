/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protobuf/src/wire/text-encoding.ts (decodeUtf8: a TextDecoder without
 * fatal) and the parts of from-binary.ts where pkg/pb parses differently: string fields of proto2
 * messages (descriptor.proto, plugin.proto) are decoded without UTF-8 validation, and a known
 * field is read by its type whatever the wire type of the record. */
#include "protobuf.h"

/* Length of the maximal subpart of an ill-formed UTF-8 sequence at p (WHATWG Encoding, UTF-8
 * decoder), or 0 if p starts a well-formed sequence of *len bytes. */
static size_t ill_formed(const uint8_t *p, size_t n, size_t *len)
{
	uint8_t b = p[0], lo = 0x80, hi = 0xbf;
	size_t need;
	if (b < 0x80)
		return *len = 1, 0;
	if (b >= 0xc2 && b <= 0xdf)
		need = 1;
	else if (b >= 0xe0 && b <= 0xef)
		need = 2, lo = b == 0xe0 ? 0xa0 : 0x80, hi = b == 0xed ? 0x9f : 0xbf;
	else if (b >= 0xf0 && b <= 0xf4)
		need = 3, lo = b == 0xf0 ? 0x90 : 0x80, hi = b == 0xf4 ? 0x8f : 0xbf;
	else
		return 1;
	for (size_t k = 1; k <= need; k++) {
		if (k >= n || p[k] < lo || p[k] > hi)
			return k; /* the byte at k (if any) starts the next sequence */
		lo = 0x80, hi = 0xbf;
	}
	return *len = need + 1, 0;
}

Str decode_utf8(Str s)
{
	const uint8_t *p = (const uint8_t *)s.p;
	size_t i = 0, len = 0;
	while (i < s.n && !ill_formed(p + i, s.n - i, &len))
		i += len;
	if (i == s.n)
		return s;
	Buf b = {0};
	buf_add(&b, s.p, i);
	while (i < s.n) {
		size_t bad = ill_formed(p + i, s.n - i, &len);
		if (bad) {
			buf_adds(&b, "\xef\xbf\xbd"); /* U+FFFD */
			i += bad;
		} else {
			buf_add(&b, p + i, len);
			i += len;
		}
	}
	return buf_view(&b);
}

static int wire_type_of(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WT_I32;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return WT_LEN;
	case PT_GROUP: return WT_SGROUP;
	default: return WT_VARINT;
	}
}

void from_binary(PbMsg *m)
{
	const PbMsgDef *def = m->def;
	/* pkg/pb keeps a known field with a mismatching wire type as an unknown field (packed
	 * records of repeated scalars are known); upstream reads it by the field type. Unknown
	 * values of closed enums are unknown fields with the right wire type. */
	PbReader r = pb_unknown_fields(m);
	PbRawField uf;
	while (pb_next_field(&r, &uf)) {
		for (int i = 0; i < def->n; i++)
			if (def->f[i].num == uf.num && uf.wt != wire_type_of(def->f[i].type))
				fail("unsupported by protoc-gen-uni: field %d of a message in the request has wire type "
				     "%d instead of %d",
				     (int)uf.num, uf.wt, wire_type_of(def->f[i].type));
	}
	for (int i = 0; i < def->n; i++) {
		PbSlot *s = &m->slots[i];
		for (int j = 0; j < s->n; j++) {
			if (def->f[i].type == PT_STRING)
				s->v[j].s = decode_utf8(s->v[j].s);
			else if (def->f[i].type == PT_MESSAGE)
				from_binary(s->v[j].m);
		}
	}
}
