// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_field_base.{h,cc} (protobuf v36.2):
 * FieldGeneratorBase, the constructors of its subclasses (cs_field_generator_new) and the
 * dispatch of its virtual methods to the implementation of the most derived class. WriteToString
 * and GenerateFreezingCode are never called upstream and not ported. */

static void cs_fg_set(CsFieldGen *g, const char *key, const char *value) { lp_vars_set(&g->variables, key, value); }

static const char *cs_fg_var(CsFieldGen *g, const char *key)
{
	const char *v = lp_vars_get(&g->variables, key);
	return v ? v : "";
}

static void cs_fg_print(CsFieldGen *g, LpPrinter *printer, const char *text)
{
	lp_print_vars(printer, &g->variables, text);
}

static char *cs_utoa(uint64_t v)
{
	char tmp[24];
	size_t i = sizeof tmp;
	tmp[--i] = 0;
	do {
		tmp[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	return xstrdup(tmp + i);
}

/* WireFormatLite::WireTypeForFieldType */
static int cs_wire_type_for_field_type(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return 1;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return 5;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return 2;
	case PT_GROUP: return 3;
	default: return 0;
	}
}

static uint32_t cs_make_tag(int number, int wire_type) { return ((uint32_t)number << 3) | (uint32_t)wire_type; }

/* CodedOutputStream::WriteTagToArray; returns the number of bytes. */
static int cs_write_tag_to_array(uint32_t tag, uint8_t out[5])
{
	int n = 0;
	while (tag >= 0x80) {
		out[n++] = (uint8_t)(tag | 0x80);
		tag >>= 7;
	}
	out[n++] = (uint8_t)tag;
	return n;
}

static char *cs_tag_bytes(uint32_t tag, int part_tag_size)
{
	uint8_t tag_array[5] = {0};
	(void)cs_write_tag_to_array(tag, tag_array);
	Buf b = {0};
	buf_adds(&b, cs_itoa(tag_array[0]));
	for (int i = 1; i < part_tag_size && i < 5; i++) {
		buf_adds(&b, ", ");
		buf_adds(&b, cs_itoa(tag_array[i]));
	}
	return (char *)buf_str(&b);
}

static char *cs_fg_oneof_case_name(CsFieldGen *g) { return cs_get_oneof_case_name(g->descriptor); }

static char *cs_fg_oneof_property_name(CsFieldGen *g)
{
	return lp_csharp_underscores_to_camel_case(g->descriptor->containing_oneof->name, true, false);
}

static char *cs_fg_oneof_name(CsFieldGen *g)
{
	return lp_csharp_underscores_to_camel_case(g->descriptor->containing_oneof->name, false, false);
}

static char *cs_fg_property_name(CsFieldGen *g) { return cs_get_property_name(g->descriptor); }

static char *cs_fg_name(CsFieldGen *g)
{
	return lp_csharp_underscores_to_camel_case(cs_get_field_name(g->descriptor), false, false);
}

/* Wrapper types nest through field(0); upstream recurses without a limit (a cycle in a
 * google/protobuf/wrappers.proto of the request overflows its stack). */
enum { CS_MAX_WRAPPER_DEPTH = 32 };

static void cs_check_wrapper_depth(const LpField *descriptor, int depth)
{
	if (depth > CS_MAX_WRAPPER_DEPTH)
		fail("unsupported by protoc-gen-uni: wrapper types of google/protobuf/wrappers.proto nested more than "
		     "%d levels (field %s)", CS_MAX_WRAPPER_DEPTH, descriptor->full_name);
}

static char *cs_type_name_depth(CsFieldGen *g, const LpField *descriptor, int depth)
{
	switch (descriptor->type) {
	case PT_ENUM: return lp_csharp_class_name_enum(descriptor->enum_type);
	case PT_MESSAGE:
	case PT_GROUP:
		if (cs_is_wrapper_type(descriptor)) {
			cs_check_wrapper_depth(descriptor, depth);
			const LpField *wrapped_field = cs_wrapped_field(descriptor);
			char *wrapped_field_type_name = cs_type_name_depth(g, wrapped_field, depth + 1);
			/* String and ByteString go to the same type (unless nullable reference type is
			 * supported for this field); other wrapped types go to the nullable equivalent. */
			if (!g->nrt_enabled && (wrapped_field->type == PT_STRING || wrapped_field->type == PT_BYTES))
				return wrapped_field_type_name;
			return xcat(wrapped_field_type_name, "?");
		}
		return lp_csharp_class_name_message(descriptor->message_type);
	case PT_DOUBLE: return "double";
	case PT_FLOAT: return "float";
	case PT_INT64: return "long";
	case PT_UINT64: return "ulong";
	case PT_INT32: return "int";
	case PT_FIXED64: return "ulong";
	case PT_FIXED32: return "uint";
	case PT_BOOL: return "bool";
	case PT_STRING: return "string";
	case PT_BYTES: return "pb::ByteString";
	case PT_UINT32: return "uint";
	case PT_SFIXED32: return "int";
	case PT_SFIXED64: return "long";
	case PT_SINT32: return "int";
	case PT_SINT64: return "long";
	default: return "";
	}
}

static char *cs_fg_type_name(CsFieldGen *g, const LpField *descriptor) { return cs_type_name_depth(g, descriptor, 0); }

static bool cs_fg_has_default_value(CsFieldGen *g)
{
	const LpField *d = g->descriptor;
	switch (d->type) {
	case PT_ENUM: case PT_MESSAGE: case PT_GROUP: case PT_STRING: case PT_BYTES: return true;
	case PT_DOUBLE: return d->default_value.d != 0.0;
	case PT_FLOAT: return d->default_value.f != 0.0;
	case PT_INT64: case PT_INT32: case PT_SFIXED32: case PT_SFIXED64: case PT_SINT32: case PT_SINT64:
		return d->default_value.i != 0;
	case PT_UINT64: case PT_FIXED64: case PT_FIXED32: case PT_UINT32: return d->default_value.u != 0;
	case PT_BOOL: return d->default_value.b;
	default: return true;
	}
}

static char *cs_default_value_depth(CsFieldGen *g, const LpField *descriptor, int depth)
{
	switch (descriptor->type) {
	case PT_ENUM: {
		const LpEnumValue *v = descriptor->default_value_enum;
		if (!v)
			return "";
		return xcat(lp_csharp_class_name_enum(v->type), ".", cs_get_enum_value_name(v->type->name, v->name));
	}
	case PT_MESSAGE:
	case PT_GROUP:
		if (cs_is_wrapper_type(descriptor)) {
			cs_check_wrapper_depth(descriptor, depth);
			return cs_default_value_depth(g, cs_wrapped_field(descriptor), depth + 1);
		}
		return "null";
	case PT_DOUBLE: {
		double value = descriptor->default_value.d;
		if (value == INFINITY)
			return "double.PositiveInfinity";
		if (value == -INFINITY)
			return "double.NegativeInfinity";
		if (isnan(value))
			return "double.NaN";
		return xcat(lp_six_digits(value), "D");
	}
	case PT_FLOAT: {
		float value = descriptor->default_value.f;
		if (value == INFINITY)
			return "float.PositiveInfinity";
		if (value == -INFINITY)
			return "float.NegativeInfinity";
		if (isnan(value))
			return "float.NaN";
		return xcat(lp_six_digits(value), "F");
	}
	case PT_INT64: case PT_SFIXED64: case PT_SINT64: return xcat(cs_itoa(descriptor->default_value.i), "L");
	case PT_UINT64: case PT_FIXED64: return xcat(cs_utoa(descriptor->default_value.u), "UL");
	case PT_INT32: case PT_SFIXED32: case PT_SINT32: return cs_itoa(descriptor->default_value.i);
	case PT_FIXED32: case PT_UINT32: return cs_utoa(descriptor->default_value.u);
	case PT_BOOL: return descriptor->default_value.b ? "true" : "false";
	case PT_STRING: {
		/* GetStringDefaultValueInternal */
		Str s = descriptor->default_value_string;
		if (!s.n)
			return "\"\"";
		return xcat("global::System.Text.Encoding.UTF8.GetString(global::System.Convert.FromBase64String(\"",
			    lp_base64_escape(s), "\"), 0, ", cs_utoa(s.n), ")");
	}
	case PT_BYTES: {
		/* GetBytesDefaultValueInternal */
		Str s = descriptor->default_value_string;
		if (!s.n)
			return "pb::ByteString.Empty";
		return xcat("pb::ByteString.FromBase64(\"", lp_base64_escape(s), "\")");
	}
	default: return "";
	}
}

static char *cs_fg_default_value(CsFieldGen *g, const LpField *descriptor) { return cs_default_value_depth(g, descriptor, 0); }

static const char *cs_fg_capitalized_type_name(CsFieldGen *g)
{
	switch (g->descriptor->type) {
	case PT_ENUM: return "Enum";
	case PT_MESSAGE: return "Message";
	case PT_GROUP: return "Group";
	case PT_DOUBLE: return "Double";
	case PT_FLOAT: return "Float";
	case PT_INT64: return "Int64";
	case PT_UINT64: return "UInt64";
	case PT_INT32: return "Int32";
	case PT_FIXED64: return "Fixed64";
	case PT_FIXED32: return "Fixed32";
	case PT_BOOL: return "Bool";
	case PT_STRING: return "String";
	case PT_BYTES: return "Bytes";
	case PT_UINT32: return "UInt32";
	case PT_SFIXED32: return "SFixed32";
	case PT_SFIXED64: return "SFixed64";
	case PT_SINT32: return "SInt32";
	case PT_SINT64: return "SInt64";
	default: return "";
	}
}

/* SetCommonFieldVariables */
static void cs_set_common_field_variables(CsFieldGen *g)
{
	const LpField *d = g->descriptor;
	/* Note: this will be valid even though the tag emitted for packed and unpacked versions of
	 * repeated fields varies by wire format. The wire format is encoded in the bottom 3 bits,
	 * which never effects the tag size. */
	uint8_t scratch[5];
	int tag_size = cs_write_tag_to_array(cs_make_tag(d->number, 0), scratch);
	if (d->type == PT_GROUP)
		tag_size *= 2;
	int part_tag_size = tag_size;
	if (d->type == PT_GROUP)
		part_tag_size /= 2;
	/* WireFormat::MakeTag(field): packed fields are length-delimited */
	uint32_t tag = cs_make_tag(d->number, proto_field_is_packed(d) ? 2 : cs_wire_type_for_field_type(d->type));
	cs_fg_set(g, "tag", cs_utoa(tag));
	cs_fg_set(g, "tag_size", cs_itoa(tag_size));
	cs_fg_set(g, "tag_bytes", cs_tag_bytes(tag, part_tag_size));
	if (d->type == PT_GROUP) {
		tag = cs_make_tag(d->number, 4);
		cs_fg_set(g, "end_tag", cs_utoa(tag));
		cs_fg_set(g, "end_tag_bytes", cs_tag_bytes(tag, part_tag_size));
	}

	cs_fg_set(g, "access_level", "public");
	char *property_name = cs_fg_property_name(g);
	char *name = cs_fg_name(g);
	char *default_value = cs_fg_default_value(g, d);
	cs_fg_set(g, "property_name", property_name);
	cs_fg_set(g, "type_name", cs_fg_type_name(g, d));
	cs_fg_set(g, "extended_type", lp_csharp_class_name_message(d->containing_type));
	cs_fg_set(g, "name", name);
	cs_fg_set(g, "descriptor_name", d->name);
	cs_fg_set(g, "default_value", default_value);
	cs_fg_set(g, "capitalized_type_name", cs_fg_capitalized_type_name(g));
	cs_fg_set(g, "number", cs_itoa(d->number));
	bool presence_api = cs_supports_presence_api(d);
	if (cs_fg_has_default_value(g) && !presence_api)
		cs_fg_set(g, "name_def_message", xcat(name, "_ = ", default_value));
	else
		cs_fg_set(g, "name_def_message", xcat(name, "_"));
	if (presence_api) {
		char *has_property_check = xcat("Has", property_name);
		char *other_has_property_check = xcat("other.Has", property_name);
		cs_fg_set(g, "has_property_check", has_property_check);
		cs_fg_set(g, "other_has_property_check", other_has_property_check);
		cs_fg_set(g, "has_not_property_check", xcat("!", has_property_check));
		cs_fg_set(g, "other_has_not_property_check", xcat("!", other_has_property_check));
		if (g->presence_index != -1) {
			char *has_bits_number = cs_itoa(g->presence_index / 32);
			/* 1 << 31 is INT_MIN */
			char *has_bits_mask = cs_itoa((int32_t)(1u << (g->presence_index % 32)));
			cs_fg_set(g, "has_field_check", xcat("(_hasBits", has_bits_number, " & ", has_bits_mask, ") != 0"));
			cs_fg_set(g, "set_has_field", xcat("_hasBits", has_bits_number, " |= ", has_bits_mask));
			cs_fg_set(g, "clear_has_field", xcat("_hasBits", has_bits_number, " &= ~", has_bits_mask));
		}
	} else {
		cs_fg_set(g, "has_property_check", xcat(property_name, " != ", default_value));
		cs_fg_set(g, "other_has_property_check", xcat("other.", property_name, " != ", default_value));
	}
	/* This isn't valid everywhere, but we assume that the code which uses the variable will
	 * only do so in a context where the annotation is valid. */
	cs_fg_set(g, "nrt_annotation", g->nrt_enabled ? "?" : "");
}

/* SetCommonOneofFieldVariables */
static void cs_set_common_oneof_field_variables(CsFieldGen *g)
{
	cs_fg_set(g, "oneof_name", cs_fg_oneof_name(g));
	if (cs_supports_presence_api(g->descriptor))
		cs_fg_set(g, "has_property_check", xcat("Has", cs_fg_property_name(g)));
	else
		cs_fg_set(g, "has_property_check", xcat(cs_fg_oneof_name(g), "Case_ == ", cs_fg_oneof_property_name(g),
							"OneofCase.", cs_fg_oneof_case_name(g)));
	cs_fg_set(g, "oneof_case_name", cs_fg_oneof_case_name(g));
	cs_fg_set(g, "oneof_property_name", cs_fg_oneof_property_name(g));
	cs_fg_set(g, "nrt_annotation", g->nrt_enabled ? "?" : "");
}

/* The constructors: FieldGeneratorBase, then those of the classes down to kind. */
static CsFieldGen *cs_field_generator_new(CsFieldKind kind, const LpField *descriptor, int presence_index,
					  const CsOptions *options)
{
	CsFieldGen *g = xalloc(sizeof *g);
	g->kind = kind;
	g->descriptor = descriptor;
	g->presence_index = presence_index;
	g->options = options;
	g->nrt_enabled = cs_nrt_enabled(descriptor->file);
	cs_set_common_field_variables(g);
	switch (kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF: case CS_ENUM: case CS_ENUM_ONEOF:
		cs_primitive_field_init(g);
		break;
	case CS_MESSAGE: case CS_MESSAGE_ONEOF:
		cs_message_field_init(g);
		break;
	case CS_WRAPPER: case CS_WRAPPER_ONEOF:
		cs_wrapper_field_init(g);
		break;
	default:
		break;
	}
	if (kind == CS_PRIMITIVE_ONEOF || kind == CS_ENUM_ONEOF || kind == CS_MESSAGE_ONEOF || kind == CS_WRAPPER_ONEOF)
		cs_set_common_oneof_field_variables(g);
	return g;
}

static void cs_add_deprecated_flag(CsFieldGen *g, LpPrinter *printer)
{
	const LpField *d = g->descriptor;
	if (pb_int(d->options, FIELDOPT_DEPRECATED, 0))
		lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
	else if (d->type == PT_MESSAGE && pb_int(d->message_type->options, MSGOPT_DEPRECATED, 0))
		lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
}

static void cs_add_public_member_attributes(CsFieldGen *g, LpPrinter *printer)
{
	cs_add_deprecated_flag(g, printer);
	cs_write_generated_code_attributes(printer);
}

/* ---- virtual methods ---- */

static void cs_generate_cloning_code(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_ENUM: cs_fg_print(g, printer, "$name$_ = other.$name$_;\n"); break;
	case CS_PRIMITIVE_ONEOF: case CS_ENUM_ONEOF: case CS_WRAPPER: case CS_WRAPPER_ONEOF:
		cs_fg_print(g, printer, "$property_name$ = other.$property_name$;\n");
		break;
	case CS_MESSAGE:
		cs_fg_print(g, printer, "$name$_ = other.$has_property_check$ ? other.$name$_.Clone() : null;\n");
		break;
	case CS_MESSAGE_ONEOF: cs_fg_print(g, printer, "$property_name$ = other.$property_name$.Clone();\n"); break;
	default: cs_fg_print(g, printer, "$name$_ = other.$name$_.Clone();\n"); break;
	}
}

