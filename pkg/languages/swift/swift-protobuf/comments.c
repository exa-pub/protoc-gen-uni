/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/ProvidesSourceCodeLocation.swift,
 * ProvidesLocationPath.swift, ProvidesDeprecationComment.swift,
 * Google_Protobuf_SourceCodeInfo+Extensions.swift and the location paths of
 * Descriptor+Extensions.swift (swift-protobuf 1.39.0). */
#include "swift_protobuf.h"

#include <string.h>

enum { SCI_LOC_PATH = 1, SCI_LOC_LEADING = 3, SCI_LOC_TRAILING = 4, SCI_LOC_DETACHED = 6 };

static char *path_key(const int32_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, i ? ",%d" : "%d", (int)path[i]);
	return xstrdup(buf_str(&b));
}

PbMsg *sw_file_location(SwFile *file, const int32_t *path, int n)
{
	if (!file->locations_built) {
		file->locations_built = true;
		PbMsg *info = pb_msg(file->proto, FDP_SOURCE_CODE_INFO);
		for (int i = 0; i < pb_count(info, SCI_LOCATION); i++) {
			PbMsg *loc = pb_msg_at(info, SCI_LOCATION, i);
			int np = pb_count(loc, SCI_LOC_PATH);
			int32_t *p = xalloc(sizeof(int32_t) * (size_t)(np ? np : 1));
			for (int j = 0; j < np; j++)
				p[j] = (int32_t)pb_int_at(loc, SCI_LOC_PATH, j);
			map_put(&file->locations, path_key(p, np), loc);
		}
	}
	return map_get(&file->locations, path_key(path, n));
}

/* ---- location paths (getLocationPath) ---- */

typedef struct {
	int32_t *v;
	int n, cap;
} Path;

static void path_add(Path *p, int32_t x)
{
	if (p->n == p->cap) {
		int cap = p->cap ? p->cap * 2 : 16;
		p->v = xrealloc(p->v, sizeof(int32_t) * (size_t)p->cap, sizeof(int32_t) * (size_t)cap);
		p->cap = cap;
	}
	p->v[p->n++] = x;
}

static void message_path(Path *p, const SwMessage *m)
{
	if (m->containing_type) {
		message_path(p, m->containing_type);
		path_add(p, DP_NESTED_TYPE);
	} else {
		path_add(p, FDP_MESSAGE_TYPE);
	}
	path_add(p, m->index);
}

static void enum_path(Path *p, const SwEnum *e)
{
	if (e->containing_type) {
		message_path(p, e->containing_type);
		path_add(p, DP_ENUM_TYPE);
	} else {
		path_add(p, FDP_ENUM_TYPE);
	}
	path_add(p, e->index);
}

static SwFile *location_path(Path *p, SwKind k, const void *d)
{
	switch (k) {
	case SW_FILE:
		return (SwFile *)d;
	case SW_MESSAGE:
		message_path(p, d);
		return ((const SwMessage *)d)->file;
	case SW_ENUM:
		enum_path(p, d);
		return ((const SwEnum *)d)->file;
	case SW_ENUM_VALUE: {
		const SwEnumValue *v = d;
		enum_path(p, v->enum_type);
		path_add(p, EDP_VALUE);
		path_add(p, v->index);
		return v->enum_type->file;
	}
	case SW_ONEOF: {
		const SwOneof *o = d;
		message_path(p, o->containing_type);
		path_add(p, DP_ONEOF_DECL);
		path_add(p, o->index);
		return o->containing_type->file;
	}
	case SW_FIELD: {
		const SwField *f = d;
		if (f->is_extension) {
			if (f->extension_scope) {
				message_path(p, f->extension_scope);
				path_add(p, DP_EXTENSION);
			} else {
				path_add(p, FDP_EXTENSION);
			}
		} else {
			message_path(p, f->containing_type);
			path_add(p, DP_FIELD);
		}
		path_add(p, f->index);
		return f->file;
	}
	case SW_SERVICE:
		path_add(p, FDP_SERVICE);
		path_add(p, ((const SwService *)d)->index);
		return ((const SwService *)d)->file;
	case SW_METHOD: {
		const SwMethod *m = d;
		path_add(p, FDP_SERVICE);
		path_add(p, m->service->index);
		path_add(p, SDP_METHOD);
		path_add(p, m->index);
		return m->service->file;
	}
	}
	return NULL;
}

/* ---- Google_Protobuf_SourceCodeInfo+Extensions.swift ---- */

static bool is_blank(const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		uint32_t c;
		i += (size_t)sw_next_scalar(s + i, n - i, &c);
		if (!sw_is_whitespace(c))
			return false;
	}
	return true;
}

