/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Copyright (c) 2013, The GoGo Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/marshal/marshalto.go (vtprotobuf v0.6.0). */

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool Stable, once, strict;
} Marshal;

/* counter */
static const char *counter_next(int *cnt) { return go_itoa(++*cnt); }
static const char *counter_current(const int *cnt) { return go_itoa(*cnt); }

static void marshal_encode_fixed64(Marshal *p, const char *var_name)
{
	P(p->g, "i -= 8");
	const char *le = vt_ident(p->f, "encoding/binary", "LittleEndian");
	P(p->g, le, ".PutUint64(dAtA[i:], uint64(", var_name, "))");
}

static void marshal_encode_fixed32(Marshal *p, const char *var_name)
{
	P(p->g, "i -= 4");
	const char *le = vt_ident(p->f, "encoding/binary", "LittleEndian");
	P(p->g, le, ".PutUint32(dAtA[i:], uint32(", var_name, "))");
}

static void marshal_encode_varint(Marshal *p, const char *var_name)
{
	P(p->g, "i = ", vt_helper("EncodeVarint"), "(dAtA, i, uint64(", var_name, "))");
}

static void marshal_encode_key(Marshal *p, int32_t field_number, int wire_type)
{
	uint32_t x = (uint32_t)field_number << 3 | (uint32_t)wire_type;
	uint8_t keybuf[5];
	int n = 0;
	for (; x > 127; x >>= 7)
		keybuf[n++] = (uint8_t)(0x80 | (x & 0x7F));
	keybuf[n++] = (uint8_t)x;
	for (int i = n - 1; i >= 0; i--) {
		P(p->g, "i--");
		P(p->g, "dAtA[i] = ", xfmt("0x%x", keybuf[i])); /* fmt.Sprintf("%#v", uint8) */
	}
}

/* reverseListRange */
static const char *marshal_reverse_list_range(Marshal *p, const char *exp)
{
	P(p->g, "for iNdEx := len(", exp, ") - 1; iNdEx >= 0; iNdEx-- {");
	return xcat(exp, "[iNdEx]");
}

static void marshal_backward_size(Marshal *p, bool var_int)
{
	P(p->g, "if err != nil {");
	P(p->g, "return 0, err");
	P(p->g, "}");
	P(p->g, "i -= size");
	if (var_int)
		marshal_encode_varint(p, "size");
}

static const char *marshal_method_marshal_to_sized_buffer(const Marshal *p)
{
	return p->strict ? "MarshalToSizedBufferVTStrict" : "MarshalToSizedBufferVT";
}

static const char *marshal_method_marshal_to(const Marshal *p) { return p->strict ? "MarshalToVTStrict" : "MarshalToVT"; }

static const char *marshal_method_marshal(const Marshal *p) { return p->strict ? "MarshalVTStrict" : "MarshalVT"; }

static void marshal_backward(Marshal *p, const char *var_name, bool var_int, const PgMessage *message)
{
	if (vt_is_well_known_type(message)) {
		P(p->g, "size, err := (*", vt_well_known_type_map(p->f, message), ")(", var_name, ").", marshal_method_marshal_to_sized_buffer(p),
		  "(dAtA[:i])");
		marshal_backward_size(p, var_int);
	} else if (vt_is_local_message(p->f, message)) {
		P(p->g, "size, err := ", var_name, ".", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:i])");
		marshal_backward_size(p, var_int);
	} else {
		P(p->g, "if vtmsg, ok := interface{}(", var_name, ").(interface{");
		P(p->g, marshal_method_marshal_to_sized_buffer(p), "([]byte) (int, error)");
		P(p->g, "}); ok{");
		P(p->g, "size, err := vtmsg.", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:i])");
		marshal_backward_size(p, var_int);
		P(p->g, "} else {");
		const char *m = vt_ident(p->f, PROTO_PKG, "Marshal");
		P(p->g, "encoded, err := ", m, "(", var_name, ")");
		P(p->g, "if err != nil {");
		P(p->g, "return 0, err");
		P(p->g, "}");
		P(p->g, "i -= len(encoded)");
		P(p->g, "copy(dAtA[i:], encoded)");
		if (var_int)
			marshal_encode_varint(p, "len(encoded)");
		P(p->g, "}");
	}
}

