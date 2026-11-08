// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/helpers.{h,cc} and internal_helpers.{h,cc} (without
 * GenerateLarge, which only the large_enum feature reaches; see the generator README), and the
 * accessors of java_features.pb.h and descriptor.pb.h options the generators read. */

const char jv_thick_separator[] = "// ===================================================================\n";
const char jv_thin_separator[] = "// -------------------------------------------------------------------\n";

/* ---- per-descriptor data ---- */

static JvFileData *jv_file_data(const LpFile *f)
{
	LpFile *mf = (LpFile *)f;
	if (!mf->user)
		mf->user = xalloc(sizeof(JvFileData));
	return mf->user;
}

static JvMessageData *jv_message_data(const LpMessage *m)
{
	LpMessage *mm = (LpMessage *)m;
	if (!mm->user)
		mm->user = xalloc(sizeof(JvMessageData));
	return mm->user;
}

/* ---- features and options ---- */

int jv_resolved_java_feature(const ProtoDesc *d, int field)
{
	return (int)pb_int(pb_msg(d->merged_features, JV_FEATURES_EXT), (uint32_t)field, 0);
}

bool jv_unresolved_has_java_feature(const ProtoDesc *d, int field)
{
	return pb_has(pb_msg(d->proto_features, JV_FEATURES_EXT), (uint32_t)field);
}

bool jv_opt_bool(const ProtoDesc *d, int field) { return pb_int(d->options, (uint32_t)field, 0) != 0; }
bool jv_file_deprecated(const LpFile *f) { return jv_opt_bool((const ProtoDesc *)f, JV_FILEOPT_DEPRECATED); }
bool jv_message_deprecated(const LpMessage *m) { return jv_opt_bool((const ProtoDesc *)m, JV_MSGOPT_DEPRECATED); }
bool jv_field_deprecated(const LpField *f) { return jv_opt_bool((const ProtoDesc *)f, JV_FIELDOPT_DEPRECATED); }
bool jv_enum_deprecated(const LpEnum *e) { return jv_opt_bool((const ProtoDesc *)e, JV_ENUMOPT_DEPRECATED); }
bool jv_enum_value_deprecated(const LpEnumValue *v)
{
	return jv_opt_bool((const ProtoDesc *)v, JV_ENUMVALOPT_DEPRECATED);
}
int jv_file_optimize_for(const LpFile *f) { return (int)pb_int(f->options, JV_FILEOPT_OPTIMIZE_FOR, JV_OPTIMIZE_SPEED); }

/* ---- helpers.cc ---- */

void jv_print_generated_annotation(LpPrinter *p, char delimiter, const char *annotation_file,
				   const JvOptions *options)
{
	(void)options;
	lp_print(p, "@com.google.protobuf.Generated\n", NULL);
	if (!annotation_file || !*annotation_file)
		return;
	char d[2] = {delimiter, 0};
	const char *ptemplate = xcat("@javax.annotation.Generated(value=\"protoc\", comments=\"annotations:", d,
				     "annotation_file", d, "\")\n");
	lp_print(p, ptemplate, "annotation_file", annotation_file, NULL);
}

void jv_print_enum_verifier_logic(LpPrinter *p, const LpField *descriptor, const LpVars *variables,
				  const char *var_name, const char *terminating_string, bool enforce_lite)
{
	(void)descriptor;
	const char *enum_verifier_string =
		enforce_lite ? xcat(var_name, ".internalGetVerifier()")
			     : xcat("new com.google.protobuf.Internal.EnumVerifier() {\n"
				    "        @java.lang.Override\n"
				    "        public boolean isInRange(int number) {\n"
				    "          return ",
				    var_name,
				    ".forNumber(number) != null;\n"
				    "        }\n"
				    "      }");
	lp_print_vars(p, variables, xcat(enum_verifier_string, terminating_string));
}

void jv_print_gencode_version_validator(LpPrinter *p, bool oss_runtime, const char *java_class_name)
{
	/* GetProtobufJavaVersion(oss_runtime): PROTOBUF_JAVA_VERSION_STRING "4.36.2" */
	lp_print(p,
		 "com.google.protobuf.RuntimeVersion.validateProtobufGencodeVersion(\n"
		 "  com.google.protobuf.RuntimeVersion.RuntimeDomain.$domain$,\n"
		 "  $major$,\n"
		 "  $minor$,\n"
		 "  $patch$,\n"
		 "  $suffix$,\n"
		 "  \"$location$\");\n",
		 "domain", oss_runtime ? "PUBLIC" : "GOOGLE_INTERNAL", "major", "/* major= */ 4", "minor",
		 "/* minor= */ 36", "patch", "/* patch= */ 2", "suffix", "/* suffix= */ \"\"", "location",
		 java_class_name, NULL);
}

