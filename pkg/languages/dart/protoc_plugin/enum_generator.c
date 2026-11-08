/* Port of protoc_plugin 25.1.0 lib/src/enum_generator.dart.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static const char *enum_dartName(EnumGen *e, const char *valueName) { return map_get(&e->dartNames, valueName); }

static void il_push(IntList *l, int x)
{
	l->v = xrealloc(l->v, (size_t)l->n * sizeof(int), (size_t)(l->n + 1) * sizeof(int));
	l->v[l->n++] = x;
}

/* EnumGenerator._ (topLevel: fieldIdTag 5, nested: 4) */
static EnumGen *enum_new(PbMsg *descriptor, Pc *parent, Map *usedClassNames, int repeatedFieldIndex, int fieldIdTag)
{
	EnumGen *e = xalloc(sizeof *e);
	e->pc.kind = PC_ENUM;
	e->pc.parent = parent;
	e->pc.fileGen = parent->fileGen;
	e->pc.package = parent->package;
	e->pc.fieldPath = il_append(parent->fieldPath, fieldIdTag, repeatedFieldIndex);
	e->descriptor = descriptor;
	const char *name = pb_str(descriptor, EDP_NAME);
	e->pc.classname = messageOrEnumClassName(name, usedClassNames, parent->classname);
	e->pc.fullName = *parent->fullName ? xfmt("%s.%s", parent->fullName, name) : name;
	e->pc.features = resolveFeatures(&parent->features, features_of(pb_msg(pb_msg(descriptor, EDP_OPTIONS), EO_FEATURES)));
	Map usedNames = {0};
	reservedEnumNames(&usedNames);
	int n = pb_count(descriptor, EDP_VALUE);
	Map firstByNumber = {0};
	for (int i = 0; i < n; i++) {
		PbMsg *value = pb_msg_at(descriptor, EDP_VALUE, i);
		const char *key = xfmt("%lld", (long long)pb_int(value, EVDP_NUMBER, 0));
		PbMsg *canonical = map_get(&firstByNumber, key);
		if (!canonical) {
			canonical = value;
			map_put(&firstByNumber, key, value);
		}
		if (canonical == value) {
			vec_push(&e->canonicalValues, value);
			il_push(&e->originalCanonicalIndices, i);
		} else {
			vec_push(&e->aliases, value);
			vec_push(&e->aliases, canonical);
			il_push(&e->originalAliasIndices, i);
		}
		const char *vname = pb_str(value, EVDP_NAME);
		map_put(&e->dartNames, vname,
			disambiguateName(avoidInitialUnderscore(vname), &usedNames, SUFFIX_ENUM, 0, NULL, NULL));
	}
	return e;
}

/* getJsonConstant(context) */
static Node *getJsonConstant(Pc *pc, FileGen *context)
{
	const char *name = xcat(pc->classname, "$json");
	if (strcmp(context->protoFileUri, pc->fileGen->protoFileUri) == 0)
		return ds_id(name);
	return ds_path(importPrefix(context, pc), name, (char *)0);
}

static Node *deprecated_annot(const char *message) { return annot_call(coreImportPrefix, "Deprecated", ds_str(xfmt("'%s'", message))); }

