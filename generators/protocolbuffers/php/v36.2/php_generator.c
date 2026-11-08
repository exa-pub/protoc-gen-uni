// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/php/php_generator.{h,cc} (protobuf v36.2). The names of
 * names.cc are libprotoc's (php_names.h). The C extension output of internal_generate_c_wkt
 * (GenerateCWellKnownTypes) and aggregate_metadata (GenerateAddFilesToPool) are not ported, see
 * php_generate_all. */

#define kDescriptorFile "google/protobuf/descriptor.proto"
#define kEmptyFile "google/protobuf/empty.proto"
#define kEmptyMetadataFile "GPBMetadata/Google/Protobuf/GPBEmpty.php"
#define kDescriptorMetadataFile "GPBMetadata/Google/Protobuf/Internal/Descriptor.php"
#define kDescriptorPackageName "Google\\Protobuf\\Internal"

// This is for backwards compatibility. New values added to kReservedNames in
// names.c should be added here to prevent unnecessary breaking changes, as
// constants do not need the PB prefix.
// BEGIN VALID CONSTANT NAMES
// @see php/update_reserved_words.sh - DO NOT MODIFY THIS LIST MANUALLY
static const char *const kValidConstantNames[] = {
	"bool",     "false", "float",  "int",    "iterable",
	"mixed",    "never", "null",   "object", "parent",
	"readonly", "self",  "string", "true",   "void"};
// END VALID CONSTANT NAMES
enum { kFieldSetter = 1, kFieldGetter = 2, kFieldProperty = 3 };

typedef struct {
	bool is_descriptor;
	/* GetSourceLocation of the descriptors of the file being generated */
	const LpSourceLocations *locations;
} Options;

/* FieldDescriptor::type_name() and cpp_type_name() */
static const char *const php_type_names[] = {
	"", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool", "string",
	"group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64",
};
static const char *const php_cpp_type_names[] = {
	"", "int32", "int64", "uint32", "uint64", "double", "float", "bool", "enum", "string", "message",
};

static const char *type_name(const LpField *f) { return php_type_names[f->type]; }
static int cpp_type(const LpField *f) { return lp_field_cpp_type(f); }

static char *ascii_str_to_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return r;
}

static char *ascii_str_to_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	return r;
}

static bool deprecated(const PbMsg *options) { return pb_int(options, OPT_DEPRECATED, 0) != 0; }

/* kBannedPhpNamespaceChars, IsValidPhpNamespace */
static bool is_valid_php_namespace(Str ns, Buf *error)
{
	static const char banned[] = ";{}()'\"`$#=<>|&[]/\n\r!?*~^%@,";
	for (size_t i = 0; i < ns.n; i++) {
		char c = ns.p[i];
		if (c == 0 || memchr(banned, c, sizeof banned - 1)) {
			buf_adds(error, "Invalid character in PHP namespace option: character '");
			buf_addc(error, c);
			buf_adds(error, "' is not allowed because it could enable code injection.");
			return false;
		}
	}
	return true;
}

static char *php_name(const char *full_name, const Options *options);
static char *underscores_to_camel_case(Str name, bool cap_first_letter);

/* DescriptorFullName */
static char *descriptor_full_name(const char *full_name, bool is_internal)
{
	if (is_internal) {
		const char *index = strstr(full_name, "google.protobuf");
		if (index)
			return xcat(xstrndup(full_name, (size_t)(index - full_name)), "google.protobuf.internal",
				    index + strlen("google.protobuf"));
	}
	return xstrdup(full_name);
}

/* ConstantNamePrefix */
static const char *constant_name_prefix(const char *classname)
{
	char *lower = ascii_str_to_lower(classname);
	bool is_reserved = lp_php_is_reserved_name(lower);
	for (size_t i = 0; i < sizeof kValidConstantNames / sizeof *kValidConstantNames; i++) {
		if (str_eq(lower, kValidConstantNames[i])) {
			is_reserved = false;
			break;
		}
	}
	return is_reserved ? "PB" : "";
}

/* RootPhpNamespace */
static char *root_php_namespace(const LpFile *file, const Options *options)
{
	if (pb_has(file->options, FILEOPT_PHP_NAMESPACE))
		return xstrdup(pb_str(file->options, FILEOPT_PHP_NAMESPACE));
	if (*file->package)
		return php_name(file->package, options);
	return "";
}

/* GeneratedClassName for messages, enums and services */
static char *generated_class_name(const ProtoDesc *desc)
{
	switch (desc->kind) {
	case PROTO_MESSAGE: return lp_php_generated_class_name_message((const LpMessage *)desc);
	case PROTO_ENUM: return lp_php_generated_class_name_enum((const LpEnum *)desc);
	default: return lp_php_generated_class_name_service((const LpService *)desc);
	}
}

/* FullClassName(desc, options) and FullClassName(desc, is_descriptor) */
static char *full_class_name(const void *desc, bool is_descriptor)
{
	Options options = {.is_descriptor = is_descriptor};
	char *classname = generated_class_name(desc);
	char *php_namespace = root_php_namespace(proto_desc_file(desc), &options);
	if (*php_namespace)
		return xcat(php_namespace, "\\", classname);
	return classname;
}

/* PhpNamePrefix */
static const char *php_name_prefix(const char *classname) { return lp_php_is_reserved_name(classname) ? "PB" : ""; }

/* PhpName */
static char *php_name(const char *full_name, const Options *options)
{
	if (options->is_descriptor)
		return xstrdup(kDescriptorPackageName);
	Buf segment = {0}, result = {0};
	bool cap_next_letter = true;
	for (const char *p = full_name; *p; p++) {
		if ('a' <= *p && *p <= 'z' && cap_next_letter) {
			buf_addc(&segment, (char)(*p + ('A' - 'a')));
			cap_next_letter = false;
		} else if (*p == '.') {
			const char *s = buf_str(&segment);
			buf_adds(&result, php_name_prefix(s));
			buf_adds(&result, s);
			buf_addc(&result, '\\');
			segment.n = 0;
			cap_next_letter = true;
		} else {
			buf_addc(&segment, *p);
			cap_next_letter = false;
		}
	}
	const char *s = buf_str(&segment);
	buf_adds(&result, php_name_prefix(s));
	buf_adds(&result, s);
	return xstrdup(buf_str(&result));
}

/* DefaultForField */
static const char *default_for_field(const LpField *field)
{
	switch (field->type) {
	case PT_INT32:
	case PT_INT64:
	case PT_UINT32:
	case PT_UINT64:
	case PT_SINT32:
	case PT_SINT64:
	case PT_FIXED32:
	case PT_FIXED64:
	case PT_SFIXED32:
	case PT_SFIXED64:
	case PT_ENUM: return "0";
	case PT_DOUBLE:
	case PT_FLOAT: return "0.0";
	case PT_BOOL: return "false";
	case PT_STRING:
	case PT_BYTES: return "''";
	case PT_MESSAGE:
	case PT_GROUP: return "null";
	default: return "";
	}
}

/* absl::StrCat of an unsigned integer */
static char *utoa(uint64_t v)
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

/* DefaultForFieldWithPresence */
static char *default_for_field_with_presence(const LpField *field)
{
	if (field->has_default_value) {
		switch (cpp_type(field)) {
		case LP_CPPTYPE_INT32: return xfmt("%d", (int32_t)field->default_value.i);
		case LP_CPPTYPE_INT64: {
			char *v = xfmt("%lld", (long long)field->default_value.i);
			return xcat("GPBUtil::compatibleInt64(", v, ", '", v, "')");
		}
		case LP_CPPTYPE_UINT32: return utoa((uint32_t)field->default_value.u);
		case LP_CPPTYPE_UINT64: {
			char *v = utoa(field->default_value.u);
			return xcat("GPBUtil::compatibleInt64(", v, ", '", v, "')");
		}
		case LP_CPPTYPE_FLOAT: return lp_six_digits(field->default_value.f);
		case LP_CPPTYPE_DOUBLE: return lp_six_digits(field->default_value.d);
		case LP_CPPTYPE_BOOL: return field->default_value.b ? "true" : "false";
		case LP_CPPTYPE_ENUM: return xfmt("%d", field->default_value_enum->number);
		case LP_CPPTYPE_STRING: return xcat("'", lp_cescape(field->default_value_string), "'");
		case LP_CPPTYPE_MESSAGE: return "null";
		}
	}
	switch (field->type) {
	case PT_INT64:
	case PT_UINT64:
	case PT_SINT64:
	case PT_FIXED64:
	case PT_SFIXED64: return "GPBUtil::compatibleInt64(0, '0')";
	default: return (char *)default_for_field(field);
	}
}

