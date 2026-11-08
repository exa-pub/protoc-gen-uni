// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/names.cc and nsobject_methods.h (protobuf
 * v36.2), see objectivec_names.h. The files of the prefix options are not read (line_consumer.cc
 * is not ported). */
#include "objectivec_names.h"

enum { FILEOPT_OBJC_CLASS_PREFIX = 36 };

/* PrefixModeStorage, without the mappings and exceptions files. */
static struct {
	bool use_package_name;
	const char *forced_prefix;
} g_prefix_mode;

void lp_objc_reset_prefix_mode(void)
{
	g_prefix_mode.use_package_name = false;
	g_prefix_mode.forced_prefix = "";
}

void lp_objc_set_use_package_as_default_prefix(bool on_or_off) { g_prefix_mode.use_package_name = on_or_off; }

void lp_objc_set_forced_package_prefix(const char *prefix) { g_prefix_mode.forced_prefix = xstrdup(prefix); }

static bool objc_ascii_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static bool objc_ascii_islower(char c) { return c >= 'a' && c <= 'z'; }

/* UnderscoresToCamelCase: segments start at digits after non-digits, at lower-case letters after
 * non-letters and at upper-case letters after non-upper-case; other characters end a segment.
 * "url", "http" and "https" segments are upper-cased. */
static char *underscores_to_camel_case(const char *input, bool first_capitalized)
{
	Vec values = {0};
	Buf current = {0};
	bool last_char_was_number = false, last_char_was_lower = false, last_char_was_upper = false;
	for (const char *p = input; *p; p++) {
		char c = *p;
		if (ascii_isdigit((unsigned char)c)) {
			if (!last_char_was_number) {
				vec_push(&values, xstrdup(buf_str(&current)));
				current.n = 0;
			}
			buf_addc(&current, c);
			last_char_was_number = true;
			last_char_was_lower = last_char_was_upper = false;
		} else if (objc_ascii_islower(c)) {
			if (!last_char_was_lower && !last_char_was_upper) {
				vec_push(&values, xstrdup(buf_str(&current)));
				current.n = 0;
			}
			buf_addc(&current, c);
			last_char_was_number = last_char_was_upper = false;
			last_char_was_lower = true;
		} else if (objc_ascii_isupper(c)) {
			if (!last_char_was_upper) {
				vec_push(&values, xstrdup(buf_str(&current)));
				current.n = 0;
			}
			buf_addc(&current, ascii_tolower(c));
			last_char_was_number = last_char_was_lower = false;
			last_char_was_upper = true;
		} else {
			last_char_was_number = last_char_was_lower = last_char_was_upper = false;
		}
	}
	vec_push(&values, xstrdup(buf_str(&current)));

	Buf result = {0};
	bool first_segment_forces_upper = false;
	for (int i = 0; i < values.n; i++) {
		char *value = values.v[i];
		bool all_upper = str_eq(value, "url") || str_eq(value, "http") || str_eq(value, "https");
		if (all_upper && result.n == 0)
			first_segment_forces_upper = true;
		if (all_upper) {
			for (char *q = value; *q; q++)
				*q = ascii_toupper(*q);
		} else if (*value) {
			value[0] = ascii_toupper(value[0]);
		}
		buf_adds(&result, value);
	}
	char *s = (char *)buf_str(&result);
	if (result.n && !first_capitalized && !first_segment_forces_upper)
		s[0] = ascii_tolower(s[0]);
	return s;
}

