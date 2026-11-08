// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_repeated_message_field.{h,cc} (protobuf
 * v36.2): RepeatedMessageFieldGenerator. */

/* The codec of a single WrapperFieldGenerator or MessageFieldGenerator of the field. */
static void cs_repeated_message_single_codec(CsFieldGen *g, LpPrinter *printer, int presence_index)
{
	CsFieldKind kind = cs_is_wrapper_type(g->descriptor) ? CS_WRAPPER : CS_MESSAGE;
	cs_generate_codec_code(cs_field_generator_new(kind, g->descriptor, presence_index, g->options), printer);
}

static void cs_repeated_message_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer,
		    "private static readonly pb::FieldCodec<$type_name$> _repeated_$name$_codec\n"
		    "    = ");
	cs_repeated_message_single_codec(g, printer, g->presence_index);
	lp_print(printer, ";\n", NULL);
	cs_fg_print(g, printer,
		    "private readonly pbc::RepeatedField<$type_name$> $name$_ = new pbc::RepeatedField<$type_name$>();\n");
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ pbc::RepeatedField<$type_name$> $property_name$ {\n"
		    "  get { return $name$_; }\n"
		    "}\n");
}

static void cs_repeated_message_generate_extension_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_deprecated_flag(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ static readonly pb::RepeatedExtension<$extended_type$, $type_name$> $property_name$ =\n"
		    "  new pb::RepeatedExtension<$extended_type$, $type_name$>($number$, ");
	cs_repeated_message_single_codec(g, printer, -1);
	lp_print(printer, ");\n", NULL);
}
