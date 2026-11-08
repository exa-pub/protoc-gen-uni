// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of the names of src/google/protobuf/compiler/cpp/names.h, implemented in
 * compiler/cpp/helpers.cc (protobuf v36.2): see cpp_names.h. */
#include "cpp_names.h"

enum { FILEOPT_PB_FILE_CPP = 990, CPP_FILE_OPTIONS_NAMESPACE = 1, MSGOPT_NO_STANDARD_DESCRIPTOR_ACCESSOR = 2 };

static const char *const kCppKeywordList[] = {
	"NULL", "alignas", "alignof", "and", "and_eq", "asm", "assert", "auto", "bitand", "bitor", "bool",
	"break", "case", "catch", "char", "class", "compl", "const", "constexpr", "const_cast", "continue",
	"decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
	"export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
	"mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
	"private", "protected", "public", "register", "reinterpret_cast", "return", "short", "signed",
	"sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this",
	"thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
	"using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "char8_t", "char16_t",
	"char32_t", "concept", "consteval", "constinit", "co_await", "co_return", "co_yield", "requires",
};

static bool cpp_in_list(const char *name, const char *const *list, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (str_eq(name, list[i]))
			return true;
	return false;
}

#define CPP_IN(name, list) cpp_in_list(name, list, sizeof list / sizeof *list)

bool lp_cpp_is_keyword(const char *name) { return CPP_IN(name, kCppKeywordList); }

char *lp_cpp_resolve_keyword(const char *name)
{
	if (lp_cpp_is_keyword(name))
		return xcat(name, "_");
	return xstrdup(name);
}

static const char *const kFileScopeKnownNames[] = {"swap"};
static const char *const kMessageKnownMethodsCamelCase[] = {
	"GetDescriptor", "GetReflection", "default_instance", "Swap", "UnsafeArenaSwap", "New",
	"CopyFrom", "MergeFrom", "IsInitialized", "GetMetadata", "Clear",
};
static const char *const kMessageKnownNullaryMethodsSnakeCase[] = {
	"unknown_fields", "mutable_unknown_fields", "descriptor", "default_instance",
};
static const char *const kMessageKnownNonNullaryMethodsSnakeCase[] = {"swap"};

char *lp_cpp_resolve_known_name_collisions(const char *name, int name_context, int name_kind)
{
	bool conflict = lp_cpp_is_keyword(name);
	if (!conflict) {
		switch (name_kind) {
		case LP_CPP_KIND_TYPE:
			conflict = CPP_IN(name, kMessageKnownMethodsCamelCase);
			break;
		case LP_CPP_KIND_VALUE:
			if (name_context == LP_CPP_CTX_FILE)
				conflict = CPP_IN(name, kFileScopeKnownNames);
			else
				conflict = CPP_IN(name, kMessageKnownNullaryMethodsSnakeCase) ||
					   CPP_IN(name, kMessageKnownNonNullaryMethodsSnakeCase);
			break;
		case LP_CPP_KIND_FUNCTION:
			conflict = CPP_IN(name, kMessageKnownNullaryMethodsSnakeCase);
			break;
		}
	}
	if (conflict)
		return xcat(name, "_");
	return xstrdup(name);
}

char *lp_cpp_underscores_to_camel_case(const char *input, bool cap_next_letter)
{
	Buf result = {0};
	for (const char *c = input; *c; c++) {
		if ('a' <= *c && *c <= 'z') {
			buf_addc(&result, cap_next_letter ? (char)(*c + ('A' - 'a')) : *c);
			cap_next_letter = false;
		} else if ('A' <= *c && *c <= 'Z') {
			buf_addc(&result, *c);
			cap_next_letter = false;
		} else if ('0' <= *c && *c <= '9') {
			buf_addc(&result, *c);
			cap_next_letter = true;
		} else {
			cap_next_letter = true;
		}
	}
	return xstrdup(buf_str(&result));
}

char *lp_cpp_package_namespace(const char *package)
{
	if (!*package)
		return xstrdup("");
	Buf b = {0};
	Vec scope = str_split(package, '.');
	for (int i = 0; i < scope.n; i++) {
		const char *word = scope.v[i];
		if (!*word)
			continue; /* SkipEmpty */
		buf_adds(&b, "::");
		buf_adds(&b, lp_cpp_resolve_keyword(word));
	}
	return xstrdup(buf_str(&b));
}

