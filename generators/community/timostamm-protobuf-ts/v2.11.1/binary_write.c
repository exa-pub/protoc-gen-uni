/* Copyright (c) 2016 Timo Stamm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/plugin/src/message-type-extensions/internal-binary-write.ts. Imports are
 * added in upstream's evaluation order. */

enum { WIRE_VARINT = 0, WIRE_BIT64 = 1, WIRE_LENGTH_DELIMITED = 2, WIRE_BIT32 = 5 };

/* makeWriterCall: writer.<method>(argument) */
static TsNode *writer_call(TsNode *writer, const char *method, TsNode *argument)
{
	return CALL(PA(writer, method), argument ? ts_list(argument) : NULL);
}

/* makeWriterTagCall: writer.tag(<no>, WireType.<name>) */
static TsNode *writer_tag_call(OutFile *f, TsNode *writer, int no, int wire_type)
{
	TsNode *prop = PA(writer, "tag");
	const char *name = wire_type == WIRE_LENGTH_DELIMITED ? "LengthDelimited"
			   : wire_type == WIRE_BIT64	  ? "Bit64"
			   : wire_type == WIRE_BIT32	  ? "Bit32"
							  : "Varint";
	TsNode *access = PA(ID(import_name(f, "WireType", G.o->runtime_import_path, false)), name);
	return CALL(prop, ts_list(NUMI(no), access));
}

/* wireTypeForSingleScalar */
static int wire_type_for_single_scalar(int t)
{
	switch (t) {
	case PT_BYTES: case PT_STRING: return WIRE_LENGTH_DELIMITED;
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WIRE_BIT64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WIRE_BIT32;
	default: return WIRE_VARINT;
	}
}

static TsNode *with_comment(TsNode *st, const char *decl)
{
	ts_add_synthetic_leading_comment(st, TS_MultiLineCommentTrivia, str_c(xcat(" ", decl, " ")), true);
	return st;
}

/* for (let i = 0; i < <access>.length; i++) <body> */
static TsNode *for_each_index(TsNode *access, TsNode *body)
{
	return ts_create_for(ts_create_variable_declaration_list(ts_list(VAR("i", NULL, NUM("0"))), TS_NodeFlags_Let),
			     BIN(ID("i"), TS_LessThanToken, PA(access, "length")), ts_create_postfix(ID("i"), TS_PlusPlusToken),
			     body);
}

static TsNode *write_message_call(OutFile *f, const RtFieldInfo *fi, TsNode *value)
{
	TsNode *tag_fork = writer_call(writer_tag_call(f, ID("writer"), fi->no, WIRE_LENGTH_DELIMITED), "fork", NULL);
	const char *name = import_type(&G.imports, f, fi->ref, "default", false);
	TsNode *write = CALL(PA(ID(name), "internalBinaryWrite"), ts_list(value, tag_fork, ID("options")));
	return writer_call(write, "join", NULL);
}

