/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Copyright (c) 2013, The GoGo Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/size/size.go (vtprotobuf v0.6.0). */

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool once;
} Size;

static void size_message_size(Size *p, const char *var_name, const char *size_name, const PgMessage *message)
{
	if (vt_is_well_known_type(message)) {
		P(p->g, "l = (*", vt_well_known_type_map(p->f, message), ")(", var_name, ").", size_name, "()");
	} else if (vt_is_local_message(p->f, message)) {
		P(p->g, "l = ", var_name, ".", size_name, "()");
	} else {
		P(p->g, "if size, ok := interface{}(", var_name, ").(interface{");
		P(p->g, size_name, "() int");
		P(p->g, "}); ok{");
		P(p->g, "l = size.", size_name, "()");
		P(p->g, "} else {");
		const char *size = vt_ident(p->f, PROTO_PKG, "Size");
		P(p->g, "l = ", size, "(", var_name, ")");
		P(p->g, "}");
	}
}

static const char *size_itoa(long long v) { return go_itoa(v); }

static void size_field(Size *p, bool oneof, const PgField *field, const char *size_name)
{
	const char *fieldname = field->GoName;
	bool nullable = field->Message || (!oneof && pg_v131_field_has_presence(field->Desc));
	bool repeated = is_repeated(field);
	if (repeated)
		P(p->g, "if len(m.", fieldname, ") > 0 {");
	else if (nullable)
		P(p->g, "if m.", fieldname, " != nil {");
	bool packed = pg_v131_field_is_packed(field->Desc);
	int wire_type = proto_wire_type(field->Desc->kind);
	int32_t field_number = field->Desc->number;
	if (packed)
		wire_type = WIRE_BYTES;
	int key = key_size(field_number, wire_type);
	PgGoIdent size_of_varint = vt_helper("SizeOfVarint"), size_of_zigzag = vt_helper("SizeOfZigzag");
	switch (field->Desc->kind) {
	case PT_DOUBLE:
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_FLOAT:
	case PT_FIXED32:
	case PT_SFIXED32: {
		int w = (field->Desc->kind == PT_DOUBLE || field->Desc->kind == PT_FIXED64 || field->Desc->kind == PT_SFIXED64) ? 8 : 4;
		const char *ws = w == 8 ? "8" : "4";
		if (packed) {
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(len(m.", fieldname, ")*", ws, "))", "+len(m.", fieldname, ")*", ws);
		} else if (repeated) {
			P(p->g, "n+=", size_itoa(key + w), "*len(m.", fieldname, ")");
		} else if (!oneof && !nullable) {
			P(p->g, "if m.", fieldname, " != 0 {");
			P(p->g, "n+=", size_itoa(key + w));
			P(p->g, "}");
		} else {
			P(p->g, "n+=", size_itoa(key + w));
		}
		break;
	}
	case PT_INT64:
	case PT_UINT64:
	case PT_UINT32:
	case PT_ENUM:
	case PT_INT32:
		if (packed) {
			P(p->g, "l = 0");
			P(p->g, "for _, e := range m.", fieldname, " {");
			P(p->g, "l+=", size_of_varint, "(uint64(e))");
			P(p->g, "}");
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(l))+l");
		} else if (repeated) {
			P(p->g, "for _, e := range m.", fieldname, " {");
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(e))");
			P(p->g, "}");
		} else if (nullable) {
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(*m.", fieldname, "))");
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(m.", fieldname, "))");
			P(p->g, "}");
		} else {
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(m.", fieldname, "))");
		}
		break;
	case PT_BOOL:
		if (packed) {
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(len(m.", fieldname, ")))", "+len(m.", fieldname, ")*1");
		} else if (repeated) {
			P(p->g, "n+=", size_itoa(key + 1), "*len(m.", fieldname, ")");
		} else if (!oneof && !nullable) {
			P(p->g, "if m.", fieldname, " {");
			P(p->g, "n+=", size_itoa(key + 1));
			P(p->g, "}");
		} else {
			P(p->g, "n+=", size_itoa(key + 1));
		}
		break;
	case PT_STRING:
		if (repeated) {
			P(p->g, "for _, s := range m.", fieldname, " { ");
			P(p->g, "l = len(s)");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
			P(p->g, "}");
		} else if (nullable) {
			P(p->g, "l=len(*m.", fieldname, ")");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
		} else if (!oneof) {
			P(p->g, "l=len(m.", fieldname, ")");
			P(p->g, "if l > 0 {");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
			P(p->g, "}");
		} else {
			P(p->g, "l=len(m.", fieldname, ")");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
		}
		break;
	case PT_GROUP:
		size_message_size(p, xcat("m.", fieldname), size_name, field->Message);
		P(p->g, "n+=l+", size_itoa(2 * key));
		break;
	case PT_MESSAGE:
		if (go_field_is_map(field->Desc)) {
			const PgField *kf = field->Message->Fields[0], *vf = field->Message->Fields[1];
			int field_key_size = key_size(field->Desc->number, proto_wire_type(field->Desc->kind));
			int key_key_size = key_size(1, proto_wire_type(kf->Desc->kind));
			int value_key_size = key_size(2, proto_wire_type(vf->Desc->kind));
			P(p->g, "for k, v := range m.", fieldname, " { ");
			P(p->g, "_ = k");
			P(p->g, "_ = v");
			/* sum: strings and helper identifiers */
			PgArg sum[16];
			int ns = 0;
			sum[ns++] = pg_arg_s(size_itoa(key_key_size));
			switch (kf->Desc->kind) {
			case PT_DOUBLE:
			case PT_FIXED64:
			case PT_SFIXED64: sum[ns++] = pg_arg_s("8"); break;
			case PT_FLOAT:
			case PT_FIXED32:
			case PT_SFIXED32: sum[ns++] = pg_arg_s("4"); break;
			case PT_INT64:
			case PT_UINT64:
			case PT_UINT32:
			case PT_ENUM:
			case PT_INT32:
				sum[ns++] = pg_arg_id(size_of_varint);
				sum[ns++] = pg_arg_s("(uint64(k))");
				break;
			case PT_BOOL: sum[ns++] = pg_arg_s("1"); break;
			case PT_STRING:
			case PT_BYTES:
				sum[ns++] = pg_arg_s("len(k)");
				sum[ns++] = pg_arg_id(size_of_varint);
				sum[ns++] = pg_arg_s("(uint64(len(k)))");
				break;
			case PT_SINT32:
			case PT_SINT64:
				sum[ns++] = pg_arg_id(size_of_zigzag);
				sum[ns++] = pg_arg_s("(uint64(k))");
				break;
			}

			switch (vf->Desc->kind) {
			case PT_DOUBLE:
			case PT_FIXED64:
			case PT_SFIXED64:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_s("8");
				break;
			case PT_FLOAT:
			case PT_FIXED32:
			case PT_SFIXED32:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_s("4");
				break;
			case PT_INT64:
			case PT_UINT64:
			case PT_UINT32:
			case PT_ENUM:
			case PT_INT32:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_id(size_of_varint);
				sum[ns++] = pg_arg_s("(uint64(v))");
				break;
			case PT_BOOL:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_s("1");
				break;
			case PT_STRING:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_s("len(v)");
				sum[ns++] = pg_arg_id(size_of_varint);
				sum[ns++] = pg_arg_s("(uint64(len(v)))");
				break;
			case PT_BYTES:
				P(p->g, "l = ", size_itoa(value_key_size), " + len(v)+", size_of_varint, "(uint64(len(v)))");
				sum[ns++] = pg_arg_s("l");
				break;
			case PT_SINT32:
			case PT_SINT64:
				sum[ns++] = pg_arg_s(size_itoa(value_key_size));
				sum[ns++] = pg_arg_id(size_of_zigzag);
				sum[ns++] = pg_arg_s("(uint64(v))");
				break;
			case PT_MESSAGE:
				P(p->g, "l = 0");
				P(p->g, "if v != nil {");
				size_message_size(p, "v", size_name, vf->Message);
				P(p->g, "}");
				P(p->g, "l += ", size_itoa(value_key_size), " + ", size_of_varint, "(uint64(l))");
				sum[ns++] = pg_arg_s("l");
				break;
			}
			PgArg map_entry_size[40];
			int nm = 0;
			map_entry_size[nm++] = pg_arg_s("mapEntrySize := ");
			for (int i = 0; i < ns; i++) {
				map_entry_size[nm++] = sum[i];
				/* if elt is not a string, then it is a helper function call */
				if (sum[i].kind == PGA_S && i < ns - 1)
					map_entry_size[nm++] = pg_arg_s("+");
			}
			pg_p(p->g, map_entry_size, nm);
			P(p->g, "n+=mapEntrySize+", field_key_size, "+", size_of_varint, "(uint64(mapEntrySize))");
			P(p->g, "}");
		} else if (go_field_is_list(field->Desc)) {
			P(p->g, "for _, e := range m.", fieldname, " { ");
			size_message_size(p, "e", size_name, field->Message);
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
			P(p->g, "}");
		} else {
			size_message_size(p, xcat("m.", fieldname), size_name, field->Message);
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
		}
		break;
	case PT_BYTES:
		if (repeated) {
			P(p->g, "for _, b := range m.", fieldname, " { ");
			P(p->g, "l = len(b)");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
			P(p->g, "}");
		} else if (!oneof && !pg_v131_field_has_presence(field->Desc)) {
			P(p->g, "l=len(m.", fieldname, ")");
			P(p->g, "if l > 0 {");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
			P(p->g, "}");
		} else {
			P(p->g, "l=len(m.", fieldname, ")");
			P(p->g, "n+=", size_itoa(key), "+l+", size_of_varint, "(uint64(l))");
		}
		break;
	case PT_SINT32:
	case PT_SINT64:
		if (packed) {
			P(p->g, "l = 0");
			P(p->g, "for _, e := range m.", fieldname, " {");
			P(p->g, "l+=", size_of_zigzag, "(uint64(e))");
			P(p->g, "}");
			P(p->g, "n+=", size_itoa(key), "+", size_of_varint, "(uint64(l))+l");
		} else if (repeated) {
			P(p->g, "for _, e := range m.", fieldname, " {");
			P(p->g, "n+=", size_itoa(key), "+", size_of_zigzag, "(uint64(e))");
			P(p->g, "}");
		} else if (nullable) {
			P(p->g, "n+=", size_itoa(key), "+", size_of_zigzag, "(uint64(*m.", fieldname, "))");
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			P(p->g, "n+=", size_itoa(key), "+", size_of_zigzag, "(uint64(m.", fieldname, "))");
			P(p->g, "}");
		} else {
			P(p->g, "n+=", size_itoa(key), "+", size_of_zigzag, "(uint64(m.", fieldname, "))");
		}
		break;
	default: fail("not implemented");
	}
	if (repeated || nullable)
		P(p->g, "}");
}