/* kReservedWordList and kNSObjectMethodsList (nsobject_methods.h) together, in byte order. */
static const char *const kReservedWords[] = {
	"AbsoluteTime", "ByteCount", "ByteOffset", "CAMLType", "CA_copyRenderValue",
	"CA_prepareRenderValue", "Category", "Duration", "Fixed", "FixedPoint", "FixedRect", "Fract",
	"ItemCount", "Ivar", "LangCode", "LogicalAddress", "Method", "NO", "NS_copyCGImage",
	"NS_tiledLayerVisibleRect", "NULL", "Nil", "OSType", "OptionBits", "PBVersion", "PhysicalAddress",
	"Point", "ProcessSerialNumber", "Protocol", "Rect", "RegionCode", "ScriptCode", "Size", "Style",
	"StyleField", "StyleParameter", "TimeBase", "TimeRecord", "TimeScale", "YES", "___tryRetain_OA",
	"__autorelease_OA", "__dealloc_zombie", "__release_OA", "__retain_OA", "_accessibilityFinalize",
	"_accessibilityIsTableViewDescendant", "_accessibilityUIElementSpecifier",
	"_accessibilityUseConvenienceAPI", "_allowsDirectEncoding", "_asScriptTerminologyNameArray",
	"_asScriptTerminologyNameString", "_bindingAdaptor", "_cfTypeID", "_cmd", "_copyDescription",
	"_destroyObserverList", "_didEndKeyValueObserving", "_implicitObservationInfo",
	"_internalAccessibilityAttributedHint", "_internalAccessibilityAttributedLabel",
	"_internalAccessibilityAttributedValue", "_isAXConnector",
	"_isAccessibilityContainerSectionCandidate", "_isAccessibilityContentNavigatorSectionCandidate",
	"_isAccessibilityContentSectionCandidate", "_isAccessibilityTopLevelNavigatorSectionCandidate",
	"_isDeallocating", "_isKVOA", "_isToManyChangeInformation", "_ivarDescription",
	"_localClassNameForClass", "_methodDescription", "_observerStorage",
	"_overrideUseFastBlockObservers", "_propertyDescription", "_releaseBindingAdaptor",
	"_scriptingCount", "_scriptingCountNonrecursively", "_scriptingDebugDescription",
	"_scriptingExists", "_scriptingShouldCheckObjectIndexes", "_shortMethodDescription",
	"_shouldSearchChildrenForSection", "_traitStorageList", "_tryRetain", "_ui_descriptionBuilder",
	"_uikit_variesByTraitCollections", "_web_description", "_webkit_invokeOnMainThread",
	"_willBeginKeyValueObserving", "accessibilityActivate", "accessibilityActivationPoint",
	"accessibilityAllowsOverriddenAttributesWhenIgnored",
	"accessibilityAssistiveTechnologyFocusedIdentifiers", "accessibilityAttributedHint",
	"accessibilityAttributedLabel", "accessibilityAttributedValue", "accessibilityContainer",
	"accessibilityContainerType", "accessibilityCustomActions", "accessibilityCustomRotors",
	"accessibilityDecrement", "accessibilityDragSourceDescriptors",
	"accessibilityDropPointDescriptors", "accessibilityElementCount",
	"accessibilityElementDidBecomeFocused", "accessibilityElementDidLoseFocus",
	"accessibilityElementIsFocused", "accessibilityElements", "accessibilityElementsHidden",
	"accessibilityFrame", "accessibilityHeaderElements", "accessibilityHint",
	"accessibilityIdentification", "accessibilityIdentifier", "accessibilityIncrement",
	"accessibilityLabel", "accessibilityLanguage", "accessibilityLocalizedStringKey",
	"accessibilityNavigationStyle", "accessibilityOverriddenAttributes",
	"accessibilityParameterizedAttributeNames", "accessibilityPath", "accessibilityPerformEscape",
	"accessibilityPerformMagicTap", "accessibilityPresenterProcessIdentifier",
	"accessibilityShouldUseUniqueId", "accessibilitySupportsNotifications",
	"accessibilitySupportsOverriddenAttributes", "accessibilityTemporaryChildren",
	"accessibilityTraits", "accessibilityValue", "accessibilityViewIsModal",
	"accessibilityVisibleArea", "alignas", "alignof", "allPropertyKeys", "allowsWeakReference", "and",
	"and_eq", "asm", "attributeKeys", "auto", "autoContentAccessingProxy", "autorelease",
	"awakeFromNib", "bitand", "bitor", "bool", "boolValueSafe", "break", "bs_encoded",
	"bs_isPlistableType", "bs_secureEncoded", "bycopy", "byref", "case", "catch", "char", "char16_t",
	"char32_t", "cl_json_serializeKey", "class", "classCode", "classDescription", "classForArchiver",
	"classForCoder", "classForKeyedArchiver", "classForPortCoder", "className", "clear",
	"clearProperties", "compl", "const", "const_cast", "constexpr", "continue", "copy", "data",
	"dealloc", "debugDescription", "decltype", "default", "defaultAccessibilityTraits", "delete",
	"delimitedData", "description", "descriptor", "double", "doubleValueSafe", "dynamic_cast", "else",
	"entityName", "enum", "explicit", "export", "exposedBindings", "extensionRegistry",
	"extensionsCurrentlySet", "extern ", "false", "finalize", "finishObserving", "float",
	"flushKeyBindings", "for", "friend", "goto", "hash", "id", "if", "in", "init", "initialized",
	"inline", "inout", "instancetype", "int", "int64ValueSafe", "isAccessibilityElement",
	"isAccessibilityElementByDefault", "isElementAccessibilityExposedToInterfaceBuilder", "isFault",
	"isInitialized", "isNSArray__", "isNSCFConstantString__", "isNSData__", "isNSDate__",
	"isNSDictionary__", "isNSNumber__", "isNSObject__", "isNSOrderedSet__", "isNSSet__",
	"isNSString__", "isNSTimeZone__", "isNSValue__", "isProxy", "long", "mutable", "mutableCopy",
	"namespace", "new", "nil", "nilValueForKey", "noexcept", "nonnull", "not", "not_eq", "nullable",
	"nullptr", "objectSpecifier", "observationInfo", "oneway", "operator", "or", "or_eq", "out",
	"pep_onDetachedThread", "pep_onMainThread", "pep_onMainThreadIfNecessary",
	"prepareForInterfaceBuilder", "private", "protected", "public", "register", "reinterpret_cast",
	"release", "releaseOnMainThread", "restrict", "retain", "retainCount", "retainWeakReference",
	"return", "scriptingProperties", "self", "serializedSize", "short",
	"shouldGroupAccessibilityChildren", "signed", "sizeof", "sortedExtensionsInUse", "static",
	"static_assert", "static_cast", "stderr", "stdin", "stdout", "storedAccessibilityActivationPoint",
	"storedAccessibilityContainerType", "storedAccessibilityElementsHidden",
	"storedAccessibilityFrame", "storedAccessibilityNavigationStyle", "storedAccessibilityTraits",
	"storedAccessibilityViewIsModal", "storedIsAccessibilityElement",
	"storedShouldGroupAccessibilityChildren", "stringValueSafe", "struct", "super", "superclass",
	"switch", "template", "this", "thread_local", "throw", "toManyRelationshipKeys",
	"toOneRelationshipKeys", "traitStorageList", "true", "try", "typedef", "typeid", "typename",
	"typeof", "un_safeBoolValue", "union", "unknownFields", "unsigned", "userInterfaceItemIdentifier",
	"using", "utf8ValueSafe", "valuesForKeysWithDictionary", "virtual", "void", "volatile", "wchar_t",
	"weak", "while", "xor", "xor_eq", "zone",
};

