/* Copyright 2021 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* Port of cmd/protoc-gen-openapi/generator/wellknown/schemas.go (gnostic v0.7.1). Every
 * function returns new messages (the generator modifies them). */

/* &v3.SchemaOrReference{Oneof: &v3.SchemaOrReference_Schema{Schema: schema}} */
static PbMsg *wk_sor_schema(PbMsg *schema)
{
	PbMsg *s = oa_new(OA_SchemaOrReference);
	oa_set_msg(s, OA_SchemaOrReference_schema, schema);
	return s;
}

/* &v3.Schema{Type: type, Format: format} */
static PbMsg *wk_schema(const char *type, const char *format)
{
	PbMsg *s = oa_new(OA_Schema);
	if (type)
		oa_set_cstr(s, OA_Schema_type, type);
	if (format)
		oa_set_cstr(s, OA_Schema_format, format);
	return s;
}

static PbMsg *wk_NewStringSchema(void) { return wk_sor_schema(wk_schema("string", NULL)); }
static PbMsg *wk_NewBooleanSchema(void) { return wk_sor_schema(wk_schema("boolean", NULL)); }
static PbMsg *wk_NewBytesSchema(void) { return wk_sor_schema(wk_schema("string", "bytes")); }
static PbMsg *wk_NewIntegerSchema(const char *format) { return wk_sor_schema(wk_schema("integer", format)); }
static PbMsg *wk_NewNumberSchema(const char *format) { return wk_sor_schema(wk_schema("number", format)); }

static PbMsg *wk_NewEnumSchema(Str enum_type, const GoField *field)
{
	PbMsg *schema = wk_schema(NULL, "enum");
	if (str_eqc(enum_type, "string")) {
		oa_set_cstr(schema, OA_Schema_type, "string");
		const GoEnum *e = field->enum_type;
		for (int i = 0; i < e->n_values; i++) {
			PbMsg *any = oa_new(OA_Any);
			oa_set_cstr(any, OA_Any_yaml, e->values[i]->d.name);
			oa_add_msg(schema, OA_Schema_enum, any);
		}
	} else {
		oa_set_cstr(schema, OA_Schema_type, "integer");
	}
	return wk_sor_schema(schema);
}

static PbMsg *wk_NewListSchema(PbMsg *item_schema)
{
	PbMsg *items = oa_new(OA_ItemsItem);
	oa_add_msg(items, OA_ItemsItem_schema_or_reference, item_schema); /* may be nil */
	PbMsg *s = wk_schema("array", NULL);
	oa_set_msg(s, OA_Schema_items, items);
	return wk_sor_schema(s);
}

static PbMsg *wk_NewGoogleApiHttpBodySchema(void) { return wk_sor_schema(wk_schema("string", NULL)); }
static PbMsg *wk_NewGoogleProtobufTimestampSchema(void) { return wk_sor_schema(wk_schema("string", "date-time")); }

static PbMsg *wk_NewGoogleProtobufDurationSchema(void)
{
	/* From: https://github.com/protocolbuffers/protobuf/blob/ece5ef6b9b6fa66ef4638335612284379ee4548f/src/google/protobuf/duration.proto
	 * In JSON format, the Duration type is encoded as a string rather than an object, where
	 * the string ends in the suffix "s" (indicating seconds) and is preceded by the number of
	 * seconds, with nanoseconds expressed as fractional seconds. (...) So the well known type
	 * google.protobuf.Duration needs a string. */
	PbMsg *s = wk_schema("string", NULL);
	oa_set_cstr(s, OA_Schema_pattern, "^-?(?:0|[1-9][0-9]{0,11})(?:\\.[0-9]{1,9})?s$");
	oa_set_cstr(s, OA_Schema_description,
		    "Represents a a duration between -315,576,000,000s and 315,576,000,000s (around 10000 years). Precision is "
		    "in nanoseconds. 1 nanosecond is represented as 0.000000001s");
	return wk_sor_schema(s);
}

static PbMsg *wk_NewGoogleTypeDateSchema(void) { return wk_sor_schema(wk_schema("string", "date")); }
static PbMsg *wk_NewGoogleTypeDateTimeSchema(void) { return wk_sor_schema(wk_schema("string", "date-time")); }
static PbMsg *wk_NewGoogleProtobufFieldMaskSchema(void) { return wk_sor_schema(wk_schema("string", "field-mask")); }
static PbMsg *wk_NewGoogleProtobufStructSchema(void) { return wk_sor_schema(wk_schema("object", NULL)); }

/* &v3.NamedSchemaOrReference{Name: name, Value: value} */
static PbMsg *wk_named_schema(Str name, PbMsg *value)
{
	PbMsg *n = oa_new(OA_NamedSchemaOrReference);
	oa_set_str(n, OA_NamedSchemaOrReference_name, name);
	oa_set_msg(n, OA_NamedSchemaOrReference_value, value);
	return n;
}

