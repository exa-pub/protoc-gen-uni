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

/* Ported from packages/plugin/src/code-gen/local-type-name.ts, message-interface-generator.ts
 * and enum-generator.ts. */

/* createLocalTypeName */
static const char *create_local_type_name(const Desc *d)
{
	const char *full = desc_type_name(d);
	const char *pkg = pb_str(desc_file_of(d)->h.proto, FDP_PACKAGE);
	if (*pkg)
		full += strlen(pkg) + 1 <= strlen(full) ? strlen(pkg) + 1 : strlen(full);
	char *name = xstrdup(full);
	for (char *p = name; *p; p++)
		if (*p == '.')
			*p = '_';
	static const char *const keywords[] = {
		"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
		"enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof",
		"new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void",
		"while", "with", "as", "implements", "interface", "let", "package", "private", "protected", "public",
		"static", "yield", "any", "boolean", "constructor", "declare", "get", "module", "require", "number",
		"set", "string", "symbol", "type", "from", "of",
	};
	static const char *const type_names[] = {
		"object", "Uint8Array", "array", "Array", "string", "String", "number", "Number", "boolean", "Boolean",
		"bigint", "BigInt",
	};
	const char *r = name;
	for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++)
		if (str_eq(keywords[i], r)) {
			r = xcat(r, "$");
			break;
		}
	for (size_t i = 0; i < sizeof type_names / sizeof *type_names; i++)
		if (str_eq(type_names[i], r)) {
			r = xcat(r, "$");
			break;
		}
	return r;
}

/* descMessage.fields.find(f => f.number === no), indexed */
static DescField *field_by_number(DescMessage *m, int no)
{
	const char *key = xfmt("m%llx", (unsigned long long)(uintptr_t)m);
	Map *idx = map_get(&G.index, key);
	if (!idx) {
		idx = xalloc(sizeof *idx);
		for (int i = 0; i < m->fields.n; i++) {
			const char *k = xfmt("%d", ((DescField *)m->fields.v[i])->number);
			if (!map_has(idx, k))
				map_put(idx, k, m->fields.v[i]);
		}
		map_put(&G.index, key, idx);
	}
	DescField *f = map_get(idx, xfmt("%d", no));
	if (!f)
		fail("Error: assertion failed");
	return f;
}

/* descEnum.values.find(v => v.number === no), indexed */
static DescEnumValue *enum_value_by_number(DescEnum *e, int no)
{
	const char *key = xfmt("e%llx", (unsigned long long)(uintptr_t)e);
	Map *idx = map_get(&G.index, key);
	if (!idx) {
		idx = xalloc(sizeof *idx);
		for (int i = 0; i < e->values.n; i++) {
			const char *k = xfmt("%d", ((DescEnumValue *)e->values.v[i])->number);
			if (!map_has(idx, k))
				map_put(idx, k, e->values.v[i]);
		}
		map_put(&G.index, key, idx);
	}
	return map_get(idx, xfmt("%d", no));
}

/* MessageInterfaceGenerator.registerSymbols */
static void message_interface_register_symbols(OutFile *f, DescMessage *m)
{
	symbols_register(&G.symbols, create_local_type_name(&m->h), &m->h, f, "default");
}

/* createScalarTypeNode */
static TsNode *create_scalar_type_node(int t, int long_type)
{
	switch (t) {
	case PT_BOOL: return KW(TS_BooleanKeyword);
	case PT_STRING: return KW(TS_StringKeyword);
	case PT_BYTES: return TREF("Uint8Array");
	case PT_SFIXED64: case PT_INT64: case PT_UINT64: case PT_FIXED64: case PT_SINT64:
		switch (long_type == RT_LONG_UNSET ? RT_LONG_STRING : long_type) {
		case RT_LONG_NUMBER: return KW(TS_NumberKeyword);
		case RT_LONG_BIGINT: return KW(TS_BigIntKeyword);
		default: return KW(TS_StringKeyword);
		}
	default: return KW(TS_NumberKeyword);
	}
}

static TsNode *create_ref_type_node(OutFile *f, void *ref)
{
	return TREF(import_type(&G.imports, f, ref, "default", false));
}

static TsNode *create_field_property_signature(OutFile *f, DescField *field, RtFieldInfo *fi)
{
	TsNode *type = NULL;
	switch (fi->kind) {
	case RT_SCALAR: type = create_scalar_type_node(fi->T, fi->L); break;
	case RT_ENUM:
	case RT_MESSAGE: type = create_ref_type_node(f, fi->ref); break;
	case RT_MAP: {
		TsNode *key = fi->K == PT_BOOL ? KW(TS_StringKeyword) : create_scalar_type_node(fi->K, RT_LONG_STRING);
		TsNode *value = fi->V_kind == RT_SCALAR ? create_scalar_type_node(fi->V_T, fi->V_L) : create_ref_type_node(f, fi->ref);
		type = ts_create_type_literal_node(
			ts_list(ts_create_index_signature(NULL, NULL, ts_list(PARAM("key", NULL, key)), value)));
		break;
	}
	}
	if (fi->repeat)
		type = ts_create_array_type_node(type);
	TsNode *q = fi->opt ? TOK(TS_QuestionToken) : NULL;
	TsNode *prop = ts_create_property_signature(NULL, ID(fi->local_name), q, type, NULL);
	add_comments_for_descriptor(prop, &field->h, TRAILING_LINES);
	return prop;
}

