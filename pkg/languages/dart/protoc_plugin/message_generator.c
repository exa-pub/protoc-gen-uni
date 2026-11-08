/* Port of protoc_plugin 25.1.0 lib/src/message_generator.dart (MessageGenerator,
 * OneofEnumGenerator).
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static ExtGen *ext_new(PbMsg *descriptor, Pc *parent, Map *usedNames, int repeatedFieldIndex, int fieldIdTag);
static void ext_resolve(ExtGen *x, GenerationContext *ctx);
static bool ext_needsFixnumImport(ExtGen *x);
static void ext_addImportsTo(ExtGen *x, Vec *imports, Vec *enumImports);
static void ext_generate(ExtGen *x, Out *o);
static const PbMixin *wellKnownMixinForFullName(const char *name);
static const PbMixin *findMixin(const char *name);
static int mixin_findMixinsToApply(const PbMixin *m, const PbMixin **out);
static const char *const *mixin_findReservedNames(const PbMixin *m);
static void mixin_injectHelpers(const PbMixin *m, Out *o);

static void set_add_gen(Vec *set, void *p)
{
	if (vec_index(set, p) < 0)
		vec_push(set, p);
}

static MsgGen *msg_new(PbMsg *descriptor, Pc *parent, Map *declaredMixins, const PbMixin *defaultMixin,
		       Map *usedTopLevelNames, IntList fieldPath)
{
	MsgGen *m = xalloc(sizeof *m);
	m->pc.kind = PC_MESSAGE;
	m->pc.parent = parent;
	m->pc.fileGen = parent->fileGen;
	m->pc.package = parent->package;
	m->pc.fieldPath = fieldPath;
	m->descriptor = descriptor;
	m->usedTopLevelNames = usedTopLevelNames;
	const char *name = pb_str(descriptor, DP_NAME);
	m->pc.classname = messageOrEnumClassName(name, usedTopLevelNames, parent->classname);
	m->pc.fullName = *parent->fullName ? xfmt("%s.%s", parent->fullName, name) : name;
	m->noneofs = countRealOneofs(descriptor);
	m->oneofFields = xalloc((size_t)(m->noneofs + 1) * sizeof *m->oneofFields);
	m->oneofFeatures = xalloc((size_t)(m->noneofs + 1) * sizeof *m->oneofFeatures);
	PbMsg *options = pb_msg(descriptor, DP_OPTIONS);
	m->pc.features = resolveFeatures(&parent->features, features_of(pb_msg(options, MO_FEATURES)));

	/* _getMixin */
	m->mixin = wellKnownMixinForFullName(m->pc.fullName);
	if (!m->mixin) {
		if (!pb_has(options, DPB_EXT_MIXIN)) {
			m->mixin = defaultMixin;
		} else {
			const char *mixinName = pb_str(options, DPB_EXT_MIXIN);
			if (*mixinName) {
				m->mixin = map_get(declaredMixins, mixinName);
				if (!m->mixin)
					m->mixin = findMixin(mixinName);
				if (!m->mixin)
					dart_throw(xfmt("%s in %s: mixin \"%s\" not found", name,
							pb_str(parent->fileGen->descriptor, FDP_NAME), mixinName));
			}
		}
	}
	for (int i = 0; i < pb_count(descriptor, DP_ENUM_TYPE); i++)
		vec_push(&m->enumGenerators, enum_new(pb_msg_at(descriptor, DP_ENUM_TYPE, i), &m->pc, usedTopLevelNames, i, 4));
	for (int i = 0; i < pb_count(descriptor, DP_NESTED_TYPE); i++)
		vec_push(&m->messageGenerators, msg_new(pb_msg_at(descriptor, DP_NESTED_TYPE, i), &m->pc, declaredMixins,
							defaultMixin, usedTopLevelNames,
							buildNestedMessagePath(m->pc.fieldPath, i)));
	for (int i = 0; i < m->noneofs; i++) {
		if (i >= pb_count(descriptor, DP_ONEOF_DECL))
			dart_throw(xfmt("RangeError (length): Invalid value: Not in inclusive range 0..%d: %d",
					pb_count(descriptor, DP_ONEOF_DECL) - 1, i));
		PbMsg *oneof = pb_msg_at(descriptor, DP_ONEOF_DECL, i);
		m->oneofFeatures[i] = resolveFeatures(&m->pc.features, features_of(pb_msg(pb_msg(oneof, 2), OO_FEATURES)));
	}
	Map usedExtensionNames = {0};
	reservedMemberNames(&usedExtensionNames);
	for (int i = 0; i < pb_count(descriptor, DP_EXTENSION); i++)
		vec_push(&m->extensionGenerators, ext_new(pb_msg_at(descriptor, DP_EXTENSION, i), &m->pc, &usedExtensionNames, i, 6));
	return m;
}

