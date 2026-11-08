// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_message.{h,cc} (protobuf v36.2):
 * MessageGenerator. Upstream creates a field generator per field and method, and finds the
 * presence index of a field by a scan of the fields; the port computes the presence indexes once
 * and keeps one generator per field (the methods do not depend on each other's variables). */

typedef struct {
	const LpMessage *descriptor;
	const CsOptions *options;
	const LpField **fields_by_number;
	int *presence_index;     /* GetPresenceIndex, by field index */
	CsFieldGen **generators; /* CreateFieldGeneratorInternal, by field index */
	int has_bit_field_count;
	bool has_extension_ranges;
	bool nrt_enabled;
} CsMessageGen;

static int cs_compare_field_numbers(const void *a, const void *b)
{
	int na = (*(const LpField *const *)a)->number, nb = (*(const LpField *const *)b)->number;
	return na < nb ? -1 : na > nb;
}

static void cs_message_init(CsMessageGen *m, const LpMessage *descriptor, const CsOptions *options)
{
	memset(m, 0, sizeof *m);
	m->descriptor = descriptor;
	m->options = options;
	m->has_extension_ranges = descriptor->n_extension_ranges > 0;
	size_t n = (size_t)descriptor->n_fields;
	m->fields_by_number = xalloc(sizeof *m->fields_by_number * (n + 1));
	m->presence_index = xalloc(sizeof *m->presence_index * (n + 1));
	m->generators = xalloc(sizeof *m->generators * (n + 1));
	for (size_t i = 0; i < n; i++)
		m->fields_by_number[i] = &descriptor->fields[i];
	if (n > 1)
		qsort(m->fields_by_number, n, sizeof *m->fields_by_number, cs_compare_field_numbers);
	int presence_bit_count = 0;
	for (size_t i = 0; i < n; i++) {
		const LpField *field = m->fields_by_number[i];
		int index = (int)(field - descriptor->fields);
		/* it's a waste of space to track presence for all values, so we only track them if
		 * they're not nullable */
		m->presence_index[index] = cs_requires_presence_bit(field) ? presence_bit_count++ : -1;
	}
	m->has_bit_field_count = (presence_bit_count + 31) / 32;
	m->nrt_enabled = cs_nrt_enabled(descriptor->file);
}

/* CreateFieldGeneratorInternal for a field of the message */
static CsFieldGen *cs_message_field_generator(CsMessageGen *m, const LpField *field)
{
	int index = (int)(field - m->descriptor->fields);
	if (!m->generators[index])
		m->generators[index] = cs_create_field_generator(field, m->presence_index[index], m->options);
	return m->generators[index];
}

static void cs_message_add_deprecated_flag(CsMessageGen *m, LpPrinter *printer)
{
	if (pb_int(m->descriptor->options, MSGOPT_DEPRECATED, 0))
		lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
}

static void cs_message_add_serializable_attribute(CsMessageGen *m, LpPrinter *printer)
{
	if (m->options->serializable)
		lp_print(printer, "[global::System.SerializableAttribute]\n", NULL);
}

/* HasNestedGeneratedTypes: nested enums or messages other than map entries */
static bool cs_message_has_nested_generated_types(CsMessageGen *m)
{
	if (m->descriptor->n_enums > 0)
		return true;
	for (int i = 0; i < m->descriptor->n_nested; i++)
		if (!cs_is_map_entry_message(&m->descriptor->nested[i]))
			return true;
	return false;
}

static void cs_message_set_oneof_vars(LpVars *vars, const LpOneof *oneof)
{
	lp_vars_set(vars, "name", lp_csharp_underscores_to_camel_case(oneof->name, false, false));
	lp_vars_set(vars, "property_name", lp_csharp_underscores_to_camel_case(oneof->name, true, false));
}

static void cs_message_generate_cloning_code(CsMessageGen *m, LpPrinter *printer)
{
	const LpMessage *d = m->descriptor;
	LpVars vars = {0};
	cs_write_generated_code_attributes(printer);
	lp_vars_set(&vars, "class_name", d->name);
	lp_print_vars(printer, &vars, "public $class_name$($class_name$ other) : this() {\n");
	lp_indent(printer);
	for (int i = 0; i < m->has_bit_field_count; i++)
		lp_print(printer, "_hasBits$i$ = other._hasBits$i$;\n", "i", cs_itoa(i), NULL);
	/* Clone non-oneof fields first (treating optional proto3 fields as non-oneof) */
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (proto_field_real_oneof(field))
			continue;
		cs_generate_cloning_code(cs_message_field_generator(m, field), printer);
	}
	/* Clone just the right field for each real oneof */
	for (int i = 0; i < d->n_real_oneofs; ++i) {
		const LpOneof *oneof = &d->oneofs[i];
		cs_message_set_oneof_vars(&vars, oneof);
		lp_print_vars(printer, &vars, "switch (other.$property_name$Case) {\n");
		lp_indent(printer);
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			CsFieldGen *generator = cs_message_field_generator(m, field);
			lp_vars_set(&vars, "oneof_case_name", cs_get_oneof_case_name(field));
			lp_print_vars(printer, &vars, "case $property_name$OneofCase.$oneof_case_name$:\n");
			lp_indent(printer);
			cs_generate_cloning_code(generator, printer);
			lp_print(printer, "break;\n", NULL);
			lp_outdent(printer);
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}
	/* Clone unknown fields */
	lp_print(printer, "_unknownFields = pb::UnknownFieldSet.Clone(other._unknownFields);\n", NULL);
	if (m->has_extension_ranges)
		lp_print(printer, "_extensions = pb::ExtensionSet.Clone(other._extensions);\n", NULL);

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);

	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "public $class_name$ Clone() {\n"
		      "  return new $class_name$(this);\n"
		      "}\n\n");
}

