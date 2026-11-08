/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* Port of cmd/protoc-gen-openapi/generator/reflector.go (gnostic v0.7.1). The list of required
 * schemas is the scheduler of generator.c (schedule_required). */

#define protobufValueName "GoogleProtobufValue"
#define protobufAnyName "GoogleProtobufAny"

static Str get_message_name(const GoMessage *message)
{
	const GoDesc *parent = message->d.parent;
	if (parent && parent->kind == GD_MESSAGE)
		return str_c(xcat(parent->name, "_", message->d.name));
	return str_c(message->d.name);
}

/* fullMessageTypeName builds the full type name of a message. */
static Str full_message_type_name(const GoMessage *message)
{
	return str_c(xcat(".", message->d.file->d.full_name, ".", get_message_name(message).p));
}

/* formatMessageName of a message given by its package, getMessageName and type name. */
static Str format_message_name_of(const Configuration *conf, const char *package, Str name, Str type_name)
{
	if (!conf->FQSchemaNaming) {
		if (str_eqc(type_name, ".google.protobuf.Value"))
			name = STR_LIT(protobufValueName);
		else if (str_eqc(type_name, ".google.protobuf.Any"))
			name = STR_LIT(protobufAnyName);
	}
	if (str_eqc(conf->Naming, "json")) {
		/* names are ASCII (protodesc validates them): strings.ToUpper and ToLower of one
		 * byte */
		if (name.n > 1) {
			char *s = xstrndup(name.p, name.n);
			if (s[0] >= 'a' && s[0] <= 'z')
				s[0] = (char)(s[0] - 32);
			name = (Str){s, name.n};
		}
		if (name.n == 1) {
			char *s = xstrndup(name.p, 1);
			if (s[0] >= 'A' && s[0] <= 'Z')
				s[0] = (char)(s[0] + 32);
			name = (Str){s, 1};
		}
	}
	if (conf->FQSchemaNaming)
		name = str_c(xcat(package, ".", name.p));
	return name;
}

static Str format_message_name(const Configuration *conf, const GoMessage *message)
{
	return format_message_name_of(conf, message->d.file->d.full_name, get_message_name(message), full_message_type_name(message));
}

static Str format_field_name(const Configuration *conf, const GoField *field)
{
	if (str_eqc(conf->Naming, "proto"))
		return str_c(field->d.name);
	return field->json_name;
}

static PbMsg *schema_or_reference_for_message(Gen *g, const GoMessage *message);

static Str response_content_for_message(Gen *g, const GoMessage *message, PbMsg **content)
{
	Str type_name = full_message_type_name(message);
	if (str_eqc(type_name, ".google.protobuf.Empty")) {
		*content = oa_new(OA_MediaTypes);
		return STR_LIT("200");
	}
	if (str_eqc(type_name, ".google.api.HttpBody")) {
		*content = wk_NewGoogleApiHttpBodyMediaType();
		return STR_LIT("200");
	}
	*content = wk_NewApplicationJsonMediaType(schema_or_reference_for_message(g, message));
	return STR_LIT("200");
}

static Str schema_reference_for_message(Gen *g, const GoMessage *message)
{
	Str schema_name = format_message_name(g->conf, message);
	require_schema(g, schema_name);
	return str_c(xcat("#/components/schemas/", schema_name.p));
}

/* Returns a full schema for simple types, and a schema reference for complex types that
 * reference the definition in `#/components/schemas/` */
