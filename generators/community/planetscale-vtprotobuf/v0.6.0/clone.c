/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Copyright (c) 2013, The GoGo Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/clone/clone.go (vtprotobuf v0.6.0). */

#define CLONE_NAME "CloneVT"
#define CLONE_MESSAGE_NAME "CloneMessageVT"

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool once;
} Clone;

/* isReference checks whether the Go equivalent of the given field is of reference type, i.e.,
 * can be nil. */
static bool clone_is_reference(bool all_fields_nullable, const PgField *field)
{
	if (all_fields_nullable || field->Oneof || field->Message || is_repeated(field) || field->Desc->kind == PT_BYTES)
		return true;
	if (!is_scalar(field->Desc->kind))
		fail("unexpected non-reference, non-scalar field");
	return false;
}

/* cloneOneofField generates the statements for cloning a oneof field */
static void clone_oneof_field(Clone *p, const char *lhs_base, const char *rhs_base, const PgOneof *oneof)
{
	const char *fieldname = oneof->GoName;
	const char *cc_interface_name = xcat("is", oneof->GoIdent.GoName);
	const char *lhs = xcat(lhs_base, ".", fieldname);
	const char *rhs = xcat(rhs_base, ".", fieldname);
	P(p->g, "if ", rhs, " != nil {");
	if (vt_is_well_known_type(oneof->Parent)) {
		P(p->g, "switch c := ", rhs, ".(type) {");
		for (int i = 0; i < oneof->nFields; i++) {
			const PgField *f = oneof->Fields[i];
			P(p->g, "case *", f->GoIdent, ":");
			PgGoIdent wkt = vt_well_known_field_map(p->f, f);
			P(p->g, lhs, "= (*", f->GoIdent, ")((*", wkt, ")(c).", CLONE_NAME, "())");
		}
		P(p->g, "}");
	} else {
		P(p->g, lhs, " = ", rhs, ".(interface{ ", CLONE_NAME, "() ", cc_interface_name, " }).", CLONE_NAME, "()");
	}
	P(p->g, "}");
}

/* cloneFieldSingular generates the code for cloning a singular, non-oneof field. */
static void clone_field_singular(Clone *p, const char *lhs, const char *rhs, int kind, const PgMessage *message)
{
	if (kind == PT_MESSAGE || kind == PT_GROUP) {
		if (vt_is_well_known_type(message)) {
			PgGoIdent wkt = vt_well_known_type_map(p->f, message);
			P(p->g, lhs, " = (*", message->GoIdent, ")((*", wkt, ")(", rhs, ").", CLONE_NAME, "())");
		} else if (vt_is_local_message(p->f, message)) {
			P(p->g, lhs, " = ", rhs, ".", CLONE_NAME, "()");
		} else {
			/* rhs is a concrete type, we need to first convert it to an interface in order
			 * to use an interface type assertion. */
			P(p->g, "if vtpb, ok := interface{}(", rhs, ").(interface{ ", CLONE_NAME, "() *", message->GoIdent, " }); ok {");
			P(p->g, lhs, " = vtpb.", CLONE_NAME, "()");
			P(p->g, "} else {");
			P(p->g, lhs, " = ", pg_ident(PROTO_PKG, "Clone"), "(", rhs, ").(*", message->GoIdent, ")");
			P(p->g, "}");
		}
	} else if (kind == PT_BYTES) {
		P(p->g, "tmpBytes := make([]byte, len(", rhs, "))");
		P(p->g, "copy(tmpBytes, ", rhs, ")");
		P(p->g, lhs, " = tmpBytes");
	} else if (is_scalar(kind)) {
		P(p->g, lhs, " = ", rhs);
	} else {
		fail("unexpected");
	}
}

