/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/GeneratorOptions.swift (with buf's extramoduleimports.patch)
 * and GenerationError.swift. */

typedef enum { NAMING_FULL_PATH, NAMING_PATH_TO_UNDERSCORES, NAMING_DROP_PATH } OutputNaming;
typedef enum { ENUM_GEN_NONE, ENUM_GEN_NONEXHAUSTIVE, ENUM_GEN_NONEXHAUSTIVE_WARN } EnumGeneration;
typedef enum { VIS_INTERNAL, VIS_PUBLIC, VIS_PACKAGE } Visibility;
typedef enum { IMPORT_ACCESS_LEVEL, IMPORT_PLAIN, IMPORT_IMPLEMENTATION_ONLY } ImportDirective;
enum { HIDDEN_FIELDS = 1, HIDDEN_ENUM_VALUES = 2, HIDDEN_TYPES = 4 };

/* The Swift compiler may fail when checking whether large switch statements are exhaustive. */
#define MAX_CASES_IN_SWITCH 500

typedef struct {
	Vec extra_module_imports; /* char * */
	OutputNaming output_naming;
	EnumGeneration enum_generation;
	SwModuleMappings *proto_to_module_mappings;
	Visibility visibility;
	ImportDirective import_directive;
	bool experimental_strip_nonfunctional_codegen;
	int experimental_hidden_names;
	const char *visibility_source_snippet;
} Options;

static const char *const visibility_names[] = {"internal", "public", "package"};

/* ImportDirective.snippet */
static const char *import_snippet(const Options *o)
{
	switch (o->import_directive) {
	case IMPORT_ACCESS_LEVEL: return xcat(visibility_names[o->visibility], " import");
	case IMPORT_PLAIN: return "import";
	case IMPORT_IMPLEMENTATION_ONLY: return "@_implementationOnly import";
	}
	return "";
}

/* enumAnnotation */
static const char *enum_annotation(const Options *o)
{
	switch (o->enum_generation) {
	case ENUM_GEN_NONE: return "";
	case ENUM_GEN_NONEXHAUSTIVE: return "@nonexhaustive ";
	case ENUM_GEN_NONEXHAUSTIVE_WARN: return "@nonexhaustive(warn) ";
	}
	return "";
}

NORETURN static void unknown_parameter(Str name)
{
	Buf b = {0};
	buf_adds(&b, "Unknown generation parameter '");
	buf_add_str(&b, name);
	buf_addc(&b, '\'');
	fail_str(buf_view(&b));
}

NORETURN static void invalid_parameter_value(Str name, Str value)
{
	Buf b = {0};
	buf_adds(&b, "Unknown value for generation parameter '");
	buf_add_str(&b, name);
	buf_adds(&b, "': '");
	buf_add_str(&b, value);
	buf_addc(&b, '\'');
	fail_str(buf_view(&b));
}

/* String.lowercased() as far as it can give one of the ASCII flags: ASCII letters and U+212A
 * KELVIN SIGN, which lowercases to "k" (no other non-ASCII scalar lowercases to ASCII only).
 * Other bytes, NUL included, are kept, so the result then equals no flag. */
static Str str_lower(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n; i++) {
		char c = s.p[i];
		if (c >= 'A' && c <= 'Z') {
			buf_addc(&b, (char)(c + 0x20));
		} else if (str_has_prefix((Str){s.p + i, s.n - i}, STR_LIT("\xe2\x84\xaa"))) {
			buf_addc(&b, 'k');
			i += 2;
		} else {
			buf_addc(&b, c);
		}
	}
	return buf_view(&b);
}

/* switch flag.lowercased() { case "...": } */
static bool flag_is(Str lowered, const char *flag) { return str_eqc(lowered, flag); }

/* Swift's Bool(String): only "true" and "false". */
static bool parse_bool(Str value, bool *out)
{
	if (str_eqc(value, "true"))
		*out = true;
	else if (str_eqc(value, "false"))
		*out = false;
	else
		return false;
	return true;
}

/* ProtoFileToModuleMappings(path:swiftProtobufModuleName:) */
static SwModuleMappings *load_module_mappings(const char *path, const char *swift_protobuf_module_name);