static void msg_checkResolved(MsgGen *m)
{
	if (!m->resolved)
		dart_throw(xfmt("Bad state: message not resolved: %s", m->pc.fullName));
}

static void ctx_registerFieldType(GenerationContext *ctx, Pc *type);

static void msg_register(MsgGen *m, GenerationContext *ctx)
{
	ctx_registerFieldType(ctx, &m->pc);
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_register(VEC_AT(m->messageGenerators, MsgGen, i), ctx);
	for (int i = 0; i < m->enumGenerators.n; i++)
		ctx_registerFieldType(ctx, &VEC_AT(m->enumGenerators, EnumGen, i)->pc);
}

static void msg_resolve(MsgGen *m, GenerationContext *ctx)
{
	if (m->resolved)
		dart_throw("Bad state: message already resolved");
	m->resolved = true;
	MemberNames members = messageMemberNames(m->descriptor, m->pc.classname, m->usedTopLevelNames,
						 m->mixin ? mixin_findReservedNames(m->mixin) : NULL);
	for (int i = 0; i < members.nfields; i++) {
		FieldNames *names = members.fieldNames[i];
		if (!names)
			dart_throw("type 'Null' is not a subtype of type 'FieldNames' in type cast");
		PbMsg *d = names->descriptor;
		Field *field;
		if (pb_has(d, FD_ONEOF_INDEX) && !pb_int(d, FD_PROTO3_OPTIONAL, 0)) {
			int idx = (int)pb_int(d, FD_ONEOF_INDEX, 0);
			if (idx < 0 || idx >= m->noneofs)
				dart_throw(xfmt("RangeError (index): Index out of range: index should be less than %d: %d", m->noneofs, idx));
			field = field_new(d, names, &m->pc, &m->oneofFeatures[idx], ctx);
			vec_push(&m->oneofFields[idx], field);
		} else {
			field = field_new(d, names, &m->pc, &m->pc.features, ctx);
		}
		vec_push(&m->fieldList, field);
	}
	m->oneofNames = members.oneofNames;
	m->noneofNames = members.noneofs;
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_resolve(VEC_AT(m->messageGenerators, MsgGen, i), ctx);
	for (int i = 0; i < m->extensionGenerators.n; i++)
		ext_resolve(VEC_AT(m->extensionGenerators, ExtGen, i), ctx);
}

static bool msg_needsFixnumImport(MsgGen *m)
{
	msg_checkResolved(m);
	for (int i = 0; i < m->fieldList.n; i++)
		if (field_needsFixnumImport(VEC_AT(m->fieldList, Field, i)))
			return true;
	for (int i = 0; i < m->messageGenerators.n; i++)
		if (msg_needsFixnumImport(VEC_AT(m->messageGenerators, MsgGen, i)))
			return true;
	for (int i = 0; i < m->extensionGenerators.n; i++)
		if (ext_needsFixnumImport(VEC_AT(m->extensionGenerators, ExtGen, i)))
			return true;
	return false;
}

static void msg_addImportsTo(MsgGen *m, Vec *imports, Vec *enumImports)
{
	msg_checkResolved(m);
	for (int i = 0; i < m->fieldList.n; i++) {
		Pc *typeGen = VEC_AT(m->fieldList, Field, i)->baseType.generator;
		if (typeGen && typeGen->kind == PC_ENUM)
			set_add_gen(enumImports, typeGen->fileGen);
		else if (typeGen)
			set_add_gen(imports, typeGen->fileGen);
	}
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_addImportsTo(VEC_AT(m->messageGenerators, MsgGen, i), imports, enumImports);
	for (int i = 0; i < m->extensionGenerators.n; i++)
		ext_addImportsTo(VEC_AT(m->extensionGenerators, ExtGen, i), imports, enumImports);
}