char *jv_underscores_to_camel_case(const char *input, bool cap_next_letter)
{
	size_t n = strlen(input);
	if (n == 0)
		fail("Check failed: !input.empty()");
	Buf result = {0};
	for (size_t i = 0; i < n; i++) {
		char c = input[i];
		if ('a' <= c && c <= 'z') {
			buf_addc(&result, cap_next_letter ? (char)(c + ('A' - 'a')) : c);
			cap_next_letter = false;
		} else if ('A' <= c && c <= 'Z') {
			if (i == 0 && !cap_next_letter)
				buf_addc(&result, (char)(c + ('a' - 'A')));
			else
				buf_addc(&result, c);
			cap_next_letter = false;
		} else if ('0' <= c && c <= '9') {
			buf_addc(&result, c);
			cap_next_letter = true;
		} else {
			cap_next_letter = true;
		}
	}
	if (input[n - 1] == '#')
		buf_addc(&result, '_');
	return (char *)buf_str(&result);
}

char *jv_to_camel_case(const char *input, bool lower_first)
{
	bool capitalize_next = !lower_first;
	Buf result = {0};
	for (const char *p = input; *p; p++) {
		if (*p == '_') {
			capitalize_next = true;
		} else if (capitalize_next) {
			buf_addc(&result, jv_ascii_toupper(*p));
			capitalize_next = false;
		} else {
			buf_addc(&result, *p);
		}
	}
	char *s = (char *)buf_str(&result);
	if (lower_first && *s)
		s[0] = jv_ascii_tolower(s[0]);
	return s;
}

