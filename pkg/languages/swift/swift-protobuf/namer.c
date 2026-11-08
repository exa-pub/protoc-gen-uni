/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/SwiftProtobufNamer.swift,
 * ProtoFileToModuleMappings.swift and SwiftProtobufInfo.swift (swift-protobuf 1.39.0). */
#include "swift_protobuf.h"

#include <string.h>

/* ---- SwiftProtobufInfo.swift ---- */

static const char *const bundled_proto_files[] = {
	"google/protobuf/any.proto", "google/protobuf/api.proto", "google/protobuf/descriptor.proto",
	"google/protobuf/duration.proto", "google/protobuf/empty.proto", "google/protobuf/field_mask.proto",
	"google/protobuf/source_context.proto", "google/protobuf/struct.proto",
	"google/protobuf/timestamp.proto", "google/protobuf/type.proto", "google/protobuf/wrappers.proto",
};
#define N_BUNDLED (int)(sizeof bundled_proto_files / sizeof bundled_proto_files[0])

bool sw_is_bundled_proto(const SwFile *file)
{
	if (!str_eq(file->package, "google.protobuf"))
		return false;
	for (int i = 0; i < N_BUNDLED; i++)
		if (str_eq(file->name, bundled_proto_files[i]))
			return true;
	return false;
}

/* ---- ProtoFileToModuleMappings.swift ---- */

SwModuleMappings *sw_module_mappings(const char *swift_protobuf_module_name)
{
	SwModuleMappings *m = xalloc(sizeof *m);
	m->swift_protobuf_module_name = swift_protobuf_module_name ? swift_protobuf_module_name : "SwiftProtobuf";
	for (int i = 0; i < N_BUNDLED; i++)
		map_put(&m->mappings, bundled_proto_files[i], (void *)m->swift_protobuf_module_name);
	return m;
}

/* String.debugDescription of an associated value in an error description. */
char *sw_swift_debug_quote(const char *s)
{
	char *q = sw_debug_description(str_c(s));
	if (!q)
		fail("unsupported by protoc-gen-uni: non-ASCII character in %s", sw_quote(str_c(s)));
	return q;
}

const char *sw_module_mappings_add(SwModuleMappings *m, int index, const char *module_name, const Vec *proto_file_paths)
{
	if (!*module_name)
		return xfmt("entryMissingModuleName(mappingIndex: %d)", index);
	if (!proto_file_paths->n)
		return xfmt("entryHasNoProtoPaths(mappingIndex: %d)", index);
	for (int i = 0; i < proto_file_paths->n; i++) {
		const char *path = proto_file_paths->v[i];
		const char *existing = map_get(&m->mappings, path);
		if (existing) {
			if (!str_eq(existing, module_name))
				return xfmt("duplicateProtoPathMapping(path: %s, firstModule: %s, secondModule: %s)",
					    sw_swift_debug_quote(path), sw_swift_debug_quote(existing), sw_swift_debug_quote(module_name));
		} else {
			map_put(&m->mappings, path, (void *)module_name);
		}
	}
	m->has_mappings = (int)m->mappings.n != N_BUNDLED;
	return NULL;
}

const char *sw_module_name_for_file(const SwModuleMappings *m, const SwFile *file) { return map_get(&m->mappings, file->name); }

/* ---- SwiftProtobufNamer.swift ---- */

SwNamer *sw_namer(const SwFile *file, SwModuleMappings *mappings)
{
	SwNamer *n = xalloc(sizeof *n);
	n->mappings = mappings;
	const char *target = sw_module_name_for_file(mappings, file);
	n->target_module = target ? target : "";
	return n;
}

const char *sw_namer_swift_protobuf_module_name(const SwNamer *n) { return n->mappings->swift_protobuf_module_name; }

const char *sw_namer_swift_protobuf_module_prefix(const SwNamer *n)
{
	if (str_eq(n->target_module, n->mappings->swift_protobuf_module_name))
		return "";
	return xcat(n->mappings->swift_protobuf_module_name, ".");
}

char *sw_relative_name_message(SwNamer *n, const SwMessage *m)
{
	const char *forbidden = sw_namer_swift_protobuf_module_name(n);
	if (m->containing_type)
		return sw_sanitize_message_name(m->name, forbidden);
	return sw_sanitize_message_name(xcat(sw_namer_type_prefix(n, m->file), m->name), forbidden);
}