/* cloneField generates the code for cloning a field in a protobuf. */
static void clone_field(Clone *p, const char *lhs_base, const char *rhs_base, bool all_fields_nullable, const PgField *field)
{
	/* At this point, if we encounter a non-synthetic oneof, we assume it to be the
	 * representative field for that oneof. */
	if (is_real_oneof(field)) {
		clone_oneof_field(p, lhs_base, rhs_base, field->Oneof);
		return;
	}
	if (!clone_is_reference(all_fields_nullable, field))
		fail("method should not be invoked for non-reference fields");

	const char *fieldname = field->GoName;
	const char *lhs = xcat(lhs_base, ".", fieldname);
	const char *rhs = xcat(rhs_base, ".", fieldname);

	/* At this point, we are only looking at reference types (pointers, maps, slices,
	 * interfaces), which can all be nil. */
	P(p->g, "if rhs := ", rhs, "; rhs != nil {");
	rhs = "rhs";

	int field_kind = field->Desc->kind;
	const PgMessage *msg = field->Message; /* possibly nil */

	if (is_repeated(field)) { /* maps and slices */
		const char *go_type = vt_field_go_type(p->f, field, NULL);
		P(p->g, "tmpContainer := make(", go_type, ", len(", rhs, "))");
		if (is_scalar(field_kind) && go_field_is_list(field->Desc)) {
			/* Generated code optimization: instead of iterating over all (key/index, value)
			 * pairs, do a single copy(dst, src) invocation for slices whose elements aren't
			 * reference types. */
			P(p->g, "copy(tmpContainer, ", rhs, ")");
		} else {
			if (go_field_is_map(field->Desc)) {
				/* For maps, the type of the value field determines what code is
				 * generated for cloning an entry. */
				const PgField *value_field = field->Message->Fields[1];
				field_kind = value_field->Desc->kind;
				msg = value_field->Message;
			}
			P(p->g, "for k, v := range ", rhs, " {");
			clone_field_singular(p, "tmpContainer[k]", "v", field_kind, msg);
			P(p->g, "}");
		}
		P(p->g, lhs, " = tmpContainer");
	} else if (is_scalar(field_kind)) {
		P(p->g, "tmpVal := *", rhs);
		P(p->g, lhs, " = &tmpVal");
	} else {
		clone_field_singular(p, lhs, rhs, field_kind, msg);
	}
	P(p->g, "}");
}

/* body generates the code for the actual cloning logic of a structure containing the given
 * fields. The object to be cloned is assumed to be called "m". */
static void clone_body(Clone *p, bool all_fields_nullable, const char *cc_type_name, const PgMessage *message, bool clone_unknown_fields)
{
	/* The method body for a message or a oneof wrapper always starts with a nil check. We use
	 * an explicitly typed nil to avoid returning the nil interface in the oneof wrapper case. */
	P(p->g, "if m == nil {");
	P(p->g, "return (*", cc_type_name, ")(nil)");
	P(p->g, "}");

	/* Make a first pass over the fields, in which we initialize all non-reference fields via
	 * direct struct literal initialization, and extract all other (reference) fields for a
	 * second pass. Do not require qualified name because CloneVT generates in same file with
	 * definition. */
	vt_alloc(p->f, "r", message, false);
	Vec ref_fields = {0};
	Map oneof_fields = {0};
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (is_real_oneof(field)) {
			/* Use the first field in a oneof as the representative for that oneof,
			 * disregard the other fields in that oneof. */
			if (!map_get(&oneof_fields, field->Oneof->GoName)) {
				vec_push(&ref_fields, (void *)field);
				map_put(&oneof_fields, field->Oneof->GoName, MAP_TRUE);
			}
			continue;
		}
		if (!clone_is_reference(all_fields_nullable, field)) {
			P(p->g, "r.", field->GoName, " = m.", field->GoName);
			continue;
		}
		/* Shortcut: for types where we know that an optimized clone method exists, we can
		 * call it directly as it is nil-safe. */
		if (!is_repeated(field)) {
			if (vt_is_well_known_type(field->Message)) {
				PgGoIdent wkt = vt_well_known_type_map(p->f, field->Message);
				P(p->g, "r.", field->GoName, " = (*", field->Message->GoIdent, ")((*", wkt, ")(m.", field->GoName, ").", CLONE_NAME,
				  "())");
				continue;
			}
			if (vt_is_local_message(p->f, field->Message)) {
				P(p->g, "r.", field->GoName, " = m.", field->GoName, ".", CLONE_NAME, "()");
				continue;
			}
		}
		vec_push(&ref_fields, (void *)field);
	}

	/* Generate explicit assignment statements for all reference fields. */
	for (int i = 0; i < ref_fields.n; i++)
		clone_field(p, "r", "m", all_fields_nullable, ref_fields.v[i]);

	if (clone_unknown_fields && !vt_wrapper(p->f)) {
		/* Clone unknown fields, if any */
		P(p->g, "if len(m.unknownFields) > 0 {");
		P(p->g, "r.unknownFields = make([]byte, len(m.unknownFields))");
		P(p->g, "copy(r.unknownFields, m.unknownFields)");
		P(p->g, "}");
	}
	P(p->g, "return r");
}

