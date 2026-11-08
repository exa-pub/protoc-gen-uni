// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_map_field.{h,cc} (protobuf v36.2):
 * MapFieldGenerator. */

static void cs_map_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	const LpMessage *entry = g->descriptor->message_type;
	const LpField *key_descriptor = &entry->fields[0];   /* map_key() */
	const LpField *value_descriptor = &entry->fields[1]; /* map_value() */
	cs_fg_set(g, "key_type_name", cs_fg_type_name(g, key_descriptor));
	cs_fg_set(g, "value_type_name", cs_fg_type_name(g, value_descriptor));
	CsFieldGen *key_generator = cs_create_field_generator(key_descriptor, 1, g->options);
	CsFieldGen *value_generator = cs_create_field_generator(value_descriptor, 2, g->options);

	cs_fg_print(g, printer,
		    "private static readonly pbc::MapField<$key_type_name$, $value_type_name$>.Codec _map_$name$_codec\n"
		    "    = new pbc::MapField<$key_type_name$, $value_type_name$>.Codec(");
	cs_generate_codec_code(key_generator, printer);
	lp_print(printer, ", ", NULL);
	cs_generate_codec_code(value_generator, printer);
	cs_fg_print(g, printer,
		    ", $tag$);\n"
		    "private readonly pbc::MapField<$key_type_name$, $value_type_name$> $name$_ = new "
		    "pbc::MapField<$key_type_name$, $value_type_name$>();\n");
	cs_write_doc_comment(printer, g->options, g->descriptor);
	cs_add_public_member_attributes(g, printer);
	cs_fg_print(g, printer,
		    "$access_level$ pbc::MapField<$key_type_name$, $value_type_name$> $property_name$ {\n"
		    "  get { return $name$_; }\n"
		    "}\n");
}