static int objc_strcmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static bool is_reserved_word(const char *s)
{
	return bsearch(&s, kReservedWords, sizeof kReservedWords / sizeof *kReservedWords, sizeof *kReservedWords,
		       objc_strcmp) != NULL;
}

/* IsReservedCIdentifier: __ or _[A-Z] at the start (and longer than 2). */
static bool is_reserved_c_identifier(const char *input)
{
	return strlen(input) > 2 && input[0] == '_' && (objc_ascii_isupper(input[1]) || input[1] == '_');
}

/* SanitizeNameForObjC */
static char *sanitize_name_for_objc(const char *prefix, const char *input, const char *extension)
{
	char *sanitized;
	size_t pn = strlen(prefix);
	if (has_prefix(input, prefix)) {
		if (strlen(input) == pn || !objc_ascii_isupper(input[pn]))
			sanitized = xcat(prefix, input);
		else
			sanitized = xstrdup(input);
	} else {
		sanitized = xcat(prefix, input);
	}
	if (is_reserved_c_identifier(sanitized) || is_reserved_word(sanitized))
		return xcat(sanitized, extension);
	return sanitized;
}

static const char *name_from_field_descriptor(const LpField *field)
{
	return lp_field_is_group_like(field) ? field->message_type->name : field->name;
}