static void cs_message_generate_framework_methods(CsMessageGen *m, LpPrinter *printer)
{
	const LpMessage *d = m->descriptor;
	LpVars vars = {0};
	lp_vars_set(&vars, "class_name", d->name);
	lp_vars_set(&vars, "nrt_annotation", m->nrt_enabled ? "?" : "");

	/* Equality */
	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "public override bool Equals(object$nrt_annotation$ other) {\n"
		      "  return Equals(other as $class_name$);\n"
		      "}\n\n");
	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "public bool Equals($class_name$$nrt_annotation$ other) {\n"
		      "  if (ReferenceEquals(other, null)) {\n"
		      "    return false;\n"
		      "  }\n"
		      "  if (ReferenceEquals(other, this)) {\n"
		      "    return true;\n"
		      "  }\n");
	lp_indent(printer);
	for (int i = 0; i < d->n_fields; i++)
		cs_write_equals(cs_message_field_generator(m, &d->fields[i]), printer);
	for (int i = 0; i < d->n_real_oneofs; i++)
		lp_print(printer, "if ($property_name$Case != other.$property_name$Case) return false;\n", "property_name",
			 lp_csharp_underscores_to_camel_case(d->oneofs[i].name, true, false), NULL);
	if (m->has_extension_ranges)
		lp_print(printer,
			 "if (!Equals(_extensions, other._extensions)) {\n"
			 "  return false;\n"
			 "}\n",
			 NULL);
	lp_outdent(printer);
	lp_print(printer,
		 "  return Equals(_unknownFields, other._unknownFields);\n"
		 "}\n\n",
		 NULL);

	/* GetHashCode. Start with a non-zero value to easily distinguish between null and "empty"
	 * messages. */
	cs_write_generated_code_attributes(printer);
	lp_print(printer,
		 "public override int GetHashCode() {\n"
		 "  int hash = 1;\n",
		 NULL);
	lp_indent(printer);
	for (int i = 0; i < d->n_fields; i++)
		cs_write_hash(cs_message_field_generator(m, &d->fields[i]), printer);
	for (int i = 0; i < d->n_real_oneofs; i++)
		lp_print(printer, "hash ^= (int) $name$Case_;\n", "name",
			 lp_csharp_underscores_to_camel_case(d->oneofs[i].name, false, false), NULL);
	if (m->has_extension_ranges)
		lp_print(printer,
			 "if (_extensions != null) {\n"
			 "  hash ^= _extensions.GetHashCode();\n"
			 "}\n",
			 NULL);
	lp_print(printer,
		 "if (_unknownFields != null) {\n"
		 "  hash ^= _unknownFields.GetHashCode();\n"
		 "}\n"
		 "return hash;\n",
		 NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);

	cs_write_generated_code_attributes(printer);
	lp_print(printer,
		 "public override string ToString() {\n"
		 "  return pb::JsonFormatter.ToDiagnosticString(this);\n"
		 "}\n\n",
		 NULL);
}

