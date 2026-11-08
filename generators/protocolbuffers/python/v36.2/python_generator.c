// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/python/generator.{h,cc} (protobuf v36.2), with
 * opensource_runtime_ = true (the bootstrap option and the google3 paths are not compiled in).
 * The functions that Generate() never reaches (PrintMessages, PrintServices, ...) are left out.
 * The shared helpers and PyiGenerator are pkg/languages/cpp/libprotoc/compiler/python. */

enum {
	PY_ODP_OPTIONS = 2, PY_SDP_OPTIONS = 3, PY_FOPT_RETENTION = 17, PY_RETENTION_SOURCE = 2,
};

/* CopyPublicDependenciesAliases prints one alias per path through public imports, which grows
 * exponentially with layers of files that publicly import several others. The aliases of a file
 * may take up to these bytes, so that memory stays bounded whatever the import graph. */
#define PY_PUBLIC_ALIASES_MAX_BYTES ((uint64_t)16 << 20)
/* The bytes of an alias besides the names (CopyPublicDependenciesAliases). */
#define PY_PUBLIC_ALIAS_BYTES 42
#define PY_SATURATED ((uint64_t)1 << 62)

/* kDescriptorKey */
#define PY_DESCRIPTOR_KEY "DESCRIPTOR"

/* GeneratorOptions */
typedef struct {
	bool generate_pyi, annotate_pyi, strip_nonfunctional_codegen;
} PyOptions;

/* The members of Generator set in Generate() */
typedef struct {
	const LpFile *file;
	LpPrinter *printer;
	PbMsg *proto;                /* proto_ */
	Str serialized;              /* file_descriptor_serialized_ */
	uint32_t *sa;                /* py_find: suffix array of serialized, built on first use */
	uint32_t *sa_min;            /* py_find: segment tree of the minimum of sa over ranges */
	struct PyAliasFrame *stack;  /* py_copy_public_dependencies_aliases */
	size_t stack_cap;
} Py;

static char *py_utoa(uint64_t v)
{
	char b[24];
	int i = (int)sizeof b;
	b[--i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	return xstrdup(b + i);
}

static char *py_itoa(int64_t v)
{
	return v < 0 ? xcat("-", py_utoa((uint64_t)0 - (uint64_t)v)) : py_utoa((uint64_t)v);
}

static char *py_ascii_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return r;
}

static Str py_serialize(const PbMsg *m)
{
	Buf b = {0};
	if (m)
		lp_msg_serialize(m, &b);
	return (Str){b.p ? b.p : "", b.n};
}

/* ModuleAlias: the alias of the module of a .proto file when importing. */
static char *py_module_alias(const char *filename)
{
	char *module_name = lpy_module_name(filename);
	/* We can't have dots in the module name, so we replace each with _dot_. But that could lead
	 * to a collision between a.b and a_dot_b, so we also duplicate each underscore. */
	module_name = str_replace_all(module_name, "_", "__");
	return str_replace_all(module_name, ".", "_dot_");
}

/* OptionsValue: a CEscaped string of serialized_options. */
static char *py_options_value(Str serialized_options)
{
	if (!serialized_options.n)
		return "None";
	return xcat("b'", lp_cescape(serialized_options), "'");
}

/* The options of a descriptor proto, serialized (proto.options().SerializeAsString()). */
static char *py_proto_options_value(const PbMsg *proto, uint32_t options_field)
{
	return py_options_value(py_serialize(pb_msg(proto, options_field)));
}

/* StringifyDefaultValue */
static char *py_stringify_default_value(const LpField *field)
{
	if (proto_field_is_repeated(field))
		return "[]";
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_INT64:
		return py_itoa(field->default_value.i);
	case LP_CPPTYPE_UINT32:
	case LP_CPPTYPE_UINT64:
		return py_utoa(field->default_value.u);
	case LP_CPPTYPE_DOUBLE: {
		double value = field->default_value.d;
		if (value == INFINITY)
			/* Python pre-2.6 on Windows does not parse "inf" correctly.  However, a numeric
			 * literal that is too big for a double will become infinity. */
			return "1e10000";
		if (value == -INFINITY)
			return "-1e10000";
		if (value != value)
			return "(1e10000 * 0)";
		return xcat("float(", lp_simple_dtoa(value), ")");
	}
	case LP_CPPTYPE_FLOAT: {
		float value = field->default_value.f;
		if (value == INFINITY)
			return "1e10000";
		if (value == -INFINITY)
			return "-1e10000";
		if (value != value)
			return "(1e10000 * 0)";
		return xcat("float(", lp_simple_ftoa(value), ")");
	}
	case LP_CPPTYPE_BOOL:
		return field->default_value.b ? "True" : "False";
	case LP_CPPTYPE_ENUM:
		return py_itoa(field->default_value_enum ? field->default_value_enum->number : 0);
	case LP_CPPTYPE_STRING:
		return xcat("b\"", lp_cescape(field->default_value_string),
			    field->type != PT_STRING ? "\"" : "\".decode('utf-8')");
	default:
		return "None";
	}
}

/* GetLegacySyntaxName */
static const char *py_legacy_syntax_name(int edition)
{
	switch (edition) {
	case LP_EDITION_PROTO2: return "proto2";
	case LP_EDITION_PROTO3: return "proto3";
	default: return "editions";
	}
}

/* Edition_Name of the editions a file can have (the others are rejected by the pool). */
static const char *py_edition_name(int edition)
{
	switch (edition) {
	case LP_EDITION_PROTO2: return "EDITION_PROTO2";
	case LP_EDITION_PROTO3: return "EDITION_PROTO3";
	case LP_EDITION_2023: return "EDITION_2023";
	case LP_EDITION_2024: return "EDITION_2024";
	case LP_EDITION_2026: return "EDITION_2026";
	default: return "";
	}
}

/* GeneratingDescriptorProto */
static bool py_generating_descriptor_proto(const Py *g)
{
	return str_eq(g->file->name, "net/proto2/proto/descriptor.proto") ||
	       str_eq(g->file->name, "google/protobuf/descriptor.proto");
}

/* ModuleLevelDescriptorName of a message or an enum */
static char *py_module_level_descriptor_name(const Py *g, const ProtoDesc *d)
{
	char *name = xcat("_", py_ascii_upper(lpy_name_prefixed_with_nested_types(d, "_")));
	const LpFile *file = proto_desc_file(d);
	if (file != g->file)
		name = xcat(py_module_alias(file->name), ".", name);
	return name;
}

/* ModuleLevelServiceDescriptorName */
static char *py_module_level_service_descriptor_name(const Py *g, const LpService *d)
{
	char *name = py_ascii_upper(xcat("_", d->name));
	if (d->file != g->file)
		name = xcat(py_module_alias(d->file->name), ".", name);
	return name;
}

#define PY_DESC(d) ((const ProtoDesc *)(d))

