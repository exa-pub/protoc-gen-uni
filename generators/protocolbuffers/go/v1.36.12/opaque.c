/* Copyright 2024 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from cmd/protoc-gen-go/internal_gengo/opaque.go (protobuf-go v1.36.12). */

static Str field_protobuf_tag_value(PgField *field);
static Str field_json_tag_value(PgField *field);
static const char *field_default_value(PgGeneratedFile *g, FileInfo *f, MessageInfo *m, PgField *field);
static void gen_message_default_decls(PgGeneratedFile *g, FileInfo *f, MessageInfo *m);
static void gen_message_base_methods(PgGeneratedFile *g, FileInfo *f, MessageInfo *m);
static const char *opaque_field_go_type(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field, bool *pointer);
static PgGoIdent opaque_field_oneof_type(const PgField *field, bool is_opaque);
static const char *opaque_oneof_field_name(const PgOneof *oneof, bool is_opaque);
static const char *opaque_oneof_interface_name(const PgOneof *oneof);
static const char *opaque_oneof_case_type_name(const PgOneof *oneof);
static int opaque_field_presence_index(const PgField *field);
static int opaque_num_presence_fields(const MessageInfo *message);
static bool opaque_needs_presence_array(const MessageInfo *message);
static bool opaque_needs_lazy_struct(const MessageInfo *message);
static void opaque_gen_message_field(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field);
static void opaque_gen_oneof_fields(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof);
static void opaque_gen_message_internal_fields(PgGeneratedFile *g, FileInfo *f, MessageInfo *message);
static void opaque_gen_message_methods(PgGeneratedFile *g, FileInfo *f, MessageInfo *message);
static void opaque_gen_message_builder(PgGeneratedFile *g, FileInfo *f, MessageInfo *message);
static void opaque_gen_oneof_wrapper_types(PgGeneratedFile *g, FileInfo *f, MessageInfo *message);

#define X_IDENT ID(protoimplPackage, "X")

/* strings.ReplaceAll for a Str (struct tags may contain any bytes of a JSON name or default) */
static COLD Str replace_all(Str s, const char *from, const char *to)
{
	size_t fl = strlen(from);
	Buf b = {0};
	size_t i = 0;
	while (i < s.n) {
		if (s.n - i >= fl && memcmp(s.p + i, from, fl) == 0) {
			buf_adds(&b, to);
			i += fl;
		} else {
			buf_addc(&b, s.p[i++]);
		}
	}
	Str r = buf_view(&b);
	return r.p ? r : (Str){"", 0};
}

static COLD bool is_first_oneof_field(const PgField *field)
{
	return field->Oneof && field == field->Oneof->Fields[0] && !go_oneof_is_synthetic(field->Oneof->Desc);
}

static COLD bool is_last_oneof_field(const PgField *field)
{
	return field->Oneof && field == field->Oneof->Fields[field->Oneof->nFields - 1];
}

static bool is_real_oneof(const PgOneof *o) { return o && !go_oneof_is_synthetic(o->Desc); }

static COLD bool field_deprecated(const PgField *field)
{
	return pb_int(field->Desc->d.options, GENID_FieldOptions_Deprecated_field_number, 0) != 0;
}

static COLD void opaque_gen_message(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	/* Message type declaration. */
	pg_annotate_symbol(g, message->m->GoIdent.GoName, (PgAnnotation){message->m->Location, 0});
	PgComments leading = append_deprecation_suffix(message->m->Comments.Leading, message->m->Desc->d.file,
						       pb_int(message->m->Desc->d.options, GENID_MessageOptions_Deprecated_field_number, 0) != 0);
	P(g, leading, "type ", message->m->GoIdent, " struct {");

	StructTag tag = {"protogen", str_c(is_open(message) ? "open.v1" : is_hybrid(message) ? "hybrid.v1" : "opaque.v1")};
	const char *tags = is_open(message) || is_hybrid(message) || is_opaque(message) ? struct_tags(&tag, 1) : "";
	P(g, GENID_State_goname, " ", ID(protoimplPackage, "MessageState"), tags);
	sf_append(message);
	for (int i = 0; i < message->m->nFields; i++)
		opaque_gen_message_field(g, f, message, message->m->Fields[i]);
	opaque_gen_message_internal_fields(g, f, message);
	P(g, "}");
	P(g, "");

	gen_message_known_functions(g, f, message);
	gen_message_default_decls(g, f, message);
	opaque_gen_message_methods(g, f, message);
	opaque_gen_message_builder(g, f, message);
	opaque_gen_oneof_wrapper_types(g, f, message);
}

/* opaqueGenMessageField generates a struct field. */
static COLD void opaque_gen_message_field(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field)
{
	PgOneof *oneof = field->Oneof;
	if (is_real_oneof(oneof)) {
		/* Generating the field here keeps the contents of the Go struct in the same order as
		 * the contents of the source .proto file. */
		if (field != oneof->Fields[0])
			return;
		opaque_gen_oneof_fields(g, f, message, oneof);
		return;
	}

	bool pointer;
	const char *go_type = opaque_field_go_type(g, f, message, field, &pointer);
	if (pointer)
		go_type = xcat("*", go_type);
	Str protobuf_tag = field_protobuf_tag_value(field);
	Str json_tag = field_json_tag_value(field);
	if (pg_generated_file_internal_strip_for_editions_diff(g)) {
		if (field->Desc->oneof && go_oneof_is_synthetic(field->Desc->oneof))
			protobuf_tag = replace_all(protobuf_tag, ",oneof", "");
		protobuf_tag = replace_all(protobuf_tag, ",proto3", "");
	}
	StructTag tagv[5];
	int nt = 0;
	tagv[nt++] = (StructTag){"protobuf", protobuf_tag};
	if (!is_opaque(message))
		tagv[nt++] = (StructTag){"json", json_tag};
	if (go_field_is_map(field->Desc)) {
		Str key = field_protobuf_tag_value(field->Message->Fields[0]);
		Str val = field_protobuf_tag_value(field->Message->Fields[1]);
		tagv[nt++] = (StructTag){"protobuf_key", replace_all(key, ",proto3", "")};
		tagv[nt++] = (StructTag){"protobuf_val", replace_all(val, ",proto3", "")};
	}

	const char *name = field->GoName;
	if (is_opaque(message))
		name = xcat("xxx_hidden_", name);

	if (is_opaque(message)) {
		P(g, name, " ", go_type, struct_tags(tagv, nt));
		sf_append(message);
		if (message->isTracked) {
			P(g, "// Deprecated: Do not use. This will be deleted in the near future.");
			P(g, "XXX_ft_", field->GoName, " struct{} `go:\"track\"`");
			sf_append(message);
		}
	} else {
		if (message->isTracked)
			tagv[nt++] = (StructTag){"go", str_c("track")};
		pg_annotate_symbol(g, xcat(field->Parent->GoIdent.GoName, ".", name), (PgAnnotation){field->Location, 0});
		PgComments leading = append_deprecation_suffix(field->Comments.Leading, field->Desc->d.file, field_deprecated(field));
		P(g, leading, name, " ", go_type, struct_tags(tagv, nt), trailing_comment(field->Comments.Trailing));
		sf_append(message);
	}
}