static void size_message(Size *p, const PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		size_message(p, message->Messages[i]);
	if (message->Desc->is_map_entry)
		return;
	p->once = true;

	const char *size_name = "SizeVT";
	const char *cc_type_name = message->GoIdent.GoName;

	P(p->g, "func (m *", cc_type_name, ") ", size_name, "() (n int) {");
	P(p->g, "if m == nil {");
	P(p->g, "return 0");
	P(p->g, "}");
	P(p->g, "var l int");
	P(p->g, "_ = l");
	Map oneofs = {0};
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (!is_real_oneof(field)) {
			size_field(p, false, field, size_name);
			continue;
		}
		const char *fieldname = field->Oneof->GoName;
		if (map_get(&oneofs, fieldname))
			continue;
		map_put(&oneofs, fieldname, MAP_TRUE);
		if (vt_is_well_known_type(message)) {
			P(p->g, "switch c := m.", fieldname, ".(type) {");
			for (int k = 0; k < field->Oneof->nFields; k++) {
				const PgField *f = field->Oneof->Fields[k];
				P(p->g, "case *", f->GoIdent, ":");
				P(p->g, "n += (*", vt_well_known_field_map(p->f, f), ")(c).", size_name, "()");
			}
			P(p->g, "}");
		} else {
			P(p->g, "if vtmsg, ok := m.", fieldname, ".(interface{ SizeVT() int }); ok {");
			P(p->g, "n+=vtmsg.", size_name, "()");
			P(p->g, "}");
		}
	}
	if (!vt_wrapper(p->f))
		P(p->g, "n+=len(m.unknownFields)");
	P(p->g, "return n");
	P(p->g, "}");
	P(p->g, "");

	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (!is_real_oneof(field))
			continue;
		PgGoIdent cc = field->GoIdent;
		if (vt_is_well_known_type(message) && vt_is_local_message(p->f, message))
			cc.GoImportPath = "";
		P(p->g, "func (m *", cc, ") ", size_name, "() (n int) {");
		P(p->g, "if m == nil {");
		P(p->g, "return 0");
		P(p->g, "}");
		P(p->g, "var l int");
		P(p->g, "_ = l");
		size_field(p, true, field, size_name);
		P(p->g, "return n");
		P(p->g, "}");
	}
}

static bool size_generate_file(VtFile *gen, PgFile *file)
{
	Size p = {gen, gen->g, false};
	for (int i = 0; i < file->nMessages; i++)
		size_message(&p, file->Messages[i]);
	return p.once;
}
