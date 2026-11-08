// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/python/pyi_generator.{h,cc} (protobuf v36.2), with
 * opensource_runtime_ = true. The annotations of annotate_code are collected upstream but never
 * written, so the option only has to be accepted. */

/* The members of PyiGenerator set in Generate() */
typedef struct {
	const LpFile *file;
	LpPrinter *printer;
	bool strip_nonfunctional_codegen;
	Map import_map; /* filename -> module alias */
} Pyi;

/* ModuleLevelName */
static char *pyi_module_level_name(Pyi *g, const ProtoDesc *d)
{
	char *name = lpy_name_prefixed_with_nested_types(d, ".");
	const LpFile *file = proto_desc_file(d);
	if (file != g->file) {
		const char *module_alias = map_get(&g->import_map, file->name);
		if (!module_alias) {
			char *module_name = lpy_module_name(file->name);
			const char *dot = strrchr(module_name, '.');
			module_alias = xcat("_", dot ? dot + 1 : module_name);
		}
		name = xcat(module_alias, ".", name);
	}
	return name;
}

/* PublicPackage, InternalPackage */
#define PYI_PUBLIC_PACKAGE "google.protobuf"
#define PYI_INTERNAL_PACKAGE "google.protobuf.internal"

typedef struct {
	bool has_repeated, has_iterable, has_messages, has_enums, has_extendable, has_mapping, has_optional,
		has_union, has_callable, has_well_known_type, has_datetime;
} PyiImportModules;

/* IsWellKnownType */
static bool pyi_is_well_known_type(const char *name)
{
	return str_eq(name, "google.protobuf.Any") || str_eq(name, "google.protobuf.Duration") ||
	       str_eq(name, "google.protobuf.FieldMask") || str_eq(name, "google.protobuf.ListValue") ||
	       str_eq(name, "google.protobuf.Struct") || str_eq(name, "google.protobuf.Timestamp");
}

static bool pyi_is_datetime(const char *name)
{
	return str_eq(name, "google.protobuf.Duration") || str_eq(name, "google.protobuf.Timestamp");
}

/* CheckImportModules */
static void pyi_check_import_modules(const LpMessage *descriptor, PyiImportModules *import_modules)
{
	if (descriptor->n_extension_ranges > 0)
		import_modules->has_extendable = true;
	if (descriptor->n_enums > 0)
		import_modules->has_enums = true;
	if (pyi_is_well_known_type(descriptor->full_name))
		import_modules->has_well_known_type = true;
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpField *field = &descriptor->fields[i];
		if (lpy_is_python_keyword(field->name))
			continue;
		import_modules->has_optional = true;
		if (proto_field_is_repeated(field))
			import_modules->has_repeated = true;
		if (proto_field_is_map(field)) {
			import_modules->has_mapping = true;
			const LpField *value_des = &field->message_type->fields[1];
			if (lp_field_cpp_type(value_des) == LP_CPPTYPE_MESSAGE) {
				import_modules->has_union = true;
				if (pyi_is_datetime(value_des->message_type->full_name))
					import_modules->has_datetime = true;
			}
			if (lp_field_cpp_type(value_des) == LP_CPPTYPE_ENUM)
				import_modules->has_union = true;
		} else {
			if (proto_field_is_repeated(field))
				import_modules->has_iterable = true;
			if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE) {
				import_modules->has_union = true;
				import_modules->has_mapping = true;
				if (pyi_is_datetime(field->message_type->full_name))
					import_modules->has_datetime = true;
			}
			if (lp_field_cpp_type(field) == LP_CPPTYPE_ENUM)
				import_modules->has_union = true;
		}
	}
	for (int i = 0; i < descriptor->n_nested; i++)
		pyi_check_import_modules(&descriptor->nested[i], import_modules);
}