/* prefixLines(text:prefix:) */
static void prefix_lines(Buf *out, Str text, const char *prefix)
{
	/* replacingOccurrences(of: "\r\n", with: "\n") */
	Buf t = {0};
	for (size_t i = 0; i < text.n; i++) {
		if (text.p[i] == '\r' && i + 1 < text.n && text.p[i + 1] == '\n')
			continue;
		buf_addc(&t, text.p[i]);
	}
	const char *s = buf_str(&t);
	/* components(separatedBy: .newlines) */
	Vec starts = {0}, ends = {0};
	size_t start = 0;
	for (size_t i = 0; i < t.n;) {
		uint32_t c;
		int w = sw_next_scalar(s + i, t.n - i, &c);
		if (sw_is_newline(c)) {
			vec_push(&starts, (void *)(uintptr_t)start);
			vec_push(&ends, (void *)(uintptr_t)i);
			start = i + (size_t)w;
		}
		i += (size_t)w;
	}
	vec_push(&starts, (void *)(uintptr_t)start);
	vec_push(&ends, (void *)(uintptr_t)t.n);
	int n = starts.n;
	while (n > 0) {
		size_t a = (uintptr_t)starts.v[n - 1], b = (uintptr_t)ends.v[n - 1];
		if (!is_blank(s + a, b - a))
			break;
		n--;
	}
	for (int i = 0; i < n; i++) {
		size_t a = (uintptr_t)starts.v[i], b = (uintptr_t)ends.v[i];
		buf_adds(out, prefix);
		buf_add(out, s + a, b - a);
		buf_addc(out, '\n');
	}
}

Str sw_as_source_comment(const PbMsg *loc, const char *prefix, const char *detached_prefix)
{
	Buf result = {0};
	if (detached_prefix) {
		for (int i = 0; i < pb_count(loc, SCI_LOC_DETACHED); i++) {
			Buf comment = {0};
			prefix_lines(&comment, pb_bytes_at(loc, SCI_LOC_DETACHED, i), detached_prefix);
			if (comment.n) {
				buf_add(&result, comment.p, comment.n);
				buf_addc(&result, '\n');
			}
		}
	}
	Str comments = pb_has(loc, SCI_LOC_LEADING) ? pb_bytes(loc, SCI_LOC_LEADING) : pb_bytes(loc, SCI_LOC_TRAILING);
	prefix_lines(&result, comments, prefix);
	buf_str(&result);
	return buf_view(&result);
}

Str sw_proto_source_comments(SwKind k, const void *d, const char *prefix, const char *detached_prefix)
{
	Path p = {0};
	SwFile *file = location_path(&p, k, d);
	PbMsg *loc = sw_file_location(file, p.v, p.n);
	if (!loc)
		return STR_LIT("");
	return sw_as_source_comment(loc, prefix, detached_prefix);
}

/* ---- ProvidesDeprecationComment.swift ---- */

static Str type_or_file(const char *prefix, const char *type_name, bool deprecated, const SwFile *file)
{
	if (deprecated)
		return str_c(xcat(prefix, " NOTE: This ", type_name, " was marked as deprecated in the .proto file.\n"));
	if (!file->deprecated)
		return STR_LIT("");
	return str_c(xcat(prefix, " NOTE: The whole .proto file that defined this ", type_name,
			  " was marked as deprecated.\n"));
}

static Str simple(const char *prefix, const char *type_name, bool deprecated)
{
	if (!deprecated)
		return STR_LIT("");
	return str_c(xcat(prefix, " NOTE: This ", type_name, " was marked as deprecated in the .proto file\n"));
}

Str sw_deprecation_comment(SwKind k, const void *d, const char *prefix)
{
	switch (k) {
	case SW_MESSAGE: {
		const SwMessage *m = d;
		return type_or_file(prefix, "message", m->deprecated, m->file);
	}
	case SW_ENUM: {
		const SwEnum *e = d;
		return type_or_file(prefix, "enum", e->deprecated, e->file);
	}
	case SW_SERVICE: {
		const SwService *s = d;
		return type_or_file(prefix, "service", s->deprecated, s->file);
	}
	case SW_ENUM_VALUE:
		return simple(prefix, "enum value", ((const SwEnumValue *)d)->deprecated);
	case SW_METHOD:
		return simple(prefix, "method", ((const SwMethod *)d)->deprecated);
	case SW_FIELD: {
		const SwField *f = d;
		if (f->deprecated)
			return str_c(xcat(prefix, " NOTE: This ", f->is_extension ? "extension field" : "field",
					  " was marked as deprecated in the .proto file.\n"));
		if (f->is_extension && f->file->deprecated)
			return str_c(xcat(prefix,
					  " NOTE: The whole .proto file that defined this extension field was marked as deprecated.\n"));
		return STR_LIT("");
	}
	case SW_FILE:
	case SW_ONEOF:
		break;
	}
	return STR_LIT("");
}

Str sw_proto_source_comments_with_deprecation(SwKind k, const void *d, const char *prefix, const char *detached_prefix)
{
	Str comments = sw_proto_source_comments(k, d, prefix, detached_prefix);
	Str deprecation = sw_deprecation_comment(k, d, prefix);
	if (!deprecation.n)
		return comments;
	if (!comments.n)
		return deprecation;
	Buf b = {0};
	buf_add_str(&b, comments);
	buf_adds(&b, prefix);
	buf_addc(&b, '\n');
	buf_add_str(&b, deprecation);
	return buf_view(&b);
}
