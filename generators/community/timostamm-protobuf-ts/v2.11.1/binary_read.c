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

/* Ported from packages/plugin/src/message-type-extensions/internal-binary-read.ts. Imports are
 * added in upstream's evaluation order (they are listed in reverse order of first use). */

static const char *scalar_method_name(int t)
{
	const char *n = scalar_name(t);
	char *l = xstrdup(n);
	for (char *p = l; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p + 32);
	return l;
}

/* makeReaderCall */
static TsNode *make_reader_call(const char *reader, int type, int long_type)
{
	TsNode *call = CALL(PA(ID(reader), scalar_method_name(type)), NULL);
	if (!is_long_value_type(type))
		return call;
	const char *m = long_type == RT_LONG_NUMBER ? "toNumber" : long_type == RT_LONG_BIGINT ? "toBigInt" : "toString";
	return CALL(PA(call, m), NULL);
}

static TsNode *field_access(const char *local_name) { return PA(ID("message"), local_name); }

/* let [fieldNo, wireType] = reader.tag(); */
static TsNode *read_tag_statement(void)
{
	TsNode *pattern = ts_create_array_binding_pattern(ts_list(ts_create_binding_element(NULL, NULL, ID("fieldNo"), NULL),
								  ts_create_binding_element(NULL, NULL, ID("wireType"), NULL)));
	return VARS(TS_NodeFlags_Let, ts_list(ts_create_variable_declaration(pattern, NULL, CALL(PA(ID("reader"), "tag"), NULL))));
}

static TsNode *read_message_call(OutFile *f, DescMessage *m, TsNode *target)
{
	const char *name = import_type(&G.imports, f, &m->h, "default", false);
	TsNodeArray *args = ts_list(ID("reader"), make_reader_call("reader", PT_UINT32, RT_LONG_UNSET), ID("options"));
	if (target)
		ts_list_push(args, target);
	return CALL(PA(ID(name), "internalBinaryRead"), args);
}

static TsNodeArray *case_statements(OutFile *f, const RtFieldInfo *fi)
{
	TsNode *access = field_access(fi->local_name);
	int type = fi->kind == RT_ENUM ? PT_INT32 : fi->T;
	int lt = fi->kind == RT_ENUM ? RT_LONG_UNSET : fi->L;
	switch (fi->kind) {
	case RT_SCALAR:
	case RT_ENUM:
		if (fi->repeat) {
			if (type == PT_STRING || type == PT_BYTES)
				return ts_list(EXPR(CALL(PA(access, "push"), ts_list(make_reader_call("reader", type, lt)))));
			const char *wire_type = import_name(f, "WireType", G.o->runtime_import_path, false);
			TsNode *loop = ts_create_for(
				ts_create_variable_declaration_list(
					ts_list(VAR("e", NULL, BIN(make_reader_call("reader", PT_INT32, RT_LONG_UNSET), TS_PlusToken,
								  PA(ID("reader"), "pos")))),
					TS_NodeFlags_Let),
				BIN(PA(ID("reader"), "pos"), TS_LessThanToken, ID("e")), NULL,
				EXPR(CALL(PA(access, "push"), ts_list(make_reader_call("reader", type, lt)))));
			return ts_list(ts_create_if(BIN(ID("wireType"), TS_EqualsEqualsEqualsToken, PA(ID(wire_type), "LengthDelimited")),
						    loop, EXPR(CALL(PA(access, "push"), ts_list(make_reader_call("reader", type, lt))))));
		}
		if (fi->oneof)
			return ts_list(EXPR(BIN(field_access(fi->oneof), TS_EqualsToken,
						ts_create_object_literal(ts_list(PROP(G.o->oneof_kind_discriminator, STR(fi->local_name)),
										 PROP(fi->local_name, make_reader_call("reader", type, lt))),
									 true))));
		return ts_list(EXPR(BIN(access, TS_EqualsToken, make_reader_call("reader", type, lt))));
	case RT_MESSAGE:
		if (fi->repeat)
			return ts_list(EXPR(CALL(PA(access, "push"), ts_list(read_message_call(f, fi->ref, NULL)))));
		if (fi->oneof) {
			TsNode *prev = PA(ts_create_paren(ts_create_as_expression(field_access(fi->oneof), KW(TS_AnyKeyword))), fi->local_name);
			TsNode *call = read_message_call(f, fi->ref, prev);
			return ts_list(EXPR(BIN(field_access(fi->oneof), TS_EqualsToken,
						ts_create_object_literal(ts_list(PROP(G.o->oneof_kind_discriminator, STR(fi->local_name)),
										 PROP(fi->local_name, call)),
									 true))));
		}
		{
			TsNode *call = read_message_call(f, fi->ref, access);
			return ts_list(EXPR(BIN(field_access(fi->local_name), TS_EqualsToken, call)));
		}
	case RT_MAP:
		return ts_list(EXPR(CALL(PA(THIS(), xfmt("binaryReadMap%d", fi->no)), ts_list(access, ID("reader"), ID("options")))));
	}
	return ts_list_new();
}