char *sw_full_name_message(SwNamer *n, const SwMessage *m)
{
	char *relative = sw_relative_name_message(n, m);
	if (!m->containing_type)
		return xcat(sw_namer_module_prefix(n, m->file), relative);
	return xcat(sw_full_name_message(n, m->containing_type), ".", relative);
}

char *sw_relative_name_enum(SwNamer *n, const SwEnum *e)
{
	const char *forbidden = sw_namer_swift_protobuf_module_name(n);
	if (e->containing_type)
		return sw_sanitize_enum_name(e->name, forbidden);
	return sw_sanitize_enum_name(xcat(sw_namer_type_prefix(n, e->file), e->name), forbidden);
}

char *sw_full_name_enum(SwNamer *n, const SwEnum *e)
{
	char *relative = sw_relative_name_enum(n, e);
	if (!e->containing_type)
		return xcat(sw_namer_module_prefix(n, e->file), relative);
	return xcat(sw_full_name_message(n, e->containing_type), ".", relative);
}

/* computeRelativeNames(enum:). Upstream buckets the candidates in a Dictionary and walks it in
 * random order; that only fills the cache, so the walk here follows the first appearance. */
static void compute_relative_names(SwNamer *n, const SwEnum *e)
{
	int nv = e->values.n;
	char **candidate = xalloc(sizeof(char *) * (size_t)(nv ? nv : 1));
	Map buckets = {0}; /* candidate -> Vec of indexes */
	Vec order = {0};   /* the buckets in order of first appearance */
	for (int i = 0; i < nv; i++) {
		const SwEnumValue *v = e->values.v[i];
		char *stripped = sw_strip_prefix(e->name, v->name);
		candidate[i] = sw_to_lower_camel(stripped ? stripped : v->name);
		Vec *bucket = map_get(&buckets, candidate[i]);
		if (!bucket) {
			bucket = xalloc(sizeof *bucket);
			map_put(&buckets, candidate[i], bucket);
			vec_push(&order, bucket);
		}
		vec_push(bucket, (void *)(intptr_t)i);
	}
	for (int b = 0; b < order.n; b++) {
		Vec *bucket = order.v[b];
		int first_index = (int)(intptr_t)bucket->v[0];
		const char *camel_cased = candidate[first_index];
		if (bucket->n == 1) {
			const SwEnumValue *v = e->values.v[first_index];
			map_put(&n->enum_value_cache, v->full_name, sw_sanitize_enum_case_name(camel_cased));
			continue;
		}
		int32_t first_value = VEC_AT(e->values, SwEnumValue, first_index)->number;
		bool has_multiple_values = false;
		for (int j = 0; j < bucket->n; j++)
			if (VEC_AT(e->values, SwEnumValue, (int)(intptr_t)bucket->v[j])->number != first_value)
				has_multiple_values = true;
		if (!has_multiple_values) {
			char *name = sw_sanitize_enum_case_name(camel_cased);
			for (int j = 0; j < bucket->n; j++)
				map_put(&n->enum_value_cache, VEC_AT(e->values, SwEnumValue, (int)(intptr_t)bucket->v[j])->full_name, name);
			continue;
		}
		for (int j = 0; j < bucket->n; j++) {
			const SwEnumValue *v = e->values.v[(int)(intptr_t)bucket->v[j]];
			char *suffix = v->number >= 0 ? xfmt("_%d", (int)v->number) : xfmt("_n%lld", -(long long)v->number);
			map_put(&n->enum_value_cache, v->full_name, sw_sanitize_enum_case_name(xcat(camel_cased, suffix)));
		}
	}
}

const char *sw_relative_name_enum_value(SwNamer *n, const SwEnumValue *v)
{
	const char *name = map_get(&n->enum_value_cache, v->full_name);
	if (name)
		return name;
	compute_relative_names(n, v->enum_type);
	return map_get(&n->enum_value_cache, v->full_name);
}

char *sw_full_name_enum_value(SwNamer *n, const SwEnumValue *v)
{
	return xcat(sw_full_name_enum(n, v->enum_type), ".", sw_relative_name_enum_value(n, v));
}

