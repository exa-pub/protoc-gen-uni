// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_field.{h,cc} (protobuf-c v1.5.2): FieldGenerator, its generic
 * descriptor initializer and FieldGeneratorMap. The virtual methods dispatch on the kind of
 * generator (c_primitive_field.c, c_string_field.c, c_bytes_field.c, c_enum_field.c,
 * c_message_field.c). */

static bool is_packable_type(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FLOAT: case PT_INT64: case PT_UINT64: case PT_INT32: case PT_FIXED64:
	case PT_FIXED32: case PT_BOOL: case PT_UINT32: case PT_ENUM: case PT_SFIXED32: case PT_SFIXED64:
	case PT_SINT32: case PT_SINT64:
		return true;
	default: /* TYPE_BYTES, TYPE_STRING, TYPE_GROUP, TYPE_MESSAGE */
		return false;
	}
}

/* FieldScope: the scope where the field was defined */
static const LpMessage *field_scope(const LpField *field)
{
	return field->is_extension ? field->extension_scope : field->containing_type;
}

static void field_generate_descriptor_initializer_generic(const FieldGenerator *g, LpPrinter3 *printer,
							  bool optional_uses_has, const char *type_macro,
							  const char *descriptor_addr)
{
	const LpField *d = g->descriptor;
	LpVars3 variables = {0};
	const LpOneof *oneof = d->containing_oneof;
	const PbcFileOptions *opt = pbc_file_options(d->file);
	lp3_vars_set(&variables, "TYPE", type_macro);
	lp3_vars_set(&variables, "classname", full_name_to_c(field_scope(d)->full_name, field_scope(d)->file));
	lp3_vars_set(&variables, "name", field_name(d));
	if (opt->use_oneof_field_name) {
		if (!oneof) /* upstream dereferences the NULL oneof */
			fail("unsupported by protoc-gen-uni: use_oneof_field_name with the field %s, which is not in a "
			     "oneof (protoc-gen-c v1.5.2 crashes with SIGSEGV)",
			     d->full_name);
		lp3_vars_set(&variables, "proto_name", oneof->name);
	} else {
		lp3_vars_set(&variables, "proto_name", d->name);
	}
	lp3_vars_set(&variables, "descriptor_addr", descriptor_addr);
	lp3_vars_set(&variables, "value", simple_itoa(d->number));
	if (oneof)
		lp3_vars_set(&variables, "oneofname", camel_to_lower(oneof->name));

	if (field_syntax(d) == 3 && d->label == LABEL_OPTIONAL) {
		lp3_vars_set(&variables, "LABEL", "NONE");
		optional_uses_has = false;
	} else {
		lp3_vars_set(&variables, "LABEL", camel_to_upper(get_label_name(d->label)));
	}

	if (d->has_default_value)
		lp3_vars_set(&variables, "default_value", xcat("&", full_name_to_lower(d->full_name, d->file), "__default_value"));
	else if (field_syntax(d) == 3 && d->type == PT_STRING)
		lp3_vars_set(&variables, "default_value", "&protobuf_c_empty_string");
	else
		lp3_vars_set(&variables, "default_value", "NULL");

	Buf flags = {0};
	buf_adds(&flags, "0");
	if (d->label == LABEL_REPEATED && is_packable_type(d->type) && pb_int(d->options, FOPT_PACKED, 0))
		buf_adds(&flags, " | PROTOBUF_C_FIELD_FLAG_PACKED");
	else if (d->label == LABEL_REPEATED && is_packable_type(d->type) && field_syntax(d) == 3 &&
		 !pb_has(d->options, FOPT_PACKED))
		buf_adds(&flags, " | PROTOBUF_C_FIELD_FLAG_PACKED");
	if (pb_int(d->options, FOPT_DEPRECATED, 0))
		buf_adds(&flags, " | PROTOBUF_C_FIELD_FLAG_DEPRECATED");
	if (oneof)
		buf_adds(&flags, " | PROTOBUF_C_FIELD_FLAG_ONEOF");
	/* Eliminate codesmell "or with 0" */
	const char *f = buf_str(&flags);
	lp3_vars_set(&variables, "flags", has_prefix(f, "0 | ") ? f + 4 : f);

	lp3_print(printer, "{\n", NULL);
	if (optimize_code_size(d->file))
		lp3_print(printer, "  NULL, /* CODE_SIZE */\n", NULL);
	else
		lp3_print_vars(printer, &variables, "  \"$proto_name$\",\n");
	lp3_print_vars(printer, &variables,
		       "  $value$,\n"
		       "  PROTOBUF_C_LABEL_$LABEL$,\n"
		       "  PROTOBUF_C_TYPE_$TYPE$,\n");
	switch (d->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &variables, "  0,   /* quantifier_offset */\n");
		break;
	case LABEL_OPTIONAL:
		if (oneof)
			lp3_print_vars(printer, &variables, "  offsetof($classname$, $oneofname$_case),\n");
		else if (optional_uses_has)
			lp3_print_vars(printer, &variables, "  offsetof($classname$, has_$name$),\n");
		else
			lp3_print_vars(printer, &variables, "  0,   /* quantifier_offset */\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &variables, "  offsetof($classname$, n_$name$),\n");
		break;
	}
	lp3_print_vars(printer, &variables, "  offsetof($classname$, $name$),\n");
	lp3_print_vars(printer, &variables, "  $descriptor_addr$,\n");
	lp3_print_vars(printer, &variables, "  $default_value$,\n");
	lp3_print_vars(printer, &variables, "  $flags$,             /* flags */\n");
	lp3_print_vars(printer, &variables, "  0,NULL,NULL    /* reserved1,reserved2, etc */\n");
	lp3_print(printer, "},\n", NULL);
}