/* " " + getDeclarationString(descField).replace(/= \d+$/, '').replace(/]$/, '] ') */
static Str case_comment(const DescField *field)
{
	Str d = declaration_string(&field->h);
	size_t n = d.n, k = n;
	while (k > 0 && d.p[k - 1] >= '0' && d.p[k - 1] <= '9')
		k--;
	if (k < n && k >= 2 && d.p[k - 1] == ' ' && d.p[k - 2] == '=')
		n = k - 2;
	Buf b = {0};
	buf_addc(&b, ' ');
	buf_add(&b, d.p, n);
	if (n && d.p[n - 1] == ']')
		buf_addc(&b, ' ');
	return buf_view(&b);
}

static TsNode *default_clause(OutFile *f)
{
	const char *handler = import_name(f, "UnknownFieldHandler", G.o->runtime_import_path, false);
	TsNode *tmpl = ts_create_template_expression(
		ts_create_template_literal_like_node(TS_TemplateHead, STR_LIT("Unknown field "), true),
		ts_list(ts_create_template_span(ID("fieldNo"),
						ts_create_template_literal_like_node(TS_TemplateMiddle, STR_LIT(" (wire type "), true)),
			ts_create_template_span(ID("wireType"),
						ts_create_template_literal_like_node(TS_TemplateMiddle, STR_LIT(") for "), true)),
			ts_create_template_span(PA(THIS(), "typeName"),
						ts_create_template_literal_like_node(TS_TemplateTail, STR_LIT(""), true))));
	return ts_create_default_clause(ts_list(
		VARS(TS_NodeFlags_Let, ts_list(VAR("u", NULL, PA(ID("options"), "readUnknownField")))),
		ts_create_if(BIN(ID("u"), TS_EqualsEqualsEqualsToken, STR("throw")),
			     ts_create_throw(ts_create_new(PA(ID("globalThis"), "Error"), NULL, ts_list(tmpl))), NULL),
		VARS(TS_NodeFlags_Let, ts_list(VAR("d", NULL, CALL(PA(ID("reader"), "skip"), ts_list(ID("wireType")))))),
		ts_create_if(BIN(ID("u"), TS_ExclamationEqualsEqualsToken, ts_create_false()),
			     EXPR(CALL(ts_create_paren(ts_create_conditional(BIN(ID("u"), TS_EqualsEqualsEqualsToken, ts_create_true()),
									     PA(ID(handler), "onRead"), ID("u"))),
				       ts_list(PA(THIS(), "typeName"), ID("message"), ID("fieldNo"), ID("wireType"), ID("d")))),
			     NULL)));
}

/* createScalarDefaultValue of a map key, as a property key */
static TsNode *map_key_default(int type)
{
	JsVal *v = rt_scalar_default(type, RT_LONG_UNSET);
	if (v->t == JS_BOOL)
		v = js_string(str_c(v->b ? "true" : "false"));
	return literal_from_value(v);
}

static TsNode *map_value_default(OutFile *f, const RtFieldInfo *fi)
{
	switch (fi->V_kind) {
	case RT_SCALAR: return literal_from_value(rt_scalar_default(fi->V_T, fi->V_L));
	case RT_ENUM: return literal_from_value(rt_scalar_default(PT_INT32, RT_LONG_UNSET));
	default: {
		const char *name = import_type(&G.imports, f, fi->ref, "default", false);
		return CALL(PA(ID(name), "create"), NULL);
	}
	}
}