static void cs_generate_codec_code(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	/* EnumOneofFieldGenerator derives from PrimitiveOneofFieldGenerator, not EnumFieldGenerator */
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF: case CS_ENUM_ONEOF: cs_primitive_generate_codec_code(g, printer); break;
	case CS_ENUM: cs_enum_generate_codec_code(g, printer); break;
	case CS_MESSAGE: case CS_MESSAGE_ONEOF: cs_message_generate_codec_code(g, printer); break;
	case CS_WRAPPER: case CS_WRAPPER_ONEOF: cs_wrapper_generate_codec_code(g, printer); break;
	default: break; /* FieldGeneratorBase::GenerateCodecCode is a no-op */
	}
}

static void cs_generate_extension_code(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_MAP: break; /* FieldGeneratorBase::GenerateExtensionCode is a no-op */
	case CS_REPEATED_ENUM: cs_repeated_enum_generate_extension_code(g, printer); break;
	case CS_REPEATED_MESSAGE: cs_repeated_message_generate_extension_code(g, printer); break;
	case CS_REPEATED_PRIMITIVE: cs_repeated_primitive_generate_extension_code(g, printer); break;
	default:
		/* the same text in PrimitiveFieldGenerator, EnumFieldGenerator, MessageFieldGenerator
		 * and WrapperFieldGenerator, around their codec */
		cs_write_doc_comment(printer, g->options, g->descriptor);
		cs_add_deprecated_flag(g, printer);
		cs_fg_print(g, printer,
			    "$access_level$ static readonly pb::Extension<$extended_type$, $type_name$> $property_name$ =\n"
			    "  new pb::Extension<$extended_type$, $type_name$>($number$, ");
		cs_generate_codec_code(g, printer);
		lp_print(printer, ");\n", NULL);
		break;
	}
}

