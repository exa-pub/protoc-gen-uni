/* Copyright (c) 2022 PlanetScale Inc. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/equal/equal.go (vtprotobuf v0.6.0). */

#define EQUAL_NAME "EqualVT"
#define EQUAL_MESSAGE_NAME "EqualMessageVT"

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool once;
} Equal;

static void equal_compare_scalar(Equal *p, const char *lhs, const char *rhs, bool nullable)
{
	if (nullable)
		P(p->g, "if p, q := ", lhs, ", ", rhs, "; (p == nil && q != nil) || (p != nil && (q == nil || *p != *q)) {");
	else
		P(p->g, "if ", lhs, " != ", rhs, " {");
	P(p->g, "\treturn false");
	P(p->g, "}");
}

static void equal_compare_bytes(Equal *p, const char *lhs, const char *rhs, bool nullable)
{
	if (nullable) {
		P(p->g, "if p, q := ", lhs, ", ", rhs, "; (p == nil && q != nil) || (p != nil && q == nil) || string(p) != string(q) {");
	} else {
		/* Inlined call to bytes.Equal() */
		P(p->g, "if string(", lhs, ") != string(", rhs, ") {");
	}
	P(p->g, "\treturn false");
	P(p->g, "}");
}

static void equal_compare_call(Equal *p, const char *lhs, const char *rhs, const PgMessage *msg, bool nullable)
{
	if (!nullable) {
		/* The p != q check is mostly intended to catch the lhs = nil, rhs = nil case in which
		 * we would pointlessly allocate not just one but two empty values. However, it also
		 * provides us with an extra scope to establish our p and q variables. */
		P(p->g, "if p, q := ", lhs, ", ", rhs, "; p != q {");

		P(p->g, "if p == nil {");
		const char *t = pg_qualified_go_ident(p->g, msg->GoIdent);
		P(p->g, "p = &", t, "{}");
		P(p->g, "}");
		P(p->g, "if q == nil {");
		t = pg_qualified_go_ident(p->g, msg->GoIdent);
		P(p->g, "q = &", t, "{}");
		P(p->g, "}");
		lhs = "p", rhs = "q";
	}
	if (vt_is_well_known_type(msg)) {
		PgGoIdent wkt = vt_well_known_type_map(p->f, msg);
		P(p->g, "if !(*", wkt, ")(", lhs, ").", EQUAL_NAME, "((*", wkt, ")(", rhs, ")) {");
		P(p->g, "\treturn false");
		P(p->g, "}");
	} else if (vt_is_local_message(p->f, msg)) {
		P(p->g, "if !", lhs, ".", EQUAL_NAME, "(", rhs, ") {");
		P(p->g, "\treturn false");
		P(p->g, "}");
	} else {
		const char *t = pg_qualified_go_ident(p->g, msg->GoIdent);
		P(p->g, "if equal, ok := interface{}(", lhs, ").(interface { ", EQUAL_NAME, "(*", t, ") bool }); ok {");
		P(p->g, "\tif !equal.", EQUAL_NAME, "(", rhs, ") {");
		P(p->g, "\t\treturn false");
		P(p->g, "\t}");
		const char *eq = vt_ident(p->f, "google.golang.org/protobuf/proto", "Equal");
		P(p->g, "} else if !", eq, "(", lhs, ", ", rhs, ") {");
		P(p->g, "\treturn false");
		P(p->g, "}");
	}
	if (!nullable)
		P(p->g, "}"); /* defer p.P(`}`) */
}

static void equal_field(Equal *p, const PgField *field, bool nullable)
{
	const char *fieldname = field->GoName;
	bool repeated = is_repeated(field);
	const char *lhs = xcat("this.", fieldname);
	const char *rhs = xcat("that.", fieldname);

	if (repeated) {
		P(p->g, "if len(", lhs, ") != len(", rhs, ") {");
		P(p->g, "\treturn false");
		P(p->g, "}");
		P(p->g, "for i, vx := range ", lhs, " {");
		if (go_field_is_map(field->Desc)) {
			P(p->g, "vy, ok := ", rhs, "[i]");
			P(p->g, "if !ok {");
			P(p->g, "return false");
			P(p->g, "}");
			field = field->Message->Fields[1];
		} else {
			P(p->g, "vy := ", rhs, "[i]");
		}
		lhs = "vx", rhs = "vy";
		nullable = false;
	}

	int kind = field->Desc->kind;
	if (is_scalar(kind))
		equal_compare_scalar(p, lhs, rhs, nullable);
	else if (kind == PT_BYTES)
		equal_compare_bytes(p, lhs, rhs, nullable);
	else if (kind == PT_MESSAGE || kind == PT_GROUP)
		equal_compare_call(p, lhs, rhs, field->Message, nullable);
	else
		fail("not implemented");

	if (repeated)
		P(p->g, "}"); /* close for loop */
}

