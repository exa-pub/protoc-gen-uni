/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 internal/encoding/tag/tag.go and
 * internal/encoding/defval/default.go. */
#include "tag.h"

#include "go1_26_5.h"

#include <math.h>

static char *itoa64(int64_t v)
{
	char t[24];
	int i = (int)sizeof t;
	uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
	t[--i] = 0;
	do
		t[--i] = (char)('0' + u % 10);
	while ((u /= 10) > 0);
	if (v < 0)
		t[--i] = '-';
	return xstrdup(t + i);
}

static char *utoa64(uint64_t u)
{
	char t[24];
	int i = (int)sizeof t;
	t[--i] = 0;
	do
		t[--i] = (char)('0' + u % 10);
	while ((u /= 10) > 0);
	return xstrdup(t + i);
}

/* marshalBytes: C escaping like protoc's CEscape. */
static Str marshal_bytes(Str b)
{
	Buf s = {0};
	for (size_t i = 0; i < b.n; i++) {
		unsigned char c = (unsigned char)b.p[i];
		switch (c) {
		case '\n': buf_adds(&s, "\\n"); break;
		case '\r': buf_adds(&s, "\\r"); break;
		case '\t': buf_adds(&s, "\\t"); break;
		case '"': buf_adds(&s, "\\\""); break;
		case '\'': buf_adds(&s, "\\'"); break;
		case '\\': buf_adds(&s, "\\\\"); break;
		default:
			if (c >= 0x20 && c <= 0x7e) {
				buf_addc(&s, (char)c);
			} else {
				char o[5] = {'\\', (char)('0' + (c >> 6)), (char)('0' + ((c >> 3) & 7)), (char)('0' + (c & 7)), 0};
				buf_adds(&s, o);
			}
		}
	}
	Str r = buf_view(&s);
	return r.p ? r : (Str){"", 0};
}

const char *tag_marshal_default(const GoValue *v, int kind, Str *out)
{
	switch (kind) {
	case PT_BOOL:
		*out = str_c(v->b ? "1" : "0");
		return NULL;
	case PT_ENUM:
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: case PT_INT64: case PT_SINT64: case PT_SFIXED64:
		*out = str_c(itoa64(v->i));
		return NULL;
	case PT_UINT32: case PT_FIXED32: case PT_UINT64: case PT_FIXED64:
		*out = str_c(utoa64(v->u));
		return NULL;
	case PT_FLOAT:
	case PT_DOUBLE:
		if (isinf(v->f) && v->f < 0)
			*out = str_c("-inf");
		else if (isinf(v->f))
			*out = str_c("inf");
		else if (isnan(v->f))
			*out = str_c("nan");
		else
			*out = str_c(go1_26_5_format_float(v->f, 'g', -1, kind == PT_FLOAT ? 32 : 64));
		return NULL;
	case PT_STRING:
		/* String values are serialized as is without any escaping. */
		*out = v->s;
		return NULL;
	case PT_BYTES:
		*out = marshal_bytes(v->s);
		return NULL;
	}
	return "could not format value";
}

Str tag_marshal(const GoField *fd, const char *enum_name)
{
	Buf tag = {0};
	switch (fd->kind) {
	case PT_BOOL: case PT_ENUM: case PT_INT32: case PT_UINT32: case PT_INT64: case PT_UINT64:
		buf_adds(&tag, "varint");
		break;
	case PT_SINT32: buf_adds(&tag, "zigzag32"); break;
	case PT_SINT64: buf_adds(&tag, "zigzag64"); break;
	case PT_SFIXED32: case PT_FIXED32: case PT_FLOAT: buf_adds(&tag, "fixed32"); break;
	case PT_SFIXED64: case PT_FIXED64: case PT_DOUBLE: buf_adds(&tag, "fixed64"); break;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: buf_adds(&tag, "bytes"); break;
	case PT_GROUP: buf_adds(&tag, "group"); break;
	}
	if (tag.n)
		buf_addc(&tag, ',');
	buf_adds(&tag, itoa64(fd->number));
	switch (fd->cardinality) {
	case GO_OPTIONAL: buf_adds(&tag, ",opt"); break;
	case GO_REQUIRED: buf_adds(&tag, ",req"); break;
	case GO_REPEATED: buf_adds(&tag, ",rep"); break;
	}
	if (go_field_is_packed(fd))
		buf_adds(&tag, ",packed");
	const char *name = fd->d.name;
	if (fd->kind == PT_GROUP && fd->message_type) {
		/* The name of the FieldDescriptor for a group field is lowercased. To find the
		 * original capitalization, we look in the field's MessageType. */
		name = fd->message_type->d.name;
	}
	buf_adds(&tag, xcat(",name=", name));
	if (fd->json_name.n && !str_eqc(fd->json_name, name) && !fd->is_extension) {
		/* NOTE: The jsonName != name condition is suspect, but it preserve the exact same
		 * semantics from the previous generator. */
		buf_adds(&tag, ",json=");
		buf_add_str(&tag, fd->json_name);
	}
	/* The previous implementation does not tag extension fields as proto3, even when the
	 * field is defined in a proto3 file. Match that behavior for consistency. */
	if (go_field_syntax(fd) == GO_PROTO3 && !fd->is_extension)
		buf_adds(&tag, ",proto3");
	if (fd->kind == PT_ENUM && enum_name && *enum_name)
		buf_adds(&tag, xcat(",enum=", enum_name));
	if (fd->oneof)
		buf_adds(&tag, ",oneof");
	/* This must appear last in the tag, since commas in strings aren't escaped. */
	if (fd->has_default) {
		Str def = {"", 0};
		tag_marshal_default(&fd->default_value, fd->kind, &def);
		buf_adds(&tag, ",def=");
		buf_add_str(&tag, def);
	}
	return buf_view(&tag);
}
