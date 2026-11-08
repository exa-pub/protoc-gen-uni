/* Copyright (c) ScalaPB authors (Nadav Samet and contributors).
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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/DescriptorImplicits.scala (with
 * NameUtils.scala, the SealedOneofsCache and the ScalaCompatConstants), plus
 * FileOptionsCache.scala and the GeneratorParams. */

/* ---- GeneratorException (ProtobufGenerator.scala) ---- */

/* ProtobufGenerator.handleCodeGeneratorRequest reports the message of a GeneratorException; a
 * plugin that does not catch it (zio-grpc) gets the exception like any other: the stack trace,
 * whose first line is Throwable.toString (the case class keeps it). */
#define GENERATOR_EXCEPTION "scalapb.compiler.GeneratorException: "
static bool generator_exception_thrown;

NORETURN COLD static void generator_exception(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
NORETURN COLD static void generator_exception(const char *fmt, ...)
{
	Buf b = {0};
	buf_adds(&b, GENERATOR_EXCEPTION);
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	generator_exception_thrown = true;
	fail_str(buf_view(&b));
}

/* ---- GeneratorParams.scala ---- */

typedef struct {
	bool java_conversions, flat_package, grpc, single_line_to_proto_string, ascii_format_to_string, lenses,
		retain_source_code_info, scala3_sources;
} Params;

static Params P;
static Vec all_files; /* JFile*: request.allProtos */

/* ---- Java/Scala string helpers ---- */

static Vec vec_of(const char *s)
{
	Vec v = {0};
	vec_push(&v, (void *)s);
	return v;
}

static Vec vec_concat(const Vec *a, const Vec *b)
{
	Vec v = {0};
	for (int i = 0; a && i < a->n; i++)
		vec_push(&v, a->v[i]);
	for (int i = 0; b && i < b->n; i++)
		vec_push(&v, b->v[i]);
	return v;
}

static bool vec_contains_str(const Vec *v, const char *s)
{
	for (int i = 0; i < v->n; i++)
		if (str_eq(v->v[i], s))
			return true;
	return false;
}

/* Java String.split(regex) for a literal one-character separator: trailing empty strings are
 * removed, and a string without the separator yields itself. */
static Vec java_split(const char *s, char sep)
{
	Vec parts = str_split(s, sep);
	if (parts.n == 1)
		return parts;
	while (parts.n > 0 && !*(const char *)parts.v[parts.n - 1])
		parts.n--;
	return parts;
}

/* The repeated string field num of m as a Vec. */
static Vec pb_strs(const PbMsg *m, uint32_t num)
{
	Vec v = {0};
	for (int i = 0; i < pb_count(m, num); i++)
		vec_push(&v, (void *)jstr_at(m, num, i));
	return v;
}

static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* ---- NameUtils.scala ---- */

static char *snake_case_to_camel_case(const char *name, bool upper_initial)
{
	java_require_ascii(name);
	Buf b = {0};
	bool cap_next = upper_initial;
	for (int index = 0; name[index]; index++) {
		char c = name[index];
		if (is_lower(c)) {
			buf_addc(&b, cap_next ? (char)(c - 'a' + 'A') : c);
			cap_next = false;
		} else if (is_upper(c)) {
			buf_addc(&b, index == 0 && !cap_next ? (char)(c - 'A' + 'a') : c);
			cap_next = false;
		} else if (is_digit(c)) {
			buf_addc(&b, c);
			cap_next = true;
		} else {
			cap_next = true;
		}
	}
	return (char *)buf_str(&b);
}

static char *to_all_caps(const char *name)
{
	java_require_ascii(name);
	Buf b = {0};
	bool last_lower = false;
	for (const char *p = name; *p; p++) {
		char c = *p;
		if (is_lower(c)) {
			buf_addc(&b, (char)(c - 'a' + 'A'));
			last_lower = true;
		} else if (is_upper(c)) {
			if (last_lower)
				buf_addc(&b, '_');
			buf_addc(&b, c);
			last_lower = false;
		} else {
			buf_addc(&b, c);
			last_lower = false;
		}
	}
	return (char *)buf_str(&b);
}

/* DescriptorImplicits.allCapsToCamelCase */
static char *all_caps_to_camel_case(const char *name, bool upper_initial)
{
	java_require_ascii(name);
	Buf b = {0};
	bool cap_next = upper_initial;
	for (const char *p = name; *p; p++) {
		char c = *p;
		if (is_upper(c)) {
			buf_addc(&b, cap_next ? c : (char)(c - 'A' + 'a'));
			cap_next = false;
		} else if (is_lower(c)) {
			buf_addc(&b, c);
			cap_next = true;
		} else if (is_digit(c)) {
			buf_addc(&b, c);
			cap_next = true;
		} else {
			cap_next = true;
		}
	}
	return (char *)buf_str(&b);
}

static const char *const SCALA_RESERVED_WORDS[] = {
	"abstract", "case", "catch", "class", "def", "do", "else", "enum", "export", "extends", "false",
	"final", "finally", "for", "forSome", "given", "if", "implicit", "import", "infix", "inline", "lazy",
	"macro", "match", "ne", "new", "null", "object", "opaque", "open", "override", "package", "private",
	"protected", "return", "sealed", "super", "then", "this", "throw", "trait", "transparent", "try",
	"true", "type", "using", "val", "var", "while", "with", "yield",
};

static const char *as_symbol(const char *s)
{
	if (!s[0])
		java_exception("java.lang.StringIndexOutOfBoundsException", "index 0, length 0");
	for (size_t i = 0; i < sizeof SCALA_RESERVED_WORDS / sizeof *SCALA_RESERVED_WORDS; i++)
		if (str_eq(s, SCALA_RESERVED_WORDS[i]))
			return xcat("`", s, "`");
	if ((unsigned char)s[0] >= 0xc0 && (unsigned char)s[0] < 0xf0)
		java_require_ascii(s); /* Character.isDigit of a BMP character */
	if (is_digit(s[0]))
		return xcat("`", s, "`");
	return s;
}

static const char *const FORBIDDEN_FIELD_NAMES[] = {
	"hashCode", "equals", "class", "clone", "copy", "finalize", "getClass", "notify", "notifyAll",
	"toString", "wait", "productArity", "productElementName", "productElementNames", "productIterator",
	"productPrefix",
};

static bool is_forbidden_field_name(const char *s)
{
	for (size_t i = 0; i < sizeof FORBIDDEN_FIELD_NAMES / sizeof *FORBIDDEN_FIELD_NAMES; i++)
		if (str_eq(s, FORBIDDEN_FIELD_NAMES[i]))
			return true;
	return false;
}

/* ---- ScalaName ---- */

struct ScalaName {
	bool empty_package;
	Vec xs; /* const char* */
};

static ScalaName *sn_new(bool empty_package, Vec xs)
{
	ScalaName *n = xalloc(sizeof *n);
	n->empty_package = empty_package;
	n->xs = xs;
	return n;
}

static const char *sn_name(const ScalaName *n)
{
	if (!n->xs.n)
		java_exception("java.util.NoSuchElementException", NULL);
	return n->xs.v[n->xs.n - 1];
}

static const char *sn_name_relative(const ScalaName *n, int levels)
{
	Buf b = {0};
	int start = n->xs.n - (levels + 1);
	if (start < 0)
		start = 0;
	for (int i = start; i < n->xs.n; i++) {
		if (i > start)
			buf_addc(&b, '.');
		buf_adds(&b, as_symbol(n->xs.v[i]));
	}
	return buf_str(&b);
}

static const char *sn_name_symbol(const ScalaName *n) { return sn_name_relative(n, 0); }

static const char *sn_full_name(const ScalaName *n) { return sn_name_relative(n, n->xs.n); }

static const char *sn_full_name_with_maybe_root(const ScalaName *n)
{
	return !n->empty_package ? xcat("_root_.", sn_full_name(n)) : sn_full_name(n);
}

/* context_names: a set (Map) of the names (Seq.contains upstream) */
static const char *sn_full_name_with_maybe_root_ctx(const ScalaName *n, const Map *context_names)
{
	if (!n->xs.n)
		java_exception("java.util.NoSuchElementException", "head of empty list");
	const char *top = n->xs.v[0];
	if (map_has(context_names, top) && !n->empty_package)
		return xcat("_root_.", sn_full_name(n));
	return sn_full_name(n);
}

static ScalaName *sn_child(const ScalaName *n, const char *name)
{
	Vec xs = vec_concat(&n->xs, NULL);
	vec_push(&xs, (void *)name);
	return sn_new(n->empty_package, xs);
}

/* ---- ScalaCompatConstants ---- */

typedef struct {
	const char *wildcard_type, *wildcard_import, *comp_seq_type, *private_this, *with_operator, *message_lens;
} Compat;

static const Compat COMPAT2 = {
	"_", "_", "Seq[_root_.scalapb.GeneratedMessageCompanion[_ <: _root_.scalapb.GeneratedMessage]]",
	"private[this]", " with ", "_root_.scalapb.lenses.ObjectLens",
};
static const Compat COMPAT3 = {
	"?", "*", "Seq[_root_.scalapb.GeneratedMessageCompanion[? <: _root_.scalapb.GeneratedMessage]]",
	"private", " & ", "_root_.scalapb.lenses.MessageLens",
};

/* ---- DescriptorImplicits object ---- */

#define ScalaSeq "_root_.scala.Seq"
#define ScalaMap "_root_.scala.collection.immutable.Map"
#define ScalaVector "_root_.scala.collection.immutable.Vector"
#define ScalaIterable "_root_.scala.collection.immutable.Iterable"
#define ScalaIterator "_root_.scala.collection.Iterator"
#define ScalaOption "_root_.scala.Option"

static bool is_value_class_name(const char *s)
{
	return str_eq(s, "AnyVal") || str_eq(s, "scala.AnyVal") || str_eq(s, "_root_.scala.AnyVal");
}
static bool is_universal_trait_name(const char *s)
{
	return str_eq(s, "Any") || str_eq(s, "scala.Any") || str_eq(s, "_root_.scala.Any");
}

static const char *primitive_wrapper_type(const JMsg *m)
{
	static const char *const map[][2] = {
		{"google.protobuf.Int32Value", "_root_.scala.Int"},
		{"google.protobuf.Int64Value", "_root_.scala.Long"},
		{"google.protobuf.UInt32Value", "_root_.scala.Int"},
		{"google.protobuf.UInt64Value", "_root_.scala.Long"},
		{"google.protobuf.DoubleValue", "_root_.scala.Double"},
		{"google.protobuf.FloatValue", "_root_.scala.Float"},
		{"google.protobuf.StringValue", "_root_.scala.Predef.String"},
		{"google.protobuf.BoolValue", "_root_.scala.Boolean"},
		{"google.protobuf.BytesValue", "_root_.com.google.protobuf.ByteString"},
	};
	for (size_t i = 0; i < sizeof map / sizeof *map; i++)
		if (str_eq(m->full_name, map[i][0]))
			return map[i][1];
	return NULL;
}

/* ---- Helper ---- */

static const char *escape_comment(Str s)
{
	char *t = xstrndup(s.p, s.n);
	/* the comment is a Java string: NUL characters are kept */
	Buf b = {0};
	for (size_t i = 0; i < s.n; i++) {
		char c = t[i];
		if (c == '&')
			buf_adds(&b, "&amp;");
		else if (c == '/' && i + 1 < s.n && t[i + 1] == '*')
			buf_adds(&b, "/&#42;"), i++;
		else if (c == '*' && i + 1 < s.n && t[i + 1] == '/')
			buf_adds(&b, "*&#47;"), i++;
		else if (c == '@')
			buf_adds(&b, "&#64;");
		else if (c == '<')
			buf_adds(&b, "&lt;");
		else if (c == '>')
			buf_adds(&b, "&gt;");
		else if (c == '\\')
			buf_adds(&b, "&92;");
		else
			buf_addc(&b, c);
	}
	return buf_str(&b);
}


/* ---- options ---- */

static PbMsg *empty_opts(const PbMsgDef *def) { return pb_new(def); }

/* The scalapb extension (field 1020) of an options message, or an empty message. */
static PbMsg *local_opts(const PbMsg *options, const PbMsgDef *def)
{
	PbMsg *m = pb_msg(options, SCALAPB_EXT);
	return m ? m : empty_opts(def);
}

static PbMsg *file_opts(JFile *f);

static PbMsg *field_options(JField *f);
static PbMsg *message_options(JMsg *m);

/* ---- SealedOneofsCache ---- */

static bool msg_is_sealed_oneof_type(const JMsg *m)
{
	if (!m->sealed_type) {
		bool r = false;
		for (int i = 0; i < m->real_oneofs && !r; i++) {
			const char *n = VEC_AT(m->oneofs, JOneof, i)->name;
			r = str_eq(n, "sealed_value") || str_eq(n, "sealed_value_optional");
		}
		((JMsg *)m)->sealed_type = r ? 2 : 1;
	}
	return m->sealed_type == 2;
}

typedef struct {
	bool built; /* the results are in JMsg.sealed_cases and JMsg.sealed_container */
} SealedCache;
static SealedCache sealed_cache;

static void all_messages_rec(JMsg *m, Vec *out)
{
	vec_push(out, m);
	for (int i = 0; i < m->nested.n; i++)
		all_messages_rec(VEC_AT(m->nested, JMsg, i), out);
}

static Vec file_all_messages(JFile *f)
{
	Vec out = {0};
	for (int i = 0; i < f->msgs.n; i++)
		all_messages_rec(VEC_AT(f->msgs, JMsg, i), &out);
	return out;
}

static void sealed_cache_build(void)
{
	if (sealed_cache.built)
		return;
	sealed_cache.built = true;
	for (int i = 0; i < all_files.n; i++) {
		Vec msgs = file_all_messages(VEC_AT(all_files, JFile, i));
		for (int j = 0; j < msgs.n; j++) {
			JMsg *m = VEC_AT(msgs, JMsg, j);
			if (!msg_is_sealed_oneof_type(m))
				continue;
			JOneof *o = VEC_AT(m->oneofs, JOneof, 0);
			Vec *cases = xalloc(sizeof *cases);
			for (int k = 0; k < o->fields.n; k++) {
				JField *f = VEC_AT(o->fields, JField, k);
				if (java_type_of(f->type) != JT_MESSAGE)
					java_exception("java.lang.UnsupportedOperationException",
						       xfmt("This field is not of message type. (%s)", f->full_name));
				vec_push(cases, f->msg_type);
			}
			/* containers -> cases and case -> container: toMap, the last one wins */
			m->sealed_cases = cases;
			for (int k = 0; k < cases->n; k++)
				if (cases->v[k])
					((JMsg *)cases->v[k])->sealed_container = m;
		}
	}
}

static Vec *sealed_cases_of(const JMsg *m)
{
	sealed_cache_build();
	return m->sealed_cases;
}

static JMsg *sealed_container_of(const JMsg *m)
{
	sealed_cache_build();
	return m->sealed_container;
}

/* ---- ExtendedFileDescriptor ---- */

static PbMsg *file_options_proto(const JFile *f) { return pb_msg(f->proto, FDP_OPTIONS); }

static bool file_java_conversions(JFile *f)
{
	PbMsg *o = file_opts(f);
	return (jpb_bool(o, SPO_JAVA_CONVERSIONS, false) || P.java_conversions) &&
	       !jpb_bool(o, SPO_TEST_ONLY_NO_JAVA_CONVERSIONS, false);
}

static const char *file_java_package(const JFile *f)
{
	PbMsg *o = file_options_proto(f);
	if (pb_has(o, 1))
		return jstr(o, 1);
	return f->package;
}

static bool file_scala3(JFile *f) { return jpb_bool(file_opts(f), SPO_SCALA3_SOURCES, false) || P.scala3_sources; }

static bool file_public_ctor_params(JFile *f) { return jpb_bool(file_opts(f), SPO_PUBLIC_CONSTRUCTOR_PARAMETERS, false); }

static const Compat *file_v(JFile *f) { return file_scala3(f) ? &COMPAT3 : &COMPAT2; }

static const char *file_java_package_as_symbol(const JFile *f)
{
	Vec parts = java_split(file_java_package(f), '.');
	Buf b = {0};
	for (int i = 0; i < parts.n; i++) {
		if (i)
			buf_addc(&b, '.');
		buf_adds(&b, as_symbol(parts.v[i]));
	}
	return buf_str(&b);
}

static bool msg_has_conflicting_java_class_name(const JMsg *m, const char *name)
{
	if (str_eq(m->name, name))
		return true;
	for (int i = 0; i < m->enums.n; i++)
		if (str_eq(VEC_AT(m->enums, JEnum, i)->name, name))
			return true;
	for (int i = 0; i < m->nested.n; i++)
		if (msg_has_conflicting_java_class_name(VEC_AT(m->nested, JMsg, i), name))
			return true;
	return false;
}

static bool file_has_conflicting_java_class_name(const JFile *f, const char *name)
{
	for (int i = 0; i < f->enums.n; i++)
		if (str_eq(VEC_AT(f->enums, JEnum, i)->name, name))
			return true;
	for (int i = 0; i < f->services.n; i++)
		if (str_eq(VEC_AT(f->services, JService, i)->name, name))
			return true;
	for (int i = 0; i < f->msgs.n; i++)
		if (msg_has_conflicting_java_class_name(VEC_AT(f->msgs, JMsg, i), name))
			return true;
	return false;
}

static bool eq_ignore_ascii_case(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = is_upper(*a) ? (char)(*a - 'A' + 'a') : *a, y = is_upper(*b) ? (char)(*b - 'A' + 'a') : *b;
		if (x != y)
			return false;
	}
	return !*a && !*b;
}

