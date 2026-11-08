/*
 *
 * Copyright 2016 gRPC authors.
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

/* grpc/php v1.85.1: a port of grpc_php_plugin of grpc v1.85.1 (buf.build/grpc/php,
 * https://github.com/grpc/grpc): src/compiler/php_plugin.cc (PHPGrpcGenerator and its main) and
 * the PHP generator of grpc_plugin_support (php_generator.c). The helpers shared by the grpc
 * plugins are pkg/languages/cpp/grpc; the plugin links protobuf 35.1 (MODULE.bazel):
 * pkg/languages/cpp/libprotoc with its 35.1 version (lp_version_35_1), whose php names reserve
 * 80 words.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc.h"
#include "php_names.h"
#include "uni.h"

#include <string.h>

#include "php_generator.c"

/* PHPGrpcGenerator::GenerateService */
static COLD void generate_service(const LpFile *file, const LpService *service, const char *class_suffix,
				  bool is_server, LpContext *ctx)
{
	char *code = php_generate_file(file, service, class_suffix, is_server);
	/* Get output file name */
	char *file_name = php_service_filename(file, service, class_suffix, is_server);
	buf_adds(lp_context_open(ctx, file_name), code);
}

/* PHPGrpcGenerator::Generate */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	(void)g;
	if (file->n_services == 0)
		return true;

	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	bool generate_server = false;
	const char *class_suffix = "";
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "class_suffix")) {
			class_suffix = values[i];
		} else if (str_eq(keys[i], "generate_server")) {
			generate_server = true;
		} else {
			buf_adds(error, "unsupported options: ");
			buf_adds(error, keys[i]);
			return false;
		}
	}

	for (int i = 0; i < file->n_services; i++) {
		generate_service(file, &file->services[i], class_suffix, false, ctx);
		if (generate_server)
			generate_service(file, &file->services[i], class_suffix, true, ctx);
	}
	return true;
}

static const LpGenerator php_grpc_generator = {
	.plugin_name = "/grpc_php_plugin_binary", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = generate_file,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2024, /* GOOGLE_PROTOBUF_VERSION >= 6032000 */
	.version = &lp_version_35_1,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &php_grpc_generator, out);
}

const UniGenerator gen_grpc_php_v1_85_1 = {
	.name = "grpc/php",
	.version = "v1.85.1",
	.generate = generate,
};
