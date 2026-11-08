/* Port of protoc_plugin 25.1.0 lib/src/protobuf_field.dart.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* FeatureSet values of a message (NULL: none set). */
static Features features_of(PbMsg *fs)
{
	Features f = {0};
	for (int i = 1; i <= 8; i++)
		if (pb_has(fs, (uint32_t)i))
			f.v[i] = (int)pb_int(fs, (uint32_t)i, 0);
	return f;
}

/* resolveFeatures: parent.deepCopy()..mergeFromMessage(child) */
static Features resolveFeatures(const Features *parent, Features child)
{
	Features r = *parent;
	for (int i = 1; i <= 8; i++)
		if (child.v[i])
			r.v[i] = child.v[i];
	return r;
}

/* _inferLegacyProtoFeatures */
static Features inferLegacyProtoFeatures(PbMsg *descriptor, int edition)
{
	PbMsg *options = pb_msg(descriptor, FD_OPTIONS);
	if (edition >= EDITION_2023)
		return features_of(pb_msg(options, FO_FEATURES));
	Features f = {0};
	if (pb_int(descriptor, FD_LABEL, 1) == LABEL_REQUIRED)
		f.v[FS_FIELD_PRESENCE] = LEGACY_REQUIRED;
	if (pb_int(descriptor, FD_TYPE, 1) == TYPE_GROUP)
		f.v[FS_MESSAGE_ENCODING] = DELIMITED;
	if (pb_int(options, FO_PACKED, 0))
		f.v[FS_REPEATED_FIELD_ENCODING] = PACKED;
	if (edition == EDITION_PROTO3 && pb_has(options, FO_PACKED) && !pb_int(options, FO_PACKED, 0))
		f.v[FS_REPEATED_FIELD_ENCODING] = EXPANDED;
	return f;
}

static Field *field_new(PbMsg *descriptor, FieldNames *names, Pc *parent, const Features *inherit, GenerationContext *ctx)
{
	Field *f = xalloc(sizeof *f);
	f->descriptor = descriptor;
	f->memberNames = names;
	f->parent = parent;
	f->features = resolveFeatures(inherit, inferLegacyProtoFeatures(descriptor, parent->fileGen->edition));
	f->fullName = xfmt("%s.%s", parent->fullName, pb_str(descriptor, FD_NAME));
	f->baseType = bt_new(descriptor, &f->features, ctx);
	return f;
}

static int field_index(const Field *f) { return f->memberNames ? f->memberNames->index : -1; }
static int field_number(const Field *f) { return (int)pb_int(f->descriptor, FD_NUMBER, 0); }
static bool field_isDeprecated(const Field *f) { return pb_int(pb_msg(f->descriptor, FD_OPTIONS), FO_DEPRECATED, 0) != 0; }
static bool field_isRequired(const Field *f) { return f->features.v[FS_FIELD_PRESENCE] == LEGACY_REQUIRED; }
static bool field_isRepeated(const Field *f) { return pb_int(f->descriptor, FD_LABEL, 1) == LABEL_REPEATED; }

static bool field_isPacked(const Field *f)
{
	return field_isRepeated(f) && bt_isPackable(&f->baseType) && f->features.v[FS_REPEATED_FIELD_ENCODING] == PACKED;
}

static bool field_hasBooleanOption(const Field *f, uint32_t ext) { return pb_int(pb_msg(f->descriptor, FD_OPTIONS), ext, 0) != 0; }
static bool field_needsFixnumImport(const Field *f) { return strcmp(dt_str(&f->baseType.unprefixed), "$fixnum.Int64") == 0; }

static bool field_isMapField(const Field *f)
{
	if (!field_isRepeated(f) || !bt_isMessage(&f->baseType))
		return false;
	MsgGen *g = (MsgGen *)f->baseType.generator;
	return pb_int(pb_msg(g->descriptor, DP_OPTIONS), MO_MAP_ENTRY, 0) != 0;
}

static bool field_hasPresence(const Field *f) { return !field_isRepeated(f); }