/* PathSplit: the basename (directory: NULL to skip). */
static const char *path_split(const char *path, char **directory)
{
	const char *slash = strrchr(path, '/');
	if (directory)
		*directory = slash ? xstrndup(path, (size_t)(slash - path)) : xstrdup("");
	return slash ? slash + 1 : path;
}

/* IsSpecialNamePrefix */
static bool is_special_name_prefix(const char *name, const char *const *special_names, int n)
{
	for (int i = 0; i < n; i++) {
		size_t length = strlen(special_names[i]);
		if (strncmp(name, special_names[i], length) == 0)
			return strlen(name) > length ? !objc_ascii_islower(name[length]) : true;
	}
	return false;
}

/* ProtoPackageToCamelCase */
static char *proto_package_to_camel_case(const char *package)
{
	Buf result = {0};
	Vec segments = str_split(package, '.');
	for (int i = 0; i < segments.n; i++) {
		const char *segment = segments.v[i];
		if (!*segment)
			continue;
		char *part = underscores_to_camel_case(segment, true);
		if (!*part)
			continue;
		if (result.n)
			buf_addc(&result, '_');
		buf_adds(&result, part);
	}
	return (char *)buf_str(&result);
}

bool lp_objc_is_retained_name(const char *name)
{
	static const char *const retained_names[] = {"new", "alloc", "copy", "mutableCopy"};
	return is_special_name_prefix(name, retained_names, 4);
}

bool lp_objc_is_init_name(const char *name)
{
	static const char *const init_names[] = {"init"};
	return is_special_name_prefix(name, init_names, 1);
}

bool lp_objc_is_create_name(const char *name)
{
	static const char *const create_names[] = {"Create", "Copy"};
	for (int i = 0; i < 2; i++) {
		size_t length = strlen(create_names[i]);
		const char *pos = strstr(name, create_names[i]);
		if (pos)
			return strlen(pos) > length ? !objc_ascii_islower(pos[length]) : true;
	}
	return false;
}

/* FileClassPrefix(file, out_is_proto_package_prefix) */
static char *file_class_prefix(const LpFile *file, bool *out_is_proto_package_prefix)
{
	if (out_is_proto_package_prefix)
		*out_is_proto_package_prefix = false;
	/* always honor the file option */
	if (pb_has(file->options, FILEOPT_OBJC_CLASS_PREFIX))
		return xstrdup(pb_str(file->options, FILEOPT_OBJC_CLASS_PREFIX));
	/* (no prefix to proto package mappings file) */
	if (!g_prefix_mode.use_package_name)
		return xstrdup("");
	/* (no package exceptions file) */
	char *result = proto_package_to_camel_case(file->package);
	if (*result)
		result = xcat(result, "_");
	if (out_is_proto_package_prefix)
		*out_is_proto_package_prefix = true;
	return xcat(g_prefix_mode.forced_prefix, result);
}

char *lp_objc_file_class_prefix(const LpFile *file) { return file_class_prefix(file, NULL); }

char *lp_objc_file_path(const LpFile *file)
{
	char *directory;
	const char *basename = path_split(file->name, &directory);
	const char *output = *directory ? xcat(directory, "/") : "";
	return xcat(output, underscores_to_camel_case(lp_strip_proto(basename), true));
}

char *lp_objc_file_path_basename(const LpFile *file)
{
	return underscores_to_camel_case(lp_strip_proto(path_split(file->name, NULL)), true);
}

char *lp_objc_file_class_name(const LpFile *file)
{
	char *prefix = lp_objc_file_class_prefix(file);
	char *name = xcat(underscores_to_camel_case(lp_strip_proto(path_split(file->name, NULL)), true), "Root");
	return sanitize_name_for_objc(prefix, name, "_RootClass");
}

char *lp_objc_extension_registry_function_name(const LpFile *file)
{
	return lp_objc_file_unique_symbol_name(file, "Registry");
}