/* PrintImportForDescriptor */
static void pyi_print_import_for_descriptor(Pyi *g, const LpFile *desc, Map *seen_aliases, Map *alias_hints,
					    bool *has_importlib)
{
	const char *filename = desc->name;
	char *module_name = lpy_stripped_module_name(filename);
	const char *last_dot = strrchr(module_name, '.');
	char *start = xcat("_", last_dot ? last_dot + 1 : module_name);
	/* Generate a unique alias by adding _1 suffixes until we get an unused alias. Upstream appends
	 * in place (absl::StrAppend); here the suffixes go to one growing buffer, and the search resumes
	 * where the last one with the same start ended (seen_aliases only grows, so every candidate
	 * before it is still taken). That keeps time and memory O(k^2) for k files with one basename,
	 * the size of the import lines printed for them. */
	char *alias = map_get(alias_hints, start);
	if (!alias)
		alias = start;
	if (map_has(seen_aliases, alias)) {
		Buf b = {0};
		buf_adds(&b, alias);
		do
			buf_adds(&b, "_1");
		while (map_has(seen_aliases, b.p));
		alias = b.p;
	}
	map_put(alias_hints, start, alias);
	if (lpy_contains_python_keyword(module_name)) {
		if (!*has_importlib) {
			lp_print(g->printer, "import importlib\n", NULL);
			*has_importlib = true;
		}
		lp_print(g->printer, "$alias$ = importlib.import_module('$name$')\n", "alias", alias, "name", module_name,
			 NULL);
	} else {
		char *import_statement;
		if (!last_dot)
			import_statement = xcat("import ", module_name);
		else
			import_statement = xcat("from ", xstrndup(module_name, (size_t)(last_dot - module_name)), " import ",
						last_dot + 1);
		lp_print(g->printer, "$statement$ as $alias$\n", "statement", import_statement, "alias", alias, NULL);
		map_put(&g->import_map, filename, alias);
		map_put(seen_aliases, alias, alias);
	}
}

static const LpFile *pyi_public_dependency(const LpFile *f, int i)
{
	return f->dependencies[f->public_dependencies[i]];
}