/* quotedProtoName: NULL if _unCamelCase(jsonName) == name. */
static const char *field_quotedProtoName(const Field *f)
{
	Str json = pb_bytes(f->descriptor, FD_JSON_NAME);
	const char *name = pb_str(f->descriptor, FD_NAME);
	Buf b = {0};
	for (size_t i = 0; i < json.n; i++) {
		char c = json.p[i];
		if (c >= 'A' && c <= 'Z') {
			buf_addc(&b, '_');
			buf_addc(&b, (char)(c - 'A' + 'a'));
		} else {
			buf_addc(&b, c);
		}
	}
	if (b.n == strlen(name) && memcmp(buf_str(&b), name, b.n) == 0)
		return NULL;
	return xfmt("'%s'", name);
}

static const DType *field_getDartMapKeyType(const Field *f)
{
	MsgGen *g = (MsgGen *)f->baseType.generator;
	if (g->fieldList.n < 1)
		dart_throw("RangeError (index): Index out of range: no indices are valid: 0");
	return bt_getDartType(&VEC_AT(g->fieldList, Field, 0)->baseType, f->parent->fileGen);
}

static const DType *field_getDartMapValueType(const Field *f)
{
	MsgGen *g = (MsgGen *)f->baseType.generator;
	if (g->fieldList.n < 2)
		dart_throw(xfmt("RangeError (index): Invalid value: Only valid value is 0: 1"));
	return bt_getDartType(&VEC_AT(g->fieldList, Field, 1)->baseType, f->parent->fileGen);
}

/* getDartType() */
static const DType *field_getDartType(const Field *f)
{
	if (field_isMapField(f))
		return dt_gen(protobufImportPrefix, "PbMap", field_getDartMapKeyType(f), field_getDartMapValueType(f));
	if (field_isRepeated(f))
		return bt_getRepeatedDartType(&f->baseType, f->parent->fileGen);
	return bt_getDartType(&f->baseType, f->parent->fileGen);
}

/* typeConstant: "$pb.PbFieldType.<prefix><suffix>"; the part after the last dot. */
static const char *field_typeConstantName(const Field *f)
{
	const char *prefix = "O";
	if (field_isRequired(f))
		prefix = "Q";
	else if (field_isPacked(f))
		prefix = "K";
	else if (field_isRepeated(f))
		prefix = "P";
	return xcat(prefix, f->baseType.typeConstantSuffix);
}

static Node *pbFieldType(const char *name) { return ds_path(protobufImportPrefix, "PbFieldType", name, (char *)0); }

/* ---- default values: the Dart expressions upstream returns as strings ---- */

typedef enum {
	DE_NULL,      /* no expression (null) */
	DE_TEXT,      /* a literal: int, double or bool text, maybe with a leading minus */
	DE_STRING,    /* a string literal (a: the quoted lexeme) */
	DE_CORE_DOUBLE, /* $core.double.<a> */
	DE_HEX_DOUBLE,  /* (<a>).toDouble() */
	DE_INT64_ZERO,  /* $fixnum.Int64.ZERO */
	DE_PARSE_LONG,  /* $pb.parseLongInt('<a>') */
	DE_BYTES,       /* () => <$core.int>[<code units of a>] */
	DE_MEMBER,      /* <type>.<a> */
} DefKind;

typedef struct {
	DefKind kind;
	const char *a;
	Str s;
	const DType *type;
} DefExpr;

static const DefExpr defNull = {DE_NULL, NULL, {0}, NULL};

static DefExpr def_text(const char *a) { return (DefExpr){DE_TEXT, a, {0}, NULL}; }

/* The text of the expression (for upstream's string comparisons). */
static bool def_is(const DefExpr *d, const char *text)
{
	switch (d->kind) {
	case DE_NULL: return strcmp(text, "null") == 0;
	case DE_TEXT: case DE_STRING: return strcmp(d->a, text) == 0;
	case DE_INT64_ZERO: return strcmp(text, "$fixnum.Int64.ZERO") == 0;
	default: return false;
	}
}