static void cs_generate_members(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_ENUM: cs_primitive_generate_members(g, printer); break;
	case CS_PRIMITIVE_ONEOF: case CS_ENUM_ONEOF: cs_primitive_oneof_generate_members(g, printer); break;
	case CS_MESSAGE: cs_message_generate_members(g, printer); break;
	case CS_MESSAGE_ONEOF: cs_message_oneof_generate_members(g, printer); break;
	case CS_WRAPPER: cs_wrapper_generate_members(g, printer); break;
	case CS_WRAPPER_ONEOF: cs_wrapper_oneof_generate_members(g, printer); break;
	case CS_MAP: cs_map_generate_members(g, printer); break;
	case CS_REPEATED_ENUM: cs_repeated_enum_generate_members(g, printer); break;
	case CS_REPEATED_MESSAGE: cs_repeated_message_generate_members(g, printer); break;
	case CS_REPEATED_PRIMITIVE: cs_repeated_primitive_generate_members(g, printer); break;
	}
}

static void cs_generate_merging_code(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_ENUM:
		cs_fg_print(g, printer,
			    "if ($other_has_property_check$) {\n"
			    "  $property_name$ = other.$property_name$;\n"
			    "}\n");
		break;
	case CS_PRIMITIVE_ONEOF: case CS_ENUM_ONEOF: case CS_WRAPPER_ONEOF:
		cs_fg_print(g, printer, "$property_name$ = other.$property_name$;\n");
		break;
	case CS_MESSAGE:
		cs_fg_print(g, printer,
			    "if (other.$has_property_check$) {\n"
			    "  if ($has_not_property_check$) {\n"
			    "    $property_name$ = new $type_name$();\n"
			    "  }\n"
			    "  $property_name$.MergeFrom(other.$property_name$);\n"
			    "}\n");
		break;
	case CS_MESSAGE_ONEOF:
		cs_fg_print(g, printer,
			    "if ($property_name$ == null) {\n"
			    "  $property_name$ = new $type_name$();\n"
			    "}\n"
			    "$property_name$.MergeFrom(other.$property_name$);\n");
		break;
	case CS_WRAPPER:
		cs_fg_print(g, printer,
			    "if (other.$has_property_check$) {\n"
			    "  if ($has_not_property_check$ || other.$property_name$ != $default_value$) {\n"
			    "    $property_name$ = other.$property_name$;\n"
			    "  }\n"
			    "}\n");
		break;
	case CS_MAP: cs_fg_print(g, printer, "$name$_.MergeFrom(other.$name$_);\n"); break;
	default: cs_fg_print(g, printer, "$name$_.Add(other.$name$_);\n"); break;
	}
}

