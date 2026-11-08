// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of the DebugString methods of src/google/protobuf/descriptor.cc (FieldDescriptor,
 * EnumValueDescriptor, MethodDescriptor: RetrieveOptions, FormatBracketedOptions,
 * FormatLineOptions) and of TextFormat::Printer::PrintFieldValueToString (text_format.cc,
 * FastFieldValuePrinter) for the values of options, with the default DebugStringOptions (no
 * comments).
 *
 * The generators only print the first line of a debug string (java/kotlin doc_comment.cc
 * FirstLineOf, cpp FieldComment, php), so these functions build the debug string up to its first
 * newline and stop there: the text of a message-typed option value starts with "{\n", and the
 * group body and the options of a method on their own lines come after it. */

static const char *const lp_type_names[] = {
	"", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool", "string",
	"group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64",
};
static const char *const lp_label_names[] = {"", "optional", "required", "repeated"};

/* IsGroupSyntax */
static bool is_group_syntax(int edition, const LpField *f) { return is_legacy_edition(edition) && f->type == PT_GROUP; }

/* FieldDescriptor::FieldTypeNameDebugString */
static const char *field_type_name_debug_string(const LpField *f)
{
	switch (f->type) {
	case PT_MESSAGE:
	case PT_GROUP:
		if (is_group_syntax(f->file->edition, f))
			return lp_type_names[f->type];
		return xcat(".", f->message_type ? f->message_type->full_name : "");
	case PT_ENUM: return xcat(".", f->enum_type ? f->enum_type->full_name : "");
	default: return f->type < (int)(sizeof lp_type_names / sizeof *lp_type_names) ? lp_type_names[f->type] : "";
	}
}

/* FastFieldValuePrinter for one scalar value of field f (TextFormat::PrintFieldValueToString). */
static void print_scalar_value(Buf *out, const LpField *f, PbVal v)
{
	switch (cpp_type_of(f->type)) {
	case LP_CPPTYPE_INT32: buf_adds(out, lp_itoa((int32_t)v.u)); break;
	case LP_CPPTYPE_INT64: buf_adds(out, lp_itoa((int64_t)v.u)); break;
	case LP_CPPTYPE_UINT32: buf_adds(out, lp_utoa((uint32_t)v.u)); break;
	case LP_CPPTYPE_UINT64: buf_adds(out, lp_utoa(v.u)); break;
	case LP_CPPTYPE_FLOAT: {
		uint32_t bits = (uint32_t)v.u;
		float x;
		memcpy(&x, &bits, sizeof x);
		buf_adds(out, isnan(x) ? "nan" : lp_simple_ftoa(x));
		break;
	}
	case LP_CPPTYPE_DOUBLE: {
		double x;
		memcpy(&x, &v.u, sizeof x);
		buf_adds(out, isnan(x) ? "nan" : lp_simple_dtoa(x));
		break;
	}
	case LP_CPPTYPE_BOOL: buf_adds(out, v.u ? "true" : "false"); break;
	case LP_CPPTYPE_STRING:
		buf_addc(out, '"');
		buf_adds(out, lp_cescape(v.s));
		buf_addc(out, '"');
		break;
	case LP_CPPTYPE_ENUM: {
		const LpEnumValue *ev = f->enum_type ? proto_enum_value_by_number(f->enum_type, (int32_t)v.u) : NULL;
		buf_adds(out, ev ? ev->name : lp_itoa((int32_t)v.u));
		break;
	}
	default: break;
	}
}

/* RetrieveOptions (with the reparse in the descriptor's pool when it defines the options type)
 * + RetrieveOptionsAssumingRightPool, joined with sep. Returns false if there are no options;
 * sets *cut when a message value started (its text continues on the next line). */
static bool retrieve_options(const PbMsg *options, const char *full_name, LpPool *pool, const char *sep, Buf *out,
			     bool *cut)
{
	*cut = false;
	if (!options)
		return false;
	const LpMessage *desc = lp_find_message(pool, full_name);
	if (desc && pool->builtin) {
		PbMsg *dynamic_options = pb_new(&lp_msgdef(pool, desc)->pb);
		Str serialized = lp_serialize_str(options);
		if (lp_decode(dynamic_options, (const uint8_t *)serialized.p, serialized.n))
			options = dynamic_options;
		/* else: "Found invalid proto option data for: ..." (logged), the compiled options */
	}
	const LpMsgDef *d = lp_def(options);
	int count = 0;
	for (int i = 0; i < d->pb.n; i++) {
		const PbSlot *s = &options->slots[i];
		const LpField *field = d->fields[i];
		for (int j = 0; j < s->n; j++) {
			if (count++)
				buf_adds(out, sep);
			if (field->is_extension)
				buf_adds(out, xcat("(.", field->full_name, ")"));
			else
				buf_adds(out, field->name);
			buf_adds(out, " = ");
			if (cpp_type_of(field->type) == LP_CPPTYPE_MESSAGE) {
				/* "{\n" + TextFormat of the message + "}" */
				buf_addc(out, '{');
				*cut = true;
				return true;
			}
			print_scalar_value(out, field, s->v[j]);
		}
	}
	return count > 0;
}

/* The options of a descriptor with its proto_features copied back (CopyFeaturesToOptions). */
static PbMsg *new_options(LpPool *pool, const char *full_name)
{
	return pb_new(&lp_msgdef_by_name(pool->builtin ? pool->builtin : pool, full_name)->pb);
}