/* PrintTopBoilerplate */
static void py_print_top_boilerplate(Py *g)
{
	LpPrinter *printer = g->printer;
	lp_print(printer,
		 "# -*- coding: utf-8 -*-\n"
		 "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		 "# NO CHECKED-IN PROTOBUF GENCODE\n"
		 "# source: $filename$\n",
		 "filename", g->file->name, NULL);
	lp_print(printer, "# Protobuf Python Version: $protobuf_python_version$\n", "protobuf_python_version",
		 "7.36.2", NULL);
	lp_print(printer, "\"\"\"Generated protocol buffer code.\"\"\"\n", NULL);
	lp_print(printer,
		 "from google.protobuf import descriptor as _descriptor\n"
		 "from google.protobuf import descriptor_pool as _descriptor_pool\n"
		 "$runtime_version_import$"
		 "from google.protobuf import symbol_database as _symbol_database\n"
		 "from google.protobuf.internal import builder as _builder\n",
		 "runtime_version_import", "from google.protobuf import runtime_version as _runtime_version\n", NULL);
	lp_print(printer,
		 "_runtime_version.ValidateProtobufRuntimeVersion(\n"
		 "    $domain$,\n"
		 "    $major$,\n"
		 "    $minor$,\n"
		 "    $patch$,\n"
		 "    '$suffix$',\n"
		 "    '$location$'\n"
		 ")\n",
		 "domain", "_runtime_version.Domain.PUBLIC", "major", "7", "minor", "36", "patch", "2", "suffix", "",
		 "location", g->file->name, NULL);
	lp_print(printer, "# @@protoc_insertion_point(imports)\n\n", NULL);
	lp_print(printer, "_sym_db = _symbol_database.Default()\n", NULL);
	lp_print(printer, "\n\n", NULL);
}

/* A file on the path through public imports that py_copy_public_dependencies_aliases and
 * py_count_public_aliases walk without recursion (chains of public imports can be long). */
typedef struct PyAliasFrame {
	const LpFile *file;
	int next;              /* the next public dependency */
	uint64_t n, bytes;     /* py_count_public_aliases: the totals of the public dependencies done */
} PyAliasFrame;

static void py_alias_push(PyAliasFrame **stack, size_t *cap, size_t *depth, const LpFile *file)
{
	if (*depth == *cap) {
		size_t n = *cap ? *cap * 2 : 16;
		*stack = xrealloc(*stack, sizeof **stack * *cap, sizeof **stack * n);
		*cap = n;
	}
	(*stack)[(*depth)++] = (PyAliasFrame){.file = file};
}

/* CopyPublicDependenciesAliases: if a dependency forwards other files through public
 * dependencies, copy over the corresponding module aliases (depth first, in order). */
static void py_copy_public_dependencies_aliases(Py *g, const char *copy_from, const LpFile *file)
{
	size_t depth = 0;
	py_alias_push(&g->stack, &g->stack_cap, &depth, file);
	while (depth) {
		PyAliasFrame *top = &g->stack[depth - 1];
		if (top->next == top->file->n_public_dependencies) {
			depth--;
			continue;
		}
		const LpFile *dep = top->file->dependencies[top->file->public_dependencies[top->next++]];
		char *module_name = lpy_module_name(dep->name);
		char *module_alias = py_module_alias(dep->name);
		/* There's no module alias in the dependent file if it was generated by an old protoc
		 * (less than 3.0.0-alpha-1). Use module name in this situation. */
		lp_print(g->printer,
			 "try:\n"
			 "  $alias$ = $copy_from$.$alias$\n"
			 "except AttributeError:\n"
			 "  $alias$ = $copy_from$.$module$\n",
			 "alias", module_alias, "module", module_name, "copy_from", copy_from, NULL);
		py_alias_push(&g->stack, &g->stack_cap, &depth, dep);
	}
}

static uint64_t py_sat_add(uint64_t a, uint64_t b)
{
	return a + b > PY_SATURATED ? PY_SATURATED : a + b;
}

static uint64_t py_sat_mul(uint64_t a, uint64_t b)
{
	return b && a > PY_SATURATED / b ? PY_SATURATED : a * b;
}

/* The aliases that py_copy_public_dependencies_aliases prints for a file and their bytes without
 * copy_from, saturated at PY_SATURATED. */
typedef struct {
	uint64_t n, bytes;
	uint64_t line; /* the bytes of the alias of the file itself, without copy_from */
	bool done;
} PyAliasCount;

static PyAliasCount *py_alias_count(Map *memo, const LpFile *file)
{
	PyAliasCount *c = map_get(memo, file->name);
	if (!c) {
		c = xalloc(sizeof *c);
		c->line = PY_PUBLIC_ALIAS_BYTES + 3 * (uint64_t)strlen(py_module_alias(file->name)) +
			  strlen(lpy_module_name(file->name));
		map_put(memo, file->name, c);
	}
	return c;
}

static void py_alias_add(PyAliasFrame *parent, const PyAliasCount *child)
{
	parent->n = py_sat_add(parent->n, py_sat_add(1, child->n));
	parent->bytes = py_sat_add(parent->bytes, py_sat_add(child->line, child->bytes));
}

/* Counts the aliases of file and of the files it reaches through public imports, once per file
 * (post-order on an explicit stack; the imports form a DAG). */
static PyAliasCount *py_count_public_aliases(Map *memo, PyAliasFrame **stack, size_t *cap, const LpFile *file)
{
	PyAliasCount *root = py_alias_count(memo, file);
	if (root->done)
		return root;
	size_t depth = 0;
	py_alias_push(stack, cap, &depth, file);
	while (depth) {
		PyAliasFrame *top = &(*stack)[depth - 1];
		if (top->next < top->file->n_public_dependencies) {
			const LpFile *dep = top->file->dependencies[top->file->public_dependencies[top->next++]];
			PyAliasCount *c = py_alias_count(memo, dep);
			if (c->done)
				py_alias_add(top, c);
			else
				py_alias_push(stack, cap, &depth, dep);
			continue;
		}
		PyAliasCount *c = py_alias_count(memo, top->file);
		c->n = top->n;
		c->bytes = top->bytes;
		c->done = true;
		if (--depth)
			py_alias_add(&(*stack)[depth - 1], c);
	}
	return root;
}

/* Fails before printing if the module aliases copied through public imports would take more
 * than PY_PUBLIC_ALIASES_MAX_BYTES. */
static COLD bool py_check_public_aliases(Py *g, const LpFile *file, Buf *error)
{
	Map memo = {0};
	uint64_t bytes = 0;
	for (int i = 0; i < file->n_dependencies; i++) {
		const LpFile *dep = file->dependencies[i];
		const PyAliasCount *c = py_count_public_aliases(&memo, &g->stack, &g->stack_cap, dep);
		uint64_t copy_from = 2 * (uint64_t)strlen(py_module_alias(dep->name));
		bytes = py_sat_add(bytes, py_sat_add(c->bytes, py_sat_mul(c->n, copy_from)));
	}
	if (bytes <= PY_PUBLIC_ALIASES_MAX_BYTES)
		return true;
	buf_fmt(error, "unsupported by protoc-gen-uni: module aliases copied through public imports "
		       "larger than 16 MiB");
	return false;
}