static bool file_has_conflicting_scala_class_name(const JFile *f, const char *s)
{
	for (int i = 0; i < f->msgs.n; i++)
		if (eq_ignore_ascii_case(VEC_AT(f->msgs, JMsg, i)->name, s))
			return true;
	for (int i = 0; i < f->enums.n; i++)
		if (eq_ignore_ascii_case(VEC_AT(f->enums, JEnum, i)->name, s))
			return true;
	for (int i = 0; i < f->services.n; i++)
		if (eq_ignore_ascii_case(VEC_AT(f->services, JService, i)->name, s))
			return true;
	return false;
}

/* baseName: fileName.split("/").last.replaceAll("[.]proto$|[.]protodevel", "") */
static const char *base_name(const char *file_name)
{
	Vec parts = java_split(file_name, '/');
	if (!parts.n)
		java_exception("java.util.NoSuchElementException", NULL);
	const char *last = parts.v[parts.n - 1];
	Buf b = {0};
	size_t n = strlen(last);
	for (size_t i = 0; i < n;) {
		if (has_prefix(last + i, ".protodevel")) {
			i += 11;
		} else if (i + 6 == n && str_eq(last + i, ".proto")) {
			i += 6;
		} else {
			buf_addc(&b, last[i]);
			i++;
		}
	}
	return buf_str(&b);
}

/* javaOuterClassName (computed once per file: it scans the file's definitions) */
static const char *file_java_outer_class_name(const JFile *f)
{
	if (f->outer_class_name)
		return f->outer_class_name;
	PbMsg *o = file_options_proto(f);
	const char *r;
	if (pb_has(o, 8)) {
		r = jstr(o, 8);
	} else {
		r = snake_case_to_camel_case(base_name(f->name), true);
		if (file_has_conflicting_java_class_name(f, r))
			r = xcat(r, "OuterClass");
	}
	((JFile *)f)->outer_class_name = r;
	return r;
}

static bool file_is_flat_package(JFile *f)
{
	bool special = str_eq(f->package, "google.protobuf") || str_eq(f->package, "scalapb");
	PbMsg *o = file_opts(f);
	if (pb_has(o, SPO_FLAT_PACKAGE))
		return jpb_bool(o, SPO_FLAT_PACKAGE, false);
	return P.flat_package && !special;
}

static Vec file_scala_package_parts(JFile *f)
{
	PbMsg *o = file_opts(f);
	Vec requested = java_split(pb_has(o, SPO_PACKAGE_NAME) ? jstr(o, SPO_PACKAGE_NAME) : file_java_package(f), '.');
	Vec parts = {0};
	for (int i = 0; i < requested.n; i++)
		if (*(const char *)requested.v[i])
			vec_push(&parts, requested.v[i]);
	if (file_is_flat_package(f))
		return parts;
	char *bn = xstrdup(base_name(f->name));
	for (char *p = bn; *p; p++)
		if (*p == '-')
			*p = '_';
	Vec more = java_split(bn, '.');
	return vec_concat(&parts, &more);
}

/* scalaPackage and scalaDirectory, computed once per file */
static ScalaName *file_scala_package(JFile *f)
{
	if (!f->scala_package) {
		Vec parts = file_scala_package_parts(f);
		f->scala_package = sn_new(parts.n == 0, parts);
	}
	return f->scala_package;
}