static bool is_digits(const char *s, size_t n)
{
	if (!n)
		return false;
	for (size_t i = 0; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	return true;
}

/* A literal from upstream's text: [-]digits, [-]double, true/false; anything else would not be
 * valid Dart and make the formatter fail. */
static Node *literal_node(const char *text)
{
	if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0)
		return ds_lit(N_BooleanLiteral, TT_IDENTIFIER, text);
	if (*text == '-')
		return ds_prefix(TT_MINUS, "-", literal_node(text + 1));
	size_t n = strlen(text);
	if (is_digits(text, n))
		return ds_lit(N_IntegerLiteral, TT_INT, text);
	/* [digits].digits[e[+-]digits] or digits e[+-]digits (protobuf_field.dart's regexes) */
	size_t i = 0;
	while (i < n && text[i] >= '0' && text[i] <= '9')
		i++;
	bool ok = true, point = false;
	if (i < n && text[i] == '.') {
		size_t j = ++i;
		while (i < n && text[i] >= '0' && text[i] <= '9')
			i++;
		ok = i > j;
		point = true;
	}
	if (ok && i < n && (text[i] == 'e' || text[i] == 'E')) {
		i++;
		if (i < n && (text[i] == '+' || text[i] == '-'))
			i++;
		ok = is_digits(text + i, n - i);
		i = n;
	} else if (!point) {
		ok = false;
	}
	if (!ok || i != n)
		fail("dart_style: could not format because the source could not be parsed: unexpected default value '%s'",
		     text);
	return ds_lit(N_DoubleLiteral, TT_DOUBLE, text);
}

/* A fresh node of the expression (NULL for DE_NULL). */
static Node *def_node(const DefExpr *d)
{
	switch (d->kind) {
	case DE_NULL: return NULL;
	case DE_TEXT: return literal_node(d->a);
	case DE_STRING: return ds_str(d->a);
	case DE_CORE_DOUBLE: return ds_path(coreImportPrefix, "double", d->a, (char *)0);
	case DE_HEX_DOUBLE: return ds_call(ds_paren(ds_lit(N_IntegerLiteral, TT_HEXADECIMAL, d->a)), "toDouble", NULL, ds_args((Node *)0));
	case DE_INT64_ZERO: return ds_path(fixnumImportPrefix, "Int64", "ZERO", (char *)0);
	case DE_PARSE_LONG: return ds_call(ds_id(protobufImportPrefix), "parseLongInt", NULL, ds_args(ds_str(xfmt("'%s'", d->a)), (Node *)0));
	case DE_BYTES: {
		NodeList elements = {0};
		size_t n = d->s.n;
		for (size_t i = 0; i < n;) {
			uint32_t r;
			i += (size_t)utf8_decode(d->s.p + i, n - i, &r);
			uint32_t units[2] = {r, 0};
			int nu = 1;
			if (r >= 0x10000) {
				units[0] = 0xD800 + ((r - 0x10000) >> 10);
				units[1] = 0xDC00 + ((r - 0x10000) & 0x3FF);
				nu = 2;
			}
			for (int k = 0; k < nu; k++) {
				Node *e = ds_lit(N_IntegerLiteral, TT_HEXADECIMAL, xfmt("0x%x", (int)units[k]));
				if (elements.n)
					ds_nl(e, 0, 0);
				ds_push(&elements, e);
			}
		}
		return ds_fnexpr(ds_params(NONE, NONE, false),
				 ds_arrow(ds_list(NULL, ds_targs(core_type("int"), (Node *)0), elements)));
	}
	case DE_MEMBER: return ds_prop(dt_expr(d->type), d->a);
	}
	return NULL;
}

static bool hasDefaultValue(const Field *f) { return pb_has(f->descriptor, FD_DEFAULT_VALUE); }
static const char *defaultValue(const Field *f) { return pb_str(f->descriptor, FD_DEFAULT_VALUE); }

static DefExpr getDefaultAsBoolExpr(const Field *f, const char *noDefault)
{
	if (hasDefaultValue(f) && strcmp(defaultValue(f), "false") != 0)
		return def_text(defaultValue(f));
	return noDefault ? def_text(noDefault) : defNull;
}

static DefExpr getDefaultAsStringExpr(const Field *f, const char *noDefault)
{
	Str v = pb_bytes(f->descriptor, FD_DEFAULT_VALUE);
	if (!hasDefaultValue(f) || !v.n)
		return noDefault ? (DefExpr){DE_STRING, noDefault, {0}, NULL} : defNull;
	return (DefExpr){DE_STRING, quoted(v), {0}, NULL};
}

static DefExpr getDefaultAsInt32Expr(const Field *f, const char *noDefault)
{
	if (hasDefaultValue(f) && strcmp(defaultValue(f), "0") != 0)
		return def_text(defaultValue(f));
	return noDefault ? def_text(noDefault) : defNull;
}

