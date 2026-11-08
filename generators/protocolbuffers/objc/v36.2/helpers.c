// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/helpers.{h,cc} (protobuf v36.2), with the
 * containers the generator uses (btree_set<std::string>, flat_hash_set<const FileDescriptor*>). */

/* ObjectiveCType */
typedef enum {
	OBJECTIVECTYPE_INT32, OBJECTIVECTYPE_UINT32, OBJECTIVECTYPE_INT64, OBJECTIVECTYPE_UINT64,
	OBJECTIVECTYPE_FLOAT, OBJECTIVECTYPE_DOUBLE, OBJECTIVECTYPE_BOOLEAN, OBJECTIVECTYPE_STRING,
	OBJECTIVECTYPE_DATA, OBJECTIVECTYPE_ENUM, OBJECTIVECTYPE_MESSAGE,
} ObjectiveCType;

/* FlagType */
typedef enum { FLAGTYPE_DESCRIPTOR_INITIALIZATION, FLAGTYPE_EXTENSION, FLAGTYPE_FIELD } FlagType;

/* CommentStringFlags */
enum { COMMENT_STRING_ADD_LEADING_NEWLINE = 1 << 1, COMMENT_STRING_FORCE_MULTILINE = 1 << 2 };

static char *objc_utoa(uint64_t v)
{
	char tmp[24];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	char *s = xalloc((size_t)n + 1);
	for (int i = 0; i < n; i++)
		s[i] = tmp[n - 1 - i];
	return s;
}

static char *objc_itoa(int64_t v)
{
	return v < 0 ? xcat("-", objc_utoa(0 - (uint64_t)v)) : objc_utoa((uint64_t)v);
}

/* EscapeTrigraphs: every '?' becomes "\?". */
static char *escape_trigraphs(const char *to_escape) { return str_replace_all(to_escape, "?", "\\?"); }

/* ExtensionIsCustomOption */
static bool extension_is_custom_option(const LpField *extension_field)
{
	return str_eq(extension_field->containing_type->file->name, "google/protobuf/descriptor.proto");
}

static COLD const char *get_capitalized_type(const LpField *field)
{
	static const char *const names[] = {
		[PT_INT32] = "Int32",     [PT_UINT32] = "UInt32",     [PT_SINT32] = "SInt32", [PT_FIXED32] = "Fixed32",
		[PT_SFIXED32] = "SFixed32", [PT_INT64] = "Int64",     [PT_UINT64] = "UInt64", [PT_SINT64] = "SInt64",
		[PT_FIXED64] = "Fixed64", [PT_SFIXED64] = "SFixed64", [PT_FLOAT] = "Float",   [PT_DOUBLE] = "Double",
		[PT_BOOL] = "Bool",       [PT_STRING] = "String",     [PT_BYTES] = "Bytes",   [PT_ENUM] = "Enum",
		[PT_GROUP] = "Group",     [PT_MESSAGE] = "Message",
	};
	if (field->type < 1 || field->type > PT_SINT64)
		fail("Can't get here.");
	return names[field->type];
}

static ObjectiveCType get_objective_c_type(const LpField *field)
{
	switch (field->type) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return OBJECTIVECTYPE_INT32;
	case PT_UINT32: case PT_FIXED32: return OBJECTIVECTYPE_UINT32;
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return OBJECTIVECTYPE_INT64;
	case PT_UINT64: case PT_FIXED64: return OBJECTIVECTYPE_UINT64;
	case PT_FLOAT: return OBJECTIVECTYPE_FLOAT;
	case PT_DOUBLE: return OBJECTIVECTYPE_DOUBLE;
	case PT_BOOL: return OBJECTIVECTYPE_BOOLEAN;
	case PT_STRING: return OBJECTIVECTYPE_STRING;
	case PT_BYTES: return OBJECTIVECTYPE_DATA;
	case PT_ENUM: return OBJECTIVECTYPE_ENUM;
	case PT_GROUP: case PT_MESSAGE: return OBJECTIVECTYPE_MESSAGE;
	}
	fail("Can't get here.");
}

static bool is_primitive_type(const LpField *field)
{
	ObjectiveCType type = get_objective_c_type(field);
	return type != OBJECTIVECTYPE_STRING && type != OBJECTIVECTYPE_DATA && type != OBJECTIVECTYPE_MESSAGE;
}

static bool is_reference_type(const LpField *field) { return !is_primitive_type(field); }

