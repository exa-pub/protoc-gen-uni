// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/objc v36.2: a port of the Objective-C generator of protoc v36.2
 * (buf.build/protocolbuffers/objc, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (src/google/protobuf/compiler/objectivec/plugin_main.cc) and
 * src/google/protobuf/compiler/objectivec/generator.{h,cc} (this file), with the other files of
 * compiler/objectivec in the .c files of the same names. The libprotoc parts (plugin.cc,
 * descriptor.cc, io::Printer, objectivec/names.cc) live in pkg/languages/cpp/libprotoc.
 *
 * The Emit templates are upstream's raw strings with their common indentation reduced to one
 * space (TokenizeFormat strips it, so the output is the same; one space keeps '#' lines from
 * being taken for preprocessor lines at column 0) and without the whitespace of their last line.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "objectivec.h"
#include "uni.h"

#include "helpers.c"
#include "tf_decode_data.c"
#include "import_writer.c"
#include "field.c"
#include "primitive_field.c"
#include "enum_field.c"
#include "message_field.c"
#include "map_field.c"
#include "oneof.c"
#include "enum.c"
#include "extension.c"
#include "message.c"
#include "file.c"

/* StringToBool: "yes"/"no" (case insensitive) or "" (true); false for other values. */
static bool string_to_bool(const char *value, bool *result)
{
	char *upper = xstrdup(value);
	for (char *q = upper; *q; q++)
		*q = tf_toupper(*q);
	if (str_eq(upper, "NO")) {
		*result = false;
		return true;
	}
	if (str_eq(upper, "YES") || !*upper) {
		*result = true;
		return true;
	}
	return false;
}

static char *numbered_objc_m_file_name(const char *basename, int number)
{
	return xcat(basename, ".out/", objc_itoa(number), ".pbobjc.m");
}

static bool objc_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	buf_adds(error, "Unimplemented Generate() method. Call GenerateAll() instead.");
	return false;
}

/* The options that name files on the plugin host: not read (see README). */
static const char *const kPathOptions[] = {
	"expected_prefixes_path",
	"named_framework_to_proto_path_mappings_path",
	"package_to_prefix_mappings_path",
	"proto_package_prefix_exceptions_path",
};

