/* Port of protoc_plugin 25.1.0 lib/names.dart, with the reserved names of package:protobuf's
 * lib/meta.dart.
 *
 * Copyright (c) 2016, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static const char *const dartReservedWords[] = {
	"assert", "bool", "break", "case", "catch", "class", "const", "continue", "default", "do", "double",
	"else", "enum", "extends", "false", "final", "finally", "for", "if", "in", "int", "is", "new", "null",
	"rethrow", "return", "super", "switch", "this", "throw", "true", "try", "var", "void", "while", "with",
	NULL,
};

/* GeneratedMessage_reservedNames (package:protobuf meta.dart) */
static const char *const generatedMessageReservedNames[] = {
	"==", "GeneratedMessage", "Object", "addExtension", "check", "clear", "clearExtension", "clearField",
	"clone", "copyWith", "createEmptyInstance", "extensionsAreInitialized", "freeze", "getDefaultForField",
	"getExtension", "getField", "getFieldOrNull", "getTagNumber", "hasExtension", "hasField",
	"hasRequiredFields", "hashCode", "info_", "isFrozen", "isInitialized", "mergeFromBuffer",
	"mergeFromCodedBufferReader", "mergeFromJson", "mergeFromJsonMap", "mergeFromMessage",
	"mergeFromProto3Json", "mergeUnknownFields", "noSuchMethod", "runtimeType", "setExtension", "setField",
	"toBuilder", "toDebugString", "toProto3Json", "toString", "unknownFields", "writeToBuffer",
	"writeToCodedBufferWriter", "writeToJson", "writeToJsonMap", "$_clearField", "$_ensure", "$_get",
	"$_getI64", "$_getList", "$_getMap", "$_getN", "$_getB", "$_getBF", "$_getI", "$_getIZ", "$_getS",
	"$_getSZ", "$_has", "$_setBool", "$_setBytes", "$_setDouble", "$_setField", "$_setFloat", "$_setInt64",
	"$_setSignedInt32", "$_setString", "$_setUnsignedInt32", "$_whichOneof", NULL,
};

/* ProtobufEnum_reservedNames */
static const char *const protobufEnumReservedNames[] = {
	"==", "Object", "ProtobufEnum", "hashCode", "noSuchMethod", "runtimeType", "toString", NULL,
};

static const char *const generatedMessageNames[] = {"create", "getDefault", "List", "notSet", NULL};
static const char *const protobufEnumNames[] = {"List", "valueOf", "values", NULL};
static const char *const oneofEnumMemberNames[] = {"default", "index", "values", NULL};
static const char *const forbiddenTopLevelExtra[] = {"List", "Function", "Map", NULL};

static void set_add(Map *set, const char *s)
{
	if (!map_has(set, s))
		map_put(set, s, (void *)1);
}

static void set_add_all(Map *set, const char *const *names)
{
	for (; *names; names++)
		set_add(set, *names);
}

/* forbiddenTopLevelNames */
static void forbiddenTopLevelNames(Map *set)
{
	set_add_all(set, forbiddenTopLevelExtra);
	set_add_all(set, dartReservedWords);
}

/* reservedMemberNames == forbiddenExtensionNames */
static void reservedMemberNames(Map *set)
{
	set_add_all(set, dartReservedWords);
	set_add_all(set, generatedMessageReservedNames);
	set_add_all(set, generatedMessageNames);
}

/* reservedEnumNames */
static void reservedEnumNames(Map *set)
{
	set_add_all(set, protobufEnumReservedNames);
	set_add_all(set, dartReservedWords);
	set_add_all(set, protobufEnumNames);
}

/* Unhandled exceptions of the Dart program: the error text of a plugin that crashed (without the
 * stack trace, see README). */
static NORETURN void dart_throw(const char *msg) { fail("Unhandled exception:\n%s", msg); }

static bool startsWithDigit(const char *s) { return *s >= '0' && *s <= '9'; }