char *sw_dotted_relative_name_enum_value(SwNamer *n, const SwEnumValue *v)
{
	return xcat(".", sw_trim_backticks(sw_relative_name_enum_value(n, v)));
}

char *sw_relative_name_oneof(SwNamer *n, const SwOneof *o)
{
	return sw_sanitize_oneof_name(xcat("OneOf_", sw_to_upper_camel(o->name)), sw_namer_swift_protobuf_module_name(n));
}

char *sw_full_name_oneof(SwNamer *n, const SwOneof *o)
{
	return xcat(sw_full_name_message(n, o->containing_type), ".", sw_relative_name_oneof(n, o));
}

char *sw_relative_name_extension(SwNamer *n, const SwField *f)
{
	if (f->extension_scope)
		return sw_sanitize_message_scoped_extension_name(sw_field_naming_base(f));
	return xcat(sw_namer_type_prefix(n, f->file), "Extensions_", sw_field_naming_base(f));
}

char *sw_full_name_extension(SwNamer *n, const SwField *f)
{
	char *relative = sw_relative_name_extension(n, f);
	if (!f->extension_scope)
		return xcat(sw_namer_module_prefix(n, f->file), relative);
	return xcat(sw_full_name_message(n, f->extension_scope), ".Extensions.", sw_trim_backticks(relative));
}

SwFieldNames sw_message_property_names(SwNamer *n, const SwField *f, const char *prefixed, bool include_has_and_clear)
{
	(void)n;
	const char *base = sw_field_naming_base(f);
	char *lower = sw_to_lower_camel(base);
	SwFieldNames r = {0};
	r.name = sw_sanitize_field_name(lower, lower);
	r.prefixed = *prefixed ? sw_sanitize_field_name(xcat(prefixed, lower), lower) : "";
	r.has = r.clear = "";
	if (!include_has_and_clear)
		return r;
	char *upper = sw_to_upper_camel(base);
	r.has = sw_sanitize_field_name(xcat("has", upper), lower);
	r.clear = sw_sanitize_field_name(xcat("clear", upper), lower);
	return r;
}

SwFieldNames sw_message_property_name_oneof(SwNamer *n, const SwOneof *o, const char *prefixed)
{
	(void)n;
	char *lower = sw_to_lower_camel(o->name);
	SwFieldNames r = {0};
	r.name = sw_sanitize_field_name(lower, lower);
	r.prefixed = sw_sanitize_field_name(xcat(prefixed, lower), lower);
	r.has = r.clear = "";
	return r;
}

SwFieldNames sw_message_property_names_extension(SwNamer *n, const SwField *f)
{
	char *base = sw_to_lower_camel(sw_field_naming_base(f));
	SwFieldNames r = {0};
	r.prefixed = "";
	if (f->extension_scope) {
		r.name = sw_periods_to_underscores(xcat(sw_full_name_message(n, f->extension_scope), "_", base));
		char *first_up = sw_uppercase_first(r.name);
		r.has = xcat("has", first_up);
		r.clear = xcat("clear", first_up);
	} else {
		const char *swift_prefix = sw_namer_type_prefix(n, f->file);
		r.name = sw_sanitize_field_name(xcat(swift_prefix, base), xcat(swift_prefix, base));
		if (!*swift_prefix) {
			char *upper_cleaned = sw_sanitize_field_name(sw_to_upper_camel(sw_field_naming_base(f)), base);
			r.has = xcat("has", upper_cleaned);
			r.clear = xcat("clear", upper_cleaned);
		} else {
			char *first_up = sw_uppercase_first(r.name);
			r.has = xcat("has", first_up);
			r.clear = xcat("clear", first_up);
		}
	}
	return r;
}

const char *sw_namer_type_prefix(SwNamer *n, const SwFile *file)
{
	const char *r = map_get(&n->file_prefix_cache, file->name);
	if (r)
		return r;
	r = sw_type_prefix(file->package, file->options);
	map_put(&n->file_prefix_cache, file->name, (void *)r);
	return r;
}

const char *sw_namer_module_prefix(SwNamer *n, const SwFile *file)
{
	const char *prefix = sw_module_name_for_file(n->mappings, file);
	if (!prefix || str_eq(prefix, n->target_module))
		return "";
	return xcat(prefix, ".");
}