/* DeprecatedConditionalForField */
static char *deprecated_conditional_for_field(const LpField *field)
{
	if (proto_field_is_repeated(field))
		return xcat("count($this->", field->name, ") !== 0");
	if (proto_field_real_oneof(field))
		return xfmt("$this->hasOneof(%d)", field->number);
	if (proto_field_has_presence(field))
		return xcat("isset($this->", field->name, ")");
	return xcat("$this->", field->name, " !== ", default_for_field(field));
}

/* std::string::find_first_of / find_last_of / substr on a string of length n; npos is -1. */
static long find_first_of(const char *s, long n, char c, long pos)
{
	for (long i = pos; i < n; i++)
		if (s[i] == c)
			return i;
	return -1;
}

static long find_last_of(const char *s, long n, char c)
{
	for (long i = n - 1; i >= 0; i--)
		if (s[i] == c)
			return i;
	return -1;
}

/* substr(pos, count) with count converted to size_t (a negative count is npos) */
static Str substr(const char *s, long n, long pos, long count)
{
	if (count < 0 || count > n - pos)
		count = n - pos;
	return (Str){s + pos, (size_t)count};
}

/* GeneratedMetadataFileName (the int indexes of upstream compare equal to npos as -1) */
static char *generated_metadata_file_name(const LpFile *file, const Options *options)
{
	const char *proto_file = file->name;
	long proto_n = (long)strlen(proto_file);
	long start_index = 0;
	long first_index = find_first_of(proto_file, proto_n, '/', start_index);
	Buf result = {0};
	char *segment;

	if (str_eq(proto_file, kEmptyFile))
		return kEmptyMetadataFile;
	if (options->is_descriptor)
		return kDescriptorMetadataFile;

	// Append directory name.
	long lastindex = find_last_of(proto_file, proto_n, '.');
	Str file_no_suffix = substr(proto_file, proto_n, 0, lastindex);
	long n = (long)file_no_suffix.n;

	if (pb_has(file->options, FILEOPT_PHP_METADATA_NAMESPACE)) {
		const char *php_metadata_namespace = pb_str(file->options, FILEOPT_PHP_METADATA_NAMESPACE);
		if (*php_metadata_namespace && !str_eq(php_metadata_namespace, "\\")) {
			buf_adds(&result, php_metadata_namespace);
			for (size_t i = 0; i < result.n; i++)
				if (result.p[i] == '\\')
					result.p[i] = '/';
			if (result.p[result.n - 1] != '/')
				buf_addc(&result, '/');
		}
	} else {
		buf_adds(&result, "GPBMetadata/");
		while (first_index != -1) {
			segment = underscores_to_camel_case(
				substr(file_no_suffix.p, n, start_index, first_index - start_index), true);
			buf_adds(&result, lp_php_reserved_name_prefix(segment, file));
			buf_adds(&result, segment);
			buf_addc(&result, '/');
			start_index = first_index + 1;
			first_index = find_first_of(file_no_suffix.p, n, '/', start_index);
		}
	}

	// Append file name.
	long file_name_start = find_last_of(file_no_suffix.p, n, '/');
	if (file_name_start == -1)
		file_name_start = 0;
	else
		file_name_start += 1;
	segment = underscores_to_camel_case(substr(file_no_suffix.p, n, file_name_start, first_index - file_name_start),
					    true);
	buf_adds(&result, lp_php_reserved_name_prefix(segment, file));
	buf_adds(&result, segment);
	buf_adds(&result, ".php");
	return xstrdup(buf_str(&result));
}

/* GeneratedClassFileName */
static char *generated_class_file_name(const void *desc, const Options *options)
{
	char *result = full_class_name(desc, options->is_descriptor);
	for (char *p = result; *p; p++)
		if (*p == '\\')
			*p = '/';
	return xcat(result, ".php");
}

/* IntToString */
static char *int_to_string(int32_t value) { return xfmt("%d", value); }

/* LabelForField */
static const char *label_for_field(const LpField *field)
{
	if (proto_field_is_required(field))
		return "required";
	if (proto_field_is_repeated(field))
		return "repeated";
	return "optional";
}

/* The scalar part shared by PhpSetterTypeName, PhpDocSetterTypeName and PhpDocGetterTypeName;
 * NULL for groups. */
static char *php_scalar_type_name(const LpField *field, const Options *options)
{
	switch (field->type) {
	case PT_INT32:
	case PT_UINT32:
	case PT_SINT32:
	case PT_FIXED32:
	case PT_SFIXED32:
	case PT_ENUM: return "int";
	case PT_INT64:
	case PT_UINT64:
	case PT_SINT64:
	case PT_FIXED64:
	case PT_SFIXED64: return "int|string";
	case PT_DOUBLE:
	case PT_FLOAT: return "float";
	case PT_BOOL: return "bool";
	case PT_STRING:
	case PT_BYTES: return "string";
	case PT_MESSAGE: return xcat("\\", full_class_name(field->message_type, options->is_descriptor));
	case PT_GROUP: return NULL;
	default: return "";
	}
}

/* PhpSetterTypeName */
static char *php_setter_type_name(const LpField *field, const Options *options)
{
	if (proto_field_is_map(field))
		return "array|\\Google\\Protobuf\\Internal\\MapField";
	if (proto_field_is_repeated(field))
		return "array|RepeatedField";
	char *type = php_scalar_type_name(field, options);
	if (!type)
		return "null";
	if (cpp_type(field) == LP_CPPTYPE_MESSAGE)
		return xcat(type, "|null");
	return type;
}

/* PhpDocSetterTypeName */
static char *php_doc_setter_type_name(const LpField *field, const Options *options)
{
	if (proto_field_is_map(field))
		return "array|\\Google\\Protobuf\\Internal\\MapField";
	char *type = php_scalar_type_name(field, options);
	if (!type)
		return "null";
	if (proto_field_is_repeated(field)) {
		// accommodate for edge case with multiple types.
		char *bar = strchr(type, '|');
		if (bar)
			type = xcat(xstrndup(type, (size_t)(bar - type)), "[]|", bar + 1);
		type = xcat(type, "[]");
	}
	return type;
}

/* PhpDocGetterTypeName */
static char *php_doc_getter_type_name(const LpField *field, const Options *options)
{
	if (proto_field_is_map(field))
		return "\\Google\\Protobuf\\Internal\\MapField";
	char *type = php_scalar_type_name(field, options);
	if (!type)
		type = "null";
	if (proto_field_is_repeated(field)) {
		// accommodate for edge case with multiple types.
		char *bar = strchr(type, '|');
		if (bar)
			type = xcat(xstrndup(type, (size_t)(bar - type)), ">|RepeatedField<", bar + 1);
		type = xcat("RepeatedField<", type, ">");
	}
	return type;
}

/* EnumOrMessageSuffix */
static char *enum_or_message_suffix(const LpField *field, bool is_descriptor)
{
	if (cpp_type(field) == LP_CPPTYPE_MESSAGE)
		return xcat(", '", descriptor_full_name(field->message_type->full_name, is_descriptor), "'");
	if (cpp_type(field) == LP_CPPTYPE_ENUM)
		return xcat(", '", descriptor_full_name(field->enum_type->full_name, is_descriptor), "'");
	return "";
}