static void marshal_map_field(Marshal *p, const PgField *kv_field, const char *var_name)
{
	switch (kv_field->Desc->kind) {
	case PT_DOUBLE: {
		const char *f = vt_ident(p->f, "math", "Float64bits");
		marshal_encode_fixed64(p, xcat(f, "(float64(", var_name, "))"));
		break;
	}
	case PT_FLOAT: {
		const char *f = vt_ident(p->f, "math", "Float32bits");
		marshal_encode_fixed32(p, xcat(f, "(float32(", var_name, "))"));
		break;
	}
	case PT_INT64:
	case PT_UINT64:
	case PT_INT32:
	case PT_UINT32:
	case PT_ENUM: marshal_encode_varint(p, var_name); break;
	case PT_FIXED64:
	case PT_SFIXED64: marshal_encode_fixed64(p, var_name); break;
	case PT_FIXED32:
	case PT_SFIXED32: marshal_encode_fixed32(p, var_name); break;
	case PT_BOOL:
		P(p->g, "i--");
		P(p->g, "if ", var_name, " {");
		P(p->g, "dAtA[i] = 1");
		P(p->g, "} else {");
		P(p->g, "dAtA[i] = 0");
		P(p->g, "}");
		break;
	case PT_STRING:
	case PT_BYTES:
		P(p->g, "i -= len(", var_name, ")");
		P(p->g, "copy(dAtA[i:], ", var_name, ")");
		marshal_encode_varint(p, xcat("len(", var_name, ")"));
		break;
	case PT_SINT32: marshal_encode_varint(p, xcat("(uint32(", var_name, ") << 1) ^ uint32((", var_name, " >> 31))")); break;
	case PT_SINT64: marshal_encode_varint(p, xcat("(uint64(", var_name, ") << 1) ^ uint64((", var_name, " >> 63))")); break;
	case PT_MESSAGE: marshal_backward(p, var_name, true, kv_field->Message); break;
	}
}

/* The 1-byte bool encoding of a value (val), shared by several branches. */
static void marshal_bool_byte(Marshal *p, const char *cond)
{
	P(p->g, "i--");
	P(p->g, "if ", cond, " {");
	P(p->g, "dAtA[i] = 1");
	P(p->g, "} else {");
	P(p->g, "dAtA[i] = 0");
	P(p->g, "}");
}

/* The packed varint encoding of the Sint32/Sint64 kinds (bits 32 or 64). */
static void marshal_packed_zigzag(Marshal *p, int *num_gen, const char *fieldname, int bits, int32_t field_number, int wire_type)
{
	const char *jvar = xcat("j", counter_next(num_gen));
	const char *total = xcat("pksize", counter_next(num_gen));

	P(p->g, "var ", total, " int");
	P(p->g, "for _, num := range m.", fieldname, " {");
	P(p->g, total, " += ", vt_helper("SizeOfZigzag"), "(uint64(num))");
	P(p->g, "}");
	P(p->g, "i -= ", total);
	P(p->g, jvar, ":= i");

	P(p->g, "for _, num := range m.", fieldname, " {");
	const char *xvar = xcat("x", counter_next(num_gen));
	if (bits == 32)
		P(p->g, xvar, " := (uint32(num) << 1) ^ uint32((num >> 31))");
	else
		P(p->g, xvar, " := (uint64(num) << 1) ^ uint64((num >> 63))");
	P(p->g, "for ", xvar, " >= 1<<7 {");
	P(p->g, "dAtA[", jvar, "] = uint8(uint64(", xvar, ")&0x7f|0x80)");
	P(p->g, jvar, "++");
	P(p->g, xvar, " >>= 7");
	P(p->g, "}");
	P(p->g, "dAtA[", jvar, "] = uint8(", xvar, ")");
	P(p->g, jvar, "++");
	P(p->g, "}");

	marshal_encode_varint(p, total);
	marshal_encode_key(p, field_number, wire_type);
}

