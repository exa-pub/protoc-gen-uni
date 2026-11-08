// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_wrapper_field.{h,cc} (protobuf v36.2):
 * WrapperFieldGenerator and WrapperOneofFieldGenerator. */

/* WrapperFieldGenerator::WrapperFieldGenerator */
static void cs_wrapper_field_init(CsFieldGen *g)
{
	char *name = cs_fg_name(g);
	cs_fg_set(g, "has_property_check", xcat(name, "_ != null"));
	cs_fg_set(g, "has_not_property_check", xcat(name, "_ == null"));
	const LpField *wrapped_field = cs_wrapped_field(g->descriptor);
	g->is_value_type = wrapped_field->type != PT_STRING && wrapped_field->type != PT_BYTES;
	if (g->is_value_type)
		cs_fg_set(g, "nonnullable_type_name", cs_fg_type_name(g, wrapped_field));
}

static void cs_wrapper_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer, "private static readonly pb::FieldCodec<$type_name$> _single_$name$_codec = ");
	cs_generate_codec_code(g, printer);
	cs_fg_print(g, printer,
		    ";\n"
		    "private $type_name$ $name$_;\n");
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ $type_name$ $property_name$ {\n"
		    "  get { return $name$_; }\n"
		    "  set {\n"
		    "    $name$_ = value;\n"
		    "  }\n"
		    "}\n\n");
	if (cs_supports_presence_api(g->descriptor)) {
		cs_fg_print(g, printer, "/// <summary>Gets whether the $descriptor_name$ field is set</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ bool Has$property_name$ {\n"
			    "  get { return $name$_ != null; }\n"
			    "}\n\n");
		cs_fg_print(g, printer, "/// <summary>Clears the value of the $descriptor_name$ field</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ void Clear$property_name$() {\n"
			    "  $name$_ = null;\n"
			    "}\n");
	}
}

static void cs_wrapper_generate_codec_code(CsFieldGen *g, LpPrinter *printer)
{
	if (g->is_value_type)
		cs_fg_print(g, printer, "pb::FieldCodec.ForStructWrapper<$nonnullable_type_name$>($tag$)");
	else
		cs_fg_print(g, printer, "pb::FieldCodec.ForClassWrapper<$type_name$>($tag$)");
}

static void cs_wrapper_oneof_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	/* Note: deliberately _oneof_$name$_codec, not _$oneof_name$_codec... we have one codec per
	 * field. */
	cs_fg_print(g, printer, "private static readonly pb::FieldCodec<$type_name$> _oneof_$name$_codec = ");
	cs_generate_codec_code(g, printer);
	lp_print(printer, ";\n", NULL);
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ $type_name$ $property_name$ {\n"
		    "  get { return $has_property_check$ ? ($type_name$) $oneof_name$_ : ($type_name$) null; }\n"
		    "  set {\n"
		    "    $oneof_name$_ = value;\n"
		    "    $oneof_name$Case_ = value == null ? $oneof_property_name$OneofCase.None : "
		    "$oneof_property_name$OneofCase.$oneof_case_name$;\n"
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