/* opaqueGenOneofFields generates the message fields for a oneof. */
static COLD void opaque_gen_oneof_fields(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof)
{
	(void)f;
	StructTag tagv[2] = {{"protobuf_oneof", str_c(oneof->Desc->d.name)}, {"go", str_c("track")}};
	int nt = message->isTracked ? 2 : 1;
	const char *oneof_name = opaque_oneof_field_name(oneof, is_opaque(message));
	const char *go_type = opaque_oneof_interface_name(oneof);

	if (is_opaque(message)) {
		P(g, oneof_name, " ", go_type, struct_tags(tagv, nt));
		sf_append(message);
		if (message->isTracked) {
			P(g, "// Deprecated: Do not use. This will be deleted in the near future.");
			P(g, "XXX_ft_", oneof->GoName, " struct{} `go:\"track\"`");
			sf_append(message);
		}
		return;
	}

	PgComments leading = oneof->Comments.Leading;
	if (leading.s.n)
		leading = pg_comments_cat(leading, "\n");
	/* The extra \n here is working around #52605, making the comment be in Go 1.19 doc
	 * comment format even though it's not really a doc comment. */
	Buf ss = {0};
	buf_adds(&ss, xcat(" Types that are valid to be assigned to ", oneof_name, ":\n\n"));
	for (int i = 0; i < oneof->nFields; i++)
		buf_adds(&ss, xcat("\t*", opaque_field_oneof_type(oneof->Fields[i], is_opaque(message)).GoName, "\n"));
	leading = pg_comments_cat(leading, buf_str(&ss));
	P(g, leading, oneof_name, " ", go_type, struct_tags(tagv, nt));
	sf_append(message);
}

/* opaqueGenMessageInternalFields adds additional XXX_ fields to a message struct. */
static COLD void opaque_gen_message_internal_fields(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	(void)f;
	if (opaque_needs_presence_array(message)) {
		if (opaque_needs_lazy_struct(message)) {
			P(g, "// Deprecated: Do not use. This will be deleted in the near future.");
			P(g, "XXX_lazyUnmarshalInfo ", ID(protoimplPackage, "LazyUnmarshalInfo"));
			sf_append(message);
		}
		P(g, "XXX_raceDetectHookData ", ID(protoimplPackage, "RaceDetectHookData"));
		sf_append(message);
		/* Presence must be stored in a data type no larger than 32 bit. */
		P(g, "XXX_presence [", (opaque_num_presence_fields(message) + 31) / 32, "]uint32");
		sf_append(message);
	}
	if (message->m->Desc->n_ext_ranges > 0) {
		P(g, GENID_ExtensionFields_goname, " ", ID(protoimplPackage, "ExtensionFields"));
		sf_append(message);
	}
	P(g, GENID_UnknownFields_goname, " ", ID(protoimplPackage, "UnknownFields"));
	sf_append(message);
	P(g, GENID_SizeCache_goname, " ", ID(protoimplPackage, "SizeCache"));
	sf_append(message);
}

static void opaque_gen_get(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field);
static void opaque_gen_set(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field);
static void opaque_gen_has(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field);
static void opaque_gen_clear(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field);
static void opaque_gen_get_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof);
static void opaque_gen_has_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof);
static void opaque_gen_clear_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof);
static void opaque_gen_which_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message);

static COLD void opaque_gen_message_methods(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	gen_message_base_methods(g, f, message);
	PgMessage *m = message->m;
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		if (is_first_oneof_field(field) && !is_opaque(message))
			opaque_gen_get_oneof(g, f, message, field->Oneof);
		opaque_gen_get(g, f, message, field);
	}
	for (int i = 0; i < m->nFields; i++) {
		/* For the plain open mode, we do not have setters. */
		if (is_open(message))
			continue;
		opaque_gen_set(g, f, message, m->Fields[i]);
	}
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		/* Open API does not have Has method. Repeated (includes map) fields do not have Has
		 * method. */
		if (is_open(message) || field->Desc->cardinality == GO_REPEATED)
			continue;
		if (!go_field_has_presence(field->Desc))
			continue;
		if (is_first_oneof_field(field))
			opaque_gen_has_oneof(g, f, message, field->Oneof);
		opaque_gen_has(g, f, message, field);
	}
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		if (is_open(message) || field->Desc->cardinality == GO_REPEATED)
			continue;
		if (!go_field_has_presence(field->Desc))
			continue;
		if (is_first_oneof_field(field))
			opaque_gen_clear_oneof(g, f, message, field->Oneof);
		opaque_gen_clear(g, f, message, field);
	}
	/* Plain open protos do not have which methods. */
	if (!is_open(message))
		opaque_gen_which_oneof(g, f, message);
}