/* UnderscoresToCamelCase */
static char *underscores_to_camel_case(Str name, bool cap_first_letter)
{
	Buf result = {0};
	for (size_t i = 0; i < name.n; i++) {
		char c = name.p[i];
		if ('a' <= c && c <= 'z') {
			buf_addc(&result, cap_first_letter ? (char)(c + ('A' - 'a')) : c);
			cap_first_letter = false;
		} else if ('A' <= c && c <= 'Z') {
			if (i == 0 && !cap_first_letter) {
				// Force first letter to lower-case unless explicitly told to
				// capitalize it.
				buf_addc(&result, (char)(c + ('a' - 'A')));
			} else {
				// Capital letters after the first are left as-is.
				buf_addc(&result, c);
			}
			cap_first_letter = false;
		} else if ('0' <= c && c <= '9') {
			buf_addc(&result, c);
			cap_first_letter = true;
		} else {
			cap_first_letter = true;
		}
	}
	// Add a trailing "_" if the name should be altered. (Upstream reads name[-1] for an empty
	// name; the names passed here are never empty or follow a '/'.)
	if (name.n && name.p[name.n - 1] == '#')
		buf_addc(&result, '_');
	return xstrdup(buf_str(&result));
}

static char *camel(const char *name) { return underscores_to_camel_case(str_c(name), true); }

static void indent(LpPrinter *printer)
{
	lp_indent(printer);
	lp_indent(printer);
}

static void outdent(LpPrinter *printer)
{
	lp_outdent(printer);
	lp_outdent(printer);
}

/* BinaryToPhpString */
static char *binary_to_php_string(Str src)
{
	static const char symbol[16] = "0123456789ABCDEF";
	Buf dest = {0};
	buf_grow(&dest, src.n + src.n / 5 + 1);
	for (size_t i = 0; i < src.n; i++) {
		unsigned char c = (unsigned char)src.p[i];
		// To escape:
		// - escape sequences
		// - variable expansion: "hello $username";
		// - string termination
		if (c == '\\' || c == '$' || c == '"') {
			buf_addc(&dest, '\\');
			buf_addc(&dest, (char)c);
		} else if (c >= 0x20 && c < 0x7f) {
			buf_addc(&dest, (char)c);
		} else {
			buf_addc(&dest, '\\');
			buf_addc(&dest, 'x');
			buf_addc(&dest, symbol[c >> 4]);
			buf_addc(&dest, symbol[c & 0x0f]);
		}
	}
	return xstrdup(buf_str(&dest));
}

static void generate_field_doc_comment(LpPrinter *printer, const LpField *field, const Options *options,
				       int function_type);
static void generate_wrapper_field_getter_doc_comment(LpPrinter *printer, const LpField *field,
						      const Options *options);
static void generate_wrapper_field_setter_doc_comment(LpPrinter *printer, const LpField *field,
						      const Options *options);

/* GenerateField */
static bool generate_field(const LpField *field, LpPrinter *printer, const Options *options, Buf *error)
{
	if (!options->is_descriptor && proto_field_is_required(field)) {
		buf_adds(error, xcat("Can't generate PHP code for required field ", field->full_name, ".\n"));
		return false;
	}
	if (field->type == PT_GROUP) {
		buf_adds(error, xcat("Can't generate PHP code for group field ", field->full_name,
				     ".  Use regular message encoding instead.\n"));
		return false;
	}

	if (proto_field_is_repeated(field)) {
		generate_field_doc_comment(printer, field, options, kFieldProperty);
		lp_print(printer, "private $^name^;\n", "name", field->name, NULL);
	} else if (proto_field_real_oneof(field)) {
		// Oneof fields are handled by GenerateOneofField.
		return true;
	} else {
		const char *initial_value = proto_field_has_presence(field) ? "null" : default_for_field(field);
		generate_field_doc_comment(printer, field, options, kFieldProperty);
		lp_print(printer, "protected $^name^ = ^initial_value^;\n", "name", field->name, "initial_value",
			 initial_value, NULL);
	}
	return true;
}

/* GenerateOneofField */
static void generate_oneof_field(const LpOneof *oneof, LpPrinter *printer)
{
	// Oneof property needs to be protected in order to be accessed by parent
	// class in implementation.
	lp_print(printer, "protected $^name^;\n", "name", oneof->name, NULL);
}

/* Descriptor::map_key(), map_value() */
static const LpField *map_key(const LpMessage *entry) { return proto_message_field_by_number(entry, 1); }
static const LpField *map_value(const LpMessage *entry) { return proto_message_field_by_number(entry, 2); }

/* IsWrapperType (php_generator.h) */
static bool is_wrapper_type(const LpField *field)
{
	return cpp_type(field) == LP_CPPTYPE_MESSAGE &&
	       str_eq(field->message_type->file->name, "google/protobuf/wrappers.proto");
}

