/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Copyright (c) 2013, The GoGo Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/unmarshal/unmarshal.go (vtprotobuf v0.6.0). */

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool unsafe;
	bool once;
} Unmarshal;

static const char *unmarshal_method(const Unmarshal *p) { return p->unsafe ? "UnmarshalVTUnsafe" : "UnmarshalVT"; }

static void unmarshal_return_eof(Unmarshal *p)
{
	const char *eof = vt_ident(p->f, "io", "ErrUnexpectedEOF");
	P(p->g, "return ", eof);
}

static void unmarshal_decode_message(Unmarshal *p, const char *var_name, const char *buf, const PgMessage *message)
{
	if (vt_is_well_known_type(message)) {
		P(p->g, "if err := (*", vt_well_known_type_map(p->f, message), ")(", var_name, ").", unmarshal_method(p), "(", buf, "); err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
	} else if (vt_is_local_message(p->f, message)) {
		P(p->g, "if err := ", var_name, ".", unmarshal_method(p), "(", buf, "); err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
	} else {
		P(p->g, "if unmarshal, ok := interface{}(", var_name, ").(interface{");
		P(p->g, unmarshal_method(p), "([]byte) error");
		P(p->g, "}); ok{");
		P(p->g, "if err := unmarshal.", unmarshal_method(p), "(", buf, "); err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
		P(p->g, "} else {");
		const char *um = vt_ident(p->f, PROTO_PKG, "Unmarshal");
		P(p->g, "if err := ", um, "(", buf, ", ", var_name, "); err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
		P(p->g, "}");
	}
}

static void unmarshal_decode_varint(Unmarshal *p, const char *var_name, const char *typ_name)
{
	P(p->g, "for shift := uint(0); ; shift += 7 {");
	P(p->g, "if shift >= 64 {");
	P(p->g, "return ", vt_helper("ErrIntOverflow"));
	P(p->g, "}");
	P(p->g, "if iNdEx >= l {");
	unmarshal_return_eof(p);
	P(p->g, "}");
	P(p->g, "b := dAtA[iNdEx]");
	P(p->g, "iNdEx++");
	P(p->g, var_name, " |= ", typ_name, "(b&0x7F) << shift");
	P(p->g, "if b < 0x80 {");
	P(p->g, "break");
	P(p->g, "}");
	P(p->g, "}");
}

static void unmarshal_decode_fixed(Unmarshal *p, const char *var_name, const char *type_name, int width)
{
	P(p->g, "if (iNdEx+", width == 4 ? "4" : "8", ") > l {");
	unmarshal_return_eof(p);
	P(p->g, "}");
	const char *le = vt_ident(p->f, "encoding/binary", "LittleEndian");
	P(p->g, var_name, " = ", type_name, "(", le, width == 4 ? ".Uint32" : ".Uint64", "(dAtA[iNdEx:]))");
	P(p->g, "iNdEx += ", width == 4 ? "4" : "8");
}

static void unmarshal_decode_fixed32(Unmarshal *p, const char *var_name, const char *type_name)
{
	unmarshal_decode_fixed(p, var_name, type_name, 4);
}

static void unmarshal_decode_fixed64(Unmarshal *p, const char *var_name, const char *type_name)
{
	unmarshal_decode_fixed(p, var_name, type_name, 8);
}

static const char *unmarshal_no_star_or_slice_type(Unmarshal *p, const PgField *field)
{
	const char *typ = vt_field_go_type(p->f, field, NULL);
	if (typ[0] == '[' && typ[1] == ']')
		typ += 2;
	if (typ[0] == '*')
		typ++;
	return typ;
}

/* The length checks of a length-delimited value: "<len>" was decoded into var, the end index
 * goes to post. */
static void unmarshal_check_len(Unmarshal *p, const char *len_var, const char *post_var, const char *post_init)
{
	P(p->g, "if ", len_var, " < 0 {");
	P(p->g, "return ", vt_helper("ErrInvalidLength"));
	P(p->g, "}");
	P(p->g, post_var, " := ", post_init);
	P(p->g, "if ", post_var, " < 0 {");
	P(p->g, "return ", vt_helper("ErrInvalidLength"));
	P(p->g, "}");
	P(p->g, "if ", post_var, " > l {");
	unmarshal_return_eof(p);
	P(p->g, "}");
}

static void unmarshal_map_field(Unmarshal *p, const char *var_name, const PgField *field)
{
	switch (field->Desc->kind) {
	case PT_DOUBLE: {
		P(p->g, "var ", var_name, "temp uint64");
		unmarshal_decode_fixed64(p, xcat(var_name, "temp"), "uint64");
		const char *fn = vt_ident(p->f, "math", "Float64frombits");
		P(p->g, var_name, " = ", fn, "(", var_name, "temp)");
		break;
	}
	case PT_FLOAT: {
		P(p->g, "var ", var_name, "temp uint32");
		unmarshal_decode_fixed32(p, xcat(var_name, "temp"), "uint32");
		const char *fn = vt_ident(p->f, "math", "Float32frombits");
		P(p->g, var_name, " = ", fn, "(", var_name, "temp)");
		break;
	}
	case PT_INT64: unmarshal_decode_varint(p, var_name, "int64"); break;
	case PT_UINT64: unmarshal_decode_varint(p, var_name, "uint64"); break;
	case PT_INT32: unmarshal_decode_varint(p, var_name, "int32"); break;
	case PT_FIXED64: unmarshal_decode_fixed64(p, var_name, "uint64"); break;
	case PT_FIXED32: unmarshal_decode_fixed32(p, var_name, "uint32"); break;
	case PT_BOOL:
		P(p->g, "var ", var_name, "temp int");
		unmarshal_decode_varint(p, xcat(var_name, "temp"), "int");
		P(p->g, var_name, " = bool(", var_name, "temp != 0)");
		break;
	case PT_STRING:
		P(p->g, "var stringLen", var_name, " uint64");
		unmarshal_decode_varint(p, xcat("stringLen", var_name), "uint64");
		P(p->g, "intStringLen", var_name, " := int(stringLen", var_name, ")");
		unmarshal_check_len(p, xcat("intStringLen", var_name), xcat("postStringIndex", var_name),
				    xcat("iNdEx + intStringLen", var_name));
		if (p->unsafe) {
			P(p->g, "if intStringLen", var_name, " == 0 {");
			P(p->g, var_name, " = \"\"");
			P(p->g, "} else {");
			const char *us = vt_ident(p->f, "unsafe", "String");
			P(p->g, var_name, " = ", us, "(&dAtA[iNdEx], intStringLen", var_name, ")");
			P(p->g, "}");
		} else {
			P(p->g, var_name, " = ", "string", "(dAtA[iNdEx:postStringIndex", var_name, "])");
		}
		P(p->g, "iNdEx = postStringIndex", var_name);
		break;
	case PT_MESSAGE: {
		P(p->g, "var mapmsglen int");
		unmarshal_decode_varint(p, "mapmsglen", "int");
		unmarshal_check_len(p, "mapmsglen", "postmsgIndex", "iNdEx + mapmsglen");
		const char *buf = "dAtA[iNdEx:postmsgIndex]";
		const char *t = unmarshal_no_star_or_slice_type(p, field);
		P(p->g, var_name, " = &", t, "{}");
		unmarshal_decode_message(p, var_name, buf, field->Message);
		P(p->g, "iNdEx = postmsgIndex");
		break;
	}
	case PT_BYTES:
		P(p->g, "var mapbyteLen uint64");
		unmarshal_decode_varint(p, "mapbyteLen", "uint64");
		P(p->g, "intMapbyteLen := int(mapbyteLen)");
		unmarshal_check_len(p, "intMapbyteLen", "postbytesIndex", "iNdEx + intMapbyteLen");
		if (p->unsafe) {
			P(p->g, var_name, " = dAtA[iNdEx:postbytesIndex]");
		} else {
			P(p->g, var_name, " = make([]byte, mapbyteLen)");
			P(p->g, "copy(", var_name, ", dAtA[iNdEx:postbytesIndex])");
		}
		P(p->g, "iNdEx = postbytesIndex");
		break;
	case PT_UINT32: unmarshal_decode_varint(p, var_name, "uint32"); break;
	case PT_ENUM: {
		const char *go_typ_v = vt_field_go_type(p->f, field, NULL);
		unmarshal_decode_varint(p, var_name, go_typ_v);
		break;
	}
	case PT_SFIXED32: unmarshal_decode_fixed32(p, var_name, "int32"); break;
	case PT_SFIXED64: unmarshal_decode_fixed64(p, var_name, "int64"); break;
	case PT_SINT32:
		P(p->g, "var ", var_name, "temp int32");
		unmarshal_decode_varint(p, xcat(var_name, "temp"), "int32");
		P(p->g, var_name, "temp = int32((uint32(", var_name, "temp) >> 1) ^ uint32(((", var_name, "temp&1)<<31)>>31))");
		P(p->g, var_name, " = int32(", var_name, "temp)");
		break;
	case PT_SINT64:
		P(p->g, "var ", var_name, "temp uint64");
		unmarshal_decode_varint(p, xcat(var_name, "temp"), "uint64");
		P(p->g, var_name, "temp = (", var_name, "temp >> 1) ^ uint64((int64(", var_name, "temp&1)<<63)>>63)");
		P(p->g, var_name, " = int64(", var_name, "temp)");
		break;
	}
}

typedef void (*DecodeFn)(Unmarshal *p, const char *var_name, const char *typ);

/* The integer kinds: decode into v (or the field) and store it by cardinality. */
static void unmarshal_int_item(Unmarshal *p, DecodeFn decode, const PgField *field, const char *fieldname, const char *typ, bool oneof,
			       bool repeated, bool proto3, bool nullable)
{
	if (oneof) {
		P(p->g, "var v ", typ);
		decode(p, "v", typ);
		P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": v}");
	} else if (repeated) {
		P(p->g, "var v ", typ);
		decode(p, "v", typ);
		P(p->g, "m.", fieldname, " = append(m.", fieldname, ", v)");
	} else if (proto3 && !nullable) {
		P(p->g, "m.", fieldname, " = 0");
		decode(p, xcat("m.", fieldname), typ);
	} else {
		P(p->g, "var v ", typ);
		decode(p, "v", typ);
		P(p->g, "m.", fieldname, " = &v");
	}
}

static void unmarshal_field_item(Unmarshal *p, const PgField *field, const char *fieldname, const PgMessage *message, bool proto3)
{
	bool repeated = is_repeated(field);
	const char *typ = unmarshal_no_star_or_slice_type(p, field);
	bool oneof = is_real_oneof(field);
	bool nullable = field->Oneof && pg_v131_oneof_is_synthetic(field->Oneof->Desc);

	switch (field->Desc->kind) {
	case PT_DOUBLE:
	case PT_FLOAT: {
		bool dbl = field->Desc->kind == PT_DOUBLE;
		const char *from_bits = dbl ? "Float64frombits" : "Float32frombits";
		P(p->g, dbl ? "var v uint64" : "var v uint32");
		if (dbl)
			unmarshal_decode_fixed64(p, "v", "uint64");
		else
			unmarshal_decode_fixed32(p, "v", "uint32");
		const char *fn = vt_ident(p->f, "math", from_bits);
		if (oneof) {
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": ", typ, "(", fn, "(v))}");
		} else if (repeated) {
			P(p->g, "v2 := ", typ, "(", fn, "(v))");
			P(p->g, "m.", fieldname, " = append(m.", fieldname, ", v2)");
		} else if (proto3 && !nullable) {
			P(p->g, "m.", fieldname, " = ", typ, "(", fn, "(v))");
		} else {
			P(p->g, "v2 := ", typ, "(", fn, "(v))");
			P(p->g, "m.", fieldname, " = &v2");
		}
		break;
	}
	case PT_INT64:
	case PT_UINT64:
	case PT_INT32:
	case PT_UINT32:
	case PT_ENUM: unmarshal_int_item(p, unmarshal_decode_varint, field, fieldname, typ, oneof, repeated, proto3, nullable); break;
	case PT_FIXED64:
	case PT_SFIXED64: unmarshal_int_item(p, unmarshal_decode_fixed64, field, fieldname, typ, oneof, repeated, proto3, nullable); break;
	case PT_FIXED32:
	case PT_SFIXED32: unmarshal_int_item(p, unmarshal_decode_fixed32, field, fieldname, typ, oneof, repeated, proto3, nullable); break;
	case PT_BOOL:
		P(p->g, "var v int");
		unmarshal_decode_varint(p, "v", "int");
		if (oneof) {
			P(p->g, "b := ", typ, "(v != 0)");
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": b}");
		} else if (repeated) {
			P(p->g, "m.", fieldname, " = append(m.", fieldname, ", ", typ, "(v != 0))");
		} else if (proto3 && !nullable) {
			P(p->g, "m.", fieldname, " = ", typ, "(v != 0)");
		} else {
			P(p->g, "b := ", typ, "(v != 0)");
			P(p->g, "m.", fieldname, " = &b");
		}
		break;
	case PT_STRING: {
		P(p->g, "var stringLen uint64");
		unmarshal_decode_varint(p, "stringLen", "uint64");
		P(p->g, "intStringLen := int(stringLen)");
		unmarshal_check_len(p, "intStringLen", "postIndex", "iNdEx + intStringLen");
		const char *str = "string(dAtA[iNdEx:postIndex])";
		if (p->unsafe) {
			str = "stringValue";
			P(p->g, "var stringValue string");
			P(p->g, "if intStringLen > 0 {");
			const char *us = vt_ident(p->f, "unsafe", "String");
			P(p->g, "stringValue = ", us, "(&dAtA[iNdEx], intStringLen)");
			P(p->g, "}");
		}
		if (oneof) {
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": ", str, "}");
		} else if (repeated) {
			P(p->g, "m.", fieldname, " = append(m.", fieldname, ", ", str, ")");
		} else if (proto3 && !nullable) {
			P(p->g, "m.", fieldname, " = ", str);
		} else {
			P(p->g, "s := ", str);
			P(p->g, "m.", fieldname, " = &s");
		}
		P(p->g, "iNdEx = postIndex");
		break;
	}
	case PT_GROUP: {
		P(p->g, "groupStart := iNdEx");
		P(p->g, "for {");
		P(p->g, "maybeGroupEnd := iNdEx");
		P(p->g, "var groupFieldWire uint64");
		unmarshal_decode_varint(p, "groupFieldWire", "uint64");
		P(p->g, "groupWireType := int(wire & 0x7)");
		P(p->g, "if groupWireType == ", go_itoa(WIRE_END_GROUP), "{");
		unmarshal_decode_message(p, xcat("m.", fieldname), "dAtA[groupStart:maybeGroupEnd]", field->Message);
		P(p->g, "break");
		P(p->g, "}");
		P(p->g, "skippy, err := ", vt_helper("Skip"), "(dAtA[iNdEx:])");
		P(p->g, "if err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
		P(p->g, "if (skippy < 0) || (iNdEx + skippy) < 0 {");
		P(p->g, "return ", vt_helper("ErrInvalidLength"));
		P(p->g, "}");
		P(p->g, "iNdEx += skippy");
		P(p->g, "}");
		break;
	}
	case PT_MESSAGE:
		P(p->g, "var msglen int");
		unmarshal_decode_varint(p, "msglen", "int");
		unmarshal_check_len(p, "msglen", "postIndex", "iNdEx + msglen");
		if (oneof) {
			const char *buf = "dAtA[iNdEx:postIndex]";
			const char *msgname = unmarshal_no_star_or_slice_type(p, field);
			P(p->g, "if oneof, ok := m.", fieldname, ".(*", field->GoIdent, "); ok {");
			unmarshal_decode_message(p, xcat("oneof.", field->GoName), buf, field->Message);
			P(p->g, "} else {");
			if (vt_should_pool(p->f, message) && vt_should_pool(p->f, field->Message))
				P(p->g, "v := ", msgname, "FromVTPool()");
			else
				P(p->g, "v := &", msgname, "{}");
			unmarshal_decode_message(p, "v", buf, field->Message);
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": v}");
			P(p->g, "}");
		} else if (go_field_is_map(field->Desc)) {
			const char *go_typ = vt_field_go_type(p->f, field, NULL);
			const char *go_typ_k = vt_field_go_type(p->f, field->Message->Fields[0], NULL);
			const char *go_typ_v = vt_field_go_type(p->f, field->Message->Fields[1], NULL);

			P(p->g, "if m.", fieldname, " == nil {");
			P(p->g, "m.", fieldname, " = make(", go_typ, ")");
			P(p->g, "}");

			P(p->g, "var mapkey ", go_typ_k);
			P(p->g, "var mapvalue ", go_typ_v);
			P(p->g, "for iNdEx < postIndex {");

			P(p->g, "entryPreIndex := iNdEx");
			P(p->g, "var wire uint64");
			unmarshal_decode_varint(p, "wire", "uint64");
			P(p->g, "fieldNum := int32(wire >> 3)");

			P(p->g, "if fieldNum == 1 {");
			unmarshal_map_field(p, "mapkey", field->Message->Fields[0]);
			P(p->g, "} else if fieldNum == 2 {");
			unmarshal_map_field(p, "mapvalue", field->Message->Fields[1]);
			P(p->g, "} else {");
			P(p->g, "iNdEx = entryPreIndex");
			P(p->g, "skippy, err := ", vt_helper("Skip"), "(dAtA[iNdEx:])");
			P(p->g, "if err != nil {");
			P(p->g, "return err");
			P(p->g, "}");
			P(p->g, "if (skippy < 0) || (iNdEx + skippy) < 0 {");
			P(p->g, "return ", vt_helper("ErrInvalidLength"));
			P(p->g, "}");
			P(p->g, "if (iNdEx + skippy) > postIndex {");
			unmarshal_return_eof(p);
			P(p->g, "}");
			P(p->g, "iNdEx += skippy");
			P(p->g, "}");
			P(p->g, "}");
			P(p->g, "m.", fieldname, "[mapkey] = mapvalue");
		} else if (repeated) {
			if (vt_should_pool(p->f, message)) {
				P(p->g, "if len(m.", fieldname, ") == cap(m.", fieldname, ") {");
				P(p->g, "m.", fieldname, " = append(m.", fieldname, ", &", field->Message->GoIdent, "{})");
				P(p->g, "} else {");
				P(p->g, "m.", fieldname, " = m.", fieldname, "[:len(m.", fieldname, ") + 1]");
				P(p->g, "if m.", fieldname, "[len(m.", fieldname, ") - 1] == nil {");
				P(p->g, "m.", fieldname, "[len(m.", fieldname, ") - 1] = &", field->Message->GoIdent, "{}");
				P(p->g, "}");
				P(p->g, "}");
			} else {
				P(p->g, "m.", fieldname, " = append(m.", fieldname, ", &", field->Message->GoIdent, "{})");
			}
			const char *varname = xcat("m.", fieldname, "[len(m.", fieldname, ") - 1]");
			unmarshal_decode_message(p, varname, "dAtA[iNdEx:postIndex]", field->Message);
		} else {
			P(p->g, "if m.", fieldname, " == nil {");
			if (vt_should_pool(p->f, message) && vt_should_pool(p->f, field->Message))
				P(p->g, "m.", fieldname, " = ", field->Message->GoIdent, "FromVTPool()");
			else
				P(p->g, "m.", fieldname, " = &", field->Message->GoIdent, "{}");
			P(p->g, "}");
			unmarshal_decode_message(p, xcat("m.", fieldname), "dAtA[iNdEx:postIndex]", field->Message);
		}
		P(p->g, "iNdEx = postIndex");
		break;
	case PT_BYTES:
		P(p->g, "var byteLen int");
		unmarshal_decode_varint(p, "byteLen", "int");
		unmarshal_check_len(p, "byteLen", "postIndex", "iNdEx + byteLen");
		if (oneof) {
			if (p->unsafe) {
				P(p->g, "v := dAtA[iNdEx:postIndex]");
			} else {
				P(p->g, "v := make([]byte, postIndex-iNdEx)");
				P(p->g, "copy(v, dAtA[iNdEx:postIndex])");
			}
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": v}");
		} else if (repeated) {
			if (p->unsafe) {
				P(p->g, "m.", fieldname, " = append(m.", fieldname, ", dAtA[iNdEx:postIndex])");
			} else {
				P(p->g, "m.", fieldname, " = append(m.", fieldname, ", make([]byte, postIndex-iNdEx))");
				P(p->g, "copy(m.", fieldname, "[len(m.", fieldname, ")-1], dAtA[iNdEx:postIndex])");
			}
		} else {
			if (p->unsafe) {
				P(p->g, "m.", fieldname, " = dAtA[iNdEx:postIndex]");
			} else {
				P(p->g, "m.", fieldname, " = append(m.", fieldname, "[:0] , dAtA[iNdEx:postIndex]...)");
				P(p->g, "if m.", fieldname, " == nil {");
				P(p->g, "m.", fieldname, " = []byte{}");
				P(p->g, "}");
			}
		}
		P(p->g, "iNdEx = postIndex");
		break;
	case PT_SINT32:
		P(p->g, "var v ", typ);
		unmarshal_decode_varint(p, "v", typ);
		P(p->g, "v = ", typ, "((uint32(v) >> 1) ^ uint32(((v&1)<<31)>>31))");
		if (oneof)
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": v}");
		else if (repeated)
			P(p->g, "m.", fieldname, " = append(m.", fieldname, ", v)");
		else if (proto3 && !nullable)
			P(p->g, "m.", fieldname, " = v");
		else
			P(p->g, "m.", fieldname, " = &v");
		break;
	case PT_SINT64:
		P(p->g, "var v uint64");
		unmarshal_decode_varint(p, "v", "uint64");
		P(p->g, "v = (v >> 1) ^ uint64((int64(v&1)<<63)>>63)");
		if (oneof) {
			P(p->g, "m.", fieldname, " = &", field->GoIdent, "{", field->GoName, ": ", typ, "(v)}");
		} else if (repeated) {
			P(p->g, "m.", fieldname, " = append(m.", fieldname, ", ", typ, "(v))");
		} else if (proto3 && !nullable) {
			P(p->g, "m.", fieldname, " = ", typ, "(v)");
		} else {
			P(p->g, "v2 := ", typ, "(v)");
			P(p->g, "m.", fieldname, " = &v2");
		}
		break;
	default: fail("not implemented");
	}
}

