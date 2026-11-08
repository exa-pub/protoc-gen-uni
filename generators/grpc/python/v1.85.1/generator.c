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

/* grpc/python v1.85.1: a port of grpc_python_plugin of grpc v1.85.1 (buf.build/grpc/python,
 * https://github.com/grpc/grpc): src/compiler/python_plugin.cc (its main) and
 * PythonGrpcGenerator::Generate of python_generator.cc, with the rest of the Python generator of
 * grpc_plugin_support in python_generator.c. The helpers shared by the grpc plugins are
 * pkg/languages/cpp/grpc; the plugin links protobuf 35.1 (MODULE.bazel):
 * pkg/languages/cpp/libprotoc with its 35.1 version (lp_version_35_1).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

#include "python_generator.c"

/* PythonGrpcGenerator::Generate */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	(void)parameter; /* the parameter with its NUL bytes is g->user */

	/* Get output file name. */
	if (!py_has_proto_suffix(file->name)) {
		buf_adds(error, "Invalid proto file name. Proto file must end with .proto");
		return false;
	}
	enum { PROTO_SUFFIX_LENGTH = 6 }; /* strlen(".proto") */
	char *base = xstrndup(file->name, strlen(file->name) - PROTO_SUFFIX_LENGTH);
	for (char *p = base; *p; p++) {
		if (*p == '-')
			*p = '_';
		if (*p == '.')
			*p = '/';
	}
	char *pb2_file_name = xcat(base, "_pb2.py");
	char *pb2_grpc_file_name = xcat(base, "_pb2_grpc.py");
	py_generator_file_name = file->name;

	Str grpc_version = {0};
	PyGeneratorConfiguration extended_config = {0};
	if (!py_parse_parameters(*(const Str *)g->user, &grpc_version, &extended_config, error))
		return false;
	PyPrivateGenerator generator = {&extended_config, file, lp_source_locations(file), false};
	if (str_eqc(grpc_version, "grpc_2_0"))
		return py_generate_grpc(ctx, &generator, pb2_grpc_file_name, true);
	if (str_eqc(grpc_version, "grpc_1_0"))
		return py_generate_grpc(ctx, &generator, pb2_grpc_file_name, true) &&
		       py_generate_grpc(ctx, &generator, pb2_file_name, false);
	buf_adds(error, "Invalid grpc version '");
	buf_add_str(error, grpc_version);
	buf_adds(error, "'.");
	return false;
}

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Str parameter = pb_bytes(req, CGR_PARAMETER);
	const LpGenerator python_grpc_generator = {
		.plugin_name = "/grpc_python_plugin_binary", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
		.generate = generate_file,
		.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
		.minimum_edition = LP_EDITION_PROTO2,
		.maximum_edition = LP_EDITION_2024, /* GOOGLE_PROTOBUF_VERSION >= 6032000 */
		.version = &lp_version_35_1,
		.user = &parameter, /* the parameter with its NUL bytes */
	};
	return lp_plugin_main(raw, parameter, &python_grpc_generator, out);
}

const UniGenerator gen_grpc_python_v1_85_1 = {
	.name = "grpc/python",
	.version = "v1.85.1",
	.generate = generate,
};