bool jv_is_forbidden_kotlin(const char *field_name)
{
	static const char *const names[] = {
		"as",      "as?",       "break",  "class", "continue", "do",    "else",  "false",
		"for",     "fun",       "if",     "in",    "!in",      "interface", "is", "!is",
		"null",    "object",    "package", "return", "super",  "this",  "throw", "true",
		"try",     "typealias", "typeof", "val",   "var",      "when",  "while",
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (str_eq(names[i], field_name))
			return true;
	return false;
}

char *jv_escape_kotlin_keywords(const char *name)
{
	Vec packages = str_split(name, '.');
	Vec escaped = {0};
	for (int i = 0; i < packages.n; i++) {
		const char *package = VEC_AT(packages, const char, i);
		vec_push(&escaped, jv_is_forbidden_kotlin(package) ? xcat("`", package, "`") : (char *)package);
	}
	return str_join(&escaped, ".");
}

char *jv_unique_file_scope_identifier(const LpMessage *descriptor)
{
	return xcat("static_", str_replace_all(descriptor->full_name, ".", "_"));
}

char *jv_camel_case_field_name(const LpField *field)
{
	char *field_name = jv_underscores_to_camel_case_field(field);
	if ('0' <= field_name[0] && field_name[0] <= '9')
		return xcat("_", field_name);
	return field_name;
}

char *jv_file_class_name(const LpFile *file, bool immutable) { return jv_get_file_class_name(file, immutable, false); }

char *jv_java_package_to_dir(const char *package_name)
{
	char *package_dir = str_replace_all(package_name, ".", "/");
	if (*package_dir)
		package_dir = xcat(package_dir, "/");
	return package_dir;
}

char *jv_extra_message_interfaces(const LpMessage *descriptor)
{
	return xcat("// @@protoc_insertion_point(message_implements:", descriptor->full_name, ")");
}

char *jv_extra_builder_interfaces(const LpMessage *descriptor)
{
	return xcat("// @@protoc_insertion_point(builder_implements:", descriptor->full_name, ")");
}

char *jv_extra_message_or_builder_interfaces(const LpMessage *descriptor)
{
	return xcat("// @@protoc_insertion_point(interface_extends:", descriptor->full_name, ")");
}

bool jv_is_descriptor_proto(const LpMessage *descriptor)
{
	return str_eq(descriptor->file->name, "net/proto2/proto/descriptor.proto") ||
	       str_eq(descriptor->file->name, "google/protobuf/descriptor.proto");
}

char *jv_field_constant_name(const LpField *field)
{
	return jv_ascii_str_to_upper(xcat(field->name, "_FIELD_NUMBER"));
}

int jv_get_type(const LpField *field) { return field->type; }

JvJavaType jv_get_java_type(const LpField *field)
{
	switch (jv_get_type(field)) {
	case PT_INT32:
	case PT_UINT32:
	case PT_SINT32:
	case PT_FIXED32:
	case PT_SFIXED32: return JAVATYPE_INT;
	case PT_INT64:
	case PT_UINT64:
	case PT_SINT64:
	case PT_FIXED64:
	case PT_SFIXED64: return JAVATYPE_LONG;
	case PT_FLOAT: return JAVATYPE_FLOAT;
	case PT_DOUBLE: return JAVATYPE_DOUBLE;
	case PT_BOOL: return JAVATYPE_BOOLEAN;
	case PT_STRING: return JAVATYPE_STRING;
	case PT_BYTES: return JAVATYPE_BYTES;
	case PT_ENUM: return JAVATYPE_ENUM;
	case PT_GROUP:
	case PT_MESSAGE: return JAVATYPE_MESSAGE;
	}
	fail("Can't get here.");
}

const char *jv_primitive_type_name(JvJavaType type)
{
	switch (type) {
	case JAVATYPE_INT: return "int";
	case JAVATYPE_LONG: return "long";
	case JAVATYPE_FLOAT: return "float";
	case JAVATYPE_DOUBLE: return "double";
	case JAVATYPE_BOOLEAN: return "boolean";
	case JAVATYPE_STRING: return "java.lang.String";
	case JAVATYPE_BYTES: return "com.google.protobuf.ByteString";
	default: return NULL;
	}
}

const char *jv_boxed_primitive_type_name(JvJavaType type)
{
	switch (type) {
	case JAVATYPE_INT: return "java.lang.Integer";
	case JAVATYPE_LONG: return "java.lang.Long";
	case JAVATYPE_FLOAT: return "java.lang.Float";
	case JAVATYPE_DOUBLE: return "java.lang.Double";
	case JAVATYPE_BOOLEAN: return "java.lang.Boolean";
	case JAVATYPE_STRING: return "java.lang.String";
	case JAVATYPE_BYTES: return "com.google.protobuf.ByteString";
	default: return NULL;
	}
}

const char *jv_kotlin_type_name(JvJavaType type)
{
	switch (type) {
	case JAVATYPE_INT: return "kotlin.Int";
	case JAVATYPE_LONG: return "kotlin.Long";
	case JAVATYPE_FLOAT: return "kotlin.Float";
	case JAVATYPE_DOUBLE: return "kotlin.Double";
	case JAVATYPE_BOOLEAN: return "kotlin.Boolean";
	case JAVATYPE_STRING: return "kotlin.String";
	case JAVATYPE_BYTES: return "com.google.protobuf.ByteString";
	default: return NULL;
	}
}

char *jv_get_oneof_stored_type(const LpField *field)
{
	JvJavaType java_type = jv_get_java_type(field);
	switch (java_type) {
	case JAVATYPE_ENUM: return "java.lang.Integer";
	case JAVATYPE_MESSAGE: return jv_get_class_name_message(field->message_type, true, false);
	default: return (char *)jv_boxed_primitive_type_name(java_type);
	}
}

const char *jv_field_type_name(int field_type)
{
	switch (field_type) {
	case PT_INT32: return "INT32";
	case PT_UINT32: return "UINT32";
	case PT_SINT32: return "SINT32";
	case PT_FIXED32: return "FIXED32";
	case PT_SFIXED32: return "SFIXED32";
	case PT_INT64: return "INT64";
	case PT_UINT64: return "UINT64";
	case PT_SINT64: return "SINT64";
	case PT_FIXED64: return "FIXED64";
	case PT_SFIXED64: return "SFIXED64";
	case PT_FLOAT: return "FLOAT";
	case PT_DOUBLE: return "DOUBLE";
	case PT_BOOL: return "BOOL";
	case PT_STRING: return "STRING";
	case PT_BYTES: return "BYTES";
	case PT_ENUM: return "ENUM";
	case PT_GROUP: return "GROUP";
	case PT_MESSAGE: return "MESSAGE";
	}
	fail("Can't get here.");
}

static bool all_ascii(Str text)
{
	for (size_t i = 0; i < text.n; i++)
		if (text.p[i] & 0x80)
			return false;
	return true;
}

char *jv_default_value(const LpField *field, bool immutable, const JvOptions *options)
{
	(void)options;
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: return jv_itoa((int32_t)field->default_value.i);
	case LP_CPPTYPE_UINT32: return jv_itoa((int32_t)(uint32_t)field->default_value.u);
	case LP_CPPTYPE_INT64: return xcat(jv_itoa(field->default_value.i), "L");
	case LP_CPPTYPE_UINT64: return xcat(jv_itoa((int64_t)field->default_value.u), "L");
	case LP_CPPTYPE_DOUBLE: {
		double value = field->default_value.d;
		if (value == INFINITY)
			return "Double.POSITIVE_INFINITY";
		if (value == -INFINITY)
			return "Double.NEGATIVE_INFINITY";
		if (value != value)
			return "Double.NaN";
		return xcat(lp_simple_dtoa(value), "D");
	}
	case LP_CPPTYPE_FLOAT: {
		float value = field->default_value.f;
		if (value == INFINITY)
			return "Float.POSITIVE_INFINITY";
		if (value == -INFINITY)
			return "Float.NEGATIVE_INFINITY";
		if (value != value)
			return "Float.NaN";
		return xcat(lp_simple_ftoa(value), "F");
	}
	case LP_CPPTYPE_BOOL: return field->default_value.b ? "true" : "false";
	case LP_CPPTYPE_STRING:
		if (jv_get_type(field) == PT_BYTES) {
			if (field->has_default_value)
				return xcat("com.google.protobuf.Internal.bytesDefaultValue(\"",
					    lp_cescape(field->default_value_string), "\")");
			return "com.google.protobuf.ByteString.EMPTY";
		}
		if (all_ascii(field->default_value_string))
			return xcat("\"", lp_cescape(field->default_value_string), "\"");
		return xcat("com.google.protobuf.Internal.stringDefaultValue(\"", lp_cescape(field->default_value_string),
			    "\")");
	case LP_CPPTYPE_ENUM:
		return xcat(jv_get_class_name_enum(field->enum_type, immutable, false), ".",
			    field->default_value_enum ? field->default_value_enum->name : "");
	case LP_CPPTYPE_MESSAGE:
		return xcat(jv_get_class_name_message(field->message_type, immutable, false), ".getDefaultInstance()");
	}
	fail("Can't get here.");
}