/* PrintImports: Python imports for all modules imported by the file. */
static void py_print_imports(Py *g)
{
	LpPrinter *printer = g->printer;
	bool has_importlib = false;
	for (int i = 0; i < g->file->n_dependencies; i++) {
		const char *filename = g->file->dependencies[i]->name;
		char *module_name = lpy_module_name(filename); /* ImportModuleName */
		char *module_alias = py_module_alias(filename);
		if (lpy_contains_python_keyword(module_name)) {
			/* If the module path contains a Python keyword, we have to quote the module name
			 * and import it using importlib. */
			if (!has_importlib) {
				lp_print(printer, "import importlib\n", NULL);
				has_importlib = true;
			}
			lp_print(printer, "$alias$ = importlib.import_module('$name$')\n", "alias", module_alias, "name",
				 module_name, NULL);
		} else {
			const char *last_dot = strrchr(module_name, '.');
			char *import_statement;
			if (!last_dot)
				import_statement = xcat("import ", module_name);
			else
				import_statement = xcat("from ", xstrndup(module_name, (size_t)(last_dot - module_name)),
							" import ", last_dot + 1);
			lp_print(printer, "$statement$ as $alias$\n", "statement", import_statement, "alias", module_alias,
				 NULL);
		}
		py_copy_public_dependencies_aliases(g, module_alias, g->file->dependencies[i]);
	}
	lp_print(printer, "\n", NULL);

	/* Print public imports. */
	for (int i = 0; i < g->file->n_public_dependencies; i++) {
		const LpFile *dep = g->file->dependencies[g->file->public_dependencies[i]];
		lp_print(printer, "from $module$ import *\n", "module", lpy_module_name(dep->name), NULL);
	}
	lp_print(printer, "\n", NULL);
}

/* ---- descriptor.proto: the pure-Python descriptors (GeneratingDescriptorProto) ---- */

/* GetResolvedFeatures */
static char *py_get_resolved_features(Py *g, const ProtoDesc *descriptor)
{
	if (!py_generating_descriptor_proto(g))
		/* Everything but descriptor.proto can handle proper feature resolution. */
		return "None";

	/* Load the resolved features from our pool. */
	const LpMessage *feature_set = NULL;
	for (int i = 0; i < g->file->n_messages && !feature_set; i++)
		if (str_eq(g->file->messages[i].name, "FeatureSet"))
			feature_set = &g->file->messages[i];
	if (!feature_set)
		fail("Check failed: feature_set != nullptr Malformed descriptor.proto doesn't contain "
		     "google.protobuf.FeatureSet");
	PbMsg *features =
		lp_dynamic_message_parse(g->file, feature_set->full_name, py_serialize(descriptor->merged_features));
	if (!features)
		fail("Check failed: features->ParseFromString( GetResolvedSourceFeatures(descriptor).SerializeAsString())");

	/* Collect all of the resolved features. */
	Vec feature_args = {0};
	for (int i = 0; i < features->def->n; i++) {
		if (!features->slots[i].n)
			continue;
		const LpField *field = lp_msg_slot_field(features, i);
		/* Assume these are all enums. */
		if (field->type != PT_ENUM) {
			if (!field->is_extension)
				fail("Check failed: field->is_extension() Unsupported non-enum global feature found: %s",
				     field->full_name);
			if (field->number == 1003)
				fail("Check failed: field->number() != 1003 Unsupported python-specific feature found: %s",
				     field->full_name);
			/* Skip any non-python language-specific features. */
			continue;
		}
		if (pb_int(field->options, PY_FOPT_RETENTION, 0) == PY_RETENTION_SOURCE)
			/* Skip any source-retention features. */
			continue;
		const LpEnum *enm = field->enum_type;
		const LpEnumValue *value =
			proto_enum_value_by_number(enm, (int32_t)features->slots[i].v[features->slots[i].n - 1].u);
		if (!value)
			fail("Check failed: value != nullptr (feature %s)", field->full_name);
		vec_push(&feature_args, xcat(field->name, "=", py_module_level_descriptor_name(g, PY_DESC(enm)),
					     ".values_by_name[\"", value->name, "\"].number"));
	}
	return xcat("_ResolvedFeatures(", str_join(&feature_args, ","), ")");
}

/* The callbacks of PrintResolvedFeatures (MAKE_NESTED) */
typedef struct {
	Py *g;
	const ProtoDesc *const *descs;
	int n;
	const char *field;
} PyNested;

static void py_nested_features(LpPrinter *p, void *arg)
{
	PyNested *a = arg;
	for (int i = 0; i < a->n; i++) {
		LpSub subs[] = {
			LP_SUB("resolved_subfeatures", py_get_resolved_features(a->g, a->descs[i])),
			LP_SUB("index", py_itoa(i)),
			LP_SUB("field", a->field),
		};
		lp_emit(p, subs, 3, "$descriptor_name$.$field$[$index$]._features = $resolved_subfeatures$\n");
	}
}

#define PY_DESCS(arr, count, out)                                                                  \
	do {                                                                                       \
		const ProtoDesc **v_ = xalloc(sizeof *v_ * (size_t)((count) + 1));                \
		for (int k_ = 0; k_ < (count); k_++)                                               \
			v_[k_] = PY_DESC(&(arr)[k_]);                                              \
		(out) = v_;                                                                        \
	} while (0)

static void py_resolved_features_message(Py *g, const LpMessage *msg)
{
	PyNested fields = {g, NULL, msg->n_fields, "fields"};
	PyNested oneofs = {g, NULL, msg->n_oneofs, "oneofs"};
	PyNested exts = {g, NULL, msg->n_extensions, "extensions"};
	PY_DESCS(msg->fields, msg->n_fields, fields.descs);
	PY_DESCS(msg->oneofs, msg->n_oneofs, oneofs.descs);
	PY_DESCS(msg->extensions, msg->n_extensions, exts.descs);
	LpSub subs[] = {
		LP_SUB("resolved_features", py_get_resolved_features(g, PY_DESC(msg))),
		LP_SUB("descriptor_name", py_module_level_descriptor_name(g, PY_DESC(msg))),
		LP_SUB_CB("field_features", py_nested_features, &fields),
		LP_SUB_CB("oneof_features", py_nested_features, &oneofs),
		LP_SUB_CB("ext_features", py_nested_features, &exts),
	};
	lp_emit(g->printer, subs, 5,
		"\n"
		"          $descriptor_name$._features = $resolved_features$\n"
		"          $field_features$\n"
		"          $oneof_features$\n"
		"          $ext_features$\n"
		"        ");
	for (int i = 0; i < msg->n_nested; i++)
		py_resolved_features_message(g, &msg->nested[i]);
}

static void py_resolved_features_enum(Py *g, const LpEnum *enm)
{
	PyNested values = {g, NULL, enm->n_values, "values"};
	PY_DESCS(enm->values, enm->n_values, values.descs);
	LpSub subs[] = {
		LP_SUB("resolved_features", py_get_resolved_features(g, PY_DESC(enm))),
		LP_SUB("descriptor_name", py_module_level_descriptor_name(g, PY_DESC(enm))),
		LP_SUB_CB("value_features", py_nested_features, &values),
	};
	lp_emit(g->printer, subs, 3,
		"\n"
		"                    $descriptor_name$._features = $resolved_features$\n"
		"                    $value_features$\n"
		"                  ");
}

/* VisitDescriptors(file, EnumDescriptor): the enums of a message (then of its nested messages) */
static void py_resolved_features_message_enums(Py *g, const LpMessage *msg)
{
	for (int i = 0; i < msg->n_enums; i++)
		py_resolved_features_enum(g, &msg->enums[i]);
	for (int i = 0; i < msg->n_nested; i++)
		py_resolved_features_message_enums(g, &msg->nested[i]);
}

