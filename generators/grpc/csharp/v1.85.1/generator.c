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

/* grpc/csharp v1.85.1: a port of grpc_csharp_plugin of grpc v1.85.1 (buf.build/grpc/csharp,
 * https://github.com/grpc/grpc): src/compiler/csharp_plugin.cc (CSharpGrpcGenerator and its
 * main) and the C# generator of grpc_plugin_support (csharp_generator.c). The helpers shared by
 * the grpc plugins are pkg/languages/cpp/grpc; the plugin links protobuf 35.1 (MODULE.bazel):
 * pkg/languages/cpp/libprotoc with its 35.1 version (lp_version_35_1) and its C# names
 * (csharp_names.h, the GRPC_CUSTOM_CSHARP_* macros of config_protobuf.h).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "csharp_names.h"
#include "grpc.h"
#include "uni.h"

#include <string.h>

#include "csharp_generator.c"

/* CSharpGrpcGenerator::Generate */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	(void)g;
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);

	bool generate_client = true;
	bool generate_server = true;
	bool internal_access = false;
	bool append_async_suffix = false;
	const char *base_namespace = "";
	bool base_namespace_present = false;

	/* the suffix that will get appended to the name generated from the name of the original
	 * .proto file */
	const char *file_suffix = "Grpc.cs";
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "no_client")) {
			generate_client = false;
		} else if (str_eq(keys[i], "no_server")) {
			generate_server = false;
		} else if (str_eq(keys[i], "internal_access")) {
			internal_access = true;
		} else if (str_eq(keys[i], "file_suffix")) {
			file_suffix = values[i];
		} else if (str_eq(keys[i], "base_namespace")) {
			/* Support for base_namespace option in this plugin is experimental. The option
			 * may be removed or file names generated may change in the future. */
			base_namespace = values[i];
			base_namespace_present = true;
		} else if (str_eq(keys[i], "append_async_suffix")) {
			append_async_suffix = str_eq(values[i], "true");
		} else {
			buf_adds(error, "Unknown generator option: ");
			buf_adds(error, keys[i]);
			return false;
		}
	}

	Str code = csg_get_services(file, generate_client, generate_server, internal_access, append_async_suffix);
	if (code.n == 0)
		return true; /* don't generate a file if there are no services */

	/* Get output file name. */
	char *file_name, *file_error = NULL;
	if (!csg_services_filename(file, file_suffix, base_namespace_present, base_namespace, &file_name,
				   &file_error)) {
		if (file_error)
			buf_adds(error, file_error);
		return false;
	}
	buf_add_str(lp_context_open(ctx, file_name), code); /* WriteRaw(code.data(), code.size()) */
	return true;
}

static const LpGenerator csharp_grpc_generator = {
	.plugin_name = "/grpc_csharp_plugin_binary", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = generate_file,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2024, /* GOOGLE_PROTOBUF_VERSION >= 6032000 */
	.version = &lp_version_35_1,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &csharp_grpc_generator, out);
}

const UniGenerator gen_grpc_csharp_v1_85_1 = {
	.name = "grpc/csharp",
	.version = "v1.85.1",
	.generate = generate,
};