static void string_field_init(FieldGenerator *g);
static void bytes_field_init(FieldGenerator *g);
static void enum_field_init(FieldGenerator *g);

/* FieldGeneratorMap::MakeGenerator; NULL for groups */
static FieldGenerator *make_generator(const LpField *field)
{
	FieldGenerator *g = xalloc(sizeof *g);
	g->descriptor = field;
	switch (field->type) {
	case PT_MESSAGE: g->kind = FG_MESSAGE; break;
	case PT_STRING:
		if (pbc_field_options(field).string_as_bytes) {
			g->kind = FG_BYTES;
			bytes_field_init(g);
		} else {
			g->kind = FG_STRING;
			string_field_init(g);
		}
		break;
	case PT_BYTES:
		g->kind = FG_BYTES;
		bytes_field_init(g);
		break;
	case PT_ENUM:
		g->kind = FG_ENUM;
		enum_field_init(g);
		break;
	case PT_GROUP: return NULL; /* XXX */
	default: g->kind = FG_PRIMITIVE; break;
	}
	return g;
}

static void field_generator_map_init(FieldGeneratorMap *m, const LpMessage *descriptor)
{
	m->descriptor = descriptor;
	m->field_generators = xalloc(sizeof(FieldGenerator *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++)
		m->field_generators[i] = make_generator(&descriptor->fields[i]);
}

/* FieldGeneratorMap::get; the generator of a group field is NULL, which upstream dereferences. */
static const FieldGenerator *field_generator_map_get(const FieldGeneratorMap *m, const LpField *field)
{
	const FieldGenerator *g = m->field_generators[field->index];
	if (!g)
		fail("unsupported by protoc-gen-uni: the group field %s (protoc-gen-c v1.5.2 crashes with SIGSEGV "
		     "on group fields)",
		     field->full_name);
	return g;
}

static void primitive_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static char *primitive_field_get_default_value(const FieldGenerator *g);
static void primitive_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void primitive_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer);
static void string_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static void string_field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer);
static void string_field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer);
static void string_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void bytes_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static void bytes_field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer);
static void bytes_field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer);
static char *bytes_field_get_default_value(const FieldGenerator *g);
static void bytes_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void enum_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static void enum_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void enum_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer);
static void message_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static void message_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void message_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer);

/* The virtual methods of FieldGenerator */
static void field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->kind) {
	case FG_PRIMITIVE: primitive_field_generate_struct_members(g, printer); break;
	case FG_STRING: string_field_generate_struct_members(g, printer); break;
	case FG_BYTES: bytes_field_generate_struct_members(g, printer); break;
	case FG_ENUM: enum_field_generate_struct_members(g, printer); break;
	case FG_MESSAGE: message_field_generate_struct_members(g, printer); break;
	}
}

static void field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->kind) {
	case FG_PRIMITIVE: primitive_field_generate_descriptor_initializer(g, printer); break;
	case FG_STRING: field_generate_descriptor_initializer_generic(g, printer, false, "STRING", "NULL"); break;
	case FG_BYTES: field_generate_descriptor_initializer_generic(g, printer, true, "BYTES", "NULL"); break;
	case FG_ENUM: enum_field_generate_descriptor_initializer(g, printer); break;
	case FG_MESSAGE: message_field_generate_descriptor_initializer(g, printer); break;
	}
}

static void field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer)
{
	if (g->kind == FG_STRING)
		string_field_generate_default_value_declarations(g, printer);
	else if (g->kind == FG_BYTES)
		bytes_field_generate_default_value_declarations(g, printer);
}

static void field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer)
{
	if (g->kind == FG_STRING)
		string_field_generate_default_value_implementations(g, printer);
	else if (g->kind == FG_BYTES)
		bytes_field_generate_default_value_implementations(g, printer);
}

static char *field_get_default_value(const FieldGenerator *g)
{
	switch (g->kind) {
	case FG_PRIMITIVE: return primitive_field_get_default_value(g);
	case FG_BYTES: return bytes_field_get_default_value(g);
	case FG_STRING: /* fall through: "default" of variables_ */
	case FG_ENUM: return (char *)lp3_vars_get(&g->variables, "default")->p;
	case FG_MESSAGE: return "NULL";
	}
	return "";
}

static void field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->kind) {
	case FG_PRIMITIVE: primitive_field_generate_static_init(g, printer); break;
	case FG_STRING: string_field_generate_static_init(g, printer); break;
	case FG_BYTES: bytes_field_generate_static_init(g, printer); break;
	case FG_ENUM: enum_field_generate_static_init(g, printer); break;
	case FG_MESSAGE: message_field_generate_static_init(g, printer); break;
	}
}
