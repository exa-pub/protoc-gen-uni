/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* grpc/cpp v1.85.1: a port of grpc_cpp_plugin of grpc v1.85.1 (buf.build/grpc/cpp,
 * https://github.com/grpc/grpc): src/compiler/cpp_plugin.{h,cc} (CppGrpcGenerator and its main)
 * and the C++ generator of grpc_plugin_support (cpp_generator.c). The helpers shared by the grpc
 * plugins are pkg/languages/cpp/grpc; the plugin links protobuf 35.1 (MODULE.bazel):
 * pkg/languages/cpp/libprotoc with its 35.1 version (lp_version_35_1).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc.h"
#include "uni.h"

#include <string.h>

enum { FILEOPT_CC_GENERIC_SERVICES = 16 };

#include "cpp_generator.c"

/* param[1] of a parameter without "=": upstream reads past the end of the vector. */
static const char *param_value(const Vec *param, const char *parameter_string)
{
	if (param->n < 2)
		fail("unsupported by protoc-gen-uni: parameter \"%s\" without a value", parameter_string);
	return param->v[1];
}

/* A boolean parameter: "true" or "false", anything else is an "Invalid parameter". With
 * `only_true` (generate_mock_code, include_import_headers) upstream sets only "true" and tests
 * `!= "false"`, so "false" leaves an earlier "true" in place. */
static bool param_bool(const Vec *param, const char *parameter_string, bool *out, bool only_true, Buf *error)
{
	const char *value = param_value(param, parameter_string);
	if (str_eq(value, "true")) {
		*out = true;
	} else if (str_eq(value, "false")) {
		if (!only_true)
			*out = false;
	} else {
		buf_adds(error, xcat("Invalid parameter: ", parameter_string));
		return false;
	}
	return true;
}

/* CppGrpcGenerator::Generate */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	if (pb_int(file->options, FILEOPT_CC_GENERIC_SERVICES, 0)) {
		buf_adds(error, "cpp grpc proto compiler plugin does not work with generic "
				"services. To generate cpp grpc APIs, please set \""
				"cc_generic_service = false\".");
		return false;
	}

	Parameters generator_parameters = {
		.services_namespace = "",
		.use_system_headers = true,
		.grpc_search_path = "",
		.generate_mock_code = false,
		.gmock_search_path = "",
		.message_header_extension = "",
		.include_import_headers = false,
		.allow_sync_server_api = true,
		.allow_cq_api = true,
		.allow_deprecated = false,
	};

	const Str *raw_parameter = g->user;
	if (raw_parameter->n) {
		if (memchr(raw_parameter->p, 0, raw_parameter->n))
			fail("unsupported by protoc-gen-uni: a parameter with a NUL byte");
		Vec parameters_list = grpc_tokenize(parameter, ",");
		for (int i = 0; i < parameters_list.n; i++) {
			const char *parameter_string = parameters_list.v[i];
			Vec param = grpc_tokenize(parameter_string, "=");
			const char *key = param.v[0];
			bool ok = true;
			if (str_eq(key, "services_namespace")) {
				generator_parameters.services_namespace = param_value(&param, parameter_string);
			} else if (str_eq(key, "use_system_headers")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.use_system_headers,
						false, error);
			} else if (str_eq(key, "grpc_search_path")) {
				generator_parameters.grpc_search_path = param_value(&param, parameter_string);
			} else if (str_eq(key, "generate_mock_code")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.generate_mock_code,
						true, error);
			} else if (str_eq(key, "allow_sync_server_api")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.allow_sync_server_api,
						false, error);
			} else if (str_eq(key, "allow_cq_api")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.allow_cq_api,
						false, error);
			} else if (str_eq(key, "gmock_search_path")) {
				generator_parameters.gmock_search_path = param_value(&param, parameter_string);
			} else if (str_eq(key, "additional_header_includes")) {
				generator_parameters.additional_header_includes =
					grpc_tokenize(param_value(&param, parameter_string), ":");
			} else if (str_eq(key, "message_header_extension")) {
				generator_parameters.message_header_extension = param_value(&param, parameter_string);
			} else if (str_eq(key, "include_import_headers")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.include_import_headers,
						true, error);
			} else if (str_eq(key, "allow_deprecated")) {
				ok = param_bool(&param, parameter_string, &generator_parameters.allow_deprecated,
						false, error);
			} else {
				buf_adds(error, xcat("Unknown parameter: ", parameter_string));
				return false;
			}
			if (!ok)
				return false;
		}
	}

	ProtoBufFile pbfile = {file, lp_source_locations(file)};
	char *file_name = grpc_strip_proto(file->name);

	Buf *header_code = lp_context_open(ctx, xcat(file_name, ".grpc.pb.h"));
	GetHeaderPrologue(header_code, &pbfile, &generator_parameters);
	GetHeaderIncludes(header_code, &pbfile, &generator_parameters);
	GetHeaderServices(header_code, &pbfile, &generator_parameters);
	GetHeaderEpilogue(header_code, &pbfile);

	Buf *source_code = lp_context_open(ctx, xcat(file_name, ".grpc.pb.cc"));
	GetSourcePrologue(source_code, &pbfile, &generator_parameters);
	GetSourceIncludes(source_code, &pbfile, &generator_parameters);
	GetSourceServices(source_code, &pbfile, &generator_parameters);
	GetSourceEpilogue(source_code, &pbfile);

	if (!generator_parameters.generate_mock_code)
		return true;
	Buf *mock_code = lp_context_open(ctx, xcat(file_name, "_mock.grpc.pb.h"));
	GetMockPrologue(mock_code, &pbfile, &generator_parameters);
	GetMockIncludes(mock_code, &pbfile, &generator_parameters);
	GetMockServices(mock_code, &pbfile, &generator_parameters);
	GetMockEpilogue(mock_code, &pbfile);

	return true;
}

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Str parameter = pb_bytes(req, CGR_PARAMETER);
	const LpGenerator cpp_grpc_generator = {
		.plugin_name = "/grpc_cpp_plugin_binary", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
		.generate = generate_file,
		.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
		.minimum_edition = LP_EDITION_PROTO2,
		.maximum_edition = LP_EDITION_2024, /* GOOGLE_PROTOBUF_VERSION >= 6032000 */
		.version = &lp_version_35_1,
		.user = &parameter, /* the parameter with its NUL bytes */
	};
	return lp_plugin_main(raw, parameter, &cpp_grpc_generator, out);
}

const UniGenerator gen_grpc_cpp_v1_85_1 = {
	.name = "grpc/cpp",
	.version = "v1.85.1",
	.generate = generate,
};