static const char *gpb_generic_value_field_name(const LpField *field)
{
	if (proto_field_is_repeated(field))
		return "valueMessage";
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: return "valueInt32";
	case LP_CPPTYPE_UINT32: return "valueUInt32";
	case LP_CPPTYPE_INT64: return "valueInt64";
	case LP_CPPTYPE_UINT64: return "valueUInt64";
	case LP_CPPTYPE_FLOAT: return "valueFloat";
	case LP_CPPTYPE_DOUBLE: return "valueDouble";
	case LP_CPPTYPE_BOOL: return "valueBool";
	case LP_CPPTYPE_STRING: return field->type == PT_BYTES ? "valueData" : "valueString";
	case LP_CPPTYPE_ENUM: return "valueEnum";
	case LP_CPPTYPE_MESSAGE: return "valueMessage";
	}
	fail("Can't get here.");
}

/* HandleExtremeFloatingPoint */
static char *handle_extreme_floating_point(char *val, bool add_float_suffix)
{
	if (str_eq(val, "nan"))
		return "NAN";
	if (str_eq(val, "inf"))
		return "INFINITY";
	if (str_eq(val, "-inf"))
		return "-INFINITY";
	/* float strings with ., e or E need to have f appended */
	if (add_float_suffix && (strchr(val, '.') || strchr(val, 'e') || strchr(val, 'E')))
		return xcat(val, "f");
	return val;
}

static const char *default_value(const LpField *field)
{
	/* repeated fields don't have defaults */
	if (proto_field_is_repeated(field))
		return "nil";
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32:
		/* gcc and llvm reject the decimal form of kint32min and kint64min */
		if ((int32_t)field->default_value.i == INT32_MIN)
			return "-0x80000000";
		return objc_itoa((int32_t)field->default_value.i);
	case LP_CPPTYPE_UINT32:
		return xcat(objc_utoa((uint32_t)field->default_value.u), "U");
	case LP_CPPTYPE_INT64:
		if (field->default_value.i == INT64_MIN)
			return "-0x8000000000000000LL";
		return xcat(objc_itoa(field->default_value.i), "LL");
	case LP_CPPTYPE_UINT64:
		return xcat(objc_utoa(field->default_value.u), "ULL");
	case LP_CPPTYPE_DOUBLE:
		return handle_extreme_floating_point(lp_simple_dtoa(field->default_value.d), false);
	case LP_CPPTYPE_FLOAT:
		return handle_extreme_floating_point(lp_simple_ftoa(field->default_value.f), true);
	case LP_CPPTYPE_BOOL:
		return field->default_value.b ? "YES" : "NO";
	case LP_CPPTYPE_STRING: {
		Str s = field->default_value_string;
		/* the empty string is the default for both strings and data */
		if (!field->has_default_value || s.n == 0)
			return "nil";
		if (field->type == PT_BYTES) {
			/* a C string prefixed with its length in network byte order, cast to NSData* */
			Buf bytes = {0};
			uint32_t n = (uint32_t)s.n;
			uint8_t len[4] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
			buf_add(&bytes, len, 4);
			buf_add(&bytes, s.p, s.n);
			return xcat("(NSData*)\"", escape_trigraphs(lp_cescape(buf_view(&bytes))), "\"");
		}
		return xcat("@\"", escape_trigraphs(lp_cescape(s)), "\"");
	}
	case LP_CPPTYPE_ENUM:
		if (!field->default_value_enum)
			fail("enum field without default value");
		return lp_objc_enum_value_name(field->default_value_enum);
	case LP_CPPTYPE_MESSAGE:
		return "nil";
	}
	fail("Can't get here.");
}

/* BuildFlagsString over strings[0..n) */
static const char *build_flags_string(FlagType flag_type, const char *const *strings, int n)
{
	static const char *const zero_names[] = {"GPBDescriptorInitializationFlag_None", "GPBExtensionNone",
						 "GPBFieldNone"};
	static const char *const enum_names[] = {"GPBDescriptorInitializationFlags", "GPBExtensionOptions",
						 "GPBFieldFlags"};
	if (n == 0)
		return zero_names[flag_type];
	if (n == 1)
		return strings[0];
	Buf b = {0};
	buf_adds(&b, "(");
	buf_adds(&b, enum_names[flag_type]);
	buf_adds(&b, ")(");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			buf_adds(&b, " | ");
		buf_adds(&b, strings[i]);
	}
	buf_addc(&b, ')');
	return buf_str(&b);
}

static char *objc_class(const char *class_name) { return xcat("GPBObjCClass(", class_name, ")"); }

static char *objc_class_declaration(const char *class_name) { return xcat("GPBObjCClassDeclaration(", class_name, ");"); }