static PbMsg *wk_NewGoogleProtobufValueSchema(Str name)
{
	PbMsg *s = wk_schema(NULL, NULL);
	oa_set_cstr(s, OA_Schema_description,
		    "Represents a dynamically typed value which can be either null, a number, a string, a boolean, a recursive "
		    "struct value, or a list of values.");
	return wk_named_schema(name, wk_sor_schema(s));
}

static PbMsg *wk_property(Str name, PbMsg *schema)
{
	return wk_named_schema(name, wk_sor_schema(schema));
}

static PbMsg *wk_NewGoogleProtobufAnySchema(Str name)
{
	PbMsg *t = wk_schema("string", NULL);
	oa_set_cstr(t, OA_Schema_description, "The type of the serialized message.");
	PbMsg *props = oa_new(OA_Properties);
	oa_add_msg(props, OA_Properties_additional_properties, wk_property(STR_LIT("@type"), t));
	PbMsg *ap = oa_new(OA_AdditionalPropertiesItem);
	oa_set_bool(ap, OA_AdditionalPropertiesItem_boolean, true);
	PbMsg *s = wk_schema("object", NULL);
	oa_set_cstr(s, OA_Schema_description,
		    "Contains an arbitrary serialized message along with a @type that describes the type of the serialized "
		    "message.");
	oa_set_msg(s, OA_Schema_properties, props);
	oa_set_msg(s, OA_Schema_additional_properties, ap);
	return wk_named_schema(name, wk_sor_schema(s));
}

static PbMsg *wk_NewGoogleRpcStatusSchema(Str name, Str any_name)
{
	PbMsg *code = wk_schema("integer", "int32");
	oa_set_cstr(code, OA_Schema_description, "The status code, which should be an enum value of [google.rpc.Code][google.rpc.Code].");
	PbMsg *message = wk_schema("string", NULL);
	oa_set_cstr(message, OA_Schema_description,
		    "A developer-facing error message, which should be in English. Any user-facing error message should be "
		    "localized and sent in the [google.rpc.Status.details][google.rpc.Status.details] field, or localized by "
		    "the client.");
	PbMsg *ref = oa_new(OA_Reference);
	Buf b = {0};
	buf_adds(&b, "#/components/schemas/");
	buf_add_str(&b, any_name);
	oa_set_str(ref, OA_Reference__ref, buf_view(&b));
	PbMsg *ref_sor = oa_new(OA_SchemaOrReference);
	oa_set_msg(ref_sor, OA_SchemaOrReference_reference, ref);
	PbMsg *items = oa_new(OA_ItemsItem);
	oa_add_msg(items, OA_ItemsItem_schema_or_reference, ref_sor);
	PbMsg *details = wk_schema("array", NULL);
	oa_set_msg(details, OA_Schema_items, items);
	oa_set_cstr(details, OA_Schema_description,
		    "A list of messages that carry the error details.  There is a common set of message types for APIs to use.");
	PbMsg *props = oa_new(OA_Properties);
	oa_add_msg(props, OA_Properties_additional_properties, wk_property(STR_LIT("code"), code));
	oa_add_msg(props, OA_Properties_additional_properties, wk_property(STR_LIT("message"), message));
	oa_add_msg(props, OA_Properties_additional_properties, wk_property(STR_LIT("details"), details));
	PbMsg *s = wk_schema("object", NULL);
	oa_set_cstr(s, OA_Schema_description,
		    "The `Status` type defines a logical error model that is suitable for different programming environments, "
		    "including REST APIs and RPC APIs. It is used by [gRPC](https://github.com/grpc). Each `Status` message "
		    "contains three pieces of data: error code, error message, and error details. You can find out more about "
		    "this error model and how to work with it in the [API Design Guide](https://cloud.google.com/apis/design/errors).");
	oa_set_msg(s, OA_Schema_properties, props);
	return wk_named_schema(name, wk_sor_schema(s));
}

static PbMsg *wk_NewGoogleProtobufMapFieldEntrySchema(PbMsg *value_field_schema)
{
	PbMsg *ap = oa_new(OA_AdditionalPropertiesItem);
	/* Oneof: &v3.AdditionalPropertiesItem_SchemaOrReference{SchemaOrReference: value} (a nil
	 * value reads like an unset oneof) */
	oa_set_msg(ap, OA_AdditionalPropertiesItem_schema_or_reference, value_field_schema);
	PbMsg *s = wk_schema("object", NULL);
	oa_set_msg(s, OA_Schema_additional_properties, ap);
	return wk_sor_schema(s);
}
