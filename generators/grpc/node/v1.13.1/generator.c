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

/* grpc/node v1.13.1: a port of grpc_node_plugin of grpc-tools 1.13.1 (buf.build/grpc/node,
 * https://github.com/grpc/grpc-node, tag grpc-tools@1.13.1): packages/grpc-tools/src/
 * node_plugin.cc (NodeGrpcGenerator and its main, below) and node_generator.cc
 * (node_generator.c). grpc-tools vendors the helpers of grpc v1.25.0 (pkg/languages/cpp/grpc with
 * its 1.25.0 part) and links protobuf v3.19.1 (its deps/protobuf submodule):
 * pkg/languages/cpp/libprotoc with its 3.19.1 version (lp_version_3_19_1) and the io::Printer of
 * printer_3.21.12.h, which 3.19.1 shares.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc.h"
#include "printer_3.21.12.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

#include "node_generator.c"

/* The options of NodeGrpcGenerator::Generate: grpc_generator::tokenize(parameter, ",") and exact
 * matches (unknown options are ignored). The parameter is a std::string: NUL bytes are part of
 * the tokens. */
static COLD NodeParameters node_parse_parameters(Str parameter)
{
	NodeParameters p = {0};
	size_t pos = 0;
	while (parameter.n) {
		const char *comma = memchr(parameter.p + pos, ',', parameter.n - pos);
		size_t end = comma ? (size_t)(comma - parameter.p) : parameter.n;
		Str token = {parameter.p + pos, end - pos};
		if (str_eqc(token, "generate_package_definition"))
			p.generate_package_definition = true;
		else if (str_eqc(token, "grpc_js"))
			p.grpc_js = true;
		else if (str_eqc(token, "omit_serialize_instanceof"))
			p.omit_serialize_instanceof = true;
		if (!comma)
			break;
		pos = end + 1;
	}
	return p;
}

/* NodeGrpcGenerator::Generate (the parameter was parsed into g->user) */
static COLD bool generate_file(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			       Buf *error)
{
	(void)parameter, (void)error;
	Buf code = {0};
	node_generate_file(file, g->user, &code);
	if (code.n == 0)
		return true;

	/* Get output file name */
	char *file_name = node_js_service_filename(file->name);
	buf_add(lp_context_open(ctx, file_name), code.p, code.n);
	return true;
}

/* main: PluginMain(argc, argv, &generator) */
static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	NodeParameters params = node_parse_parameters(pb_bytes(req, CGR_PARAMETER));
	const LpGenerator node_grpc_generator = {
		.plugin_name = "/grpc_node_plugin", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
		.generate = generate_file,
		.supported_features = 1, /* FEATURE_PROTO3_OPTIONAL */
		.version = &lp_version_3_19_1,
		.user = &params,
	};
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &node_grpc_generator, out);
}

/* parses_raw: lp_plugin_main parses raw with the semantics of protobuf 3.19.1 (100 levels of
 * nesting, the request at level 0) and generate takes only the parameter from req. */
const UniGenerator gen_grpc_node_v1_13_1 = {
	.name = "grpc/node",
	.version = "v1.13.1",
	.generate = generate,
	.parses_raw = true,
};