/* The index of a required field in RequiredNumbers (panics if missing). */
static int required_bit(const int32_t *required, int n_required, int32_t number)
{
	int field_bit;
	for (field_bit = 0; field_bit < n_required; field_bit++)
		if (required[field_bit] == number)
			break;
	if (field_bit == n_required)
		fail("missing required field");
	return field_bit;
}

static void unmarshal_field(Unmarshal *p, bool proto3, const PgField *field, const PgMessage *message, const int32_t *required,
			    int n_required)
{
	const char *fieldname = field->GoName;
	const char *err_fieldname = fieldname;
	if (is_real_oneof(field))
		fieldname = field->Oneof->GoName;

	P(p->g, "case ", go_itoa(field->Desc->number), ":");
	int wire_type = proto_wire_type(field->Desc->kind);
	if (go_field_is_list(field->Desc) && wire_type != WIRE_BYTES) {
		P(p->g, "if wireType == ", go_itoa(wire_type), "{");
		unmarshal_field_item(p, field, fieldname, message, false);
		P(p->g, "} else if wireType == ", go_itoa(WIRE_BYTES), "{");
		P(p->g, "var packedLen int");
		unmarshal_decode_varint(p, "packedLen", "int");
		unmarshal_check_len(p, "packedLen", "postIndex", "iNdEx + packedLen");

		P(p->g, "var elementCount int");
		switch (field->Desc->kind) {
		case PT_DOUBLE:
		case PT_FIXED64:
		case PT_SFIXED64: P(p->g, "elementCount = packedLen/", 8); break;
		case PT_FLOAT:
		case PT_FIXED32:
		case PT_SFIXED32: P(p->g, "elementCount = packedLen/", 4); break;
		case PT_INT64:
		case PT_UINT64:
		case PT_INT32:
		case PT_UINT32:
		case PT_SINT32:
		case PT_SINT64:
			P(p->g, "var count int");
			P(p->g, "for _, integer := range dAtA[iNdEx:postIndex] {");
			P(p->g, "if integer < 128 {");
			P(p->g, "count++");
			P(p->g, "}");
			P(p->g, "}");
			P(p->g, "elementCount = count");
			break;
		case PT_BOOL: P(p->g, "elementCount = packedLen"); break;
		}

		if (vt_should_pool(p->f, message))
			P(p->g, "if elementCount != 0 && len(m.", fieldname, ") == 0 && cap(m.", fieldname, ") < elementCount {");
		else
			P(p->g, "if elementCount != 0 && len(m.", fieldname, ") == 0 {");

		const char *fieldtyp = vt_field_go_type(p->f, field, NULL);
		P(p->g, "m.", fieldname, " = make(", fieldtyp, ", 0, elementCount)");
		P(p->g, "}");

		P(p->g, "for iNdEx < postIndex {");
		unmarshal_field_item(p, field, fieldname, message, false);
		P(p->g, "}");
		P(p->g, "} else {");
		const char *errorf = vt_ident(p->f, "fmt", "Errorf");
		P(p->g, "return ", errorf, "(\"proto: wrong wireType = %d for field ", err_fieldname, "\", wireType)");
		P(p->g, "}");
	} else {
		P(p->g, "if wireType != ", go_itoa(wire_type), "{");
		const char *errorf = vt_ident(p->f, "fmt", "Errorf");
		P(p->g, "return ", errorf, "(\"proto: wrong wireType = %d for field ", err_fieldname, "\", wireType)");
		P(p->g, "}");
		unmarshal_field_item(p, field, fieldname, message, proto3);
	}

	if (field->Desc->cardinality == GO_REQUIRED) {
		int field_bit = required_bit(required, n_required, field->Desc->number);
		P(p->g, "hasFields[", go_itoa(field_bit / 64), "] |= uint64(", xfmt("0x%08llx", 1ULL << (field_bit % 64)), ")");
	}
}

