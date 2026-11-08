// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_enum_field.{h,cc} (protobuf-c v1.5.2): EnumFieldGenerator. */

/* SetEnumVariables */
static void enum_field_init(FieldGenerator *g)
{
	const LpField *d = g->descriptor;
	lp3_vars_set(&g->variables, "name", field_name(d));
	lp3_vars_set(&g->variables, "type", full_name_to_c(d->enum_type->full_name, d->enum_type->file));
	const LpEnumValue *default_value = d->default_value_enum;
	lp3_vars_set(&g->variables, "default",
		     xcat(full_name_to_upper(default_value->type->full_name, default_value->type->file), "__",
			  default_value->name));
	lp3_vars_set(&g->variables, "deprecated", field_deprecated(d));
}

static void enum_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	switch (d->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &g->variables, "$type$ $name$$deprecated$;\n");
		break;
	case LABEL_OPTIONAL:
		if (!d->containing_oneof && field_syntax(d) == 2)
			lp3_print_vars(printer, &g->variables, "protobuf_c_boolean has_$name$$deprecated$;\n");
		lp3_print_vars(printer, &g->variables, "$type$ $name$$deprecated$;\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &g->variables, "size_t n_$name$$deprecated$;\n");
		lp3_print_vars(printer, &g->variables, "$type$ *$name$$deprecated$;\n");
		break;
	}
}

static void enum_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->descriptor->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &g->variables, "$default$");
		break;
	case LABEL_OPTIONAL:
		if (field_syntax(g->descriptor) == 2)
			lp3_print_vars(printer, &g->variables, "0, ");
		lp3_print_vars(printer, &g->variables, "$default$");
		break;
	case LABEL_REPEATED:
		/* no support for default? */
		lp3_print(printer, "0,NULL", NULL);
		break;
	}
}

static void enum_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpEnum *e = g->descriptor->enum_type;
	char *addr = xcat("&", full_name_to_lower(e->full_name, e->file), "__descriptor");
	field_generate_descriptor_initializer_generic(g, printer, true, "ENUM", addr);
}
