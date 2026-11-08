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

/* grpc/ruby v1.85.1: a port of grpc_ruby_plugin of grpc v1.85.1 (buf.build/grpc/ruby,
 * https://github.com/grpc/grpc): src/compiler/ruby_plugin.cc (RubyGrpcGenerator and its main)
 * and the Ruby generator of grpc_plugin_support (ruby_generator.c). The helpers shared by the
 * grpc plugins are pkg/languages/cpp/grpc; the plugin links protobuf 35.1 (MODULE.bazel):
 * pkg/languages/cpp/libprotoc with its 35.1 version (lp_version_35_1).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc.h"
#include "uni.h"

#include <string.h>

enum { FILEOPT_RUBY_PACKAGE = 45 };

#include "ruby_generator.c"

/* RubyGrpcGenerator::Generate (the parameter is ignored) */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	(void)g, (void)parameter, (void)error;
	char *code = rb_get_services(file);
	if (!*code)
		return true; /* don't generate a file if there are no services */

	/* Get output file name. */
	char *file_name;
	if (!rb_services_filename(file, &file_name))
		return false;
	buf_adds(lp_context_open(ctx, file_name), code);
	return true;
}

static const LpGenerator ruby_grpc_generator = {
	.plugin_name = "/grpc_ruby_plugin_binary", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = generate_file,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2024, /* GOOGLE_PROTOBUF_VERSION >= 6032000 */
	.version = &lp_version_35_1,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &ruby_grpc_generator, out);
}

const UniGenerator gen_grpc_ruby_v1_85_1 = {
	.name = "grpc/ruby",
	.version = "v1.85.1",
	.generate = generate,
};