static TsNode *create_oneof_adt_property_signature(OutFile *f, DescMessage *m, DescOneof *oneof)
{
	RtMessageType *t = get_message_type(&G.interp, oneof->parent);
	/* the field infos by number (the first one) and by oneof name, indexed */
	const char *key = xfmt("t%llx", (unsigned long long)(uintptr_t)t);
	Map *idx = map_get(&G.index, key);
	if (!idx) {
		idx = xalloc(sizeof *idx);
		for (int i = 0; i < t->n_fields; i++) {
			RtFieldInfo *fi = t->fields[i];
			const char *k = xfmt("%d", fi->no);
			if (!map_has(idx, k))
				map_put(idx, k, fi);
			if (fi->oneof) {
				const char *ok = xcat("\1", fi->oneof);
				Vec *members = map_get(idx, ok);
				if (!members) {
					members = xalloc(sizeof *members);
					map_put(idx, ok, members);
				}
				vec_push(members, fi);
			}
		}
		map_put(&G.index, key, idx);
	}
	DescField *sample = oneof->fields.v[0];
	RtFieldInfo *sample_fi = map_get(idx, xfmt("%d", sample->number));
	if (!sample_fi || !sample_fi->oneof)
		fail("Error: assertion failed");
	const char *oneof_name = sample_fi->oneof;
	TsNodeArray *cases = ts_list_new();
	Vec *members = map_get(idx, xcat("\1", oneof_name));
	for (int i = 0; members && i < members->n; i++) {
		RtFieldInfo *fi = members->v[i];
		TsNode *kind = ts_create_property_signature(NULL, ID(G.o->oneof_kind_discriminator), NULL,
							    ts_create_literal_type_node(STR(fi->local_name)), NULL);
		TsNode *value = create_field_property_signature(f, field_by_number(m, fi->no), fi);
		ts_list_push(cases, ts_create_type_literal_node(ts_list(kind, value)));
	}
	ts_list_push(cases, ts_create_type_literal_node(ts_list(ts_create_property_signature(
				    NULL, ID(G.o->oneof_kind_discriminator), NULL, KW(TS_UndefinedKeyword), NULL))));
	TsNode *prop = ts_create_property_signature(NULL, ID(oneof_name), NULL, ts_create_union_type_node(cases), NULL);
	add_comments_for_descriptor(prop, &oneof->h, APPEND_TO_LEADING_BLOCK);
	return prop;
}

/* generateMessageInterface */
static void generate_message_interface(OutFile *f, DescMessage *m)
{
	RtMessageType *t = get_message_type(&G.interp, m);
	Map processed = {0};
	TsNodeArray *members = ts_list_new();
	for (int i = 0; i < t->n_fields; i++) {
		RtFieldInfo *fi = t->fields[i];
		DescField *field = field_by_number(m, fi->no);
		if (fi->oneof && field->oneof) {
			if (map_has(&processed, fi->oneof))
				continue;
			ts_list_push(members, create_oneof_adt_property_signature(f, m, field->oneof));
			map_put(&processed, fi->oneof, (void *)1);
		} else {
			ts_list_push(members, create_field_property_signature(f, field, fi));
		}
	}
	const char *name = import_type(&G.imports, f, &m->h, "default", false);
	TsNode *st = ts_create_interface_declaration(NULL, ts_list(TOK(TS_ExportKeyword)), ID(name), NULL, NULL, members);
	add_statement(f, st, false);
	add_comments_for_descriptor(st, &m->h, APPEND_TO_LEADING_BLOCK);
}

/* ---- enum-generator.ts ---- */

static void enum_register_symbols(OutFile *f, DescEnum *e)
{
	symbols_register(&G.symbols, create_local_type_name(&e->h), &e->h, f, "default");
}

/* generateEnum */
static void generate_enum(OutFile *f, DescEnum *e)
{
	RtEnumObject *obj = get_enum_info(&G.interp, e)->obj;
	EnumBuilder b = {0};
	for (int i = 0; i < obj->n; i++) {
		/* listEnumValues: the names in creation order with their numbers */
		const char *name = obj->names[i];
		int32_t number = obj->numbers[i];
		DescEnumValue *ev = enum_value_by_number(e, number);
		Str *c = xalloc(sizeof *c);
		*c = ev ? get_comment_block(&ev->h, true)
			: STR_LIT("@generated synthetic value - protobuf-ts requires all enums to have a 0 value");
		enum_builder_add(&b, name, number, c);
	}
	const char *name = import_type(&G.imports, f, &e->h, "default", false);
	TsNode *st = enum_builder_build(&b, name, ts_list(TOK(TS_ExportKeyword)));
	add_statement(f, st, false);
	add_comments_for_descriptor(st, &e->h, APPEND_TO_LEADING_BLOCK);
}