bool jv_is_default_value_java_default(const LpField *field)
{
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: return (int32_t)field->default_value.i == 0;
	case LP_CPPTYPE_UINT32: return (uint32_t)field->default_value.u == 0;
	case LP_CPPTYPE_INT64: return field->default_value.i == 0;
	case LP_CPPTYPE_UINT64: return field->default_value.u == 0;
	case LP_CPPTYPE_DOUBLE: return field->default_value.d == 0.0;
	case LP_CPPTYPE_FLOAT: return field->default_value.f == 0.0;
	case LP_CPPTYPE_BOOL: return !field->default_value.b;
	case LP_CPPTYPE_ENUM: return field->default_value_enum && field->default_value_enum->number == 0;
	case LP_CPPTYPE_STRING:
	case LP_CPPTYPE_MESSAGE: return false;
	}
	fail("Can't get here.");
}

bool jv_is_byte_string_with_custom_default_value(const LpField *field)
{
	return jv_get_java_type(field) == JAVATYPE_BYTES && field->default_value_string.n != 0;
}

bool jv_has_descriptor_methods(bool enforce_lite) { return !enforce_lite; }

bool jv_has_generic_services(const LpFile *file, bool enforce_lite)
{
	return file->n_services > 0 && jv_has_descriptor_methods(enforce_lite) &&
	       jv_opt_bool((const ProtoDesc *)file, JV_FILEOPT_JAVA_GENERIC_SERVICES);
}