static char *avoidInitialUnderscore(const char *input)
{
	size_t n = strlen(input), lead = 0;
	while (lead < n && input[lead] == '_')
		lead++;
	if (lead == n && n > 0)
		fail("protoc_plugin would not terminate: avoidInitialUnderscore of '%s' loops forever", input);
	Buf b = {0};
	if (startsWithDigit(input + lead))
		buf_addc(&b, 'x');
	buf_adds(&b, input + lead);
	for (size_t i = 0; i < lead; i++)
		buf_addc(&b, '_');
	return (char *)buf_str(&b);
}

/* String.toUpperCase / toLowerCase of the first code unit (ASCII). */
static char *capitalize(const char *s)
{
	char *r = xstrdup(s);
	if (*r >= 'a' && *r <= 'z')
		*r = (char)(*r - 'a' + 'A');
	return r;
}

static char *lowerCaseFirstLetter(const char *input)
{
	if (!*input)
		dart_throw("RangeError (index): Index out of range: no indices are valid: 0");
	char *r = xstrdup(input);
	if (*r >= 'A' && *r <= 'Z')
		*r = (char)(*r - 'A' + 'a');
	return r;
}

static char *underscoresToCamelCase(const char *s)
{
	Buf b = {0};
	Vec parts = str_split(s, '_');
	for (int i = 0; i < parts.n; i++)
		buf_adds(&b, capitalize(VEC_AT(parts, char, i)));
	return (char *)buf_str(&b);
}

/* legalDartIdentifier: [^a-zA-Z0-9$_] -> '_' per UTF-16 code unit. */
static char *legalDartIdentifier(const char *s)
{
	Buf b = {0};
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		unsigned char c = (unsigned char)s[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_') {
			buf_addc(&b, (char)c);
			i++;
			continue;
		}
		uint32_t r;
		i += (size_t)utf8_decode(s + i, n - i, &r);
		buf_adds(&b, r >= 0x10000 ? "__" : "_");
	}
	return (char *)buf_str(&b);
}

/* Suffix iterables of disambiguateName. */
enum { SUFFIX_DEFAULT, SUFFIX_ENUM, SUFFIX_EXTENSION, SUFFIX_MEMBER };

static const char *suffix_at(int kind, int i, int number)
{
	Buf b = {0};
	switch (kind) {
	case SUFFIX_DEFAULT: /* '_', '_0', '_1', ... */
		if (i == 0)
			return "_";
		return xfmt("_%d", i - 1);
	case SUFFIX_ENUM: /* '_', '__', ... */
		for (int k = 0; k <= i; k++)
			buf_addc(&b, '_');
		return (char *)buf_str(&b);
	case SUFFIX_EXTENSION: /* 'Ext', '2', '3', ... */
		return i == 0 ? "Ext" : xfmt("%d", i + 1);
	default: /* '_N', '_N_N', ... */
		for (int k = 0; k <= i; k++)
			buf_fmt(&b, "_%d", number);
		return (char *)buf_str(&b);
	}
}

/* generateVariants: up to 4 names. */
typedef int (*VariantsFn)(const char *candidate, const char **out, void *ctx);

static int variants_identity(const char *candidate, const char **out, void *ctx)
{
	(void)ctx;
	out[0] = candidate;
	return 1;
}

static char *disambiguateName(const char *name, Map *usedNames, int suffixKind, int number, VariantsFn generateVariants,
			      void *ctx)
{
	if (!generateVariants)
		generateVariants = variants_identity;
	const char *variants[4];
	int n = generateVariants(name, variants, ctx);
	const char *usedSuffix = "";
	bool available = true;
	for (int i = 0; i < n; i++)
		available &= !map_has(usedNames, variants[i]);
	for (int k = 0; !available; k++) {
		const char *suffix = suffix_at(suffixKind, k, number);
		n = generateVariants(xcat(name, suffix), variants, ctx);
		available = true;
		for (int i = 0; i < n; i++)
			available &= !map_has(usedNames, variants[i]);
		if (available)
			usedSuffix = suffix;
	}
	for (int i = 0; i < n; i++)
		set_add(usedNames, variants[i]);
	return xcat(name, usedSuffix);
}

