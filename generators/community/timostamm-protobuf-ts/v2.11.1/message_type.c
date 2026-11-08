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

/* Ported from packages/plugin/src/code-gen/field-info-generator.ts, message-type-generator.ts
 * and message-type-extensions/create.ts. */

/* ---- field-info-generator.ts ---- */

static TsNode *num_with_comment(int v, const char *comment)
{
	TsNode *e = NUMI(v);
	ts_add_synthetic_trailing_comment(e, TS_MultiLineCommentTrivia, str_c(comment), false);
	return e;
}

static TsNode *create_scalar_type(int t) { return num_with_comment(t, xcat("ScalarType.", scalar_name(t))); }

static TsNode *create_long_type(int l)
{
	static const char *const names[] = {"BIGINT", "STRING", "NUMBER"};
	return num_with_comment(l, xcat("LongType.", names[l]));
}

static TsNode *create_repeat_type(int r)
{
	static const char *const names[] = {"NO", "PACKED", "UNPACKED"};
	return num_with_comment(r, xcat("RepeatType.", names[r]));
}

static TsNode *create_message_t(OutFile *f, DescMessage *m)
{
	const char *name = import_type(&G.imports, f, &m->h, "default", false);
	return ARROW(ts_list_new(), ID(name));
}

static TsNode *create_enum_t(OutFile *f, DescEnum *e)
{
	RtEnumInfo *ei = get_enum_info(&G.interp, e);
	const char *name = import_type(&G.imports, f, &e->h, "default", false);
	TsNodeArray *el = ts_list(STR(ei->type_name), ID(name));
	if (ei->shared_prefix)
		ts_list_push(el, STR(ei->shared_prefix));
	return ARROW(ts_list_new(), ts_create_array_literal(el, false));
}

static TsNode *create_map_v(OutFile *f, const RtFieldInfo *fi)
{
	static const char *const kinds[] = {[RT_SCALAR] = "scalar", [RT_ENUM] = "enum", [RT_MESSAGE] = "message"};
	TsNode *T = NULL, *L = NULL;
	switch (fi->V_kind) {
	case RT_MESSAGE: get_message_type(&G.interp, fi->ref); T = create_message_t(f, fi->ref); break;
	case RT_ENUM: T = create_enum_t(f, fi->ref); break;
	case RT_SCALAR:
		T = create_scalar_type(fi->V_T);
		if (fi->V_L != RT_LONG_UNSET)
			L = create_long_type(fi->V_L);
		break;
	}
	TsNodeArray *props = ts_list(PROP("kind", STR(kinds[fi->V_kind])), PROP("T", T));
	if (L)
		ts_list_push(props, PROP("L", L));
	return ts_create_object_literal(props, false);
}

/* createFieldInfoLiteral with denormalizeFieldInfo */
static TsNode *create_field_info_literal(OutFile *f, const RtFieldInfo *fi)
{
	static const char *const kinds[] = {[RT_SCALAR] = "scalar", [RT_ENUM] = "enum", [RT_MESSAGE] = "message",
					    [RT_MAP] = "map"};
	const char *lcc = rt_lower_camel_case(fi->name);
	TsNodeArray *props = ts_list_new();
	ts_list_push(props, PROP("no", NUMI(fi->no)));
	ts_list_push(props, PROP("name", STR(fi->name)));
	ts_list_push(props, PROP("kind", STR(kinds[fi->kind])));
	if (!str_eq(fi->local_name, lcc))
		ts_list_push(props, PROP("localName", STR(fi->local_name)));
	if (!str_eq(fi->json_name, lcc))
		ts_list_push(props, PROP("jsonName", STR(fi->json_name)));
	if (fi->oneof)
		ts_list_push(props, PROP("oneof", STR(fi->oneof)));
	if (fi->repeat != RT_REPEAT_NO)
		ts_list_push(props, PROP("repeat", create_repeat_type(fi->repeat)));
	if (fi->opt && fi->kind != RT_MESSAGE)
		ts_list_push(props, PROP("opt", ts_create_true()));
	switch (fi->kind) {
	case RT_SCALAR:
		ts_list_push(props, PROP("T", create_scalar_type(fi->T)));
		if (fi->L != RT_LONG_UNSET)
			ts_list_push(props, PROP("L", create_long_type(fi->L)));
		break;
	case RT_ENUM: ts_list_push(props, PROP("T", create_enum_t(f, fi->ref))); break;
	case RT_MESSAGE:
		get_message_type(&G.interp, fi->ref);
		ts_list_push(props, PROP("T", create_message_t(f, fi->ref)));
		break;
	case RT_MAP:
		ts_list_push(props, PROP("K", create_scalar_type(fi->K)));
		ts_list_push(props, PROP("V", create_map_v(f, fi)));
		break;
	}
	if (fi->options)
		ts_list_push(props, PROP("options", literal_from_value(fi->options)));
	return ts_create_object_literal(props, false);
}

static TsNode *create_field_info_literals(OutFile *f, const RtMessageType *t)
{
	TsNodeArray *el = ts_list_new();
	for (int i = 0; i < t->n_fields; i++)
		ts_list_push(el, create_field_info_literal(f, t->fields[i]));
	return ts_create_array_literal(el, true);
}

/* ---- message-type-extensions/create.ts ---- */

