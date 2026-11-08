// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_bytes_field.{h,cc} (protobuf-c v1.5.2): BytesFieldGenerator. */

/* SetBytesVariables and the constructor */
static void bytes_field_init(FieldGenerator *g)
{
	const LpField *d = g->descriptor;
	lp3_vars_set(&g->variables, "name", field_name(d));
	lp3_vars_set(&g->variables, "default", xcat("\"", c_escape(d->default_value_string), "\""));
	lp3_vars_set(&g->variables, "deprecated", field_deprecated(d));
	lp3_vars_set(&g->variables, "default_value", d->has_default_value ? bytes_field_get_default_value(g) : "{0,NULL}");
}

static void bytes_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	switch (d->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &g->variables, "ProtobufCBinaryData $name$$deprecated$;\n");
		break;
	case LABEL_OPTIONAL:
		if (!d->containing_oneof && field_syntax(d) == 2)
			lp3_print_vars(printer, &g->variables, "protobuf_c_boolean has_$name$$deprecated$;\n");
		lp3_print_vars(printer, &g->variables, "ProtobufCBinaryData $name$$deprecated$;\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &g->variables, "size_t n_$name$$deprecated$;\n");
		lp3_print_vars(printer, &g->variables, "ProtobufCBinaryData *$name$$deprecated$;\n");
		break;
	}
}

static void bytes_field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer)
{
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "default_value_data",
		     xcat(full_name_to_lower(g->descriptor->full_name, g->descriptor->file), "__default_value_data"));
	lp3_print_vars(printer, &vars, "extern uint8_t $default_value_data$[];\n");
}

static void bytes_field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer)
{
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "default_value_data",
		     xcat(full_name_to_lower(g->descriptor->full_name, g->descriptor->file), "__default_value_data"));
	lp3_vars_set(&vars, "escaped", c_escape(g->descriptor->default_value_string));
	lp3_print_vars(printer, &vars, "uint8_t $default_value_data$[] = \"$escaped$\";\n");
}

static char *bytes_field_get_default_value(const FieldGenerator *g)
{
	const LpField *d = g->descriptor;
	return xcat("{ ", simple_utoa(d->default_value_string.n), ", ", full_name_to_lower(d->full_name, d->file),
		    "__default_value_data }");
}

static void bytes_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->descriptor->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &g->variables, "$default_value$");
		break;
	case LABEL_OPTIONAL:
		if (field_syntax(g->descriptor) == 2)
			lp3_print_vars(printer, &g->variables, "0, ");
		lp3_print_vars(printer, &g->variables, "$default_value$");
		break;
	case LABEL_REPEATED:
		/* no support for default? */
		lp3_print(printer, "0,NULL", NULL);
		break;
	}
}