static int msg_enumCount(MsgGen *m)
{
	int count = m->enumGenerators.n;
	for (int i = 0; i < m->messageGenerators.n; i++)
		count += msg_enumCount(VEC_AT(m->messageGenerators, MsgGen, i));
	return count;
}

static void msg_addConstantImportsTo(MsgGen *m, Vec *imports)
{
	msg_checkResolved(m);
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_addConstantImportsTo(VEC_AT(m->messageGenerators, MsgGen, i), imports);
}

static void msg_addMixinsTo(MsgGen *m, Vec *output)
{
	if (m->mixin) {
		const PbMixin *chain[64];
		int n = mixin_findMixinsToApply(m->mixin, chain);
		for (int i = 0; i < n; i++)
			set_add_gen(output, (void *)chain[i]);
	}
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_addMixinsTo(VEC_AT(m->messageGenerators, MsgGen, i), output);
}

static Node *tagNumber(int number) { return annot_call(protobufImportPrefix, "TagNumber", ds_int(number)); }
static Node *core_override(void) { return ds_annot(ds_path(coreImportPrefix, "override", (char *)0), NULL); }
static Node *core_pragma(void) { return annot_call(coreImportPrefix, "pragma", ds_str("'dart2js:noInline'")); }
static Node *cls_type(MsgGen *m) { return ds_type(NULL, m->pc.classname, NULL, false); }
static Node *no_params(void) { return ds_params(NONE, NONE, false); }
static Node *call0(const char *target, const char *name) { return ds_call(target ? ds_id(target) : NULL, name, NULL, ds_args((Node *)0)); }

/* OneofEnumGenerator.generate */
static void oneofEnum_generate(Out *o, OneofNames *oneof, Vec *fields, IntList parentPath)
{
	NodeList constants = {0};
	Node *e = out_add(o, ds_enum(NONE, oneof->oneofEnumName, NONE));
	out_annotate(o, e->namePart->name, buildOneofPath(parentPath, oneof));
	for (int i = 0; i < fields->n; i++) {
		Field *field = VEC_AT(*fields, Field, i);
		Node *c = ds_enum_const(oneofEnumMemberName(field->memberNames->fieldName));
		ds_comma(c)->trail = " ";
		out_annotate(o, c->name, buildFieldPath(parentPath, field));
		ds_push(&constants, c);
	}
	ds_push(&constants, ds_enum_const("notSet"));
	e->body->list = constants;
	out_blank(o); /* '}\n' */
}

/* _hasRequiredFields */
static bool hasRequiredFields(MsgGen *type, Map *alreadySeen)
{
	if (map_has(alreadySeen, type->pc.fullName))
		return false;
	map_put(alreadySeen, type->pc.fullName, (void *)1);
	if (pb_count(type->descriptor, DP_EXTENSION_RANGE))
		return true;
	for (int i = 0; i < type->fieldList.n; i++) {
		Field *field = VEC_AT(type->fieldList, Field, i);
		if (field_isRequired(field))
			return true;
		if (bt_isMessage(&field->baseType) && hasRequiredFields((MsgGen *)field->baseType.generator, alreadySeen))
			return true;
	}
	return false;
}