static TsNode *create_make_method(OutFile *f, DescMessage *m, TsNodeArray *body)
{
	const char *iface = import_type(&G.imports, f, &m->h, "default", false);
	const char *partial = import_name(f, "PartialMessage", G.o->runtime_import_path, true);
	TsNode *param = PARAM("value", TOK(TS_QuestionToken), ts_create_type_reference_node(ID(partial), ts_list(TREF(iface))));
	return ts_create_method(NULL, NULL, NULL, ID("create"), NULL, NULL, ts_list(param), TREF(iface),
				ts_create_block(body, true));
}

static TsNodeArray *create_method_create(OutFile *f, DescMessage *m)
{
	TsNodeArray *body = ts_list_new();
	/* const message = globalThis.Object.create((this.messagePrototype!)); */
	TsNode *proto = ts_create_non_null_expression(PA(THIS(), "messagePrototype"));
	ts_list_push(body, VARS(TS_NodeFlags_Const,
				ts_list(VAR("message", NULL, CALL(PA(PA(ID("globalThis"), "Object"), "create"), ts_list(proto))))));
	/* message.<field> = <default>; */
	JsVal *def = rt_create(get_message_type(&G.interp, m));
	Str *keys;
	int n = js_obj_keys(def, &keys);
	for (int i = 0; i < n; i++)
		ts_list_push(body, EXPR(BIN(PA(ID("message"), xstrndup(keys[i].p, keys[i].n)), TS_EqualsToken,
					    literal_from_value(js_obj_get(def, keys[i])))));
	/* if (value !== undefined) reflectionMergePartial<Message>(this, message, value); */
	const char *iface = import_type(&G.imports, f, &m->h, "default", false);
	const char *merge = import_name(f, "reflectionMergePartial", G.o->runtime_import_path, false);
	ts_list_push(body, ts_create_if(BIN(ID("value"), TS_ExclamationEqualsEqualsToken, ID("undefined")),
					EXPR(ts_create_call(ID(merge), ts_list(TREF(iface)),
							    ts_list(THIS(), ID("message"), ID("value")))),
					NULL));
	ts_list_push(body, ts_create_return(ID("message")));
	return ts_list(create_make_method(f, m, body));
}

/* ---- message-type-generator.ts ---- */

static TsNodeArray *wkt_make(OutFile *f, DescMessage *m);
static TsNodeArray *google_types_make(OutFile *f, DescMessage *m);
static TsNodeArray *internal_binary_read_make(OutFile *f, DescMessage *m);
static TsNodeArray *internal_binary_write_make(OutFile *f, DescMessage *m);

static void push_all(TsNodeArray *to, TsNodeArray *from)
{
	for (int i = 0; from && i < from->n; i++)
		ts_list_push(to, from->v[i]);
}

/* generateMessageType */
static void generate_message_type(OutFile *f, DescMessage *m, int optimize_for)
{
	const char *my_message = import_type(&G.imports, f, &m->h, "default", false);
	TsNode *message_type_id = ID(xcat(import_type(&G.imports, f, &m->h, "default", false), "$Type"));
	TsNode *message_type = ID(import_name(f, "MessageType", G.o->runtime_import_path, false));
	RtMessageType *t = get_message_type(&G.interp, m);
	TsNodeArray *super_args = ts_list(STR(m->type_name));
	ts_list_push(super_args, create_field_info_literals(f, t));
	Str *keys;
	if (js_obj_keys(t->options, &keys))
		ts_list_push(super_args, literal_from_value(t->options));
	TsNodeArray *members = ts_list(ts_create_constructor(
		NULL, NULL, ts_list_new(), ts_create_block(ts_list(EXPR(CALL(ts_create_super(), super_args))), true)));
	push_all(members, wkt_make(f, m));
	push_all(members, google_types_make(f, m));
	if (optimize_for == OPTIMIZE_SPEED) {
		push_all(members, create_method_create(f, m));
		push_all(members, internal_binary_read_make(f, m));
		push_all(members, internal_binary_write_make(f, m));
	}
	TsNode *cls = ts_create_class_declaration(
		NULL, NULL, message_type_id, NULL,
		ts_list(ts_create_heritage_clause(
			TS_ExtendsKeyword, ts_list(ts_create_expression_with_type_arguments(ts_list(TREF(my_message)), message_type)))),
		members);
	TsNode *export_const = ts_create_variable_statement(
		ts_list(TOK(TS_ExportKeyword)),
		ts_create_variable_declaration_list(
			ts_list(ts_create_variable_declaration(ID(my_message), NULL, ts_create_new(message_type_id, NULL, ts_list_new()))),
			TS_NodeFlags_Const));
	add_statement(f, cls, false);
	add_statement(f, export_const, false);
	ts_add_synthetic_leading_comment(
		cls, TS_SingleLineCommentTrivia,
		STR_LIT(" @generated message type with reflection information, may provide speed optimized methods"), false);
	Buf c = {0};
	buf_adds(&c, make_deprecated_tag(&m->h));
	buf_adds(&c, "@generated MessageType for ");
	Str tag = make_generated_tag(&m->h); /* "@generated from protobuf message ..." */
	buf_add(&c, tag.p + 16, tag.n - 16);
	add_comment_block_as_js_doc(export_const, buf_view(&c));
}