static void cs_message_generate_write_to_body(CsMessageGen *m, LpPrinter *printer, bool use_write_context)
{
	/* Serialize all the fields */
	for (int i = 0; i < m->descriptor->n_fields; i++)
		cs_generate_serialization_code(cs_message_field_generator(m, m->fields_by_number[i]), printer,
					       use_write_context);
	if (m->has_extension_ranges) {
		/* Serialize extensions */
		lp_print(printer,
			 use_write_context ? "if (_extensions != null) {\n"
					     "  _extensions.WriteTo(ref output);\n"
					     "}\n"
					   : "if (_extensions != null) {\n"
					     "  _extensions.WriteTo(output);\n"
					     "}\n",
			 NULL);
	}
	/* Serialize unknown fields */
	lp_print(printer,
		 use_write_context ? "if (_unknownFields != null) {\n"
				     "  _unknownFields.WriteTo(ref output);\n"
				     "}\n"
				   : "if (_unknownFields != null) {\n"
				     "  _unknownFields.WriteTo(output);\n"
				     "}\n",
		 NULL);
}

static void cs_message_generate_serialization_methods(CsMessageGen *m, LpPrinter *printer)
{
	cs_write_generated_code_attributes(printer);
	lp_print(printer, "public void WriteTo(pb::CodedOutputStream output) {\n", NULL);
	lp_print(printer, "#if !GOOGLE_PROTOBUF_REFSTRUCT_COMPATIBILITY_MODE\n", NULL);
	lp_indent(printer);
	lp_print(printer, "output.WriteRawMessage(this);\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "#else\n", NULL);
	lp_indent(printer);
	cs_message_generate_write_to_body(m, printer, false);
	lp_outdent(printer);
	lp_print(printer, "#endif\n", NULL);
	lp_print(printer, "}\n\n", NULL);

	lp_print(printer, "#if !GOOGLE_PROTOBUF_REFSTRUCT_COMPATIBILITY_MODE\n", NULL);
	cs_write_generated_code_attributes(printer);
	lp_print(printer, "void pb::IBufferMessage.InternalWriteTo(ref pb::WriteContext output) {\n", NULL);
	lp_indent(printer);
	cs_message_generate_write_to_body(m, printer, true);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
	lp_print(printer, "#endif\n\n", NULL);

	cs_write_generated_code_attributes(printer);
	lp_print(printer, "public int CalculateSize() {\n", NULL);
	lp_indent(printer);
	lp_print(printer, "int size = 0;\n", NULL);
	for (int i = 0; i < m->descriptor->n_fields; i++)
		cs_generate_serialized_size_code(cs_message_field_generator(m, &m->descriptor->fields[i]), printer);
	if (m->has_extension_ranges)
		lp_print(printer,
			 "if (_extensions != null) {\n"
			 "  size += _extensions.CalculateSize();\n"
			 "}\n",
			 NULL);
	lp_print(printer,
		 "if (_unknownFields != null) {\n"
		 "  size += _unknownFields.CalculateSize();\n"
		 "}\n",
		 NULL);
	lp_print(printer, "return size;\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void cs_message_generate_main_parse_loop(CsMessageGen *m, LpPrinter *printer, bool use_parse_context)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "maybe_ref_input", use_parse_context ? "ref input" : "input");
	lp_emit(printer, NULL, 0,
		"\n"
		"    uint tag;\n"
		"    while ((tag = input.ReadTag()) != 0) {\n"
		"    if ((tag & 7) == 4) {\n"
		"      // Abort on any end group tag.\n"
		"      return;\n"
		"    }\n"
		"    switch(tag) {\n"
		"  ");
	lp_indent(printer);
	lp_indent(printer);
	if (m->has_extension_ranges)
		lp_print_vars(printer, &vars,
			      "default:\n"
			      "  if (!pb::ExtensionSet.TryMergeFieldFrom(ref _extensions, $maybe_ref_input$)) {\n"
			      "    _unknownFields = pb::UnknownFieldSet.MergeFieldFrom(_unknownFields, $maybe_ref_input$);\n"
			      "  }\n"
			      "  break;\n");
	else
		lp_print_vars(printer, &vars,
			      "default:\n"
			      "  _unknownFields = pb::UnknownFieldSet.MergeFieldFrom(_unknownFields, $maybe_ref_input$);\n"
			      "  break;\n");
	for (int i = 0; i < m->descriptor->n_fields; i++) {
		const LpField *field = m->fields_by_number[i];
		uint32_t tag = cs_make_tag(field->number, cs_wire_type_for_field_type(field->type));
		/* Handle both packed and unpacked repeated fields with the same Read*Array call; the
		 * two generated cases are the packed and unpacked tags. */
		if (proto_field_is_packable(field))
			lp_print(printer, "case $packed_tag$:\n", "packed_tag", cs_utoa(cs_make_tag(field->number, 2)), NULL);
		lp_print(printer, "case $tag$: {\n", "tag", cs_utoa(tag), NULL);
		lp_indent(printer);
		cs_generate_parsing_code(cs_message_field_generator(m, field), printer, use_parse_context);
		lp_print(printer, "break;\n", NULL);
		lp_outdent(printer);
		lp_print(printer, "}\n", NULL);
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL); /* switch */
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL); /* while */
}

static void cs_message_generate_merging_methods(CsMessageGen *m, LpPrinter *printer)
{
	const LpMessage *d = m->descriptor;
	LpVars vars = {0};
	lp_vars_set(&vars, "class_name", d->name);

	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars, "public void MergeFrom($class_name$ other) {\n");
	lp_indent(printer);
	lp_print(printer,
		 "if (other == null) {\n"
		 "  return;\n"
		 "}\n",
		 NULL);
	/* Merge non-oneof fields, treating optional proto3 fields as normal fields */
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (proto_field_real_oneof(field))
			continue;
		cs_generate_merging_code(cs_message_field_generator(m, field), printer);
	}
	/* Merge oneof fields (for non-synthetic oneofs) */
	for (int i = 0; i < d->n_real_oneofs; ++i) {
		const LpOneof *oneof = &d->oneofs[i];
		cs_message_set_oneof_vars(&vars, oneof);
		lp_print_vars(printer, &vars, "switch (other.$property_name$Case) {\n");
		lp_indent(printer);
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			lp_vars_set(&vars, "oneof_case_name", cs_get_oneof_case_name(field));
			lp_print_vars(printer, &vars, "case $property_name$OneofCase.$oneof_case_name$:\n");
			lp_indent(printer);
			cs_generate_merging_code(cs_message_field_generator(m, field), printer);
			lp_print(printer, "break;\n", NULL);
			lp_outdent(printer);
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}
	/* Merge extensions */
	if (m->has_extension_ranges)
		lp_print(printer, "pb::ExtensionSet.MergeFrom(ref _extensions, other._extensions);\n", NULL);
	/* Merge unknown fields. */
	lp_print(printer, "_unknownFields = pb::UnknownFieldSet.MergeFrom(_unknownFields, other._unknownFields);\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);

	cs_write_generated_code_attributes(printer);
	lp_print(printer, "public void MergeFrom(pb::CodedInputStream input) {\n", NULL);
	lp_print(printer, "#if !GOOGLE_PROTOBUF_REFSTRUCT_COMPATIBILITY_MODE\n", NULL);
	lp_indent(printer);
	lp_print(printer, "input.ReadRawMessage(this);\n", NULL);
	lp_outdent(printer);
	lp_print(printer, "#else\n", NULL);
	lp_indent(printer);
	cs_message_generate_main_parse_loop(m, printer, false);
	lp_outdent(printer);
	lp_print(printer, "#endif\n", NULL);
	lp_print(printer, "}\n\n", NULL);

	lp_print(printer, "#if !GOOGLE_PROTOBUF_REFSTRUCT_COMPATIBILITY_MODE\n", NULL);
	cs_write_generated_code_attributes(printer);
	lp_print(printer, "void pb::IBufferMessage.InternalMergeFrom(ref pb::ParseContext input) {\n", NULL);
	lp_indent(printer);
	cs_message_generate_main_parse_loop(m, printer, true);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL); /* method */
	lp_print(printer, "#endif\n\n", NULL);
}

