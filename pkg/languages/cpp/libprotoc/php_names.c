// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/php/names.cc (protobuf v36.2), see php_names.h. The words
 * an older version does not reserve are in its LpVersion (php_unreserved_names). */
#include "php_names.h"

enum { FILEOPT_PHP_CLASS_PREFIX = 40 };

// BEGIN RESERVED NAMES
// @see php/update_reserved_words.sh - DO NOT MODIFY THIS LIST MANUALLY
static const char *const kReservedNames[] = {
	"abstract",  "and",          "array",     "as",           "bool",
	"break",     "callable",     "case",      "catch",        "class",
	"clone",     "const",        "continue",  "declare",      "default",
	"die",       "do",           "echo",      "else",         "elseif",
	"empty",     "enddeclare",   "endfor",    "endforeach",   "endif",
	"endswitch", "endwhile",     "eval",      "exit",         "extends",
	"false",     "final",        "finally",   "float",        "fn",
	"for",       "foreach",      "function",  "global",       "goto",
	"if",        "implements",   "include",   "include_once", "instanceof",
	"insteadof", "int",          "interface", "isset",        "iterable",
	"list",      "match",        "mixed",     "namespace",    "never",
	"new",       "null",         "object",    "or",           "parent",
	"print",     "private",      "protected", "public",       "readonly",
	"require",   "require_once", "return",    "self",         "static",
	"string",    "switch",       "throw",     "trait",        "true",
	"try",       "unset",        "use",       "var",          "void",
	"while",     "xor",          "yield"};
// END RESERVED NAMES

/* IsReservedName with the reserved words of a protobuf version: kReservedNames without
 * unreserved (LpVersion.php_unreserved_names, NULL: none). */
static bool php_is_reserved_name(const char *name, const char *const *unreserved)
{
	/* the longest reserved name has 12 characters */
	char lower[16];
	size_t n = strlen(name);
	if (n >= sizeof lower)
		return false;
	for (size_t i = 0; i <= n; i++)
		lower[i] = ascii_tolower(name[i]);
	for (const char *const *u = unreserved; u && *u; u++)
		if (str_eq(lower, *u))
			return false;
	for (size_t i = 0; i < sizeof kReservedNames / sizeof *kReservedNames; i++)
		if (str_eq(lower, kReservedNames[i]))
			return true;
	return false;
}

bool lp_php_is_reserved_name(const char *name) { return php_is_reserved_name(name, NULL); }

const char *lp_php_reserved_name_prefix(const char *classname, const LpFile *file)
{
	const LpVersion *v = lp_file_pool(file)->version;
	if (php_is_reserved_name(classname, v ? v->php_unreserved_names : NULL))
		return str_eq(file->package, "google.protobuf") ? "GPB" : "PB";
	return "";
}

/* ClassNamePrefixImpl */
const char *lp_php_class_name_prefix(const char *classname, const ProtoDesc *desc)
{
	const LpFile *file = proto_desc_file(desc);
	const char *prefix = pb_str(file->options, FILEOPT_PHP_CLASS_PREFIX);
	if (*prefix)
		return prefix;
	return lp_php_reserved_name_prefix(classname, file);
}

/* GeneratedClassNameImpl for messages and enums: the prefix of the containing types is computed
 * with desc (only its file matters). */
static char *php_generated_class_name(const ProtoDesc *desc, const LpMessage *containing)
{
	char *classname = xcat(lp_php_class_name_prefix(desc->name, desc), desc->name);
	for (; containing; containing = containing->containing_type)
		classname = xcat(lp_php_class_name_prefix(containing->name, desc), containing->name, "\\", classname);
	return classname;
}

char *lp_php_generated_class_name_message(const LpMessage *desc)
{
	return php_generated_class_name((const ProtoDesc *)desc, desc->containing_type);
}

char *lp_php_generated_class_name_enum(const LpEnum *desc)
{
	return php_generated_class_name((const ProtoDesc *)desc, desc->containing_type);
}

char *lp_php_generated_class_name_service(const LpService *desc)
{
	return xcat(lp_php_class_name_prefix(desc->name, (const ProtoDesc *)desc), desc->name);
}