static void msg_generateFactory(MsgGen *m, Out *o)
{
	FileGen *fg = m->pc.fileGen;
	const char *classname = m->pc.classname;
	if (fg->options->disableConstructorArgs || m->fieldList.n == 0) {
		out_add(o, ds_ctor(NONE, KW_FACTORY, classname, NULL, no_params(), ds_arrow(call0(classname, "_"))));
		return;
	}
	NodeList params = {0};
	bool resultUsed = false;
	for (int i = 0; i < m->fieldList.n; i++) {
		Field *field = VEC_AT(m->fieldList, Field, i);
		const DType *type;
		if (field_isRepeated(field) && !field_isMapField(field))
			type = bt_getRepeatedDartTypeIterable(&field->baseType, fg);
		else if (field_isMapField(field))
			type = dt_gen(coreImportPrefix, "Iterable",
				      dt_gen(coreImportPrefix, "MapEntry", field_getDartMapKeyType(field), field_getDartMapValueType(field)), NULL);
		else
			type = field_getDartType(field);
		Node *t = dt_node(type, true);
		Node *p = ds_param(t, field->memberNames->fieldName, NULL);
		if (field_isDeprecated(field)) {
			Node *a = deprecated_annot("This field is deprecated.");
			ds_nl(a, 1, -2);
			ds_push(&p->meta, a);
			ds_nl(t, 1, 0);
		}
		ds_push(&params, p);
		resultUsed |= strcmp(field->memberNames->fieldName, "result") == 0;
	}
	const char *result = resultUsed ? "result$" : "result";
	NodeList stmts = {0};
	ds_push(&stmts, ds_var_stmt(KW_FINAL, NULL, result, call0(classname, "_")));
	for (int i = 0; i < m->fieldList.n; i++) {
		Field *field = VEC_AT(m->fieldList, Field, i);
		const char *name = field->memberNames->fieldName;
		Node *member = ds_path(result, name, (char *)0);
		Node *s;
		if (field_isRepeated(field) && !field_isMapField(field))
			s = ds_call(member, "addAll", NULL, ds_args(ds_id(name), (Node *)0));
		else if (field_isMapField(field))
			s = ds_call(member, "addEntries", NULL, ds_args(ds_id(name), (Node *)0));
		else
			s = ds_assign(member, TT_EQ, "=", ds_id(name));
		ds_push(&stmts, ds_if(ds_bin(ds_id(name), TT_BANG_EQ, "!=", ds_lit(N_NullLiteral, TT_IDENTIFIER, "null")),
				      ds_expr_stmt(s)));
	}
	ds_push(&stmts, ds_return(ds_id(result)));
	Node *pl = ds_params(NONE, params, true);
	pl->flags |= DS_VERTICAL;
	out_add(o, ds_ctor(NONE, KW_FACTORY, classname, NULL, pl, ds_bbody(KW_NONE, false, ds_block(stmts))));
}

/* _getterExpression */
static Node *getterExpression(const DType *fieldType, int index, DefExpr *defaultExpr, bool isRepeated, bool isMapField)
{
	const char *t = dt_str(fieldType);
	const char *name;
	bool withDefault = false;
	if (isMapField) {
		name = "$_getMap";
	} else if (strcmp(t, "$core.String") == 0) {
		withDefault = !(def_is(defaultExpr, "\"\"") || def_is(defaultExpr, "''"));
		name = withDefault ? "$_getS" : "$_getSZ";
	} else if (strcmp(t, "$core.bool") == 0) {
		withDefault = !def_is(defaultExpr, "false");
		name = withDefault ? "$_getB" : "$_getBF";
	} else if (strcmp(t, "$core.int") == 0) {
		withDefault = !def_is(defaultExpr, "0");
		name = withDefault ? "$_getI" : "$_getIZ";
	} else if (strcmp(t, "$fixnum.Int64") == 0 && defaultExpr->kind == DE_NULL) {
		name = "$_getI64";
	} else if (defaultExpr->kind == DE_NULL) {
		name = isRepeated ? "$_getList" : "$_getN";
	} else {
		name = "$_get";
		withDefault = true;
	}
	return ds_call(NULL, name, NULL, ds_args(ds_int(index), withDefault ? def_node(defaultExpr) : NULL, (Node *)0));
}

static NodeList member_meta(bool deprecated, bool override, int number)
{
	NodeList meta = {0};
	if (deprecated)
		ds_push(&meta, deprecated_annot("This field is deprecated."));
	if (override)
		ds_push(&meta, core_override());
	ds_push(&meta, tagNumber(number));
	return meta;
}