/* PrintResolvedFeatures */
static COLD void py_print_resolved_features(Py *g)
{
	/* Since features are used during the descriptor build, it's impossible to do feature
	 * resolution at the normal point for descriptor.proto. Instead, we do feature resolution
	 * here in the generator, and embed a custom object on all of the generated descriptors. */
	LpSub subs[] = {
		LP_SUB("resolved_features", py_get_resolved_features(g, PY_DESC(g->file))),
		LP_SUB("descriptor_name", PY_DESCRIPTOR_KEY),
	};
	lp_emit(g->printer, subs, 2,
		"\n"
		"                  class _ResolvedFeatures:\n"
		"                    def __init__(self, features = None, **kwargs):\n"
		"                      if features:\n"
		"                        for k, v in features.FIELDS.items():\n"
		"                          setattr(self, k, getattr(features, k))\n"
		"                      else:\n"
		"                        for k, v in kwargs.items():\n"
		"                          setattr(self, k, v)\n"
		"                  $descriptor_name$._features = $resolved_features$\n"
		"                ");
	for (int i = 0; i < g->file->n_messages; i++)
		py_resolved_features_message(g, &g->file->messages[i]);
	for (int i = 0; i < g->file->n_messages; i++)
		py_resolved_features_message_enums(g, &g->file->messages[i]);
	for (int i = 0; i < g->file->n_enums; i++)
		py_resolved_features_enum(g, &g->file->enums[i]);
}

/* PrintEnumValueDescriptor */
static void py_print_enum_value_descriptor(Py *g, const LpEnumValue *descriptor, const PbMsg *proto)
{
	lp_print(g->printer,
		 "_descriptor.EnumValueDescriptor(\n"
		 "  name='$name$', index=$index$, number=$number$,\n"
		 "  serialized_options=$options$,\n"
		 "  type=None,\n"
		 "  create_key=_descriptor._internal_create_key)",
		 "name", descriptor->name, "index", py_itoa(descriptor->index), "number", py_itoa(descriptor->number),
		 "options", py_proto_options_value(proto, EVDP_OPTIONS), NULL);
}

/* PrintEnum */
static void py_print_enum(Py *g, const LpEnum *enum_descriptor, const PbMsg *proto)
{
	LpPrinter *printer = g->printer;
	char *module_level_descriptor_name = py_module_level_descriptor_name(g, PY_DESC(enum_descriptor));
	lp_print(printer,
		 "$descriptor_name$ = _descriptor.EnumDescriptor(\n"
		 "  name='$name$',\n"
		 "  full_name='$full_name$',\n"
		 "  filename=None,\n"
		 "  file=$file$,\n"
		 "  create_key=_descriptor._internal_create_key,\n"
		 "  values=[\n",
		 "descriptor_name", module_level_descriptor_name, "name", enum_descriptor->name, "full_name",
		 enum_descriptor->full_name, "file", PY_DESCRIPTOR_KEY, NULL);
	char *options_string = py_proto_options_value(proto, EDP_OPTIONS);
	lp_indent(printer);
	lp_indent(printer);
	for (int i = 0; i < enum_descriptor->n_values; i++) {
		py_print_enum_value_descriptor(g, &enum_descriptor->values[i], pb_msg_at(proto, EDP_VALUE, i));
		lp_print(printer, ",\n", NULL);
	}
	lp_outdent(printer);
	lp_print(printer, "],\n", NULL);
	lp_print(printer, "containing_type=None,\n", NULL);
	lp_print(printer, "serialized_options=$options_value$,\n", "options_value", options_string, NULL);
	lp_outdent(printer);
	lp_print(printer, ")\n", NULL);
	lp_print(printer, "_sym_db.RegisterEnumDescriptor($name$)\n", "name", module_level_descriptor_name, NULL);
	lp_print(printer, "\n", NULL);
}

/* PrintNestedEnums */
static void py_print_nested_enums(Py *g, const LpMessage *descriptor, const PbMsg *proto)
{
	for (int i = 0; i < descriptor->n_nested; i++)
		py_print_nested_enums(g, &descriptor->nested[i], pb_msg_at(proto, DP_NESTED_TYPE, i));
	for (int i = 0; i < descriptor->n_enums; i++)
		py_print_enum(g, &descriptor->enums[i], pb_msg_at(proto, DP_ENUM_TYPE, i));
}

/* PrintAllEnumsInFile */
static void py_print_all_enums_in_file(Py *g)
{
	for (int i = 0; i < g->file->n_enums; i++)
		py_print_enum(g, &g->file->enums[i], pb_msg_at(g->proto, FDP_ENUM_TYPE, i));
	for (int i = 0; i < g->file->n_messages; i++)
		py_print_nested_enums(g, &g->file->messages[i], pb_msg_at(g->proto, FDP_MESSAGE_TYPE, i));
}

/* PrintFieldDescriptor */
static void py_print_field_descriptor(Py *g, const LpField *field, const PbMsg *proto)
{
	const char *label = proto_field_is_required(field)   ? "2"
			    : proto_field_is_repeated(field) ? "3"
							     : "1";
	/* We always set message_type and enum_type to None at this point, and then these fields in
	 * correctly after all referenced descriptors have been defined and/or imported (see
	 * FixForeignFieldsInDescriptors()). */
	lp_print(g->printer,
		 "_descriptor.FieldDescriptor(\n"
		 "  name='$name$', full_name='$full_name$', index=$index$,\n"
		 "  number=$number$, type=$type$, cpp_type=$cpp_type$, label=$label$,\n"
		 "  has_default_value=$has_default_value$, default_value=$default_value$,\n"
		 "  message_type=None, enum_type=None, containing_type=None,\n"
		 "  is_extension=$is_extension$, extension_scope=None,\n"
		 "  serialized_options=$serialized_options$$json_name$, file=DESCRIPTOR,"
		 "  create_key=_descriptor._internal_create_key)",
		 "name", field->name, "full_name", field->full_name, "index", py_itoa(field->index), "number",
		 py_itoa(field->number), "type", py_itoa(field->type), "cpp_type", py_itoa(lp_field_cpp_type(field)),
		 "label", label, "has_default_value", field->has_default_value ? "True" : "False", "default_value",
		 py_stringify_default_value(field), "is_extension", field->is_extension ? "True" : "False",
		 "serialized_options", py_proto_options_value(proto, FD_OPTIONS), "json_name",
		 field->has_json_name ? xcat(", json_name='", field->json_name, "'") : "", NULL);
}

/* PrintFieldDescriptorsInDescriptor (PrintFieldsInDescriptor, PrintExtensionsInDescriptor) */
static void py_print_field_descriptors_in_descriptor(Py *g, const LpMessage *message_descriptor, const PbMsg *proto,
						     bool is_extension, const char *list_variable_name)
{
	lp_print(g->printer, "$list$=[\n", "list", list_variable_name, NULL);
	lp_indent(g->printer);
	int count = is_extension ? message_descriptor->n_extensions : message_descriptor->n_fields;
	for (int i = 0; i < count; i++) {
		py_print_field_descriptor(g, is_extension ? &message_descriptor->extensions[i] : &message_descriptor->fields[i],
					  pb_msg_at(proto, is_extension ? DP_EXTENSION : DP_FIELD, i));
		lp_print(g->printer, ",\n", NULL);
	}
	lp_outdent(g->printer);
	lp_print(g->printer, "],\n", NULL);
}

static void py_print_descriptor(Py *g, const LpMessage *message_descriptor, const PbMsg *proto);