/* PrintImports */
static void pyi_print_imports(Pyi *g)
{
	const LpFile *file = g->file;
	LpPrinter *printer = g->printer;
	/* Checks what modules should be imported. */
	PyiImportModules import_modules = {0};
	if (file->n_messages > 0)
		import_modules.has_messages = true;
	if (file->n_enums > 0)
		import_modules.has_enums = true;
	for (int i = 0; i < file->n_messages; i++)
		pyi_check_import_modules(&file->messages[i], &import_modules);
	if (import_modules.has_datetime)
		lp_print(printer, "import datetime\n\n", NULL);

	/* Prints imported dependent _pb2 files. */
	Map seen_aliases = {0}, alias_hints = {0};
	bool has_importlib = false;
	for (int i = 0; i < file->n_dependencies; i++) {
		const LpFile *dep = file->dependencies[i];
		if (g->strip_nonfunctional_codegen && lp_is_known_feature_proto(dep->name))
			continue;
		pyi_print_import_for_descriptor(g, dep, &seen_aliases, &alias_hints, &has_importlib);
		for (int j = 0; j < dep->n_public_dependencies; j++)
			pyi_print_import_for_descriptor(g, pyi_public_dependency(dep, j), &seen_aliases, &alias_hints,
							&has_importlib);
	}

	/* Prints modules (e.g. _containers, _messages, typing) that are required in the proto file. */
	if (import_modules.has_repeated)
		lp_print(printer, "from $internal_package$ import containers as _containers\n", "internal_package",
			 PYI_INTERNAL_PACKAGE, NULL);
	if (import_modules.has_enums)
		lp_print(printer, "from $internal_package$ import enum_type_wrapper as _enum_type_wrapper\n",
			 "internal_package", PYI_INTERNAL_PACKAGE, NULL);
	if (import_modules.has_extendable)
		lp_print(printer, "from $internal_package$ import python_message as _python_message\n",
			 "internal_package", PYI_INTERNAL_PACKAGE, NULL);
	if (import_modules.has_well_known_type)
		lp_print(printer, "from $internal_package$ import well_known_types as _well_known_types\n",
			 "internal_package", PYI_INTERNAL_PACKAGE, NULL);
	lp_print(printer, "from $public_package$ import descriptor as _descriptor\n", "public_package",
		 PYI_PUBLIC_PACKAGE, NULL);
	if (import_modules.has_messages)
		lp_print(printer, "from $public_package$ import message as _message\n", "public_package",
			 PYI_PUBLIC_PACKAGE, NULL);
	if (lpy_has_generic_services(file))
		lp_print(printer, "from $public_package$ import service as _service\n", "public_package",
			 PYI_PUBLIC_PACKAGE, NULL);
	if (import_modules.has_iterable || import_modules.has_mapping) {
		lp_print(printer, "from collections.abc import", NULL);
		if (import_modules.has_iterable) {
			lp_print(printer, " Iterable as _Iterable", NULL);
			if (import_modules.has_mapping)
				lp_print(printer, ",", NULL);
		}
		if (import_modules.has_mapping)
			lp_print(printer, " Mapping as _Mapping", NULL);
		lp_print(printer, "\n", NULL);
	}
	lp_print(printer, "from typing import ", NULL);
	if (import_modules.has_callable)
		lp_print(printer, "Callable as _Callable, ", NULL);
	lp_print(printer, "ClassVar as _ClassVar", NULL);
	if (import_modules.has_optional)
		lp_print(printer, ", Optional as _Optional", NULL);
	if (import_modules.has_union)
		lp_print(printer, ", Union as _Union", NULL);
	lp_print(printer, "\n", NULL);

	/* Public imports */
	for (int i = 0; i < file->n_public_dependencies; i++) {
		const LpFile *public_dep = pyi_public_dependency(file, i);
		char *module_name = lpy_stripped_module_name(public_dep->name);
		/* Top level messages in public imports */
		for (int j = 0; j < public_dep->n_messages; j++)
			lp_print(printer, "from $module$ import $message_class$ as $message_class$\n", "module", module_name,
				 "message_class", public_dep->messages[j].name, NULL);
		/* Top level enums for public imports */
		for (int j = 0; j < public_dep->n_enums; j++)
			lp_print(printer, "from $module$ import $enum_class$ as $enum_class$\n", "module", module_name,
				 "enum_class", public_dep->enums[j].name, NULL);
	}
	lp_print(printer, "\n", NULL);
}

/* PrintEnumValues */
static void pyi_print_enum_values(Pyi *g, const LpEnum *enum_descriptor, bool is_classvar)
{
	char *module_enum_name = pyi_module_level_name(g, (const ProtoDesc *)enum_descriptor);
	for (int j = 0; j < enum_descriptor->n_values; j++) {
		const LpEnumValue *value_descriptor = &enum_descriptor->values[j];
		if (is_classvar)
			lp_print(g->printer, "$name$: _ClassVar[$module_enum_name$]\n", "name", value_descriptor->name,
				 "module_enum_name", module_enum_name, NULL);
		else
			lp_print(g->printer, "$name$: $module_enum_name$\n", "name", value_descriptor->name,
				 "module_enum_name", module_enum_name, NULL);
	}
}

/* PrintEnum */
static void pyi_print_enum(Pyi *g, const LpEnum *enum_descriptor)
{
	lp_print(g->printer,
		 "class $enum_name$(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):\n"
		 "    __slots__ = ()\n",
		 "enum_name", enum_descriptor->name, NULL);
	lp_indent(g->printer);
	pyi_print_enum_values(g, enum_descriptor, true);
	lp_outdent(g->printer);
}