static TsNode *write_map(OutFile *f, const RtFieldInfo *fi, TsNode *access)
{
	TsNode *key_read, *value_read = ts_create_element_access(access, ID("k"));
	switch (fi->K) {
	case PT_BOOL: key_read = BIN(ID("k"), TS_EqualsEqualsEqualsToken, STR("true")); break;
	case PT_INT32: case PT_SINT32: case PT_UINT32: case PT_FIXED32: case PT_SFIXED32:
		key_read = CALL(ID("parseInt"), ts_list(ID("k")));
		value_read = ts_create_element_access(access, ts_create_as_expression(ID("k"), KW(TS_AnyKeyword)));
		break;
	default: key_read = ID("k");
	}
	TsNode *body;
	if (fi->V_kind == RT_MESSAGE) {
		TsNode *entry = writer_call(writer_tag_call(f, ID("writer"), fi->no, WIRE_LENGTH_DELIMITED), "fork", NULL);
		TsNode *s1 = EXPR(writer_call(writer_tag_call(f, entry, 1, wire_type_for_single_scalar(fi->K)), scalar_method_name(fi->K),
					      key_read));
		TsNode *s2 = EXPR(writer_call(writer_tag_call(f, ID("writer"), 2, WIRE_LENGTH_DELIMITED), "fork", NULL));
		const char *name = import_type(&G.imports, f, fi->ref, "default", false);
		TsNode *s3 = EXPR(CALL(PA(ID(name), "internalBinaryWrite"), ts_list(value_read, ID("writer"), ID("options"))));
		TsNode *s4 = EXPR(writer_call(writer_call(ID("writer"), "join", NULL), "join", NULL));
		body = ts_create_block(ts_list(s1, s2, s3, s4), true);
	} else {
		int vt = fi->V_kind == RT_ENUM ? PT_INT32 : fi->V_T;
		TsNode *entry = writer_call(writer_tag_call(f, ID("writer"), fi->no, WIRE_LENGTH_DELIMITED), "fork", NULL);
		TsNode *key = writer_call(writer_tag_call(f, entry, 1, wire_type_for_single_scalar(fi->K)), scalar_method_name(fi->K),
					  key_read);
		TsNode *value = writer_call(writer_tag_call(f, key, 2, wire_type_for_single_scalar(vt)), scalar_method_name(vt),
					    value_read);
		body = EXPR(writer_call(value, "join", NULL));
	}
	return ts_create_for_of(NULL, ts_create_variable_declaration_list(ts_list(VAR("k", NULL, NULL)), TS_NodeFlags_Let),
				CALL(PA(PA(ID("globalThis"), "Object"), "keys"), ts_list(access)), body);
}

static TsNode *write_field(OutFile *f, const RtFieldInfo *fi, const char *decl)
{
	TsNode *access = field_access(fi->local_name);
	int type = fi->kind == RT_ENUM ? PT_INT32 : fi->T;
	switch (fi->kind) {
	case RT_SCALAR:
	case RT_ENUM:
		if (fi->repeat == RT_REPEAT_PACKED) {
			TsNode *s1 = EXPR(writer_call(writer_tag_call(f, ID("writer"), fi->no, WIRE_LENGTH_DELIMITED), "fork", NULL));
			TsNode *s2 = for_each_index(access, EXPR(writer_call(ID("writer"), scalar_method_name(type),
									      ts_create_element_access(access, ID("i")))));
			TsNode *s3 = EXPR(writer_call(ID("writer"), "join", NULL));
			return with_comment(ts_create_if(PA(access, "length"), ts_create_block(ts_list(s1, s2, s3), true), NULL), decl);
		}
		if (fi->repeat) {
			TsNode *tag = writer_tag_call(f, ID("writer"), fi->no, wire_type_for_single_scalar(type));
			return with_comment(for_each_index(access, EXPR(writer_call(tag, scalar_method_name(type),
										    ts_create_element_access(access, ID("i"))))),
					    decl);
		}
		if (fi->oneof) {
			TsNode *group = PA(ID("message"), fi->oneof);
			TsNode *cond = BIN(PA(group, G.o->oneof_kind_discriminator), TS_EqualsEqualsEqualsToken, STR(fi->local_name));
			TsNode *tag = writer_tag_call(f, ID("writer"), fi->no, wire_type_for_single_scalar(type));
			return with_comment(ts_create_if(cond, EXPR(writer_call(tag, scalar_method_name(type), PA(group, fi->local_name))), NULL),
					    decl);
		}
		{
			TsNode *cond;
			if (fi->kind == RT_SCALAR && fi->T == PT_BYTES && fi->opt) {
				cond = BIN(access, TS_ExclamationEqualsEqualsToken, ID("undefined"));
			} else if (fi->kind == RT_SCALAR && fi->T == PT_BYTES) {
				cond = PA(access, "length");
			} else {
				JsVal *def = NULL;
				if (!fi->opt)
					def = fi->kind == RT_ENUM ? js_number(0) : rt_scalar_default(fi->T, fi->L);
				cond = BIN(access, TS_ExclamationEqualsEqualsToken, literal_from_value(def));
			}
			TsNode *tag = writer_tag_call(f, ID("writer"), fi->no, wire_type_for_single_scalar(type));
			return with_comment(ts_create_if(cond, EXPR(writer_call(tag, scalar_method_name(type), access)), NULL), decl);
		}
	case RT_MESSAGE:
		if (fi->repeat)
			return with_comment(for_each_index(access, EXPR(write_message_call(f, fi, ts_create_element_access(access, ID("i"))))),
					    decl);
		if (fi->oneof) {
			TsNode *group = PA(ID("message"), fi->oneof);
			TsNode *call = write_message_call(f, fi, PA(group, fi->local_name));
			TsNode *cond = BIN(PA(group, G.o->oneof_kind_discriminator), TS_EqualsEqualsEqualsToken, STR(fi->local_name));
			return with_comment(ts_create_if(cond, EXPR(call), NULL), decl);
		}
		return with_comment(ts_create_if(access, EXPR(write_message_call(f, fi, access)), NULL), decl);
	case RT_MAP: return with_comment(write_map(f, fi, access), decl);
	}
	return NULL;
}