/* PrintNestedDescriptors */
static void py_print_nested_descriptors(Py *g, const LpMessage *containing_descriptor, const PbMsg *proto)
{
	for (int i = 0; i < containing_descriptor->n_nested; i++)
		py_print_descriptor(g, &containing_descriptor->nested[i], pb_msg_at(proto, DP_NESTED_TYPE, i));
}

/* PrintDescriptor */
static void py_print_descriptor(Py *g, const LpMessage *message_descriptor, const PbMsg *proto)
{
	LpPrinter *printer = g->printer;
	py_print_nested_descriptors(g, message_descriptor, proto);

	lp_print(printer, "\n", NULL);
	lp_print(printer, "$descriptor_name$ = _descriptor.Descriptor(\n", "descriptor_name",
		 py_module_level_descriptor_name(g, PY_DESC(message_descriptor)), NULL);
	lp_indent(printer);
	lp_print(printer,
		 "name='$name$',\n"
		 "full_name='$full_name$',\n"
		 "filename=None,\n"
		 "file=$file$,\n"
		 "containing_type=None,\n"
		 "create_key=_descriptor._internal_create_key,\n",
		 "name", message_descriptor->name, "full_name", message_descriptor->full_name, "file", PY_DESCRIPTOR_KEY,
		 NULL);
	py_print_field_descriptors_in_descriptor(g, message_descriptor, proto, false, "fields");
	py_print_field_descriptors_in_descriptor(g, message_descriptor, proto, true, "extensions");

	/* Nested types */
	lp_print(printer, "nested_types=[", NULL);
	for (int i = 0; i < message_descriptor->n_nested; i++)
		lp_print(printer, "$name$, ", "name",
			 py_module_level_descriptor_name(g, PY_DESC(&message_descriptor->nested[i])), NULL);
	lp_print(printer, "],\n", NULL);

	/* Enum types */
	lp_print(printer, "enum_types=[\n", NULL);
	lp_indent(printer);
	for (int i = 0; i < message_descriptor->n_enums; i++) {
		lp_print(printer, py_module_level_descriptor_name(g, PY_DESC(&message_descriptor->enums[i])), NULL);
		lp_print(printer, ",\n", NULL);
	}
	lp_outdent(printer);
	lp_print(printer, "],\n", NULL);
	lp_print(printer,
		 "serialized_options=$options_value$,\n"
		 "is_extendable=$extendable$",
		 "options_value", py_proto_options_value(proto, DP_OPTIONS), "extendable",
		 message_descriptor->n_extension_ranges > 0 ? "True" : "False", NULL);
	lp_print(printer, ",\n", NULL);

	/* Extension ranges */
	lp_print(printer, "extension_ranges=[", NULL);
	for (int i = 0; i < message_descriptor->n_extension_ranges; i++) {
		const LpExtensionRange *range = &message_descriptor->extension_ranges[i];
		lp_print(printer, "($start$, $end$), ", "start", py_itoa(range->start), "end", py_itoa(range->end), NULL);
	}
	lp_print(printer, "],\n", NULL);
	lp_print(printer, "oneofs=[\n", NULL);
	lp_indent(printer);
	for (int i = 0; i < message_descriptor->n_oneofs; i++) {
		const LpOneof *desc = &message_descriptor->oneofs[i];
		char *options_string = py_proto_options_value(pb_msg_at(proto, DP_ONEOF_DECL, i), PY_ODP_OPTIONS);
		const char *serialized_options =
			str_eq(options_string, "None") ? "" : xcat(", serialized_options=", options_string);
		lp_print(printer,
			 "_descriptor.OneofDescriptor(\n"
			 "  name='$name$', full_name='$full_name$',\n"
			 "  index=$index$, containing_type=None,\n"
			 "  create_key=_descriptor._internal_create_key,\n"
			 "fields=[]$serialized_options$),\n",
			 "name", desc->name, "full_name", desc->full_name, "index", py_itoa(desc->index),
			 "serialized_options", serialized_options, NULL);
	}
	lp_outdent(printer);
	lp_print(printer, "],\n", NULL);

	lp_outdent(printer);
	lp_print(printer, ")\n", NULL);
}

/* PrintMessageDescriptors */
static void py_print_message_descriptors(Py *g)
{
	for (int i = 0; i < g->file->n_messages; i++) {
		py_print_descriptor(g, &g->file->messages[i], pb_msg_at(g->proto, FDP_MESSAGE_TYPE, i));
		lp_print(g->printer, "\n", NULL);
	}
}

/* FieldReferencingExpression: the module-level expression of a field of the file (containing_type
 * NULL: a module-level field). */
static char *py_field_referencing_expression(const Py *g, const LpMessage *containing_type, const LpField *field,
					     const char *python_dict_name)
{
	if (field->file != g->file)
		fail("Check failed: field.file() == file_ (%s vs. %s)", field->file->name, g->file->name);
	if (!containing_type)
		return lpy_resolve_keyword(field->name);
	return xcat(py_module_level_descriptor_name(g, PY_DESC(containing_type)), ".", python_dict_name, "['",
		    field->name, "']");
}

/* FixForeignFieldsInField */
static void py_fix_foreign_fields_in_field(Py *g, const LpMessage *containing_type, const LpField *field,
					   const char *python_dict_name)
{
	char *field_ref = py_field_referencing_expression(g, containing_type, field, python_dict_name);
	if (field->message_type)
		lp_print(g->printer, "$field_ref$.message_type = $foreign_type$\n", "field_ref", field_ref, "foreign_type",
			 py_module_level_descriptor_name(g, PY_DESC(field->message_type)), NULL);
	if (field->enum_type)
		lp_print(g->printer, "$field_ref$.enum_type = $enum_type$\n", "field_ref", field_ref, "enum_type",
			 py_module_level_descriptor_name(g, PY_DESC(field->enum_type)), NULL);
}

/* FixContainingTypeInDescriptor */
static void py_fix_containing_type_in_descriptor(Py *g, const ProtoDesc *descriptor,
						 const LpMessage *containing_descriptor)
{
	if (containing_descriptor)
		lp_print(g->printer, "$nested_name$.containing_type = $parent_name$\n", "nested_name",
			 py_module_level_descriptor_name(g, descriptor), "parent_name",
			 py_module_level_descriptor_name(g, PY_DESC(containing_descriptor)), NULL);
}

/* FixForeignFieldsInDescriptor */
static void py_fix_foreign_fields_in_descriptor(Py *g, const LpMessage *descriptor,
						const LpMessage *containing_descriptor)
{
	for (int i = 0; i < descriptor->n_nested; i++)
		py_fix_foreign_fields_in_descriptor(g, &descriptor->nested[i], descriptor);
	for (int i = 0; i < descriptor->n_fields; i++)
		py_fix_foreign_fields_in_field(g, descriptor, &descriptor->fields[i], "fields_by_name");
	py_fix_containing_type_in_descriptor(g, PY_DESC(descriptor), containing_descriptor);
	for (int i = 0; i < descriptor->n_enums; i++)
		py_fix_containing_type_in_descriptor(g, PY_DESC(&descriptor->enums[i]), descriptor);
	for (int i = 0; i < descriptor->n_oneofs; i++) {
		const LpOneof *oneof = &descriptor->oneofs[i];
		char *descriptor_name = py_module_level_descriptor_name(g, PY_DESC(descriptor));
		for (int j = 0; j < oneof->n_fields; j++) {
			const char *vars[] = {"descriptor_name", descriptor_name, "oneof_name", oneof->name,
					      "field_name", proto_oneof_field(oneof, j)->name};
			lp_printv(g->printer,
				  "$descriptor_name$.oneofs_by_name['$oneof_name$'].fields.append(\n"
				  "  $descriptor_name$.fields_by_name['$field_name$'])\n",
				  3, vars);
			lp_printv(g->printer,
				  "$descriptor_name$.fields_by_name['$field_name$'].containing_oneof = "
				  "$descriptor_name$.oneofs_by_name['$oneof_name$']\n",
				  3, vars);
		}
	}
}