/* GenerateFieldAccessor */
static void generate_field_accessor(const LpField *field, const Options *options, LpPrinter *printer)
{
	const LpOneof *oneof = proto_field_real_oneof(field);
	char *camel_name = camel(field->name);
	bool is_map = proto_field_is_map(field), is_repeated = proto_field_is_repeated(field);

	// Generate getter.
	generate_field_doc_comment(printer, field, options, kFieldGetter);

	// deprecation
	bool is_deprecated = deprecated(field->options);
	char *deprecation_trigger =
		is_deprecated ? xcat("@trigger_error('", field->name, " is deprecated.', E_USER_DEPRECATED);\n        ")
			      : "";
	char *deprecation_trigger_with_conditional =
		is_deprecated ? xcat("if (", deprecated_conditional_for_field(field), ") {\n            ",
				     deprecation_trigger, "}\n        ")
			      : "";

	// Emit getter.
	if (oneof) {
		lp_print(printer,
			 "public function get^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger^return $this->readOneof(^number^);\n"
			 "}\n\n",
			 "camel_name", camel_name, "number", int_to_string(field->number), "deprecation_trigger",
			 deprecation_trigger_with_conditional, NULL);
	} else if (proto_field_has_presence(field) && !field->message_type) {
		lp_print(printer,
			 "public function get^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger^return isset($this->^name^) ? $this->^name^ "
			 ": ^default_value^;\n"
			 "}\n\n",
			 "camel_name", camel_name, "name", field->name, "default_value",
			 default_for_field_with_presence(field), "deprecation_trigger",
			 deprecation_trigger_with_conditional, NULL);
	} else {
		lp_print(printer,
			 "public function get^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger^return $this->^name^;\n"
			 "}\n\n",
			 "camel_name", camel_name, "name", field->name, "deprecation_trigger",
			 deprecation_trigger_with_conditional, NULL);
	}

	// Emit hazzers/clear.
	if (oneof) {
		lp_print(printer,
			 "public function has^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger^return $this->hasOneof(^number^);\n"
			 "}\n\n",
			 "camel_name", camel_name, "number", int_to_string(field->number), "deprecation_trigger",
			 deprecation_trigger_with_conditional, NULL);
	} else if (proto_field_has_presence(field)) {
		lp_print(printer,
			 "public function has^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger_with_conditional^return isset($this->^name^);"
			 "\n}\n\n"
			 "public function clear^camel_name^()\n"
			 "{\n"
			 "    ^deprecation_trigger^unset($this->^name^);\n"
			 "}\n\n",
			 "camel_name", camel_name, "name", field->name, "default_value", default_for_field(field),
			 "deprecation_trigger", deprecation_trigger, "deprecation_trigger_with_conditional",
			 deprecation_trigger_with_conditional, NULL);
	}

	// For wrapper types, generate an additional getXXXUnwrapped getter
	if (!is_map && !is_repeated && cpp_type(field) == LP_CPPTYPE_MESSAGE && is_wrapper_type(field)) {
		generate_wrapper_field_getter_doc_comment(printer, field, options);
		lp_print(printer,
			 "public function get^camel_name^Unwrapped()\n"
			 "{\n"
			 "    ^deprecation_trigger^return "
			 "$this->readWrapperValue(\"^field_name^\");\n"
			 "}\n\n",
			 "camel_name", camel_name, "field_name", field->name, "deprecation_trigger",
			 deprecation_trigger_with_conditional, NULL);
	}

	// Generate setter.
	generate_field_doc_comment(printer, field, options, kFieldSetter);
	lp_print(printer,
		 "public function set^camel_name^(^php_type^ $var)\n"
		 "{\n",
		 "camel_name", camel_name, "php_type", php_setter_type_name(field, options), NULL);

	indent(printer);

	if (is_deprecated && !is_map && !is_repeated)
		lp_print(printer, "^deprecation_trigger^", "deprecation_trigger", deprecation_trigger, NULL);

	// Type check.
	if (is_map) {
		const LpMessage *map_entry = field->message_type;
		const LpField *key = map_key(map_entry);
		const LpField *value = map_value(map_entry);
		lp_print(printer,
			 "$arr = GPBUtil::checkMapField($var, "
			 "\\Google\\Protobuf\\Internal\\GPBType::^key_type^, "
			 "\\Google\\Protobuf\\Internal\\GPBType::^value_type^",
			 "key_type", ascii_str_to_upper(type_name(key)), "value_type",
			 ascii_str_to_upper(type_name(value)), NULL);
		if (cpp_type(value) == LP_CPPTYPE_MESSAGE)
			lp_print(printer, ", \\^class_name^);\n", "class_name",
				 xcat(full_class_name(value->message_type, options->is_descriptor), "::class"), NULL);
		else if (cpp_type(value) == LP_CPPTYPE_ENUM)
			lp_print(printer, ", \\^class_name^);\n", "class_name",
				 xcat(full_class_name(value->enum_type, options->is_descriptor), "::class"), NULL);
		else
			lp_print(printer, ");\n", NULL);
	} else if (is_repeated) {
		lp_print(printer,
			 "$arr = GPBUtil::checkRepeatedField($var, "
			 "\\Google\\Protobuf\\Internal\\GPBType::^type^",
			 "type", ascii_str_to_upper(type_name(field)), NULL);
		if (cpp_type(field) == LP_CPPTYPE_MESSAGE)
			lp_print(printer, ", \\^class_name^);\n", "class_name",
				 xcat(full_class_name(field->message_type, options->is_descriptor), "::class"), NULL);
		else if (cpp_type(field) == LP_CPPTYPE_ENUM)
			lp_print(printer, ", \\^class_name^);\n", "class_name",
				 xcat(full_class_name(field->enum_type, options->is_descriptor), "::class"), NULL);
		else
			lp_print(printer, ");\n", NULL);
	} else if (cpp_type(field) == LP_CPPTYPE_ENUM) {
		lp_print(printer, "GPBUtil::checkEnum($var, \\^class_name^::class);\n", "class_name",
			 full_class_name(field->enum_type, options->is_descriptor), NULL);
	} else if (cpp_type(field) == LP_CPPTYPE_STRING) {
		lp_print(printer, "GPBUtil::checkString($var, ^utf8^);\n", "utf8",
			 field->type == PT_STRING ? "true" : "false", NULL);
	} else if (cpp_type(field) == LP_CPPTYPE_INT32 || cpp_type(field) == LP_CPPTYPE_UINT32 ||
		   cpp_type(field) == LP_CPPTYPE_INT64 || cpp_type(field) == LP_CPPTYPE_UINT64) {
		lp_print(printer, "GPBUtil::check^type^($var);\n", "type", camel(php_cpp_type_names[cpp_type(field)]),
			 NULL);
	}

	if (is_deprecated && (is_map || is_repeated))
		lp_print(printer, "if (count($arr) !== 0) {\n    ^deprecation_trigger^}\n", "deprecation_trigger",
			 deprecation_trigger, NULL);

	if (oneof)
		lp_print(printer, "$this->writeOneof(^number^, $var);\n", "number", int_to_string(field->number), NULL);
	else if (is_repeated)
		lp_print(printer, "$this->^name^ = $arr;\n", "name", field->name, NULL);
	else
		lp_print(printer, "$this->^name^ = $var;\n", "name", field->name, NULL);

	lp_print(printer, "\nreturn $this;\n", NULL);

	outdent(printer);

	lp_print(printer, "}\n\n", NULL);

	// For wrapper types, generate an additional setXXXValue getter
	if (!is_map && !is_repeated && cpp_type(field) == LP_CPPTYPE_MESSAGE && is_wrapper_type(field)) {
		generate_wrapper_field_setter_doc_comment(printer, field, options);
		lp_print(printer,
			 "public function set^camel_name^Unwrapped($var)\n"
			 "{\n"
			 "    $this->writeWrapperValue(\"^field_name^\", $var);\n"
			 "    return $this;\n"
			 "}\n\n",
			 "camel_name", camel_name, "field_name", field->name, NULL);
	}
}

/* value->options().HasExtension(pb::enumvalue::json): the custom JSON name, or NULL */
static const Str *enum_value_json_name(const LpEnumValue *value, Str *out)
{
	if (!pb_has(value->options, EVOPT_JSON))
		return NULL;
	*out = pb_bytes(pb_msg(value->options, EVOPT_JSON), JSON_ENUMVALUE_STRING);
	return out;
}

/* GenerateEnumToPool */
static void generate_enum_to_pool(const LpEnum *en, LpPrinter *printer)
{
	lp_print(printer,
		 "$pool->addEnum('^name^', "
		 "\\Google\\Protobuf\\Internal\\^class_name^::class)\n",
		 "name", descriptor_full_name(en->full_name, true), "class_name", en->name, NULL);
	indent(printer);

	for (int i = 0; i < en->n_values; i++) {
		const LpEnumValue *value = &en->values[i];
		const char *custom_json_name = "";
		Str json_name;
		if (enum_value_json_name(value, &json_name))
			custom_json_name = xcat(", \"", binary_to_php_string(json_name), "\"");
		lp_print(printer, "->value(\"^name^\", ^number^^custom_json_name^)\n", "name",
			 xcat(constant_name_prefix(value->name), value->name), "number", int_to_string(value->number),
			 "custom_json_name", custom_json_name, NULL);
	}
	lp_print(printer, "->finalizeToPool();\n\n", NULL);
	outdent(printer);
}

/* GenerateMessageToPool */
static void generate_message_to_pool(const char *name_prefix, const LpMessage *message, LpPrinter *printer)
{
	// Don't generate MapEntry messages -- we use the PHP extension's native
	// support for map fields instead.
	if (pb_int(message->options, MSGOPT_MAP_ENTRY, 0))
		return;
	char *class_name = xcat(*name_prefix ? xcat(name_prefix, "\\") : "",
				lp_php_reserved_name_prefix(message->name, message->file), message->name);

	lp_print(printer,
		 "$pool->addMessage('^message^', "
		 "\\Google\\Protobuf\\Internal\\^class_name^::class)\n",
		 "message", descriptor_full_name(message->full_name, true), "class_name", class_name, NULL);

	indent(printer);

	for (int i = 0; i < message->n_fields; i++) {
		const LpField *field = &message->fields[i];
		if (proto_field_is_map(field)) {
			const LpField *key = map_key(field->message_type);
			const LpField *val = map_value(field->message_type);
			lp_print(printer,
				 "->map('^field^', \\Google\\Protobuf\\Internal\\GPBType::^key^, "
				 "\\Google\\Protobuf\\Internal\\GPBType::^value^, ^number^^other^)\n",
				 "field", field->name, "key", ascii_str_to_upper(type_name(key)), "value",
				 ascii_str_to_upper(type_name(val)), "number", int_to_string(field->number), "other",
				 enum_or_message_suffix(val, true), NULL);
		} else if (!proto_field_real_oneof(field)) {
			lp_print(printer,
				 "->^label^('^field^', "
				 "\\Google\\Protobuf\\Internal\\GPBType::^type^, ^number^^other^)\n",
				 "field", field->name, "label", label_for_field(field), "type",
				 ascii_str_to_upper(type_name(field)), "number", int_to_string(field->number), "other",
				 enum_or_message_suffix(field, true), NULL);
		}
	}

	// oneofs.
	for (int i = 0; i < message->n_real_oneofs; i++) {
		const LpOneof *oneof = &message->oneofs[i];
		lp_print(printer, "->oneof(^name^)\n", "name", oneof->name, NULL);
		indent(printer);
		for (int index = 0; index < oneof->n_fields; index++) {
			const LpField *field = proto_oneof_field(oneof, index);
			lp_print(printer,
				 "->value('^field^', "
				 "\\Google\\Protobuf\\Internal\\GPBType::^type^, ^number^^other^)\n",
				 "field", field->name, "type", ascii_str_to_upper(type_name(field)), "number",
				 int_to_string(field->number), "other", enum_or_message_suffix(field, true), NULL);
		}
		lp_print(printer, "->finish()\n", NULL);
		outdent(printer);
	}

	lp_print(printer, "->finalizeToPool();\n", NULL);

	outdent(printer);

	lp_print(printer, "\n", NULL);

	for (int i = 0; i < message->n_nested; i++)
		generate_message_to_pool(class_name, &message->nested[i], printer);
	for (int i = 0; i < message->n_enums; i++)
		generate_enum_to_pool(&message->enums[i], printer);
}