char *lp_objc_file_unique_symbol_name(const LpFile *file, const char *suffix)
{
	/* [FileClassPrefix]_[PackageDerivedString]_[FileClassName]_[suffix] */
	Vec parts = {0};
	char *prefix = lp_objc_file_class_prefix(file);
	if (*prefix)
		vec_push(&parts, prefix);
	char *package_derived_string = proto_package_to_camel_case(file->package);
	if (*package_derived_string)
		vec_push(&parts, package_derived_string);
	char *file_class_name = lp_objc_file_class_name(file);
	if (*file_class_name)
		vec_push(&parts, file_class_name);
	if (*suffix)
		vec_push(&parts, (void *)suffix);
	return sanitize_name_for_objc("", str_join(&parts, "_"), "_");
}

/* ClassNameWorker */
static char *class_name_worker(const LpMessage *containing, const char *name)
{
	if (containing)
		return xcat(class_name_worker(containing->containing_type, containing->name), "_", name);
	return xstrdup(name);
}

char *lp_objc_class_name(const LpMessage *descriptor)
{
	char *prefix = lp_objc_file_class_prefix(descriptor->file);
	char *name = class_name_worker(descriptor->containing_type, descriptor->name);
	return sanitize_name_for_objc(prefix, name, "_Class");
}

char *lp_objc_enum_name(const LpEnum *descriptor)
{
	char *prefix = lp_objc_file_class_prefix(descriptor->file);
	char *name = class_name_worker(descriptor->containing_type, descriptor->name);
	return sanitize_name_for_objc(prefix, name, "_Enum");
}

char *lp_objc_enum_value_name(const LpEnumValue *descriptor)
{
	char *class_name = lp_objc_enum_name(descriptor->type);
	char *value_str = underscores_to_camel_case(descriptor->name, true);
	return sanitize_name_for_objc("", xcat(class_name, "_", value_str), "_Value");
}

char *lp_objc_enum_value_short_name(const LpEnumValue *descriptor)
{
	char *long_name_prefix = xcat(lp_objc_enum_name(descriptor->type), "_");
	char *long_name = lp_objc_enum_value_name(descriptor);
	return has_prefix(long_name, long_name_prefix) ? long_name + strlen(long_name_prefix) : long_name;
}

char *lp_objc_un_camel_case_enum_short_name(const char *name)
{
	Buf result = {0};
	for (int i = 0; name[i]; i++) {
		if (i > 0 && objc_ascii_isupper(name[i]))
			buf_addc(&result, '_');
		buf_addc(&result, ascii_toupper(name[i]));
	}
	return (char *)buf_str(&result);
}

char *lp_objc_extension_class_name(const LpField *descriptor)
{
	if (!descriptor->extension_scope)
		return lp_objc_file_class_name(descriptor->file);
	return lp_objc_class_name(descriptor->extension_scope);
}

char *lp_objc_extension_function_name(const LpField *descriptor)
{
	char *camel_case_name = underscores_to_camel_case(name_from_field_descriptor(descriptor), true);
	Vec elements = {0};
	if (!descriptor->extension_scope) {
		/* <FileClassPrefix>_<ProtoPackageCamelCase>_extension_<NameCamelCase> */
		bool is_proto_package_prefix = false;
		char *prefix = file_class_prefix(descriptor->file, &is_proto_package_prefix);
		if (*prefix) {
			if (is_proto_package_prefix)
				prefix = str_trim_suffix(prefix, "_");
			vec_push(&elements, prefix);
		}
		if (!is_proto_package_prefix)
			vec_push(&elements, proto_package_to_camel_case(descriptor->file->package));
	} else {
		/* <ScopeMessageCamelCase>_extension_<NameCamelCase> */
		vec_push(&elements, lp_objc_class_name(descriptor->extension_scope));
	}
	vec_push(&elements, "extension");
	vec_push(&elements, camel_case_name);
	return sanitize_name_for_objc("", str_join(&elements, "_"), "_Extension");
}

char *lp_objc_extension_method_name(const LpField *descriptor)
{
	char *result = underscores_to_camel_case(name_from_field_descriptor(descriptor), false);
	return sanitize_name_for_objc("", result, "_Extension");
}