char *lp_cpp_validate_cc_namespace(const LpFile *file)
{
	const PbMsg *cpp = pb_msg(file->options, FILEOPT_PB_FILE_CPP);
	if (!pb_has(cpp, CPP_FILE_OPTIONS_NAMESPACE))
		return NULL;
	const char *cc_namespace = pb_str(cpp, CPP_FILE_OPTIONS_NAMESPACE);
	if (has_prefix(cc_namespace, "::"))
		return xcat("Namespace ", cc_namespace, " can not start with `::`.");
	/* StrSplit(cc_namespace, "::", SkipEmpty): every character of the names */
	size_t n = strlen(cc_namespace);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)cc_namespace[i];
		if (c == ':' && i + 1 < n && cc_namespace[i + 1] == ':') {
			i++;
			continue;
		}
		if (!(ascii_isalnum(c) || c == '_'))
			return xcat("Namespace ", cc_namespace, " contains invalid characters.");
	}
	return NULL;
}

char *lp_cpp_namespace(const LpFile *file)
{
	const PbMsg *cpp = pb_msg(file->options, FILEOPT_PB_FILE_CPP);
	if (pb_has(cpp, CPP_FILE_OPTIONS_NAMESPACE)) {
		const char *cc_namespace = pb_str(cpp, CPP_FILE_OPTIONS_NAMESPACE);
		if (!*cc_namespace)
			return xstrdup("");
		return xcat("::", cc_namespace);
	}
	return lp_cpp_package_namespace(file->package);
}

char *lp_cpp_class_name(const LpMessage *descriptor)
{
	const char *parent = descriptor->containing_type ? lp_cpp_class_name(descriptor->containing_type) : NULL;
	char *res = parent ? xcat(parent, "_", descriptor->name) : xstrdup(descriptor->name);
	if (proto_message_is_map_entry(descriptor))
		res = xcat(res, "_DoNotUse");
	return lp_cpp_resolve_known_name_collisions(res, LP_CPP_CTX_FILE, LP_CPP_KIND_TYPE);
}

char *lp_cpp_enum_class_name(const LpEnum *enum_descriptor)
{
	if (!enum_descriptor->containing_type)
		return lp_cpp_resolve_known_name_collisions(enum_descriptor->name, LP_CPP_CTX_FILE, LP_CPP_KIND_TYPE);
	return xcat(lp_cpp_class_name(enum_descriptor->containing_type), "_", enum_descriptor->name);
}

char *lp_cpp_qualified_file_level_symbol(const LpFile *file, const char *name)
{
	if (!*file->package)
		return xcat("::", name);
	return xcat(lp_cpp_namespace(file), "::", name);
}

char *lp_cpp_qualified_class_name(const LpMessage *d)
{
	return lp_cpp_qualified_file_level_symbol(d->file, lp_cpp_class_name(d));
}

char *lp_cpp_qualified_enum_class_name(const LpEnum *d)
{
	return lp_cpp_qualified_file_level_symbol(d->file, lp_cpp_enum_class_name(d));
}

char *lp_cpp_extension_name(const LpField *d)
{
	if (d->extension_scope)
		return xcat(lp_cpp_class_name(d->extension_scope), "::",
			    lp_cpp_resolve_known_name_collisions(d->name, LP_CPP_CTX_MESSAGE, LP_CPP_KIND_VALUE));
	return lp_cpp_resolve_known_name_collisions(d->name, LP_CPP_CTX_FILE, LP_CPP_KIND_VALUE);
}

char *lp_cpp_qualified_extension_name(const LpField *d)
{
	return lp_cpp_qualified_file_level_symbol(d->file, lp_cpp_extension_name(d));
}

char *lp_cpp_field_name(const LpField *field)
{
	if (field->containing_type && pb_int(field->containing_type->options, MSGOPT_NO_STANDARD_DESCRIPTOR_ACCESSOR, 0) &&
	    str_eq(field->name, "descriptor"))
		return xstrdup("descriptor");
	char *result = xstrdup(field->name);
	for (char *c = result; *c; c++)
		*c = ascii_tolower(*c);
	return lp_cpp_resolve_known_name_collisions(result, LP_CPP_CTX_MESSAGE, LP_CPP_KIND_FUNCTION);
}

char *lp_cpp_oneof_case_constant_name(const LpField *field)
{
	return xcat("k", lp_cpp_underscores_to_camel_case(field->name, true));
}

char *lp_cpp_qualified_oneof_case_constant_name(const LpField *field)
{
	return xcat(lp_cpp_qualified_class_name(field->containing_type), "::", lp_cpp_oneof_case_constant_name(field));
}

char *lp_cpp_enum_value_name(const LpEnumValue *enum_value) { return lp_cpp_resolve_keyword(enum_value->name); }