static void cs_generate_parsing_code(CsFieldGen *g, LpPrinter *printer, bool use_parse_context)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF:
		/* Note: invoke the property setter rather than writing straight to the field, so that
		 * we can normalize "null to empty" for strings and bytes. */
		cs_fg_print(g, printer, "$property_name$ = input.Read$capitalized_type_name$();\n");
		break;
	case CS_ENUM: cs_fg_print(g, printer, "$property_name$ = ($type_name$) input.ReadEnum();\n"); break;
	case CS_ENUM_ONEOF:
		cs_fg_print(g, printer,
			    "$oneof_name$_ = input.ReadEnum();\n"
			    "$oneof_name$Case_ = $oneof_property_name$OneofCase.$oneof_case_name$;\n");
		break;
	case CS_MESSAGE: cs_message_generate_parsing_code(g, printer); break;
	case CS_MESSAGE_ONEOF: cs_message_oneof_generate_parsing_code(g, printer); break;
	case CS_WRAPPER:
		cs_fg_print(g, printer,
			    use_parse_context ? "$type_name$ value = _single_$name$_codec.Read(ref input);\n"
						"if ($has_not_property_check$ || value != $default_value$) {\n"
						"  $property_name$ = value;\n"
						"}\n"
					      : "$type_name$ value = _single_$name$_codec.Read(input);\n"
						"if ($has_not_property_check$ || value != $default_value$) {\n"
						"  $property_name$ = value;\n"
						"}\n");
		break;
	case CS_WRAPPER_ONEOF:
		cs_fg_print(g, printer,
			    use_parse_context ? "$property_name$ = _oneof_$name$_codec.Read(ref input);\n"
					      : "$property_name$ = _oneof_$name$_codec.Read(input);\n");
		break;
	case CS_MAP:
		cs_fg_print(g, printer,
			    use_parse_context ? "$name$_.AddEntriesFrom(ref input, _map_$name$_codec);\n"
					      : "$name$_.AddEntriesFrom(input, _map_$name$_codec);\n");
		break;
	default:
		cs_fg_print(g, printer,
			    use_parse_context ? "$name$_.AddEntriesFrom(ref input, _repeated_$name$_codec);\n"
					      : "$name$_.AddEntriesFrom(input, _repeated_$name$_codec);\n");
		break;
	}
}

