/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/Descriptor+Extensions.swift and
 * SwiftProtobufNamer+Extensions.swift. */

/* Recursion guard for walks over message references (upstream recurses without a limit). */
#define MAX_RECURSION 10000

/* ---- FileDescriptor / Descriptor ---- */

static bool message_needs_foundation_import(const SwMessage *m, int depth)
{
	if (depth > MAX_RECURSION)
		fail("messages nested too deeply");
	for (int i = 0; i < m->fields.n; i++)
		if (VEC_AT(m->fields, SwField, i)->type == PT_BYTES)
			return true;
	for (int i = 0; i < m->extensions.n; i++)
		if (VEC_AT(m->extensions, SwField, i)->type == PT_BYTES)
			return true;
	for (int i = 0; i < m->messages.n; i++)
		if (message_needs_foundation_import(m->messages.v[i], depth + 1))
			return true;
	return false;
}

static bool file_needs_foundation_import(const SwFile *f)
{
	for (int i = 0; i < f->extensions.n; i++)
		if (VEC_AT(f->extensions, SwField, i)->type == PT_BYTES)
			return true;
	for (int i = 0; i < f->messages.n; i++)
		if (message_needs_foundation_import(f->messages.v[i], 0))
			return true;
	return false;
}

/* computeImports(namer:directive:reexportPublicImports:): without module mappings (the port
 * cannot load them, see load_module_mappings) everything is in one module and no import is
 * needed. */
static const char *compute_imports(const SwNamer *namer)
{
	if (!namer->mappings->has_mappings)
		return "";
	fail("module mappings are not supported");
}

static bool required_helper(const SwMessage *d, Map *seen, int depth)
{
	if (depth > MAX_RECURSION)
		fail("messages nested too deeply");
	if (map_has(seen, d->full_name))
		return false;
	map_put(seen, d->full_name, (void *)d);
	if (d->n_extension_ranges)
		return true;
	for (int i = 0; i < d->fields.n; i++) {
		const SwField *f = d->fields.v[i];
		if (sw_field_is_required(f))
			return true;
		if (f->message_type && required_helper(f->message_type, seen, depth + 1))
			return true;
	}
	return false;
}

/* containsRequiredFields() */
static bool contains_required_fields(const SwMessage *m)
{
	Map seen = {0};
	return required_helper(m, &seen, 0);
}

/* _normalizedExtensionRanges */
static int normalized_extension_ranges(const SwMessage *m, SwRange **out)
{
	SwRange *r = xalloc(sizeof(SwRange) * (size_t)(m->n_extension_ranges ? m->n_extension_ranges : 1));
	for (int i = 0; i < m->n_extension_ranges; i++) {
		r[i] = m->extension_ranges[i];
		if (r[i].end < r[i].start)
			fail("Range requires lowerBound <= upperBound");
	}
	*out = r;
	return sort_and_merge_continuous(r, m->n_extension_ranges);
}

static int cmp_desc_int64(const void *a, const void *b)
{
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return x > y ? -1 : x < y;
}

/* _ambitiousExtensionRanges */
static int ambitious_extension_ranges(const SwMessage *m, SwRange **out)
{
	SwRange *merged;
	int n = normalized_extension_ranges(m, &merged);
	*out = merged;
	if (n <= 1)
		return n;
	int nf = m->fields.n;
	int64_t *numbers = xalloc(sizeof(int64_t) * (size_t)(nf ? nf : 1));
	for (int i = 0; i < nf; i++)
		numbers[i] = VEC_AT(m->fields, SwField, i)->number;
	qsort(numbers, (size_t)nf, sizeof *numbers, cmp_desc_int64);
	int it = 0;
#define NEXT_FIELD() (it < nf ? &numbers[it++] : NULL)
	int64_t *next = NEXT_FIELD();
	while (next && merged[n - 1].start < *next)
		next = NEXT_FIELD();
	for (int i = n - 2; i >= 0; i--) {
		if (!next || merged[i].start > *next) {
			merged[i].end = merged[i + 1].end;
			memmove(&merged[i + 1], &merged[i + 2], sizeof(SwRange) * (size_t)(n - i - 2));
			n--;
		} else {
			while (next && merged[i].start < *next)
				next = NEXT_FIELD();
		}
	}
#undef NEXT_FIELD
	return n;
}

/* ---- FieldDescriptor ---- */