/* FixForeignFieldsInDescriptors */
static void py_fix_foreign_fields_in_descriptors(Py *g)
{
	LpPrinter *printer = g->printer;
	const LpFile *file = g->file;
	for (int i = 0; i < file->n_messages; i++)
		py_fix_foreign_fields_in_descriptor(g, &file->messages[i], NULL);
	/* AddMessageToFileDescriptor */
	for (int i = 0; i < file->n_messages; i++)
		lp_print(printer, "$descriptor_name$.message_types_by_name['$message_name$'] = $message_descriptor_name$\n",
			 "descriptor_name", PY_DESCRIPTOR_KEY, "message_name", file->messages[i].name,
			 "message_descriptor_name", py_module_level_descriptor_name(g, PY_DESC(&file->messages[i])), NULL);
	/* AddEnumToFileDescriptor */
	for (int i = 0; i < file->n_enums; i++)
		lp_print(printer, "$descriptor_name$.enum_types_by_name['$enum_name$'] = $enum_descriptor_name$\n",
			 "descriptor_name", PY_DESCRIPTOR_KEY, "enum_name", file->enums[i].name, "enum_descriptor_name",
			 py_module_level_descriptor_name(g, PY_DESC(&file->enums[i])), NULL);
	/* AddExtensionToFileDescriptor */
	for (int i = 0; i < file->n_extensions; i++)
		lp_print(printer, "$descriptor_name$.extensions_by_name['$field_name$'] = $resolved_name$\n",
			 "descriptor_name", PY_DESCRIPTOR_KEY, "field_name", file->extensions[i].name, "resolved_name",
			 lpy_resolve_keyword(file->extensions[i].name), NULL);
	lp_print(printer, "_sym_db.RegisterFileDescriptor($name$)\n", "name", PY_DESCRIPTOR_KEY, NULL);
	lp_print(printer, "\n", NULL);
}

/* PrintFileDescriptor: the single file descriptor of this file. */
static void py_print_file_descriptor(Py *g)
{
	LpPrinter *printer = g->printer;
	const LpFile *file = g->file;
	char *serialized_descriptor = lp_chexescape(g->serialized);
	if (py_generating_descriptor_proto(g)) {
		lp_print(printer, "if not _descriptor._USE_C_DESCRIPTORS:\n", NULL);
		lp_indent(printer);
		/* Pure python's AddSerializedFile() depend on the generated descriptor_pb2.py thus we
		 * can not use AddSerializedFile() when generated descriptor.proto for pure python. */
		lp_print(printer,
			 "$descriptor_name$ = _descriptor.FileDescriptor(\n"
			 "  name='$name$',\n"
			 "  package='$package$',\n"
			 "  syntax='$syntax$',\n"
			 "  edition='$edition$',\n"
			 "  serialized_options=$options$,\n"
			 "  create_key=_descriptor._internal_create_key,\n",
			 "descriptor_name", PY_DESCRIPTOR_KEY, "name", file->name, "package", file->package, "syntax",
			 py_legacy_syntax_name(file->edition), "edition", py_edition_name(file->edition), "options",
			 py_proto_options_value(g->proto, FDP_OPTIONS), NULL);
		lp_indent(printer);
		lp_print(printer, "serialized_pb=b'$value$'\n", "value", serialized_descriptor, NULL);
		if (file->n_dependencies != 0) {
			lp_print(printer, ",\ndependencies=[", NULL);
			for (int i = 0; i < file->n_dependencies; i++)
				lp_print(printer, "$module_alias$.DESCRIPTOR,", "module_alias",
					 py_module_alias(file->dependencies[i]->name), NULL);
			lp_print(printer, "]", NULL);
		}
		if (file->n_public_dependencies > 0) {
			lp_print(printer, ",\npublic_dependencies=[", NULL);
			for (int i = 0; i < file->n_public_dependencies; i++)
				lp_print(printer, "$module_alias$.DESCRIPTOR,", "module_alias",
					 py_module_alias(file->dependencies[file->public_dependencies[i]]->name), NULL);
			lp_print(printer, "]", NULL);
		}
		lp_outdent(printer);
		lp_print(printer, ")\n", NULL);
		lp_outdent(printer);
		lp_print(printer, "else:\n", NULL);
		lp_indent(printer);
	}
	lp_print(printer, "$descriptor_name$ = _descriptor_pool.Default().AddSerializedFile(b'$serialized_descriptor$')\n",
		 "descriptor_name", PY_DESCRIPTOR_KEY, "serialized_descriptor", serialized_descriptor, NULL);
	if (py_generating_descriptor_proto(g))
		lp_outdent(printer);
	lp_print(printer, "\n", NULL);
}

/* ---- options and offsets (all files) ---- */

/* The suffix array of s (prefix doubling with counting sorts, O(n log n)). */
static COLD uint32_t *py_suffix_array(Str s)
{
	uint32_t n = (uint32_t)s.n, m = 256;
	uint32_t *sa = xalloc(sizeof *sa * n), *rank = xalloc(sizeof *rank * n), *tmp = xalloc(sizeof *tmp * n);
	uint32_t *cnt = xalloc(sizeof *cnt * (n > 256 ? n : 256) + sizeof *cnt);
	for (uint32_t i = 0; i < n; i++)
		rank[i] = (unsigned char)s.p[i];
	for (uint32_t i = 0; i < n; i++)
		tmp[i] = i;
	for (uint32_t k = 0;; k = k ? 2 * k : 1) {
		/* tmp holds the positions ordered by the rank at +k; sort them stably by rank. */
		memset(cnt, 0, sizeof *cnt * (m + 1));
		for (uint32_t i = 0; i < n; i++)
			cnt[rank[i] + 1]++;
		for (uint32_t c = 0; c < m; c++)
			cnt[c + 1] += cnt[c];
		for (uint32_t i = 0; i < n; i++)
			sa[cnt[rank[tmp[i]]]++] = tmp[i];
		/* New ranks: equal when the ranks at +0 and +k are equal. */
		uint32_t classes = 1;
		tmp[sa[0]] = 0;
		for (uint32_t i = 1; i < n; i++) {
			uint32_t a = sa[i - 1], b = sa[i];
			bool same = k && rank[a] == rank[b] && a + k < n && b + k < n && rank[a + k] == rank[b + k];
			if (!k)
				same = rank[a] == rank[b];
			tmp[b] = same ? classes - 1 : classes++;
		}
		uint32_t *t = rank;
		rank = tmp;
		tmp = t;
		if (classes == n)
			return sa;
		m = classes;
		/* Order by the rank at +2k for the next round: the suffixes shorter than 2k first. */
		uint32_t k2 = k ? 2 * k : 1, j = 0;
		for (uint32_t i = n > k2 ? n - k2 : 0; i < n; i++)
			tmp[j++] = i;
		for (uint32_t i = 0; i < n; i++)
			if (sa[i] >= k2)
				tmp[j++] = sa[i] - k2;
	}
}