static void cs_generate_serialization_code(CsFieldGen *g, LpPrinter *printer, bool use_write_context)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF:
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  output.WriteRawTag($tag_bytes$);\n"
			    "  output.Write$capitalized_type_name$($property_name$);\n"
			    "}\n");
		break;
	case CS_ENUM: case CS_ENUM_ONEOF:
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  output.WriteRawTag($tag_bytes$);\n"
			    "  output.WriteEnum((int) $property_name$);\n"
			    "}\n");
		break;
	case CS_MESSAGE: case CS_MESSAGE_ONEOF: cs_message_generate_serialization_code(g, printer); break;
	case CS_WRAPPER:
		cs_fg_print(g, printer,
			    use_write_context ? "if ($has_property_check$) {\n"
						"  _single_$name$_codec.WriteTagAndValue(ref output, $property_name$);\n"
						"}\n"
					      : "if ($has_property_check$) {\n"
						"  _single_$name$_codec.WriteTagAndValue(output, $property_name$);\n"
						"}\n");
		break;
	case CS_WRAPPER_ONEOF:
		cs_fg_print(g, printer,
			    use_write_context
				    ? "if ($has_property_check$) {\n"
				      "  _oneof_$name$_codec.WriteTagAndValue(ref output, ($type_name$) $oneof_name$_);\n"
				      "}\n"
				    : "if ($has_property_check$) {\n"
				      "  _oneof_$name$_codec.WriteTagAndValue(output, ($type_name$) $oneof_name$_);\n"
				      "}\n");
		break;
	case CS_MAP:
		cs_fg_print(g, printer,
			    use_write_context ? "$name$_.WriteTo(ref output, _map_$name$_codec);\n"
					      : "$name$_.WriteTo(output, _map_$name$_codec);\n");
		break;
	default:
		cs_fg_print(g, printer,
			    use_write_context ? "$name$_.WriteTo(ref output, _repeated_$name$_codec);\n"
					      : "$name$_.WriteTo(output, _repeated_$name$_codec);\n");
		break;
	}
}