static char *messageOrEnumClassName(const char *descriptorName, Map *usedNames, const char *parent)
{
	if (*parent)
		descriptorName = xcat(parent, "_", descriptorName);
	return disambiguateName(avoidInitialUnderscore(descriptorName), usedNames, SUFFIX_DEFAULT, 0, NULL, NULL);
}

static char *oneofEnumClassName(const char *descriptorName, Map *usedNames, const char *parentName)
{
	descriptorName = xcat(parentName, "_", underscoresToCamelCase(descriptorName));
	return disambiguateName(avoidInitialUnderscore(descriptorName), usedNames, SUFFIX_DEFAULT, 0, NULL, NULL);
}

static char *oneofEnumMemberName(const char *fieldName)
{
	Map used = {0};
	set_add_all(&used, oneofEnumMemberNames);
	return disambiguateName(fieldName, &used, SUFFIX_DEFAULT, 0, NULL, NULL);
}

/* _fileNameWithoutExtension: the last segment of Uri.file(name), without its extension. */
static char *fileNameWithoutExtension(const char *name)
{
	const char *slash = strrchr(name, '/');
	const char *file = slash ? slash + 1 : name;
	const char *dot = strrchr(file, '.');
	return dot ? xstrndup(file, (size_t)(dot - file)) : xstrdup(file);
}

static char *extensionClassName(PbMsg *descriptor, Map *usedNames)
{
	char *s = avoidInitialUnderscore(legalDartIdentifier(fileNameWithoutExtension(pb_str(descriptor, FDP_NAME))));
	if (!*s)
		dart_throw("RangeError (index): Index out of range: no indices are valid: 0");
	return disambiguateName(capitalize(s), usedNames, SUFFIX_EXTENSION, 0, NULL, NULL);
}

/* ---- message member names ---- */

static bool isRepeated(PbMsg *field) { return pb_int(field, FD_LABEL, 1) == LABEL_REPEATED; }

static bool isGroupOrMessage(PbMsg *field)
{
	int64_t t = pb_int(field, FD_TYPE, 1);
	return t == TYPE_MESSAGE || t == TYPE_GROUP;
}

/* _nameOption: the dart_name extension ("" if not set). */
static const char *nameOption(PbMsg *field) { return pb_str(pb_msg(field, FD_OPTIONS), DPB_EXT_DART_NAME); }

static const char *fieldMethodSuffix(PbMsg *field)
{
	const char *name = nameOption(field);
	if (*name)
		return capitalize(name);
	if (pb_int(field, FD_TYPE, 1) != TYPE_GROUP)
		return underscoresToCamelCase(pb_str(field, FD_NAME));
	/* For groups, use capitalization of 'typeName' rather than 'name'. */
	name = pb_str(field, FD_TYPE_NAME);
	const char *dot = strrchr(name, '.');
	if (dot)
		name = dot + 1;
	return underscoresToCamelCase(name);
}

static int field_variants(const char *name, const char **out, void *ctx)
{
	PbMsg *field = ctx;
	out[0] = lowerCaseFirstLetter(name);
	out[1] = xcat("has", name);
	out[2] = xcat("clear", name);
	if (isGroupOrMessage(field)) {
		out[3] = xcat("ensure", name);
		return 4;
	}
	return 3;
}

static FieldNames *unusedMemberNames(PbMsg *field, int index, int sourcePosition, Map *existingNames)
{
	FieldNames *f = xalloc(sizeof *f);
	f->descriptor = field;
	f->index = index;
	f->sourcePosition = sourcePosition;
	int number = (int)pb_int(field, FD_NUMBER, 0);
	if (isRepeated(field)) {
		f->fieldName = disambiguateName(lowerCaseFirstLetter(fieldMethodSuffix(field)), existingNames, SUFFIX_MEMBER,
						number, NULL, NULL);
		return f;
	}
	const char *name = disambiguateName(fieldMethodSuffix(field), existingNames, SUFFIX_MEMBER, number, field_variants,
					    field);
	f->fieldName = avoidInitialUnderscore(lowerCaseFirstLetter(name));
	f->hasMethodName = xcat("has", name);
	f->clearMethodName = xcat("clear", name);
	if (isGroupOrMessage(field))
		f->ensureMethodName = xcat("ensure", name);
	return f;
}

