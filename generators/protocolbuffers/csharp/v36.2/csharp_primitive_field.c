// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_primitive_field.{h,cc} (protobuf v36.2):
 * PrimitiveFieldGenerator and PrimitiveOneofFieldGenerator. The methods that are one Print are in
 * the dispatch of csharp_field_base.c. */

/* PrimitiveFieldGenerator::PrimitiveFieldGenerator */
static void cs_primitive_field_init(CsFieldGen *g)
{
	g->is_value_type = g->descriptor->type != PT_STRING && g->descriptor->type != PT_BYTES;
	if (!g->is_value_type && !cs_supports_presence_api(g->descriptor)) {
		const char *property_name = cs_fg_var(g, "property_name");
		cs_fg_set(g, "has_property_check", xcat(property_name, ".Length != 0"));
		cs_fg_set(g, "other_has_property_check", xcat("other.", property_name, ".Length != 0"));
	}
}

static void cs_primitive_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	const LpField *d = g->descriptor;
	/* Explicit presence allows different default values to be specified. These are retained
	 * via static fields. Under implicit presence we just use the literals instead. */
	if (proto_field_has_presence(d)) {
		cs_fg_print(g, printer,
			    "private readonly static $type_name$ $property_name$DefaultValue = $default_value$;\n\n");
		cs_fg_set(g, "default_value_access", xcat(cs_fg_var(g, "property_name"), "DefaultValue"));
	} else {
		cs_fg_set(g, "default_value_access", cs_fg_var(g, "default_value"));
	}
	/* For string and bytes proto fields, the C# field is nullable, but the property isn't. (The
	 * null value is used for presence.) */
	cs_fg_set(g, "nrt_field_annotation", g->nrt_enabled && cs_is_nullable(d) ? "?" : "");

	/* Declare the field itself. */
	cs_fg_print(g, printer, "private $type_name$$nrt_field_annotation$ $name_def_message$;\n");

	cs_write_doc_comment(printer, g->options, d);
	cs_add_public_member_attributes(g, printer);

	/* Declare the property itself (the same for all options) */
	cs_fg_print(g, printer, "$access_level$ $type_name$ $property_name$ {\n");

	/* Specify the "getter", which may need to check for a presence field. */
	if (cs_supports_presence_api(d)) {
		if (cs_is_nullable(d))
			cs_fg_print(g, printer, "  get { return $name$_ ?? $default_value_access$; }\n");
		else
			cs_fg_print(g, printer,
				    "  get { if ($has_field_check$) { return $name$_; } else { return $default_value_access$; } }\n");
	} else {
		cs_fg_print(g, printer, "  get { return $name$_; }\n");
	}

	/* Specify the "setter", which may need to set a field bit as well as the value. */
	lp_print(printer, "  set {\n", NULL);
	if (g->presence_index != -1)
		cs_fg_print(g, printer, "    $set_has_field$;\n");
	if (g->is_value_type)
		cs_fg_print(g, printer, "    $name$_ = value;\n");
	else
		cs_fg_print(g, printer, "    $name$_ = pb::ProtoPreconditions.CheckNotNull(value, \"value\");\n");
	lp_print(printer,
		 "  }\n"
		 "}\n",
		 NULL);

	/* The "HasFoo" property, where required. */
	if (cs_supports_presence_api(d)) {
		cs_fg_print(g, printer, "/// <summary>Gets whether the \"$descriptor_name$\" field is set</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ bool Has$property_name$ {\n"
			    "  get { return ");
		if (cs_is_nullable(d))
			cs_fg_print(g, printer, "$name$_ != null; }\n}\n");
		else
			cs_fg_print(g, printer, "$has_field_check$; }\n}\n");
	}

	/* The "ClearFoo" method, where required. */
	if (cs_supports_presence_api(d)) {
		cs_fg_print(g, printer, "/// <summary>Clears the value of the \"$descriptor_name$\" field</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer, "$access_level$ void Clear$property_name$() {\n");
		if (cs_is_nullable(d))
			cs_fg_print(g, printer, "  $name$_ = null;\n");
		else
			cs_fg_print(g, printer, "  $clear_has_field$;\n");
		lp_print(printer, "}\n", NULL);
	}
}

static void cs_primitive_generate_serialized_size_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer, "if ($has_property_check$) {\n");
	lp_indent(printer);
	int fixed_size = cs_get_fixed_size(g->descriptor->type);
	if (fixed_size == -1)
		cs_fg_print(g, printer,
			    "size += $tag_size$ + pb::CodedOutputStream.Compute$capitalized_type_name$Size($property_name$);\n");
	else
		lp_print(printer, "size += $tag_size$ + $fixed_size$;\n", "fixed_size", cs_itoa(fixed_size), "tag_size",
			 cs_fg_var(g, "tag_size"), NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void cs_primitive_generate_codec_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer, "pb::FieldCodec.For$capitalized_type_name$($tag$, $default_value$)");
}

static void cs_primitive_oneof_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ $type_name$ $property_name$ {\n"
		    "  get { return $has_property_check$ ? ($type_name$) $oneof_name$_ : $default_value$; }\n"
		    "  set {\n");
	if (g->is_value_type)
		cs_fg_print(g, printer, "    $oneof_name$_ = value;\n");
	else
		cs_fg_print(g, printer, "    $oneof_name$_ = pb::ProtoPreconditions.CheckNotNull(value, \"value\");\n");
	cs_fg_print(g, printer,
		    "    $oneof_name$Case_ = $oneof_property_name$OneofCase.$oneof_case_name$;\n"
		    "  }\n"
		    "}\n");
	if (cs_supports_presence_api(g->descriptor)) {
		cs_fg_print(g, printer, "/// <summary>Gets whether the \"$descriptor_name$\" field is set</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ bool Has$property_name$ {\n"
			    "  get { return $oneof_name$Case_ == $oneof_property_name$OneofCase.$oneof_case_name$; }\n"
			    "}\n");
		cs_fg_print(g, printer,
			    "/// <summary> Clears the value of the oneof if it's currently set to \"$descriptor_name$\" "
			    "</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ void Clear$property_name$() {\n"
			    "  if ($has_property_check$) {\n"
			    "    Clear$oneof_property_name$();\n"
			    "  }\n"
			    "}\n");
	}
}