static void cs_message_generate(const LpMessage *descriptor, const CsOptions *options, LpPrinter *printer);

static void cs_message_generate_impl(CsMessageGen *m, LpPrinter *printer)
{
	const LpMessage *d = m->descriptor;
	LpVars vars = {0};
	lp_vars_set(&vars, "class_name", d->name);
	lp_vars_set(&vars, "access_level", cs_class_access_level(m->options));
	lp_vars_set(&vars, "nrt_annotation", m->nrt_enabled ? "?" : "");

	cs_write_doc_comment(printer, m->options, d);
	cs_message_add_deprecated_flag(m, printer);
	cs_message_add_serializable_attribute(m, printer);

	lp_print(printer, "[global::System.Diagnostics.DebuggerDisplayAttribute(\"{ToString(),nq}\")]\n", NULL);
	lp_print_vars(printer, &vars, "$access_level$ sealed partial class $class_name$ : ");
	if (m->has_extension_ranges)
		lp_print_vars(printer, &vars, "pb::IExtendableMessage<$class_name$>\n");
	else
		lp_print_vars(printer, &vars, "pb::IMessage<$class_name$>\n");
	lp_print(printer, "#if !GOOGLE_PROTOBUF_REFSTRUCT_COMPATIBILITY_MODE\n", NULL);
	lp_print(printer, "    , pb::IBufferMessage\n", NULL);
	lp_print(printer, "#endif\n", NULL);
	lp_print(printer, "{\n", NULL);
	lp_indent(printer);

	/* All static fields and properties */
	lp_print_vars(printer, &vars,
		      "private static readonly pb::MessageParser<$class_name$> _parser = new "
		      "pb::MessageParser<$class_name$>(() => new $class_name$());\n");
	lp_print_vars(printer, &vars, "private pb::UnknownFieldSet$nrt_annotation$ _unknownFields;\n");
	if (m->has_extension_ranges) {
		if (cs_is_descriptor_proto(d->file))
			/* CustomOptions compatibility */
			lp_print_vars(printer, &vars, "internal pb::ExtensionSet<$class_name$>$nrt_annotation$ _extensions;\n");
		else
			lp_print_vars(printer, &vars, "private pb::ExtensionSet<$class_name$>$nrt_annotation$ _extensions;\n");
		/* a read-only property for fast retrieval of the set in IsInitialized */
		lp_print_vars(printer, &vars,
			      "private pb::ExtensionSet<$class_name$>$nrt_annotation$  _Extensions { get { return "
			      "_extensions; } }\n");
	}
	for (int i = 0; i < m->has_bit_field_count; i++)
		lp_print(printer, "private int _hasBits$i$;\n", "i", cs_itoa(i), NULL);

	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars, "public static pb::MessageParser<$class_name$> Parser { get { return _parser; } }\n\n");

	/* Access the message descriptor via the relevant file descriptor or containing message
	 * descriptor. */
	if (!d->containing_type)
		lp_vars_set(&vars, "descriptor_accessor",
			    xcat(lp_csharp_reflection_class_name(d->file), ".Descriptor.MessageTypes[", cs_itoa(d->index),
				 "]"));
	else
		lp_vars_set(&vars, "descriptor_accessor",
			    xcat(lp_csharp_class_name_message(d->containing_type), ".Descriptor.NestedTypes[",
				 cs_itoa(d->index), "]"));

	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "public static pbr::MessageDescriptor Descriptor {\n"
		      "  get { return $descriptor_accessor$; }\n"
		      "}\n"
		      "\n");
	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "pbr::MessageDescriptor pb::IMessage.Descriptor {\n"
		      "  get { return Descriptor; }\n"
		      "}\n"
		      "\n");

	/* Parameterless constructor and partial OnConstruction method. */
	cs_write_generated_code_attributes(printer);
	lp_print_vars(printer, &vars,
		      "public $class_name$() {\n"
		      "  OnConstruction();\n"
		      "}\n\n"
		      "partial void OnConstruction();\n\n");

	cs_message_generate_cloning_code(m, printer);
	/* GenerateFreezingCode is empty */

	/* Fields/properties */
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		lp_print(printer,
			 "/// <summary>Field number for the \"$field_name$\" field.</summary>\n"
			 "public const int $field_constant_name$ = $index$;\n",
			 "field_name", field->name, "field_constant_name", cs_get_field_constant_name(field), "index",
			 cs_itoa(field->number), NULL);
		cs_generate_members(cs_message_field_generator(m, field), printer);
		lp_print(printer, "\n", NULL);
	}

	/* oneof properties (for real oneofs, which come before synthetic ones) */
	for (int i = 0; i < d->n_real_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		cs_message_set_oneof_vars(&vars, oneof);
		lp_vars_set(&vars, "original_name", oneof->name);
		lp_print_vars(printer, &vars,
			      "private object $name$_;\n"
			      "/// <summary>Enum of possible cases for the \"$original_name$\" oneof.</summary>\n"
			      "public enum $property_name$OneofCase {\n");
		lp_indent(printer);
		lp_print(printer, "None = 0,\n", NULL);
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *field = proto_oneof_field(oneof, j);
			lp_print(printer, "$oneof_case_name$ = $index$,\n", "oneof_case_name", cs_get_oneof_case_name(field),
				 "index", cs_itoa(field->number), NULL);
		}
		lp_outdent(printer);
		lp_print(printer, "}\n", NULL);
		lp_print_vars(printer, &vars,
			      "private $property_name$OneofCase $name$Case_ = $property_name$OneofCase.None;\n");
		cs_write_generated_code_attributes(printer);
		lp_print_vars(printer, &vars,
			      "public $property_name$OneofCase $property_name$Case {\n"
			      "  get { return $name$Case_; }\n"
			      "}\n\n");
		cs_write_generated_code_attributes(printer);
		lp_print_vars(printer, &vars,
			      "public void Clear$property_name$() {\n"
			      "  $name$Case_ = $property_name$OneofCase.None;\n"
			      "  $name$_ = null;\n"
			      "}\n\n");
	}

	/* Standard methods */
	cs_message_generate_framework_methods(m, printer);
	cs_message_generate_serialization_methods(m, printer);
	cs_message_generate_merging_methods(m, printer);

	if (m->has_extension_ranges) {
		lp_print_vars(printer, &vars,
			      "public TValue GetExtension<TValue>(pb::Extension<$class_name$, TValue> extension) {\n"
			      "  return pb::ExtensionSet.Get(ref _extensions, extension);\n"
			      "}\n"
			      "public pbc::RepeatedField<TValue>$nrt_annotation$ "
			      "GetExtension<TValue>(pb::RepeatedExtension<$class_name$, TValue> extension) {\n"
			      "  return pb::ExtensionSet.Get(ref _extensions, extension);\n"
			      "}\n"
			      "public pbc::RepeatedField<TValue> "
			      "GetOrInitializeExtension<TValue>(pb::RepeatedExtension<$class_name$, TValue> extension) {\n"
			      "  return pb::ExtensionSet.GetOrInitialize(ref _extensions, extension);\n"
			      "}\n"
			      "public void SetExtension<TValue>(pb::Extension<$class_name$, TValue> extension, TValue value) {\n"
			      "  pb::ExtensionSet.Set(ref _extensions, extension, value);\n"
			      "}\n"
			      "public bool HasExtension<TValue>(pb::Extension<$class_name$, TValue> extension) {\n"
			      "  return pb::ExtensionSet.Has(ref _extensions, extension);\n"
			      "}\n"
			      "public void ClearExtension<TValue>(pb::Extension<$class_name$, TValue> extension) {\n"
			      "  pb::ExtensionSet.Clear(ref _extensions, extension);\n"
			      "}\n"
			      "public void ClearExtension<TValue>(pb::RepeatedExtension<$class_name$, TValue> extension) {\n"
			      "  pb::ExtensionSet.Clear(ref _extensions, extension);\n"
			      "}\n\n");
	}

	/* Nested messages and enums */
	if (cs_message_has_nested_generated_types(m)) {
		lp_print_vars(printer, &vars,
			      "#region Nested types\n"
			      "/// <summary>Container for nested types declared in the $class_name$ message type.</summary>\n");
		cs_write_generated_code_attributes(printer);
		lp_print(printer, "public static partial class Types {\n", NULL);
		lp_indent(printer);
		for (int i = 0; i < d->n_enums; i++)
			cs_enum_generate(&d->enums[i], m->options, printer);
		for (int i = 0; i < d->n_nested; i++)
			/* Don't generate nested types for maps... */
			if (!cs_is_map_entry_message(&d->nested[i]))
				cs_message_generate(&d->nested[i], m->options, printer);
		lp_outdent(printer);
		lp_print(printer,
			 "}\n"
			 "#endregion\n"
			 "\n",
			 NULL);
	}

	if (d->n_extensions > 0) {
		lp_print_vars(printer, &vars,
			      "#region Extensions\n"
			      "/// <summary>Container for extensions for other messages declared in the $class_name$ message "
			      "type.</summary>\n");
		cs_write_generated_code_attributes(printer);
		lp_print(printer, "public static partial class Extensions {\n", NULL);
		lp_indent(printer);
		for (int i = 0; i < d->n_extensions; i++)
			cs_generate_extension_code(cs_create_field_generator(&d->extensions[i], -1, m->options), printer);
		lp_outdent(printer);
		lp_print(printer,
			 "}\n"
			 "#endregion\n"
			 "\n",
			 NULL);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
	lp_print(printer, "\n", NULL);
}

/* MessageGenerator(descriptor, options).Generate(printer) */
static void cs_message_generate(const LpMessage *descriptor, const CsOptions *options, LpPrinter *printer)
{
	CsMessageGen m;
	cs_message_init(&m, descriptor, options);
	cs_message_generate_impl(&m, printer);
}