static COLD bool is_lazy(const PgField *field)
{
	/* Prerequisite: field is of kind message */
	if (!field->Message)
		return false;
	/* Was the field marked as [lazy = true] in the .proto file? */
	return field->Desc->is_lazy;
}

/* usePresence returns true if the presence map should be used for a field. */
static COLD bool use_presence(const MessageInfo *message, const PgField *field)
{
	if (!is_opaque(message))
		return false;
	const GoField *fd = field->Desc;
	bool can_be_lazy;
	return features_use_presence_for_field(fd->oneof && !go_oneof_is_synthetic(fd->oneof), go_field_is_map(fd),
					       fd->kind == PT_MESSAGE || fd->kind == PT_GROUP, fd->is_lazy, go_field_has_presence(fd),
					       &can_be_lazy);
}

static COLD void field_track_no_interface(PgGeneratedFile *g, bool is_tracked)
{
	if (is_tracked)
		P(g, "//go:nointerface");
}

/* opaqueGenGet generates a Get method for a field. */
static COLD void opaque_gen_get(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field)
{
	bool pointer;
	const char *go_type = opaque_field_go_type(g, f, message, field, &pointer);
	const char *bc_name;
	const char *getter_name = pg_field_method_name(field, "Get", &bc_name);

	PgComments leading = append_deprecation_suffix(pg_comments(""), field->Desc->d.file, field_deprecated(field));
	field_track_no_interface(g, message->isTracked);
	pg_annotate_symbol(g, xcat(message->m->GoIdent.GoName, ".", getter_name), (PgAnnotation){field->Location, 0});

	const char *default_value = field_default_value(g, f, message, field);

	PgOneof *oneof = field->Oneof;
	if (is_real_oneof(oneof)) {
		/* Oneof field. */
		const char *struct_ptr = "x";
		P(g, leading, "func (x *", message->m->GoIdent, ") ", getter_name, "() ", go_type, " {");
		P(g, "if x != nil {");
		if (is_opaque(message) && message->isTracked)
			P(g, "_ = ", struct_ptr, ".XXX_ft_", field->Oneof->GoName);
		P(g, "if x, ok := ", struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), ".(*",
		  opaque_field_oneof_type(field, is_opaque(message)), "); ok {");
		P(g, "return x.", field->GoName);
		P(g, "}");
		P(g, "}"); /* End if m != nil {. */
		P(g, "return ", default_value);
		P(g, "}");
		P(g, "");
	} else if (!is_opaque(message)) {
		/* Non-oneof field for open type message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", getter_name, "() ", go_type, " {");
		if (!go_field_has_presence(field->Desc) || str_eq(default_value, "nil"))
			P(g, "if x != nil {");
		else
			P(g, "if x != nil && x.", field->GoName, " != nil {");
		P(g, "return ", pointer ? "*" : "", " x.", field->GoName);
		P(g, "}");
		P(g, "return ", default_value);
		P(g, "}");
		P(g, "");
	} else {
		/* Non-oneof field for opaque type message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", getter_name, "() ", go_type, "{");
		const char *struct_ptr = "x";
		P(g, "if x != nil {");
		if (message->isTracked)
			P(g, "_ = ", struct_ptr, ".XXX_ft_", field->GoName);
		if (use_presence(message, field)) {
			int pi = opaque_field_presence_index(field);
			int ai = pi / 32;
			/* For lazy message fields, fields with a default value and closed enums,
			 * presence is checked; for other fields using presence, whatever is there is
			 * returned. */
			bool is_enum = field->Desc->kind == PT_ENUM;
			bool use_presence_for_read = is_lazy(field) || field->Desc->has_default || is_enum;
			if (use_presence_for_read)
				P(g, "if ", X_IDENT, ".Present(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ") {");
			/* For lazy, check if pointer is nil and optionally unmarshal */
			if (is_lazy(field)) {
				P(g, "if ", X_IDENT, ".AtomicCheckPointerIsNil(&", struct_ptr, ".xxx_hidden_", field->GoName, ") {");
				P(g, X_IDENT, ".UnmarshalField(", struct_ptr, ", ", field->Desc->number, ")");
				P(g, "}");
			}
			const char *star = pointer ? "*" : "";
			if (!field->Message || go_field_is_map(field->Desc)) {
				if (pointer)
					P(g, "if ", struct_ptr, ".xxx_hidden_", field->GoName, "!= nil {");
				P(g, "return ", star, struct_ptr, ".xxx_hidden_", field->GoName);
				if (pointer) {
					P(g, "}");
					P(g, "return ", default_value);
				}
			} else {
				/* An atomic load of the msg pointer field via protoimpl.Pointer. */
				if (is_lazy(field)) {
					P(g, "var rv ", star, go_type);
					P(g, X_IDENT, ".AtomicLoadPointer(", ID(protoimplPackage, "Pointer"), "(&", struct_ptr, ".xxx_hidden_",
					  field->GoName, "), ", ID(protoimplPackage, "Pointer"), "(&rv))");
					P(g, "return ", star, "rv");
				} else {
					if (pointer)
						P(g, "if ", struct_ptr, ".xxx_hidden_", field->GoName, "!= nil {");
					P(g, "return ", star, struct_ptr, ".xxx_hidden_", field->GoName);
					if (pointer)
						P(g, "}");
				}
			}
			if (use_presence_for_read)
				P(g, "}");
		} else if (pointer) {
			P(g, "if ", struct_ptr, ".xxx_hidden_", field->GoName, " != nil {");
			P(g, "return *", struct_ptr, ".xxx_hidden_", field->GoName);
			P(g, "}");
		} else {
			P(g, "return ", struct_ptr, ".xxx_hidden_", field->GoName);
		}
		P(g, "}"); /* End if m != nil {. */
		P(g, "return ", default_value);
		P(g, "}");
		P(g, "");
	}

	/* If we need a backwards compatible getter name, we add it now (deferred upstream). */
	if (bc_name) {
		P(g, "// Deprecated: Use ", getter_name, " instead.");
		P(g, "func (x *", message->m->GoIdent, ") ", bc_name, "() ", go_type, " {");
		P(g, "return x.", getter_name, "()");
		P(g, "}");
		P(g, "");
	}
}