static const char *file_scala_directory(JFile *f)
{
	if (!f->scala_directory) {
		Vec parts = file_scala_package(f)->xs;
		f->scala_directory = str_join(&parts, "/");
	}
	return f->scala_directory;
}

/* fileDescriptorObject (computed once per file: it scans the file's definitions) */
static ScalaName *file_descriptor_object(JFile *f)
{
	if (f->descriptor_object)
		return f->descriptor_object;
	PbMsg *o = file_opts(f);
	const char *object_name;
	if (pb_has(o, SPO_OBJECT_NAME)) {
		object_name = jstr(o, SPO_OBJECT_NAME);
	} else {
		const char *s = snake_case_to_camel_case(xcat(base_name(f->name), "Proto"), true);
		if (!file_has_conflicting_java_class_name(f, s) && !file_has_conflicting_scala_class_name(f, s))
			object_name = s;
		else
			object_name = xcat(s, "Companion");
	}
	f->descriptor_object = sn_child(file_scala_package(f), object_name);
	return f->descriptor_object;
}

static const char *file_scala_file_name(JFile *f)
{
	return xcat(file_scala_directory(f), "/", sn_name(file_descriptor_object(f)), ".scala");
}

static const char *file_java_full_outer_class_name(const JFile *f)
{
	const char *pkg = file_java_package_as_symbol(f);
	if (!pkg[0])
		return file_java_outer_class_name(f);
	return xcat(pkg, ".", file_java_outer_class_name(f));
}

static const char *file_full_java_name(const JFile *f, const char *full_name)
{
	const char *base;
	if (!jpb_bool(file_options_proto(f), 10, false)) {
		base = xcat(file_java_full_outer_class_name(f), ".");
	} else {
		const char *pkg = file_java_package_as_symbol(f);
		base = pkg[0] ? xcat(pkg, ".") : "";
	}
	const char *stripped = full_name;
	if (f->package[0]) {
		size_t n = strlen(f->package);
		if (!(has_prefix(full_name, f->package) && full_name[n] == '.'))
			java_exception("java.lang.AssertionError", "assertion failed");
		stripped = full_name + n + 1;
	}
	Vec parts = java_split(stripped, '.');
	Buf b = {0};
	buf_adds(&b, base);
	for (int i = 0; i < parts.n; i++) {
		if (i)
			buf_addc(&b, '.');
		buf_adds(&b, as_symbol(parts.v[i]));
	}
	return buf_str(&b);
}

static bool file_is_proto3(const JFile *f) { return f->syntax == SYN_PROTO3; }

/* findLocationByPath: the first location with exactly this path. Upstream scans the locations on
 * each lookup; the locations sorted once per file by (path, position) and a binary search give the
 * same result without the time quadratic in the input. */
typedef struct {
	PbMsg *loc;
	int pos, n;
} JLoc;

typedef struct {
	JLoc *v;
	int n;
} JLocIndex;

static int jloc_cmp(const void *pa, const void *pb)
{
	const JLoc *a = pa, *b = pb;
	if (a->n != b->n)
		return a->n < b->n ? -1 : 1;
	for (int j = 0; j < a->n; j++) {
		int64_t x = pb_int_at(a->loc, LOC_PATH, j), y = pb_int_at(b->loc, LOC_PATH, j);
		if (x != y)
			return x < y ? -1 : 1;
	}
	return a->pos < b->pos ? -1 : a->pos > b->pos;
}