static const char *const bit_masks[] = {
	"0x00000001", "0x00000002", "0x00000004", "0x00000008", "0x00000010", "0x00000020", "0x00000040",
	"0x00000080", "0x00000100", "0x00000200", "0x00000400", "0x00000800", "0x00001000", "0x00002000",
	"0x00004000", "0x00008000", "0x00010000", "0x00020000", "0x00040000", "0x00080000", "0x00100000",
	"0x00200000", "0x00400000", "0x00800000", "0x01000000", "0x02000000", "0x04000000", "0x08000000",
	"0x10000000", "0x20000000", "0x40000000", "0x80000000",
};

char *jv_get_bit_field_name(int index) { return xcat("bitField", jv_itoa(index), "_"); }
char *jv_get_bit_field_name_for_bit(int bit_index) { return jv_get_bit_field_name(bit_index / 32); }

static char *generate_get_bit_internal(const char *prefix, int bit_index)
{
	char *var_name = xcat(prefix, jv_get_bit_field_name_for_bit(bit_index));
	return xcat("((", var_name, " & ", bit_masks[bit_index % 32], ") != 0)");
}

static char *generate_set_bit_internal(const char *prefix, int bit_index)
{
	char *var_name = xcat(prefix, jv_get_bit_field_name_for_bit(bit_index));
	return xcat(var_name, " |= ", bit_masks[bit_index % 32]);
}

char *jv_generate_get_bit(int bit_index) { return generate_get_bit_internal("", bit_index); }
char *jv_generate_set_bit(int bit_index) { return generate_set_bit_internal("", bit_index); }

char *jv_generate_clear_bit(int bit_index)
{
	char *var_name = jv_get_bit_field_name_for_bit(bit_index);
	return xcat(var_name, " = (", var_name, " & ~", bit_masks[bit_index % 32], ")");
}

char *jv_generate_get_bit_from_local(int bit_index) { return generate_get_bit_internal("from_", bit_index); }
char *jv_generate_set_bit_to_local(int bit_index) { return generate_set_bit_internal("to_", bit_index); }
char *jv_generate_get_bit_mutable_local(int bit_index) { return generate_get_bit_internal("mutable_", bit_index); }
char *jv_generate_set_bit_mutable_local(int bit_index) { return generate_set_bit_internal("mutable_", bit_index); }

bool jv_is_reference_type(JvJavaType type)
{
	switch (type) {
	case JAVATYPE_INT:
	case JAVATYPE_LONG:
	case JAVATYPE_FLOAT:
	case JAVATYPE_DOUBLE:
	case JAVATYPE_BOOLEAN: return false;
	default: return true;
	}
}

const char *jv_get_capitalized_type(const LpField *field, bool immutable)
{
	(void)immutable;
	switch (jv_get_type(field)) {
	case PT_INT32: return "Int32";
	case PT_UINT32: return "UInt32";
	case PT_SINT32: return "SInt32";
	case PT_FIXED32: return "Fixed32";
	case PT_SFIXED32: return "SFixed32";
	case PT_INT64: return "Int64";
	case PT_UINT64: return "UInt64";
	case PT_SINT64: return "SInt64";
	case PT_FIXED64: return "Fixed64";
	case PT_SFIXED64: return "SFixed64";
	case PT_FLOAT: return "Float";
	case PT_DOUBLE: return "Double";
	case PT_BOOL: return "Bool";
	case PT_STRING: return "String";
	case PT_BYTES: return "Bytes";
	case PT_ENUM: return "Enum";
	case PT_GROUP: return "Group";
	case PT_MESSAGE: return "Message";
	}
	fail("Can't get here.");
}

int jv_fixed_size(int type)
{
	switch (type) {
	case PT_FIXED32:
	case PT_SFIXED32:
	case PT_FLOAT: return 4;
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_DOUBLE: return 8;
	case PT_BOOL: return 1;
	default: return -1;
	}
}

static int cmp_field_number(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	return x->number < y->number ? -1 : x->number > y->number;
}