/* opaqueGenSet generates a Set method for a field. */
static COLD void opaque_gen_set(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field)
{
	bool pointer;
	const char *go_type = opaque_field_go_type(g, f, message, field, &pointer);
	const char *bc_name;
	const char *setter_name = pg_field_method_name(field, "Set", &bc_name);

	PgComments leading = append_deprecation_suffix(pg_comments(""), field->Desc->d.file, field_deprecated(field));
	pg_annotate_symbol(g, xcat(message->m->GoIdent.GoName, ".", setter_name),
			   (PgAnnotation){field->Location, GENID_GeneratedCodeInfo_Annotation_SET_enum_value + 1});
	field_track_no_interface(g, message->noInterface);

	PgOneof *oneof = field->Oneof;
	const char *amp = pointer ? "&" : "";
	if (is_real_oneof(oneof)) {
		/* Oneof field. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", setter_name, "(v ", go_type, ") {");
		const char *struct_ptr = "x";
		if (is_opaque(message) && message->isTracked)
			P(g, struct_ptr, ".XXX_ft_", oneof->GoName, " = struct{}{}");
		if (field->Desc->kind == PT_BYTES) {
			P(g, "if v == nil { v = []byte{} }");
		} else if (field->Message) {
			P(g, "if v == nil {");
			P(g, struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), "= nil");
			P(g, "return");
			P(g, "}");
		}
		P(g, struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), "= &", opaque_field_oneof_type(field, is_opaque(message)), "{v}");
		P(g, "}");
		P(g, "");
	} else if (!is_opaque(message)) {
		/* Non-oneof field for open type message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", setter_name, "(v ", go_type, ") {");
		if (field->Desc->cardinality != GO_REPEATED && field->Desc->kind == PT_BYTES)
			P(g, "if v == nil { v = []byte{} }");
		P(g, "x.", field->GoName, " = ", amp, "v");
		P(g, "}");
		P(g, "");
	} else {
		/* Non-oneof field for opaque type message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", setter_name, "(v ", go_type, ") {");
		const char *struct_ptr = "x";
		if (message->isTracked)
			P(g, struct_ptr, ".XXX_ft_", field->GoName, " = struct{}{}");
		if (field->Desc->cardinality != GO_REPEATED && field->Desc->kind == PT_BYTES)
			P(g, "if v == nil { v = []byte{} }");
		if (use_presence(message, field)) {
			int pi = opaque_field_presence_index(field);
			int ai = pi / 32;
			if (field->Message && go_field_is_list(field->Desc)) {
				P(g, "var sv *", go_type);
				P(g, X_IDENT, ".AtomicLoadPointer(", ID(protoimplPackage, "Pointer"), "(&", struct_ptr, ".xxx_hidden_", field->GoName,
				  "), ", ID(protoimplPackage, "Pointer"), "(&sv))");
				P(g, "if sv == nil {");
				P(g, "sv = &", go_type, "{}");
				P(g, X_IDENT, ".AtomicInitializePointer(", ID(protoimplPackage, "Pointer"), "(&", struct_ptr, ".xxx_hidden_",
				  field->GoName, "), ", ID(protoimplPackage, "Pointer"), "(&sv))");
				P(g, "}");
				P(g, "*sv = v");
				P(g, X_IDENT, ".SetPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ",", opaque_num_presence_fields(message), ")");
			} else if (field->Message && !go_field_is_map(field->Desc)) {
				/* Only for lazy messages do we need to set pointers atomically */
				if (is_lazy(field))
					P(g, X_IDENT, ".AtomicSetPointer(&", struct_ptr, ".xxx_hidden_", field->GoName, ", ", amp, "v)");
				else
					P(g, struct_ptr, ".xxx_hidden_", field->GoName, " = ", amp, "v");
				/* When setting a message to nil, clear the presence bit, else we would
				 * later think that this field still needs to be lazily decoded. */
				P(g, "if v == nil {");
				P(g, X_IDENT, ".ClearPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ")");
				P(g, "} else {");
				P(g, X_IDENT, ".SetPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ",", opaque_num_presence_fields(message), ")");
				P(g, "}");
			} else {
				/* Any map or non-message, possibly repeated, field that uses presence
				 * (proto2 only) */
				P(g, struct_ptr, ".xxx_hidden_", field->GoName, " = ", amp, "v");
				/* Non-map repeated fields are cleared when the last object is removed;
				 * maps when set to a nil map. */
				if (field->Desc->cardinality == GO_REPEATED) { /* Includes maps. */
					P(g, "if v == nil {");
					P(g, X_IDENT, ".ClearPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ")");
					P(g, "} else {");
				}
				P(g, X_IDENT, ".SetPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ",", opaque_num_presence_fields(message), ")");
				if (field->Desc->cardinality == GO_REPEATED)
					P(g, "}");
			}
		} else {
			/* proto3 non-lazy fields */
			P(g, struct_ptr, ".xxx_hidden_", field->GoName, " = ", amp, "v");
		}
		P(g, "}");
		P(g, "");
	}

	/* If we need a backwards compatible setter name, we add it now (deferred upstream). */
	if (bc_name) {
		P(g, "// Deprecated: Use ", setter_name, " instead.");
		P(g, "func (x *", message->m->GoIdent, ") ", bc_name, "(v ", go_type, ") {");
		P(g, "x.", setter_name, "(v)");
		P(g, "}");
		P(g, "");
	}
}