static TsNodeArray *internal_binary_write_make(OutFile *f, DescMessage *m)
{
	RtMessageType *t = get_message_type(&G.interp, m);
	/* interpreterType.fields.concat().sort((a, b) => a.no - b.no): stable */
	int n = t->n_fields;
	RtFieldInfo **sorted = xalloc(sizeof *sorted * (size_t)(n + 1));
	memcpy(sorted, t->fields, sizeof *sorted * (size_t)n);
	for (int w = 1; w < n; w *= 2) {
		RtFieldInfo **tmp = xalloc(sizeof *tmp * (size_t)(n + 1));
		for (int lo = 0; lo < n; lo += 2 * w) {
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				tmp[k++] = sorted[j]->no < sorted[i]->no ? sorted[j++] : sorted[i++];
			while (i < mid)
				tmp[k++] = sorted[i++];
			while (j < hi)
				tmp[k++] = sorted[j++];
		}
		sorted = tmp;
	}
	TsNodeArray *body = ts_list_new();
	for (int i = 0; i < n; i++) {
		DescField *field = field_by_number(m, sorted[i]->no);
		Str d = declaration_string(&field->h);
		const char *decl = xcat(xstrndup(d.p, d.n), ";");
		ts_list_push(body, write_field(f, sorted[i], decl));
	}
	const char *handler = import_name(f, "UnknownFieldHandler", G.o->runtime_import_path, false);
	ts_list_push(body, VARS(TS_NodeFlags_Let, ts_list(VAR("u", NULL, PA(ID("options"), "writeUnknownFields")))));
	ts_list_push(body, ts_create_if(BIN(ID("u"), TS_ExclamationEqualsEqualsToken, ts_create_false()),
					EXPR(CALL(ts_create_paren(ts_create_conditional(BIN(ID("u"), TS_EqualsEqualsToken, ts_create_true()),
											PA(ID(handler), "onWrite"), ID("u"))),
						  ts_list(PA(THIS(), "typeName"), ID("message"), ID("writer")))),
					NULL));
	ts_list_push(body, ts_create_return(ID("writer")));
	const char *iface = import_type(&G.imports, f, &m->h, "default", false);
	const char *writer_t = import_name(f, "IBinaryWriter", G.o->runtime_import_path, true);
	const char *options_t = import_name(f, "BinaryWriteOptions", G.o->runtime_import_path, true);
	TsNodeArray *params = ts_list(PARAM("message", NULL, TREF(iface)), PARAM("writer", NULL, TREF(writer_t)),
				      PARAM("options", NULL, TREF(options_t)));
	return ts_list(ts_create_method(NULL, NULL, NULL, ID("internalBinaryWrite"), NULL, NULL, params, TREF(writer_t),
					ts_create_block(body, true)));
}