/* CollectCustomEnumJsonNames: (fqn, custom name) pairs appended to names */
static void collect_custom_enum_json_names_enum(const LpEnum *en, Vec *names)
{
	for (int i = 0; i < en->n_values; ++i) {
		const LpEnumValue *value = &en->values[i];
		Str json_name;
		if (enum_value_json_name(value, &json_name)) {
			vec_push(names, xcat(en->full_name, ".", value->name));
			Str *custom_name = xalloc(sizeof *custom_name);
			*custom_name = json_name;
			vec_push(names, custom_name);
		}
	}
}

static void collect_custom_enum_json_names_message(const LpMessage *message, Vec *names)
{
	for (int i = 0; i < message->n_enums; ++i)
		collect_custom_enum_json_names_enum(&message->enums[i], names);
	for (int i = 0; i < message->n_nested; ++i)
		collect_custom_enum_json_names_message(&message->nested[i], names);
}

static void collect_custom_enum_json_names(const LpFile *file, Vec *names)
{
	for (int i = 0; i < file->n_enums; ++i)
		collect_custom_enum_json_names_enum(&file->enums[i], names);
	for (int i = 0; i < file->n_messages; ++i)
		collect_custom_enum_json_names_message(&file->messages[i], names);
}

static char *filename_to_classname(const char *filename);

/* Removes element i of a repeated field of m (RepeatedPtrField::erase). */
static void erase_repeated(PbMsg *m, uint32_t num, int i)
{
	PbSlot *s = &m->slots[proto_msg_field_index(m->def, num)];
	memmove(&s->v[i], &s->v[i + 1], sizeof(PbVal) * (size_t)(s->n - i - 1));
	s->n--;
}

/* The serialized FileDescriptorSet of GenerateAddFileToPool: the file proto with the first
 * dependency that is not descriptor.proto erased (the loop meant to drop descriptor.proto), no
 * public and weak dependencies and no extensions. */
static Str files_data(const LpFile *file)
{
	PbMsg *file_proto = lp_strip_source_retention_options(file);

	// Filter out descriptor.proto as it cannot be depended on for now.
	for (int i = 0; i < pb_count(file_proto, FDP_DEPENDENCY); i++) {
		if (!str_eqc(pb_bytes_at(file_proto, FDP_DEPENDENCY, i), kDescriptorFile)) {
			erase_repeated(file_proto, FDP_DEPENDENCY, i);
			break;
		}
	}

	// Clear public_dependency and weak_dependency since they contain indices
	// into the dependency array which we just modified above. PHP handles
	// dependency loading through initOnce() calls instead.
	proto_msg_clear(file_proto, FDP_PUBLIC_DEPENDENCY);
	proto_msg_clear(file_proto, FDP_WEAK_DEPENDENCY);

	// Filter out all extensions, since we do not support extension yet.
	proto_msg_clear(file_proto, FDP_EXTENSION);
	for (int i = 0; i < pb_count(file_proto, FDP_MESSAGE_TYPE); i++)
		proto_msg_clear(pb_msg_at(file_proto, FDP_MESSAGE_TYPE, i), DP_EXTENSION);

	/* FileDescriptorSet { file: file_proto } */
	Buf proto = {0}, files = {0};
	lp_msg_serialize(file_proto, &proto);
	pb_put_bytes(&files, FDS_FILE, buf_view(&proto));
	return buf_view(&files);
}

/* GenerateAddFileToPool (without aggregate_metadata) */
static void generate_add_file_to_pool(const LpFile *file, const Options *options, LpPrinter *printer)
{
	lp_print(printer,
		 "public static bool $is_initialized = false;\n\n"
		 "public static function initOnce(): void {\n",
		 NULL);
	indent(printer);

	lp_print(printer,
		 "$pool = \\Google\\Protobuf\\Internal\\"
		 "DescriptorPool::getGeneratedPool();\n\n"
		 "if (static::$is_initialized == true) {\n"
		 "  return;\n"
		 "}\n",
		 NULL);

	if (options->is_descriptor) {
		for (int i = 0; i < file->n_messages; i++)
			generate_message_to_pool("", &file->messages[i], printer);
		for (int i = 0; i < file->n_enums; i++)
			generate_enum_to_pool(&file->enums[i], printer);

		lp_print(printer, "$pool->finish();\n", NULL);
	} else {
		for (int i = 0; i < file->n_dependencies; i++) {
			const char *name = file->dependencies[i]->name;
			// Currently, descriptor.proto is not ready for external usage. Skip to
			// import it for now, so that its dependencies can still work as long as
			// they don't use protos defined in descriptor.proto.
			if (str_eq(name, kDescriptorFile))
				continue;
			char *dependency_filename = generated_metadata_file_name(file->dependencies[i], options);
			lp_print(printer, "\\^name^::initOnce();\n", "name", filename_to_classname(dependency_filename),
				 NULL);
		}

		// Add messages and enums to descriptor pool.
		Str data = files_data(file);

		Vec custom_json_names = {0};
		collect_custom_enum_json_names(file, &custom_json_names);

		lp_print(printer, "$pool->internalAddGeneratedFile(\n", NULL);
		indent(printer);
		lp_print(printer, "\"^data^\"\n", "data", binary_to_php_string(data), NULL);
		outdent(printer);
		if (!custom_json_names.n) {
			lp_print(printer, ", true);\n\n", NULL);
		} else {
			lp_print(printer, ", true, [\n", NULL);
			indent(printer);
			for (int i = 0; i < custom_json_names.n; i += 2)
				lp_print(printer, "\"^fqn^\" => \"^custom_name^\",\n", "fqn",
					 VEC_AT(custom_json_names, char, i), "custom_name",
					 binary_to_php_string(*VEC_AT(custom_json_names, Str, i + 1)), NULL);
			outdent(printer);
			lp_print(printer, "]);\n\n", NULL);
		}
	}
	lp_print(printer, "static::$is_initialized = true;\n", NULL);

	outdent(printer);
	lp_print(printer, "}\n", NULL);
}

/* GenerateUseDeclaration */
static void generate_use_declaration(const Options *options, LpPrinter *printer)
{
	if (!options->is_descriptor)
		lp_print(printer,
			 "use Google\\Protobuf\\Internal\\GPBType;\n"
			 "use Google\\Protobuf\\Internal\\GPBUtil;\n"
			 "use Google\\Protobuf\\RepeatedField;\n\n",
			 NULL);
	else
		lp_print(printer,
			 "use Google\\Protobuf\\Internal\\GPBType;\n"
			 "use Google\\Protobuf\\Internal\\GPBWire;\n"
			 "use Google\\Protobuf\\Internal\\InputStream;\n"
			 "use Google\\Protobuf\\Internal\\GPBUtil;\n"
			 "use Google\\Protobuf\\RepeatedField;\n\n",
			 NULL);
}