static COLD bool objc_generate_all(const LpGenerator *gen, LpFile *const *files, int n_files, const char *parameter,
				   LpContext *ctx, Buf *error)
{
	LpObjcValidationOptions validation_options = {.expected_prefixes_path = ""};
	Vec suppressions = {0};
	GenerationOptions generation_options = {
		.generate_for_named_framework = "",
		.named_framework_to_proto_path_mappings_path = "",
		.runtime_import_prefix = "",
		.strip_custom_options = true,
		.generate_minimal_imports = true,
		.annotation_pragma_name = "",
		.annotation_guard_name = "",
		.extension_generation_mode = EXT_MODE_CLASS_BASED,
	};
	Map source_locations = {0};
	generation_options.source_locations = &source_locations;
	lp_objc_reset_prefix_mode();
	const char *unsupported = NULL;

	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	for (int i = 0; i < n; i++) {
		const char *k = keys[i], *v = values[i];
		for (size_t j = 0; j < sizeof kPathOptions / sizeof *kPathOptions; j++)
			if (str_eq(k, kPathOptions[j]) && *v && !(j == 0 && str_eq(v, "-")) && !unsupported)
				unsupported = xcat("unsupported by protoc-gen-uni: the option ", k,
						   " (protoc-gen-uni reads no files besides the request)");
		if (str_eq(k, "expected_prefixes_path")) {
			validation_options.expected_prefixes_path = v;
		} else if (str_eq(k, "expected_prefixes_suppressions")) {
			Vec parts = str_split(v, ';');
			for (int j = 0; j < parts.n; j++)
				if (*(const char *)parts.v[j])
					vec_push(&suppressions, parts.v[j]);
		} else if (str_eq(k, "prefixes_must_be_registered")) {
			if (!string_to_bool(v, &validation_options.prefixes_must_be_registered)) {
				buf_adds(error, xcat("error: Unknown value for prefixes_must_be_registered: ", v));
				return false;
			}
		} else if (str_eq(k, "require_prefixes")) {
			if (!string_to_bool(v, &validation_options.require_prefixes)) {
				buf_adds(error, xcat("error: Unknown value for require_prefixes: ", v));
				return false;
			}
		} else if (str_eq(k, "generate_for_named_framework")) {
			generation_options.generate_for_named_framework = v;
		} else if (str_eq(k, "named_framework_to_proto_path_mappings_path")) {
			generation_options.named_framework_to_proto_path_mappings_path = v;
		} else if (str_eq(k, "runtime_import_prefix")) {
			generation_options.runtime_import_prefix = str_trim_suffix(v, "/");
		} else if (str_eq(k, "package_to_prefix_mappings_path")) {
			/* SetPackageToPrefixMappingsPath (not read) */
		} else if (str_eq(k, "use_package_as_prefix")) {
			bool value = false;
			if (!string_to_bool(v, &value)) {
				buf_adds(error, xcat("error: Unknown use_package_as_prefix: ", v));
				return false;
			}
			lp_objc_set_use_package_as_default_prefix(value);
		} else if (str_eq(k, "proto_package_prefix_exceptions_path")) {
			/* SetProtoPackagePrefixExceptionList (not read) */
		} else if (str_eq(k, "package_as_prefix_forced_prefix")) {
			lp_objc_set_forced_package_prefix(v);
		} else if (str_eq(k, "headers_use_forward_declarations")) {
			if (!string_to_bool(v, &generation_options.headers_use_forward_declarations)) {
				buf_adds(error, xcat("error: Unknown value for headers_use_forward_declarations: ", v));
				return false;
			}
		} else if (str_eq(k, "strip_custom_options")) {
			if (!string_to_bool(v, &generation_options.strip_custom_options)) {
				buf_adds(error, xcat("error: Unknown value for strip_custom_options: ", v));
				return false;
			}
		} else if (str_eq(k, "generate_minimal_imports")) {
			if (!string_to_bool(v, &generation_options.generate_minimal_imports)) {
				buf_adds(error, xcat("error: Unknown value for generate_minimal_imports: ", v));
				return false;
			}
		} else if (str_eq(k, "experimental_multi_source_generation")) {
			if (!string_to_bool(v, &generation_options.experimental_multi_source_generation)) {
				buf_adds(error, xcat("error: Unknown value for experimental_multi_source_generation: ", v));
				return false;
			}
		} else if (str_eq(k, "experimental_strip_nonfunctional_codegen")) {
			if (!string_to_bool(v, &generation_options.experimental_strip_nonfunctional_codegen)) {
				buf_adds(error, xcat("error: Unknown value for experimental_strip_nonfunctional_codegen: ", v));
				return false;
			}
		} else if (str_eq(k, "annotation_pragma_name")) {
			generation_options.annotation_pragma_name = v;
		} else if (str_eq(k, "annotation_guard_name")) {
			generation_options.annotation_guard_name = v;
		} else if (str_eq(k, "extension_generation_mode")) {
			if (str_eq(v, "c_function")) {
				generation_options.extension_generation_mode = EXT_MODE_C_FUNCTION;
			} else if (str_eq(v, "class_based")) {
				generation_options.extension_generation_mode = EXT_MODE_CLASS_BASED;
			} else if (str_eq(v, "migration")) {
				generation_options.extension_generation_mode = EXT_MODE_MIGRATION;
			} else {
				buf_adds(error, xcat("error: Unknown value for extension_generation_mode. Expected one of "
						     "'c_function', 'class_based', or 'migration'. Got: ",
						     v));
				return false;
			}
		} else {
			buf_adds(error, xcat("error: Unknown generator option: ", k));
			return false;
		}
	}
	validation_options.expected_prefixes_suppressions = (const char *const *)suppressions.v;
	validation_options.n_expected_prefixes_suppressions = suppressions.n;
	if (unsupported) {
		buf_adds(error, unsupported);
		return false;
	}

	/* multi source generation forces imports instead of forward declarations, and minimal imports */
	if (generation_options.experimental_multi_source_generation) {
		generation_options.headers_use_forward_declarations = false;
		generation_options.generate_minimal_imports = true;
	}
	if (generation_options.experimental_strip_nonfunctional_codegen)
		generation_options.generate_minimal_imports = true;

	/* (the deprecation warnings of headers_use_forward_declarations and strip_custom_options go
	 * to stderr upstream; GPB_OBJC_HEADERS_ONLY and GPB_OBJC_SKIP_IMPLS_FILE are not read) */

	if (!*generation_options.annotation_guard_name != !*generation_options.annotation_pragma_name) {
		buf_adds(error, "error: both annotation_guard_name and annotation_pragma_name must be set to output "
				"annotations");
		return false;
	}
	bool should_annotate_headers = *generation_options.annotation_pragma_name && *generation_options.annotation_guard_name;

	/* the objc prefix/package pairings */
	const char *err = lp_objc_validate_objc_class_prefixes(files, n_files, &validation_options);
	if (err) {
		buf_adds(error, err);
		return false;
	}

	CommonState state = {.include_custom_options = !generation_options.strip_custom_options};
	for (int f = 0; f < n_files; f++) {
		const LpFile *file = files[f];
		FileGenerator file_generator;
		file_generator_init(&file_generator, file->edition, file, &generation_options, &state);
		char *filepath = lp_objc_file_path(file);

		/* the header */
		{
			Buf *output = lp_context_open(ctx, xcat(filepath, ".pbobjc.h"));
			Buf annotations = {0};
			LpPrinter printer;
			lp_printer_init(&printer, output, '$');
			const char *info_path = "";
			if (should_annotate_headers) {
				info_path = xcat(filepath, ".pbobjc.h.meta");
				printer.annotations = &annotations;
			}
			file_generate_header(&file_generator, &printer, info_path);
			if (should_annotate_headers) {
				Buf *info_output = lp_context_open(ctx, info_path);
				buf_add(info_output, annotations.p, annotations.n);
			}
		}

		/* the m file(s) */
		if (generation_options.experimental_multi_source_generation) {
			int file_number = 0;
			/* the Root and FileDescriptor (if needed) */
			{
				LpPrinter printer;
				lp_printer_init(&printer, lp_context_open(ctx, numbered_objc_m_file_name(filepath, file_number++)), '$');
				file_generate_global_source(&file_generator, &printer);
			}
			/* enums only generate C functions: one file */
			if (file_generator.enum_generators.n > 0) {
				LpPrinter printer;
				lp_printer_init(&printer, lp_context_open(ctx, numbered_objc_m_file_name(filepath, file_number++)), '$');
				file_generate_source_for_enums(&file_generator, &printer);
			}
			for (int i = 0; i < file_generator.message_generators.n; i++) {
				LpPrinter printer;
				lp_printer_init(&printer, lp_context_open(ctx, numbered_objc_m_file_name(filepath, file_number++)), '$');
				file_generate_source_for_message(&file_generator, i, &printer);
			}
		} else {
			LpPrinter printer;
			lp_printer_init(&printer, lp_context_open(ctx, xcat(filepath, ".pbobjc.m")), '$');
			file_generate_source(&file_generator, &printer);
		}
	}
	return true;
}

/* generator.h: ObjectiveCGenerator */
static const LpGenerator objc_generator = {
	.plugin_name = "/protoc-gen-objectivec", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = objc_generate,
	.generate_all = objc_generate_all,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &objc_generator, out);
}

const UniGenerator gen_protocolbuffers_objc_v36_2 = {
	.name = "protocolbuffers/objc",
	.version = "v36.2",
	.generate = generate,
};