static void msg_generateFieldAccessorsMutators(MsgGen *m, Field *field, Out *o, IntList memberFieldPath)
{
	FileGen *fg = m->pc.fileGen;
	const DType *fieldType = field_getDartType(field);
	DefExpr defaultExpr = field_getDefaultExpr(field);
	FieldNames *names = field->memberNames;
	bool deprecated = field_isDeprecated(field);
	int number = field_number(field), index = field_index(field);
	out_commentBlock(o, commentBlock(fg, memberFieldPath));
	Node *getter = ds_method(member_meta(deprecated, field_hasBooleanOption(field, DPB_EXT_OVERRIDE_GETTER), number),
				 KW_NONE, dt_node(fieldType, false), KW_GET, names->fieldName, NULL,
				 ds_arrow(getterExpression(fieldType, index, &defaultExpr, field_isRepeated(field), field_isMapField(field))));
	out_add(o, getter);
	out_annotate(o, getter->name, memberFieldPath);
	if (field_isRepeated(field)) {
		if (field_hasBooleanOption(field, DPB_EXT_OVERRIDE_SETTER))
			dart_throw(xfmt("Field %s cannot override a setter for %s because it is repeated.", field->fullName, names->fieldName));
		if (field_hasBooleanOption(field, DPB_EXT_OVERRIDE_HAS_METHOD))
			dart_throw(xfmt("Field %s cannot override %s() because it is repeated.", field->fullName, "null"));
		if (field_hasBooleanOption(field, DPB_EXT_OVERRIDE_CLEAR_METHOD))
			dart_throw(xfmt("Field %s cannot override %s() because it is repeated.", field->fullName, "null"));
		return;
	}
	const char *fastSetter = field->baseType.setter;
	Node *setterBody = fastSetter ? ds_call(NULL, fastSetter, NULL, ds_args(ds_int(index), ds_id("value"), (Node *)0))
				      : ds_call(NULL, "$_setField", NULL, ds_args(ds_int(number), ds_id("value"), (Node *)0));
	Node *setter = ds_method(member_meta(deprecated, field_hasBooleanOption(field, DPB_EXT_OVERRIDE_SETTER), number),
				 KW_NONE, NULL, KW_SET, names->fieldName,
				 ds_params(NL(ds_param(dt_node(fieldType, false), "value", NULL)), NONE, false), ds_arrow(setterBody));
	out_add(o, setter);
	out_annotate(o, setter->name, memberFieldPath);
	if (field_hasPresence(field)) {
		Node *has = ds_method(member_meta(deprecated, field_hasBooleanOption(field, DPB_EXT_OVERRIDE_HAS_METHOD), number),
				      KW_NONE, core_type("bool"), KW_NONE, names->hasMethodName, no_params(),
				      ds_arrow(ds_call(NULL, "$_has", NULL, ds_args(ds_int(index), (Node *)0))));
		out_add(o, has);
		out_annotate(o, has->name, memberFieldPath);
	}
	Node *clear = ds_method(member_meta(deprecated, field_hasBooleanOption(field, DPB_EXT_OVERRIDE_CLEAR_METHOD), number),
				KW_NONE, ds_type(NULL, "void", NULL, false), KW_NONE, names->clearMethodName, no_params(),
				ds_arrow(ds_call(NULL, "$_clearField", NULL, ds_args(ds_int(number), (Node *)0))));
	out_add(o, clear);
	out_annotate(o, clear->name, memberFieldPath);
	if (bt_isMessage(&field->baseType)) {
		Node *ensure = ds_method(member_meta(deprecated, false, number), KW_NONE, dt_node(fieldType, false), KW_NONE,
					 names->ensureMethodName, no_params(),
					 ds_arrow(ds_call(NULL, "$_ensure", NULL, ds_args(ds_int(index), (Node *)0))));
		out_add(o, ensure);
		out_annotate(o, ensure->name, memberFieldPath);
	}
}

static void msg_generateOneofAccessors(MsgGen *m, Out *o, OneofNames *oneof)
{
	Vec *fields = &m->oneofFields[oneof->index];
	out_blank(o);
	NodeList meta = {0};
	for (int i = 0; i < fields->n; i++)
		ds_push(&meta, tagNumber(field_number(VEC_AT(*fields, Field, i))));
	Node *which = ds_method(meta, KW_NONE, ds_type(NULL, oneof->oneofEnumName, NULL, false), KW_NONE,
				oneof->whichOneofMethodName, no_params(),
				ds_arrow(ds_postfix(ds_index(ds_id(oneof->byTagMapName),
							     ds_call(NULL, "$_whichOneof", NULL, ds_args(ds_int(oneof->index), (Node *)0))),
						    TT_BANG, "!")));
	out_add(o, which);
	out_annotate(o, which->name, buildOneofPath(m->pc.fieldPath, oneof));
	NodeList meta2 = {0};
	for (int i = 0; i < fields->n; i++)
		ds_push(&meta2, tagNumber(field_number(VEC_AT(*fields, Field, i))));
	Node *clear = ds_method(meta2, KW_NONE, ds_type(NULL, "void", NULL, false), KW_NONE, oneof->clearMethodName, no_params(),
				ds_arrow(ds_call(NULL, "$_clearField", NULL,
						 ds_args(ds_call(NULL, "$_whichOneof", NULL, ds_args(ds_int(oneof->index), (Node *)0)), (Node *)0))));
	out_add(o, clear);
	out_annotate(o, clear->name, buildOneofPath(m->pc.fieldPath, oneof));
}