/* makeMapEntryReadMethod */
static TsNode *map_entry_read_method(OutFile *f, DescMessage *m, const RtFieldInfo *fi)
{
	const char *method = xfmt("binaryReadMap%d", fi->no);
	const char *iface = import_type(&G.imports, f, &m->h, "default", false);
	const char *reader_t = import_name(f, "IBinaryReader", G.o->runtime_import_path, true);
	const char *options_t = import_name(f, "BinaryReadOptions", G.o->runtime_import_path, true);
	const char *qualified = xcat(m->type_name, ".", fi->name);
	TsNodeArray *body = ts_list_new();
	TsNode *field_type = ts_create_indexed_access_type_node(TREF(iface), ts_create_literal_type_node(STR(fi->local_name)));
	ts_list_push(body, VARS(TS_NodeFlags_Let,
				ts_list(VAR("len", NULL, CALL(PA(ID("reader"), "uint32"), NULL)),
					VAR("end", NULL, BIN(PA(ID("reader"), "pos"), TS_PlusToken, ID("len"))),
					VAR("key", ts_create_union_type_node(ts_list(ts_create_type_operator_node(0, field_type), KW(TS_UndefinedKeyword))), NULL),
					VAR("val", ts_create_union_type_node(ts_list(ts_create_indexed_access_type_node(
										     ts_create_indexed_access_type_node(TREF(iface), ts_create_literal_type_node(STR(fi->local_name))),
										     KW(TS_AnyKeyword)),
									     KW(TS_UndefinedKeyword))),
					    NULL))));
	TsNode *read_key = make_reader_call("reader", fi->K, RT_LONG_STRING);
	if (fi->K == PT_BOOL)
		read_key = CALL(PA(read_key, "toString"), NULL);
	TsNode *read_value;
	switch (fi->V_kind) {
	case RT_SCALAR: read_value = make_reader_call("reader", fi->V_T, fi->V_L); break;
	case RT_ENUM: read_value = make_reader_call("reader", PT_INT32, RT_LONG_UNSET); break;
	default: {
		const char *name = import_type(&G.imports, f, fi->ref, "default", false);
		read_value = CALL(PA(ID(name), "internalBinaryRead"),
				  ts_list(ID("reader"), make_reader_call("reader", PT_UINT32, RT_LONG_UNSET), ID("options")));
	}
	}
	TsNode *c1 = ts_create_case_clause(NUM("1"), ts_list(EXPR(BIN(ID("key"), TS_EqualsToken, read_key)), ts_create_break(NULL)));
	TsNode *c2 = ts_create_case_clause(NUM("2"), ts_list(EXPR(BIN(ID("val"), TS_EqualsToken, read_value)), ts_create_break(NULL)));
	TsNode *err = ts_create_new(PA(ID("globalThis"), "Error"), NULL, ts_list(STR(xcat("unknown map entry field for ", qualified))));
	TsNode *c3 = ts_create_default_clause(ts_list(ts_create_throw(err)));
	TsNode *sw = ts_create_switch(ID("fieldNo"), ts_create_case_block(ts_list(c1, c2, c3)));
	ts_list_push(body, ts_create_while(BIN(PA(ID("reader"), "pos"), TS_LessThanToken, ID("end")),
					   ts_create_block(ts_list(read_tag_statement(), sw), true)));
	TsNode *key_default = map_key_default(fi->K);
	TsNode *value_default = map_value_default(f, fi);
	ts_list_push(body, EXPR(BIN(ts_create_element_access(ID("map"), BIN(ID("key"), TS_QuestionQuestionToken, key_default)),
				    TS_EqualsToken, BIN(ID("val"), TS_QuestionQuestionToken, value_default))));
	TsNodeArray *params = ts_list(
		PARAM("map", NULL, ts_create_indexed_access_type_node(TREF(iface), ts_create_literal_type_node(STR(fi->local_name)))),
		PARAM("reader", NULL, TREF(reader_t)), PARAM("options", NULL, TREF(options_t)));
	return ts_create_method(NULL, ts_list(TOK(TS_PrivateKeyword)), NULL, ID(method), NULL, NULL, params,
				KW(TS_VoidKeyword), ts_create_block(body, true));
}

static TsNodeArray *internal_binary_read_make(OutFile *f, DescMessage *m)
{
	RtMessageType *t = get_message_type(&G.interp, m);
	/* let message = target ?? this.create(), end = reader.pos + length; */
	TsNode *vars = VARS(TS_NodeFlags_Let,
			    ts_list(VAR("message", NULL, BIN(ID("target"), TS_QuestionQuestionToken, CALL(PA(THIS(), "create"), NULL))),
				    VAR("end", NULL, BIN(PA(ID("reader"), "pos"), TS_PlusToken, ID("length")))));
	TsNodeArray *clauses = ts_list_new();
	for (int i = 0; i < t->n_fields; i++) {
		RtFieldInfo *fi = t->fields[i];
		TsNodeArray *st = case_statements(f, fi);
		DescField *field = field_by_number(m, fi->no);
		TsNode *no = NUMI(fi->no);
		ts_add_synthetic_leading_comment(no, TS_MultiLineCommentTrivia, case_comment(field), false);
		ts_list_push(st, ts_create_break(NULL));
		ts_list_push(clauses, ts_create_case_clause(no, st));
	}
	ts_list_push(clauses, default_clause(f));
	TsNode *loop = ts_create_while(BIN(PA(ID("reader"), "pos"), TS_LessThanToken, ID("end")),
				       ts_create_block(ts_list(read_tag_statement(), ts_create_switch(ID("fieldNo"), ts_create_case_block(clauses))), true));
	TsNodeArray *body = ts_list(vars, loop, ts_create_return(ID("message")));
	const char *iface = import_type(&G.imports, f, &m->h, "default", false);
	const char *reader_t = import_name(f, "IBinaryReader", G.o->runtime_import_path, true);
	const char *options_t = import_name(f, "BinaryReadOptions", G.o->runtime_import_path, true);
	TsNodeArray *params = ts_list(PARAM("reader", NULL, TREF(reader_t)), PARAM("length", NULL, KW(TS_NumberKeyword)),
				      PARAM("options", NULL, TREF(options_t)), PARAM("target", TOK(TS_QuestionToken), TREF(iface)));
	TsNodeArray *methods = ts_list(ts_create_method(NULL, NULL, NULL, ID("internalBinaryRead"), NULL, NULL, params,
							TREF(iface), ts_create_block(body, true)));
	for (int i = 0; i < t->n_fields; i++)
		if (t->fields[i]->kind == RT_MAP)
			ts_list_push(methods, map_entry_read_method(f, m, t->fields[i]));
	return methods;
}