/* getDefaultExpr: the default value for the getter ("null" if unavailable). */
static DefExpr field_getDefaultExpr(const Field *f)
{
	if (field_isRepeated(f))
		return defNull;
	switch (pb_int(f->descriptor, FD_TYPE, TYPE_DOUBLE)) {
	case TYPE_BOOL: return getDefaultAsBoolExpr(f, "false");
	case TYPE_INT32: case TYPE_UINT32: case TYPE_SINT32: case TYPE_FIXED32: case TYPE_SFIXED32:
		return getDefaultAsInt32Expr(f, "0");
	case TYPE_STRING: return getDefaultAsStringExpr(f, "''");
	default: return defNull;
	}
}

/* ^[+-]?... regexes of protobuf_field.dart (case-insensitive) */
static bool match_hex(const char *s)
{
	if (!(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) || !s[2])
		return false;
	for (s += 2; *s; s++)
		if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')))
			return false;
	return true;
}

static const char *skip_sign(const char *s) { return *s == '+' || *s == '-' ? s + 1 : s; }

static bool match_integer(const char *s)
{
	s = skip_sign(s);
	return is_digits(s, strlen(s));
}

static bool match_exponent(const char *s)
{
	if (*s != 'e' && *s != 'E')
		return false;
	s = skip_sign(s + 1);
	return is_digits(s, strlen(s));
}

static bool match_decimalA(const char *s)
{
	s = skip_sign(s);
	while (*s >= '0' && *s <= '9')
		s++;
	if (*s++ != '.')
		return false;
	const char *d = s;
	while (*s >= '0' && *s <= '9')
		s++;
	if (s == d)
		return false;
	return !*s || match_exponent(s);
}

static bool match_decimalB(const char *s)
{
	s = skip_sign(s);
	const char *d = s;
	while (*s >= '0' && *s <= '9')
		s++;
	return s > d && match_exponent(s);
}

static const char *enum_dartName(EnumGen *e, const char *valueName);

/* generateDefaultFunction */
static DefExpr field_generateDefaultFunction(const Field *f, bool omitIfFirstEnumValue)
{
	FileGen *fg = f->parent->fileGen;
	switch (pb_int(f->descriptor, FD_TYPE, TYPE_DOUBLE)) {
	case TYPE_BOOL: return getDefaultAsBoolExpr(f, NULL);
	case TYPE_FLOAT: case TYPE_DOUBLE: {
		if (!hasDefaultValue(f))
			return defNull;
		const char *v = defaultValue(f);
		if (strcmp(v, "0.0") == 0 || strcmp(v, "0") == 0)
			return defNull;
		if (strcmp(v, "inf") == 0)
			return (DefExpr){DE_CORE_DOUBLE, "infinity", {0}, NULL};
		if (strcmp(v, "-inf") == 0)
			return (DefExpr){DE_CORE_DOUBLE, "negativeInfinity", {0}, NULL};
		if (strcmp(v, "nan") == 0)
			return (DefExpr){DE_CORE_DOUBLE, "nan", {0}, NULL};
		if (match_hex(v))
			return (DefExpr){DE_HEX_DOUBLE, v, {0}, NULL};
		if (match_integer(v))
			return def_text(xcat(v, ".0"));
		if (match_decimalA(v) || match_decimalB(v))
			return def_text(v);
		dart_throw(xfmt("dart-protoc-plugin: invalid default value (%s) found in field %s", v, f->fullName));
	}
	case TYPE_INT32: case TYPE_UINT32: case TYPE_SINT32: case TYPE_FIXED32: case TYPE_SFIXED32:
		return getDefaultAsInt32Expr(f, NULL);
	case TYPE_INT64: case TYPE_UINT64: case TYPE_SINT64: case TYPE_FIXED64: case TYPE_SFIXED64: {
		const char *v = hasDefaultValue(f) ? defaultValue(f) : "0";
		if (strcmp(v, "0") == 0)
			return (DefExpr){DE_INT64_ZERO, NULL, {0}, NULL};
		return (DefExpr){DE_PARSE_LONG, v, {0}, NULL};
	}
	case TYPE_STRING: return getDefaultAsStringExpr(f, NULL);
	case TYPE_BYTES: {
		Str v = pb_bytes(f->descriptor, FD_DEFAULT_VALUE);
		if (!hasDefaultValue(f) || !v.n)
			return defNull;
		return (DefExpr){DE_BYTES, NULL, v, NULL};
	}
	case TYPE_GROUP: case TYPE_MESSAGE:
		return (DefExpr){DE_MEMBER, "getDefault", {0}, bt_getDartType(&f->baseType, fg)};
	case TYPE_ENUM: {
		const DType *className = bt_getDartType(&f->baseType, fg);
		EnumGen *gen = (EnumGen *)f->baseType.generator;
		if (hasDefaultValue(f) && *defaultValue(f))
			return (DefExpr){DE_MEMBER, defaultValue(f), {0}, className};
		if (gen->canonicalValues.n) {
			if (omitIfFirstEnumValue)
				return defNull;
			PbMsg *first = VEC_AT(gen->canonicalValues, PbMsg, 0);
			return (DefExpr){DE_MEMBER, enum_dartName(gen, pb_str(first, EVDP_NAME)), {0}, className};
		}
		return defNull;
	}
	default:
		dart_throw(xfmt("dart-protoc-plugin: generatedDefaultFunction not implemented for type (%s) found in field %s",
				"", f->fullName));
	}
}