static void msg_generate(MsgGen *m, Out *o)
{
	msg_checkResolved(m);
	FileGen *fg = m->pc.fileGen;
	const char *classname = m->pc.classname;
	for (int i = 0; i < m->messageGenerators.n; i++) {
		MsgGen *nested = VEC_AT(m->messageGenerators, MsgGen, i);
		if (pb_has(pb_msg(nested->descriptor, DP_OPTIONS), MO_MAP_ENTRY))
			continue;
		msg_generate(nested, o);
	}
	for (int i = 0; i < m->noneofNames; i++)
		oneofEnum_generate(o, m->oneofNames[i], &m->oneofFields[m->oneofNames[i]->index], m->pc.fieldPath);

	NodeList mixins = {0};
	if (m->mixin) {
		const PbMixin *chain[64];
		int n = mixin_findMixinsToApply(m->mixin, chain);
		for (int i = 0; i < n; i++)
			ds_push(&mixins, ds_type(mixinImportPrefix, chain[i]->name, NULL, false));
	}
	out_addSuffix(o, "omit_message_names");
	const char *package = m->pc.package;
	PbMsg *options = pb_msg(m->descriptor, DP_OPTIONS);
	const char *extendedClass = pb_int(options, MO_MESSAGE_SET_WIRE_FORMAT, 0) ? "$_MessageSet" : "GeneratedMessage";
	out_commentBlock(o, commentBlock(fg, m->pc.fieldPath));
	NodeList meta = {0};
	if (pb_int(options, MO_DEPRECATED, 0))
		ds_push(&meta, deprecated_annot("This message is deprecated"));
	Node *cls = out_add(o, ds_class(meta, false, classname, pb_type(extendedClass), mixins, NONE));
	out_annotate(o, cls->namePart->name, m->pc.fieldPath);
	OutBody body = out_begin_body(o, &cls->body->list);

	msg_generateFactory(m, o);
	out_blank(o);
	Node *privateCtor = out_add(o, ds_ctor(NONE, KW_NONE, classname, "_", no_params(), ds_empty_body()));
	out_annotate(o, privateCtor->ident->name, m->pc.fieldPath);
	out_blank(o);

	for (int k = 0; k < 2; k++) {
		const char *ctor = k == 0 ? "fromBuffer" : "fromJson";
		Node *first = k == 0 ? ds_param(ds_type(coreImportPrefix, "List", ds_targs(core_type("int"), (Node *)0), false), "data", NULL)
				     : ds_param(core_type("String"), "json", NULL);
		Node *registry = ds_param(pb_type("ExtensionRegistry"), "registry",
					  ds_path(protobufImportPrefix, "ExtensionRegistry", "EMPTY", (char *)0));
		Node *body = ds_cascade(call0(NULL, classname),
					NL(ds_csec_call(k == 0 ? "mergeFromBuffer" : "mergeFromJson", NULL,
							ds_args(ds_id(k == 0 ? "data" : "json"), ds_id("registry"), (Node *)0))));
		out_add(o, ds_ctor(NONE, KW_FACTORY, classname, ctor, ds_params(NL(first), NL(registry), false), ds_arrow(body)));
	}
	out_blank(o);

	for (int i = 0; i < m->noneofNames; i++) {
		OneofNames *oneof = m->oneofNames[i];
		Vec *fields = &m->oneofFields[oneof->index];
		NodeList entries = {0};
		for (int j = 0; j < fields->n; j++) {
			Field *field = VEC_AT(*fields, Field, j);
			Node *e = ds_entry(ds_int(field_number(field)),
					   ds_path(oneof->oneofEnumName, oneofEnumMemberName(field->memberNames->fieldName), (char *)0));
			ds_tnl(e->op, 0, 1);
			ds_push(&entries, e);
		}
		Node *last = ds_entry(ds_int(0), ds_path(oneof->oneofEnumName, "notSet", (char *)0));
		ds_tnl(last->op, 0, 1);
		ds_push(&entries, last);
		Node *map = ds_map(NULL, NULL, entries);
		map->flags |= DS_VERTICAL;
		out_add(o, ds_field(NONE, true, KW_CONST,
				    ds_type(coreImportPrefix, "Map", ds_targs(core_type("int"), ds_type(NULL, oneof->oneofEnumName, NULL, false), (Node *)0), false),
				    oneof->byTagMapName, map));
	}

	/* static final $pb.BuilderInfo _i = $pb.BuilderInfo(...)..calls; */
	out_addSuffix(o, "omit_message_names");
	const char *messageName = m->pc.fullName + (*package ? strlen(package) + 1 : 0);
	NodeList biArgs = {0};
	ds_push(&biArgs, ccd_createTernary("omit_message_names", str_c(messageName)));
	if (*package)
		ds_push(&biArgs, ds_named("package", ds_new(KW_CONST, pb_type("PackageName"), NULL,
							    ds_args(ccd_createTernary("omit_message_names", str_c(package)), (Node *)0))));
	ds_push(&biArgs, ds_named("createEmptyInstance", ds_path(classname, "$_createMessage", (char *)0)));
	if (m->mixin && m->mixin->wellKnownType)
		ds_push(&biArgs, ds_named("wellKnownType", ds_path(mixinImportPrefix, "WellKnownType", m->mixin->wellKnownType, (char *)0)));
	Node *bi = ds_call(ds_id(protobufImportPrefix), "BuilderInfo", NULL, ds_argl(biArgs));
	NodeList sections = {0};
	for (int i = 0; i < m->noneofs; i++) {
		NodeList tags = {0};
		for (int j = 0; j < m->oneofFields[i].n; j++)
			ds_push(&tags, ds_int(field_number(VEC_AT(m->oneofFields[i], Field, j))));
		ds_push(&sections, ds_csec_call("oo", NULL, ds_args(ds_int(i), ds_list(NULL, NULL, tags), (Node *)0)));
	}
	for (int i = 0; i < m->fieldList.n; i++)
		ds_push(&sections, field_generateBuilderInfoCall(VEC_AT(m->fieldList, Field, i), o, package));
	if (pb_count(m->descriptor, DP_EXTENSION_RANGE))
		ds_push(&sections, ds_csec_assign("hasExtensions", ds_lit(N_BooleanLiteral, TT_IDENTIFIER, "true")));
	Map seen = {0};
	if (!hasRequiredFields(m, &seen))
		ds_push(&sections, ds_csec_assign("hasRequiredFields", ds_lit(N_BooleanLiteral, TT_IDENTIFIER, "false")));
	for (int i = 0; i < sections.n; i++)
		ds_nl(sections.v[i], 1, 2);
	Node *biField = ds_field(NONE, true, KW_FINAL, pb_type("BuilderInfo"), "_i", sections.n ? ds_cascade(bi, sections) : bi);
	ds_tnl(biField->semi, 1, 0);
	out_add(o, biField);

	for (int i = 0; i < m->extensionGenerators.n; i++)
		ext_generate(VEC_AT(m->extensionGenerators, ExtGen, i), o);

	out_blank(o);
	Node *seeIssue = ds_str("'See https://github.com/google/protobuf.dart/issues/998.'");
	out_add(o, ds_method(NL(annot_call(coreImportPrefix, "Deprecated", seeIssue)), KW_NONE, cls_type(m), KW_NONE, "clone",
			     no_params(), ds_arrow(call0(NULL, "deepCopy"))));
	Node *updates = ds_param(ds_fntype(ds_type(NULL, "void", NULL, false),
					   ds_params(NL(ds_param(cls_type(m), NULL, NULL)), NONE, false)),
				 "updates", NULL);
	Node *closure = ds_fnexpr(ds_params(NL(ds_param(NULL, "message", NULL)), NONE, false),
				  ds_arrow(ds_call(NULL, "updates", NULL, ds_args(ds_as(ds_id("message"), cls_type(m)), (Node *)0))));
	out_add(o, ds_method(NL(annot_call(coreImportPrefix, "Deprecated", ds_str("'See https://github.com/google/protobuf.dart/issues/998.'"))),
			     KW_NONE, cls_type(m), KW_NONE, "copyWith", ds_params(NL(updates), NONE, false),
			     ds_arrow(ds_as(ds_call(ds_super(), "copyWith", NULL, ds_args(closure, (Node *)0)), cls_type(m)))));

	out_blank(o);
	out_add(o, ds_method(NL(core_override()), KW_NONE, pb_type("BuilderInfo"), KW_GET, "info_", NULL, ds_arrow(ds_id("_i"))));

	out_blank(o);
	out_add(o, ds_method(NL(core_pragma(), annot_call(coreImportPrefix, "Deprecated", ds_str(xfmt("'Use %s() / %s.new instead'", classname, classname)))),
			     KW_STATIC, cls_type(m), KW_NONE, "create", no_params(), ds_arrow(call0(classname, "_"))));
	out_add(o, ds_method(NONE, KW_STATIC, pb_type("GeneratedMessage"), KW_NONE, "$_createMessage", no_params(),
			     ds_arrow(call0(classname, "_"))));
	out_add(o, ds_method(NL(core_override()), KW_NONE, cls_type(m), KW_NONE, "createEmptyInstance", no_params(),
			     ds_arrow(call0(classname, "_"))));
	Node *getDefault = ds_assign(ds_id("_defaultInstance"), TT_QUESTION_QUESTION_EQ, "?\?=",
				     ds_call(ds_path(protobufImportPrefix, "GeneratedMessage", (char *)0), "$_defaultFor",
					     ds_targs(cls_type(m), (Node *)0), ds_args(ds_path(classname, "$_createMessage", (char *)0), (Node *)0)));
	out_add(o, ds_method(NL(core_pragma()), KW_STATIC, cls_type(m), KW_NONE, "getDefault", no_params(), ds_arrow(getDefault)));
	out_add(o, ds_field(NONE, true, KW_NONE, ds_type(NULL, classname, NULL, true), "_defaultInstance", NULL));

	/* generateFieldsAccessorsMutators */
	for (int i = 0; i < m->noneofNames; i++)
		msg_generateOneofAccessors(m, o, m->oneofNames[i]);
	for (int i = 0; i < m->fieldList.n; i++) {
		Field *field = VEC_AT(m->fieldList, Field, i);
		out_blank(o);
		msg_generateFieldAccessorsMutators(m, field, o, buildFieldPath(m->pc.fieldPath, field));
	}
	if (m->mixin)
		mixin_injectHelpers(m->mixin, o);
	out_end_body(o, body, cls->body->rb);
	out_blank(o);
}