static void cs_generate_serialized_size_code(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF: cs_primitive_generate_serialized_size_code(g, printer); break;
	case CS_ENUM: case CS_ENUM_ONEOF:
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  size += $tag_size$ + pb::CodedOutputStream.ComputeEnumSize((int) $property_name$);\n"
			    "}\n");
		break;
	case CS_MESSAGE: case CS_MESSAGE_ONEOF:
		cs_fg_print(g, printer,
			    g->descriptor->type == PT_MESSAGE
				    ? "if ($has_property_check$) {\n"
				      "  size += $tag_size$ + pb::CodedOutputStream.ComputeMessageSize($property_name$);\n"
				      "}\n"
				    : "if ($has_property_check$) {\n"
				      "  size += $tag_size$ + pb::CodedOutputStream.ComputeGroupSize($property_name$);\n"
				      "}\n");
		break;
	case CS_WRAPPER:
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  size += _single_$name$_codec.CalculateSizeWithTag($property_name$);\n"
			    "}\n");
		break;
	case CS_WRAPPER_ONEOF:
		cs_fg_print(g, printer,
			    "if ($has_property_check$) {\n"
			    "  size += _oneof_$name$_codec.CalculateSizeWithTag($property_name$);\n"
			    "}\n");
		break;
	case CS_MAP: cs_fg_print(g, printer, "size += $name$_.CalculateSize(_map_$name$_codec);\n"); break;
	default: cs_fg_print(g, printer, "size += $name$_.CalculateSize(_repeated_$name$_codec);\n"); break;
	}
}