static Options *parse_options(Str parameter)
{
	Options *o = xalloc(sizeof *o);
	o->output_naming = NAMING_FULL_PATH;
	o->enum_generation = ENUM_GEN_NONE;
	o->visibility = VIS_INTERNAL;
	const char *module_map_path = NULL;
	const char *swift_protobuf_module_name = NULL;
	bool implementation_only_imports = false, use_access_level_on_imports = false;

	SwParamPair *pairs;
	int n = sw_parse_parameter(parameter, &pairs);
	for (int i = 0; i < n; i++) {
		Str key = pairs[i].key, value = pairs[i].value;
		Str lower = str_lower(value);
		if (str_eqc(key, "FileNaming")) {
			if (flag_is(lower, "fullpath") || flag_is(lower, "full_path"))
				o->output_naming = NAMING_FULL_PATH;
			else if (flag_is(lower, "pathtounderscores") || flag_is(lower, "path_to_underscores"))
				o->output_naming = NAMING_PATH_TO_UNDERSCORES;
			else if (flag_is(lower, "droppath") || flag_is(lower, "drop_path"))
				o->output_naming = NAMING_DROP_PATH;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "EnumGeneration")) {
			if (flag_is(lower, "none"))
				o->enum_generation = ENUM_GEN_NONE;
			else if (flag_is(lower, "nonexhaustive"))
				o->enum_generation = ENUM_GEN_NONEXHAUSTIVE;
			else if (flag_is(lower, "nonexhaustivewarn"))
				o->enum_generation = ENUM_GEN_NONEXHAUSTIVE_WARN;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "ProtoPathModuleMappings")) {
			/* The path goes to the error text: ASCII only, so that it is quoted as Swift does. */
			if (value.n)
				module_map_path = sw_ascii_name(value, "parameter ProtoPathModuleMappings");
		} else if (str_eqc(key, "Visibility")) {
			if (flag_is(lower, "internal"))
				o->visibility = VIS_INTERNAL;
			else if (flag_is(lower, "public"))
				o->visibility = VIS_PUBLIC;
			else if (flag_is(lower, "package"))
				o->visibility = VIS_PACKAGE;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "SwiftProtobufModuleName")) {
			char *v = xstrndup(value.p, value.n);
			if (strlen(v) == value.n && sw_is_valid_swift_identifier(v, false))
				swift_protobuf_module_name = v;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "ImplementationOnlyImports")) {
			if (!parse_bool(value, &implementation_only_imports))
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "UseAccessLevelOnImports")) {
			if (!parse_bool(value, &use_access_level_on_imports))
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "experimental_strip_nonfunctional_codegen")) {
			if (!value.n)
				o->experimental_strip_nonfunctional_codegen = true;
			else if (!parse_bool(value, &o->experimental_strip_nonfunctional_codegen))
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "ExperimentalHiddenNames")) {
			/* ExperimentalHiddenNames(flag:): split(separator: ",") of a value that has no comma
			 * (the parameter was split at commas): no part for an empty value. */
			int hidden = 0;
			if (value.n) {
				if (str_eqc(value, "Fields"))
					hidden = HIDDEN_FIELDS;
				else if (str_eqc(value, "EnumValues"))
					hidden = HIDDEN_ENUM_VALUES;
				else if (str_eqc(value, "Types"))
					hidden = HIDDEN_TYPES;
				else if (str_eqc(value, "All"))
					hidden = HIDDEN_FIELDS | HIDDEN_ENUM_VALUES | HIDDEN_TYPES;
				else
					invalid_parameter_value(key, value);
			}
			o->experimental_hidden_names = hidden;
		} else if (str_eqc(key, "ExtraModuleImports")) {
			if (value.n && memchr(value.p, 0, value.n))
				fail("unsupported by protoc-gen-uni: NUL byte in parameter ExtraModuleImports %s", sw_quote(value));
			if (value.n)
				vec_push(&o->extra_module_imports, xstrndup(value.p, value.n));
			else
				invalid_parameter_value(key, value);
		} else {
			unknown_parameter(key);
		}
	}

	if (module_map_path)
		o->proto_to_module_mappings = load_module_mappings(module_map_path, swift_protobuf_module_name);
	else
		o->proto_to_module_mappings = sw_module_mappings(swift_protobuf_module_name);

	switch (o->visibility) {
	case VIS_INTERNAL: o->visibility_source_snippet = ""; break;
	case VIS_PUBLIC: o->visibility_source_snippet = "public "; break;
	case VIS_PACKAGE: o->visibility_source_snippet = "package "; break;
	}

	if (!implementation_only_imports && !use_access_level_on_imports)
		o->import_directive = IMPORT_PLAIN;
	else if (!implementation_only_imports)
		o->import_directive = IMPORT_ACCESS_LEVEL;
	else if (!use_access_level_on_imports)
		o->import_directive = IMPORT_IMPLEMENTATION_ONLY;
	else
		fail("When using access levels on imports the @_implementationOnly option is unnecessary.\n"
		     "Disable @_implementationOnly imports.");

	if (implementation_only_imports && o->visibility != VIS_INTERNAL)
		fail("Cannot use @_implementationOnly imports when the proto visibility is public or package.\n"
		     "Either change the visibility to internal, or disable @_implementationOnly imports.");
	return o;
}

/* Reading the mappings file is not ported: the remote plugin (buf.build) cannot see local files,
 * so it always reports LoadError.failToOpen, and so does the port. */
static SwModuleMappings *load_module_mappings(const char *path, const char *swift_protobuf_module_name)
{
	(void)swift_protobuf_module_name;
	fail("Parameter 'ProtoPathModuleMappings=%s': failToOpen(path: %s)", path, sw_swift_debug_quote(path));
}