static void unmarshal_message(Unmarshal *p, bool proto3, const PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		unmarshal_message(p, proto3, message->Messages[i]);
	if (message->Desc->is_map_entry)
		return;
	p->once = true;

	const char *cc_type_name = message->GoIdent.GoName;
	const int32_t *required;
	int n_required = pg_v131_message_required_numbers(message->Desc, &required);

	P(p->g, "func (m *", cc_type_name, ") ", unmarshal_method(p), "(dAtA []byte) error {");
	if (n_required > 0)
		P(p->g, "var hasFields [", go_itoa(1 + (n_required - 1) / 64), "]uint64");
	P(p->g, "l := len(dAtA)");
	P(p->g, "iNdEx := 0");
	P(p->g, "for iNdEx < l {");
	P(p->g, "preIndex := iNdEx");
	P(p->g, "var wire uint64");
	unmarshal_decode_varint(p, "wire", "uint64");
	P(p->g, "fieldNum := int32(wire >> 3)");
	P(p->g, "wireType := int(wire & 0x7)");
	P(p->g, "if wireType == ", go_itoa(WIRE_END_GROUP), " {");
	const char *errorf = vt_ident(p->f, "fmt", "Errorf");
	P(p->g, "return ", errorf, "(\"proto: ", message->GoIdent.GoName, ": wiretype end group for non-group\")");
	P(p->g, "}");
	P(p->g, "if fieldNum <= 0 {");
	errorf = vt_ident(p->f, "fmt", "Errorf");
	P(p->g, "return ", errorf, "(\"proto: ", message->GoIdent.GoName, ": illegal tag %d (wire type %d)\", fieldNum, wire)");
	P(p->g, "}");
	P(p->g, "switch fieldNum {");
	for (int i = 0; i < message->nFields; i++)
		unmarshal_field(p, proto3, message->Fields[i], message, required, n_required);
	P(p->g, "default:");
	P(p->g, "iNdEx=preIndex");
	P(p->g, "skippy, err := ", vt_helper("Skip"), "(dAtA[iNdEx:])");
	P(p->g, "if err != nil {");
	P(p->g, "return err");
	P(p->g, "}");
	P(p->g, "if (skippy < 0) || (iNdEx + skippy) < 0 {");
	P(p->g, "return ", vt_helper("ErrInvalidLength"));
	P(p->g, "}");
	P(p->g, "if (iNdEx + skippy) > l {");
	unmarshal_return_eof(p);
	P(p->g, "}");
	const GoMessage *md = message->Desc;
	if (md->n_ext_ranges > 0) {
		Buf c = {0};
		for (int e = 0; e < md->n_ext_ranges; e++) {
			if (e)
				buf_adds(&c, "||");
			buf_adds(&c, xcat("((fieldNum >= ", go_itoa(md->ext_ranges[e][0]), ") && (fieldNum < ", go_itoa(md->ext_ranges[e][1]), "))"));
		}
		P(p->g, "if ", buf_str(&c), "{");
		const char *uo = vt_ident(p->f, PROTO_PKG, "UnmarshalOptions");
		P(p->g, "err = ", uo, "{AllowPartial: true}.Unmarshal(dAtA[iNdEx:iNdEx+skippy], m)");
		P(p->g, "if err != nil {");
		P(p->g, "return err");
		P(p->g, "}");
		P(p->g, "iNdEx += skippy");
		P(p->g, "} else {");
	}
	if (!vt_wrapper(p->f))
		P(p->g, "m.unknownFields = append(m.unknownFields, dAtA[iNdEx:iNdEx+skippy]...)");
	P(p->g, "iNdEx += skippy");
	if (md->n_ext_ranges > 0)
		P(p->g, "}");
	P(p->g, "}");
	P(p->g, "}");

	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		if (field->Desc->cardinality != GO_REQUIRED)
			continue;
		int field_bit = required_bit(required, n_required, field->Desc->number);
		P(p->g, "if hasFields[", go_itoa(field_bit / 64), "] & uint64(", xfmt("0x%08llx", 1ULL << (field_bit % 64)), ") == 0 {");
		errorf = vt_ident(p->f, "fmt", "Errorf");
		P(p->g, "return ", errorf, "(\"proto: required field ", field->Desc->d.name, " not set\")");
		P(p->g, "}");
	}
	P(p->g, "");
	P(p->g, "if iNdEx > l {");
	unmarshal_return_eof(p);
	P(p->g, "}");
	P(p->g, "return nil");
	P(p->g, "}");
}

static bool unmarshal_generate(VtFile *gen, PgFile *file, bool unsafe)
{
	Unmarshal p = {gen, gen->g, unsafe, false};
	bool proto3 = file->Desc->syntax == GO_PROTO3;
	for (int i = 0; i < file->nMessages; i++)
		unmarshal_message(&p, proto3, file->Messages[i]);
	return p.once;
}

static bool unmarshal_generate_file(VtFile *gen, PgFile *file) { return unmarshal_generate(gen, file, false); }
static bool unmarshal_unsafe_generate_file(VtFile *gen, PgFile *file) { return unmarshal_generate(gen, file, true); }