static void marshal_field(Marshal *p, bool oneof, int *num_gen, const PgField *field)
{
	const char *fieldname = field->GoName;
	bool nullable = field->Message || (!oneof && pg_v131_field_has_presence(field->Desc));
	bool repeated = is_repeated(field);
	if (repeated) {
		P(p->g, "if len(m.", fieldname, ") > 0 {");
	} else if (nullable) {
		if (field->Desc->cardinality == GO_REQUIRED) {
			P(p->g, "if m.", fieldname, " == nil {");
			const char *errorf = vt_ident(p->f, "fmt", "Errorf");
			P(p->g, "return 0, ", errorf, "(\"proto: required field ", field->Desc->d.name, " not set\")");
			P(p->g, "} else {");
		} else {
			P(p->g, "if m.", fieldname, " != nil {");
		}
	}
	bool packed = pg_v131_field_is_packed(field->Desc);
	int wire_type = proto_wire_type(field->Desc->kind);
	int32_t field_number = field->Desc->number;
	if (packed)
		wire_type = WIRE_BYTES;
	const char *m_field = xcat("m.", fieldname);
	switch (field->Desc->kind) {
	case PT_DOUBLE:
	case PT_FLOAT: {
		bool dbl = field->Desc->kind == PT_DOUBLE;
		void (*encode)(Marshal *, const char *) = dbl ? marshal_encode_fixed64 : marshal_encode_fixed32;
		const char *bits_fn = dbl ? "Float64bits" : "Float32bits";
		const char *conv = dbl ? "float64" : "float32";
		const char *width = dbl ? "8" : "4";
		if (packed) {
			const char *val = marshal_reverse_list_range(p, m_field);
			const char *n = counter_next(num_gen);
			const char *fn = vt_ident(p->f, "math", bits_fn);
			P(p->g, "f", n, " := ", fn, "(", conv, "(", val, "))");
			encode(p, xcat("f", counter_current(num_gen)));
			P(p->g, "}");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ") * ", width));
			marshal_encode_key(p, field_number, wire_type);
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			const char *n = counter_next(num_gen);
			const char *fn = vt_ident(p->f, "math", bits_fn);
			P(p->g, "f", n, " := ", fn, "(", conv, "(", val, "))");
			encode(p, xcat("f", counter_current(num_gen)));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			const char *fn = vt_ident(p->f, "math", bits_fn);
			encode(p, xcat(fn, "(", conv, "(*m.", fieldname, "))"));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			const char *fn = vt_ident(p->f, "math", bits_fn);
			encode(p, xcat(fn, "(", conv, "(m.", fieldname, "))"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			const char *fn = vt_ident(p->f, "math", bits_fn);
			encode(p, xcat(fn, "(", conv, "(m.", fieldname, "))"));
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	}
	case PT_INT64:
	case PT_UINT64:
	case PT_INT32:
	case PT_UINT32:
	case PT_ENUM:
		if (packed) {
			const char *jvar = xcat("j", counter_next(num_gen));
			const char *total = xcat("pksize", counter_next(num_gen));

			P(p->g, "var ", total, " int");
			P(p->g, "for _, num := range m.", fieldname, " {");
			P(p->g, total, " += ", vt_helper("SizeOfVarint"), "(uint64(num))");
			P(p->g, "}");

			P(p->g, "i -= ", total);
			P(p->g, jvar, ":= i");

			switch (field->Desc->kind) {
			case PT_INT64:
			case PT_INT32:
			case PT_ENUM:
				P(p->g, "for _, num1 := range m.", fieldname, " {");
				P(p->g, "num := uint64(num1)");
				break;
			default: P(p->g, "for _, num := range m.", fieldname, " {");
			}
			P(p->g, "for num >= 1<<7 {");
			P(p->g, "dAtA[", jvar, "] = uint8(uint64(num)&0x7f|0x80)");
			P(p->g, "num >>= 7");
			P(p->g, jvar, "++");
			P(p->g, "}");
			P(p->g, "dAtA[", jvar, "] = uint8(num)");
			P(p->g, jvar, "++");
			P(p->g, "}");

			marshal_encode_varint(p, total);
			marshal_encode_key(p, field_number, wire_type);
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			marshal_encode_varint(p, val);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			marshal_encode_varint(p, xcat("*m.", fieldname));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			marshal_encode_varint(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			marshal_encode_varint(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_FIXED32:
	case PT_SFIXED32: {
		bool b64 = field->Desc->kind == PT_FIXED64 || field->Desc->kind == PT_SFIXED64;
		void (*encode)(Marshal *, const char *) = b64 ? marshal_encode_fixed64 : marshal_encode_fixed32;
		if (packed) {
			const char *val = marshal_reverse_list_range(p, m_field);
			encode(p, val);
			P(p->g, "}");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ") * ", b64 ? "8" : "4"));
			marshal_encode_key(p, field_number, wire_type);
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			encode(p, val);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			encode(p, xcat("*m.", fieldname));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			encode(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			encode(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	}
	case PT_BOOL:
		if (packed) {
			const char *val = marshal_reverse_list_range(p, m_field);
			marshal_bool_byte(p, val);
			P(p->g, "}");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			marshal_bool_byte(p, val);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			marshal_bool_byte(p, xcat("*m.", fieldname));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " {");
			marshal_bool_byte(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			marshal_bool_byte(p, m_field);
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	case PT_STRING:
		if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			P(p->g, "i -= len(", val, ")");
			P(p->g, "copy(dAtA[i:], ", val, ")");
			marshal_encode_varint(p, xcat("len(", val, ")"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			P(p->g, "i -= len(*m.", fieldname, ")");
			P(p->g, "copy(dAtA[i:], *m.", fieldname, ")");
			marshal_encode_varint(p, xcat("len(*m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if len(m.", fieldname, ") > 0 {");
			P(p->g, "i -= len(m.", fieldname, ")");
			P(p->g, "copy(dAtA[i:], m.", fieldname, ")");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			P(p->g, "i -= len(m.", fieldname, ")");
			P(p->g, "copy(dAtA[i:], m.", fieldname, ")");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	case PT_GROUP:
		marshal_encode_key(p, field_number, WIRE_END_GROUP);
		marshal_backward(p, m_field, false, field->Message);
		marshal_encode_key(p, field_number, WIRE_START_GROUP);
		break;
	case PT_MESSAGE:
		if (go_field_is_map(field->Desc)) {
			const char *go_typ_k = vt_field_go_type(p->f, field->Message->Fields[0], NULL);
			int key_kind = field->Message->Fields[0]->Desc->kind;
			int val_kind = field->Message->Fields[1]->Desc->kind;

			const char *val;
			if (p->Stable && key_kind != PT_BOOL) {
				const char *keys_name = xcat("keysFor", fieldname);
				P(p->g, keys_name, " := make([]", go_typ_k, ", 0, len(m.", fieldname, "))");
				P(p->g, "for k := range m.", fieldname, " {");
				P(p->g, keys_name, " = append(", keys_name, ", ", go_typ_k, "(k))");
				P(p->g, "}");
				const char *sort_slice = vt_ident(p->f, "sort", "Slice");
				P(p->g, sort_slice, "(", keys_name, ", func(i, j int) bool {");
				P(p->g, "return ", keys_name, "[i] < ", keys_name, "[j]");
				P(p->g, "})");
				val = marshal_reverse_list_range(p, keys_name);
			} else {
				P(p->g, "for k := range m.", fieldname, " {");
				val = "k";
			}
			if (p->Stable)
				P(p->g, "v := m.", fieldname, "[", go_typ_k, "(", val, ")]");
			else
				P(p->g, "v := m.", fieldname, "[", val, "]");
			P(p->g, "baseI := i");

			const char *accessor = "v";
			marshal_map_field(p, field->Message->Fields[1], accessor);
			marshal_encode_key(p, 2, proto_wire_type(val_kind));

			marshal_map_field(p, field->Message->Fields[0], val);
			marshal_encode_key(p, 1, proto_wire_type(key_kind));
			marshal_encode_varint(p, "baseI - i");
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			marshal_backward(p, val, true, field->Message);
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			marshal_backward(p, m_field, true, field->Message);
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	case PT_BYTES:
		if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			P(p->g, "i -= len(", val, ")");
			P(p->g, "copy(dAtA[i:], ", val, ")");
			marshal_encode_varint(p, xcat("len(", val, ")"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (!oneof && !pg_v131_field_has_presence(field->Desc)) {
			P(p->g, "if len(m.", fieldname, ") > 0 {");
			P(p->g, "i -= len(m.", fieldname, ")");
			P(p->g, "copy(dAtA[i:], m.", fieldname, ")");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			P(p->g, "i -= len(m.", fieldname, ")");
			P(p->g, "copy(dAtA[i:], m.", fieldname, ")");
			marshal_encode_varint(p, xcat("len(m.", fieldname, ")"));
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	case PT_SINT32:
	case PT_SINT64: {
		bool b64 = field->Desc->kind == PT_SINT64;
		const char *u = b64 ? "uint64" : "uint32";
		const char *shift = b64 ? "63" : "31";
		if (packed) {
			marshal_packed_zigzag(p, num_gen, fieldname, b64 ? 64 : 32, field_number, wire_type);
		} else if (repeated) {
			const char *val = marshal_reverse_list_range(p, m_field);
			P(p->g, "x", counter_next(num_gen), " := (", u, "(", val, ") << 1) ^ ", u, "((", val, " >> ", shift, "))");
			marshal_encode_varint(p, xcat("x", counter_current(num_gen)));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else if (nullable) {
			marshal_encode_varint(p, xcat("(", u, "(*m.", fieldname, ") << 1) ^ ", u, "((*m.", fieldname, " >> ", shift, "))"));
			marshal_encode_key(p, field_number, wire_type);
		} else if (!oneof) {
			P(p->g, "if m.", fieldname, " != 0 {");
			marshal_encode_varint(p, xcat("(", u, "(m.", fieldname, ") << 1) ^ ", u, "((m.", fieldname, " >> ", shift, "))"));
			marshal_encode_key(p, field_number, wire_type);
			P(p->g, "}");
		} else {
			marshal_encode_varint(p, xcat("(", u, "(m.", fieldname, ") << 1) ^ ", u, "((m.", fieldname, " >> ", shift, "))"));
			marshal_encode_key(p, field_number, wire_type);
		}
		break;
	}
	default: fail("not implemented");
	}
	if (repeated || nullable)
		P(p->g, "}");
}

/* marshalForwardOneOf: "size, err := <varname>.<method>(dAtA[:i])" and the error check. */
static void marshal_forward_oneof(Marshal *p, const char *varname)
{
	P(p->g, "size, err := ", varname, ".", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:i])");
	P(p->g, "if err != nil {");
	P(p->g, "return 0, err");
	P(p->g, "}");
	P(p->g, "i -= size");
}

static void marshal_message(Marshal *p, PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		marshal_message(p, message->Messages[i]);
	if (message->Desc->is_map_entry)
		return;
	p->once = true;

	int num_gen = 0;
	const char *cc_type_name = message->GoIdent.GoName;

	P(p->g, "func (m *", cc_type_name, ") ", marshal_method_marshal(p), "() (dAtA []byte, err error) {");
	P(p->g, "if m == nil {");
	P(p->g, "return nil, nil");
	P(p->g, "}");
	P(p->g, "size := m.SizeVT()");
	P(p->g, "dAtA = make([]byte, size)");
	P(p->g, "n, err := m.", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:size])");
	P(p->g, "if err != nil {");
	P(p->g, "return nil, err");
	P(p->g, "}");
	P(p->g, "return dAtA[:n], nil");
	P(p->g, "}");
	P(p->g, "");
	P(p->g, "func (m *", cc_type_name, ") ", marshal_method_marshal_to(p), "(dAtA []byte) (int, error) {");
	P(p->g, "size := m.SizeVT()");
	P(p->g, "return m.", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:size])");
	P(p->g, "}");
	P(p->g, "");
	P(p->g, "func (m *", cc_type_name, ") ", marshal_method_marshal_to_sized_buffer(p), "(dAtA []byte) (int, error) {");
	P(p->g, "if m == nil {");
	P(p->g, "return 0, nil");
	P(p->g, "}");
	P(p->g, "i := len(dAtA)");
	P(p->g, "_ = i");
	P(p->g, "var l int");
	P(p->g, "_ = l");

	if (!vt_wrapper(p->f)) {
		P(p->g, "if m.unknownFields != nil {");
		P(p->g, "i -= len(m.unknownFields)");
		P(p->g, "copy(dAtA[i:], m.unknownFields)");
		P(p->g, "}");
	}

	sort_fields_by_number(message);

	if (p->strict) {
		for (int i = message->nFields - 1; i >= 0; i--) {
			const PgField *field = message->Fields[i];
			if (!is_real_oneof(field)) {
				marshal_field(p, false, &num_gen, field);
			} else {
				if (vt_is_well_known_type(message)) {
					P(p->g, "if m, ok := m.", field->Oneof->GoName, ".(*", field->GoIdent, "); ok {");
					P(p->g, "msg := ((*", vt_well_known_field_map(p->f, field), ")(m))");
				} else {
					P(p->g, "if msg, ok := m.", field->Oneof->GoName, ".(*", field->GoIdent.GoName, "); ok {");
				}
				marshal_forward_oneof(p, "msg");
				P(p->g, "}");
			}
		}
	} else {
		/* To match the wire format of proto.Marshal, oneofs have to be marshaled before
		 * fields. See https://github.com/planetscale/vtprotobuf/pull/22 */
		Map oneofs = {0};
		for (int i = message->nFields - 1; i >= 0; i--) {
			const PgField *field = message->Fields[i];
			if (!is_real_oneof(field))
				continue;
			const char *fieldname = field->Oneof->GoName;
			if (map_get(&oneofs, fieldname))
				continue;
			map_put(&oneofs, fieldname, MAP_TRUE);
			if (vt_is_well_known_type(message)) {
				P(p->g, "switch c := m.", fieldname, ".(type) {");
				for (int k = 0; k < field->Oneof->nFields; k++) {
					const PgField *f = field->Oneof->Fields[k];
					P(p->g, "case *", f->GoIdent, ":");
					/* marshalForwardOneOf(`(*`, WellKnownFieldMap(f), `)(c)`): the GoIdent is
					 * qualified within the P call */
					P(p->g, "size, err := ", "(*", vt_well_known_field_map(p->f, f), ")(c)", ".",
					  marshal_method_marshal_to_sized_buffer(p), "(dAtA[:i])");
					P(p->g, "if err != nil {");
					P(p->g, "return 0, err");
					P(p->g, "}");
					P(p->g, "i -= size");
				}
				P(p->g, "}");
			} else {
				P(p->g, "if vtmsg, ok := m.", fieldname, ".(interface{");
				P(p->g, marshal_method_marshal_to_sized_buffer(p), " ([]byte) (int, error)");
				P(p->g, "}); ok {");
				marshal_forward_oneof(p, "vtmsg");
				P(p->g, "}");
			}
		}

		for (int i = message->nFields - 1; i >= 0; i--) {
			const PgField *field = message->Fields[i];
			if (!is_real_oneof(field))
				marshal_field(p, false, &num_gen, field);
		}
	}

	P(p->g, "return len(dAtA) - i, nil");
	P(p->g, "}");
	P(p->g, "");

	/* Generate MarshalToVT methods for oneof fields */
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (!is_real_oneof(field))
			continue;
		const char *cc = field->GoIdent.GoName;
		P(p->g, "func (m *", cc, ") ", marshal_method_marshal_to(p), "(dAtA []byte) (int, error) {");
		P(p->g, "size := m.SizeVT()");
		P(p->g, "return m.", marshal_method_marshal_to_sized_buffer(p), "(dAtA[:size])");
		P(p->g, "}");
		P(p->g, "");
		P(p->g, "func (m *", cc, ") ", marshal_method_marshal_to_sized_buffer(p), "(dAtA []byte) (int, error) {");
		P(p->g, "i := len(dAtA)");
		marshal_field(p, true, &num_gen, field);
		P(p->g, "return len(dAtA) - i, nil");
		P(p->g, "}");
	}
}

static bool marshal_generate(VtFile *gen, PgFile *file, bool strict)
{
	Marshal p = {gen, gen->g, false, false, strict};
	for (int i = 0; i < file->nMessages; i++)
		marshal_message(&p, file->Messages[i]);
	return p.once;
}

static bool marshal_generate_file(VtFile *gen, PgFile *file) { return marshal_generate(gen, file, false); }
static bool marshal_strict_generate_file(VtFile *gen, PgFile *file) { return marshal_generate(gen, file, true); }
