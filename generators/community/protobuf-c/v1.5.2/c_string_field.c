// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_string_field.{h,cc} (protobuf-c v1.5.2): StringFieldGenerator. */

/* SetStringVariables */
static void string_field_init(FieldGenerator *g)
{
	const LpField *d = g->descriptor;
	lp3_vars_set(&g->variables, "name", field_name(d));
	lp3_vars_set(&g->variables, "default", xcat(full_name_to_lower(d->full_name, d->file), "__default_value"));
	lp3_vars_set(&g->variables, "deprecated", field_deprecated(d));
}

static void string_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	const PbcFileOptions *opt = pbc_file_options(g->descriptor->file);
	switch (g->descriptor->label) {
	case LABEL_REQUIRED:
	case LABEL_OPTIONAL:
		if (opt->const_strings)
			lp3_print_vars(printer, &g->variables, "const ");
		lp3_print_vars(printer, &g->variables, "char *$name$$deprecated$;\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &g->variables, "size_t n_$name$$deprecated$;\n");
		if (opt->const_strings)
			lp3_print_vars(printer, &g->variables, "const ");
		lp3_print_vars(printer, &g->variables, "char **$name$$deprecated$;\n");
		break;
	}
}

static void string_field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer)
{
	lp3_print_vars(printer, &g->variables, "extern char $default$[];\n");
}

static void string_field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer)
{
	LpVars3 vars = {0};
	lp3_vars_set_s(&vars, "default", *lp3_vars_get(&g->variables, "default"));
	lp3_vars_set(&vars, "escaped", c_escape(g->descriptor->default_value_string));
	lp3_print_vars(printer, &vars, "char $default$[] = \"$escaped$\";\n");
}

static void string_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	const PbcFileOptions *opt = pbc_file_options(d->file);
	LpVars3 vars = {0};
	if (d->has_default_value)
		lp3_vars_set_s(&vars, "default", *lp3_vars_get(&g->variables, "default"));
	else if (field_syntax(d) == 2)
		lp3_vars_set(&vars, "default", "NULL");
	else if (opt->const_strings)
		lp3_vars_set(&vars, "default", "(const char *)protobuf_c_empty_string");
	else
		lp3_vars_set(&vars, "default", "(char *)protobuf_c_empty_string");
	switch (d->label) {
	case LABEL_REQUIRED:
	case LABEL_OPTIONAL:
		lp3_print_vars(printer, &vars, "$default$");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &vars, "0,NULL");
		break;
	}
}