/* GenerateHead */
static void generate_head(const LpFile *file, LpPrinter *printer)
{
	lp_print(printer,
		 "<?php\n"
		 "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		 "# NO CHECKED-IN"
		 // Intentional line breaker.
		 " PROTOBUF GENCODE\n"
		 "# source: ^filename^\n"
		 "\n",
		 "filename", file->name, NULL);
}

/* FilenameToClassname */
static char *filename_to_classname(const char *filename)
{
	const char *dot = strrchr(filename, '.');
	char *result = dot ? xstrndup(filename, (size_t)(dot - filename)) : xstrdup(filename);
	for (char *p = result; *p; p++)
		if (*p == '/')
			*p = '\\';
	return result;
}

/* fullname.find_last_of('\\') */
static const char *last_backslash(const char *s) { return strrchr(s, '\\'); }

static void open_printer(LpPrinter *printer, LpContext *ctx, const char *filename)
{
	lp_printer_init(printer, lp_context_open(ctx, filename), '^');
}

/* GenerateMetadataFile */
static void generate_metadata_file(const LpFile *file, const Options *options, LpContext *ctx)
{
	char *filename = generated_metadata_file_name(file, options);
	LpPrinter printer;
	open_printer(&printer, ctx, filename);

	generate_head(file, &printer);

	char *fullname = filename_to_classname(filename);
	const char *lastindex = last_backslash(fullname);

	if (lastindex) {
		lp_print(&printer, "namespace ^name^;\n\n", "name", xstrndup(fullname, (size_t)(lastindex - fullname)),
			 NULL);
		lp_print(&printer,
			 "class ^name^\n"
			 "{\n",
			 "name", lastindex + 1, NULL);
	} else {
		lp_print(&printer,
			 "class ^name^\n"
			 "{\n",
			 "name", fullname, NULL);
	}
	indent(&printer);

	generate_add_file_to_pool(file, options, &printer);

	outdent(&printer);
	lp_print(&printer, "}\n\n", NULL);
}

static void generate_enum_doc_comment(LpPrinter *printer, const LpEnum *en, const Options *options);
static void generate_enum_value_doc_comment(LpPrinter *printer, const LpEnumValue *value, const Options *options);
static void generate_message_doc_comment(LpPrinter *printer, const LpMessage *message, const Options *options);
static void generate_message_constructor_doc_comment(LpPrinter *printer, const LpMessage *message,
						     const Options *options);

/* GenerateEnumFile */
static bool generate_enum_file(const LpFile *file, const LpEnum *en, const Options *options, LpContext *ctx,
			       Buf *error)
{
	if (!options->is_descriptor && proto_enum_is_closed(en)) {
		buf_adds(error, xcat("Can't generate PHP code for closed enum ", en->full_name,
				     ".  Please use either proto3 or editions without "
				     "`enum_type = CLOSED`.\n"));
		return false;
	}

	char *filename = generated_class_file_name(en, options);
	LpPrinter printer;
	open_printer(&printer, ctx, filename);

	generate_head(file, &printer);

	char *fullname = filename_to_classname(filename);
	const char *lastindex = last_backslash(fullname);

	if (lastindex) {
		lp_print(&printer, "namespace ^name^;\n\n", "name", xstrndup(fullname, (size_t)(lastindex - fullname)),
			 NULL);

		// We only need this 'use' statement if the enum has a namespace.
		// Otherwise, we get a warning that the use statement has no effect.
		lp_print(&printer, "use UnexpectedValueException;\n\n", NULL);
	}

	generate_enum_doc_comment(&printer, en, options);

	if (lastindex)
		fullname = (char *)lastindex + 1;

	lp_print(&printer,
		 "class ^name^\n"
		 "{\n",
		 "name", fullname, NULL);
	indent(&printer);

	bool has_reserved = false;
	for (int i = 0; i < en->n_values; i++) {
		const LpEnumValue *value = &en->values[i];
		generate_enum_value_doc_comment(&printer, value, options);

		const char *prefix = constant_name_prefix(value->name);
		if (*prefix)
			has_reserved = true;

		lp_print(&printer, "const ^name^ = ^number^;\n", "name", xcat(prefix, value->name), "number",
			 int_to_string(value->number), NULL);
	}

	lp_print(&printer, "\nprivate static $valueToName = [\n", NULL);
	indent(&printer);
	for (int i = 0; i < en->n_values; i++) {
		const LpEnumValue *value = &en->values[i];
		lp_print(&printer, "self::^constant^ => '^name^',\n", "constant",
			 xcat(constant_name_prefix(value->name), value->name), "name", value->name, NULL);
	}
	outdent(&printer);
	lp_print(&printer, "];\n", NULL);

	lp_print(&printer,
		 "\npublic static function name($value)\n"
		 "{\n",
		 NULL);
	indent(&printer);
	lp_print(&printer, "if (!isset(self::$valueToName[$value])) {\n", NULL);
	indent(&printer);
	lp_print(&printer, "throw new UnexpectedValueException(sprintf(\n", NULL);
	indent(&printer);
	indent(&printer);
	lp_print(&printer, "'Enum %s has no name defined for value %s', __CLASS__, $value));\n", NULL);
	outdent(&printer);
	outdent(&printer);
	outdent(&printer);
	lp_print(&printer,
		 "}\n"
		 "return self::$valueToName[$value];\n",
		 NULL);
	outdent(&printer);
	lp_print(&printer, "}\n\n", NULL);

	lp_print(&printer,
		 "\npublic static function value($name)\n"
		 "{\n",
		 NULL);
	indent(&printer);
	lp_print(&printer,
		 "$const = __CLASS__ . '::' . strtoupper($name);\n"
		 "if (!defined($const)) {\n",
		 NULL);
	indent(&printer);
	if (has_reserved) {
		lp_print(&printer,
			 "$pbconst =  __CLASS__. '::PB' . strtoupper($name);\n"
			 "if (!defined($pbconst)) {\n",
			 NULL);
		indent(&printer);
	}
	lp_print(&printer, "throw new UnexpectedValueException(sprintf(\n", NULL);
	indent(&printer);
	indent(&printer);
	lp_print(&printer, "'Enum %s has no value defined for name %s', __CLASS__, $name));\n", NULL);
	outdent(&printer);
	outdent(&printer);
	if (has_reserved) {
		outdent(&printer);
		lp_print(&printer,
			 "}\n"
			 "return constant($pbconst);\n",
			 NULL);
	}
	outdent(&printer);
	lp_print(&printer,
		 "}\n"
		 "return constant($const);\n",
		 NULL);
	outdent(&printer);
	lp_print(&printer, "}\n", NULL);

	outdent(&printer);
	lp_print(&printer, "}\n\n", NULL);

	return true;
}

