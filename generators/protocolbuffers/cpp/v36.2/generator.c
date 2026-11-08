// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/cpp v36.2: a port of the C++ generator of protoc v36.2
 * (buf.build/protocolbuffers/cpp, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`cpp::CppGenerator g; return PluginMain(argc, argv, &g);`) and
 * src/google/protobuf/compiler/cpp/generator.{h,cc} (this file), with the other files of
 * compiler/cpp in the .c files of the same names (field_generators/ for the field generators)
 * and src/google/protobuf/generated_message_tctable_gen.cc in tctable_gen.c. The libprotoc parts
 * (plugin.cc, descriptor.cc, retention.cc, io::Printer, cpp/names.h) live in
 * pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "cpp.h"
#include "uni.h"

#include "helpers.c"
#include "tctable_gen.c"
#include "parse_function_generator.c"
#include "message_layout_helper.c"
#include "tracker.c"
#include "field.c"
#include "field_generators/primitive_field.c"
#include "field_generators/enum_field.c"
#include "field_generators/string_field.c"
#include "field_generators/string_view_field.c"
#include "field_generators/cord_field.c"
#include "field_generators/message_field.c"
#include "field_generators/map_field.c"
#include "enum.c"
#include "extension.c"
#include "service.c"
#include "message.c"
#include "file.c"

/* CommonVars */
static Subs common_vars(const Options *o)
{
	Subs v = {0};
	const char *ns = protobuf_namespace(o);
	subs_push(&v, S_("pb", xcat("::", ns)));
	subs_push(&v, S_("pbi", xcat("::", ns, "::internal")));
	subs_push(&v, S_("string", "std::string"));
	subs_push(&v, S_("int8", "::int8_t"));
	subs_push(&v, S_("int32", "::int32_t"));
	subs_push(&v, S_("int64", "::int64_t"));
	subs_push(&v, S_("uint8", "::uint8_t"));
	subs_push(&v, S_("uint32", "::uint32_t"));
	subs_push(&v, S_("uint64", "::uint64_t"));
	subs_push(&v, S_("hrule_thick", kThickSeparator));
	subs_push(&v, S_("hrule_thin", kThinSeparator));
	subs_push(&v, S_("nullable", "PROTOBUF_NULLABLE"));
	subs_push(&v, S_("nonnull", "PROTOBUF_NONNULL"));
	subs_push(&v, S_("GOOGLE_PROTOBUF", o->opensource_runtime ? "GOOGLE_PROTOBUF" : "GOOGLE3_PROTOBUF"));
	subs_push(&v, S_("CHK", "ABSL_CHECK"));
	subs_push(&v, S_("DCHK", "ABSL_DCHECK"));
	return v;
}

static bool is_enum_map_type(const LpField *f)
{
	if (!is_map(f))
		return false;
	for (int i = 0; i < f->message_type->n_fields; ++i)
		if (f->message_type->fields[i].type == PT_ENUM)
			return true;
	return false;
}

/* ValidateFeatures: the error of the last violating field in VisitDescriptors order, or NULL */
static void validate_field_features(const LpField *f, char **status)
{
	const PbMsg *unresolved = pb_msg(f->proto_features, PB_CPP);
	if (f->enum_type && cpp_feature(f, CPPF_LEGACY_CLOSED_ENUM) && !proto_field_has_presence(f) && !is_repeated(f))
		*status = xcat("Field ", f->full_name, " has a closed enum type with implicit presence.");
	if (!f->containing_type || !is_map_entry_message(f->containing_type)) {
		if (pb_has(unresolved, CPPF_LEGACY_CLOSED_ENUM) && cpp_type(f) != LP_CPPTYPE_ENUM && !is_enum_map_type(f))
			*status = xcat("Field ", f->full_name, " specifies the legacy_closed_enum feature but has non-enum type.");
	}
	if ((pb_int(unresolved, CPPF_STRING_TYPE, 0) == CPP_STRING_TYPE_CORD || lp_field_legacy_ctype(f) == CTYPE_CORD) &&
	    f->is_extension)
		*status = xcat("Extension ", f->full_name, " specifies CORD string type which is not supported for extensions.");
	if ((pb_has(unresolved, CPPF_STRING_TYPE) || lp_field_legacy_ctype(f) >= 0) && cpp_type(f) != LP_CPPTYPE_STRING)
		*status = xcat("Field ", f->full_name, " specifies string_type, but is not a string nor bytes field.");
	if (pb_has(unresolved, CPPF_STRING_TYPE) && lp_field_legacy_ctype(f) >= 0)
		*status = xcat("Field ", f->full_name, " specifies both string_type and ctype which is not supported.");
}

static void validate_message_features(const LpMessage *m, char **status)
{
	for (int i = 0; i < m->n_fields; i++)
		validate_field_features(&m->fields[i], status);
	for (int i = 0; i < m->n_nested; i++)
		validate_message_features(&m->nested[i], status);
	for (int i = 0; i < m->n_extensions; i++)
		validate_field_features(&m->extensions[i], status);
}