/* opaqueGenHas generates a Has method for a field. */
static COLD void opaque_gen_has(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field)
{
	(void)f;
	const char *compat;
	const char *hasser_name = pg_field_method_name(field, "Has", &compat);

	PgComments leading = append_deprecation_suffix(pg_comments(""), field->Desc->d.file, field_deprecated(field));
	pg_annotate_symbol(g, xcat(message->m->GoIdent.GoName, ".", hasser_name), (PgAnnotation){field->Location, 0});
	field_track_no_interface(g, message->noInterface);

	PgOneof *oneof = field->Oneof;
	if (is_real_oneof(oneof)) {
		/* Oneof field. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", hasser_name, "() bool {");
		const char *struct_ptr = "x";
		P(g, "if ", struct_ptr, " == nil {");
		P(g, "return false");
		P(g, "}");
		if (is_opaque(message) && message->isTracked)
			P(g, "_ = ", struct_ptr, ".", "XXX_ft_", oneof->GoName);
		P(g, "_, ok := ", struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), ".(*",
		  opaque_field_oneof_type(field, is_opaque(message)), ")");
		P(g, "return ok");
		P(g, "}");
		P(g, "");
		return;
	}

	if (!is_opaque(message)) {
		/* Non-oneof field in open message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", hasser_name, "() bool {");
		P(g, "if x == nil {");
		P(g, "return false");
		P(g, "}");
		P(g, "return ", "x.", field->GoName, " != nil");
		P(g, "}");
		P(g, "");
		return;
	}

	/* Non-oneof field in opaque message. */
	P(g, leading, "func (x *", message->m->GoIdent, ") ", hasser_name, "() bool {");
	P(g, "if x == nil {");
	P(g, "return false");
	P(g, "}");
	const char *struct_ptr = "x";
	if (message->isTracked)
		P(g, "_ = ", struct_ptr, ".", xcat("XXX_ft_", field->GoName));
	if (use_presence(message, field)) {
		int pi = opaque_field_presence_index(field);
		int ai = pi / 32;
		P(g, "return ", X_IDENT, ".Present(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ")");
	} else {
		/* Has for proto3 message without presence */
		P(g, "return ", struct_ptr, ".xxx_hidden_", field->GoName, " != nil");
	}
	P(g, "}");
	P(g, "");
}

/* Determine what value to set a cleared field to. */
static COLD const char *opaque_zero_value_for_field(PgGeneratedFile *g, PgField *field)
{
	if (field->Desc->cardinality == GO_REPEATED)
		return "nil";
	switch (field->Desc->kind) {
	case PT_STRING: return "nil";
	case PT_MESSAGE: case PT_GROUP: case PT_BYTES: return "nil";
	case PT_BOOL: return "false";
	case PT_ENUM: return pg_qualified_go_ident(g, field->Enum->Values[0]->GoIdent);
	default: return "0";
	}
}

/* opaqueGenClear generates a Clear method for a field. */
static COLD void opaque_gen_clear(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field)
{
	const char *compat;
	const char *clearer_name = pg_field_method_name(field, "Clear", &compat);
	int pi = opaque_field_presence_index(field);
	int ai = pi / 32;

	PgComments leading = append_deprecation_suffix(pg_comments(""), field->Desc->d.file, field_deprecated(field));
	pg_annotate_symbol(g, xcat(message->m->GoIdent.GoName, ".", clearer_name),
			   (PgAnnotation){field->Location, GENID_GeneratedCodeInfo_Annotation_SET_enum_value + 1});
	field_track_no_interface(g, message->noInterface);

	PgOneof *oneof = field->Oneof;
	if (is_real_oneof(oneof)) {
		/* Oneof field. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", clearer_name, "() {");
		const char *struct_ptr = "x";
		if (is_opaque(message) && message->isTracked)
			P(g, struct_ptr, ".", "XXX_ft_", oneof->GoName, " = struct{}{}");
		P(g, "if _, ok := ", struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), ".(*",
		  opaque_field_oneof_type(field, is_opaque(message)), "); ok {");
		P(g, struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), " = nil");
		P(g, "}");
		P(g, "}");
		P(g, "");
		return;
	}

	if (!is_opaque(message)) {
		/* Non-oneof field in open message. */
		P(g, leading, "func (x *", message->m->GoIdent, ") ", clearer_name, "() {");
		P(g, "x.", field->GoName, " = nil");
		P(g, "}");
		P(g, "");
		return;
	}

	/* Non-oneof field in opaque message. */
	P(g, leading, "func (x *", message->m->GoIdent, ") ", clearer_name, "() {");
	const char *struct_ptr = "x";
	if (message->isTracked)
		P(g, struct_ptr, ".", "XXX_ft_", field->GoName, " = struct{}{}");
	if (use_presence(message, field))
		P(g, X_IDENT, ".ClearPresent(&(", struct_ptr, ".XXX_presence[", ai, "]),", pi, ")");

	/* Set the right zero value (unless there is an explicit default, in which case Get checks
	 * presence). Lazy (repeated) message fields are assigned atomically. */
	const char *zv = opaque_zero_value_for_field(g, field);
	if (is_lazy(field)) {
		bool p;
		const char *go_type = opaque_field_go_type(g, f, message, field, &p);
		P(g, X_IDENT, ".AtomicSetPointer(&", struct_ptr, ".xxx_hidden_", field->GoName, ",(", go_type, ")(", zv, "))");
	} else if (!field->Desc->has_default) {
		P(g, struct_ptr, ".xxx_hidden_", field->GoName, " = ", zv);
	}
	P(g, "}");
	P(g, "");
}

/* opaqueGenGetOneof generates a Get function for a oneof union. */
static COLD void opaque_gen_get_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof)
{
	(void)f;
	const char *if_name = opaque_oneof_interface_name(oneof);
	pg_annotate_symbol(g, xcat(message->m->GoIdent.GoName, ".Get", oneof->GoName), (PgAnnotation){oneof->Location, 0});
	field_track_no_interface(g, message->isTracked);
	P(g, "func (x *", message->m->GoIdent.GoName, ") Get", oneof->GoName, "() ", if_name, " {");
	P(g, "if x != nil {");
	P(g, "return x.", opaque_oneof_field_name(oneof, is_opaque(message)));
	P(g, "}");
	P(g, "return nil");
	P(g, "}");
	P(g, "");
}