static void msg_generateEnums(MsgGen *m, Out *o)
{
	for (int i = 0; i < m->enumGenerators.n; i++)
		enum_generate(VEC_AT(m->enumGenerators, EnumGen, i), o);
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_generateEnums(VEC_AT(m->messageGenerators, MsgGen, i), o);
}

/* generateConstants: the JSON of the DescriptorProto, nested messages and enums by name. */
static void msg_generateConstants(MsgGen *m, Out *o)
{
	FileGen *fg = m->pc.fileGen;
	Jv *json = dpb_json_map(m->descriptor);
	Pc *top = &m->pc;
	while (top->parent && top->parent->kind != PC_FILE)
		top = top->parent;
	const char *descName = xcat(lowerCaseFirstLetter(top->classname), "Descriptor");
	NodeList entries = {0};
	for (int i = 0; i < json->n; i++) {
		const char *key = json->keys[i];
		Node *value;
		if (strcmp(key, "3") == 0 || strcmp(key, "4") == 0) {
			Vec *gens = strcmp(key, "3") == 0 ? &m->messageGenerators : &m->enumGenerators;
			NodeList names = {0};
			for (int j = 0; j < gens->n; j++)
				ds_push(&names, getJsonConstant(gens->v[j], fg));
			value = ds_list(NULL, NULL, names);
		} else {
			value = writeJsonConst(json->vals[i]);
		}
		ds_push(&entries, ds_entry(ds_str(xfmt("'%s'", key)), value));
	}
	Node *map = ds_map(NULL, NULL, entries);
	map->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
	Node *name = getJsonConstant(&m->pc, fg);
	out_add(o, ds_toplevel(NL(deprecated_annot(xfmt("Use %s instead", descName))), KW_CONST, NULL, name->name->lexeme, map));
	out_blank(o);
	for (int i = 0; i < m->messageGenerators.n; i++)
		msg_generateConstants(VEC_AT(m->messageGenerators, MsgGen, i), o);
	for (int i = 0; i < m->enumGenerators.n; i++)
		enum_generateConstants(VEC_AT(m->enumGenerators, EnumGen, i), o);
}