/* PrintTopLevelEnums */
static void pyi_print_top_level_enums(Pyi *g)
{
	for (int i = 0; i < g->file->n_enums; i++) {
		lp_print(g->printer, "\n", NULL);
		pyi_print_enum(g, &g->file->enums[i]);
	}
}

static char *pyi_ascii_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return r;
}

/* PrintExtensions */
static void pyi_print_extensions(Pyi *g, const LpField *extensions, int n)
{
	for (int i = 0; i < n; i++) {
		const LpField *extension_field = &extensions[i];
		char *constant_name = pyi_ascii_upper(xcat(extension_field->name, "_FIELD_NUMBER"));
		lp_print(g->printer, "$constant_name$: _ClassVar[int]\n", "constant_name", constant_name, NULL);
		lp_print(g->printer, "$name$: _descriptor.FieldDescriptor\n", "name", extension_field->name, NULL);
	}
}

/* GetFieldType */
static char *pyi_get_field_type(Pyi *g, const LpField *field_des, const LpMessage *containing_des)
{
	switch (lp_field_cpp_type(field_des)) {
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_UINT32:
	case LP_CPPTYPE_INT64:
	case LP_CPPTYPE_UINT64:
		return "int";
	case LP_CPPTYPE_DOUBLE:
	case LP_CPPTYPE_FLOAT:
		return "float";
	case LP_CPPTYPE_BOOL:
		return "bool";
	case LP_CPPTYPE_ENUM:
		return pyi_module_level_name(g, (const ProtoDesc *)field_des->enum_type);
	case LP_CPPTYPE_STRING:
		return field_des->type == PT_STRING ? "str" : "bytes";
	case LP_CPPTYPE_MESSAGE: {
		/* If the field is inside a nested message and the nested message has the same name as a
		 * top-level message, then we need to prefix the field type with the module name for
		 * disambiguation. */
		char *name = pyi_module_level_name(g, (const ProtoDesc *)field_des->message_type);
		if (containing_des->containing_type && str_eq(name, containing_des->name))
			name = xcat(lpy_module_name(field_des->file->name), ".", name);
		return name;
	}
	default:
		fail("Unsupported field type.");
	}
}

/* ExtraInitTypes */
static const char *pyi_extra_init_types(const LpMessage *msg_des)
{
	if (str_eq(msg_des->full_name, "google.protobuf.Timestamp"))
		return "datetime.datetime, ";
	if (str_eq(msg_des->full_name, "google.protobuf.Duration"))
		return "datetime.timedelta, ";
	return "";
}

/* The self parameter of __init__: "self" with underscores appended until no field has the name
 * (FindFieldByName). */
static char *pyi_self_arg_name(const LpMessage *m)
{
	if (!proto_message_field_by_name(m, "self"))
		return "self";
	Map names = {0};
	for (int i = 0; i < m->n_fields; i++)
		map_put(&names, m->fields[i].name, (void *)&m->fields[i]);
	Buf b = {0};
	buf_adds(&b, "self");
	while (map_has(&names, buf_str(&b)))
		buf_addc(&b, '_');
	return (char *)buf_str(&b);
}

