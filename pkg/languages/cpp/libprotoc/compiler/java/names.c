// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/names.{h,cc} and names_internal.h (open source
 * runtime: no "com.google.protos" package prefix) and name_resolver.{h,cc} (ClassNameResolver,
 * whose memoized NeedsOuterClassSuffix is kept per file). */

/* ---- names.cc ---- */

static bool is_reserved_name(const char *name)
{
	static const char *const names[] = {
		"abstract", "assert",     "boolean",  "break",     "byte",       "case",    "catch",
		"char",     "class",      "const",    "continue",  "default",    "do",      "double",
		"else",     "enum",       "extends",  "false",     "final",      "finally", "float",
		"for",      "goto",       "if",       "implements", "import",    "instanceof", "int",
		"interface", "java",      "long",     "native",    "new",        "null",    "package",
		"private",  "protected",  "public",   "return",    "short",      "static",  "strictfp",
		"super",    "switch",     "synchronized", "this",  "throw",      "throws",  "transient",
		"true",     "try",        "void",     "volatile",  "while",
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (str_eq(names[i], name))
			return true;
	return false;
}

static bool is_forbidden(const char *field_name)
{
	static const char *const names[] = {
		"Class", "DefaultInstanceForType", "ParserForType", "SerializedSize", "AllFields",
		"DescriptorForType", "InitializationErrorString", "UnknownFields", "CachedSize",
	};
	char *cc = jv_underscores_to_camel_case(field_name, true);
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (str_eq(names[i], cc))
			return true;
	return false;
}

/* internal::cpp::IsGroupLike */
static bool is_group_like(const LpField *field)
{
	if (field->type != PT_GROUP || !field->message_type)
		return false;
	const LpMessage *m = field->message_type;
	char *lower = xstrdup(m->name);
	for (char *p = lower; *p; p++)
		*p = jv_ascii_tolower(*p);
	if (!str_eq(field->name, lower))
		return false;
	if (m->file != field->file)
		return false;
	return field->is_extension ? m->containing_type == field->extension_scope
				   : m->containing_type == field->containing_type;
}

static char *field_name(const LpField *field)
{
	const char *name = is_group_like(field) ? field->message_type->name : field->name;
	if (is_forbidden(name))
		return xcat(name, "#");
	return (char *)name;
}

static bool nested_in_file_class_impl(const ProtoDesc *d, const LpFile *file)
{
	int nest_in_file_class = jv_resolved_java_feature(d, JV_NEST_IN_FILE_CLASS);
	if (nest_in_file_class == JV_NEST_UNKNOWN)
		fail("Check failed: nest_in_file_class != NEST_IN_FILE_CLASS_UNKNOWN");
	if (nest_in_file_class == JV_NEST_LEGACY)
		return !jv_opt_bool((const ProtoDesc *)file, JV_FILEOPT_JAVA_MULTIPLE_FILES);
	return nest_in_file_class == JV_NEST_YES;
}

bool jv_nested_in_file_class_impl_message(const LpMessage *m) { return nested_in_file_class_impl((const ProtoDesc *)m, m->file); }
bool jv_nested_in_file_class_impl_enum(const LpEnum *e) { return nested_in_file_class_impl((const ProtoDesc *)e, e->file); }
bool jv_nested_in_file_class_impl_service(const LpService *s) { return nested_in_file_class_impl((const ProtoDesc *)s, s->file); }

char *jv_join_package(const char *a, const char *b)
{
	if (!*a)
		return (char *)b;
	if (!*b)
		return (char *)a;
	return xcat(a, ".", b);
}

/* DefaultJavaPackage = Proto2DefaultJavaPackage = FileJavaPackage(file) */
char *jv_file_java_package(const LpFile *file)
{
	JvFileData *d = jv_file_data(file);
	if (!d->java_package) {
		if (pb_has(file->options, JV_FILEOPT_JAVA_PACKAGE))
			d->java_package = (char *)pb_str(file->options, JV_FILEOPT_JAVA_PACKAGE);
		else
			d->java_package = jv_join_package("", file->package ? file->package : "");
	}
	return d->java_package;
}

char *jv_java_package_directory(const LpFile *file) { return jv_java_package_to_dir(jv_file_java_package(file)); }

char *jv_capitalized_field_name(const LpField *field) { return jv_underscores_to_camel_case(field_name(field), true); }
char *jv_capitalized_oneof_name(const LpOneof *oneof) { return jv_underscores_to_camel_case(oneof->name, true); }
char *jv_underscores_to_camel_case_field(const LpField *field)
{
	return jv_underscores_to_camel_case(field_name(field), false);
}
char *jv_underscores_to_capitalized_camel_case(const LpField *field)
{
	return jv_underscores_to_camel_case(field_name(field), true);
}
char *jv_underscores_to_camel_case_method(const LpMethod *method)
{
	return jv_underscores_to_camel_case(method->name, false);
}

char *jv_underscores_to_camel_case_check_reserved(const LpField *field)
{
	char *name = jv_underscores_to_camel_case_field(field);
	if (is_reserved_name(name))
		return xcat(name, "_");
	return name;
}

char *jv_kotlin_factory_name(const LpMessage *descriptor) { return jv_get_kotlin_factory_name(descriptor); }

/* ---- name_resolver.cc ---- */

static bool use_old_file_class_name_default(const LpFile *file)
{
	return jv_resolved_java_feature((const ProtoDesc *)file, JV_USE_OLD_OUTER_CLASSNAME_DEFAULT) != 0;
}

static const char *strip_package_name(const char *full_name, const LpFile *file)
{
	if (!file->package || !*file->package)
		return full_name;
	size_t n = strlen(file->package) + 1;
	return strlen(full_name) >= n ? full_name + n : "";
}

static char *class_name_without_package_message(const LpMessage *d, bool immutable)
{
	(void)immutable;
	return (char *)strip_package_name(d->full_name, d->file);
}

static char *class_name_without_package_kotlin(const LpMessage *d)
{
	char *result = (char *)d->name;
	for (const LpMessage *t = d->containing_type; t; t = t->containing_type)
		result = xcat(t->name, "Kt.", result);
	return result;
}

static char *class_name_without_package_enum(const LpEnum *d, bool immutable)
{
	if (!d->containing_type)
		return (char *)d->name;
	return xcat(class_name_without_package_message(d->containing_type, immutable), ".", d->name);
}

static char *class_name_without_package_service(const LpService *d, bool immutable)
{
	(void)immutable;
	const char *full_name = strip_package_name(d->full_name, d->file);
	if (strchr(full_name, '.'))
		fail("Check failed: !absl::StrContains(full_name, '.')");
	return (char *)full_name;
}

static JvNameEquality check_name_equality(const char *a, const char *b)
{
	size_t n = strlen(a);
	if (n != strlen(b))
		return JV_NO_MATCH;
	for (size_t i = 0; i < n; i++)
		if (jv_ascii_toupper(a[i]) != jv_ascii_toupper(b[i]))
			return JV_NO_MATCH;
	return str_eq(a, b) ? JV_EXACT_EQUAL : JV_EQUAL_IGNORE_CASE;
}

static bool message_has_conflicting_class_name(const LpMessage *message, const char *classname,
					       JvNameEquality equality_mode)
{
	if (check_name_equality(message->name, classname) == equality_mode)
		return true;
	for (int i = 0; i < message->n_nested; i++)
		if (message_has_conflicting_class_name(&message->nested[i], classname, equality_mode))
			return true;
	for (int i = 0; i < message->n_enums; i++)
		if (check_name_equality(message->enums[i].name, classname) == equality_mode)
			return true;
	return false;
}

bool jv_has_conflicting_class_name(const LpFile *file, const char *classname, JvNameEquality equality_mode)
{
	for (int i = 0; i < file->n_enums; i++)
		if (check_name_equality(file->enums[i].name, classname) == equality_mode)
			return true;
	for (int i = 0; i < file->n_services; i++)
		if (check_name_equality(file->services[i].name, classname) == equality_mode)
			return true;
	for (int i = 0; i < file->n_messages; i++)
		if (message_has_conflicting_class_name(&file->messages[i], classname, equality_mode))
			return true;
	return false;
}

/* StripProto (compiler/code_generator.h) */
static char *strip_proto(const char *filename)
{
	if (has_suffix(filename, ".protodevel"))
		return str_trim_suffix(filename, ".protodevel");
	return str_trim_suffix(filename, ".proto");
}

char *jv_get_file_default_immutable_class_name(const LpFile *file)
{
	const char *slash = strrchr(file->name, '/');
	const char *basename = slash ? slash + 1 : file->name;
	char *ret = jv_underscores_to_camel_case(strip_proto(basename), true);
	return use_old_file_class_name_default(file) ? ret : xcat(ret, "Proto");
}

/* ComputeNeedsOuterClassSuffix */
static bool needs_outer_class_suffix(const LpFile *file)
{
	if (!use_old_file_class_name_default(file))
		return false;
	return jv_has_conflicting_class_name(file, jv_get_file_default_immutable_class_name(file), JV_EXACT_EQUAL);
}

char *jv_get_file_immutable_class_name(const LpFile *file)
{
	JvFileData *d = jv_file_data(file);
	if (d->immutable_class_name)
		return d->immutable_class_name;
	char *class_name;
	if (pb_has(file->options, JV_FILEOPT_JAVA_OUTER_CLASSNAME)) {
		class_name = (char *)pb_str(file->options, JV_FILEOPT_JAVA_OUTER_CLASSNAME);
	} else {
		class_name = jv_get_file_default_immutable_class_name(file);
		if (needs_outer_class_suffix(file))
			class_name = xcat(class_name, "OuterClass");
	}
	d->immutable_class_name = class_name;
	return class_name;
}

char *jv_get_file_class_name(const LpFile *file, bool immutable, bool kotlin)
{
	if (kotlin)
		return xcat(jv_get_file_immutable_class_name(file), "Kt");
	if (immutable)
		return jv_get_file_immutable_class_name(file);
	return xcat("Mutable", jv_get_file_immutable_class_name(file));
}

char *jv_get_descriptor_class_name(const LpFile *file) { return jv_get_file_immutable_class_name(file); }

char *jv_get_file_java_package(const LpFile *file, bool immutable)
{
	(void)immutable;
	return jv_file_java_package(file);
}

char *jv_get_class_name_file(const LpFile *d, bool immutable, bool kotlin)
{
	char *result = jv_get_file_java_package(d, immutable);
	if (*result)
		result = xcat(result, ".");
	return xcat(result, jv_get_file_class_name(d, immutable, kotlin));
}

char *jv_get_class_full_name(const char *name_without_package, const LpFile *file, bool immutable, bool is_own_file,
			     bool kotlin)
{
	char *result = is_own_file ? jv_get_file_java_package(file, immutable) : jv_get_class_name_file(file, immutable, kotlin);
	if (*result)
		result = xcat(result, ".");
	result = xcat(result, name_without_package);
	if (kotlin)
		result = xcat(result, "Kt");
	return result;
}

char *jv_get_class_name_message(const LpMessage *d, bool immutable, bool kotlin)
{
	if (immutable && !kotlin) {
		JvMessageData *md = jv_message_data(d);
		if (!md->class_name)
			md->class_name = jv_get_class_full_name(class_name_without_package_message(d, true), d->file, true,
								!jv_nested_in_file_class_message(d, true), false);
		return md->class_name;
	}
	return jv_get_class_full_name(class_name_without_package_message(d, immutable), d->file, immutable,
				      !jv_nested_in_file_class_message(d, immutable), kotlin);
}

char *jv_get_class_name_enum(const LpEnum *d, bool immutable, bool kotlin)
{
	return jv_get_class_full_name(class_name_without_package_enum(d, immutable), d->file, immutable,
				      !jv_nested_in_file_class_enum(d, immutable), kotlin);
}

char *jv_get_class_name_service(const LpService *d, bool immutable, bool kotlin)
{
	return jv_get_class_full_name(class_name_without_package_service(d, immutable), d->file, immutable,
				      jv_is_own_file_service(d, immutable), kotlin);
}

/* GetJavaClassFullName */
static char *get_java_class_full_name(const char *name_without_package, const LpFile *file, bool nested,
				      bool immutable, bool kotlin)
{
	char *result;
	if (nested) {
		result = jv_get_class_name_file(file, immutable, kotlin);
		if (*result)
			result = xcat(result, "$");
	} else {
		result = jv_get_file_java_package(file, immutable);
		if (*result)
			result = xcat(result, ".");
	}
	return xcat(result, str_replace_all(name_without_package, ".", "$"));
}

char *jv_get_extension_identifier_name(const LpField *d, bool immutable, bool kotlin)
{
	return xcat(jv_get_class_name_message(d->containing_type, immutable, kotlin), ".", d->name);
}

char *jv_get_kotlin_factory_name(const LpMessage *d)
{
	char *name = jv_to_camel_case(d->name, true);
	return jv_is_forbidden_kotlin(name) ? xcat(name, "_") : name;
}

char *jv_get_fully_qualified_kotlin_factory_name(const LpMessage *d)
{
	if (d->containing_type)
		return xcat(jv_get_kotlin_extensions_class_name(d->containing_type), ".", jv_get_kotlin_factory_name(d));
	return xcat(jv_get_file_java_package(d->file, true), ".", jv_get_kotlin_factory_name(d));
}

char *jv_get_java_immutable_class_name_message(const LpMessage *d)
{
	return get_java_class_full_name(class_name_without_package_message(d, true), d->file,
					jv_nested_in_file_class_message(d, true), true, false);
}

char *jv_get_java_immutable_class_name_enum(const LpEnum *d)
{
	return get_java_class_full_name(class_name_without_package_enum(d, true), d->file,
					jv_nested_in_file_class_enum(d, true), true, false);
}

char *jv_get_java_immutable_class_name_service(const LpService *d)
{
	return get_java_class_full_name(class_name_without_package_service(d, true), d->file,
					jv_nested_in_file_class_service(d, true), true, false);
}

char *jv_get_kotlin_extensions_class_name(const LpMessage *d)
{
	return jv_get_class_full_name(class_name_without_package_kotlin(d), d->file, true, true, true);
}

char *jv_get_kotlin_extensions_class_name_escaped(const LpMessage *d)
{
	char *name_without_package = class_name_without_package_kotlin(d);
	char *full_name = jv_get_class_full_name(name_without_package, d->file, true, true, true);
	char *suffix = xcat(".", name_without_package, "Kt");
	/* rfind */
	const char *found = NULL;
	size_t sn = strlen(suffix);
	for (const char *p = full_name; (p = strstr(p, suffix)); p++)
		found = p;
	if (found)
		return xcat("`", xstrndup(full_name, (size_t)(found - full_name)), "`", suffix);
	(void)sn;
	return full_name;
}