/* opaqueGenHasOneof generates a Has function for a oneof union. */
static COLD void opaque_gen_has_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof)
{
	(void)f;
	field_track_no_interface(g, message->noInterface);
	const char *hasser_name = pg_oneof_method_name(oneof, "Has");
	P(g, "func (x *", message->m->GoIdent, ") ", hasser_name, "() bool {");
	P(g, "if x == nil {");
	P(g, "return false");
	P(g, "}");
	const char *struct_ptr = "x";
	if (is_opaque(message) && message->isTracked)
		P(g, "_ = ", struct_ptr, ".XXX_ft_", oneof->GoName);
	P(g, "return ", struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), " != nil");
	P(g, "}");
	P(g, "");
}

/* opaqueGenClearOneof generates a Clear function for a oneof union. */
static COLD void opaque_gen_clear_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgOneof *oneof)
{
	(void)f;
	field_track_no_interface(g, message->noInterface);
	const char *clearer_name = pg_oneof_method_name(oneof, "Clear");
	P(g, "func (x *", message->m->GoIdent, ") ", clearer_name, "() {");
	const char *struct_ptr = "x";
	if (is_opaque(message) && message->isTracked)
		P(g, struct_ptr, ".", "XXX_ft_", oneof->GoName, " = struct{}{}");
	P(g, struct_ptr, ".", opaque_oneof_field_name(oneof, is_opaque(message)), " = nil");
	P(g, "}");
	P(g, "");
}

/* opaqueGenWhichOneof generates the Which method for each oneof union, as well as the case
 * values for each member of that union. */
static COLD void opaque_gen_which_oneof(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	(void)f;
	PgMessage *m = message->m;
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		PgOneof *oneof = field->Oneof;
		if (!oneof)
			continue;
		if (!is_first_oneof_field(field))
			continue;
		const char *case_type = opaque_oneof_case_type_name(oneof);
		P(g, "const ", m->GoIdent.GoName, "_", oneof->GoName, "_not_set_case ", case_type, " = ", 0);
		for (int k = 0; k < oneof->nFields; k++) {
			PgField *of = oneof->Fields[k];
			pg_annotate_symbol(g, xcat(m->GoIdent.GoName, "_", of->GoName, "_case"), (PgAnnotation){of->Location, 0});
			P(g, "const ", m->GoIdent.GoName, "_", of->GoName, "_case ", case_type, " = ", of->Desc->number);
		}
		field_track_no_interface(g, message->noInterface);
		const char *whicher_name = pg_oneof_method_name(oneof, "Which");
		pg_annotate_symbol(g, xcat(m->GoIdent.GoName, ".", whicher_name), (PgAnnotation){oneof->Location, 0});
		P(g, "func (x *", m->GoIdent, ") ", whicher_name, "() ", case_type, " {");
		P(g, "if x == nil {");
		P(g, "return ", m->GoIdent.GoName, "_", oneof->GoName, "_not_set_case ");
		P(g, "}");
		P(g, "switch x.", opaque_oneof_field_name(oneof, is_opaque(message)), ".(type) {");
		for (int k = 0; k < oneof->nFields; k++) {
			PgField *of = oneof->Fields[k];
			P(g, "case *", opaque_field_oneof_type(of, is_opaque(message)), ":");
			P(g, "return ", m->GoIdent.GoName, "_", of->GoName, "_case");
		}
		P(g, "default", ":");
		P(g, "return ", m->GoIdent.GoName, "_", oneof->GoName, "_not_set_case ");
		P(g, "}");
		P(g, "}");
		P(g, "");
	}
}

static COLD bool opaque_needs_presence_array(const MessageInfo *message)
{
	if (!is_opaque(message))
		return false;
	for (int i = 0; i < message->m->nFields; i++) {
		const GoField *fd = message->m->Fields[i]->Desc;
		bool can_be_lazy;
		if (features_use_presence_for_field(fd->oneof && !go_oneof_is_synthetic(fd->oneof), go_field_is_map(fd),
						    fd->kind == PT_MESSAGE || fd->kind == PT_GROUP, fd->is_lazy,
						    go_field_has_presence(fd), &can_be_lazy))
			return true;
	}
	return false;
}

static COLD bool opaque_needs_lazy_struct(const MessageInfo *message)
{
	for (int i = 0; i < message->m->nFields; i++)
		if (is_lazy(message->m->Fields[i]))
			return true;
	return false;
}

/* opaqueBuilderFieldGoType does the same as opaqueFieldGoType, but corrects for types that are
 * different in a builder. */
static COLD const char *opaque_builder_field_go_type(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field, bool *pointer)
{
	const char *go_type = opaque_field_go_type(g, f, message, field, pointer);
	int kind = field->Desc->kind;
	/* Use []T instead of *[]T for opaque repeated lists. */
	if (is_opaque(message) && go_field_is_list(field->Desc))
		*pointer = false;
	/* Use *T for optional fields. */
	bool optional = go_field_has_presence(field->Desc);
	if (optional && kind != PT_GROUP && kind != PT_MESSAGE && kind != PT_BYTES && field->Desc->cardinality != GO_REPEATED)
		*pointer = true;
	return go_type;
}

static void opaque_gen_build_method(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, const char *b_name);