static PbMsg *full_options(LpPool *pool, const PbMsg *options, const PbMsg *proto_features, const char *full_name,
			   uint32_t features_field)
{
	if (!options && !proto_features)
		return NULL;
	PbMsg *o = options ? proto_msg_clone(options) : new_options(pool, full_name);
	if (proto_features)
		proto_msg_set_msg(o, features_field, proto_msg_clone(proto_features));
	return o;
}

char *lp_field_debug_string_first_line(const LpField *f)
{
	Buf out = {0};
	if (f->is_extension) {
		/* "extend .$0 {\n" */
		buf_adds(&out, xcat("extend .", f->containing_type ? f->containing_type->full_name : "", " {"));
		return (char *)buf_str(&out);
	}
	const char *field_type;
	if (proto_field_is_map(f))
		field_type = xcat("map<", field_type_name_debug_string(&f->message_type->fields[0]), ", ",
				  field_type_name_debug_string(&f->message_type->fields[1]), ">");
	else
		field_type = field_type_name_debug_string(f);
	const char *label = xcat(lp_label_names[f->label >= 1 && f->label <= 3 ? f->label : 0], " ");
	int edition = f->file->edition;
	if (proto_field_is_map(f) || proto_field_real_oneof(f) ||
	    (!proto_field_is_required(f) && !proto_field_is_repeated(f) && !f->proto3_optional &&
	     edition == LP_EDITION_PROTO3))
		label = "";
	if (!proto_field_is_repeated(f) && !is_legacy_edition(edition))
		label = "";
	buf_adds(&out, label);
	buf_adds(&out, field_type);
	buf_addc(&out, ' ');
	buf_adds(&out, is_group_syntax(edition, f) && f->message_type ? f->message_type->name : f->name);
	buf_adds(&out, " = ");
	buf_adds(&out, lp_itoa(f->number));

	bool bracketed = false;
	if (f->has_default_value) {
		bracketed = true;
		buf_adds(&out, " [default = ");
		buf_adds(&out, lp_field_default_value_as_string(f, true));
	}
	if (f->has_json_name) {
		if (!bracketed) {
			bracketed = true;
			buf_adds(&out, " [");
		} else {
			buf_adds(&out, ", ");
		}
		buf_adds(&out, "json_name = \"");
		buf_adds(&out, lp_cescape(str_c(f->json_name)));
		buf_adds(&out, "\"");
	}
	LpPool *pool = lp_file_pool(f->file);
	PbMsg *opts = full_options(pool, f->options, f->proto_features, "google.protobuf.FieldOptions", FOPT_FEATURES);
	if (field_legacy_ctype(f) >= 0) {
		if (!opts)
			opts = new_options(pool, "google.protobuf.FieldOptions");
		proto_msg_set_u(opts, FOPT_CTYPE, (uint64_t)field_legacy_ctype(f));
	}
	Buf formatted = {0};
	bool cut;
	if (retrieve_options(opts, "google.protobuf.FieldOptions", pool, ", ", &formatted, &cut)) {
		buf_adds(&out, bracketed ? ", " : " [");
		bracketed = true;
		buf_add(&out, formatted.p, formatted.n);
		if (cut)
			return (char *)buf_str(&out);
	}
	if (bracketed)
		buf_adds(&out, "]");
	if (is_group_syntax(edition, f))
		buf_adds(&out, " {"); /* message_type()->DebugString(..., include_opening_clause = false) */
	else
		buf_adds(&out, ";");
	return (char *)buf_str(&out);
}

char *lp_enum_value_debug_string_first_line(const LpEnumValue *v)
{
	Buf out = {0};
	buf_adds(&out, v->name);
	buf_adds(&out, " = ");
	buf_adds(&out, lp_itoa(v->number));
	LpPool *pool = lp_file_pool(v->type->file);
	PbMsg *opts = full_options(pool, v->options, v->proto_features, "google.protobuf.EnumValueOptions",
				   EVOPT_FEATURES);
	Buf formatted = {0};
	bool cut;
	if (retrieve_options(opts, "google.protobuf.EnumValueOptions", pool, ", ", &formatted, &cut)) {
		buf_adds(&out, " [");
		buf_add(&out, formatted.p, formatted.n);
		if (cut)
			return (char *)buf_str(&out);
		buf_adds(&out, "]");
	}
	buf_adds(&out, ";");
	return (char *)buf_str(&out);
}

char *lp_method_debug_string_first_line(const LpMethod *m)
{
	Buf out = {0};
	buf_adds(&out, xcat("rpc ", m->name, "(", m->client_streaming ? "stream " : "",
			    m->input_type ? m->input_type->full_name : "", ") returns (",
			    m->server_streaming ? "stream " : "", m->output_type ? m->output_type->full_name : "",
			    ")"));
	LpPool *pool = lp_file_pool(m->service->file);
	PbMsg *opts = full_options(pool, m->options, m->proto_features, "google.protobuf.MethodOptions",
				   METHOPT_FEATURES);
	Buf formatted = {0};
	bool cut;
	/* FormatLineOptions: "  option <entry>;\n" per entry, after " {\n" */
	if (retrieve_options(opts, "google.protobuf.MethodOptions", pool, "", &formatted, &cut))
		buf_adds(&out, " {");
	else
		buf_adds(&out, ";");
	return (char *)buf_str(&out);
}
