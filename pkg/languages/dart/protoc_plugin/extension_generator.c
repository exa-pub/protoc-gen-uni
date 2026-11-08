/* Port of protoc_plugin 25.1.0 lib/src/extension_generator.dart.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* ExtensionGenerator._ (topLevel: fieldIdTag 7, nested: 6) */
static ExtGen *ext_new(PbMsg *descriptor, Pc *parent, Map *usedNames, int repeatedFieldIndex, int fieldIdTag)
{
	ExtGen *x = xalloc(sizeof *x);
	x->descriptor = descriptor;
	x->parent = parent;
	x->extensionName = extensionName(descriptor, usedNames);
	x->fieldPath = il_append(parent->fieldPath, fieldIdTag, repeatedFieldIndex);
	x->extendedFullName = "";
	return x;
}

static void ext_resolve(ExtGen *x, GenerationContext *ctx)
{
	x->field = field_new(x->descriptor, NULL, x->parent, &x->parent->features, ctx);
	x->resolved = true;
	Pc *extendedType = ctx_getFieldType(ctx, pb_str(x->descriptor, FD_EXTENDEE));
	if (extendedType)
		x->extendedFullName = extendedType->fullName;
}

static void ext_checkResolved(ExtGen *x)
{
	if (!x->resolved)
		dart_throw("Bad state: resolve not called");
}

/* name: Parent.name for extensions in a message */
static const char *ext_name(ExtGen *x)
{
	ext_checkResolved(x);
	return x->parent->kind == PC_MESSAGE ? xfmt("%s.%s", x->parent->classname, x->extensionName) : x->extensionName;
}

static bool ext_needsFixnumImport(ExtGen *x)
{
	ext_checkResolved(x);
	return field_needsFixnumImport(x->field);
}

static void ext_addImportsTo(ExtGen *x, Vec *imports, Vec *enumImports)
{
	ext_checkResolved(x);
	Pc *typeGen = x->field->baseType.generator;
	if (typeGen && typeGen->kind == PC_ENUM)
		set_add_gen(enumImports, typeGen->fileGen);
	else if (typeGen && typeGen->fileGen != x->parent->fileGen)
		set_add_gen(imports, typeGen->fileGen);
}

static void ext_generate(ExtGen *x, Out *o)
{
	ext_checkResolved(x);
	Field *field = x->field;
	const BaseType *type = &field->baseType;
	const DType *dartType = bt_getDartType(type, x->parent->fileGen);
	out_addSuffix(o, "omit_field_names");
	out_addSuffix(o, "omit_message_names");
	const char *tc = field_typeConstantName(field);
	Args a = {0};
	ds_push(&a.positionals, ccd_createTernary("omit_message_names", str_c(x->extendedFullName)));
	ds_push(&a.positionals, ccd_createTernary("omit_field_names", str_c(x->extensionName)));
	ds_push(&a.positionals, ds_int(field_number(field)));
	ds_push(&a.positionals, pbFieldType(tc));
	args_named(&a, "protoName", protoName_node(field));
	bool msg = bt_isMessage(type) || bt_isGroup(type);
	Node *init;
	Node *ext = ds_type(protobufImportPrefix, "Extension", ds_targs(dt_node(dartType, false), (Node *)0), false);
	if (field_isRepeated(field)) {
		args_named(&a, "check", ds_call(ds_id(protobufImportPrefix), "getCheckFunction", NULL, ds_args(pbFieldType(tc), (Node *)0)));
		if (msg) {
			args_named(&a, "subBuilder", ds_prop(dt_expr(dartType), "$_createMessage"));
		} else if (bt_isEnum(type)) {
			args_named(&a, "valueOf", ds_prop(dt_expr(dartType), "valueOf"));
			args_named(&a, "enumValues", ds_prop(dt_expr(dartType), "values"));
		}
		init = ds_new(KW_NONE, ext, "repeated", args_node(&a));
	} else {
		DefExpr d = field_generateDefaultFunction(field, false);
		args_named(&a, "defaultOrMaker", def_node(&d));
		if (msg) {
			args_named(&a, "subBuilder", ds_prop(dt_expr(dartType), "$_createMessage"));
		} else if (bt_isEnum(type)) {
			args_named(&a, "valueOf", ds_prop(dt_expr(dartType), "valueOf"));
			args_named(&a, "enumValues", ds_prop(dt_expr(dartType), "values"));
		}
		init = ds_call(ds_id(protobufImportPrefix), "Extension", ext->typeArgs, args_node(&a));
	}
	Node *decl = out_add(o, ds_field(NONE, true, KW_FINAL, NULL, x->extensionName, init));
	out_annotate(o, decl->vars->list.v[0]->name, x->fieldPath);
}