/* opaqueGenMessageBuilder generates a Builder type for a message. */
static COLD void opaque_gen_message_builder(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	if (is_open(message))
		return;
	PgMessage *m = message->m;
	/* Builder type. */
	const char *b_name = xcat(pg_qualified_go_ident(g, m->GoIdent), GENID_BuilderSuffix_goname);
	pg_annotate_symbol(g, xcat(m->GoIdent.GoName, GENID_BuilderSuffix_goname), (PgAnnotation){m->Location, 0});

	PgComments leading = append_deprecation_suffix(pg_comments(""), m->Desc->d.file,
						       pb_int(m->Desc->d.options, GENID_MessageOptions_Deprecated_field_number, 0) != 0);
	P(g, leading, "type ", b_name, " struct {");
	P(g, "_ [0]func() // Prevents comparability and use of unkeyed literals for the builder.");
	P(g, "");
	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		PgOneof *oneof = field->Oneof;
		bool pointer;
		const char *go_type = opaque_builder_field_go_type(g, f, message, field, &pointer);
		if (pointer)
			go_type = xcat("*", go_type);
		else if (oneof && !str_eq(field_default_value(g, f, message, field), "nil"))
			go_type = xcat("*", go_type);
		/* Track all non-oneof fields. Synthetic oneofs are an implementation detail of
		 * proto3 optional fields, which should be tracked. */
		const char *tag = "";
		if ((!oneof || go_oneof_is_synthetic(oneof->Desc)) && message->isTracked)
			tag = "`go:\"track\"`";
		if (oneof && oneof->Fields[0] == field && !go_oneof_is_synthetic(oneof->Desc)) {
			if (oneof->Comments.Leading.s.n) {
				P(g, oneof->Comments.Leading);
				P(g, "");
			}
			P(g, "// Fields of oneof ", opaque_oneof_field_name(oneof, is_opaque(message)), ":");
		}
		pg_annotate_symbol(g, xcat(field->Parent->GoIdent.GoName, GENID_BuilderSuffix_goname, ".", pg_field_builder_field_name(field)),
				   (PgAnnotation){field->Location, 0});
		PgComments fl = append_deprecation_suffix(field->Comments.Leading, field->Desc->d.file, field_deprecated(field));
		P(g, fl, pg_field_builder_field_name(field), " ", go_type, " ", tag);
		if (oneof && oneof->Fields[oneof->nFields - 1] == field && !go_oneof_is_synthetic(oneof->Desc))
			P(g, "// -- end of ", opaque_oneof_field_name(oneof, is_opaque(message)));
	}
	P(g, "}");
	P(g, "");

	opaque_gen_build_method(g, f, message, b_name);
}

/* opaqueGenBuildMethod generates the actual Build method for the builder */
static COLD void opaque_gen_build_method(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, const char *b_name)
{
	PgMessage *m = message->m;
	/* Build method on the builder type. */
	field_track_no_interface(g, message->noInterface);
	P(g, "func (b0 ", b_name, ") Build() *", m->GoIdent, " {");
	P(g, "m0 := &", m->GoIdent, "{}");

	if (message->isTracked) {
		/* Redeclare the builder and message types as local defined types, so that field
		 * tracking records the field uses against these types. */
		P(g, "type (notrackB ", b_name, "; notrackM ", m->GoIdent, ")");
		P(g, "b, x := (*notrackB)(&b0), (*notrackM)(m0)");
	} else {
		P(g, "b, x := &b0, m0");
	}
	P(g, "_, _ = b, x");

	for (int i = 0; i < m->nFields; i++) {
		PgField *field = m->Fields[i];
		PgOneof *oneof = field->Oneof;
		if (is_real_oneof(oneof)) {
			const char *qual = !str_eq(field_default_value(g, f, message, field), "nil") ? "*" : "";
			P(g, "if b.", pg_field_builder_field_name(field), " != nil {");
			const char *oneof_name = opaque_oneof_field_name(oneof, is_opaque(message));
			PgGoIdent oneof_type = opaque_field_oneof_type(field, is_opaque(message));
			P(g, "x.", oneof_name, " = &", oneof_type, "{", qual, "b.", pg_field_builder_field_name(field), "}");
			P(g, "}");
		} else { /* proto3 optional ends up here (synthetic oneof) */
			const char *qual = "";
			bool pointer;
			opaque_builder_field_go_type(g, f, message, field, &pointer);
			if (pointer && is_opaque(message) && !go_field_is_list(field->Desc) && field->Desc->kind != PT_STRING)
				qual = "*";
			else if (is_opaque(message) && go_field_is_list(field->Desc) && field->Desc->message_type)
				qual = "&";
			bool presence = use_presence(message, field);
			if (presence)
				P(g, "if b.", pg_field_builder_field_name(field), " != nil {");
			if (presence) {
				int pi = opaque_field_presence_index(field);
				P(g, X_IDENT, ".SetPresentNonAtomic(&(x.XXX_presence[", pi / 32, "]),", pi, ",", opaque_num_presence_fields(message), ")");
			}
			const char *go_name = field->GoName;
			if (is_opaque(message))
				go_name = xcat("xxx_hidden_", go_name);
			P(g, "x.", go_name, " = ", qual, "b.", pg_field_builder_field_name(field));
			if (presence)
				P(g, "}");
		}
	}

	P(g, "return m0");
	P(g, "}");
	P(g, "");
}

static COLD void opaque_gen_oneof_wrapper_types(PgGeneratedFile *g, FileInfo *f, MessageInfo *message)
{
	PgMessage *m = message->m;
	/* TODO: We should avoid generating these wrapper types in pure-opaque mode. */
	if (!is_open(message)) {
		for (int i = 0; i < m->nOneofs; i++) {
			PgOneof *oneof = m->Oneofs[i];
			if (go_oneof_is_synthetic(oneof->Desc))
				continue;
			const char *case_type_name = opaque_oneof_case_type_name(oneof);
			P(g, "type ", case_type_name, " ", ID(protoreflectPackage, "FieldNumber"));
			P(g, "");

			P(g, "func (x ", case_type_name, ") String() string {");
			P(g, "md := ", message_types_var_name(f), "[", message->index, "].Descriptor()");
			P(g, "if x == 0 {");
			P(g, "return \"not set\"");
			P(g, "}");
			P(g, "return ", X_IDENT, ".MessageFieldStringOf(md, ", ID(protoreflectPackage, "FieldNumber"), "(x))");
			P(g, "}");
			P(g, "");
		}
	}
	for (int i = 0; i < m->nOneofs; i++) {
		PgOneof *oneof = m->Oneofs[i];
		if (go_oneof_is_synthetic(oneof->Desc))
			continue;
		const char *if_name = opaque_oneof_interface_name(oneof);
		P(g, "type ", if_name, " interface {");
		P(g, if_name, "()");
		P(g, "}");
		P(g, "");
		for (int k = 0; k < oneof->nFields; k++) {
			PgField *field = oneof->Fields[k];
			PgGoIdent name = opaque_field_oneof_type(field, is_opaque(message));
			pg_annotate_symbol(g, name.GoName, (PgAnnotation){field->Location, 0});
			pg_annotate_symbol(g, xcat(name.GoName, ".", field->GoName), (PgAnnotation){field->Location, 0});
			P(g, "type ", name, " struct {");
			bool p;
			const char *go_type = opaque_field_go_type(g, f, message, field, &p);
			Str protobuf_tag = field_protobuf_tag_value(field);
			if (pg_generated_file_internal_strip_for_editions_diff(g))
				protobuf_tag = replace_all(protobuf_tag, ",proto3", "");
			StructTag tagv = {"protobuf", protobuf_tag};
			PgComments leading = append_deprecation_suffix(field->Comments.Leading, field->Desc->d.file, field_deprecated(field));
			P(g, leading, field->GoName, " ", go_type, struct_tags(&tagv, 1), trailing_comment(field->Comments.Trailing));
			P(g, "}");
			P(g, "");
		}
		for (int k = 0; k < oneof->nFields; k++) {
			P(g, "func (*", opaque_field_oneof_type(oneof->Fields[k], is_opaque(message)), ") ", if_name, "() {}");
			P(g, "");
		}
	}
}