static void equal_oneof(Equal *p, const PgField *field)
{
	const char *cc_type_name = field->GoIdent.GoName;
	const char *cc_interface_name = xcat("is", field->Oneof->GoIdent.GoName);
	const char *fieldname = field->GoName;

	if (vt_is_well_known_type(field->Parent))
		P(p->g, "func (this *", cc_type_name, ") ", EQUAL_NAME, "(thatIface any) bool {");
	else
		P(p->g, "func (this *", cc_type_name, ") ", EQUAL_NAME, "(thatIface ", cc_interface_name, ") bool {");
	P(p->g, "that, ok := thatIface.(*", cc_type_name, ")");
	P(p->g, "if !ok {");
	if (vt_is_well_known_type(field->Parent)) {
		P(p->g, "if ot, ok := thatIface.(*", field->GoIdent, "); ok {");
		P(p->g, "that = (*", cc_type_name, ")(ot)");
		P(p->g, "} else {");
		P(p->g, "return false");
		P(p->g, "}");
	} else {
		P(p->g, "return false");
	}
	P(p->g, "}");
	P(p->g, "if this == that {");
	P(p->g, "return true");
	P(p->g, "}");
	P(p->g, "if this == nil && that != nil || this != nil && that == nil {");
	P(p->g, "return false");
	P(p->g, "}");

	const char *lhs = xcat("this.", fieldname);
	const char *rhs = xcat("that.", fieldname);
	int kind = field->Desc->kind;
	if (is_scalar(kind))
		equal_compare_scalar(p, lhs, rhs, false);
	else if (kind == PT_BYTES)
		equal_compare_bytes(p, lhs, rhs, false);
	else if (kind == PT_MESSAGE || kind == PT_GROUP)
		equal_compare_call(p, lhs, rhs, field->Message, false);
	else
		fail("not implemented");
	P(p->g, "return true");
	P(p->g, "}");
	P(p->g, "");
}

static void equal_message(Equal *p, bool proto3, PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		equal_message(p, proto3, message->Messages[i]);
	if (message->Desc->is_map_entry)
		return;
	p->once = true;

	const char *cc_type_name = message->GoIdent.GoName;
	P(p->g, "func (this *", cc_type_name, ") ", EQUAL_NAME, "(that *", cc_type_name, ") bool {");

	P(p->g, "if this == that {");
	P(p->g, "\treturn true");
	P(p->g, "} else if this == nil || that == nil {");
	P(p->g, "\treturn false");
	P(p->g, "}");

	sort_fields_by_number(message);

	{
		Map oneofs = {0};
		for (int i = 0; i < message->nFields; i++) {
			const PgField *field = message->Fields[i];
			if (!is_real_oneof(field))
				continue;
			const char *fieldname = field->Oneof->GoName;
			if (map_get(&oneofs, fieldname))
				continue;
			map_put(&oneofs, fieldname, MAP_TRUE);

			P(p->g, "if this.", fieldname, " == nil && that.", fieldname, " != nil {");
			P(p->g, "\treturn false");
			P(p->g, "} else if this.", fieldname, " != nil {");
			P(p->g, "\tif that.", fieldname, " == nil {");
			P(p->g, "\t\treturn false");
			P(p->g, "\t}");
			const char *cc_interface_name = xcat("is", field->Oneof->GoIdent.GoName);
			if (vt_is_well_known_type(message)) {
				P(p->g, "switch c := this.", fieldname, ".(type) {");
				for (int k = 0; k < field->Oneof->nFields; k++) {
					const PgField *f = field->Oneof->Fields[k];
					P(p->g, "case *", f->GoIdent, ":");
					PgGoIdent wkt = vt_well_known_field_map(p->f, f);
					P(p->g, "if !(*", wkt, ")(c).", EQUAL_NAME, "(that.", fieldname, ") {");
					P(p->g, "return false");
					P(p->g, "}");
				}
				P(p->g, "}");
			} else {
				P(p->g, "if !this.", fieldname, ".(interface{ ", EQUAL_NAME, "(", cc_interface_name, ") bool }).", EQUAL_NAME, "(that.",
				  fieldname, ") {");
				P(p->g, "return false");
				P(p->g, "}");
			}
			P(p->g, "}");
		}
	}

	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		bool oneof = is_real_oneof(field);
		bool nullable = field->Message || (field->Oneof && pg_v131_oneof_is_synthetic(field->Oneof->Desc)) || (!proto3 && !oneof);
		if (!oneof)
			equal_field(p, field, nullable);
	}

	if (vt_wrapper(p->f))
		P(p->g, "return true");
	else
		P(p->g, "return string(this.unknownFields) == string(that.unknownFields)");
	P(p->g, "}");
	P(p->g, "");

	if (!vt_wrapper(p->f)) {
		P(p->g, "func (this *", cc_type_name, ") ", EQUAL_MESSAGE_NAME, "(thatMsg ", pg_ident(PROTO_PKG, "Message"), ") bool {");
		P(p->g, "that, ok := thatMsg.(*", cc_type_name, ")");
		P(p->g, "if !ok {");
		P(p->g, "return false");
		P(p->g, "}");
		P(p->g, "return this.", EQUAL_NAME, "(that)");
		P(p->g, "}");
	}

	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (!is_real_oneof(field))
			continue;
		equal_oneof(p, field);
	}
}

static bool equal_generate_file(VtFile *gen, PgFile *file)
{
	Equal p = {gen, gen->g, false};
	bool proto3 = file->Desc->syntax == GO_PROTO3;
	for (int i = 0; i < file->nMessages; i++)
		equal_message(&p, proto3, file->Messages[i]);
	return p.once;
}
