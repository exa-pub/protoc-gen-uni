// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/helpers.{h,cc} (protobuf v36.2), with the
 * internal::cpp helpers of descriptor.cc the generator uses (cpp_string_type, the UTF-8 check
 * mode, hasbit modes, closed enums). The names of compiler/cpp/names.h are libprotoc's
 * (cpp_names.h). In the open-source build lazy fields, split messages, inlined strings,
 * profiles and verification are disabled; their predicates are kept as upstream has them. */

/* ---- Subs ---- */

static void subs_push(Subs *s, LpSub sub)
{
	if (s->n == s->cap) {
		int cap = s->cap ? 2 * s->cap : 16;
		s->v = xrealloc(s->v, sizeof(LpSub) * (size_t)s->cap, sizeof(LpSub) * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = sub;
}

static int subs_find(const Subs *s, const char *key)
{
	for (int i = 0; i < s->n; i++)
		if (str_eq(s->v[i].key, key))
			return i;
	return -1;
}

static void subs_set(Subs *s, const char *key, const char *value)
{
	int i = subs_find(s, key);
	if (i >= 0)
		s->v[i] = LP_SUB(key, value);
	else
		subs_push(s, LP_SUB(key, value));
}

static void subs_insert(Subs *s, const char *key, const char *value)
{
	if (subs_find(s, key) < 0)
		subs_push(s, LP_SUB(key, value));
}

static const char *subs_get(const Subs *s, const char *key)
{
	int i = subs_find(s, key);
	return i >= 0 ? s->v[i].value : NULL;
}

static Subs subs_copy(const Subs *s)
{
	Subs c = {0};
	subs_append(&c, s);
	return c;
}

static void subs_append(Subs *dst, const Subs *src)
{
	for (int i = 0; i < src->n; i++)
		subs_push(dst, src->v[i]);
}

static bool ascii_isalnum(unsigned char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static char ascii_tolower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static char ascii_toupper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

/* ---- descriptor.h accessors ---- */

static char *cpp_itoa(long long v) { return xfmt("%lld", v); }
static char *cpp_utoa(unsigned long long v)
{
	char t[24];
	int i = 23;
	t[i] = 0;
	do {
		t[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	return xstrdup(t + i);
}

static int cpp_type(const LpField *f) { return lp_field_cpp_type(f); }
static bool is_repeated(const LpField *f) { return proto_field_is_repeated(f); }
static bool is_map(const LpField *f) { return proto_field_is_map(f); }
static const LpOneof *real_oneof(const LpField *f) { return proto_field_real_oneof(f); }
static const LpField *map_key(const LpMessage *m) { return proto_message_field_by_number(m, 1); }
static const LpField *map_value(const LpMessage *m) { return proto_message_field_by_number(m, 2); }
static bool msg_option(const LpMessage *m, int num) { return pb_int(m->options, (uint32_t)num, 0) != 0; }
static bool field_option(const LpField *f, int num) { return pb_int(f->options, (uint32_t)num, 0) != 0; }
static bool is_weak_option(const LpField *f) { return field_option(f, CPP_FOPT_WEAK); }

/* the pb.cpp feature of the resolved (merged) features */
static int cpp_feature(const LpField *f, int num)
{
	return (int)pb_int(pb_msg(f->merged_features, PB_CPP), (uint32_t)num, 0);
}

/* FieldDescriptor::legacy_enum_field_treated_as_closed */
static bool legacy_enum_field_treated_as_closed(const LpField *f)
{
	return f->type == PT_ENUM && (cpp_feature(f, CPPF_LEGACY_CLOSED_ENUM) || proto_enum_is_closed(f->enum_type));
}

/* internal::cpp::HasPreservingUnknownEnumSemantics */
static bool has_preserving_unknown_enum_semantics(const LpField *f)
{
	if (legacy_enum_field_treated_as_closed(f))
		return false;
	return f->enum_type && !proto_enum_is_closed(f->enum_type);
}

/* internal::cpp::IsStringFieldWithPrivatizedAccessors */
static bool is_string_field_with_privatized_accessors(const LpField *f)
{
	return cpp_type(f) == LP_CPPTYPE_STRING && cpp_feature(f, CPPF_STRING_TYPE) == CPP_STRING_TYPE_CORD &&
	       (f->type != PT_BYTES || is_repeated(f) || f->is_extension);
}

/* FieldDescriptor::cpp_string_type */
static CppStringType cpp_string_type(const LpField *f)
{
	if (is_string_field_with_privatized_accessors(f))
		return CPP_STR_STRING;
	switch (cpp_feature(f, CPPF_STRING_TYPE)) {
	case CPP_STRING_TYPE_VIEW:
		return CPP_STR_VIEW;
	case CPP_STRING_TYPE_CORD:
		return CPP_STR_CORD;
	default:
		return CPP_STR_STRING;
	}
}

/* FieldDescriptor::CalculateCppRepeatedType */
static CppRepeatedType calculate_cpp_repeated_type(const LpField *f)
{
	return cpp_feature(f, CPPF_REPEATED_TYPE) == 2 ? CPP_REP_PROXY : CPP_REP_REPEATED;
}

/* internal::cpp::GetFieldHasbitModeWithoutProfile */
static HasbitMode get_field_hasbit_mode_without_profile(const LpField *f)
{
	if (real_oneof(f) || is_weak_option(f) || f->is_extension)
		return HASBIT_NONE;
	if (proto_field_has_presence(f))
		return HASBIT_TRUE;
	return HASBIT_HINT;
}

/* internal::cpp::GetUtf8CheckMode */
static Utf8CheckMode get_utf8_check_mode(const LpField *f, bool is_lite)
{
	if (f->type == PT_STRING ||
	    (is_map(f) && (map_key(f->message_type)->type == PT_STRING || map_value(f->message_type)->type == PT_STRING))) {
		if (proto_feature(f->merged_features, PROTO_FEATURE_UTF8_VALIDATION) == PROTO_UTF8_VERIFY)
			return UTF8_STRICT;
	}
	return UTF8_NONE;
}

/* internal::cpp::IsLazilyInitializedFile */
static bool is_lazily_initialized_file(const char *filename)
{
	return str_eq(filename, "third_party/protobuf/cpp_features.proto") ||
	       str_eq(filename, "google/protobuf/cpp_features.proto") ||
	       str_eq(filename, "third_party/protobuf/internal_options.proto") ||
	       str_eq(filename, "google/protobuf/internal_options.proto") ||
	       str_eq(filename, "third_party/protobuf/json_enumvalue_options.proto") ||
	       str_eq(filename, "google/protobuf/json_enumvalue_options.proto") ||
	       str_eq(filename, "third_party/protobuf/cpp_file_options.proto") ||
	       str_eq(filename, "google/protobuf/cpp_file_options.proto") ||
	       str_eq(filename, "net/proto2/proto/descriptor.proto") ||
	       str_eq(filename, "google/protobuf/descriptor.proto");
}

/* internal::IsEnumFullySequential (sequential_value_limit_ of DescriptorBuilder) */
static bool is_enum_fully_sequential(const LpEnum *e)
{
	int limit = -1;
	for (int i = 0; i < 65535 && i < e->n_values &&
			(int64_t)e->values[i].number == (int64_t)i + e->values[0].number;
	     ++i)
		limit = i;
	return limit == e->n_values - 1;
}

/* ---- options ---- */

static const char *protobuf_namespace(const Options *opts)
{
	return opts->opensource_runtime ? "google::protobuf" : "proto2";
}

static const char *deprecated_attribute(const Options *opts, const LpField *d)
{
	return field_option(d, CPP_FOPT_DEPRECATED) ? "[[deprecated]] " : "";
}

static const char *const kThickSeparator = "// ===================================================================\n";
static const char *const kThinSeparator = "// -------------------------------------------------------------------\n";

/* ---- simple predicates (always false or fixed in the open-source build) ---- */

static bool is_eagerly_verified_lazy(const LpField *f, const Options *o) { return false; }
static bool is_lazily_verified_lazy(const LpField *f, const Options *o) { return false; }
static bool is_lazy(const LpField *f, const Options *o)
{
	return is_lazily_verified_lazy(f, o) || is_eagerly_verified_lazy(f, o);
}
static uint16_t get_lazy_style(const LpField *f, const Options *o) { return 0; }
static bool is_rarely_present(const LpField *f, const Options *o) { return false; }
static bool is_likely_present(const LpField *f, const Options *o) { return false; }
static bool is_string_inlined(const LpField *f, const Options *o) { return false; }
static bool should_verify_message(const LpMessage *d, const Options *o) { return false; }
static bool should_verify_file(const LpFile *f, const Options *o) { return false; }
static bool should_split_message(const LpMessage *d, const Options *o) { return false; }
static bool should_split(const LpField *f, const Options *o) { return false; }
static bool is_present_message(const LpMessage *d, const Options *o) { return true; }
static const LpField *find_hottest_field(const LpField *const *fields, int n, const Options *o) { return NULL; }
static bool should_generate_extern_specializations(const Options *o) { return !o->opensource_runtime; }

static bool is_profile_driven(const Options *o) { return false; /* access_info_map is never set */ }

static bool is_string_inlining_enabled(const Options *o) { return o->force_inline_string || is_profile_driven(o); }

static bool is_explicit_lazy(const LpField *f)
{
	if (is_map(f) || is_repeated(f))
		return false;
	if (cpp_type(f) != LP_CPPTYPE_MESSAGE)
		return false;
	return field_option(f, CPP_FOPT_LAZY) || field_option(f, CPP_FOPT_UNVERIFIED_LAZY);
}

/* IsWeak: [weak = true] fails a check in the open-source runtime. */
static bool is_weak(const LpField *f, const Options *o)
{
	if (is_weak_option(f)) {
		if (o->opensource_runtime)
			fail("unsupported by protoc-gen-uni: the field option weak (field %s; upstream fails a check)",
			     f->full_name);
		return true;
	}
	return false;
}

static bool is_cord(const LpField *f)
{
	return cpp_type(f) == LP_CPPTYPE_STRING && cpp_string_type(f) == CPP_STR_CORD;
}

static bool is_string(const LpField *f)
{
	return cpp_type(f) == LP_CPPTYPE_STRING &&
	       (cpp_string_type(f) == CPP_STR_STRING || cpp_string_type(f) == CPP_STR_VIEW);
}

static bool is_micro_string(const LpField *f, const Options *o)
{
	if (is_repeated(f) || f->is_extension || cpp_type(f) != LP_CPPTYPE_STRING ||
	    cpp_string_type(f) != CPP_STR_VIEW)
		return false;
	if (msg_option(f->containing_type, CPP_MSGOPT_MAP_ENTRY))
		return false;
	return o->experimental_use_micro_string;
}

static bool is_arena_string_ptr(const LpField *f, const Options *o)
{
	if (is_micro_string(f, o))
		return false;
	return cpp_string_type(f) == CPP_STR_STRING || cpp_string_type(f) == CPP_STR_VIEW;
}

static HasbitMode get_field_hasbit_mode(const LpField *f, const Options *o)
{
	if (is_profile_driven(o) && is_repeated(f) && is_likely_present(f, o))
		return HASBIT_NONE;
	return get_field_hasbit_mode_without_profile(f);
}

static bool has_hasbit(const LpField *f, const Options *o) { return get_field_hasbit_mode(f, o) != HASBIT_NONE; }

static bool is_map_entry_message(const LpMessage *d) { return msg_option(d, CPP_MSGOPT_MAP_ENTRY); }

static bool is_any_message_file(const LpFile *f) { return str_eq(f->name, "google/protobuf/any.proto"); }
static bool is_any_message(const LpMessage *d) { return str_eq(d->name, "Any") && is_any_message_file(d->file); }

static bool is_well_known_message(const LpFile *file)
{
	static const char *const kFiles[] = {
		"google/protobuf/any.proto", "google/protobuf/api.proto", "google/protobuf/compiler/plugin.proto",
		"google/protobuf/descriptor.proto", "google/protobuf/duration.proto", "google/protobuf/empty.proto",
		"google/protobuf/field_mask.proto", "google/protobuf/source_context.proto",
		"google/protobuf/struct.proto", "google/protobuf/timestamp.proto", "google/protobuf/type.proto",
		"google/protobuf/wrappers.proto",
	};
	for (size_t i = 0; i < sizeof kFiles / sizeof *kFiles; i++)
		if (str_eq(file->name, kFiles[i]))
			return true;
	return false;
}

static bool is_cross_file_message(const LpField *f)
{
	return cpp_type(f) == LP_CPPTYPE_MESSAGE && f->message_type->file != f->file;
}

/* ---- names ---- */

static char *class_name(const LpMessage *d) { return lp_cpp_class_name(d); }
static char *enum_class_name(const LpEnum *d) { return lp_cpp_enum_class_name(d); }
static char *qualified_class_name(const LpMessage *d, const Options *o) { return lp_cpp_qualified_class_name(d); }
static char *qualified_enum_class_name(const LpEnum *d, const Options *o) { return lp_cpp_qualified_enum_class_name(d); }
static char *field_name(const LpField *f) { return lp_cpp_field_name(f); }
static char *namespace_of_file(const LpFile *f) { return lp_cpp_namespace(f); }
static char *underscores_to_camel_case(const char *s, bool cap) { return lp_cpp_underscores_to_camel_case(s, cap); }

static char *field_member_name(const LpField *f, bool split)
{
	const char *split_prefix = split ? "_split_->" : "";
	if (!real_oneof(f))
		return xcat("_impl_.", split_prefix, field_name(f), "_");
	return xcat("_impl_.", f->containing_oneof->name, "_.", field_name(f), "_");
}

static char *make_default_name(const LpField *f)
{
	return xcat("_i_give_permission_to_break_this_code_default_", field_name(f), "_");
}
static char *make_default_field_name(const LpField *f) { return xcat("Impl_::", make_default_name(f)); }
static char *make_varint_cached_size_name(const LpField *f) { return xcat("_", field_name(f), "_cached_byte_size_"); }
static char *make_varint_cached_size_field_name(const LpField *f, bool split)
{
	return xcat("_impl_.", split ? "_split_->" : "", "_", field_name(f), "_cached_byte_size_");
}

static char *filename_identifier(const char *filename)
{
	Buf b = {0};
	for (const char *c = filename; *c; c++) {
		if (ascii_isalnum((unsigned char)*c))
			buf_addc(&b, *c);
		else
			buf_fmt(&b, "_%x", (unsigned)(uint8_t)*c);
	}
	return xstrdup(buf_str(&b));
}

static char *unique_name(const char *name, const char *filename, const Options *o)
{
	return xcat(name, "_", filename_identifier(filename));
}

static char *unique_name_file(const char *name, const LpFile *f, const Options *o) { return unique_name(name, f->name, o); }

static char *escape_trigraphs(const char *s) { return str_replace_all(s, "?", "\\?"); }

static char *qualified_file_level_symbol(const LpFile *file, const char *name, const Options *o)
{
	return lp_cpp_qualified_file_level_symbol(file, name);
}

static char *msg_globals_instance_type(const LpMessage *d, const Options *o)
{
	return xcat(class_name(d), "GlobalsTypeInternal");
}
static char *msg_globals_instance_name(const LpMessage *d, const Options *o) { return xcat(class_name(d), "_globals_"); }
static char *msg_globals_instance_ptr(const LpMessage *d, const Options *o)
{
	return xcat(msg_globals_instance_name(d, o), "ptr_");
}
static char *qualified_msg_globals_instance_name(const LpMessage *d, const Options *o)
{
	return qualified_file_level_symbol(d->file, msg_globals_instance_name(d, o), o);
}
static char *qualified_msg_globals_instance_ptr(const LpMessage *d, const Options *o)
{
	return xcat(qualified_msg_globals_instance_name(d, o), "ptr_");
}

static bool is_bootstrap_proto(const Options *o, const LpFile *file);
static int get_optimize_for(const LpFile *file, const Options *o);

static bool has_descriptor_methods(const LpFile *file, const Options *o)
{
	return get_optimize_for(file, o) != OPT_LITE_RUNTIME;
}
static bool has_generated_methods(const LpFile *file, const Options *o)
{
	return get_optimize_for(file, o) != OPT_CODE_SIZE;
}
static bool use_unknown_field_set(const LpFile *file, const Options *o)
{
	return get_optimize_for(file, o) != OPT_LITE_RUNTIME;
}
static bool has_generic_services(const LpFile *file, const Options *o)
{
	return file->n_services > 0 && get_optimize_for(file, o) != OPT_LITE_RUNTIME &&
	       pb_int(file->options, CPP_FILEOPT_CC_GENERIC_SERVICES, 0);
}

static char *class_data_type(const LpMessage *d, const Options *o)
{
	return has_descriptor_methods(d->file, o) || is_bootstrap_proto(o, d->file) ? "ClassDataFull" : "ClassDataLite";
}

static char *descriptor_table_name(const LpFile *file, const Options *o)
{
	return unique_name_file("descriptor_table", file, o);
}

static char *file_dll_export(const LpFile *file, const Options *o)
{
	return unique_name_file("PROTOBUF_INTERNAL_EXPORT", file, o);
}

static int estimate_alignment_size(const LpField *f)
{
	if (!f)
		return 0;
	if (is_repeated(f))
		return 8;
	switch (cpp_type(f)) {
	case LP_CPPTYPE_BOOL:
		return 1;
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_UINT32:
	case LP_CPPTYPE_ENUM:
	case LP_CPPTYPE_FLOAT:
		return 4;
	default:
		return 8;
	}
}

static char *field_constant_name(const LpField *f)
{
	char *result = xcat("k", underscores_to_camel_case(f->name, true), "FieldNumber");
	if (!f->is_extension && proto_message_field_by_camelcase_name(f->containing_type, lp_field_camelcase_name(f)) != f)
		result = xcat(result, "_", cpp_itoa(f->number));
	return result;
}

static char *field_message_type_name(const LpField *f, const Options *o)
{
	return qualified_class_name(f->message_type, o);
}

static const char *primitive_type_name(const Options *o, int type)
{
	switch (type) {
	case LP_CPPTYPE_INT32:
		return "::int32_t";
	case LP_CPPTYPE_INT64:
		return "::int64_t";
	case LP_CPPTYPE_UINT32:
		return "::uint32_t";
	case LP_CPPTYPE_UINT64:
		return "::uint64_t";
	case LP_CPPTYPE_DOUBLE:
		return "double";
	case LP_CPPTYPE_FLOAT:
		return "float";
	case LP_CPPTYPE_BOOL:
		return "bool";
	case LP_CPPTYPE_ENUM:
		return "int";
	case LP_CPPTYPE_STRING:
		return "::std::string";
	default:
		return "";
	}
}

static const char *declared_type_method_name(int type)
{
	static const char *const kNames[] = {
		"", "Double", "Float", "Int64", "UInt64", "Int32", "Fixed64", "Fixed32", "Bool", "String",
		"Group", "Message", "Bytes", "UInt32", "Enum", "SFixed32", "SFixed64", "SInt32", "SInt64",
	};
	return type >= 1 && type <= 18 ? kNames[type] : "";
}

static char *int32_to_string(int32_t number)
{
	if (number == INT32_MIN)
		return xcat(cpp_itoa((long long)number + 1), " - 1");
	return cpp_itoa(number);
}

static char *int64_to_string(int64_t number)
{
	if (number == INT64_MIN)
		return xcat("::int64_t{", cpp_itoa(number + 1), "} - 1");
	return xcat("::int64_t{", cpp_itoa(number), "}");
}

static char *default_value(const Options *o, const LpField *f)
{
	switch (cpp_type(f)) {
	case LP_CPPTYPE_INT32:
		return int32_to_string((int32_t)f->default_value.i);
	case LP_CPPTYPE_UINT32:
		return xcat(cpp_utoa((uint32_t)f->default_value.u), "u");
	case LP_CPPTYPE_INT64:
		return int64_to_string(f->default_value.i);
	case LP_CPPTYPE_UINT64:
		return xcat("::uint64_t{", cpp_utoa(f->default_value.u), "u}");
	case LP_CPPTYPE_DOUBLE: {
		double value = f->default_value.d;
		if (value == INFINITY)
			return "::std::numeric_limits<double>::infinity()";
		if (value == -INFINITY)
			return "-::std::numeric_limits<double>::infinity()";
		if (value != value)
			return "::std::numeric_limits<double>::quiet_NaN()";
		return lp_simple_dtoa(value);
	}
	case LP_CPPTYPE_FLOAT: {
		float value = f->default_value.f;
		if (value == INFINITY)
			return "::std::numeric_limits<float>::infinity()";
		if (value == -INFINITY)
			return "-::std::numeric_limits<float>::infinity()";
		if (value != value)
			return "::std::numeric_limits<float>::quiet_NaN()";
		char *float_value = lp_simple_ftoa(value);
		if (strpbrk(float_value, ".eE"))
			float_value = xcat(float_value, "f");
		return float_value;
	}
	case LP_CPPTYPE_BOOL:
		return f->default_value.b ? "true" : "false";
	case LP_CPPTYPE_ENUM: {
		const LpEnumValue *dv = f->default_value_enum ? f->default_value_enum : &f->enum_type->values[0];
		return xcat("static_cast< ", qualified_enum_class_name(f->enum_type, o), " >(", int32_to_string(dv->number), ")");
	}
	case LP_CPPTYPE_STRING:
		return xcat("\"", escape_trigraphs(lp_cescape(f->default_value_string)), "\"");
	case LP_CPPTYPE_MESSAGE:
		return xcat("*", field_message_type_name(f, o), "::internal_default_instance()");
	}
	return "";
}

/* the default enum value (default_value_enum()) */
static const LpEnumValue *default_value_enum(const LpField *f)
{
	return f->default_value_enum ? f->default_value_enum : &f->enum_type->values[0];
}

static char *safe_function_name(const LpMessage *d, const LpField *field, const char *prefix)
{
	char *name = xstrdup(field->name);
	for (char *c = name; *c; c++)
		*c = ascii_tolower(*c);
	char *function_name = xcat(prefix, name);
	if (proto_message_field_by_name(d, function_name))
		function_name = xcat(function_name, "__");
	else if (lp_cpp_is_keyword(name))
		function_name = xcat(function_name, "_");
	return function_name;
}

/* ---- CanInitializeByZeroing & co. ---- */

static bool zero_default(const LpField *f)
{
	switch (cpp_type(f)) {
	case LP_CPPTYPE_ENUM:
		return default_value_enum(f)->number == 0;
	case LP_CPPTYPE_INT32:
		return (int32_t)f->default_value.i == 0;
	case LP_CPPTYPE_INT64:
		return f->default_value.i == 0;
	case LP_CPPTYPE_UINT32:
		return (uint32_t)f->default_value.u == 0;
	case LP_CPPTYPE_UINT64:
		return f->default_value.u == 0;
	case LP_CPPTYPE_FLOAT:
		return f->default_value.f == 0;
	case LP_CPPTYPE_DOUBLE:
		return f->default_value.d == 0;
	case LP_CPPTYPE_BOOL:
		return !f->default_value.b;
	default:
		return false;
	}
}

static bool can_initialize_by_zeroing(const LpField *f, const Options *o)
{
	if (is_repeated(f) || f->is_extension)
		return false;
	if (cpp_type(f) == LP_CPPTYPE_MESSAGE)
		return true;
	return zero_default(f);
}

static bool can_clear_by_zeroing(const LpField *f)
{
	if (is_repeated(f) || f->is_extension)
		return false;
	return zero_default(f);
}

static bool has_trivial_swap(const LpField *f, const Options *o)
{
	if (is_repeated(f) || f->is_extension)
		return false;
	switch (cpp_type(f)) {
	case LP_CPPTYPE_STRING:
		return false;
	case LP_CPPTYPE_MESSAGE:
		return !is_lazy(f, o);
	default:
		return true;
	}
}


static bool is_string_or_message(const LpField *f)
{
	return cpp_type(f) == LP_CPPTYPE_STRING || cpp_type(f) == LP_CPPTYPE_MESSAGE;
}


/* ---- per-file predicates ---- */

typedef bool (*FieldPred)(const LpField *f, const Options *o);

static bool any_field_in_message(const LpMessage *d, FieldPred pred, const Options *o)
{
	for (int i = 0; i < d->n_fields; i++)
		if (pred(&d->fields[i], o))
			return true;
	for (int i = 0; i < d->n_nested; i++)
		if (any_field_in_message(&d->nested[i], pred, o))
			return true;
	return false;
}

static bool any_field_in_file(const LpFile *file, FieldPred pred, const Options *o)
{
	for (int i = 0; i < file->n_messages; i++)
		if (any_field_in_message(&file->messages[i], pred, o))
			return true;
	return false;
}

static bool pred_repeated_kind(const LpField *f, CppRepeatedType t)
{
	return is_repeated(f) && !is_map(f) && calculate_cpp_repeated_type(f) == t;
}
static bool pred_repeated_legacy(const LpField *f, const Options *o) { return pred_repeated_kind(f, CPP_REP_REPEATED); }
static bool pred_repeated_proxy(const LpField *f, const Options *o) { return pred_repeated_kind(f, CPP_REP_PROXY); }
static bool pred_cord(const LpField *f, const Options *o) { return is_cord(f); }
static bool pred_map(const LpField *f, const Options *o) { return is_map(f); }

static bool has_repeated_fields(const LpFile *file, CppRepeatedType t)
{
	return any_field_in_file(file, t == CPP_REP_PROXY ? pred_repeated_proxy : pred_repeated_legacy, NULL);
}
static bool has_cord_fields(const LpFile *file, const Options *o) { return any_field_in_file(file, pred_cord, o); }
static bool has_map_fields(const LpFile *file) { return any_field_in_file(file, pred_map, NULL); }

static bool has_weak_fields_message(const LpMessage *d, const Options *o)
{
	for (int i = 0; i < d->n_fields; i++)
		if (is_weak(&d->fields[i], o))
			return true;
	return false;
}

static bool has_weak_fields_file(const LpFile *file, const Options *o)
{
	for (int i = 0; i < file->n_messages; i++)
		if (has_weak_fields_message(&file->messages[i], o))
			return true;
	return false;
}

static bool has_lazy_fields_message(const LpMessage *d, const Options *o)
{
	for (int i = 0; i < d->n_fields; i++)
		if (is_lazy(&d->fields[i], o))
			return true;
	for (int i = 0; i < d->n_extensions; i++)
		if (is_lazy(&d->extensions[i], o))
			return true;
	for (int i = 0; i < d->n_nested; i++)
		if (has_lazy_fields_message(&d->nested[i], o))
			return true;
	return false;
}

static bool has_lazy_fields(const LpFile *file, const Options *o)
{
	for (int i = 0; i < file->n_messages; i++)
		if (has_lazy_fields_message(&file->messages[i], o))
			return true;
	for (int i = 0; i < file->n_extensions; i++)
		if (is_lazy(&file->extensions[i], o))
			return true;
	return false;
}



static bool has_enum_definitions_m(const LpMessage *d)
{
	if (d->n_enums > 0)
		return true;
	for (int i = 0; i < d->n_nested; i++)
		if (has_enum_definitions_m(&d->nested[i]))
			return true;
	return false;
}

static bool has_enum_definitions(const LpFile *file)
{
	if (file->n_enums > 0)
		return true;
	for (int i = 0; i < file->n_messages; i++)
		if (has_enum_definitions_m(&file->messages[i]))
			return true;
	return false;
}

/* FlattenMessagesInFile: VisitDescriptorsInFileOrder (nested messages before their parent) */
static void flatten_message(const LpMessage *d, Vec *out)
{
	for (int i = 0; i < d->n_nested; i++)
		flatten_message(&d->nested[i], out);
	vec_push(out, (void *)d);
}

static Vec flatten_messages_in_file(const LpFile *file)
{
	Vec out = {0};
	for (int i = 0; i < file->n_messages; i++)
		flatten_message(&file->messages[i], &out);
	return out;
}

/* ListAllFields */
static void list_all_fields_message(const LpMessage *d, Vec *fields)
{
	for (int i = 0; i < d->n_nested; i++)
		list_all_fields_message(&d->nested[i], fields);
	for (int i = 0; i < d->n_extensions; i++)
		vec_push(fields, &d->extensions[i]);
	for (int i = 0; i < d->n_fields; i++)
		vec_push(fields, &d->fields[i]);
}

static void list_all_fields_file(const LpFile *file, Vec *fields)
{
	for (int i = 0; i < file->n_messages; i++)
		list_all_fields_message(&file->messages[i], fields);
	for (int i = 0; i < file->n_extensions; i++)
		vec_push(fields, &file->extensions[i]);
}

static void list_all_types_for_services(const LpFile *fd, Vec *types)
{
	for (int i = 0; i < fd->n_services; i++) {
		const LpService *sd = &fd->services[i];
		for (int j = 0; j < sd->n_methods; j++) {
			vec_push(types, sd->methods[j].input_type);
			vec_push(types, sd->methods[j].output_type);
		}
	}
}

/* ---- bootstrap ---- */

static bool get_bootstrap_basename(const Options *o, const char *basename, char **bootstrap_basename)
{
	if (o->opensource_runtime)
		return false;
	*bootstrap_basename = xstrdup(basename);
	return false;
}

static bool is_bootstrap_proto(const Options *o, const LpFile *file)
{
	char *my_name = lp_strip_proto(file->name);
	return get_bootstrap_basename(o, my_name, &my_name);
}

/* HasExtensionFromFile (has_opt_codesize_extension is never read: NeedsEagerDescriptorAssignment
 * is unused) */
static bool has_extension_from_file(const PbMsg *msg, const LpFile *file, int depth)
{
	if (depth > 200)
		return false;
	for (int i = 0; i < msg->def->n; i++) {
		const PbSlot *s = &msg->slots[i];
		if (!s->n)
			continue;
		const LpField *field = lp_msg_slot_field(msg, i);
		if (!field->message_type)
			continue;
		if (field->is_extension && field->message_type->file == file)
			return true;
		if (msg->def->f[i].type != PT_MESSAGE && msg->def->f[i].type != PT_GROUP)
			continue;
		for (int j = 0; j < s->n; j++)
			if (has_extension_from_file(s->v[j].m, file, depth + 1))
				return true;
	}
	return false;
}

static bool has_bootstrap_problem(const LpFile *file, const Options *o)
{
	CppState *st = o->state;
	const char *cached = map_get(&st->bootstrap_problem, file->name);
	if (cached)
		return cached[0] == '1';
	if (str_eq(file->name, "net/proto2/proto/descriptor.proto") ||
	    str_eq(file->name, "google/protobuf/descriptor.proto"))
		return true;
	if (!proto_pool_find_message(file->pool, "google.protobuf.FileDescriptorProto"))
		return false;
	Buf data = {0};
	lp_msg_serialize(lp_file_to_proto(file), &data);
	PbMsg *fd_proto = lp_dynamic_message_parse(file, "google.protobuf.FileDescriptorProto", buf_view(&data));
	if (!fd_proto)
		fail("Check failed: fd_proto->ParseFromString(linkedin_fd_proto.SerializeAsString())");
	bool res = has_extension_from_file(fd_proto, file, 0);
	map_put(&st->bootstrap_problem, file->name, res ? "1" : "0");
	return res;
}

static int file_optimize_for(const LpFile *file) { return (int)pb_int(file->options, CPP_FILEOPT_OPTIMIZE_FOR, OPT_SPEED); }

static int get_optimize_for(const LpFile *file, const Options *o)
{
	switch (o->enforce_mode) {
	case ENFORCE_SPEED:
		return OPT_SPEED;
	case ENFORCE_LITE:
		return OPT_LITE_RUNTIME;
	case ENFORCE_CODE_SIZE:
		if (file_optimize_for(file) == OPT_LITE_RUNTIME)
			return OPT_LITE_RUNTIME;
		if (has_bootstrap_problem(file, o))
			return OPT_SPEED;
		return OPT_CODE_SIZE;
	case ENFORCE_NONE:
		if (file_optimize_for(file) == OPT_CODE_SIZE && has_bootstrap_problem(file, o))
			return OPT_SPEED;
		return file_optimize_for(file);
	}
	return OPT_SPEED;
}

/* ---- weak fields and descriptors ---- */

static bool using_implicit_weak_descriptor(const LpFile *file, const Options *o)
{
	return has_descriptor_methods(file, o) && !is_bootstrap_proto(o, file) && o->descriptor_implicit_weak_messages &&
	       !o->opensource_runtime;
}

static char *strong_reference_to_type(const LpMessage *desc, const Options *o)
{
	char *name = qualified_msg_globals_instance_name(desc, o);
	return xcat("::", protobuf_namespace(o), "::internal::StrongPointer<decltype(", name, ")*, &", name, ">()");
}



static bool using_implicit_weak_fields(const LpFile *file, const Options *o)
{
	return o->lite_implicit_weak_fields && get_optimize_for(file, o) == OPT_LITE_RUNTIME;
}

static const LpScc *scc_get(MessageSCCAnalyzer *a, const LpMessage *d) { return lp_scc_get(a->analyzer, d); }

static bool is_implicit_weak_field(const LpField *f, const Options *o)
{
	return using_implicit_weak_fields(f->file, o) && f->type == PT_MESSAGE && !proto_field_is_required(f) &&
	       !is_map(f) && !f->is_extension && !is_well_known_message(f->message_type->file) &&
	       !str_eq(f->message_type->file->name, "net/proto2/proto/descriptor.proto") &&
	       scc_get(o->scc_analyzer, f->containing_type) != scc_get(o->scc_analyzer, f->message_type);
}

/* ---- MessageSCCAnalyzer ---- */

static void scc_deps(const LpMessage *desc, Vec *out, void *arg)
{
	for (int i = 0; i < desc->n_fields; i++)
		if (desc->fields[i].message_type)
			vec_push(out, desc->fields[i].message_type);
}

static MessageSCCAnalyzer *message_scc_analyzer_new(const Options *o)
{
	MessageSCCAnalyzer *a = xalloc(sizeof *a);
	a->analyzer = lp_scc_analyzer_new(scc_deps, NULL);
	a->options = o;
	return a;
}

static bool should_ignore_required_field_check(const LpField *f, const Options *o)
{
	return is_lazily_verified_lazy(f, o);
}

/* MessageSCCAnalyzer::GetSCCAnalysis computes the analysis of a SCC from those of its child SCCs. Upstream
 * recurses into each child; the chain of child SCCs is as long as a chain of message fields in untrusted
 * input, so this walks the same DAG in post order with an explicit stack. The results are memoized per SCC
 * and depend only on the SCC, so the order does not change them. */
static MessageAnalysis *scc_analysis_cached(MessageSCCAnalyzer *a, const LpScc *scc)
{
	return map_get(&a->analysis_cache, lp_scc_representative(scc)->full_name);
}

/* Pushes the child SCCs of scc without an analysis yet; returns whether there were any. */
static bool scc_push_pending_children(MessageSCCAnalyzer *a, const LpScc *scc, Vec *stack)
{
	bool pushed = false;
	for (int i = 0; i < scc->n; i++) {
		const LpMessage *descriptor = scc->descriptors[i];
		for (int j = 0; j < descriptor->n_fields; j++) {
			const LpField *field = &descriptor->fields[j];
			if (field->type != PT_GROUP && field->type != PT_MESSAGE)
				continue;
			const LpScc *child = lp_scc_get(a->analyzer, field->message_type);
			if (child != scc && !scc_analysis_cached(a, child)) {
				vec_push(stack, (void *)child);
				pushed = true;
			}
		}
	}
	return pushed;
}

/* The body of GetSCCAnalysis, with the analyses of all child SCCs in the cache. */
static void scc_analyze(MessageSCCAnalyzer *a, const LpScc *scc)
{
	MessageAnalysis result = {0};
	if (using_implicit_weak_fields(lp_scc_representative(scc)->file, a->options))
		result.contains_weak = true;
	for (int i = 0; i < scc->n; i++) {
		const LpMessage *descriptor = scc->descriptors[i];
		if (descriptor->n_extension_ranges > 0)
			result.contains_extension = true;
		for (int j = 0; j < descriptor->n_fields; j++) {
			const LpField *field = &descriptor->fields[j];
			if (proto_field_is_required(field))
				result.contains_required = true;
			if (is_weak_option(field))
				result.contains_weak = true;
			switch (field->type) {
			case PT_STRING:
			case PT_BYTES:
				if (cpp_string_type(field) == CPP_STR_CORD)
					result.contains_cord = true;
				break;
			case PT_GROUP:
			case PT_MESSAGE: {
				const LpScc *child = lp_scc_get(a->analyzer, field->message_type);
				if (child != scc) {
					MessageAnalysis analysis = *scc_analysis_cached(a, child);
					result.contains_cord |= analysis.contains_cord;
					result.contains_extension |= analysis.contains_extension;
					if (!should_ignore_required_field_check(field, a->options))
						result.contains_required |= analysis.contains_required;
					result.contains_weak |= analysis.contains_weak;
				} else {
					result.is_recursive = true;
				}
				break;
			}
			default:
				break;
			}
		}
	}
	MessageAnalysis *stored = xalloc(sizeof *stored);
	*stored = result;
	map_put(&a->analysis_cache, lp_scc_representative(scc)->full_name, stored);
}

static MessageAnalysis get_scc_analysis(MessageSCCAnalyzer *a, const LpScc *scc)
{
	/* Each SCC pushes its children once: while it waits on the stack only its descendants are above it,
	 * and the DAG has no path back to it, so the stack holds at most 1 + (number of edges) entries. */
	Vec stack = {0};
	vec_push(&stack, (void *)scc);
	while (stack.n > 0) {
		const LpScc *top = stack.v[stack.n - 1];
		if (scc_analysis_cached(a, top)) {
			stack.n--;
			continue;
		}
		if (scc_push_pending_children(a, top, &stack))
			continue;
		scc_analyze(a, top);
		stack.n--;
	}
	return *scc_analysis_cached(a, scc);
}

static bool scc_has_required_fields(MessageSCCAnalyzer *a, const LpMessage *d)
{
	MessageAnalysis r = get_scc_analysis(a, scc_get(a, d));
	return r.contains_required || r.contains_extension;
}


/* ---- TopologicalSortMessagesInFile ---- */

typedef struct {
	const LpScc *scc;
	Vec parents;    /* child_to_parent_scc_map[scc] (distinct) */
	Map parent_set;
	bool has_parent_entry;
	int count;      /* scc_to_outgoing_edges_map[scc] */
	bool has_count_entry;
} TopoNode;

static TopoNode *topo_node(Map *nodes, Vec *order, const LpScc *scc)
{
	const char *key = lp_scc_representative(scc)->full_name;
	TopoNode *n = map_get(nodes, key);
	if (!n) {
		n = xalloc(sizeof *n);
		n->scc = scc;
		map_put(nodes, key, n);
		vec_push(order, n);
	}
	return n;
}

static int cmp_topo_desc(const void *a, const void *b)
{
	const TopoNode *x = *(TopoNode *const *)a, *y = *(TopoNode *const *)b;
	return strcmp(lp_scc_representative(x->scc)->full_name, lp_scc_representative(y->scc)->full_name);
}

static Vec topological_sort_messages_in_file(const LpFile *file, MessageSCCAnalyzer *a)
{
	Vec messages_in_file = flatten_messages_in_file(file);
	Vec sorted = {0};
	if (!messages_in_file.n)
		return sorted;
	Map in_file = {0};
	Map nodes = {0};
	Vec all = {0};
	Vec queue = {0};
	for (int i = 0; i < messages_in_file.n; i++) {
		const LpMessage *d = messages_in_file.v[i];
		map_put(&in_file, d->full_name, (void *)d);
		vec_push(&queue, topo_node(&nodes, &all, scc_get(a, d)));
	}
	for (int qi = 0; qi < queue.n; qi++) {
		TopoNode *n = queue.v[qi];
		n->has_count_entry = true;
		for (int c = 0; c < n->scc->n_children; c++) {
			TopoNode *child = topo_node(&nodes, &all, n->scc->children[c]);
			child->has_parent_entry = true;
			if (child->parents.n == 0)
				vec_push(&queue, child);
			const char *pk = lp_scc_representative(n->scc)->full_name;
			if (!map_has(&child->parent_set, pk)) {
				map_put(&child->parent_set, pk, n);
				vec_push(&child->parents, n);
				++n->count;
			}
		}
	}
	Vec next = {0};
	for (int i = 0; i < all.n; i++) {
		TopoNode *n = all.v[i];
		if (n->has_count_entry && n->count == 0)
			vec_push(&next, n);
	}
	while (next.n) {
		Vec current = next;
		next = (Vec){0};
		qsort(current.v, (size_t)current.n, sizeof(void *), cmp_topo_desc);
		while (current.n) {
			TopoNode *n = current.v[--current.n];
			for (int i = 0; i < n->scc->n; i++) {
				const LpMessage *d = n->scc->descriptors[i];
				if (map_get(&in_file, d->full_name) == d)
					vec_push(&sorted, (void *)d);
			}
			for (int i = 0; i < n->parents.n; i++) {
				TopoNode *parent = n->parents.v[i];
				if (--parent->count == 0)
					vec_push(&next, parent);
			}
		}
	}
	return sorted;
}

/* ---- misc ---- */

static bool is_layout_optimized(const LpField *f, const Options *o) { return !real_oneof(f) && !is_weak(f, o); }

/* CollectFieldsExcludingWeakAndOneof */
static int collect_fields_excluding_weak_and_oneof(const LpMessage *d, const Options *o, Vec *fields)
{
	int num_weak_fields = 0;
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *f = &d->fields[i];
		if (is_weak(f, o))
			++num_weak_fields;
		if (is_layout_optimized(f, o))
			vec_push(fields, (void *)f);
	}
	return num_weak_fields;
}

static bool is_file_descriptor_proto(const LpFile *file, const Options *o)
{
	if (!str_eq(namespace_of_file(file), xcat("::", protobuf_namespace(o))))
		return false;
	for (int i = 0; i < file->n_messages; i++)
		if (str_eq(file->messages[i].name, "FileDescriptorProto"))
			return true;
	return false;
}

static bool should_generate_class(const LpMessage *d, const Options *o)
{
	return !is_map_entry_message(d) || has_descriptor_methods(d->file, o);
}

static bool has_tracker(const LpMessage *d, const Options *o)
{
	return o->inject_field_listener_events && pb_int(d->file->options, CPP_FILEOPT_OPTIMIZE_FOR, OPT_SPEED) != OPT_LITE_RUNTIME &&
	       !is_map_entry_message(d);
}

static bool forbidden_event(const Options *o, const char *event)
{
	for (int i = 0; i < o->forbidden_field_listener_events.n; i++)
		if (str_eq(o->forbidden_field_listener_events.v[i], event))
			return true;
	return false;
}

static bool has_on_deserialize_tracker(const LpMessage *d, const Options *o)
{
	return has_tracker(d, o) && !forbidden_event(o, "deserialize");
}

static bool needs_post_loop_handler(const LpMessage *d, const Options *o) { return has_on_deserialize_tracker(d, o); }

static const char *simple_base_class(const LpMessage *d, const Options *o)
{
	if (!has_descriptor_methods(d->file, o))
		return "";
	if (using_implicit_weak_descriptor(d->file, o))
		return "";
	if (d->n_extension_ranges != 0)
		return "";
	if (o->inject_field_listener_events)
		return "";
	if (d->n_fields == 0)
		return "ZeroFieldsBase";
	return "";
}

static bool has_simple_base_class(const LpMessage *d, const Options *o) { return *simple_base_class(d, o) != 0; }

static bool has_simple_base_classes_m(const LpMessage *d, const Options *o)
{
	for (int i = 0; i < d->n_nested; i++)
		if (has_simple_base_classes_m(&d->nested[i], o))
			return true;
	return has_simple_base_class(d, o);
}

static bool has_simple_base_classes(const LpFile *file, const Options *o)
{
	for (int i = 0; i < file->n_messages; i++)
		if (has_simple_base_classes_m(&file->messages[i], o))
			return true;
	return false;
}

static bool has_impl_data(const LpMessage *d, const Options *o) { return !has_simple_base_class(d, o); }


static bool has_message_field_or_extension(const LpMessage *d)
{
	if (d->n_extension_ranges > 0)
		return true;
	for (int i = 0; i < d->n_fields; i++)
		if (cpp_type(&d->fields[i]) == LP_CPPTYPE_MESSAGE)
			return true;
	return false;
}

static bool is_strict_utf8_string(const LpField *f, const Options *o)
{
	if (f->type != PT_STRING)
		return false;
	bool is_lite = get_optimize_for(f->file, o) == OPT_LITE_RUNTIME;
	return get_utf8_check_mode(f, is_lite) == UTF8_STRICT;
}

static void generate_utf8_check_code(LpPrinter *p, const LpField *f, const Options *o, bool for_parse,
				     const char *params, const char *strict_function, const char *verify_function)
{
	if (f->type != PT_STRING)
		return;
	LpSub v[] = {S_("params", params), S_("Strict", strict_function), S_("Verify", verify_function)};
	lp_push_vars(p, v, 3);
	bool is_lite = get_optimize_for(f->file, o) == OPT_LITE_RUNTIME;
	if (get_utf8_check_mode(f, is_lite) == UTF8_STRICT) {
		if (for_parse)
			EMIT0(p, "\n"
				 " DO_($pbi$::WireFormatLite::$Strict$(\n"
				 "     $params$ $pbi$::WireFormatLite::PARSE, \"$pkg.Msg.field$\"));\n");
		else
			EMIT0(p, "\n"
				 " $pbi$::WireFormatLite::$Strict$(\n"
				 "     $params$ $pbi$::WireFormatLite::SERIALIZE, \"$pkg.Msg.field$\");\n");
	}
	POP_VARS(p);
}

static void generate_utf8_check_code_for_string(LpPrinter *p, const LpField *f, const Options *o, bool for_parse,
						const char *params)
{
	generate_utf8_check_code(p, f, o, for_parse, params, "VerifyUtf8String", "VerifyUTF8StringNamedField");
}

static void generate_utf8_check_code_for_cord(LpPrinter *p, const LpField *f, const Options *o, bool for_parse,
					      const char *params)
{
	generate_utf8_check_code(p, f, o, for_parse, params, "VerifyUtf8Cord", "VerifyUTF8CordNamedField");
}

/* AnnotatedAccessors: prefix + "name" -> prefix + FieldName, annotated with the field */
static void annotated_accessors(Subs *out, const LpField *f, const char *const *prefixes, int n, int semantic)
{
	char *name = field_name(f);
	for (int i = 0; i < n; i++)
		subs_push(out, SANNSEM(xcat(prefixes[i], "name"), xcat(prefixes[i], name), f, semantic));
}

/* FieldComment */
static char *field_comment(const LpField *f, const Options *o)
{
	if (o->strip_nonfunctional_codegen)
		return xstrdup(f->name);
	char *line = lp_field_debug_string_first_line(f);
	/* DebugStringOptions::elide_group_body */
	if (!f->is_extension && f->type == PT_GROUP && f->file->edition < LP_EDITION_2023 && has_suffix(line, " {"))
		line = xcat(str_trim_suffix(line, " {"), " { ... };");
	return line;
}

/* PrintFieldComment(format, field, options): format("// $1$\n", comment) */
static void print_field_comment(LpPrinter *p, const Subs *vars, const LpField *f, const Options *o)
{
	const char *args[] = {field_comment(f, o)};
	lp_format_internal(p, args, 1, vars ? vars->v : NULL, vars ? vars->n : 0, "// $1$\n");
}

/* Formatter(p, vars)(format, args...) */
static void format_v(LpPrinter *p, const Subs *vars, const char *format, int n_args, const char *const *args)
{
	lp_format_internal(p, args, n_args, vars ? vars->v : NULL, vars ? vars->n : 0, format);
}
#define FORMAT(p, vars, fmt, ...)                                                                  \
	do {                                                                                       \
		const char *format_args_[] = {"", __VA_ARGS__};                                   \
		format_v((p), (vars), (fmt), (int)(sizeof format_args_ / sizeof *format_args_) - 1,  \
			 format_args_ + 1);                                                        \
	} while (0)

/* ---- NamespaceOpener ---- */

typedef struct {
	LpPrinter *p;
	Vec name_stack;
} NamespaceOpener;

static Vec split_namespace(const char *name)
{
	Vec out = {0};
	const char *s = name;
	for (;;) {
		const char *e = strstr(s, "::");
		size_t n = e ? (size_t)(e - s) : strlen(s);
		if (n)
			vec_push(&out, xstrndup(s, n));
		if (!e)
			break;
		s = e + 2;
	}
	return out;
}

static void ns_change_to(NamespaceOpener *ns, const char *name)
{
	Vec new_stack = split_namespace(name);
	int len = ns->name_stack.n < new_stack.n ? ns->name_stack.n : new_stack.n;
	int common_idx = 0;
	while (common_idx < len && str_eq(ns->name_stack.v[common_idx], new_stack.v[common_idx]))
		++common_idx;
	for (int i = ns->name_stack.n; i > common_idx; i--)
		EMIT(ns->p,
		     "\n"
		     " }  // namespace $ns$\n",
		     S_("ns", ns->name_stack.v[i - 1]));
	for (int i = common_idx; i < new_stack.n; ++i)
		EMIT(ns->p,
		     "\n"
		     " namespace $ns$ {\n",
		     S_("ns", new_stack.v[i]));
	ns->name_stack = new_stack;
}

static NamespaceOpener ns_open(LpPrinter *p, const char *name)
{
	NamespaceOpener ns = {p, {0}};
	ns_change_to(&ns, name);
	return ns;
}

static void ns_close(NamespaceOpener *ns) { ns_change_to(ns, ""); }

/* ---- MessageVars / UnknownFieldsVars ---- */

static void set_common_message_data_variables(const LpMessage *d, Subs *vars)
{
	subs_insert(vars, "any_metadata", "_impl_._any_metadata_");
	subs_insert(vars, "cached_size", "_impl_._cached_size_");
	subs_insert(vars, "extensions", "_impl_._extensions_");
	subs_insert(vars, "has_bits", "_impl_._has_bits_");
	subs_insert(vars, "oneof_case", "_impl_._oneof_case_");
	subs_insert(vars, "tracker", "Impl_::_tracker_");
	subs_insert(vars, "weak_field_map", "_impl_._weak_field_map_");
	subs_insert(vars, "split", "_impl_._split_");
	subs_insert(vars, "cached_split_ptr", "cached_split_ptr");
}

static void set_unknown_fields_variable(const LpMessage *d, const Options *o, Subs *vars)
{
	const char *unknown_fields_type, *default_instance;
	if (use_unknown_field_set(d->file, o)) {
		unknown_fields_type = xcat("::", protobuf_namespace(o), "::UnknownFieldSet");
		default_instance = xcat(unknown_fields_type, "::default_instance");
	} else {
		unknown_fields_type = primitive_type_name(o, LP_CPPTYPE_STRING);
		default_instance = xcat("::", protobuf_namespace(o), "::internal::GetEmptyString");
	}
	subs_insert(vars, "unknown_fields",
		    xcat("_internal_metadata_.unknown_fields<", unknown_fields_type, ">(", default_instance, ")"));
	subs_insert(vars, "unknown_fields_type", unknown_fields_type);
	subs_insert(vars, "have_unknown_fields", "_internal_metadata_.have_unknown_fields()");
	subs_insert(vars, "mutable_unknown_fields",
		    xcat("_internal_metadata_.mutable_unknown_fields<", unknown_fields_type, ">()"));
}