char *lp_objc_field_name(const LpField *field)
{
	char *result = underscores_to_camel_case(name_from_field_descriptor(field), false);
	if (proto_field_is_repeated(field) && !proto_field_is_map(field))
		result = xcat(result, "Array"); /* before the check for reserved words */
	else if (has_suffix(result, "Array"))
		result = xcat(result, "_p");
	return sanitize_name_for_objc("", result, "_p");
}

char *lp_objc_field_name_capitalized(const LpField *field)
{
	char *result = lp_objc_field_name(field);
	if (*result)
		result[0] = ascii_toupper(result[0]);
	return result;
}

/* FragmentName */
enum { FRAGMENT_COMMON, FRAGMENT_MAP_KEY, FRAGMENT_OBJC_GENERICS };

static const char *fragment_name(const LpField *field, int mode)
{
	switch (field->type) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "Int32";
	case PT_UINT32: case PT_FIXED32: return "UInt32";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "Int64";
	case PT_UINT64: case PT_FIXED64: return "UInt64";
	case PT_FLOAT: return "Float";
	case PT_DOUBLE: return "Double";
	case PT_BOOL: return "Bool";
	case PT_STRING:
		return mode == FRAGMENT_COMMON ? "Object" : mode == FRAGMENT_MAP_KEY ? "String" : "NSString*";
	case PT_BYTES: return mode == FRAGMENT_OBJC_GENERICS ? "NSData*" : "Object";
	case PT_ENUM: return "Enum";
	case PT_GROUP: case PT_MESSAGE:
		return mode == FRAGMENT_OBJC_GENERICS ? xcat(lp_objc_class_name(field->message_type), "*") : "Object";
	}
	fail("Can't get here.");
}

static bool is_object_type(int type) { return type == PT_STRING || type == PT_BYTES || type == PT_GROUP || type == PT_MESSAGE; }

/* FieldObjCTypeInternal; generics NULL: omit them. */
static const char *field_objc_type_internal(const LpField *field, bool *out_is_ptr, const char **out_generics)
{
	if (proto_field_is_map(field)) {
		*out_is_ptr = true;
		const LpField *key_field = proto_message_field_by_number(field->message_type, 1);
		const LpField *value_field = proto_message_field_by_number(field->message_type, 2);
		bool value_is_object = is_object_type(value_field->type);
		if (value_is_object && key_field->type == PT_STRING) {
			if (out_generics)
				*out_generics = xcat("<NSString*, ", fragment_name(value_field, FRAGMENT_OBJC_GENERICS), ">");
			return "NSMutableDictionary";
		}
		if (value_is_object && out_generics)
			*out_generics = xcat("<", fragment_name(value_field, FRAGMENT_OBJC_GENERICS), ">");
		return xcat("GPB", fragment_name(key_field, FRAGMENT_MAP_KEY), fragment_name(value_field, FRAGMENT_COMMON),
			    "Dictionary");
	}
	if (proto_field_is_repeated(field)) {
		*out_is_ptr = true;
		if (is_object_type(field->type)) {
			if (out_generics)
				*out_generics = xcat("<", fragment_name(field, FRAGMENT_OBJC_GENERICS), ">");
			return "NSMutableArray";
		}
		return xcat("GPB", fragment_name(field, FRAGMENT_COMMON), "Array");
	}
	*out_is_ptr = false;
	switch (field->type) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "int32_t";
	case PT_UINT32: case PT_FIXED32: return "uint32_t";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "int64_t";
	case PT_UINT64: case PT_FIXED64: return "uint64_t";
	case PT_FLOAT: return "float";
	case PT_DOUBLE: return "double";
	case PT_BOOL: return "BOOL";
	case PT_STRING: *out_is_ptr = true; return "NSString";
	case PT_BYTES: *out_is_ptr = true; return "NSData";
	case PT_ENUM: return lp_objc_enum_name(field->enum_type);
	case PT_GROUP: case PT_MESSAGE: *out_is_ptr = true; return lp_objc_class_name(field->message_type);
	}
	fail("Can't get here.");
}