/* Compares the suffix of s at pos with the first needle.n bytes of it, needle as a prefix. */
static int py_suffix_cmp(Str s, uint32_t pos, Str needle)
{
	size_t len = s.n - pos < needle.n ? s.n - pos : needle.n;
	int c = memcmp(s.p + pos, needle.p, len);
	return c ? c : len < needle.n ? -1 : 0;
}

/* file_descriptor_serialized_.find(sp): the first occurrence (std::string::find); SIZE_MAX (npos)
 * if absent. Upstream searches the whole file for every element; here the suffixes that start with
 * the needle are a range of the suffix array, and the first occurrence is their minimum position. */
static size_t py_find(Py *g, Str hay, Str needle)
{
	if (!needle.n)
		return 0;
	if (needle.n > hay.n)
		return SIZE_MAX;
	if (hay.n >= (size_t)1 << 31) /* the limit of a protobuf message */
		fail("unsupported by protoc-gen-uni: a serialized file of 2 GiB or more");
	uint32_t n = (uint32_t)hay.n;
	if (!g->sa) {
		g->sa = py_suffix_array(hay);
		g->sa_min = xalloc(sizeof *g->sa_min * 2 * (size_t)n);
		for (uint32_t i = 0; i < n; i++)
			g->sa_min[n + i] = g->sa[i];
		for (uint32_t i = n - 1; i > 0; i--)
			g->sa_min[i] = g->sa_min[2 * i] < g->sa_min[2 * i + 1] ? g->sa_min[2 * i] : g->sa_min[2 * i + 1];
	}
	uint32_t lo = 0, hi = n;
	while (lo < hi) { /* the first suffix >= needle */
		uint32_t mid = lo + (hi - lo) / 2;
		if (py_suffix_cmp(hay, g->sa[mid], needle) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	uint32_t end = n;
	for (uint32_t l = lo; l < end;) { /* the first suffix > needle */
		uint32_t mid = l + (end - l) / 2;
		if (py_suffix_cmp(hay, g->sa[mid], needle) <= 0)
			l = mid + 1;
		else
			end = mid;
	}
	if (lo == end)
		return SIZE_MAX;
	uint32_t first = UINT32_MAX;
	for (uint32_t l = lo + n, r = end + n; l < r; l /= 2, r /= 2) {
		if (l & 1) {
			first = g->sa_min[l] < first ? g->sa_min[l] : first;
			l++;
		}
		if (r & 1) {
			r--;
			first = g->sa_min[r] < first ? g->sa_min[r] : first;
		}
	}
	return first;
}

/* PrintSerializedPbInterval: _serialized_start and _serialized_end of a descriptor proto, found
 * with find() in the serialized file (the first match, not necessarily the element's position). */
static void py_print_serialized_pb_interval(Py *g, const PbMsg *descriptor_proto, const char *name)
{
	Str sp = py_serialize(descriptor_proto);
	size_t offset = py_find(g, g->serialized, sp);
	lp_print(g->printer,
		 "_globals['$name$']._serialized_start=$serialized_start$\n"
		 "_globals['$name$']._serialized_end=$serialized_end$\n",
		 "name", name, "serialized_start", py_utoa(offset), "serialized_end", py_utoa(offset + sp.n), NULL);
}

/* PrintDescriptorOptionsFixingCode */
static bool py_print_descriptor_options_fixing_code(Py *g, const PbMsg *proto, uint32_t options_field,
						    const char *descriptor_str)
{
	char *options = py_proto_options_value(proto, options_field);
	/* Reset the _options to None thus DescriptorBase.GetOptions() can parse _options again after
	 * extensions are registered. */
	const char *dot = strchr(descriptor_str, '.');
	char *descriptor_name;
	if (!dot)
		descriptor_name = xcat("_globals['", descriptor_str, "']");
	else
		descriptor_name =
			xcat("_globals['", xstrndup(descriptor_str, (size_t)(dot - descriptor_str)), "']", dot);
	if (str_eq(options, "None"))
		return false;
	lp_print(g->printer,
		 "$descriptor_name$._loaded_options = None\n"
		 "$descriptor_name$._serialized_options = $serialized_value$\n",
		 "descriptor_name", descriptor_name, "serialized_value", options, NULL);
	return true;
}

/* SetMessagePbInterval */
static void py_set_message_pb_interval(Py *g, const PbMsg *message_proto, const LpMessage *descriptor)
{
	py_print_serialized_pb_interval(g, message_proto, py_module_level_descriptor_name(g, PY_DESC(descriptor)));
	/* Nested messages. */
	for (int i = 0; i < descriptor->n_nested; i++)
		py_set_message_pb_interval(g, pb_msg_at(message_proto, DP_NESTED_TYPE, i), &descriptor->nested[i]);
	for (int i = 0; i < descriptor->n_enums; i++)
		py_print_serialized_pb_interval(g, pb_msg_at(message_proto, DP_ENUM_TYPE, i),
						py_module_level_descriptor_name(g, PY_DESC(&descriptor->enums[i])));
}

/* SetSerializedPbInterval */
static void py_set_serialized_pb_interval(Py *g)
{
	const LpFile *file = g->file;
	/* Top level enums. */
	for (int i = 0; i < file->n_enums; i++)
		py_print_serialized_pb_interval(g, pb_msg_at(g->proto, FDP_ENUM_TYPE, i),
						py_module_level_descriptor_name(g, PY_DESC(&file->enums[i])));
	/* Messages. */
	for (int i = 0; i < file->n_messages; i++)
		py_set_message_pb_interval(g, pb_msg_at(g->proto, FDP_MESSAGE_TYPE, i), &file->messages[i]);
	/* Services. */
	for (int i = 0; i < file->n_services; i++)
		py_print_serialized_pb_interval(g, pb_msg_at(g->proto, FDP_SERVICE, i),
						py_module_level_service_descriptor_name(g, &file->services[i]));
}

/* FixOptionsForEnum */
static void py_fix_options_for_enum(Py *g, const LpEnum *enum_descriptor, const PbMsg *proto)
{
	char *descriptor_name = py_module_level_descriptor_name(g, PY_DESC(enum_descriptor));
	py_print_descriptor_options_fixing_code(g, proto, EDP_OPTIONS, descriptor_name);
	for (int i = 0; i < enum_descriptor->n_values; i++)
		py_print_descriptor_options_fixing_code(
			g, pb_msg_at(proto, EDP_VALUE, i), EVDP_OPTIONS,
			xcat(descriptor_name, ".values_by_name[\"", enum_descriptor->values[i].name, "\"]"));
}

/* FixOptionsForField */
static void py_fix_options_for_field(Py *g, const LpField *field, const PbMsg *proto)
{
	char *field_name;
	if (field->is_extension) {
		if (!field->extension_scope)
			/* Top level extensions. */
			field_name = xstrdup(field->name);
		else
			field_name = py_field_referencing_expression(g, field->extension_scope, field, "extensions_by_name");
	} else {
		field_name = py_field_referencing_expression(g, field->containing_type, field, "fields_by_name");
	}
	py_print_descriptor_options_fixing_code(g, proto, FD_OPTIONS, field_name);
}

/* FixOptionsForMessage */
static void py_fix_options_for_message(Py *g, const LpMessage *descriptor, const PbMsg *proto)
{
	/* Nested messages. */
	for (int i = 0; i < descriptor->n_nested; i++)
		py_fix_options_for_message(g, &descriptor->nested[i], pb_msg_at(proto, DP_NESTED_TYPE, i));
	/* Oneofs (FixOptionsForOneof). */
	for (int i = 0; i < descriptor->n_oneofs; i++) {
		const LpOneof *oneof = &descriptor->oneofs[i];
		char *oneof_name = xcat(py_module_level_descriptor_name(g, PY_DESC(oneof->containing_type)),
					".oneofs_by_name['", oneof->name, "']");
		py_print_descriptor_options_fixing_code(g, pb_msg_at(proto, DP_ONEOF_DECL, i), PY_ODP_OPTIONS, oneof_name);
	}
	/* Enums. */
	for (int i = 0; i < descriptor->n_enums; i++)
		py_fix_options_for_enum(g, &descriptor->enums[i], pb_msg_at(proto, DP_ENUM_TYPE, i));
	/* Fields. */
	for (int i = 0; i < descriptor->n_fields; i++)
		py_fix_options_for_field(g, &descriptor->fields[i], pb_msg_at(proto, DP_FIELD, i));
	/* Extensions. */
	for (int i = 0; i < descriptor->n_extensions; i++)
		py_fix_options_for_field(g, &descriptor->extensions[i], pb_msg_at(proto, DP_EXTENSION, i));
	/* Message option for this message. */
	py_print_descriptor_options_fixing_code(g, proto, DP_OPTIONS,
						py_module_level_descriptor_name(g, PY_DESC(descriptor)));
}

/* FixOptionsForService */
static void py_fix_options_for_service(Py *g, const LpService *service_descriptor, const PbMsg *proto)
{
	char *descriptor_name = py_module_level_service_descriptor_name(g, service_descriptor);
	py_print_descriptor_options_fixing_code(g, proto, PY_SDP_OPTIONS, descriptor_name);
	for (int i = 0; i < service_descriptor->n_methods; i++)
		py_print_descriptor_options_fixing_code(
			g, pb_msg_at(proto, SDP_METHOD, i), MDP_OPTIONS,
			xcat(descriptor_name, ".methods_by_name['", service_descriptor->methods[i].name, "']"));
}

/* FixAllDescriptorOptions: expressions that set the options field of all descriptors. */
static void py_fix_all_descriptor_options(Py *g)
{
	const LpFile *file = g->file;
	/* Prints an expression that sets the file descriptor's options. */
	if (!py_print_descriptor_options_fixing_code(g, g->proto, FDP_OPTIONS, PY_DESCRIPTOR_KEY))
		lp_print(g->printer, "DESCRIPTOR._loaded_options = None\n", NULL);
	/* Prints expressions that set the options for all top level enums. */
	for (int i = 0; i < file->n_enums; i++)
		py_fix_options_for_enum(g, &file->enums[i], pb_msg_at(g->proto, FDP_ENUM_TYPE, i));
	/* Prints expressions that set the options for all top level extensions. */
	for (int i = 0; i < file->n_extensions; i++)
		py_fix_options_for_field(g, &file->extensions[i], pb_msg_at(g->proto, FDP_EXTENSION, i));
	/* Prints expressions that set the options for all messages, nested enums, nested extensions
	 * and message fields. */
	for (int i = 0; i < file->n_messages; i++)
		py_fix_options_for_message(g, &file->messages[i], pb_msg_at(g->proto, FDP_MESSAGE_TYPE, i));
	for (int i = 0; i < file->n_services; i++)
		py_fix_options_for_service(g, &file->services[i], pb_msg_at(g->proto, FDP_SERVICE, i));
}

/* ---- Generator ---- */

/* ParseParameter: every unknown option overwrites the error, so the last one is reported. */
static PyOptions py_parse_parameter(const char *parameter, Buf *error)
{
	PyOptions options = {0};
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "pyi_out")) {
			options.generate_pyi = true;
		} else if (str_eq(keys[i], "annotate_code")) {
			options.annotate_pyi = true;
		} else if (str_eq(keys[i], "experimental_strip_nonfunctional_codegen")) {
			options.strip_nonfunctional_codegen = true;
		} else {
			error->n = 0;
			buf_adds(error, "Unknown generator option: ");
			buf_adds(error, keys[i]);
		}
	}
	return options;
}