/* GenerateMessageFile */
static bool generate_message_file(const LpFile *file, const LpMessage *message, const Options *options,
				  LpContext *ctx, Buf *error)
{
	// Don't generate MapEntry messages -- we use the PHP extension's native
	// support for map fields instead.
	if (pb_int(message->options, MSGOPT_MAP_ENTRY, 0))
		return true;

	char *filename = generated_class_file_name(message, options);
	LpPrinter printer;
	open_printer(&printer, ctx, filename);

	generate_head(file, &printer);

	char *fullname = filename_to_classname(filename);
	const char *lastindex = last_backslash(fullname);

	if (lastindex)
		lp_print(&printer, "namespace ^name^;\n\n", "name", xstrndup(fullname, (size_t)(lastindex - fullname)),
			 NULL);

	generate_use_declaration(options, &printer);

	generate_message_doc_comment(&printer, message, options);
	if (lastindex)
		fullname = (char *)lastindex + 1;

	const char *base;
	if (str_eq(message->full_name, "google.protobuf.Any"))
		base = "\\Google\\Protobuf\\Internal\\AnyBase";
	else if (str_eq(message->full_name, "google.protobuf.Timestamp"))
		base = "\\Google\\Protobuf\\Internal\\TimestampBase";
	else
		base = "\\Google\\Protobuf\\Internal\\Message";

	lp_print(&printer,
		 "class ^name^ extends ^base^\n"
		 "{\n",
		 "base", base, "name", fullname, NULL);
	indent(&printer);

	// Field and oneof definitions.
	for (int i = 0; i < message->n_fields; i++)
		if (!generate_field(&message->fields[i], &printer, options, error))
			return false;
	for (int i = 0; i < message->n_real_oneofs; i++)
		generate_oneof_field(&message->oneofs[i], &printer);
	lp_print(&printer, "\n", NULL);

	generate_message_constructor_doc_comment(&printer, message, options);
	lp_print(&printer, "public function __construct($data = null)\n{\n", NULL);
	indent(&printer);

	char *metadata_filename = generated_metadata_file_name(file, options);
	char *metadata_fullname = filename_to_classname(metadata_filename);
	lp_print(&printer, "\\^fullname^::initOnce();\n", "fullname", metadata_fullname, NULL);

	lp_print(&printer, "parent::__construct($data);\n", NULL);

	outdent(&printer);
	lp_print(&printer, "}\n\n", NULL);

	// Field and oneof accessors.
	for (int i = 0; i < message->n_fields; i++)
		generate_field_accessor(&message->fields[i], options, &printer);
	for (int i = 0; i < message->n_real_oneofs; i++) {
		const LpOneof *oneof = &message->oneofs[i];
		lp_print(&printer,
			 "/**\n"
			 " * @return string\n"
			 " */\n"
			 "public function get^camel_name^()\n"
			 "{\n"
			 "    return $this->whichOneof(\"^name^\");\n"
			 "}\n\n",
			 "camel_name", camel(oneof->name), "name", oneof->name, NULL);
	}

	outdent(&printer);
	lp_print(&printer, "}\n\n", NULL);

	// Nested messages and enums.
	for (int i = 0; i < message->n_nested; i++)
		if (!generate_message_file(file, &message->nested[i], options, ctx, error))
			return false;
	for (int i = 0; i < message->n_enums; i++)
		if (!generate_enum_file(file, &message->enums[i], options, ctx, error))
			return false;
	return true;
}

/* GenerateFile */
static bool generate_file(const LpFile *file, const Options *options, LpContext *ctx, Buf *error)
{
	if (pb_has(file->options, FILEOPT_PHP_NAMESPACE) &&
	    !is_valid_php_namespace(pb_bytes(file->options, FILEOPT_PHP_NAMESPACE), error))
		return false;
	if (pb_has(file->options, FILEOPT_PHP_METADATA_NAMESPACE) &&
	    !is_valid_php_namespace(pb_bytes(file->options, FILEOPT_PHP_METADATA_NAMESPACE), error))
		return false;

	generate_metadata_file(file, options, ctx);

	for (int i = 0; i < file->n_messages; i++)
		if (!generate_message_file(file, &file->messages[i], options, ctx, error))
			return false;
	for (int i = 0; i < file->n_enums; i++)
		if (!generate_enum_file(file, &file->enums[i], options, ctx, error))
			return false;
	return true;
}

/* EscapePhpdoc */
static char *escape_phpdoc(Str input)
{
	Buf result = {0};
	char prev = '\0';
	for (size_t i = 0; i < input.n; i++) {
		char c = input.p[i];
		switch (c) {
		// NOTE: "/*" is allowed, do not escape it
		case '/':
			// Escape "*/" with "{@*}".
			if (prev == '*') {
				result.n--;
				buf_adds(&result, "{@*}");
			} else {
				buf_addc(&result, c);
			}
			break;
		case '@':
			// '@' starts phpdoc tags. Play it safe and escape it.
			buf_adds(&result, "\\");
			buf_addc(&result, c);
			break;
		default: buf_addc(&result, c); break;
		}
		prev = c;
	}
	return xstrdup(buf_str(&result));
}

static char *escape_phpdoc_c(const char *s) { return escape_phpdoc(str_c(s)); }

/* GenerateDocCommentBodyForLocation */
static void generate_doc_comment_body_for_location(LpPrinter *printer, const LpSourceLocation *location,
						   bool trailing_newline, int indent_count)
{
	Str comments = location->leading_comments.n ? location->leading_comments : location->trailing_comments;
	if (!comments.n)
		return;
	// If the comment itself contains block comment start or end markers,
	// HTML-escape them so that they don't accidentally close the doc comment.
	const char *escaped = escape_phpdoc(comments);

	char *ind = xalloc((size_t)indent_count + 1);
	memset(ind, ' ', (size_t)indent_count);
	/* absl::StrSplit(comments, '\n', absl::SkipEmpty()) */
	for (const char *p = escaped; *p;) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		if (n) {
			char *line = xstrndup(p, n);
			// Most lines should start with a space.  Watch out for lines that start
			// with a /, since putting that right after the leading asterisk will
			// close the comment.
			if (indent_count == 0 && line[0] == '/')
				lp_print(printer, " * ^line^\n", "line", line, NULL);
			else
				lp_print(printer, " *^ind^^line^\n", "ind", ind, "line", line, NULL);
		}
		p += n;
		if (*p == '\n')
			p++;
	}
	if (trailing_newline)
		lp_print(printer, " *\n", NULL);
}

/* GenerateDocCommentBody */
static void generate_doc_comment_body(LpPrinter *printer, const void *descriptor, const Options *options)
{
	LpSourceLocation location;
	if (lp_get_source_location(options->locations, descriptor, &location))
		generate_doc_comment_body_for_location(printer, &location, true, 0);
}

/* FirstLineOf */
static char *first_line_of(const char *value)
{
	const char *nl = strchr(value, '\n');
	return nl ? xstrndup(value, (size_t)(nl - value)) : xstrdup(value);
}

static char *field_def(const LpField *field) { return escape_phpdoc_c(first_line_of(lp_field_debug_string_first_line(field))); }

/* GenerateMessageDocComment */
static void generate_message_doc_comment(LpPrinter *printer, const LpMessage *message, const Options *options)
{
	lp_print(printer, "/**\n", NULL);
	generate_doc_comment_body(printer, message, options);
	if (deprecated(message->options))
		lp_print(printer, " * @deprecated\n", NULL);

	lp_print(printer,
		 " * Generated from protobuf message <code>^messagename^</code>\n"
		 " */\n",
		 "fullname", escape_phpdoc_c(full_class_name(message, options->is_descriptor)), "messagename",
		 escape_phpdoc_c(message->full_name), NULL);
}

/* GenerateMessageConstructorDocComment */
static void generate_message_constructor_doc_comment(LpPrinter *printer, const LpMessage *message,
						     const Options *options)
{
	// In theory we should have slightly different comments for setters, getters,
	// etc., but in practice everyone already knows the difference between these
	// so it's redundant information.

	// We start the comment with the main body based on the comments from the
	// .proto file (if present). We then end with the field declaration, e.g.:
	//   optional string foo = 5;
	// If the field is a group, the debug string might end with {.
	lp_print(printer, "/**\n", NULL);
	lp_print(printer, " * Constructor.\n", NULL);
	lp_print(printer, " *\n", NULL);
	lp_print(printer, " * @param array $data {\n", NULL);
	lp_print(printer, " *     Optional. Data for populating the Message object.\n", NULL);
	lp_print(printer, " *\n", NULL);
	for (int i = 0; i < message->n_fields; i++) {
		const LpField *field = &message->fields[i];
		lp_print(printer, " *     @type ^phpdoc_type^ $^var^\n", "phpdoc_type", php_doc_setter_type_name(field, options),
			 "var", field->name, NULL);
		LpSourceLocation location;
		if (lp_get_source_location(options->locations, (const ProtoDesc *)field, &location))
			generate_doc_comment_body_for_location(printer, &location, false, 10);
	}
	lp_print(printer, " * }\n", NULL);
	lp_print(printer, " */\n", NULL);
}