char *lp_objc_field_objc_type(const LpField *field, unsigned options)
{
	const char *generics = "";
	bool is_ptr;
	const char *base_type = field_objc_type_internal(
		field, &is_ptr, (options & LP_OBJC_TYPE_OMIT_LIGHTWEIGHT_GENERICS) ? NULL : &generics);
	if (!is_ptr)
		return xcat(base_type, (options & LP_OBJC_TYPE_INCLUDE_SPACE_AFTER_BASIC_TYPES) ? " " : "");
	return xcat(base_type, generics, (options & LP_OBJC_TYPE_INCLUDE_SPACE_BEFORE_STAR) ? " *" : "*");
}

char *lp_objc_oneof_enum_name(const LpOneof *descriptor)
{
	return xcat(lp_objc_class_name(descriptor->containing_type), "_",
		    underscores_to_camel_case(descriptor->name, true), "_OneOfCase");
}

char *lp_objc_oneof_name(const LpOneof *descriptor) { return underscores_to_camel_case(descriptor->name, false); }

char *lp_objc_oneof_name_capitalized(const LpOneof *descriptor)
{
	char *result = lp_objc_oneof_name(descriptor);
	if (*result)
		result[0] = ascii_toupper(result[0]);
	return result;
}

char *lp_objc_un_camel_case_field_name(const char *name, const LpField *field)
{
	char *worker = has_suffix(name, "_p") ? str_trim_suffix(name, "_p") : xstrdup(name);
	if (proto_field_is_repeated(field) && has_suffix(worker, "Array"))
		worker = str_trim_suffix(worker, "Array");
	if (lp_field_is_group_like(field)) {
		if (*worker && objc_ascii_islower(worker[0]))
			worker[0] = ascii_toupper(worker[0]);
		return worker;
	}
	Buf result = {0};
	for (int i = 0; worker[i]; i++) {
		char c = worker[i];
		if (objc_ascii_isupper(c)) {
			if (i > 0)
				buf_addc(&result, '_');
			buf_addc(&result, ascii_tolower(c));
		} else {
			buf_addc(&result, c);
		}
	}
	return (char *)buf_str(&result);
}

char *lp_objc_protobuf_framework_import_symbol(const char *framework_name)
{
	char *upper = xstrdup(framework_name);
	for (char *q = upper; *q; q++)
		*q = ascii_toupper(*q);
	return xcat("GPB_USE_", upper, "_FRAMEWORK_IMPORTS");
}

bool lp_objc_is_protobuf_library_bundled_proto_file(const LpFile *file)
{
	/* the WKTs shipped generated by the library (not descriptor.proto) */
	static const char *const names[] = {
		"google/protobuf/any.proto",          "google/protobuf/api.proto",
		"google/protobuf/duration.proto",     "google/protobuf/empty.proto",
		"google/protobuf/field_mask.proto",   "google/protobuf/source_context.proto",
		"google/protobuf/struct.proto",       "google/protobuf/timestamp.proto",
		"google/protobuf/type.proto",         "google/protobuf/wrappers.proto",
	};
	for (int i = 0; i < 10; i++)
		if (str_eq(file->name, names[i]))
			return true;
	return false;
}

/* ValidateObjCClassPrefix without an expected prefixes file: only require_prefixes can fail
 * (the warnings go to stderr upstream). */
static const char *validate_objc_class_prefix(const LpFile *file, bool require_prefixes)
{
	if (!pb_has(file->options, FILEOPT_OBJC_CLASS_PREFIX) && require_prefixes)
		return xcat("error: '", file->name, "' does not have a required 'option objc_class_prefix'.");
	return NULL;
}

const char *lp_objc_validate_objc_class_prefixes(LpFile *const *files, int n, const LpObjcValidationOptions *options)
{
	/* '-' disables even the most basic checks */
	if (str_eq(options->expected_prefixes_path, "-"))
		return NULL;
	for (int i = 0; i < n; i++) {
		bool should_skip = false;
		for (int j = 0; j < options->n_expected_prefixes_suppressions; j++)
			if (str_eq(options->expected_prefixes_suppressions[j], files[i]->name))
				should_skip = true;
		if (should_skip)
			continue;
		const char *err = validate_objc_class_prefix(files[i], options->require_prefixes);
		if (err)
			return err;
	}
	return NULL;
}
