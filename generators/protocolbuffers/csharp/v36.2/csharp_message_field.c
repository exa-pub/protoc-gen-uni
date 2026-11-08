// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_message_field.{h,cc} (protobuf v36.2):
 * MessageFieldGenerator and MessageOneofFieldGenerator. */

/* MessageFieldGenerator::MessageFieldGenerator */
static void cs_message_field_init(CsFieldGen *g)
{
	if (!cs_supports_presence_api(g->descriptor)) {
		char *name = cs_fg_name(g);
		cs_fg_set(g, "has_property_check", xcat(name, "_ != null"));
		cs_fg_set(g, "has_not_property_check", xcat(name, "_ == null"));
	}
}

static void cs_message_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer, "private $type_name$$nrt_annotation$ $name$_;\n");
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ $type_name$$nrt_annotation$ $property_name$ {\n"
		    "  get { return $name$_; }\n"
		    "  set {\n"
		    "    $name$_ = value;\n"
		    "  }\n"
		    "}\n");
	if (cs_supports_presence_api(g->descriptor)) {
		cs_fg_print(g, printer, "/// <summary>Gets whether the $descriptor_name$ field is set</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ bool Has$property_name$ {\n"
			    "  get { return $name$_ != null; }\n"
			    "}\n");
		cs_fg_print(g, printer, "/// <summary>Clears the value of the $descriptor_name$ field</summary>\n");
		cs_add_public_member_attributes(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ void Clear$property_name$() {\n"
			    "  $name$_ = null;\n"
			    "}\n");
	}
}

static void cs_message_generate_parsing_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer,
		    "if ($has_not_property_check$) {\n"
		    "  $property_name$ = new $type_name$();\n"
		    "}\n");
	if (g->descriptor->type == PT_MESSAGE)
		cs_fg_print(g, printer, "input.ReadMessage($property_name$);\n");
	else
		cs_fg_print(g, printer, "input.ReadGroup($property_name$);\n");
}

static void cs_message_generate_serialization_code(CsFieldGen *g, LpPrinter *printer)
{
	if (g->descriptor->type == PT_MESSAGE)
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  output.WriteRawTag($tag_bytes$);\n"
			    "  output.WriteMessage($property_name$);\n"
			    "}\n");
	else
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  output.WriteRawTag($tag_bytes$);\n"
			    "  output.WriteGroup($property_name$);\n"
			    "  output.WriteRawTag($end_tag_bytes$);\n"
			    "}\n");
}

static void cs_message_generate_codec_code(CsFieldGen *g, LpPrinter *printer)
{
	if (g->descriptor->type == PT_MESSAGE)
		cs_fg_print(g, printer, "pb::FieldCodec.ForMessage($tag$, $type_name$.Parser)");
	else
		cs_fg_print(g, printer, "pb::FieldCodec.ForGroup($tag$, $end_tag$, $type_name$.Parser)");
}

static void cs_message_oneof_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ $type_name$$nrt_annotation$ $property_name$ {\n"
		    "  get { return $has_property_check$ ? ($type_name$) $oneof_name$_ : null; }\n"
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

static void cs_message_oneof_generate_parsing_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer,
		    "$type_name$ subBuilder = new $type_name$();\n"
		    "if ($has_property_check$) {\n"
		    "  subBuilder.MergeFrom($property_name$);\n"
		    "}\n");
	if (g->descriptor->type == PT_MESSAGE)
		lp_print(printer, "input.ReadMessage(subBuilder);\n", NULL);
	else
		lp_print(printer, "input.ReadGroup(subBuilder);\n", NULL);
	cs_fg_print(g, printer, "$property_name$ = subBuilder;\n");
}