/* GenerateFieldDocComment */
static void generate_field_doc_comment(LpPrinter *printer, const LpField *field, const Options *options,
				       int function_type)
{
	// In theory we should have slightly different comments for setters, getters,
	// etc., but in practice everyone already knows the difference between these
	// so it's redundant information.

	// We start the comment with the main body based on the comments from the
	// .proto file (if present). We then end with the field declaration, e.g.:
	//   optional string foo = 5;
	// If the field is a group, the debug string might end with {.
	lp_print(printer, "/**\n", NULL);
	generate_doc_comment_body(printer, field, options);
	lp_print(printer, " * Generated from protobuf field <code>^def^</code>\n", "def", field_def(field), NULL);
	if (function_type == kFieldSetter) {
		if (field->type == PT_ENUM)
			lp_print(printer,
				 " * @param ^phpdoc_type^ $var one of the values in {@see "
				 "^enum_class^}\n",
				 "phpdoc_type", php_doc_setter_type_name(field, options), "enum_class",
				 xcat("\\", full_class_name(field->enum_type, options->is_descriptor)), NULL);
		else
			lp_print(printer, " * @param ^phpdoc_type^ $var\n", "phpdoc_type",
				 php_doc_setter_type_name(field, options), NULL);
		lp_print(printer, " * @return $this\n", NULL);
	} else if (function_type == kFieldGetter) {
		bool can_return_null = proto_field_has_presence(field) && cpp_type(field) == LP_CPPTYPE_MESSAGE;
		if (field->type == PT_ENUM)
			lp_print(printer,
				 " * @return ^phpdoc_type^^maybe_null^ one of the values in {@see "
				 "^enum_class^}\n",
				 "phpdoc_type", php_doc_getter_type_name(field, options), "maybe_null",
				 can_return_null ? "|null" : "", "enum_class",
				 xcat("\\", full_class_name(field->enum_type, options->is_descriptor)), NULL);
		else
			lp_print(printer, " * @return ^phpdoc_type^^maybe_null^\n", "phpdoc_type",
				 php_doc_getter_type_name(field, options), "maybe_null", can_return_null ? "|null" : "",
				 NULL);
	}
	if (deprecated(field->options))
		lp_print(printer, " * @deprecated\n", NULL);
	lp_print(printer, " */\n", NULL);
}

/* field->message_type()->FindFieldByName("value"): upstream dereferences the result, so a message
 * of a file named google/protobuf/wrappers.proto without a field "value" crashes it (SIGSEGV) */
static COLD const LpField *wrapper_value_field(const LpField *field)
{
	const LpField *value = proto_message_field_by_name(field->message_type, "value");
	if (!value)
		fail("wrapper type %s has no field \"value\" (upstream crashes)", field->message_type->full_name);
	return value;
}

/* GenerateWrapperFieldGetterDocComment */
static void generate_wrapper_field_getter_doc_comment(LpPrinter *printer, const LpField *field,
						      const Options *options)
{
	// Generate a doc comment for the special getXXXValue methods that are
	// generated for wrapper types.
	const LpField *primitive_field = wrapper_value_field(field);
	Options plain = {.is_descriptor = false};
	lp_print(printer, "/**\n", NULL);
	lp_print(printer, " * Returns the unboxed value from <code>get^camel_name^()</code>\n\n", "camel_name",
		 camel(field->name), NULL);
	generate_doc_comment_body(printer, field, options);
	lp_print(printer, " * Generated from protobuf field <code>^def^</code>\n", "def", field_def(field), NULL);
	lp_print(printer, " * @return ^phpdoc_type^|null\n", "phpdoc_type",
		 php_doc_getter_type_name(primitive_field, &plain), NULL);
	lp_print(printer, " */\n", NULL);
}

/* GenerateWrapperFieldSetterDocComment */
static void generate_wrapper_field_setter_doc_comment(LpPrinter *printer, const LpField *field,
						      const Options *options)
{
	// Generate a doc comment for the special setXXXValue methods that are
	// generated for wrapper types.
	const LpField *primitive_field = wrapper_value_field(field);
	Options plain = {.is_descriptor = false};
	lp_print(printer, "/**\n", NULL);
	lp_print(printer,
		 " * Sets the field by wrapping a primitive type in a ^message_name^ "
		 "object.\n\n",
		 "message_name", full_class_name(field->message_type, false), NULL);
	generate_doc_comment_body(printer, field, options);
	lp_print(printer, " * Generated from protobuf field <code>^def^</code>\n", "def", field_def(field), NULL);
	lp_print(printer, " * @param ^phpdoc_type^|null $var\n", "phpdoc_type",
		 php_doc_setter_type_name(primitive_field, &plain), NULL);
	lp_print(printer, " * @return $this\n", NULL);
	lp_print(printer, " */\n", NULL);
}

/* GenerateEnumDocComment */
static void generate_enum_doc_comment(LpPrinter *printer, const LpEnum *en, const Options *options)
{
	lp_print(printer, "/**\n", NULL);
	if (deprecated(en->options))
		lp_print(printer, " * @deprecated\n", NULL);
	generate_doc_comment_body(printer, en, options);
	lp_print(printer,
		 " * Protobuf type <code>^fullname^</code>\n"
		 " */\n",
		 "fullname", escape_phpdoc_c(en->full_name), NULL);
}

/* GenerateEnumValueDocComment */
static void generate_enum_value_doc_comment(LpPrinter *printer, const LpEnumValue *value, const Options *options)
{
	lp_print(printer, "/**\n", NULL);
	generate_doc_comment_body(printer, value, options);
	lp_print(printer,
		 " * Generated from protobuf enum <code>^def^</code>\n"
		 " */\n",
		 "def", escape_phpdoc_c(first_line_of(lp_enum_value_debug_string_first_line(value))), NULL);
}

/* Generator::Generate(file, options, context, error) */
static bool php_generate_file(const LpFile *file, Options *options, LpContext *ctx, Buf *error)
{
	if (options->is_descriptor && !str_eq(file->name, kDescriptorFile)) {
		buf_adds(error, "Can only generate PHP code for google/protobuf/descriptor.proto.\n");
		return false;
	}
	options->locations = lp_source_locations(file);
	return generate_file(file, options, ctx, error);
}

/* Generator::Generate(file, parameter, context, error) */
static bool php_generate(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	(void)g;
	(void)parameter;
	Options options = {0};
	return php_generate_file(file, &options, ctx, error);
}

/* Generator::GenerateAll */
static bool php_generate_all(const LpGenerator *g, LpFile *const *files, int n, const char *parameter,
			     LpContext *ctx, Buf *error)
{
	(void)g;
	Options options = {0};

	/* absl::StrSplit(parameter, ',', absl::SkipEmpty()), then option_pair[0] is the first
	 * non-empty part of the option split on '=' */
	Vec parts = str_split(parameter, ',');
	for (int i = 0; i < parts.n; i++) {
		const char *option = VEC_AT(parts, char, i);
		if (!*option)
			continue;
		while (*option == '=')
			option++;
		const char *eq = strchr(option, '=');
		char *key = eq ? xstrndup(option, (size_t)(eq - option)) : xstrdup(option);
		if (has_prefix(key, "aggregate_metadata")) {
			/* GenerateAddFilesToPool orders the files through absl hash sets of
			 * descriptor pointers, so its output differs between runs */
			buf_adds(error, xcat("unsupported by protoc-gen-uni: option ", key));
			return false;
		} else if (str_eq(key, "internal")) {
			options.is_descriptor = true;
		} else if (str_eq(key, "internal_generate_c_wkt")) {
			/* GenerateCWellKnownTypes writes ../ext/google/protobuf/wkt.inc for the
			 * protobuf PHP extension */
			buf_adds(error, "unsupported by protoc-gen-uni: option internal_generate_c_wkt");
			return false;
		} else {
			/* upstream: ABSL_LOG(FATAL), the plugin aborts */
			buf_adds(error, xcat("Unknown codegen option: ", key));
			return false;
		}
	}

	for (int i = 0; i < n; i++)
		if (!php_generate_file(files[i], &options, ctx, error))
			return false;
	return true;
}