/* opaqueFieldGoType returns the Go type used for a field; *pointer: the struct field is a
 * pointer to the type. */
static COLD const char *opaque_field_go_type(PgGeneratedFile *g, FileInfo *f, MessageInfo *message, PgField *field, bool *pointer)
{
	const char *go_type = "";
	bool ptr = true;
	switch (field->Desc->kind) {
	case PT_BOOL: go_type = "bool"; break;
	case PT_ENUM: go_type = pg_qualified_go_ident(g, field->Enum->GoIdent); break;
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: go_type = "int32"; break;
	case PT_UINT32: case PT_FIXED32: go_type = "uint32"; break;
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: go_type = "int64"; break;
	case PT_UINT64: case PT_FIXED64: go_type = "uint64"; break;
	case PT_FLOAT: go_type = "float32"; break;
	case PT_DOUBLE: go_type = "float64"; break;
	case PT_STRING: go_type = "string"; break;
	case PT_BYTES:
		go_type = "[]byte";
		ptr = false;
		break;
	case PT_MESSAGE:
	case PT_GROUP:
		go_type = xcat("*", pg_qualified_go_ident(g, field->Message->GoIdent)); /* opaqueMessageFieldGoType */
		ptr = false;
		break;
	}
	if (go_field_is_list(field->Desc)) {
		go_type = xcat("[]", go_type);
		ptr = false;
	} else if (go_field_is_map(field->Desc)) {
		bool p;
		const char *key = opaque_field_go_type(g, f, message, field->Message->Fields[0], &p);
		const char *val = opaque_field_go_type(g, f, message, field->Message->Fields[1], &p);
		*pointer = false;
		return xcat("map[", key, "]", val);
	}

	/* Extension fields always have pointer type, even when defined in a proto3 file. */
	if (!field->Desc->is_extension && !go_field_has_presence(field->Desc))
		ptr = false;

	if (is_opaque(message)) {
		if (go_field_is_list(field->Desc) && field->Desc->message_type)
			ptr = true;
		else if (!go_field_is_list(field->Desc) && field->Desc->kind == PT_STRING)
			ptr = go_field_has_presence(field->Desc);
		else
			ptr = false;
	}
	*pointer = ptr;
	return go_type;
}

/* opaqueFieldPresenceIndex returns the index to pass to presence functions. */
static COLD int opaque_field_presence_index(const PgField *field)
{
	int idx = 0;
	for (int i = 0; i < field->Parent->nFields; i++) {
		const PgField *f = field->Parent->Fields[i];
		if (field == f)
			break;
		if (!f->Oneof || is_last_oneof_field(f))
			idx++;
	}
	return idx;
}

/* opaqueNumPresenceFields returns the number of fields that may be passed to presence
 * functions (all fields in a oneof share a single entry in the presence bitmap). */
static COLD int opaque_num_presence_fields(const MessageInfo *message)
{
	if (message->m->nFields == 0)
		return 0;
	return opaque_field_presence_index(message->m->Fields[message->m->nFields - 1]) + 1;
}

/* opaqueOneofFieldName returns the name of the struct field that holds the value of a oneof. */
static COLD const char *opaque_oneof_field_name(const PgOneof *oneof, bool is_opaque_)
{
	if (is_opaque_)
		return xcat("xxx_hidden_", oneof->GoName);
	return oneof->GoName;
}

static COLD PgGoIdent opaque_field_oneof_type(const PgField *field, bool is_opaque_)
{
	PgGoIdent ident = pg_ident(field->Parent->GoIdent.GoImportPath, xcat(field->Parent->GoIdent.GoName, "_", field->GoName));
	/* Check for collisions with nested messages or enums (incomplete: other oneof field types
	 * are not considered). */
	for (bool again = true; again;) {
		again = false;
		for (int i = 0; i < field->Parent->nMessages && !again; i++)
			if (pg_ident_eq(field->Parent->Messages[i]->GoIdent, ident))
				ident.GoName = xcat(ident.GoName, "_"), again = true;
		for (int i = 0; i < field->Parent->nEnums && !again; i++)
			if (pg_ident_eq(field->Parent->Enums[i]->GoIdent, ident))
				ident.GoName = xcat(ident.GoName, "_"), again = true;
	}
	return unexport_ident(ident, is_opaque_);
}

static COLD const char *opaque_oneof_interface_name(const PgOneof *oneof)
{
	return xcat("is", oneof->Parent->GoIdent.GoName, "_", oneof->GoName);
}

static COLD const char *opaque_oneof_case_type_name(const PgOneof *oneof)
{
	return xcat("case_", oneof->Parent->GoIdent.GoName, "_", oneof->GoName);
}
