// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_primitive_field.{h,cc} (protobuf-c v1.5.2): PrimitiveFieldGenerator. */

static void primitive_field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	const char *c_type = "";
	switch (d->type) {
	case PT_SINT32: case PT_SFIXED32: case PT_INT32: c_type = "int32_t"; break;
	case PT_SINT64: case PT_SFIXED64: case PT_INT64: c_type = "int64_t"; break;
	case PT_UINT32: case PT_FIXED32: c_type = "uint32_t"; break;
	case PT_UINT64: case PT_FIXED64: c_type = "uint64_t"; break;
	case PT_FLOAT: c_type = "float"; break;
	case PT_DOUBLE: c_type = "double"; break;
	case PT_BOOL: c_type = "protobuf_c_boolean"; break;
	default: fail("not a primitive type");
	}
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "c_type", c_type);
	lp3_vars_set(&vars, "name", field_name(d));
	lp3_vars_set(&vars, "deprecated", field_deprecated(d));
	switch (d->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &vars, "$c_type$ $name$$deprecated$;\n");
		break;
	case LABEL_OPTIONAL:
		if (!d->containing_oneof && field_syntax(d) == 2)
			lp3_print_vars(printer, &vars, "protobuf_c_boolean has_$name$$deprecated$;\n");
		lp3_print_vars(printer, &vars, "$c_type$ $name$$deprecated$;\n");
		break;
	case LABEL_REPEATED:
		lp3_print_vars(printer, &vars, "size_t n_$name$$deprecated$;\n");
		lp3_print_vars(printer, &vars, "$c_type$ *$name$$deprecated$;\n");
		break;
	}
}

static char *primitive_field_get_default_value(const FieldGenerator *g)
{
	/* XXX: SimpleItoa seems woefully inadequate for anything but int32, but that's what
	 * protobuf uses. */
	const LpField *d = g->descriptor;
	switch (lp_field_cpp_type(d)) {
	case LP_CPPTYPE_INT32: return simple_itoa((int32_t)d->default_value.i);
	case LP_CPPTYPE_INT64: return xcat(simple_itoa(d->default_value.i), "ll");
	case LP_CPPTYPE_UINT32: return xcat(simple_utoa((uint32_t)d->default_value.u), "u");
	case LP_CPPTYPE_UINT64: return xcat(simple_utoa(d->default_value.u), "ull");
	case LP_CPPTYPE_FLOAT: return simple_ftoa(d->default_value.f);
	case LP_CPPTYPE_DOUBLE: return simple_dtoa(d->default_value.d);
	case LP_CPPTYPE_BOOL: return d->default_value.b ? "1" : "0";
	default: fail("unexpected CPPTYPE in c_primitive_field");
	}
}

static void primitive_field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer)
{
	const LpField *d = g->descriptor;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "default_value", d->has_default_value ? primitive_field_get_default_value(g) : "0");
	switch (d->label) {
	case LABEL_REQUIRED:
		lp3_print_vars(printer, &vars, "$default_value$");
		break;
	case LABEL_OPTIONAL:
		if (field_syntax(d) == 2)
			lp3_print_vars(printer, &vars, "0, ");
		lp3_print_vars(printer, &vars, "$default_value$");
		break;
	case LABEL_REPEATED:
		lp3_print(printer, "0,NULL", NULL);
		break;
	}
}

static void primitive_field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer)
{
	const char *c_type_macro = "";
	switch (g->descriptor->type) {
	case PT_INT32: c_type_macro = "INT32"; break;
	case PT_SINT32: c_type_macro = "SINT32"; break;
	case PT_UINT32: c_type_macro = "UINT32"; break;
	case PT_SFIXED32: c_type_macro = "SFIXED32"; break;
	case PT_FIXED32: c_type_macro = "FIXED32"; break;
	case PT_INT64: c_type_macro = "INT64"; break;
	case PT_SINT64: c_type_macro = "SINT64"; break;
	case PT_UINT64: c_type_macro = "UINT64"; break;
	case PT_FIXED64: c_type_macro = "FIXED64"; break;
	case PT_SFIXED64: c_type_macro = "SFIXED64"; break;
	case PT_FLOAT: c_type_macro = "FLOAT"; break;
	case PT_DOUBLE: c_type_macro = "DOUBLE"; break;
	case PT_BOOL: c_type_macro = "BOOL"; break;
	default: fail("not a primitive type");
	}
	field_generate_descriptor_initializer_generic(g, printer, true, c_type_macro, "NULL");
}