/* PrintMessage */
static void pyi_print_message(Pyi *g, const LpMessage *message_descriptor, bool is_nested)
{
	LpPrinter *printer = g->printer;
	if (!is_nested)
		lp_print(printer, "\n", NULL);
	const char *class_name = message_descriptor->name;
	const char *extra_base = "";
	/* A well-known type needs to inherit from its corresponding base class in
	 * net/proto2/python/internal/well_known_types. */
	if (pyi_is_well_known_type(message_descriptor->full_name))
		extra_base = xcat(", _well_known_types.", message_descriptor->name);
	lp_print(printer, "class $class_name$(_message.Message$extra_base$):\n", "class_name", class_name,
		 "extra_base", extra_base, NULL);
	lp_indent(printer);

	/* Prints slots */
	lp_print(printer, "__slots__ = (", NULL);
	int items_printed = 0;
	for (int i = 0; i < message_descriptor->n_fields; i++) {
		const LpField *field_des = &message_descriptor->fields[i];
		if (lpy_is_python_keyword(field_des->name))
			continue;
		if (items_printed > 0)
			lp_print(printer, ", ", NULL);
		++items_printed;
		lp_print(printer, "\"$field_name$\"", "field_name", field_des->name, NULL);
	}
	lp_print(printer, items_printed == 1 ? ",)\n" : ")\n", NULL);

	/* Prints Extensions for extendable messages */
	if (message_descriptor->n_extension_ranges > 0)
		lp_print(printer, "Extensions: _python_message._ExtensionDict\n", NULL);

	/* Prints nested enums */
	for (int i = 0; i < message_descriptor->n_enums; i++) {
		pyi_print_enum(g, &message_descriptor->enums[i]);
		pyi_print_enum_values(g, &message_descriptor->enums[i], false);
	}

	/* Prints nested messages */
	for (int i = 0; i < message_descriptor->n_nested; i++)
		pyi_print_message(g, &message_descriptor->nested[i], true);

	pyi_print_extensions(g, message_descriptor->extensions, message_descriptor->n_extensions);

	/* Prints field number */
	for (int i = 0; i < message_descriptor->n_fields; i++) {
		const LpField *field_des = &message_descriptor->fields[i];
		lp_print(printer, "$field_number_name$: _ClassVar[int]\n", "field_number_name",
			 xcat(pyi_ascii_upper(field_des->name), "_FIELD_NUMBER"), NULL);
	}
	/* Prints field name and type */
	for (int i = 0; i < message_descriptor->n_fields; i++) {
		const LpField *field_des = &message_descriptor->fields[i];
		if (lpy_is_python_keyword(field_des->name))
			continue;
		char *field_type = "";
		if (proto_field_is_map(field_des)) {
			const LpField *key_des = &field_des->message_type->fields[0];
			const LpField *value_des = &field_des->message_type->fields[1];
			field_type = xcat(lp_field_cpp_type(value_des) == LP_CPPTYPE_MESSAGE ? "_containers.MessageMap["
											   : "_containers.ScalarMap[",
					  pyi_get_field_type(g, key_des, message_descriptor), ", ",
					  pyi_get_field_type(g, value_des, message_descriptor));
		} else {
			if (proto_field_is_repeated(field_des))
				field_type = lp_field_cpp_type(field_des) == LP_CPPTYPE_MESSAGE
						     ? "_containers.RepeatedCompositeFieldContainer["
						     : "_containers.RepeatedScalarFieldContainer[";
			field_type = xcat(field_type, pyi_get_field_type(g, field_des, message_descriptor));
		}
		if (proto_field_is_repeated(field_des))
			field_type = xcat(field_type, "]");
		lp_print(printer, "$name$: $type$\n", "name", field_des->name, "type", field_type, NULL);
	}

	/* Prints __init__ */
	lp_print(printer, "def __init__(", NULL);
	/* If the message has a field named "self" (see b/144146793), it can still be passed to the
	 * initializer, which takes those as **kwargs. To avoid name collision, we rename the self
	 * parameter by appending underscores until it no longer collides. */
	lp_print(printer, pyi_self_arg_name(message_descriptor), NULL);
	bool has_python_keywords = false;
	for (int i = 0; i < message_descriptor->n_fields; i++) {
		const LpField *field_des = &message_descriptor->fields[i];
		if (lpy_is_python_keyword(field_des->name)) {
			has_python_keywords = true;
			continue;
		}
		lp_print(printer, ", $field_name$: ", "field_name", field_des->name, NULL);
		lp_print(printer, "_Optional[", NULL);
		if (proto_field_is_map(field_des)) {
			const LpMessage *map_entry = field_des->message_type;
			lp_print(printer, "_Mapping[$key_type$, $value_type$]", "key_type",
				 pyi_get_field_type(g, &map_entry->fields[0], message_descriptor), "value_type",
				 pyi_get_field_type(g, &map_entry->fields[1], message_descriptor), NULL);
		} else {
			if (proto_field_is_repeated(field_des))
				lp_print(printer, "_Iterable[", NULL);
			if (lp_field_cpp_type(field_des) == LP_CPPTYPE_MESSAGE) {
				lp_print(printer, "_Union[$extra_init_types$$type_name$, _Mapping]", "extra_init_types",
					 pyi_extra_init_types(field_des->message_type), "type_name",
					 pyi_get_field_type(g, field_des, message_descriptor), NULL);
			} else if (lp_field_cpp_type(field_des) == LP_CPPTYPE_ENUM) {
				lp_print(printer, "_Union[$type_name$, str]", "type_name",
					 pyi_module_level_name(g, (const ProtoDesc *)field_des->enum_type), NULL);
			} else {
				lp_print(printer, "$type_name$", "type_name",
					 pyi_get_field_type(g, field_des, message_descriptor), NULL);
			}
			if (proto_field_is_repeated(field_des))
				lp_print(printer, "]", NULL);
		}
		lp_print(printer, "] = ...", NULL);
	}
	if (has_python_keywords)
		lp_print(printer, ", **kwargs", NULL);
	lp_print(printer, ") -> None: ...\n", NULL);
	lp_outdent(printer);
}