static void enum_generate(EnumGen *e, Out *o)
{
	FileGen *fg = e->pc.fileGen;
	const char *classname = e->pc.classname;
	out_commentBlock(o, commentBlock(fg, e->pc.fieldPath));
	NodeList meta = {0};
	if (pb_int(pb_msg(e->descriptor, EDP_OPTIONS), EO_DEPRECATED, 0))
		ds_push(&meta, deprecated_annot("This enum is deprecated"));
	Node *cls = out_add(o, ds_class(meta, false, classname, pb_type("ProtobufEnum"), NONE, NONE));
	out_annotate(o, cls->namePart->name, e->pc.fieldPath);
	OutBody body = out_begin_body(o, &cls->body->list);
	for (int i = 0; i < e->canonicalValues.n; i++) {
		PbMsg *val = VEC_AT(e->canonicalValues, PbMsg, i);
		const char *name = enum_dartName(e, pb_str(val, EVDP_NAME));
		out_addSuffix(o, "omit_enum_names");
		IntList path = il_append(e->pc.fieldPath, 2, e->originalCanonicalIndices.v[i]);
		out_commentBlock(o, commentBlock(fg, path));
		NodeList vmeta = {0};
		if (pb_int(pb_msg(val, EVDP_OPTIONS), EVO_DEPRECATED, 0))
			ds_push(&vmeta, deprecated_annot("This enum value is deprecated"));
		Node *init = ds_call(ds_id(classname), "_", NULL,
				     ds_args(ds_int(pb_int(val, EVDP_NUMBER, 0)),
					     ccd_createTernary("omit_enum_names", pb_bytes(val, EVDP_NAME)), (Node *)0));
		Node *f = out_add(o, ds_field(vmeta, true, KW_CONST, ds_type(NULL, classname, NULL, false), name, init));
		out_annotate(o, f->vars->list.v[0]->name, path);
	}
	if (e->aliases.n) {
		out_blank(o);
		for (int i = 0; i < e->aliases.n / 2; i++) {
			PbMsg *alias = VEC_AT(e->aliases, PbMsg, 2 * i), *canonical = VEC_AT(e->aliases, PbMsg, 2 * i + 1);
			const char *name = enum_dartName(e, pb_str(alias, EVDP_NAME));
			Node *f = out_add(o, ds_field(NONE, true, KW_CONST, ds_type(NULL, classname, NULL, false), name,
						      ds_id(enum_dartName(e, pb_str(canonical, EVDP_NAME)))));
			out_annotate(o, f->vars->list.v[0]->name, il_append(e->pc.fieldPath, 2, e->originalAliasIndices.v[i]));
		}
	}
	out_blank(o);

	NodeList values = {0};
	for (int i = 0; i < e->canonicalValues.n; i++)
		ds_push(&values, ds_id(enum_dartName(e, pb_str(VEC_AT(e->canonicalValues, PbMsg, i), EVDP_NAME))));
	Node *list = ds_list(NULL, ds_targs(ds_type(NULL, classname, NULL, false), (Node *)0), values);
	list->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
	ds_tnl(list->lb, 0, 1);
	out_add(o, ds_field(NONE, true, KW_CONST, ds_type(coreImportPrefix, "List", ds_targs(ds_type(NULL, classname, NULL, false), (Node *)0), false), "values", list));
	out_blank(o);

	long long maxEnumValue = -1;
	for (int i = 0; i < e->canonicalValues.n; i++) {
		long long number = pb_int(VEC_AT(e->canonicalValues, PbMsg, i), EVDP_NUMBER, 0);
		if (number < 0) {
			maxEnumValue = -1;
			break;
		}
		if (number > maxEnumValue)
			maxEnumValue = number;
	}
	bool useList = e->canonicalValues.n == 0 ||
		       (maxEnumValue >= 0 && (double)e->canonicalValues.n / (double)(maxEnumValue + 1) >= 0.7);
	Node *valueParam = ds_params(NL(ds_param(core_type("int"), "value", NULL)), NONE, false);
	Node *enumNullable = ds_type(NULL, classname, NULL, true);
	if (useList) {
		out_add(o, ds_field(NONE, true, KW_FINAL,
				    ds_type(coreImportPrefix, "List", ds_targs(ds_type(NULL, classname, NULL, true), (Node *)0), false),
				    "_byValue",
				    ds_call(ds_path(protobufImportPrefix, "ProtobufEnum", (char *)0), "$_initByValueList", NULL,
					    ds_args(ds_id("values"), ds_int(maxEnumValue), (Node *)0))));
		Node *cond = ds_cond(ds_bin(ds_bin(ds_id("value"), TT_LT, "<", ds_int(0)), TT_BAR_BAR, "||",
					    ds_bin(ds_id("value"), TT_GT_EQ, ">=", ds_path("_byValue", "length", (char *)0))),
				     ds_lit(N_NullLiteral, TT_IDENTIFIER, "null"), ds_index(ds_id("_byValue"), ds_id("value")));
		ds_nl(cond, 0, 2);
		out_add(o, ds_method(NONE, KW_STATIC, enumNullable, KW_NONE, "valueOf", valueParam, ds_arrow(cond)));
	} else {
		out_add(o, ds_field(NONE, true, KW_FINAL,
				    ds_type(coreImportPrefix, "Map", ds_targs(core_type("int"), ds_type(NULL, classname, NULL, false), (Node *)0), false),
				    "_byValue",
				    ds_call(ds_path(protobufImportPrefix, "ProtobufEnum", (char *)0), "initByValue", NULL,
					    ds_args(ds_id("values"), (Node *)0))));
		out_add(o, ds_method(NONE, KW_STATIC, enumNullable, KW_NONE, "valueOf", valueParam,
				     ds_arrow(ds_index(ds_id("_byValue"), ds_id("value")))));
	}
	out_blank(o);
	out_add(o, ds_ctor(NONE, KW_CONST, classname, "_",
			   ds_params(NL(ds_fparam(KW_SUPER, "value"), ds_fparam(KW_SUPER, "name")), NONE, false),
			   ds_empty_body()));
	out_end_body(o, body, cls->body->rb);
	out_blank(o); /* '}\n' */
}

/* generateConstants: the JSON of the EnumDescriptorProto. */
static void enum_generateConstants(EnumGen *e, Out *o)
{
	Pc *top = &e->pc;
	while (top->parent && top->parent->kind != PC_FILE)
		top = top->parent;
	const char *descName = xcat(lowerCaseFirstLetter(top->classname), "Descriptor");
	Node *init = writeJsonConst(dpb_json_map(e->descriptor));
	Node *decl = ds_toplevel(NL(deprecated_annot(xfmt("Use %s instead", descName))), KW_CONST, NULL,
				 xcat(e->pc.classname, "$json"), init);
	out_add(o, decl);
	out_blank(o);
}
