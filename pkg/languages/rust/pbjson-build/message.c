/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/message.rs of pbjson-build 0.8.0: the resolved form of a message (fields, one-ofs,
 * field types and modifiers). Upstream panics become fail(). */

enum { SC_F64, SC_F32, SC_I32, SC_I64, SC_U32, SC_U64, SC_BOOL, SC_STRING, SC_BYTES };

static const char *scalar_rust_type(int s)
{
	static const char *const names[] = {"f64", "f32", "i32", "i64", "u32", "u64", "bool", "String", "Vec<u8>"};
	return names[s];
}

static bool scalar_is_numeric(int s) { return s <= SC_U64; }

enum { FT_SCALAR, FT_ENUM, FT_MESSAGE, FT_MAP };

typedef struct FieldType {
	int kind;
	int scalar;                  /* FT_SCALAR; FT_MAP: the key */
	const TypePath *path;        /* FT_ENUM, FT_MESSAGE */
	const struct FieldType *value; /* FT_MAP */
} FieldType;

enum { FM_REQUIRED, FM_OPTIONAL, FM_USE_DEFAULT, FM_REPEATED };

typedef struct {
	const char *name;
	const char *json_name; /* NULL if not set */
	int modifier;
	const FieldType *type;
} Field;

/* Field::rust_type_name */
static char *field_rust_type_name(const Field *f) { return upper_camel_type(f->name); }
/* Field::rust_field_name, OneOf::rust_field_name */
static char *field_rust_field_name(const char *name) { return snake_ident(name); }
/* Field::json_name */
static const char *field_json_name(const Field *f) { return f->json_name ? f->json_name : heck_lower_camel(ascii_identifier(f->name)); }

typedef struct {
	const char *name;
	const TypePath *path;
	Vec fields; /* Field * */
} OneOf;

typedef struct {
	const TypePath *path;
	Vec fields;  /* Field * */
	Vec one_ofs; /* OneOf * */
} Message;

static const FieldType *field_type(const DescriptorSet *set, const PbMsg *field, int depth);

/* Upstream recurses without a limit through map entries whose value is a map entry, and
 * overflows the stack on a cycle. */
enum { MAX_MAP_DEPTH = 64 };

static const FieldType *resolve_type(const DescriptorSet *set, const char *type_name, int depth)
{
	if (*type_name != '.')
		fail("pbjson does not currently support resolving relative types");
	if (depth > MAX_MAP_DEPTH)
		fail("stack overflow resolving map type: %s", type_name);
	const Descriptor *d = descriptor_set_find(set, type_name);
	if (!d)
		fail("failed to resolve type: %s", type_name);
	FieldType *t = xalloc(sizeof *t);
	t->path = d->path;
	if (d->kind == DESC_ENUM) {
		t->kind = FT_ENUM;
	} else if (descriptor_is_map(d)) {
		if (pb_count(d->proto, DP_FIELD) != 2)
			fail("expected map to have 2 fields");
		const PbMsg *key = pb_msg_at(d->proto, DP_FIELD, 0), *value = pb_msg_at(d->proto, DP_FIELD, 1);
		if (!str_eq(pb_str(key, FD_NAME), "key") || !str_eq(pb_str(value, FD_NAME), "value"))
			fail("assertion failed: map entry fields are not key and value");
		const FieldType *k = field_type(set, key, depth + 1);
		if (k->kind != FT_SCALAR)
			fail("non scalar map key");
		t->kind = FT_MAP;
		t->scalar = k->scalar;
		t->value = field_type(set, value, depth + 1);
		t->path = NULL;
	} else {
		/* This may actually be a group; pbjson does not distinguish them. */
		t->kind = FT_MESSAGE;
	}
	return t;
}