/* PrintMessages */
static void pyi_print_messages(Pyi *g)
{
	for (int i = 0; i < g->file->n_messages; i++)
		pyi_print_message(g, &g->file->messages[i], false);
}

/* PrintServices */
static void pyi_print_services(Pyi *g)
{
	for (int i = 0; i < g->file->n_services; i++) {
		lp_print(g->printer, "\n", NULL);
		lp_print(g->printer,
			 "class $service_name$(_service.service): ...\n\n"
			 "class $service_name$_Stub($service_name$): ...\n",
			 "service_name", g->file->services[i].name, NULL);
	}
}

COLD bool lpy_pyi_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx,
			   Buf *error)
{
	(void)gen;
	Pyi g = {.file = file};
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	const char *filename = "";
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "annotate_code")) {
			/* annotate_code = true: the annotations are never written */
		} else if (has_suffix(keys[i], ".pyi")) {
			filename = keys[i];
		} else if (str_eq(keys[i], "experimental_strip_nonfunctional_codegen")) {
			g.strip_nonfunctional_codegen = true;
		} else {
			error->n = 0;
			buf_adds(error, "Unknown generator option: ");
			buf_adds(error, keys[i]);
			return false;
		}
	}
	if (!*filename)
		filename = lpy_get_file_name(file, ".pyi");

	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(ctx, filename), '$');
	printer.spaces_per_indent = 4;
	g.printer = &printer;

	pyi_print_imports(&g);
	lp_print(&printer, "DESCRIPTOR: _descriptor.FileDescriptor\n", NULL);

	/* Prints extensions and enums from imports. */
	for (int i = 0; i < file->n_public_dependencies; i++) {
		const LpFile *public_dep = pyi_public_dependency(file, i);
		pyi_print_extensions(&g, public_dep->extensions, public_dep->n_extensions);
		for (int j = 0; j < public_dep->n_enums; j++)
			pyi_print_enum_values(&g, &public_dep->enums[j], false);
	}

	pyi_print_top_level_enums(&g);
	/* Prints top level enum values */
	for (int i = 0; i < file->n_enums; i++)
		pyi_print_enum_values(&g, &file->enums[i], false);
	/* Prints top level Extensions */
	pyi_print_extensions(&g, file->extensions, file->n_extensions);
	pyi_print_messages(&g);

	if (lpy_has_generic_services(file))
		pyi_print_services(&g);
	return true;
}