static void clone_generate_methods_for_message(Clone *p, bool proto3, const PgMessage *message)
{
	const char *cc_type_name = message->GoIdent.GoName;
	P(p->g, "func (m *", cc_type_name, ") ", CLONE_NAME, "() *", cc_type_name, " {");
	clone_body(p, !proto3, cc_type_name, message, true);
	P(p->g, "}");
	P(p->g, "");

	if (!vt_wrapper(p->f)) {
		P(p->g, "func (m *", cc_type_name, ") ", CLONE_MESSAGE_NAME, "() ", pg_ident(PROTO_PKG, "Message"), " {");
		P(p->g, "return m.", CLONE_NAME, "()");
		P(p->g, "}");
		P(p->g, "");
	}
}

static void clone_body_for_oneof(Clone *p, const char *cc_type_name, const PgField *field)
{
	/* The method body for a message or a oneof wrapper always starts with a nil check. */
	P(p->g, "if m == nil {");
	P(p->g, "return (*", cc_type_name, ")(nil)");
	P(p->g, "}");

	P(p->g, "r", " := new(", cc_type_name, ")");

	if (!clone_is_reference(false, field)) {
		P(p->g, "r.", field->GoName, " = m.", field->GoName);
		P(p->g, "return r");
		return;
	}
	/* Shortcut: for types where we know that an optimized clone method exists, we can call it
	 * directly as it is nil-safe. */
	if (!is_repeated(field) && field->Message) {
		if (vt_is_well_known_type(field->Message)) {
			PgGoIdent wkt = vt_well_known_type_map(p->f, field->Message);
			P(p->g, "r.", field->GoName, " = (*", field->Message->GoIdent, ")((*", wkt, ")(m.", field->GoName, ").", CLONE_NAME, "())");
			P(p->g, "return r");
			return;
		}
		if (vt_is_local_message(p->f, field->Message)) {
			P(p->g, "r.", field->GoName, " = m.", field->GoName, ".", CLONE_NAME, "()");
			P(p->g, "return r");
			return;
		}
	}

	/* Generate explicit assignment statements for reference field. */
	clone_field(p, "r", "m", false, field);
	P(p->g, "return r");
}

/* generateCloneMethodsForOneof generates the clone method for the oneof wrapper type of a field
 * in a oneof. */
static void clone_generate_methods_for_oneof(Clone *p, const PgMessage *message, const PgField *field)
{
	const char *cc_type_name = field->GoIdent.GoName;
	const char *cc_interface_name = xcat("is", field->Oneof->GoIdent.GoName);
	if (vt_is_well_known_type(message))
		P(p->g, "func (m *", cc_type_name, ") ", CLONE_NAME, "() *", cc_type_name, " {");
	else
		P(p->g, "func (m *", cc_type_name, ") ", CLONE_NAME, "() ", cc_interface_name, " {");

	/* Create a "fake" field for the single oneof member, pretending it is not a oneof field. If
	 * we have a scalar field in a oneof, that field is never nullable, even when using
	 * proto2. */
	PgField field_in_oneof = *field;
	field_in_oneof.Oneof = NULL;
	clone_body_for_oneof(p, cc_type_name, &field_in_oneof);
	P(p->g, "}");
	P(p->g, "");
}

static void clone_process_message(Clone *p, bool proto3, const PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		clone_process_message(p, proto3, message->Messages[i]);
	if (message->Desc->is_map_entry)
		return;
	p->once = true;
	clone_generate_methods_for_message(p, proto3, message);
	/* processMessageOneofs */
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (!is_real_oneof(field))
			continue;
		clone_generate_methods_for_oneof(p, message, field);
	}
}

static bool clone_generate_file(VtFile *gen, PgFile *file)
{
	Clone p = {gen, gen->g, false};
	bool proto3 = file->Desc->syntax == GO_PROTO3;
	for (int i = 0; i < file->nMessages; i++)
		clone_process_message(&p, proto3, file->Messages[i]);
	return p.once;
}