/* <0, 0, >0 as the path of e orders before, equal to or after path[0..n). */
static int jloc_cmp_path(const JLoc *e, const int *path, int n)
{
	if (e->n != n)
		return e->n < n ? -1 : 1;
	for (int j = 0; j < n; j++) {
		int64_t x = pb_int_at(e->loc, LOC_PATH, j), y = path[j];
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

COLD static JLocIndex *file_loc_index(JFile *f)
{
	if (f->loc_index)
		return f->loc_index;
	JLocIndex *ix = xalloc(sizeof *ix);
	PbMsg *sci = pb_msg(f->proto, FDP_SOURCE_CODE_INFO);
	ix->n = pb_count(sci, SCI_LOCATION);
	ix->v = xalloc(sizeof(JLoc) * (size_t)(ix->n ? ix->n : 1));
	for (int i = 0; i < ix->n; i++) {
		PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		ix->v[i] = (JLoc){loc, i, pb_count(loc, LOC_PATH)};
	}
	if (ix->n > 1)
		qsort(ix->v, (size_t)ix->n, sizeof(JLoc), jloc_cmp);
	f->loc_index = ix;
	return ix;
}

static PbMsg *file_find_location(const JFile *f, const int *path, int n)
{
	JLocIndex *ix = file_loc_index((JFile *)f);
	int lo = 0, hi = ix->n; /* first entry not before path */
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (jloc_cmp_path(&ix->v[mid], path, n) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < ix->n && jloc_cmp_path(&ix->v[lo], path, n) == 0 ? ix->v[lo].loc : NULL;
}

/* comment: (leading + trailing comments of the location), escaped; NULL when empty. */
static const char *comment_at(const JFile *f, const int *path, int n)
{
	PbMsg *loc = file_find_location(f, path, n);
	if (!loc)
		return NULL;
	Buf b = {0};
	buf_adds(&b, jstr(loc, LOC_LEADING));
	buf_adds(&b, jstr(loc, LOC_TRAILING));
	if (!b.n)
		return NULL;
	return escape_comment(buf_view(&b));
}

static bool file_use_primitive_wrappers(JFile *f) { return !jpb_bool(file_opts(f), SPO_NO_PRIMITIVE_WRAPPERS, false); }

static bool file_retain_source_code_info(JFile *f)
{
	PbMsg *o = file_opts(f);
	if (pb_has(o, SPO_RETAIN_SOURCE_CODE_INFO))
		return jpb_bool(o, SPO_RETAIN_SOURCE_CODE_INFO, false);
	return P.retain_source_code_info;
}

static bool file_no_default_values_in_constructor(JFile *f)
{
	return jpb_bool(file_opts(f), SPO_NO_DEFAULT_VALUES_IN_CONSTRUCTOR, false);
}

static bool file_disable_output(JFile *f)
{
	PbMsg *o = file_opts(f);
	return pb_int(o, SPO_SCOPE, 0) == 1 && !f->enums.n && !f->msgs.n && !f->services.n && !f->exts.n &&
	       !pb_count(o, SPO_PREAMBLE);
}

/* ---- ExtendedMessageDescriptor ---- */

/* fields: getFields without groups */
static Vec msg_fields(const JMsg *m)
{
	Vec v = {0};
	for (int i = 0; i < m->fields.n; i++) {
		JField *f = VEC_AT(m->fields, JField, i);
		if (f->type != PT_GROUP)
			vec_push(&v, f);
	}
	return v;
}

static ScalaName *msg_scala_type(JMsg *m);
static const char *field_scala_name(JField *f);

static Vec msg_field_scala_names(JMsg *m)
{
	if (!m->field_names) {
		Vec fields = msg_fields(m);
		m->field_names = xalloc(sizeof(Vec));
		for (int i = 0; i < fields.n; i++)
			vec_push(m->field_names, (void *)field_scala_name(fields.v[i]));
	}
	return *m->field_names;
}

static ScalaName *msg_scala_type(JMsg *m)
{
	if (m->scala_type)
		return m->scala_type;
	const char *name = m->name;
	if (str_eq(name, "Option"))
		name = "OptionProto";
	else if (msg_is_sealed_oneof_type(m))
		name = xcat(name, "Message");
	ScalaName *parent = m->parent ? msg_scala_type(m->parent) : file_scala_package(m->file);
	m->scala_type = sn_child(parent, name);
	return m->scala_type;
}

static Map *names_set(const Vec *names)
{
	Map *set = xalloc(sizeof *set);
	for (int i = 0; i < names->n; i++)
		map_put(set, names->v[i], set);
	return set;
}

static const char *msg_full_name_with_maybe_root_ctx(ScalaName *n, JMsg *context)
{
	if (!context->field_name_set) {
		Vec names = msg_field_scala_names(context);
		context->field_name_set = names_set(&names);
	}
	return sn_full_name_with_maybe_root_ctx(n, context->field_name_set);
}

static const char *msg_java_type_name(JMsg *m) { return file_full_java_name(m->file, m->full_name); }

/* ---- auxiliary options ---- */

/* The aux options (field num of the file's ScalaPbOptions) whose target matches name
 * (Helper.targetMatches), in order. An index by target per file keeps this linear. */
typedef struct {
	Map by_target; /* target -> Vec* of indexes + 1 */
	Vec star;      /* indexes + 1 of target "*" */
} AuxIndex;

static Vec aux_matches(JFile *f, uint32_t num, const char *name)
{
	int kind = num == SPO_AUX_MESSAGE_OPTIONS ? 0 : num == SPO_AUX_FIELD_OPTIONS ? 1 : num == SPO_AUX_ENUM_OPTIONS ? 2 : 3;
	PbMsg *fo = file_opts(f);
	AuxIndex *x = f->aux_index[kind];
	if (!x) {
		x = f->aux_index[kind] = xalloc(sizeof *x);
		for (int i = 0; i < pb_count(fo, num); i++) {
			const char *target = jstr(pb_msg_at(fo, num, i), AUX_TARGET);
			Vec *v = str_eq(target, "*") ? &x->star : map_get(&x->by_target, target);
			if (!v) {
				v = xalloc(sizeof *v);
				map_put(&x->by_target, target, v);
			}
			vec_push(v, (void *)(intptr_t)(i + 1));
		}
	}
	Vec out = {0}, *named = map_get(&x->by_target, name);
	int i = 0, j = 0, nn = named ? named->n : 0;
	while (i < x->star.n || j < nn) {
		if (j == nn || (i < x->star.n && (intptr_t)x->star.v[i] < (intptr_t)named->v[j]))
			vec_push(&out, pb_msg_at(fo, num, (int)(intptr_t)x->star.v[i++] - 1));
		else
			vec_push(&out, pb_msg_at(fo, num, (int)(intptr_t)named->v[j++] - 1));
	}
	return out;
}

/* Merges the options of the matching aux options into local: foldLeft(local)((local, aux) =>
 * newBuilder(aux.getOptions).mergeFrom(local).build), which is the message parsed from the
 * encodings of the last match, ..., the first match, local. */
static PbMsg *aux_merged_options(JFile *f, uint32_t num, const char *name, const PbMsgDef *def, PbMsg *local)
{
	Vec aux = aux_matches(f, num, name);
	if (!aux.n)
		return local;
	Buf tmp = {0};
	for (int i = aux.n - 1; i >= 0; i--) {
		PbMsg *o = pb_msg(aux.v[i], AUX_OPTIONS);
		if (o)
			jpb_encode(o, &tmp);
	}
	if (local)
		jpb_encode(local, &tmp);
	PbMsg *m = pb_new(def);
	pb_decode(m, (const uint8_t *)tmp.p, tmp.n);
	return m;
}

static PbMsg *message_options(JMsg *m)
{
	if (m->msg_opts)
		return m->msg_opts;
	PbMsg *local = local_opts(pb_msg(m->proto, DP_OPTIONS), &sp_MessageOptions);
	local = aux_merged_options(m->file, SPO_AUX_MESSAGE_OPTIONS, m->full_name, &sp_MessageOptions, local);
	m->msg_opts = local;
	return local;
}

static bool msg_no_box(JMsg *m) { return jpb_bool(message_options(m), SMO_NO_BOX, false); }

static bool msg_no_default_value_in_constructor(JMsg *m)
{
	PbMsg *o = message_options(m);
	if (pb_has(o, SMO_NO_DEFAULT_VALUES_IN_CONSTRUCTOR))
		return jpb_bool(o, SMO_NO_DEFAULT_VALUES_IN_CONSTRUCTOR, false);
	return file_no_default_values_in_constructor(m->file);
}

#define DEPRECATED_ANNOTATION "@scala.deprecated(message=\"Marked as deprecated in proto file\", \"\")"

static Vec msg_deprecated_annotation(const JMsg *m)
{
	Vec v = {0};
	if (jpb_bool(pb_msg(m->proto, DP_OPTIONS), 3, false))
		vec_push(&v, DEPRECATED_ANNOTATION);
	return v;
}

static Vec msg_annotation_list(JMsg *m)
{
	Vec d = msg_deprecated_annotation(m), a = pb_strs(message_options(m), SMO_ANNOTATIONS);
	return vec_concat(&d, &a);
}

static Vec msg_companion_annotation_list(JMsg *m)
{
	Vec d = msg_deprecated_annotation(m), a = pb_strs(message_options(m), SMO_COMPANION_ANNOTATIONS);
	return vec_concat(&d, &a);
}

static Vec msg_extends_option(JMsg *m)
{
	Vec all = pb_strs(message_options(m), SMO_EXTENDS), v = {0};
	for (int i = 0; i < all.n; i++)
		if (!is_value_class_name(all.v[i]))
			vec_push(&v, all.v[i]);
	return v;
}

static bool msg_is_value_class(JMsg *m)
{
	PbMsg *o = message_options(m);
	for (int i = 0; i < pb_count(o, SMO_EXTENDS); i++)
		if (is_value_class_name(jstr_at(o, SMO_EXTENDS, i)))
			return true;
	return false;
}

static bool msg_is_universal_trait(JMsg *m)
{
	PbMsg *o = message_options(m);
	for (int i = 0; i < pb_count(o, SMO_SEALED_ONEOF_EXTENDS); i++)
		if (is_universal_trait_name(jstr_at(o, SMO_SEALED_ONEOF_EXTENDS, i)))
			return true;
	return false;
}

static bool msg_preserves_unknown_fields(JMsg *m)
{
	return (jmsg_is_extendable(m) || jpb_bool(file_opts(m->file), SPO_PRESERVE_UNKNOWN_FIELDS, true)) &&
	       !msg_is_value_class(m) && !msg_is_universal_trait(m);
}

enum { SEALED_DEFAULT, SEALED_OPTIONAL };

static int msg_sealed_oneof_style(JMsg *m)
{
	for (int i = 0; i < m->real_oneofs; i++)
		if (str_eq(VEC_AT(m->oneofs, JOneof, i)->name, "sealed_value"))
			return SEALED_DEFAULT;
	for (int i = 0; i < m->real_oneofs; i++)
		if (str_eq(VEC_AT(m->oneofs, JOneof, i)->name, "sealed_value_optional"))
			return SEALED_OPTIONAL;
	java_exception("java.lang.RuntimeException", "Unexpected oneof style");
}

static bool msg_is_sealed_oneof_case(const JMsg *m) { return sealed_container_of(m) != NULL; }

static ScalaName *msg_sealed_oneof_trait_scala_type(JMsg *m)
{
	const char *name = sn_name(msg_scala_type(m));
	char *stripped = has_suffix(name, "Message") ? xstrndup(name, strlen(name) - 7) : xstrdup(name);
	ScalaName *parent = m->parent ? msg_scala_type(m->parent) : file_scala_package(m->file);
	return sn_child(parent, stripped);
}

static const char *msg_sealed_oneof_scala_type(JMsg *m)
{
	if (msg_sealed_oneof_style(m) == SEALED_OPTIONAL)
		return xcat("_root_.scala.Option[", sn_full_name(msg_sealed_oneof_trait_scala_type(m)), "]");
	return sn_full_name(msg_sealed_oneof_trait_scala_type(m));
}

static ScalaName *msg_sealed_oneof_non_empty_scala_type(JMsg *m)
{
	if (msg_sealed_oneof_style(m) != SEALED_DEFAULT)
		java_exception("scala.NotImplementedError", "an implementation is missing");
	return sn_child(msg_sealed_oneof_trait_scala_type(m), "NonEmpty");
}

static Vec msg_sealed_oneof_case_bases(JMsg *m)
{
	if (msg_sealed_oneof_style(m) == SEALED_OPTIONAL)
		return vec_of(sn_full_name(msg_sealed_oneof_trait_scala_type(m)));
	return vec_of(sn_full_name(msg_sealed_oneof_non_empty_scala_type(m)));
}

static ScalaName *msg_sealed_oneof_type_mapper(JMsg *m)
{
	ScalaName *t = msg_sealed_oneof_trait_scala_type(m);
	return sn_child(t, xcat(sn_name(t), "TypeMapper"));
}

static bool msg_generate_lenses(JMsg *m)
{
	PbMsg *o = file_opts(m->file);
	if (pb_has(o, SPO_LENSES))
		return jpb_bool(o, SPO_LENSES, true);
	return P.lenses;
}

static bool msg_generate_getters(JMsg *m) { return jpb_bool(file_opts(m->file), SPO_GETTERS, true); }

static bool msg_java_conversions(JMsg *m) { return file_java_conversions(m->file) && !jmsg_is_map_entry(m); }

static Vec msg_base_classes(JMsg *m)
{
	Vec v = {0};
	if (msg_is_value_class(m))
		vec_push(&v, "AnyVal");
	vec_push(&v, "scalapb.GeneratedMessage");
	JMsg *container = sealed_container_of(m);
	if (container) {
		Vec b = msg_sealed_oneof_case_bases(container);
		v = vec_concat(&v, &b);
	}
	if (msg_generate_lenses(m))
		vec_push(&v, xcat("scalapb.lenses.Updatable[", sn_name_symbol(msg_scala_type(m)), "]"));
	if (jmsg_is_extendable(m))
		vec_push(&v, xcat("_root_.scalapb.ExtendableMessage[", sn_name_symbol(msg_scala_type(m)), "]"));
	Vec ext = msg_extends_option(m);
	v = vec_concat(&v, &ext);
	if (str_eq(m->full_name, "google.protobuf.Any"))
		vec_push(&v, "_root_.scalapb.AnyMethods");
	else if (str_eq(m->full_name, "google.protobuf.Timestamp"))
		vec_push(&v, "_root_.scalapb.TimestampMethods");
	else if (str_eq(m->full_name, "google.protobuf.Duration"))
		vec_push(&v, "_root_.scalapb.DurationMethods");
	return v;
}

static Vec msg_companion_base_classes(JMsg *m)
{
	Vec v = {0};
	vec_push(&v, xcat("scalapb.GeneratedMessageCompanion[", sn_full_name(msg_scala_type(m)), "]"));
	if (msg_java_conversions(m))
		vec_push(&v, xcat("scalapb.JavaProtoSupport[", sn_full_name(msg_scala_type(m)), ", ", msg_java_type_name(m), "]"));
	Vec ce = pb_strs(message_options(m), SMO_COMPANION_EXTENDS);
	v = vec_concat(&v, &ce);
	if (str_eq(m->full_name, "google.protobuf.Any"))
		vec_push(&v, "scalapb.AnyCompanionMethods");
	else if (str_eq(m->full_name, "google.protobuf.Timestamp"))
		vec_push(&v, "scalapb.TimestampCompanionMethods");
	else if (str_eq(m->full_name, "google.protobuf.Duration"))
		vec_push(&v, "scalapb.DurationCompanionMethods");
	return v;
}

static Vec msg_sealed_oneof_base_classes(JMsg *m)
{
	PbMsg *o = message_options(m);
	if (msg_sealed_oneof_style(m) == SEALED_DEFAULT) {
		Vec v = {0};
		if (msg_is_universal_trait(m))
			vec_push(&v, "Any");
		for (int i = 0; i < pb_count(o, SMO_SEALED_ONEOF_EXTENDS); i++)
			if (!is_universal_trait_name(jstr_at(o, SMO_SEALED_ONEOF_EXTENDS, i)))
				vec_push(&v, (void *)jstr_at(o, SMO_SEALED_ONEOF_EXTENDS, i));
		vec_push(&v, "scalapb.GeneratedSealedOneof");
		return v;
	}
	return pb_strs(o, SMO_SEALED_ONEOF_EXTENDS);
}

static const char *msg_scala_file_name(JMsg *m)
{
	if (jpb_bool(file_opts(m->file), SPO_SINGLE_FILE, false))
		return file_scala_file_name(m->file);
	if (msg_is_sealed_oneof_type(m))
		return xcat(file_scala_directory(m->file), "/", sn_name(msg_sealed_oneof_trait_scala_type(m)), ".scala");
	JMsg *container = sealed_container_of(m);
	if (container)
		return msg_scala_file_name(container);
	return xcat(file_scala_directory(m->file), "/", sn_name(msg_scala_type(m)), ".scala");
}

/* MapType.keyField / valueField: findFieldByName, used without a null check */
static JField *map_key_field(JMsg *m)
{
	JField *f = jmsg_find_field_by_name(m, "key");
	if (!f)
		java_exception("java.lang.NullPointerException", NULL);
	return f;
}

static JField *map_value_field(JMsg *m)
{
	JField *f = jmsg_find_field_by_name(m, "value");
	if (!f)
		java_exception("java.lang.NullPointerException", NULL);
	return f;
}

static const char *msg_java_descriptor_source(JMsg *m)
{
	if (!m->parent)
		return xfmt("%s.javaDescriptor.getMessageTypes().get(%d)", sn_full_name(file_descriptor_object(m->file)), m->index);
	return xfmt("%s.javaDescriptor.getNestedTypes().get(%d)", sn_full_name(msg_scala_type(m->parent)), m->index);
}

static const char *msg_scala_descriptor_source(JMsg *m)
{
	if (!m->parent)
		return xfmt("%s.scalaDescriptor.messages(%d)", sn_full_name(file_descriptor_object(m->file)), m->index);
	return xfmt("%s.scalaDescriptor.nestedMessages(%d)", sn_full_name(msg_scala_type(m->parent)), m->index);
}

/* Source paths are built in a fixed-size buffer: nesting is limited by the parser (depth 100). */
#define MAX_PATH 512

static int msg_source_path(const JMsg *m, int *path)
{
	if (!m->parent) {
		path[0] = FDP_MESSAGE_TYPE;
		path[1] = m->index;
		return 2;
	}
	int n = msg_source_path(m->parent, path);
	path[n] = DP_NESTED_TYPE;
	path[n + 1] = m->index;
	return n + 2;
}

static const char *msg_comment(JMsg *m)
{
	int path[MAX_PATH];
	int n = msg_source_path(m, path);
	return comment_at(m->file, path, n);
}

/* ---- ExtendedEnumDescriptor ---- */

static PbMsg *enum_options(JEnum *e)
{
	if (e->enum_opts)
		return e->enum_opts;
	PbMsg *local = local_opts(pb_msg(e->proto, EDP_OPTIONS), &sp_EnumOptions);
	local = aux_merged_options(e->file, SPO_AUX_ENUM_OPTIONS, e->full_name, &sp_EnumOptions, local);
	e->enum_opts = local;
	return local;
}

static ScalaName *enum_scala_type(JEnum *e)
{
	if (e->scala_type)
		return e->scala_type;
	const char *name = e->name;
	if (str_eq(name, "Option"))
		name = "OptionEnum";
	else if (str_eq(name, "ValueType"))
		name = "ValueTypeEnum";
	ScalaName *parent = e->parent ? msg_scala_type(e->parent) : file_scala_package(e->file);
	e->scala_type = sn_child(parent, name);
	return e->scala_type;
}

static ScalaName *enum_recognized(JEnum *e) { return sn_child(enum_scala_type(e), "Recognized"); }

static const char *enum_java_type_name(JEnum *e) { return file_full_java_name(e->file, e->full_name); }

static bool enum_java_conversions(JEnum *e) { return file_java_conversions(e->file); }

/* valuesWithNoDuplicates: the first value of each number, sorted by number */
static int enum_value_cmp_number(const void *a, const void *b)
{
	int x = ((const JEnumValue *)a)->number, y = ((const JEnumValue *)b)->number;
	return x < y ? -1 : x > y;
}

/* The values sorted by number, stable: the values with one number are together, in order (the
 * groups of values.filter(_.number == v.number) and groupBy(_.number) upstream). */
static JEnumValue **enum_by_number(JEnum *e)
{
	if (e->by_number)
		return e->by_number;
	int n = e->values.n;
	e->by_number = xalloc(sizeof(JEnumValue *) * (size_t)(n ? n : 1));
	e->by_number_pos = xalloc(sizeof(int) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		e->by_number[i] = VEC_AT(e->values, JEnumValue, i);
	java_sort((void **)e->by_number, n, enum_value_cmp_number);
	for (int i = 0; i < n; i++)
		e->by_number_pos[e->by_number[i]->index] = i;
	return e->by_number;
}

/* The first value (in declaration order) with the number of v. */
static JEnumValue *enum_first_with_number(JEnumValue *v)
{
	JEnumValue **s = enum_by_number(v->type);
	int i = v->type->by_number_pos[v->index];
	while (i > 0 && s[i - 1]->number == v->number)
		i--;
	return s[i];
}

/* valuesWithNoDuplicates: groupBy(_.number).mapValues(_.head).values.toSeq.sortBy(_.number) */
static Vec enum_values_with_no_duplicates(JEnum *e)
{
	JEnumValue **s = enum_by_number(e);
	Vec v = {0};
	for (int i = 0; i < e->values.n; i++)
		if (!i || s[i - 1]->number != s[i]->number)
			vec_push(&v, s[i]);
	return v;
}

static const char *enum_java_descriptor_source(JEnum *e)
{
	if (!e->parent)
		return xfmt("%s.javaDescriptor.getEnumTypes().get(%d)", sn_full_name(file_descriptor_object(e->file)), e->index);
	return xfmt("%s.javaDescriptor.getEnumTypes().get(%d)", sn_full_name(msg_scala_type(e->parent)), e->index);
}

static const char *enum_scala_descriptor_source(JEnum *e)
{
	if (!e->parent)
		return xfmt("%s.scalaDescriptor.enums(%d)", sn_full_name(file_descriptor_object(e->file)), e->index);
	return xfmt("%s.scalaDescriptor.enums(%d)", sn_full_name(msg_scala_type(e->parent)), e->index);
}

static Vec enum_base_trait_extends(JEnum *e)
{
	Vec v = vec_of("_root_.scalapb.GeneratedEnum"), x = pb_strs(enum_options(e), SEO_EXTENDS);
	return vec_concat(&v, &x);
}

static Vec enum_companion_extends(JEnum *e)
{
	Vec v = vec_of(xcat("_root_.scalapb.GeneratedEnumCompanion[", sn_name_symbol(enum_scala_type(e)), "]"));
	Vec x = pb_strs(enum_options(e), SEO_COMPANION_EXTENDS);
	return vec_concat(&v, &x);
}

static int enum_source_path(const JEnum *e, int *path)
{
	if (!e->parent) {
		path[0] = FDP_ENUM_TYPE;
		path[1] = e->index;
		return 2;
	}
	int n = msg_source_path(e->parent, path);
	path[n] = DP_ENUM_TYPE;
	path[n + 1] = e->index;
	return n + 2;
}

static const char *enum_comment(JEnum *e)
{
	int path[MAX_PATH];
	int n = enum_source_path(e, path);
	return comment_at(e->file, path, n);
}

static Vec enum_deprecated_annotation(const JEnum *e)
{
	Vec v = {0};
	if (jpb_bool(pb_msg(e->proto, EDP_OPTIONS), 3, false))
		vec_push(&v, DEPRECATED_ANNOTATION);
	return v;
}

static Vec enum_annotations(JEnum *e, uint32_t num)
{
	Vec d = enum_deprecated_annotation(e), a = pb_strs(enum_options(e), num);
	return vec_concat(&d, &a);
}

/* ---- ExtendedEnumValueDescriptor ---- */

static PbMsg *enum_value_options(JEnumValue *v)
{
	if (v->value_opts)
		return v->value_opts;
	PbMsg *local = local_opts(pb_msg(v->proto, EVDP_OPTIONS), &sp_EnumValueOptions);
	local = aux_merged_options(v->type->file, SPO_AUX_ENUM_VALUE_OPTIONS, v->full_name, &sp_EnumValueOptions, local);
	v->value_opts = local;
	return local;
}

static Vec enum_value_extends(JEnumValue *v)
{
	Vec r = {0};
	vec_push(&r, xfmt("%s(%d)", sn_name_symbol(enum_scala_type(v->type)), v->number));
	vec_push(&r, (void *)sn_name_relative(enum_recognized(v->type), 1));
	Vec x = pb_strs(enum_value_options(v), SEVO_EXTENDS);
	return vec_concat(&r, &x);
}

static const char *enum_value_scala_name(JEnumValue *v)
{
	if (v->scala_name)
		return v->scala_name;
	PbMsg *o = enum_value_options(v);
	if (pb_has(o, SEVO_SCALA_NAME)) {
		v->scala_name = jstr(o, SEVO_SCALA_NAME);
		return v->scala_name;
	}
	PbMsg *fo = file_opts(v->type->file);
	const char *name = v->name;
	if (jpb_bool(fo, SPO_ENUM_STRIP_PREFIX, false)) {
		const char *prefix = xcat(to_all_caps(v->type->name), "_");
		if (has_prefix(name, prefix))
			name += strlen(prefix);
	}
	if (pb_int(fo, SPO_ENUM_VALUE_NAMING, 0) == 1)
		name = all_caps_to_camel_case(name, true);
	v->scala_name = name;
	return name;
}

/* sortBy((number, scalaName)) */
static int enum_value_cmp_number_name(const void *a, const void *b)
{
	int c = enum_value_cmp_number(a, b);
	return c ? c : java_strcmp(enum_value_scala_name((JEnumValue *)a), enum_value_scala_name((JEnumValue *)b));
}

/* isName: Helper.makeUniqueNames over the values sorted by (number, scalaName), computed once
 * per enum. */
static const char *enum_value_is_name(JEnumValue *v)
{
	JEnum *e = v->type;
	if (e->is_names)
		return e->is_names[v->index];
	int n = e->values.n;
	JEnumValue **sorted = xalloc(sizeof *sorted * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		sorted[i] = VEC_AT(e->values, JEnumValue, i);
	java_sort((void **)sorted, n, enum_value_cmp_number_name);
	/* attempts: the last attempt per name. allCapsToCamelCase drops '_', so a name never equals
	 * another name's name_<attempt>, and resuming from the last attempt finds the same name as
	 * upstream's loop from 1 (which is quadratic in the number of equal names). */
	Map taken = {0}, attempts = {0};
	e->is_names = xalloc(sizeof(char *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		const char *name = xcat("is", all_caps_to_camel_case(enum_value_scala_name(sorted[i]), true));
		const char *new_name = name;
		intptr_t attempt = (intptr_t)map_get(&attempts, name);
		if (attempt)
			new_name = xfmt("%s_%d", name, (int)attempt);
		while (map_has(&taken, new_name)) {
			attempt++;
			new_name = xfmt("%s_%d", name, (int)attempt);
		}
		if (attempt)
			map_put(&attempts, name, (void *)attempt);
		map_put(&taken, new_name, sorted[i]);
		e->is_names[sorted[i]->index] = new_name;
	}
	return e->is_names[v->index];
}

static const char *enum_value_comment(JEnumValue *v)
{
	int path[MAX_PATH];
	int n = enum_source_path(v->type, path);
	path[n] = EDP_VALUE;
	path[n + 1] = v->index;
	return comment_at(v->type->file, path, n + 2);
}

static Vec enum_value_annotation_list(JEnumValue *v)
{
	Vec d = {0};
	if (jpb_bool(pb_msg(v->proto, EVDP_OPTIONS), 1, false))
		vec_push(&d, DEPRECATED_ANNOTATION);
	Vec a = pb_strs(enum_value_options(v), SEVO_ANNOTATIONS);
	return vec_concat(&d, &a);
}

/* ---- ExtendedOneofDescriptor ---- */

static PbMsg *oneof_options(JOneof *o) { return local_opts(pb_msg(o->proto, 2), &sp_OneofOptions); }

static const char *oneof_java_enum_name(JOneof *o) { return xcat("get", snake_case_to_camel_case(o->name, true), "Case"); }

static ScalaName *oneof_scala_name(JOneof *o)
{
	PbMsg *opts = oneof_options(o);
	const char *field_name = jstr(opts, SOO_SCALA_NAME)[0] ? jstr(opts, SOO_SCALA_NAME) : snake_case_to_camel_case(o->name, false);
	return sn_child(msg_scala_type(o->containing), field_name);
}

static ScalaName *oneof_scala_type(JOneof *o)
{
	const char *name = str_eq(o->name, "ValueType") || str_eq(o->name, "value_type") ? "ValueTypeOneof"
										      : snake_case_to_camel_case(o->name, true);
	JMsg *m = o->containing;
	bool conflicts = false;
	for (int i = 0; i < m->enums.n && !conflicts; i++)
		conflicts = str_eq(sn_name(enum_scala_type(VEC_AT(m->enums, JEnum, i))), name);
	for (int i = 0; i < m->nested.n && !conflicts; i++)
		conflicts = str_eq(sn_name(msg_scala_type(VEC_AT(m->nested, JMsg, i))), name);
	return sn_child(msg_scala_type(m), conflicts ? xcat(name, "Oneof") : name);
}

static Vec oneof_fields(JOneof *o)
{
	Vec v = {0};
	for (int i = 0; i < o->fields.n; i++)
		if (VEC_AT(o->fields, JField, i)->type != PT_GROUP)
			vec_push(&v, o->fields.v[i]);
	return v;
}

static ScalaName *oneof_empty(JOneof *o) { return sn_child(oneof_scala_type(o), "Empty"); }

static Vec oneof_base_classes(JOneof *o)
{
	Vec v = vec_of("_root_.scalapb.GeneratedOneof"), x = pb_strs(oneof_options(o), SOO_EXTENDS);
	return vec_concat(&v, &x);
}

/* contextNames of baseSingleScalaTypeName and friends: the scala names of the fields and of the
 * real oneofs of the containing message. */
static const Map *msg_context_names(JMsg *m)
{
	if (!m->ctx_names) {
		Vec fields = msg_field_scala_names(m);
		Vec names = vec_concat(&fields, NULL);
		for (int i = 0; i < m->real_oneofs; i++)
			vec_push(&names, (void *)sn_name_symbol(oneof_scala_name(VEC_AT(m->oneofs, JOneof, i))));
		m->ctx_names = names_set(&names);
	}
	return m->ctx_names;
}

/* ---- ExtendedFieldDescriptor ---- */

static bool field_is_in_oneof(const JField *f) { return jfield_real_oneof(f) != NULL; }
static bool field_is_message(const JField *f) { return f->type == PT_MESSAGE; }
static bool field_is_enum(const JField *f) { return f->type == PT_ENUM; }

static bool field_is_sealed_oneof_type(const JField *f)
{
	return field_is_message(f) && f->msg_type && msg_is_sealed_oneof_type(f->msg_type);
}

static PbMsg *field_options(JField *f)
{
	if (f->field_opts)
		return f->field_opts;
	PbMsg *local = local_opts(jfield_options(f), &sp_FieldOptions);
	/* (matching aux options :+ local).reduce((left, right) => left.toBuilder.mergeFrom(right).build):
	 * the message parsed from the encodings of the matches in order, then local */
	Vec aux = aux_matches(f->file, SPO_AUX_FIELD_OPTIONS, f->full_name);
	if (aux.n) {
		Buf tmp = {0};
		for (int i = 0; i < aux.n; i++) {
			PbMsg *o = pb_msg(aux.v[i], AUX_OPTIONS);
			if (o)
				jpb_encode(o, &tmp);
		}
		jpb_encode(local, &tmp);
		local = pb_new(&sp_FieldOptions);
		pb_decode(local, (const uint8_t *)tmp.p, tmp.n);
	}
	f->field_opts = local;
	return f->field_opts;
}

static const char *name_with_fallback(const char *x, bool pascal, bool prefix)
{
	const char *candidate = snake_case_to_camel_case(x, pascal);
	if (is_forbidden_field_name(candidate))
		return prefix ? xcat("_", candidate) : xcat(candidate, "_");
	return candidate;
}

static const char *field_scala_name(JField *f)
{
	if (f->scala_name)
		return f->scala_name;
	PbMsg *o = field_options(f);
	const char *r;
	if (jstr(o, SFO_SCALA_NAME)[0])
		r = jstr(o, SFO_SCALA_NAME);
	else if ((str_eq(f->name, "number") || str_eq(f->name, "value")) && field_is_in_oneof(f))
		r = xcat("_", f->name);
	else if (str_eq(f->name, "serialized_size"))
		r = "_serializedSize";
	else
		r = name_with_fallback(f->name, false, true);
	f->scala_name = r;
	return r;
}

static const char *field_upper_scala_name(JField *f)
{
	PbMsg *o = field_options(f);
	if (jstr(o, SFO_SCALA_NAME)[0])
		return snake_case_to_camel_case(jstr(o, SFO_SCALA_NAME), true);
	if (str_eq(f->name, "serialized_size"))
		return "_SerializedSize";
	if (str_eq(f->name, "class"))
		return "_Class";
	if (str_eq(f->name, "instance_of"))
		return "_InstanceOf";
	if (str_eq(f->name, "empty") && field_is_in_oneof(f))
		return "_Empty";
	return name_with_fallback(f->name, true, true);
}

static const char *field_upper_java_name(JField *f)
{
	if (str_eq(f->name, "serialized_size"))
		return "SerializedSize_";
	if (str_eq(f->name, "class"))
		return "Class_";
	return name_with_fallback(f->name, true, false);
}

static const char *field_number_constant_name(JField *f)
{
	char *s = xcat(f->name, "_FIELD_NUMBER");
	for (char *p = s; *p; p++)
		if (is_lower(*p))
			*p = (char)(*p - 'a' + 'A');
	return s;
}

static ScalaName *field_oneof_type_name(JField *f)
{
	if (!field_is_in_oneof(f))
		java_exception("java.lang.AssertionError", "assertion failed");
	return sn_child(oneof_scala_type(jfield_real_oneof(f)), field_upper_scala_name(f));
}

static bool field_no_box(JField *f)
{
	PbMsg *o = field_options(f);
	if (pb_has(o, SFO_NO_BOX) || pb_has(o, SFO_REQUIRED))
		return jpb_bool(o, SFO_NO_BOX, false) || jpb_bool(o, SFO_REQUIRED, false);
	if (field_is_message(f) && f->msg_type)
		return msg_no_box(f->msg_type);
	return false;
}

static bool field_no_default_value_in_constructor(JField *f)
{
	PbMsg *o = field_options(f);
	if (pb_has(o, SFO_NO_DEFAULT_VALUE_IN_CONSTRUCTOR))
		return jpb_bool(o, SFO_NO_DEFAULT_VALUE_IN_CONSTRUCTOR, false);
	return msg_no_default_value_in_constructor(f->containing_type);
}

static bool field_no_box_required(JField *f) { return jpb_bool(field_options(f), SFO_REQUIRED, false); }

static bool field_supports_presence(JField *f)
{
	return jfield_is_optional(f) && !field_is_in_oneof(f) &&
	       (!file_is_proto3(f->file) || field_is_message(f) || f->proto3_optional) && !field_no_box(f) &&
	       !field_is_sealed_oneof_type(f);
}

static bool field_is_singular(JField *f)
{
	return jfield_is_required(f) ||
	       (file_is_proto3(f->file) && !field_is_in_oneof(f) && jfield_is_optional(f) && !field_is_message(f) &&
		!f->proto3_optional) ||
	       (jfield_is_optional(f) && (field_no_box(f) || (field_is_sealed_oneof_type(f) && !field_is_in_oneof(f))));
}

/* EnclosingType */
enum { ENC_NONE, ENC_OPTION, ENC_COLLECTION };
typedef struct {
	int kind;
	const char *cc, *tc; /* Collection(cc, typeClass) */
} Enclosing;

static const Enclosing ENCLOSING_NONE = {ENC_NONE, NULL, NULL};
static const Enclosing ENCLOSING_OPTION = {ENC_OPTION, NULL, NULL};

static Enclosing enc_collection(const char *cc, const char *tc) { return (Enclosing){ENC_COLLECTION, cc, tc}; }

static bool enc_eq(Enclosing a, Enclosing b)
{
	if (a.kind != b.kind)
		return false;
	if (a.kind != ENC_COLLECTION)
		return true;
	return str_eq(a.cc, b.cc) && ((!a.tc && !b.tc) || (a.tc && b.tc && str_eq(a.tc, b.tc)));
}

static const char *enc_as_type(Enclosing e, const char *enclosed)
{
	switch (e.kind) {
	case ENC_OPTION: return xcat(ScalaOption, "[", enclosed, "]");
	case ENC_COLLECTION: return xcat(e.cc, "[", enclosed, "]");
	default: return enclosed;
	}
}

static const char *field_collection_type(JField *f);
static ScalaName *field_collection_adapter(JField *f);

static Enclosing field_enclosing_type(JField *f)
{
	if (field_is_singular(f))
		return ENCLOSING_NONE;
	if (field_supports_presence(f) || field_is_in_oneof(f))
		return ENCLOSING_OPTION;
	ScalaName *a = field_collection_adapter(f);
	return enc_collection(field_collection_type(f), a ? sn_full_name(a) : NULL);
}

static Enclosing field_map_enclosing_type(JField *f)
{
	if (field_is_singular(f))
		return ENCLOSING_NONE;
	if (field_supports_presence(f) || field_is_in_oneof(f))
		return ENCLOSING_OPTION;
	return enc_collection(ScalaSeq, NULL);
}

static bool field_is_map(const JField *f) { return jfield_is_map_field(f); }

static const char *field_single_scala_type_name(JField *f);

static const char *field_map_key_type(JField *f) { return field_single_scala_type_name(map_key_field(f->msg_type)); }
static const char *field_map_value_type(JField *f) { return field_single_scala_type_name(map_value_field(f->msg_type)); }

static const char *field_collection_type(JField *f)
{
	if (!jfield_is_repeated(f))
		java_exception("java.lang.IllegalArgumentException", "requirement failed");
	PbMsg *o = field_options(f);
	PbMsg *coll = pb_msg(o, SFO_COLLECTION);
	PbMsg *fo = file_opts(f->file);
	if (field_is_map(f)) {
		if (pb_has(coll, SCO_TYPE))
			return jstr(coll, SCO_TYPE);
		if (pb_has(o, SFO_MAP_TYPE))
			return jstr(o, SFO_MAP_TYPE);
		if (pb_has(fo, SPO_MAP_TYPE))
			return jstr(fo, SPO_MAP_TYPE);
		return ScalaMap;
	}
	if (pb_has(coll, SCO_TYPE))
		return jstr(coll, SCO_TYPE);
	if (pb_has(o, SFO_COLLECTION_TYPE))
		return jstr(o, SFO_COLLECTION_TYPE);
	if (pb_has(fo, SPO_COLLECTION_TYPE))
		return jstr(fo, SPO_COLLECTION_TYPE);
	return ScalaSeq;
}

static const char *field_scala_type_name(JField *f)
{
	if (field_is_map(f))
		return xcat(field_collection_type(f), "[", field_map_key_type(f), ", ", field_map_value_type(f), "]");
	if (jfield_is_repeated(f))
		return xcat(field_collection_type(f), "[", field_single_scala_type_name(f), "]");
	if (field_supports_presence(f))
		return xcat(ScalaOption, "[", field_single_scala_type_name(f), "]");
	return field_single_scala_type_name(f);
}

static Vec field_annotation_list(JField *f)
{
	Vec d = {0};
	if (jpb_bool(jfield_options(f), FO_DEPRECATED, false))
		vec_push(&d, DEPRECATED_ANNOTATION);
	Vec a = pb_strs(field_options(f), SFO_ANNOTATIONS);
	return vec_concat(&d, &a);
}

static PbMsg *msg_messages_options_of_type(JMsg *m) { return message_options(m); }

/* customSingleScalaTypeName: NULL for None */
static const char *field_custom_single_scala_type_name(JField *f)
{
	PbMsg *o = field_options(f);
	if (field_is_map(f))
		return xcat("(", field_map_key_type(f), ", ", field_map_value_type(f), ")");
	if (field_is_sealed_oneof_type(f))
		return msg_sealed_oneof_scala_type(f->msg_type);
	if (pb_has(o, SFO_TYPE))
		return jstr(o, SFO_TYPE);
	if (field_is_message(f) && pb_has(msg_messages_options_of_type(f->msg_type), SMO_TYPE))
		return jstr(message_options(f->msg_type), SMO_TYPE);
	if (field_is_enum(f) && pb_has(enum_options(f->enum_type), SEO_TYPE))
		return jstr(enum_options(f->enum_type), SEO_TYPE);
	if (f->type == PT_BYTES && pb_has(file_opts(f->file), SPO_BYTES_TYPE))
		return jstr(file_opts(f->file), SPO_BYTES_TYPE);
	JMsg *ct = f->containing_type;
	if (ct && jmsg_is_map_entry(ct) && (f->number == 1 || f->number == 2)) {
		/* fieldReferencingMap */
		JField *ref = NULL;
		if (!ct->parent)
			java_exception("java.lang.NullPointerException", NULL);
		for (int i = 0; i < ct->parent->fields.n && !ref; i++) {
			JField *x = VEC_AT(ct->parent->fields, JField, i);
			if (field_is_map(x) && x->msg_type == ct)
				ref = x;
		}
		if (!ref)
			java_exception("java.util.NoSuchElementException", "None.get");
		uint32_t num = f->number == 1 ? SFO_KEY_TYPE : SFO_VALUE_TYPE;
		if (pb_has(field_options(ref), num))
			return jstr(field_options(ref), num);
	}
	if (field_is_message(f) && file_use_primitive_wrappers(f->file))
		return primitive_wrapper_type(f->msg_type);
	return NULL;
}

static const char *field_base_single_scala_type_name(JField *f)
{
	switch (jfield_java_type(f)) {
	case JT_INT: return "_root_.scala.Int";
	case JT_LONG: return "_root_.scala.Long";
	case JT_FLOAT: return "_root_.scala.Float";
	case JT_DOUBLE: return "_root_.scala.Double";
	case JT_BOOLEAN: return "_root_.scala.Boolean";
	case JT_BYTE_STRING: return "_root_.com.google.protobuf.ByteString";
	case JT_STRING: return "_root_.scala.Predef.String";
	case JT_MESSAGE: {
		if (!f->containing_type)
			java_exception("java.lang.NullPointerException", NULL);
		return sn_full_name_with_maybe_root_ctx(msg_scala_type(f->msg_type), msg_context_names(f->containing_type));
	}
	default: {
		if (!f->containing_type)
			java_exception("java.lang.NullPointerException", NULL);
		return sn_full_name_with_maybe_root_ctx(enum_scala_type(f->enum_type), msg_context_names(f->containing_type));
	}
	}
}

static const char *field_single_scala_type_name(JField *f)
{
	const char *c = field_custom_single_scala_type_name(f);
	return c ? c : field_base_single_scala_type_name(f);
}

static const char *field_get_method(JField *f) { return xcat("get", field_upper_scala_name(f)); }

static const char *field_type_mapper_val_name(JField *f) { return xcat("_typemapper_", field_scala_name(f)); }

static ScalaName *field_type_mapper(JField *f)
{
	if (!f->is_ext)
		return sn_child(msg_scala_type(f->containing_type), field_type_mapper_val_name(f));
	ScalaName *c = f->ext_scope ? msg_scala_type(f->ext_scope) : file_descriptor_object(f->file);
	return sn_child(c, field_type_mapper_val_name(f));
}

static int field_source_path(const JField *f, int *path)
{
	/* fd.getContainingType.sourcePath ++ Seq(FIELD_FIELD_NUMBER, fd.getIndex) */
	int n = msg_source_path(f->containing_type, path);
	path[n] = DP_FIELD;
	path[n + 1] = f->index;
	return n + 2;
}

static const char *field_comment(JField *f)
{
	if (!f->containing_type)
		java_exception("java.lang.NullPointerException", NULL);
	int path[MAX_PATH];
	int n = field_source_path(f, path);
	return comment_at(f->file, path, n);
}

/* ---- CollectionMethods.scala ---- */

static const char *field_collection_adapter_class(JField *f)
{
	PbMsg *coll = pb_msg(field_options(f), SFO_COLLECTION);
	return pb_has(coll, SCO_ADAPTER) ? jstr(coll, SCO_ADAPTER) : NULL;
}

static ScalaName *field_collection_adapter(JField *f)
{
	if (!field_collection_adapter_class(f))
		return NULL;
	return sn_child(msg_scala_type(f->containing_type), xcat("_adapter_", field_scala_name(f)));
}

static const char *coll_new_builder(JField *f)
{
	const char *ct = field_collection_type(f);
	const char *t = str_eq(ct, ScalaSeq) ? ScalaVector : ct;
	ScalaName *a = field_collection_adapter(f);
	if (a)
		return xcat(sn_full_name(a), ".newBuilder");
	if (!field_is_map(f)) {
		if (str_eq(t, ScalaVector))
			return xcat("new _root_.scala.collection.immutable.VectorBuilder[", field_single_scala_type_name(f), "]");
		return xcat(t, ".newBuilder[", field_single_scala_type_name(f), "]");
	}
	return xcat(t, ".newBuilder[", field_map_key_type(f), ", ", field_map_value_type(f), "]");
}

static const char *coll_builder_type(JField *f)
{
	ScalaName *a = field_collection_adapter(f);
	if (a)
		return xcat(sn_full_name(a), ".Builder");
	const char *ct = field_collection_type(f);
	if (str_eq(ct, ScalaSeq) || str_eq(ct, ScalaVector))
		return xcat("_root_.scala.collection.immutable.VectorBuilder[", field_single_scala_type_name(f), "]");
	return xcat("_root_.scala.collection.mutable.Builder[", field_single_scala_type_name(f), ", ", field_scala_type_name(f), "]");
}

static const char *coll_empty(JField *f)
{
	ScalaName *a = field_collection_adapter(f);
	if (a)
		return xcat(sn_full_name(a), ".empty");
	return xcat(field_collection_type(f), ".empty");
}

static const char *coll_foreach(JField *f)
{
	ScalaName *a = field_collection_adapter(f);
	if (a)
		return xcat(sn_full_name(a), ".foreach(", as_symbol(field_scala_name(f)), ")");
	return xcat(as_symbol(field_scala_name(f)), ".foreach");
}

static const char *coll_concat(JField *f, const char *left, const char *right)
{
	ScalaName *a = field_collection_adapter(f);
	if (a)
		return xcat(sn_full_name(a), ".concat(", left, ", ", right, ")");
	return xcat(left, " ++ ", right);
}

static bool coll_non_empty_type(JField *f) { return jpb_bool(pb_msg(field_options(f), SFO_COLLECTION), SCO_NON_EMPTY, false); }

static const char *coll_non_empty_check(JField *f, const char *expr)
{
	return coll_non_empty_type(f) ? "true" : xcat(expr, ".nonEmpty");
}

/* ---- ExtendedServiceDescriptor / ExtendedMethodDescriptor ---- */

static ScalaName *service_companion_object(JService *s) { return sn_child(file_scala_package(s->file), xcat(s->name, "Grpc")); }
static const char *service_name(JService *s) { return as_symbol(s->name); }

static const char *service_scala_descriptor_source(JService *s)
{
	return xfmt("%s.scalaDescriptor.services(%d)", sn_full_name(file_descriptor_object(s->file)), s->index);
}

static const char *service_java_descriptor_source(JService *s)
{
	return xfmt("%s.javaDescriptor.getServices().get(%d)", sn_full_name(file_descriptor_object(s->file)), s->index);
}

static const char *service_comment(JService *s)
{
	int path[2] = {FDP_SERVICE, s->index};
	return comment_at(s->file, path, 2);
}

static const char *service_deprecated_annotation(JService *s)
{
	return jpb_bool(pb_msg(s->proto, 3), 33, false) ? DEPRECATED_ANNOTATION " " : "";
}

static bool method_client_streaming(const JMethod *m) { return jpb_bool(m->proto, MDP_CLIENT_STREAMING, false); }
static bool method_server_streaming(const JMethod *m) { return jpb_bool(m->proto, MDP_SERVER_STREAMING, false); }

static const char *method_name(JMethod *m) { return as_symbol(snake_case_to_camel_case(m->name, false)); }

static ScalaName *method_grpc_descriptor(JMethod *m)
{
	return sn_child(service_companion_object(m->service), xcat("METHOD_", to_all_caps(m->name)));
}

static const char *method_comment(JMethod *m)
{
	int path[4] = {FDP_SERVICE, m->service->index, SDP_METHOD, m->index};
	return comment_at(m->file, path, 4);
}

static const char *method_deprecated_annotation(JMethod *m)
{
	return jpb_bool(pb_msg(m->proto, MDP_OPTIONS), 33, false) ? DEPRECATED_ANNOTATION " " : "";
}

static const char *method_java_descriptor_source(JMethod *m)
{
	return xfmt("%s.getMethods().get(%d)", service_java_descriptor_source(m->service), m->index);
}

/* MethodTypeWrapper */
static const char *method_type_custom(JMsg *d)
{
	if (msg_is_sealed_oneof_type(d))
		return msg_sealed_oneof_scala_type(d);
	if (pb_has(message_options(d), SMO_TYPE))
		return jstr(message_options(d), SMO_TYPE);
	if (file_use_primitive_wrappers(d->file))
		return primitive_wrapper_type(d);
	return NULL;
}

static const char *method_type_base(JMsg *d)
{
	Map ctx = {0};
	map_put(&ctx, "build", &ctx);
	return sn_full_name_with_maybe_root_ctx(msg_scala_type(d), &ctx);
}

static const char *method_type_scala(JMsg *d)
{
	const char *c = method_type_custom(d);
	return c ? c : method_type_base(d);
}

/* ---- FileOptionsCache.scala ---- */

/* parentPackages("a.b.c") = ["a.b", "a"] */
/* The lengths of parentPackages(package), nearest first: package.split('.').scanLeft(...) joined
 * with '.', which are the prefixes of package up to its first k dots (k = parts - 1 down to 1). */
static int parent_package_lengths(const char *package, size_t *out)
{
	int parts = java_split(package, '.').n, n = 0;
	size_t ndots = 0;
	for (size_t i = 0; package[i]; i++)
		if (package[i] == '.' && (int)++ndots < parts)
			out[n++] = i;
	for (int i = 0; i < n / 2; i++) {
		size_t t = out[i];
		out[i] = out[n - 1 - i], out[n - 1 - i] = t;
	}
	return n;
}

static PbMsg *raw_file_scala_opts(const JFile *f) { return local_opts(file_options_proto(f), &sp_ScalaPbOptions); }

static bool opts_package_scoped(const PbMsg *o) { return pb_int(o, SPO_SCOPE, 0) == 1; }

static void jpb_set_int(PbMsg *m, uint32_t num, int64_t v)
{
	for (int i = 0; i < m->def->n; i++) {
		if (m->def->f[i].num != num)
			continue;
		PbSlot *s = &m->slots[i];
		s->v = xalloc(sizeof(PbVal));
		s->v[0].u = (uint64_t)v;
		s->n = s->cap = 1;
	}
}

static PbMsg *merge_options(const PbMsg *parent, const PbMsg *child)
{
	PbMsg *r = jpb_merge(&sp_ScalaPbOptions, parent, child);
	jpb_set_int(r, SPO_SCOPE, pb_int(child, SPO_SCOPE, 0));
	return r;
}

/* Preprocessor names are kept as Vec* of strings. */
static Vec clear_negated_preprocessors(const Vec *input)
{
	Vec out = {0};
	Map negated = {0};
	for (int i = 0; i < input->n; i++) {
		const char *q = input->v[i];
		if (q[0] == '-')
			map_put(&negated, q + 1, &negated);
	}
	for (int i = 0; i < input->n; i++) {
		const char *p = input->v[i];
		if (p[0] != '-' && !map_has(&negated, p))
			vec_push(&out, (void *)p);
	}
	return out;
}

enum { REDUCE_OPTIONS, REDUCE_PREPROCESSORS };

/* reducePackageOptions: files sorted by (package, not package-scoped); returns one value per
 * file of all_files (PbMsg* or Vec*). */
typedef struct {
	JFile *f;
	PbMsg *opts;
	bool scoped;
	int index;
} PackageOptions;

/* sortBy(f => (f.getPackage(), f.getScope != OptionsScope.PACKAGE)) */
static int package_options_cmp(const void *a, const void *b)
{
	const PackageOptions *x = a, *y = b;
	int c = java_strcmp(x->f->package, y->f->package);
	return c ? c : (int)!x->scoped - (int)!y->scoped;
}

static void **reduce_package_options(int what)
{
	int n = all_files.n;
	PackageOptions **order = xalloc(sizeof(PackageOptions *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		PackageOptions *po = xalloc(sizeof *po);
		po->f = VEC_AT(all_files, JFile, i);
		po->opts = raw_file_scala_opts(po->f);
		po->scoped = opts_package_scoped(po->opts);
		po->index = i;
		order[i] = po;
	}
	PackageOptions **by_index = xalloc(sizeof(PackageOptions *) * (size_t)(n ? n : 1));
	memcpy(by_index, order, sizeof(PackageOptions *) * (size_t)n);
	java_sort((void **)order, n, package_options_cmp);
	void **out = xalloc(sizeof(void *) * (size_t)(n ? n : 1));
	JSymTab by_package = {0}; /* package -> JSym whose p is the result */
	for (int k = 0; k < n; k++) {
		JFile *f = order[k]->f;
		PbMsg *opts = order[k]->opts;
		bool scoped = order[k]->scoped;
		size_t plen = strlen(f->package);
		uint64_t phash = jhash_add(JHASH0, f->package, plen);
		if (scoped && !f->package[0])
			generator_exception("%s: a package statement is required when package-scoped options are used", f->name);
		if (scoped && jsymtab_get(&by_package, f->package, plen, NULL, 0, phash)) {
			Vec dups = {0};
			for (int i = 0; i < n; i++)
				if (str_eq(by_index[i]->f->package, f->package) && by_index[i]->scoped)
					vec_push(&dups, (void *)by_index[i]->f->name);
			generator_exception("Multiple files contain package-scoped options for package '%s': %s", f->package, str_join(&dups, ", "));
		}
		if (scoped && pb_has(opts, SPO_OBJECT_NAME))
			generator_exception("%s: object_name is not allowed in package-scoped options.", f->name);
		/* packagesToInheritFrom.find(byPackage.contains(_)) */
		JSym *base = scoped ? NULL : jsymtab_get(&by_package, f->package, plen, NULL, 0, phash);
		if (!base && by_package.n) {
			size_t ndots = 0;
			for (size_t i = 0; i < plen; i++)
				ndots += f->package[i] == '.';
			size_t *lens = xalloc(sizeof(size_t) * (ndots + 1));
			uint64_t *hashes = xalloc(sizeof(uint64_t) * (ndots + 1));
			int nl = parent_package_lengths(f->package, lens);
			/* the hash of each prefix (lens is decreasing) */
			uint64_t h = JHASH0;
			size_t at = 0;
			for (int i = nl - 1; i >= 0; i--) {
				h = jhash_add(h, f->package + at, lens[i] - at);
				at = lens[i];
				hashes[i] = h;
			}
			for (int i = 0; i < nl && !base; i++)
				base = jsymtab_get(&by_package, f->package, lens[i], NULL, 0, hashes[i]);
		}
		void *res;
		if (what == REDUCE_OPTIONS) {
			res = base ? merge_options(base->p, opts) : opts;
		} else {
			Vec *own = xalloc(sizeof *own);
			*own = pb_strs(opts, SPO_PREPROCESSORS);
			if (base) {
				Vec cat = vec_concat(base->p, own);
				*own = clear_negated_preprocessors(&cat);
			}
			res = own;
		}
		out[order[k]->index] = res;
		if (scoped) {
			JSym *sym = xalloc(sizeof *sym);
			sym->p = res;
			jsymtab_put(&by_package, f->package, plen, phash, sym);
		}
	}
	return out;
}

static bool preprocessor_name_valid(const char *s)
{
	if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')))
		return false;
	for (const char *p = s + 1; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || is_digit(*p) || *p == '_' || *p == '.' || *p == '-'))
			return false;
	return true;
}

static bool file_opts_built;

static void build_file_options_cache(void)
{
	file_opts_built = true;
	void **pre = reduce_package_options(REDUCE_PREPROCESSORS);
	const char *first = NULL;
	for (int i = 0; i < all_files.n; i++) {
		Vec *names = pre[i];
		for (int j = 0; j < names->n; j++) {
			if (!preprocessor_name_valid(names->v[j]))
				generator_exception("%s: Invalid preprocessor name: '%s'", VEC_AT(all_files, JFile, i)->name, (const char *)names->v[j]);
			if (!first)
				first = names->v[j];
		}
	}
	if (first) {
		/* EmptySecondaryOutputProvider: the remote plugin has no secondary output directory */
		Vec files = {0};
		for (int i = 0; i < all_files.n; i++)
			if (vec_contains_str(pre[i], first))
				vec_push(&files, (void *)VEC_AT(all_files, JFile, i)->name);
		generator_exception("%s: No secondary outputs available. The most likely causes are that you are using an older "
		     "version of sbt-protoc, or the build tool you are using does not support secondary outputs.",
		     str_join(&files, ", "));
	}
	void **opts = reduce_package_options(REDUCE_OPTIONS);
	for (int i = 0; i < all_files.n; i++) {
		JFile *f = VEC_AT(all_files, JFile, i);
		PbMsg *o = opts[i];
		if (pb_count(raw_file_scala_opts(f), SPO_FIELD_TRANSFORMATIONS) || pb_count(o, SPO_FIELD_TRANSFORMATIONS))
			fail("%s: field_transformations are not supported by protoc-gen-uni", f->name);
		f->scala_opts = o;
	}
}

static PbMsg *file_opts(JFile *f)
{
	if (!file_opts_built)
		build_file_options_cache();
	if (!f->scala_opts)
		f->scala_opts = raw_file_scala_opts(f); /* not in the request (cannot happen) */
	return f->scala_opts;
}