static const char *scalar_swift_type(int type)
{
	switch (type) {
	case PT_DOUBLE: return "Double";
	case PT_FLOAT: return "Float";
	case PT_INT64: return "Int64";
	case PT_UINT64: return "UInt64";
	case PT_INT32: return "Int32";
	case PT_FIXED64: return "UInt64";
	case PT_FIXED32: return "UInt32";
	case PT_BOOL: return "Bool";
	case PT_STRING: return "String";
	case PT_BYTES: return "Data";
	case PT_UINT32: return "UInt32";
	case PT_SFIXED32: return "Int32";
	case PT_SFIXED64: return "Int64";
	case PT_SINT32: return "Int32";
	case PT_SINT64: return "Int64";
	}
	fail("unknown field type %d", type);
}

static char *swift_type(SwNamer *namer, const SwField *f)
{
	SwField *key, *value;
	if (f->message_type && sw_message_map_key_value(f->message_type, &key, &value))
		return xcat("Dictionary<", swift_type(namer, key), ",", swift_type(namer, value), ">");
	const char *result;
	switch (f->type) {
	case PT_GROUP:
	case PT_MESSAGE: result = sw_full_name_message(namer, f->message_type); break;
	case PT_ENUM: result = sw_full_name_enum(namer, f->enum_type); break;
	default: result = scalar_swift_type(f->type);
	}
	if (sw_field_is_repeated(f))
		return xcat("[", result, "]");
	return xstrdup(result);
}

static char *swift_storage_type(SwNamer *namer, const SwField *f)
{
	char *t = swift_type(namer, f);
	if (sw_field_is_repeated(f) || sw_field_real_containing_oneof(f))
		return t;
	return sw_field_has_presence(f) ? xcat(t, "?") : t;
}

static const char *proto_generic_type(const SwField *f)
{
	if (sw_field_is_map(f))
		fail("protoGenericType of a map field");
	switch (f->type) {
	case PT_DOUBLE: return "Double";
	case PT_FLOAT: return "Float";
	case PT_INT64: return "Int64";
	case PT_UINT64: return "UInt64";
	case PT_INT32: return "Int32";
	case PT_FIXED64: return "Fixed64";
	case PT_FIXED32: return "Fixed32";
	case PT_BOOL: return "Bool";
	case PT_STRING: return "String";
	case PT_GROUP: return "Group";
	case PT_MESSAGE: return "Message";
	case PT_BYTES: return "Bytes";
	case PT_UINT32: return "UInt32";
	case PT_ENUM: return "Enum";
	case PT_SFIXED32: return "SFixed32";
	case PT_SFIXED64: return "SFixed64";
	case PT_SINT32: return "SInt32";
	case PT_SINT64: return "SInt64";
	}
	fail("unknown field type %d", f->type);
}

/* EnumDescriptor.value(named:) */
static const SwEnumValue *enum_value_named(const SwEnum *e, Str name)
{
	for (int i = 0; i < e->values.n; i++) {
		const SwEnumValue *v = e->values.v[i];
		if (str_eqc(name, v->name))
			return v;
	}
	return NULL;
}

static char *swift_default_value(SwNamer *namer, const SwField *f)
{
	if (sw_field_is_map(f))
		return "[:]";
	if (sw_field_is_repeated(f))
		return "[]";
	if (f->has_default) {
		Str d = f->default_value;
		switch (f->type) {
		case PT_DOUBLE:
		case PT_FLOAT: {
			const char *t = f->type == PT_DOUBLE ? "Double" : "Float";
			if (str_eqc(d, "inf"))
				return xcat(t, ".infinity");
			if (str_eqc(d, "-inf"))
				return xcat("-", t, ".infinity");
			if (str_eqc(d, "nan") || str_eqc(d, "-nan"))
				return xcat(t, ".nan");
			return xstrndup(d.p, d.n);
		}
		case PT_STRING: return string_to_escaped_string_literal(d);
		case PT_BYTES: return escaped_to_data_literal(d);
		case PT_ENUM: {
			const SwEnumValue *v = enum_value_named(f->enum_type, d);
			if (!v)
				fail("%s: unknown enum default value", f->name);
			return sw_dotted_relative_name_enum_value(namer, v);
		}
		default: return xstrndup(d.p, d.n);
		}
	}
	switch (f->type) {
	case PT_BOOL: return "false";
	case PT_STRING: return "String()";
	case PT_BYTES: return "Data()";
	case PT_GROUP:
	case PT_MESSAGE: return xcat(sw_full_name_message(namer, f->message_type), "()");
	case PT_ENUM:
		if (!f->enum_type->values.n)
			fail("%s has no values", f->enum_type->full_name);
		return sw_dotted_relative_name_enum_value(namer, f->enum_type->values.v[0]);
	default: return "0";
	}
}