static void cs_write_hash(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF: case CS_ENUM: case CS_ENUM_ONEOF:
	case CS_WRAPPER: case CS_WRAPPER_ONEOF: {
		bool wrapper = g->kind == CS_WRAPPER || g->kind == CS_WRAPPER_ONEOF;
		int type = wrapper ? cs_wrapped_field(g->descriptor)->type : g->descriptor->type;
		const char *text = "if ($has_property_check$) hash ^= $property_name$.GetHashCode();\n";
		if (type == PT_FLOAT)
			text = wrapper ? "if ($has_property_check$) hash ^= pbc::ProtobufEqualityComparers."
					 "BitwiseNullableSingleEqualityComparer.GetHashCode($property_name$);\n"
				       : "if ($has_property_check$) hash ^= pbc::ProtobufEqualityComparers."
					 "BitwiseSingleEqualityComparer.GetHashCode($property_name$);\n";
		else if (type == PT_DOUBLE)
			text = wrapper ? "if ($has_property_check$) hash ^= pbc::ProtobufEqualityComparers."
					 "BitwiseNullableDoubleEqualityComparer.GetHashCode($property_name$);\n"
				       : "if ($has_property_check$) hash ^= pbc::ProtobufEqualityComparers."
					 "BitwiseDoubleEqualityComparer.GetHashCode($property_name$);\n";
		cs_fg_print(g, printer, text);
		break;
	}
	case CS_MESSAGE: case CS_MESSAGE_ONEOF:
		cs_fg_print(g, printer, "if ($has_property_check$) hash ^= $property_name$.GetHashCode();\n");
		break;
	case CS_MAP: cs_fg_print(g, printer, "hash ^= $property_name$.GetHashCode();\n"); break;
	default: cs_fg_print(g, printer, "hash ^= $name$_.GetHashCode();\n"); break;
	}
}

