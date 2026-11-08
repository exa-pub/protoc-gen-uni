// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_message_field.{h,cc} (protobuf-c v1.5.2): MessageFieldGenerator. */

static void message_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "name", field_name(d));
	lp3_vars_set(&vars, "type", full_name_to_c(d->message_type->full_name, d->message_type->file));
	lp3_vars_set(&vars, "deprecated", field_deprecated(d));
	switch (d->label) {
	case LABEL_REQUIRED:
	case LABEL_OPTIONAL:
		lp3_print_vars(printer, &vars, "$type$ *$name$$deprecated$;\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &vars, "size_t n_$name$$deprecated$;\n");
		lp3_print_vars(printer, &vars, "$type$ **$name$$deprecated$;\n");
		break;
	}
}

static void message_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	switch (g->descriptor->label) {
	case LABEL_REQUIRED:
	case LABEL_OPTIONAL:
		lp3_print(printer, "NULL", NULL);
		break;
	case LABEL_REPEATED:
		lp3_print(printer, "0,NULL", NULL);
		break;
	}
}

static void message_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpMessage *m = g->descriptor->message_type;
	char *addr = xcat("&", full_name_to_lower(m->full_name, m->file), "__descriptor");
	field_generate_descriptor_initializer_generic(g, printer, false, "MESSAGE", addr);
}