static char *validate_features(const LpFile *file)
{
	char *status = NULL;
	for (int i = 0; i < file->n_messages; i++)
		validate_message_features(&file->messages[i], &status);
	for (int i = 0; i < file->n_extensions; i++)
		validate_field_features(&file->extensions[i], &status);
	return status;
}

static char *numbered_cc_file_name(const char *basename, int number)
{
	return xcat(basename, ".out/", cpp_itoa(number), ".cc");
}

/* CppGenerator::GenerateImpl */
static COLD bool generate_impl(const LpFile *file, LpContext *ctx, Buf *error, const Options *file_options)
{
	char *basename = lp_strip_proto(file->name);
	char *validation = validate_features(file);
	if (validation) {
		buf_adds(error, validation);
		return false;
	}
	char *ns_error = lp_cpp_validate_cc_namespace(file);
	if (ns_error) {
		buf_adds(error, ns_error);
		return false;
	}
	FileGenerator *g = file_generator_new(file, file_options);
	Subs cv = common_vars(file_options);
	if (file_options->proto_h) {
		LpPrinter p;
		Buf annotations = {0};
		lp_printer_init(&p, lp_context_open(ctx, xcat(basename, ".proto.h")), '$');
		if (file_options->annotate_headers)
			p.annotations = &annotations;
		PUSH_SUBS(&p, &cv);
		char *info_path = xcat(basename, ".proto.h.meta");
		fg_generate_proto_header(g, &p, file_options->annotate_headers ? info_path : "");
		if (file_options->annotate_headers) {
			Buf *info_output = lp_context_open(ctx, info_path);
			if (annotations.n)
				buf_add(info_output, annotations.p, annotations.n);
		}
	}
	{
		LpPrinter p;
		Buf annotations = {0};
		lp_printer_init(&p, lp_context_open(ctx, xcat(basename, ".pb.h")), '$');
		if (file_options->annotate_headers)
			p.annotations = &annotations;
		PUSH_SUBS(&p, &cv);
		char *info_path = xcat(basename, ".pb.h.meta");
		fg_generate_pb_header(g, &p, file_options->annotate_headers ? info_path : "");
		if (file_options->annotate_headers) {
			Buf *info_output = lp_context_open(ctx, info_path);
			if (annotations.n)
				buf_add(info_output, annotations.p, annotations.n);
		}
	}
	if (using_implicit_weak_fields(file, file_options)) {
		{
			LpPrinter p;
			lp_printer_init(&p, lp_context_open(ctx, xcat(basename, ".pb.cc")), '$');
			PUSH_SUBS(&p, &cv);
			fg_generate_global_source(g, &p);
		}
		int num_cc_files = g->n_message_generators + g->extension_generators.n;
		if (file_options->num_cc_files > 0) {
			if (num_cc_files > file_options->num_cc_files)
				fail("Check failed: num_cc_files <= file_options.num_cc_files: There must be at least as many "
				     "numbered .cc files as messages and extensions.");
			num_cc_files = file_options->num_cc_files;
		}
		int cc_file_number = 0;
		for (int i = 0; i < g->n_message_generators; ++i) {
			LpPrinter p;
			lp_printer_init(&p, lp_context_open(ctx, numbered_cc_file_name(basename, cc_file_number++)), '$');
			PUSH_SUBS(&p, &cv);
			fg_generate_source_for_message(g, i, &p);
		}
		for (int i = 0; i < g->extension_generators.n; ++i) {
			LpPrinter p;
			lp_printer_init(&p, lp_context_open(ctx, numbered_cc_file_name(basename, cc_file_number++)), '$');
			PUSH_SUBS(&p, &cv);
			fg_generate_source_for_extension(g, i, &p);
		}
		while (cc_file_number < num_cc_files)
			lp_context_open(ctx, numbered_cc_file_name(basename, cc_file_number++));
	} else {
		LpPrinter p;
		lp_printer_init(&p, lp_context_open(ctx, xcat(basename, ".pb.cc")), '$');
		PUSH_SUBS(&p, &cv);
		fg_generate_source(g, &p);
	}
	return true;
}

typedef struct {
	LpFile *const *files;
	int n;
	LpContext *ctx;
	Buf *error;
	const Options *common;
	Vec protos_for_field_listener_events;
	bool ok;
} GenerateAllCtx;

static COLD void generate_all_run(void *arg)
{
	GenerateAllCtx *c = arg;
	for (int i = 0; i < c->n; i++) {
		const LpFile *file = c->files[i];
		Options file_options = *c->common;
		for (int j = 0; j < c->protos_for_field_listener_events.n; j++)
			if (str_eq(file->name, c->protos_for_field_listener_events.v[j])) {
				file_options.inject_field_listener_events = true;
				break;
			}
		Buf error = {0};
		if (!generate_impl(file, c->ctx, &error, &file_options)) {
			buf_adds(c->error, xcat(file->name, ": ", buf_str(&error)));
			c->ok = false;
			return;
		}
	}
	c->ok = true;
}