static PbMsg *schema_or_reference_for_message(Gen *g, const GoMessage *message)
{
	Str t = full_message_type_name(message);
	if (str_eqc(t, ".google.api.HttpBody"))
		return wk_NewGoogleApiHttpBodySchema();
	if (str_eqc(t, ".google.protobuf.Timestamp"))
		return wk_NewGoogleProtobufTimestampSchema();
	if (str_eqc(t, ".google.protobuf.Duration"))
		return wk_NewGoogleProtobufDurationSchema();
	if (str_eqc(t, ".google.type.Date"))
		return wk_NewGoogleTypeDateSchema();
	if (str_eqc(t, ".google.type.DateTime"))
		return wk_NewGoogleTypeDateTimeSchema();
	if (str_eqc(t, ".google.protobuf.FieldMask"))
		return wk_NewGoogleProtobufFieldMaskSchema();
	if (str_eqc(t, ".google.protobuf.Struct"))
		return wk_NewGoogleProtobufStructSchema();
	if (str_eqc(t, ".google.protobuf.Empty"))
		/* Empty is closer to JSON undefined than null, so ignore this field */
		return NULL;
	if (str_eqc(t, ".google.protobuf.BoolValue"))
		return wk_NewBooleanSchema();
	if (str_eqc(t, ".google.protobuf.BytesValue"))
		return wk_NewBytesSchema();
	if (str_eqc(t, ".google.protobuf.Int32Value") || str_eqc(t, ".google.protobuf.UInt32Value"))
		return wk_NewIntegerSchema(get_value_kind(message));
	if (str_eqc(t, ".google.protobuf.StringValue") || str_eqc(t, ".google.protobuf.Int64Value") ||
	    str_eqc(t, ".google.protobuf.UInt64Value"))
		return wk_NewStringSchema();
	if (str_eqc(t, ".google.protobuf.FloatValue") || str_eqc(t, ".google.protobuf.DoubleValue"))
		return wk_NewNumberSchema(get_value_kind(message));
	Str ref = schema_reference_for_message(g, message);
	PbMsg *r = oa_new(OA_Reference);
	oa_set_str(r, OA_Reference__ref, ref);
	PbMsg *s = oa_new(OA_SchemaOrReference);
	oa_set_msg(s, OA_SchemaOrReference_reference, r);
	return s;
}

/* field.MapValue(): the field number 2 of the map entry */
static const GoField *map_value(const GoField *field)
{
	const GoMessage *e = field->message_type;
	for (int i = 0; i < e->n_fields; i++)
		if (e->fields[i]->number == 2)
			return e->fields[i];
	return NULL;
}

static PbMsg *schema_or_reference_for_field(Gen *g, const GoField *field)
{
	if (!field)
		nil_panic("schemaOrReferenceForField", NULL);
	PbMsg *kind_schema = NULL;
	int kind = field->kind;
	switch (kind) {
	case PT_MESSAGE:
		if (go_field_is_map(field)) {
			/* This means the field is a map, for example: map<string, value_type>
			 * map_field = 1; The map ends up getting converted into a MapFieldEntry
			 * message with key and value; so we need to find the `value` field in the
			 * `MapFieldEntry` message and then return a MapFieldEntry schema using the
			 * schema for the `value` field */
			return wk_NewGoogleProtobufMapFieldEntrySchema(schema_or_reference_for_field(g, map_value(field)));
		}
		kind_schema = schema_or_reference_for_message(g, field->message_type);
		break;
	case PT_STRING:
		kind_schema = wk_NewStringSchema();
		break;
	case PT_INT32: case PT_SINT32: case PT_UINT32: case PT_SFIXED32: case PT_FIXED32:
		kind_schema = wk_NewIntegerSchema(go_kind_string(kind));
		break;
	case PT_INT64: case PT_SINT64: case PT_UINT64: case PT_SFIXED64: case PT_FIXED64:
		kind_schema = wk_NewStringSchema();
		break;
	case PT_ENUM:
		kind_schema = wk_NewEnumSchema(g->conf->EnumType, field);
		break;
	case PT_BOOL:
		kind_schema = wk_NewBooleanSchema();
		break;
	case PT_FLOAT: case PT_DOUBLE:
		kind_schema = wk_NewNumberSchema(go_kind_string(kind));
		break;
	case PT_BYTES:
		kind_schema = wk_NewBytesSchema();
		break;
	default:
		/* log.Printf("(TODO) Unsupported field type: %+v", ...) to stderr */
		break;
	}
	if (go_field_is_list(field))
		kind_schema = wk_NewListSchema(kind_schema);
	return kind_schema;
}