static const FieldType *field_type(const DescriptorSet *set, const PbMsg *field, int depth)
{
	if (pb_has(field, FD_TYPE_NAME))
		return resolve_type(set, pb_str(field, FD_TYPE_NAME), depth);
	if (!pb_has(field, FD_TYPE))
		fail("expected type");
	int s;
	switch (pb_int(field, FD_TYPE, 0)) {
	case PT_DOUBLE: s = SC_F64; break;
	case PT_FLOAT: s = SC_F32; break;
	case PT_INT64: case PT_SFIXED64: case PT_SINT64: s = SC_I64; break;
	case PT_INT32: case PT_SFIXED32: case PT_SINT32: s = SC_I32; break;
	case PT_UINT64: case PT_FIXED64: s = SC_U64; break;
	case PT_UINT32: case PT_FIXED32: s = SC_U32; break;
	case PT_BOOL: s = SC_BOOL; break;
	case PT_STRING: s = SC_STRING; break;
	case PT_BYTES: s = SC_BYTES; break;
	case PT_MESSAGE: case PT_ENUM: case PT_GROUP: fail("no type name specified");
	default: fail("valid type");
	}
	FieldType *t = xalloc(sizeof *t);
	t->kind = FT_SCALAR;
	t->scalar = s;
	return t;
}

static int field_modifier(const Descriptor *message, const PbMsg *field, const FieldType *type)
{
	if (!pb_has(field, FD_LABEL))
		fail("expected label");
	int64_t label = pb_int(field, FD_LABEL, 0);
	if (label != LABEL_OPTIONAL && label != LABEL_REQUIRED && label != LABEL_REPEATED)
		fail("valid label");
	if (pb_int(field, FD_PROTO3_OPTIONAL, 0) || pb_has(field, FD_ONEOF_INDEX)) {
		if (label != LABEL_OPTIONAL)
			fail("assertion `left == right` failed");
		return FM_OPTIONAL;
	}
	if (type->kind == FT_MAP) {
		if (label != LABEL_REPEATED)
			fail("assertion `left == right` failed");
		return FM_REPEATED;
	}
	switch (label) {
	case LABEL_OPTIONAL:
		if (message->syntax == SYNTAX_PROTO2 || type->kind == FT_MESSAGE)
			return FM_OPTIONAL;
		return FM_USE_DEFAULT;
	case LABEL_REQUIRED:
		return FM_REQUIRED;
	default:
		return FM_REPEATED;
	}
}

/* resolve_message: NULL for an auto-generated map entry. */
static Message *resolve_message(const DescriptorSet *set, const Descriptor *message)
{
	if (descriptor_is_map(message))
		return NULL;
	Message *m = xalloc(sizeof *m);
	m->path = message->path;
	int n_oneofs = pb_count(message->proto, DP_ONEOF_DECL);
	Vec *one_of_fields = xalloc(sizeof *one_of_fields * (size_t)(n_oneofs ? n_oneofs : 1));
	for (int i = 0; i < pb_count(message->proto, DP_FIELD); i++) {
		const PbMsg *field = pb_msg_at(message->proto, DP_FIELD, i);
		const FieldType *type = field_type(set, field, 0);
		int modifier = field_modifier(message, field, type);
		if (!pb_has(field, FD_NAME))
			fail("expected field to have name");
		Field *f = xalloc(sizeof *f);
		f->name = pb_str(field, FD_NAME);
		f->json_name = pb_has(field, FD_JSON_NAME) ? pb_str(field, FD_JSON_NAME) : NULL;
		f->type = type;
		f->modifier = modifier;
		/* Treat synthetic one-of as normal */
		if (pb_has(field, FD_ONEOF_INDEX) && !pb_int(field, FD_PROTO3_OPTIONAL, 0)) {
			int64_t idx = (int32_t)pb_int(field, FD_ONEOF_INDEX, 0);
			if (idx < 0 || idx >= n_oneofs)
				fail("index out of bounds: the len is %d but the index is %lld", n_oneofs, (long long)idx);
			vec_push(&one_of_fields[idx], f);
		} else {
			vec_push(&m->fields, f);
		}
	}
	for (int i = 0; i < n_oneofs; i++) {
		/* Might be empty in the event of a synthetic one-of */
		if (!one_of_fields[i].n)
			continue;
		const PbMsg *decl = pb_msg_at(message->proto, DP_ONEOF_DECL, i);
		if (!pb_has(decl, ODP_NAME))
			fail("oneof with no name");
		OneOf *o = xalloc(sizeof *o);
		o->name = pb_str(decl, ODP_NAME);
		o->path = type_path_child(message->path, o->name);
		o->fields = one_of_fields[i];
		vec_push(&m->one_ofs, o);
	}
	return m;
}