/* Generator::Generate */
static COLD bool py_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx,
			     Buf *error)
{
	PyOptions options = py_parse_parameter(parameter, error);
	if (error->n)
		return false;

	/* Generate pyi typing information */
	if (options.generate_pyi) {
		const char *pyi_options = options.annotate_pyi ? "annotate_code" : "";
		if (options.strip_nonfunctional_codegen)
			pyi_options = *pyi_options ? xcat(pyi_options, ",experimental_strip_nonfunctional_codegen")
						   : "experimental_strip_nonfunctional_codegen";
		if (!lpy_pyi_generate(gen, file, pyi_options, ctx, error))
			return false;
	}

	Py g = {.file = file};
	if (!py_check_public_aliases(&g, file, error))
		return false;
	const char *filename = lpy_get_file_name(file, ".py");
	g.proto = lp_strip_source_retention_options(file);
	g.serialized = py_serialize(g.proto);

	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(ctx, filename), '$');
	g.printer = &printer;

	py_print_top_boilerplate(&g);
	py_print_imports(&g);
	py_print_file_descriptor(&g);
	lp_print(&printer, "_globals = globals()\n", NULL);
	bool descriptor_proto = py_generating_descriptor_proto(&g);
	if (descriptor_proto) {
		lp_print(&printer, "if not _descriptor._USE_C_DESCRIPTORS:\n", NULL);
		lp_indent(&printer);
		/* Create enums before message descriptors */
		py_print_all_enums_in_file(&g);
		py_print_message_descriptors(&g);
		py_fix_foreign_fields_in_descriptors(&g);
		py_print_resolved_features(&g);
		lp_outdent(&printer);
		lp_print(&printer, "else:\n", NULL);
		lp_indent(&printer);
	}
	/* Find the message descriptors first and then use the message descriptor to find enums. */
	lp_print(&printer, "_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)\n", NULL);
	if (descriptor_proto)
		lp_outdent(&printer);
	char *module_name = lpy_module_name(file->name);
	lp_print(&printer, "_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, '$module_name$', _globals)\n",
		 "module_name", module_name, NULL);
	lp_print(&printer, "if not _descriptor._USE_C_DESCRIPTORS:\n", NULL);
	lp_indent(&printer);

	/* Descriptor options may have custom extensions. These custom options can only be
	 * successfully parsed after we register corresponding extensions. Therefore we parse all
	 * options again here to recognize custom options that may be unknown when we define the
	 * descriptors. This does not apply to services because they are not used by extensions. */
	py_fix_all_descriptor_options(&g);

	/* Set serialized_start and serialized_end. */
	py_set_serialized_pb_interval(&g);

	lp_outdent(&printer);
	if (lpy_has_generic_services(file))
		lp_print(&printer, "_builder.BuildServices(DESCRIPTOR, '$module_name$', _globals)\n", "module_name",
			 module_name, NULL);
	lp_print(&printer, "# @@protoc_insertion_point(module_scope)\n", NULL);
	return true;
}