const LpField **jv_sort_fields_by_number(const LpMessage *descriptor)
{
	const LpField **fields = xalloc(sizeof(LpField *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++)
		fields[i] = &descriptor->fields[i];
	/* std::sort: field numbers are unique in a message */
	qsort(fields, (size_t)descriptor->n_fields, sizeof *fields, cmp_field_number);
	return fields;
}

bool jv_has_packed_fields(const LpMessage *descriptor)
{
	for (int i = 0; i < descriptor->n_fields; i++)
		if (proto_field_is_packed(&descriptor->fields[i]))
			return true;
	return false;
}

/* HasRequiredFields(type): whether a type reachable from type through message fields (type
 * included) has an extension range or a required field. Upstream walks the types from each root
 * with a seen set; the answer is that reachability, computed here for all messages of the pool at
 * once (a breadth-first search from those types over the reversed field edges). */
static void collect_messages(LpMessage *messages, int n, Vec *all)
{
	for (int i = 0; i < n; i++) {
		vec_push(all, &messages[i]);
		collect_messages(messages[i].nested, messages[i].n_nested, all);
	}
}

static void compute_has_required(const ProtoPool *pool)
{
	Vec all = {0};
	for (int i = 0; i < pool->file_list.n; i++) {
		LpFile *f = VEC_AT(pool->file_list, LpFile, i);
		collect_messages(f->messages, f->n_messages, &all);
	}
	/* reversed edges: users[m] = messages with a message field of type m */
	Map index = {0};
	for (int i = 0; i < all.n; i++) {
		LpMessage *m = VEC_AT(all, LpMessage, i);
		jv_message_data(m)->has_required = 1;
		map_put(&index, m->full_name, (void *)(intptr_t)(i + 1));
	}
	Vec *users = xalloc(sizeof(Vec) * (size_t)(all.n + 1));
	Vec queue = {0};
	for (int i = 0; i < all.n; i++) {
		LpMessage *m = VEC_AT(all, LpMessage, i);
		bool bad = m->n_extension_ranges > 0;
		for (int j = 0; j < m->n_fields; j++) {
			const LpField *f = &m->fields[j];
			if (proto_field_is_required(f))
				bad = true;
			if (jv_get_java_type(f) == JAVATYPE_MESSAGE && f->message_type) {
				intptr_t k = (intptr_t)map_get(&index, f->message_type->full_name);
				if (k)
					vec_push(&users[k - 1], m);
			}
		}
		if (bad) {
			jv_message_data(m)->has_required = 2;
			vec_push(&queue, m);
		}
	}
	for (int q = 0; q < queue.n; q++) {
		LpMessage *m = VEC_AT(queue, LpMessage, q);
		intptr_t k = (intptr_t)map_get(&index, m->full_name);
		Vec *u = &users[k - 1];
		for (int j = 0; j < u->n; j++) {
			JvMessageData *d = jv_message_data(VEC_AT(*u, LpMessage, j));
			if (d->has_required != 2) {
				d->has_required = 2;
				vec_push(&queue, VEC_AT(*u, LpMessage, j));
			}
		}
	}
}

bool jv_has_required_fields(const LpMessage *descriptor)
{
	JvMessageData *d = jv_message_data(descriptor);
	if (!d->has_required)
		compute_has_required(descriptor->file->pool);
	if (!d->has_required)
		fail("libprotoc java: message %s is not in its pool", descriptor->full_name);
	return d->has_required == 2;
}

bool jv_is_real_oneof(const LpField *descriptor) { return proto_field_real_oneof(descriptor) != NULL; }

bool jv_has_hasbit(const LpField *descriptor)
{
	return proto_field_has_presence(descriptor) && !proto_field_real_oneof(descriptor);
}

bool jv_support_unknown_enum_value(const LpField *field)
{
	if (jv_resolved_java_feature((const ProtoDesc *)field, JV_LEGACY_CLOSED_ENUM))
		return false;
	return field->enum_type != NULL && !proto_enum_is_closed(field->enum_type);
}

bool jv_has_repeated_fields(const LpMessage *descriptor)
{
	for (int i = 0; i < descriptor->n_fields; i++)
		if (proto_field_is_repeated(&descriptor->fields[i]))
			return true;
	return false;
}

bool jv_is_map_entry(const LpMessage *descriptor)
{
	return jv_opt_bool((const ProtoDesc *)descriptor, JV_MSGOPT_MAP_ENTRY);
}
bool jv_is_map_field(const LpField *descriptor) { return proto_field_is_map(descriptor); }
bool jv_is_any_message(const LpMessage *descriptor) { return str_eq(descriptor->full_name, "google.protobuf.Any"); }
bool jv_is_wrappers_proto_file(const LpFile *descriptor)
{
	return str_eq(descriptor->name, "google/protobuf/wrappers.proto");
}

void jv_utf16_push(JvUtf16 *o, uint16_t c)
{
	if (o->n == o->cap) {
		int cap = o->cap ? 2 * o->cap : 64;
		o->v = xrealloc(o->v, sizeof(uint16_t) * (size_t)o->cap, sizeof(uint16_t) * (size_t)cap);
		o->cap = cap;
	}
	o->v[o->n++] = c;
}

void jv_write_uint32_to_utf16_char_sequence(uint32_t number, JvUtf16 *output)
{
	if (number < 0xD800) {
		jv_utf16_push(output, (uint16_t)number);
		return;
	}
	while (number >= 0xD800) {
		jv_utf16_push(output, (uint16_t)(0xE000 | (number & 0x1FFF)));
		number >>= 13;
	}
	jv_utf16_push(output, (uint16_t)number);
}

void jv_write_int_to_utf16_char_sequence(int value, JvUtf16 *output)
{
	jv_write_uint32_to_utf16_char_sequence((uint32_t)value, output);
}

void jv_escape_utf16_to_string(uint16_t code, Buf *output)
{
	if (code == '\t')
		buf_adds(output, "\\t");
	else if (code == '\b')
		buf_adds(output, "\\b");
	else if (code == '\n')
		buf_adds(output, "\\n");
	else if (code == '\r')
		buf_adds(output, "\\r");
	else if (code == '\f')
		buf_adds(output, "\\f");
	else if (code == '\'')
		buf_adds(output, "\\'");
	else if (code == '"')
		buf_adds(output, "\\\"");
	else if (code == '\\')
		buf_adds(output, "\\\\");
	else if (code >= 0x20 && code <= 0x7f)
		buf_addc(output, (char)code);
	else {
		static const char hex[] = "0123456789abcdef";
		char u[6] = {'\\', 'u', hex[(code >> 12) & 15], hex[(code >> 8) & 15], hex[(code >> 4) & 15],
			     hex[code & 15]};
		buf_add(output, u, 6);
	}
}

const LpField *jv_map_key_field(const LpField *descriptor)
{
	if (descriptor->type != PT_MESSAGE || !descriptor->message_type || !jv_is_map_entry(descriptor->message_type) ||
	    descriptor->message_type->n_fields < 2)
		fail("Check failed: map entry");
	return &descriptor->message_type->fields[0];
}

const LpField *jv_map_value_field(const LpField *descriptor)
{
	if (descriptor->type != PT_MESSAGE || !descriptor->message_type || !jv_is_map_entry(descriptor->message_type) ||
	    descriptor->message_type->n_fields < 2)
		fail("Check failed: map entry");
	return &descriptor->message_type->fields[1];
}

const char *jv_jvm_synthetic(bool jvm_dsl) { return jvm_dsl ? "@kotlin.jvm.JvmSynthetic\n" : ""; }

/* JvmName: `name` is a format whose variables are substituted. */
void jv_jvm_name(const char *name, const JvJvmNameContext *context)
{
	if (context->lite && !context->options->jvm_dsl)
		return;
	lp_emit(context->printer, NULL, 0, "@kotlin.jvm.JvmName(\"");
	lp_emit(context->printer, NULL, 0, name);
	lp_emit(context->printer, NULL, 0, "\")\n");
}

/* ValidateNestInFileClassFeatureHelper */
static const char *validate_nest_in_file_class(const ProtoDesc *d, const LpMessage *containing_type)
{
	if (containing_type && jv_unresolved_has_java_feature(d, JV_NEST_IN_FILE_CLASS))
		return xcat("Feature pb.java.nest_in_file_class only applies to top-level types "
			    "and is not allowed to be set on the nested type: ",
			    d->full_name);
	return NULL;
}

const char *jv_validate_nest_in_file_class_message(const LpMessage *descriptor)
{
	return validate_nest_in_file_class((const ProtoDesc *)descriptor, descriptor->containing_type);
}

const char *jv_validate_nest_in_file_class_enum(const LpEnum *descriptor)
{
	return validate_nest_in_file_class((const ProtoDesc *)descriptor, descriptor->containing_type);
}

bool jv_nested_in_file_class_message(const LpMessage *d, bool immutable)
{
	(void)immutable;
	const char *err = jv_validate_nest_in_file_class_message(d);
	if (err)
		fail("Check failed: %s", err);
	return jv_nested_in_file_class_impl_message(d);
}

bool jv_nested_in_file_class_enum(const LpEnum *d, bool immutable)
{
	(void)immutable;
	const char *err = jv_validate_nest_in_file_class_enum(d);
	if (err)
		fail("Check failed: %s", err);
	return jv_nested_in_file_class_impl_enum(d);
}

bool jv_nested_in_file_class_service(const LpService *d, bool immutable)
{
	(void)immutable;
	return jv_nested_in_file_class_impl_service(d);
}

bool jv_is_own_file_message(const LpMessage *d, bool immutable)
{
	return d->containing_type == NULL && !jv_nested_in_file_class_message(d, immutable);
}

bool jv_is_own_file_enum(const LpEnum *d, bool immutable)
{
	return d->containing_type == NULL && !jv_nested_in_file_class_enum(d, immutable);
}

bool jv_is_own_file_service(const LpService *d, bool immutable) { return !jv_nested_in_file_class_service(d, immutable); }

char *jv_annotation_file_name(const char *name, const char *suffix) { return xcat(name, suffix, ".java.pb.meta"); }

/* ---- internal_helpers.cc ---- */

bool jv_check_utf8(const LpField *descriptor)
{
	if (jv_resolved_java_feature((const ProtoDesc *)descriptor, JV_UTF8_VALIDATION) == JV_UTF8_VERIFY)
		return true;
	return proto_feature(descriptor->merged_features, PROTO_FEATURE_UTF8_VALIDATION) == PROTO_UTF8_VERIFY ||
	       jv_opt_bool((const ProtoDesc *)descriptor->file, JV_FILEOPT_JAVA_STRING_CHECK_UTF8);
}

bool jv_check_large_enum(const LpEnum *descriptor)
{
	return jv_resolved_java_feature((const ProtoDesc *)descriptor, JV_LARGE_ENUM) != 0;
}

static int experimental_java_field_type_for_singular(const LpField *field)
{
	int result = field->type;
	if (result == PT_GROUP)
		return 17;
	if (result < PT_GROUP)
		return result - 1;
	return result - 2;
}

static int experimental_java_field_type_for_repeated(const LpField *field)
{
	if (field->type == PT_GROUP)
		return 49;
	return experimental_java_field_type_for_singular(field) + 18;
}

static int experimental_java_field_type_for_packed(const LpField *field)
{
	int result = field->type;
	if (result < PT_STRING)
		return result + 34;
	if (result > PT_BYTES)
		return result + 30;
	fail("%s can't be packed.", field->full_name);
}

int jv_get_experimental_java_field_type(const LpField *field)
{
	enum {
		kMapFieldType = 50, kOneofFieldTypeOffset = 51, kRequiredBit = 0x100, kUtf8CheckBit = 0x200,
		kCheckInitialized = 0x400, kLegacyEnumIsClosedBit = 0x800, kHasHasBit = 0x1000,
	};
	int extra_bits = proto_field_is_required(field) ? kRequiredBit : 0;
	if (field->type == PT_STRING && jv_check_utf8(field))
		extra_bits |= kUtf8CheckBit;
	if (proto_field_is_required(field) ||
	    (jv_get_java_type(field) == JAVATYPE_MESSAGE && jv_has_required_fields(field->message_type)))
		extra_bits |= kCheckInitialized;
	if (jv_has_hasbit(field))
		extra_bits |= kHasHasBit;
	if (jv_get_java_type(field) == JAVATYPE_ENUM && !jv_support_unknown_enum_value(field))
		extra_bits |= kLegacyEnumIsClosedBit;

	if (proto_field_is_map(field)) {
		if (!jv_support_unknown_enum_value(jv_map_value_field(field))) {
			const LpField *value = &field->message_type->fields[1];
			if (jv_get_java_type(value) == JAVATYPE_ENUM)
				extra_bits |= kLegacyEnumIsClosedBit;
		}
		return kMapFieldType | extra_bits;
	}
	if (proto_field_is_packed(field))
		return experimental_java_field_type_for_packed(field) | extra_bits;
	if (proto_field_is_repeated(field))
		return experimental_java_field_type_for_repeated(field) | extra_bits;
	if (jv_is_real_oneof(field))
		return (experimental_java_field_type_for_singular(field) + kOneofFieldTypeOffset) | extra_bits;
	return experimental_java_field_type_for_singular(field) | extra_bits;
}