/* ^[a-z]\w+$ */
static bool isDartFieldName(const char *name)
{
	if (!(*name >= 'a' && *name <= 'z') || !name[1])
		return false;
	for (const char *p = name + 1; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	return true;
}

static FieldNames *memberNamesFromOption(PbMsg *message, PbMsg *field, int index, int sourcePosition,
					 Map *existingNames)
{
	const char *where = xfmt("%s.%s", pb_str(message, DP_NAME), pb_str(field, FD_NAME));
	const char *name = nameOption(field);
	if (!isDartFieldName(name))
		dart_throw(xfmt("%s: dart_name option is invalid: '%s' is not a valid Dart field name", where, name));
#define CHECK_AVAILABLE(s)                                                                                             \
	if (map_has(existingNames, s))                                                                                 \
		dart_throw(xfmt("%s: dart_name option is invalid: '%s' is already used", where, s));
	CHECK_AVAILABLE(name);
	FieldNames *f = xalloc(sizeof *f);
	f->descriptor = field;
	f->index = index;
	f->sourcePosition = sourcePosition;
	f->fieldName = name;
	if (isRepeated(field))
		return f;
	f->hasMethodName = xcat("has", capitalize(name));
	CHECK_AVAILABLE(f->hasMethodName);
	f->clearMethodName = xcat("clear", capitalize(name));
	CHECK_AVAILABLE(f->clearMethodName);
	if (isGroupOrMessage(field)) {
		f->ensureMethodName = xcat("ensure", capitalize(name));
		CHECK_AVAILABLE(f->ensureMethodName);
	}
#undef CHECK_AVAILABLE
	return f;
}

/* countRealOneofs */
static int countRealOneofs(PbMsg *descriptor)
{
	int highest = -1;
	for (int i = 0; i < pb_count(descriptor, DP_FIELD); i++) {
		PbMsg *f = pb_msg_at(descriptor, DP_FIELD, i);
		if (pb_has(f, FD_ONEOF_INDEX) && !pb_int(f, FD_PROTO3_OPTIONAL, 0)) {
			int idx = (int)pb_int(f, FD_ONEOF_INDEX, 0);
			if (idx > highest)
				highest = idx;
		}
	}
	return highest + 1;
}

static int oneof_variants(const char *name, const char **out, void *ctx)
{
	(void)ctx;
	out[0] = xcat("which", name);
	out[1] = xcat("clear", name);
	return 2;
}

typedef struct {
	FieldNames **fieldNames;
	int nfields;
	OneofNames **oneofNames;
	int noneofs;
} MemberNames;

static void takeFieldNames(MemberNames *m, Map *existingNames, FieldNames *chosen)
{
	m->fieldNames[chosen->index] = chosen;
	set_add(existingNames, chosen->fieldName);
	if (chosen->hasMethodName)
		set_add(existingNames, chosen->hasMethodName);
	if (chosen->clearMethodName)
		set_add(existingNames, chosen->clearMethodName);
}

static MemberNames messageMemberNames(PbMsg *descriptor, const char *parentClassName, Map *usedTopLevelNames,
				      const char *const *reserved)
{
	int n = pb_count(descriptor, DP_FIELD);
	PbMsg **sorted = xalloc((size_t)(n + 1) * sizeof *sorted);
	Map sourcePositions = {0}, indexes = {0};
	for (int i = 0; i < n; i++) {
		sorted[i] = pb_msg_at(descriptor, DP_FIELD, i);
		map_put(&sourcePositions, pb_str(sorted[i], FD_NAME), (void *)(intptr_t)(i + 1));
	}
	/* List.sort with a comparator that throws for equal numbers (stable merge sort here; equal
	 * numbers throw). */
	for (int w = 1; w < n; w *= 2) {
		PbMsg **tmp = xalloc((size_t)n * sizeof *tmp);
		for (int lo = 0; lo < n; lo += 2 * w) {
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
			while (i < mid && j < hi) {
				int64_t a = pb_int(sorted[i], FD_NUMBER, 0), b = pb_int(sorted[j], FD_NUMBER, 0);
				if (a == b)
					dart_throw(xfmt("multiple fields defined for tag %lld in %s", (long long)a,
							pb_str(descriptor, DP_NAME)));
				tmp[k++] = b < a ? sorted[j++] : sorted[i++];
			}
			while (i < mid)
				tmp[k++] = sorted[i++];
			while (j < hi)
				tmp[k++] = sorted[j++];
		}
		sorted = tmp;
	}
	int nindexes = 0;
	for (int i = 0; i < n; i++) {
		const char *name = pb_str(sorted[i], FD_NAME);
		if (!map_has(&indexes, name))
			nindexes++;
		map_put(&indexes, name, (void *)(intptr_t)(nindexes));
	}
	Map existingNames = {0};
	reservedMemberNames(&existingNames);
	if (reserved)
		set_add_all(&existingNames, reserved);
	MemberNames m = {0};
	m.nfields = nindexes;
	m.fieldNames = xalloc((size_t)(nindexes + 1) * sizeof *m.fieldNames);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < n; i++) {
			PbMsg *field = sorted[i];
			const char *name = pb_str(field, FD_NAME);
			if ((*nameOption(field) != 0) != (pass == 0))
				continue;
			int index = (int)(intptr_t)map_get(&indexes, name) - 1;
			int sourcePosition = (int)(intptr_t)map_get(&sourcePositions, name) - 1;
			if (pass == 0)
				takeFieldNames(&m, &existingNames, memberNamesFromOption(descriptor, field, index, sourcePosition, &existingNames));
			else
				takeFieldNames(&m, &existingNames, unusedMemberNames(field, index, sourcePosition, &existingNames));
		}
	int realOneofCount = countRealOneofs(descriptor);
	m.oneofNames = xalloc((size_t)(realOneofCount + 1) * sizeof *m.oneofNames);
	for (int i = 0; i < realOneofCount; i++) {
		if (i >= pb_count(descriptor, DP_ONEOF_DECL))
			dart_throw(xfmt("RangeError (length): Invalid value: Not in inclusive range 0..%d: %d",
					pb_count(descriptor, DP_ONEOF_DECL) - 1, i));
		PbMsg *oneof = pb_msg_at(descriptor, DP_ONEOF_DECL, i);
		const char *oneofName = disambiguateName(underscoresToCamelCase(pb_str(oneof, ODP_NAME)), &existingNames,
							 SUFFIX_DEFAULT, 0, oneof_variants, NULL);
		const char *oneofEnumName = oneofEnumClassName(pb_str(oneof, ODP_NAME), usedTopLevelNames, parentClassName);
		const char *enumMapName = disambiguateName(xcat("_", oneofEnumName, "ByTag"), &existingNames, SUFFIX_DEFAULT,
							   0, NULL, NULL);
		OneofNames *o = xalloc(sizeof *o);
		o->descriptor = oneof;
		o->index = i;
		o->clearMethodName = xcat("clear", oneofName);
		o->whichOneofMethodName = xcat("which", oneofName);
		o->oneofEnumName = oneofEnumName;
		o->byTagMapName = enumMapName;
		m.oneofNames[m.noneofs++] = o;
		set_add(&existingNames, o->whichOneofMethodName);
		set_add(&existingNames, o->clearMethodName);
		set_add(&existingNames, o->byTagMapName);
	}
	return m;
}

/* extensionName */
static const char *extensionName(PbMsg *descriptor, Map *usedNames)
{
	return unusedMemberNames(descriptor, -1, -1, usedNames)->fieldName;
}