static void cs_write_equals(CsFieldGen *g, LpPrinter *printer)
{
	switch (g->kind) {
	case CS_PRIMITIVE: case CS_PRIMITIVE_ONEOF: case CS_ENUM: case CS_ENUM_ONEOF:
	case CS_WRAPPER: case CS_WRAPPER_ONEOF: {
		bool wrapper = g->kind == CS_WRAPPER || g->kind == CS_WRAPPER_ONEOF;
		int type = wrapper ? cs_wrapped_field(g->descriptor)->type : g->descriptor->type;
		const char *text = "if ($property_name$ != other.$property_name$) return false;\n";
		if (type == PT_FLOAT)
			text = wrapper ? "if (!pbc::ProtobufEqualityComparers.BitwiseNullableSingleEqualityComparer."
					 "Equals($property_name$, other.$property_name$)) return false;\n"
				       : "if (!pbc::ProtobufEqualityComparers.BitwiseSingleEqualityComparer."
					 "Equals($property_name$, other.$property_name$)) return false;\n";
		else if (type == PT_DOUBLE)
			text = wrapper ? "if (!pbc::ProtobufEqualityComparers.BitwiseNullableDoubleEqualityComparer."
					 "Equals($property_name$, other.$property_name$)) return false;\n"
				       : "if (!pbc::ProtobufEqualityComparers.BitwiseDoubleEqualityComparer."
					 "Equals($property_name$, other.$property_name$)) return false;\n";
		cs_fg_print(g, printer, text);
		break;
	}
	case CS_MESSAGE: case CS_MESSAGE_ONEOF:
		cs_fg_print(g, printer, "if (!object.Equals($property_name$, other.$property_name$)) return false;\n");
		break;
	case CS_MAP:
		cs_fg_print(g, printer, "if (!$property_name$.Equals(other.$property_name$)) return false;\n");
		break;
	default: cs_fg_print(g, printer, "if(!$name$_.Equals(other.$name$_)) return false;\n"); break;
	}
}