static char *traits_type(SwNamer *namer, const SwField *f)
{
	const char *p = sw_namer_swift_protobuf_module_prefix(namer);
	SwField *key, *value;
	if (f->message_type && sw_message_map_key_value(f->message_type, &key, &value)) {
		char *k = traits_type(namer, key), *v = traits_type(namer, value);
		const char *kind = value->type == PT_MESSAGE ? "_ProtobufMessageMap" : value->type == PT_ENUM ? "_ProtobufEnumMap" : "_ProtobufMap";
		return xcat(p, kind, "<", k, ",", v, ">");
	}
	switch (f->type) {
	case PT_DOUBLE: return xcat(p, "ProtobufDouble");
	case PT_FLOAT: return xcat(p, "ProtobufFloat");
	case PT_INT64: return xcat(p, "ProtobufInt64");
	case PT_UINT64: return xcat(p, "ProtobufUInt64");
	case PT_INT32: return xcat(p, "ProtobufInt32");
	case PT_FIXED64: return xcat(p, "ProtobufFixed64");
	case PT_FIXED32: return xcat(p, "ProtobufFixed32");
	case PT_BOOL: return xcat(p, "ProtobufBool");
	case PT_STRING: return xcat(p, "ProtobufString");
	case PT_GROUP:
	case PT_MESSAGE: return sw_full_name_message(namer, f->message_type);
	case PT_BYTES: return xcat(p, "ProtobufBytes");
	case PT_UINT32: return xcat(p, "ProtobufUInt32");
	case PT_ENUM: return sw_full_name_enum(namer, f->enum_type);
	case PT_SFIXED32: return xcat(p, "ProtobufSFixed32");
	case PT_SFIXED64: return xcat(p, "ProtobufSFixed64");
	case PT_SINT32: return xcat(p, "ProtobufSInt32");
	case PT_SINT64: return xcat(p, "ProtobufSInt64");
	}
	fail("unknown field type %d", f->type);
}

/* ---- EnumDescriptor.ValueAliasInfo ---- */

typedef struct {
	Vec main_values;  /* SwEnumValue * */
	Vec **aliases;    /* by value index: the aliases of a main value (NULL: none) */
	SwEnumValue **alias_of; /* by value index: the original of an alias */
} ValueAliasInfo;

static ValueAliasInfo *value_alias_info(const SwEnum *e)
{
	ValueAliasInfo *a = xalloc(sizeof *a);
	int n = e->values.n;
	a->aliases = xalloc(sizeof(Vec *) * (size_t)(n ? n : 1));
	a->alias_of = xalloc(sizeof(SwEnumValue *) * (size_t)(n ? n : 1));
	Map first_values = {0};
	for (int i = 0; i < n; i++) {
		SwEnumValue *v = e->values.v[i];
		char *key = xfmt("%d", (int)v->number);
		SwEnumValue *aliasing = map_get(&first_values, key);
		if (aliasing) {
			if (!a->aliases[aliasing->index])
				a->aliases[aliasing->index] = xalloc(sizeof(Vec));
			vec_push(a->aliases[aliasing->index], v);
			a->alias_of[v->index] = aliasing;
		} else {
			map_put(&first_values, key, v);
			vec_push(&a->main_values, v);
		}
	}
	return a;
}

/* SwiftProtobufNamer.uniquelyNamedValues(valueAliasInfo:) */
static Vec uniquely_named_values(SwNamer *namer, const SwEnum *e, const ValueAliasInfo *a)
{
	if (!a->main_values.n)
		fail("%s has no values", e->full_name);
	Vec r = {0};
	for (int i = 0; i < e->values.n; i++) {
		SwEnumValue *v = e->values.v[i];
		const SwEnumValue *alias_of = a->alias_of[v->index];
		if (!alias_of) {
			vec_push(&r, v);
			continue;
		}
		const char *relative = sw_relative_name_enum_value(namer, v);
		if (str_eq(relative, sw_relative_name_enum_value(namer, alias_of)))
			continue;
		const Vec *aliases = a->aliases[alias_of->index];
		for (int j = 0; j < aliases->n; j++) {
			if (str_eq(sw_relative_name_enum_value(namer, aliases->v[j]), relative)) {
				if (aliases->v[j] == v)
					vec_push(&r, v);
				break;
			}
		}
	}
	return r;
}