/* ---- generateBuilderInfoCall ---- */

typedef struct {
	NodeList positionals, named;
} Args;

/* named[key] = value (null values are left out by _formatArguments) */
static void args_named(Args *a, const char *key, Node *value)
{
	if (value)
		ds_push(&a->named, ds_named(key, value));
}

static Node *args_node(Args *a)
{
	NodeList all = a->positionals;
	for (int i = 0; i < a->named.n; i++)
		ds_push(&all, a->named.v[i]);
	return ds_argl(all);
}

static Node *protoName_node(const Field *f)
{
	const char *q = field_quotedProtoName(f);
	return q ? ds_str(q) : NULL;
}

static Node *pkg_name_node(const char *package)
{
	return ds_new(KW_CONST, pb_type("PackageName"), NULL, ds_args(ds_str(xfmt("'%s'", package)), (Node *)0));
}

/* Returns the `..<invocation>(args)` cascade section adding this field to a BuilderInfo. */
static Node *field_generateBuilderInfoCall(Field *f, Out *o, const char *package)
{
	out_addSuffix(o, "omit_field_names");
	FileGen *fg = f->parent->fileGen;
	const DType *type = bt_getDartType(&f->baseType, fg);
	const char *typeStr = dt_str(type);
	const char *tc = field_typeConstantName(f);
	const char *invocation;
	Node *typeArgs = NULL;
	Args a = {0};
	Node *protoName = protoName_node(f);
	ds_push(&a.positionals, ds_int(field_number(f)));
	ds_push(&a.positionals, ccd_createTernary("omit_field_names", pb_bytes(f->descriptor, FD_JSON_NAME)));
	args_named(&a, "protoName", protoName);

	if (field_isMapField(f)) {
		MsgGen *g = (MsgGen *)f->baseType.generator;
		if (g->fieldList.n < 2)
			dart_throw("RangeError (index): Index out of range: index should be less than 2");
		Field *key = VEC_AT(g->fieldList, Field, 0), *value = VEC_AT(g->fieldList, Field, 1);
		const DType *keyType = bt_getDartType(&key->baseType, fg);
		const DType *valueType = bt_getDartType(&value->baseType, fg);
		invocation = "m";
		typeArgs = ds_targs(dt_node(keyType, false), dt_node(valueType, false), (Node *)0);
		const char *messageName = ((Pc *)g)->fullName + (*((Pc *)g)->package ? strlen(((Pc *)g)->package) + 1 : 0);
		args_named(&a, "entryClassName", ds_str(xfmt("'%s'", messageName)));
		args_named(&a, "keyFieldType", pbFieldType(field_typeConstantName(key)));
		args_named(&a, "valueFieldType", pbFieldType(field_typeConstantName(value)));
		if (bt_isMessage(&value->baseType) || bt_isGroup(&value->baseType)) {
			args_named(&a, "valueCreator", ds_prop(dt_expr(valueType), "$_createMessage"));
			DefExpr d = field_generateDefaultFunction(value, false);
			args_named(&a, "valueDefaultOrMaker", def_node(&d));
		}
		if (bt_isEnum(&value->baseType)) {
			args_named(&a, "valueOf", ds_prop(dt_expr(valueType), "valueOf"));
			args_named(&a, "enumValues", ds_prop(dt_expr(valueType), "values"));
			DefExpr d = field_generateDefaultFunction(value, false);
			args_named(&a, "valueDefaultOrMaker", def_node(&d));
			args_named(&a, "defaultEnumValue", def_node(&d));
		}
		if (*package)
			args_named(&a, "packageName", pkg_name_node(package));
	} else if (field_isRepeated(f)) {
		if (strcmp(tc, "PS") == 0) {
			invocation = "pPS";
		} else if (strcmp(tc, "PE") == 0) {
			invocation = "pPE";
			typeArgs = ds_targs(dt_node(type, false), (Node *)0);
			args_named(&a, "enumValues", ds_prop(dt_expr(type), "values"));
			DefExpr d = field_generateDefaultFunction(f, true);
			args_named(&a, "defaultEnumValue", def_node(&d));
		} else if (strcmp(tc, "PM") == 0) {
			invocation = "pPM";
			typeArgs = ds_targs(dt_node(type, false), (Node *)0);
			args_named(&a, "subBuilder", ds_prop(dt_expr(type), "$_createMessage"));
		} else {
			ds_push(&a.positionals, pbFieldType(tc));
			bool msg = bt_isMessage(&f->baseType) || bt_isGroup(&f->baseType);
			invocation = msg || bt_isEnum(&f->baseType) ? "pc" : "p";
			typeArgs = ds_targs(dt_node(type, false), (Node *)0);
			if (msg) {
				args_named(&a, "subBuilder", ds_prop(dt_expr(type), "$_createMessage"));
			} else if (bt_isEnum(&f->baseType)) {
				args_named(&a, "valueOf", ds_prop(dt_expr(type), "valueOf"));
				args_named(&a, "enumValues", ds_prop(dt_expr(type), "values"));
				DefExpr d = field_generateDefaultFunction(f, false);
				args_named(&a, "defaultEnumValue", def_node(&d));
			}
		}
	} else {
		DefExpr makeDefault = field_generateDefaultFunction(f, true);
		bool hasDefault = makeDefault.kind != DE_NULL;
		if (bt_isEnum(&f->baseType)) {
			invocation = "aE";
			typeArgs = ds_targs(dt_node(type, false), (Node *)0);
			if (strcmp(tc, "OE") != 0)
				args_named(&a, "fieldType", pbFieldType(tc));
			args_named(&a, "defaultOrMaker", def_node(&makeDefault));
			args_named(&a, "enumValues", ds_prop(dt_expr(type), "values"));
		} else if (strcmp(typeStr, "$core.int") == 0) {
			invocation = "aI";
			if (strcmp(tc, "O3") != 0)
				args_named(&a, "fieldType", pbFieldType(tc));
			args_named(&a, "defaultOrMaker", def_node(&makeDefault));
		} else if (strcmp(typeStr, "$core.double") == 0) {
			invocation = "aD";
			if (strcmp(tc, "OD") != 0)
				args_named(&a, "fieldType", pbFieldType(tc));
			args_named(&a, "defaultOrMaker", def_node(&makeDefault));
		} else if (!hasDefault) {
			if (strcmp(typeStr, "$core.String") == 0 && strcmp(tc, "OS") == 0) {
				invocation = "aOS";
			} else if (strcmp(typeStr, "$core.String") == 0 && strcmp(tc, "QS") == 0) {
				invocation = "aQS";
			} else if (strcmp(typeStr, "$core.bool") == 0 && strcmp(tc, "OB") == 0) {
				invocation = "aOB";
			} else {
				invocation = "a";
				typeArgs = ds_targs(dt_node(type, false), (Node *)0);
				ds_push(&a.positionals, pbFieldType(tc));
			}
		} else if (def_is(&makeDefault, "$fixnum.Int64.ZERO") && strcmp(typeStr, "$fixnum.Int64") == 0 &&
			   strcmp(tc, "O6") == 0) {
			invocation = "aInt64";
		} else {
			if (bt_isMessage(&f->baseType) || bt_isGroup(&f->baseType))
				args_named(&a, "subBuilder", ds_prop(dt_expr(type), "$_createMessage"));
			typeArgs = ds_targs(dt_node(type, false), (Node *)0);
			if (bt_isMessage(&f->baseType)) {
				invocation = field_isRequired(f) ? "aQM" : "aOM";
			} else {
				invocation = "a";
				args_named(&a, "defaultOrMaker", def_node(&makeDefault));
				ds_push(&a.positionals, pbFieldType(tc));
			}
		}
	}
	return ds_csec_call(invocation, typeArgs, args_node(&a));
}