/* CppGenerator::GenerateAll */
static COLD bool cpp_generate_all(const LpGenerator *gen, LpFile *const *files, int n_files, const char *parameter,
				  LpContext *ctx, Buf *error)
{
	Options o = {
		.dllexport_decl = "",
		.runtime_include_base = "",
		.annotation_pragma_name = "",
		.annotation_guard_name = "",
		.transitive_pb_h = true,
		.opensource_runtime = true,
	};
	o.state = xalloc(sizeof(CppState));
	GenerateAllCtx c = {.files = files, .n = n_files, .ctx = ctx, .error = error, .common = &o};
	const char *unsupported = NULL;
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	for (int i = 0; i < n; i++) {
		const char *key = keys[i], *value = values[i];
		if (str_eq(key, "dllexport_decl")) {
			o.dllexport_decl = value;
		} else if (str_eq(key, "annotate_headers")) {
			o.annotate_headers = true;
		} else if (str_eq(key, "annotation_pragma_name")) {
			o.annotation_pragma_name = value;
		} else if (str_eq(key, "annotation_guard_name")) {
			o.annotation_guard_name = value;
		} else if (str_eq(key, "speed")) {
			o.enforce_mode = ENFORCE_SPEED;
		} else if (str_eq(key, "code_size")) {
			o.enforce_mode = ENFORCE_CODE_SIZE;
		} else if (str_eq(key, "lite")) {
			o.enforce_mode = ENFORCE_LITE;
		} else if (str_eq(key, "lite_implicit_weak_fields")) {
			o.enforce_mode = ENFORCE_LITE;
			o.lite_implicit_weak_fields = true;
			char *end;
			long num = *value ? strtol(value, &end, 10) : 0;
			if (*value && !*end && num >= INT_MIN && num <= INT_MAX)
				o.num_cc_files = (int)num;
			if (!unsupported)
				unsupported = "lite_implicit_weak_fields";
		} else if (str_eq(key, "descriptor_implicit_weak_messages")) {
			o.descriptor_implicit_weak_messages = true;
		} else if (str_eq(key, "proto_h")) {
			o.proto_h = true;
		} else if (str_eq(key, "proto_static_reflection_h")) {
		} else if (str_eq(key, "annotate_accessor")) {
			o.annotate_accessor = true;
		} else if (str_eq(key, "protos_for_field_listener_events")) {
			c.protos_for_field_listener_events = str_split(value, ':');
		} else if (str_eq(key, "inject_field_listener_events")) {
			o.inject_field_listener_events = true;
		} else if (str_eq(key, "forbidden_field_listener_events")) {
			size_t pos = 0, len = strlen(value);
			do {
				const char *plus = strchr(value + pos, '+');
				size_t next_pos = plus ? (size_t)(plus - value) : len;
				if (next_pos > pos)
					vec_push(&o.forbidden_field_listener_events, xstrndup(value + pos, next_pos - pos));
				pos = next_pos + 1;
			} while (pos < len);
		} else if (str_eq(key, "force_eagerly_verified_lazy")) {
			o.force_eagerly_verified_lazy = true;
		} else if (str_eq(key, "experimental_strip_nonfunctional_codegen")) {
			o.strip_nonfunctional_codegen = true;
		} else if (str_eq(key, "experimental_cpp_micro_string")) {
			o.experimental_use_micro_string = true;
			if (!unsupported)
				unsupported = "experimental_cpp_micro_string";
		} else {
			buf_adds(error, xcat("Unknown generator option: ", key));
			return false;
		}
	}
	if (unsupported) {
		buf_adds(error, xcat("unsupported by protoc-gen-uni: the option ", unsupported));
		return false;
	}
	o.scc_analyzer = message_scc_analyzer_new(&o);
	const char *err = try_run(generate_all_run, &c);
	if (err) {
		buf_add_str(error, fail_message());
		return false;
	}
	return c.ok;
}

static bool cpp_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	LpFile *files[] = {(LpFile *)file};
	return cpp_generate_all(gen, files, 1, parameter, ctx, error);
}

static const char *const kCppFeatureExtensions[] = {"pb.cpp", NULL};

/* generator.h: CppGenerator */
static const LpGenerator cpp_generator = {
	.plugin_name = "protoc-gen-cpp",
	.generate = cpp_generate,
	.generate_all = cpp_generate_all,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
	.feature_extensions = kCppFeatureExtensions,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &cpp_generator, out);
}

const UniGenerator gen_protocolbuffers_cpp_v36_2 = {
	.name = "protocolbuffers/cpp",
	.version = "v36.2",
	.generate = generate,
};
