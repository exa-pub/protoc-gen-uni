/* Port of protoc_plugin 25.1.0 lib/src/base_type.dart.
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static Pc *ctx_getFieldType(GenerationContext *ctx, const char *name);
static const char *importPrefix(FileGen *f, Pc *container);

static bool bt_isGroup(const BaseType *t) { return t->descriptor == TYPE_GROUP; }
static bool bt_isMessage(const BaseType *t) { return t->descriptor == TYPE_MESSAGE; }
static bool bt_isEnum(const BaseType *t) { return t->descriptor == TYPE_ENUM; }
static bool bt_isPackable(const BaseType *t)
{
	return (!t->generator && t->descriptor != TYPE_STRING && t->descriptor != TYPE_BYTES) || bt_isEnum(t);
}

/* prefixed(fileGen) */
static const DType *bt_prefixed(const BaseType *t, FileGen *f)
{
	if (!t->generator)
		return &t->unprefixed;
	return dt_new(importPrefix(f, t->generator), t->unprefixed.name);
}

/* getDartType(fileGen) */
static const DType *bt_getDartType(const BaseType *t, FileGen *f)
{
	if (t->generator && strcmp(f->protoFileUri, t->generator->fileGen->protoFileUri) == 0)
		return &t->unprefixed;
	return bt_prefixed(t, f);
}

static const DType *bt_getRepeatedDartType(const BaseType *t, FileGen *f)
{
	return dt_gen(protobufImportPrefix, "PbList", bt_getDartType(t, f), NULL);
}

static const DType *bt_getRepeatedDartTypeIterable(const BaseType *t, FileGen *f)
{
	return dt_gen(coreImportPrefix, "Iterable", bt_getDartType(t, f), NULL);
}

static const DType coreInt = {"$core", "int", {0}, 0};

/* BaseType(field, features, ctx) */
static BaseType bt_new(PbMsg *field, const Features *features, GenerationContext *ctx)
{
	static const struct {
		const char *suffix, *name, *setter;
	} prim[19] = {
		[TYPE_BOOL] = {"B", "bool", "$_setBool"}, [TYPE_FLOAT] = {"F", "double", "$_setFloat"},
		[TYPE_DOUBLE] = {"D", "double", "$_setDouble"}, [TYPE_INT32] = {"3", "int", "$_setSignedInt32"},
		[TYPE_UINT32] = {"U3", "int", "$_setUnsignedInt32"}, [TYPE_SINT32] = {"S3", "int", "$_setSignedInt32"},
		[TYPE_FIXED32] = {"F3", "int", "$_setUnsignedInt32"}, [TYPE_SFIXED32] = {"SF3", "int", "$_setSignedInt32"},
		[TYPE_INT64] = {"6", "Int64", "$_setInt64"}, [TYPE_UINT64] = {"U6", "Int64", "$_setInt64"},
		[TYPE_SINT64] = {"S6", "Int64", "$_setInt64"}, [TYPE_FIXED64] = {"F6", "Int64", "$_setInt64"},
		[TYPE_SFIXED64] = {"SF6", "Int64", "$_setInt64"}, [TYPE_STRING] = {"S", "String", "$_setString"},
		[TYPE_BYTES] = {"Y", "List", "$_setBytes"},
	};
	BaseType t = {0};
	int type = (int)pb_int(field, FD_TYPE, TYPE_DOUBLE);
	if (type >= 1 && type <= 18 && prim[type].suffix) {
		t.descriptor = type;
		t.typeConstantSuffix = prim[type].suffix;
		t.setter = prim[type].setter;
		t.unprefixed.prefix = strcmp(prim[type].name, "Int64") == 0 ? fixnumImportPrefix : coreImportPrefix;
		t.unprefixed.name = prim[type].name;
		if (type == TYPE_BYTES) {
			t.unprefixed.args[0] = &coreInt;
			t.unprefixed.nargs = 1;
		}
		return t;
	}
	if (type == TYPE_GROUP || type == TYPE_MESSAGE) {
		if (features->v[FS_MESSAGE_ENCODING] == DELIMITED) {
			t.typeConstantSuffix = "G";
			t.descriptor = TYPE_GROUP;
		} else {
			t.typeConstantSuffix = "M";
			t.descriptor = TYPE_MESSAGE;
		}
	} else {
		t.typeConstantSuffix = "E";
		t.descriptor = TYPE_ENUM;
	}
	const char *typeName = pb_str(field, FD_TYPE_NAME);
	Pc *generator = ctx_getFieldType(ctx, typeName);
	if (!generator)
		dart_throw(xfmt("FAILURE: Unknown type reference %s", typeName));
	t.unprefixed.name = generator->classname;
	t.generator = generator;
	return t;
}