/* The comment line of EmitCommentsString: one leading space stripped, trailing whitespace
 * stripped, then backslash and '@' escaped with a backslash and the comment delimiters split
 * ("/" "*" -> "/" "\*", "*" "/" -> "*" "\/"), like absl::StrReplaceAll (leftmost match, no
 * overlaps). */
static char *comment_line(Str l)
{
	if (l.n && l.p[0] == ' ')
		l.p++, l.n--;
	while (l.n && (l.p[l.n - 1] == ' ' || (l.p[l.n - 1] >= '\t' && l.p[l.n - 1] <= '\r')))
		l.n--;
	Buf b = {0};
	for (size_t i = 0; i < l.n; i++) {
		char c = l.p[i];
		if (c == '\\')
			buf_adds(&b, "\\\\");
		else if (c == '@')
			buf_adds(&b, "\\@");
		else if (c == '/' && i + 1 < l.n && l.p[i + 1] == '*')
			buf_adds(&b, "/\\*"), i++;
		else if (c == '*' && i + 1 < l.n && l.p[i + 1] == '/')
			buf_adds(&b, "*\\/"), i++;
		else
			buf_addc(&b, c);
	}
	return (char *)buf_str(&b);
}

typedef struct {
	Vec lines;
} CommentLines;

static void emit_comment_lines(LpPrinter *p, void *arg)
{
	CommentLines *c = arg;
	for (int i = 0; i < c->lines.n; i++) {
		LpSub subs[] = {LP_SUB("text", c->lines.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" *$ text$\n");
	}
}

/* EmitCommentsString(printer, opts, location, flags) */
static void emit_comments_string_location(LpPrinter *p, const GenerationOptions *opts, const LpSourceLocation *location,
					  unsigned flags)
{
	/* comments are non-functional */
	if (opts->experimental_strip_nonfunctional_codegen)
		return;
	Str comments = location->leading_comments.n ? location->leading_comments : location->trailing_comments;
	Str *raw_lines;
	int n = str_split_s(comments, '\n', &raw_lines);
	while (n > 0 && raw_lines[n - 1].n == 0)
		n--;
	if (n == 0)
		return;
	CommentLines c = {0};
	for (int i = 0; i < n; i++)
		vec_push(&c.lines, comment_line(raw_lines[i]));

	if (flags & COMMENT_STRING_ADD_LEADING_NEWLINE)
		lp_emit(p, NULL, 0, "\n");

	if (!(flags & COMMENT_STRING_FORCE_MULTILINE) && c.lines.n == 1) {
		LpSub subs[] = {LP_SUB("text", c.lines.v[0])};
		lp_emit(p, subs, 1,
			"\n"
			" /** $text$ */\n");
		return;
	}
	LpSub subs[] = {LP_SUB_CB("lines", emit_comment_lines, &c)};
	lp_emit(p, subs, 1,
		"\n"
		" /**\n"
		"  $lines$\n"
		"  **/\n");
}

/* EmitCommentsString(printer, opts, descriptor, flags) */
static void emit_comments_string(LpPrinter *p, const GenerationOptions *opts, const void *descriptor, unsigned flags)
{
	const LpFile *file = proto_desc_file(descriptor);
	const char *key = objc_itoa(file->id);
	LpSourceLocations *t = map_get(opts->source_locations, key);
	if (!t) {
		t = lp_source_locations(file);
		map_put(opts->source_locations, key, t);
	}
	LpSourceLocation location;
	if (lp_get_source_location(t, descriptor, &location))
		emit_comments_string_location(p, opts, &location, flags);
}

/* The deprecated option of a descriptor's options. */
static bool desc_deprecated(const ProtoDesc *d)
{
	uint32_t num;
	switch (d->kind) {
	case PROTO_FILE: num = FILEOPT_DEPRECATED; break;
	case PROTO_ENUM_VALUE: num = EVOPT_DEPRECATED; break;
	default: num = MSGOPT_DEPRECATED; break; /* messages, fields, enums: 3 */
	}
	return pb_int(d->options, num, 0) != 0;
}

/* GetOptionalDeprecatedAttribute(descriptor, file) (file: NULL for fields and enum values) */
static const char *get_optional_deprecated_attribute(const void *descriptor, const LpFile *file)
{
	const ProtoDesc *d = descriptor;
	bool is_deprecated = desc_deprecated(d);
	/* the file is only passed for messages and enums (and extensions) */
	bool is_file_level_deprecation = false;
	if (!is_deprecated && file) {
		is_file_level_deprecation = desc_deprecated((const ProtoDesc *)file);
		is_deprecated = is_file_level_deprecation;
	}
	if (!is_deprecated)
		return "";
	const LpFile *source_file = proto_desc_file(d);
	const char *message = is_file_level_deprecation
				      ? xcat(source_file->name, " is deprecated.")
				      : xcat(d->full_name, " is deprecated (see ", source_file->name, ").");
	return xcat("GPB_DEPRECATED_MSG(\"", message, "\")");
}

/* HasWKTWithObjCCategory / IsWKTWithObjCCategory */
static bool has_wkt_with_objc_category(const LpFile *file)
{
	return str_eq(file->name, "google/protobuf/any.proto") || str_eq(file->name, "google/protobuf/duration.proto") ||
	       str_eq(file->name, "google/protobuf/timestamp.proto");
}

static bool is_wkt_with_objc_category(const LpMessage *descriptor)
{
	if (!has_wkt_with_objc_category(descriptor->file))
		return false;
	return str_eq(descriptor->full_name, "google.protobuf.Any") ||
	       str_eq(descriptor->full_name, "google.protobuf.Duration") ||
	       str_eq(descriptor->full_name, "google.protobuf.Timestamp");
}

/* ---- SubstitutionMap ---- */

static LpSub *subst_find(const SubstitutionMap *m, const char *key)
{
	for (int i = 0; i < m->n; i++)
		if (str_eq(m->subs[i].key, key))
			return &m->subs[i];
	return NULL;
}

/* Set(Sub): sets or replaces a variable. */
static void subst_set_sub(SubstitutionMap *m, LpSub sub)
{
	LpSub *s = subst_find(m, sub.key);
	if (s) {
		*s = sub;
		return;
	}
	if (m->n == m->cap) {
		int cap = m->cap ? 2 * m->cap : 32;
		m->subs = xrealloc(m->subs, sizeof(LpSub) * (size_t)m->cap, sizeof(LpSub) * (size_t)cap);
		m->cap = cap;
	}
	m->subs[m->n++] = sub;
}

static void subst_set(SubstitutionMap *m, const char *key, const char *value) { subst_set_sub(m, LP_SUB(key, value)); }

static const char *subst_value(const SubstitutionMap *m, const char *key)
{
	LpSub *s = subst_find(m, key);
	if (!s || !s->value)
		fail(" Unknown variable: %s", key);
	return s->value;
}

/* Install(printer); the frame is popped with lp_pop_vars. */
static void subst_install(const SubstitutionMap *m, LpPrinter *p) { lp_push_vars(p, m->subs, m->n); }

/* ---- StringSet (btree_set<std::string>) ---- */

static void string_set_insert(StringSet *s, const char *v) { vec_push(&s->v, (void *)v); }

static int string_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* Sorts and removes duplicates. */
static void string_set_finish(StringSet *s)
{
	if (s->v.n > 1)
		qsort(s->v.v, (size_t)s->v.n, sizeof(void *), string_cmp);
	int n = 0;
	for (int i = 0; i < s->v.n; i++)
		if (n == 0 || strcmp(s->v.v[n - 1], s->v.v[i]) != 0)
			s->v.v[n++] = s->v.v[i];
	s->v.n = n;
}

/* StrJoin(set, "\n") of a finished set */
static const char *string_set_join(const StringSet *s, const char *sep) { return str_join(&s->v, sep); }

/* ---- FileSet (flat_hash_set<const FileDescriptor*>) ---- */

static const char *file_key(const LpFile *f) { return objc_itoa(f->id); }

static bool file_set_contains(const FileSet *s, const LpFile *f) { return map_get(&s->ids, file_key(f)) != NULL; }

static void file_set_insert(FileSet *s, const LpFile *f)
{
	const char *k = file_key(f);
	if (map_get(&s->ids, k))
		return;
	map_put(&s->ids, k, "");
	vec_push(&s->v, (void *)f);
	s->size++;
}

/* Erasing only unmarks the file: v keeps it (iterate with file_set_contains). */
static void file_set_erase(FileSet *s, const LpFile *f)
{
	const char *k = file_key(f);
	if (!map_get(&s->ids, k))
		return;
	map_put(&s->ids, k, NULL);
	s->size--;
}

static int file_name_cmp(const void *a, const void *b)
{
	return strcmp((*(const LpFile *const *)a)->name, (*(const LpFile *const *)b)->name);
}

/* The files sorted by name (FileDescriptorsOrderedByName). */
static void sort_files_by_name(Vec *files)
{
	if (files->n > 1)
		qsort(files->v, (size_t)files->n, sizeof(void *), file_name_cmp);
}
