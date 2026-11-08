/**
 *
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* grpc/web v2.1.1: a port of protoc-gen-grpc-web of grpc-web 2.1.1 (buf.build/grpc/web,
 * https://github.com/grpc/grpc-web): javascript/net/grpc/web/generator/grpc_generator.cc
 * (grpc_generator.c, with its main below), built by Bazel (fastbuild: the debug checks of
 * io::Printer are on) against protobuf 29.1 (MODULE.bazel). plugin.cc, descriptor.cc and
 * io::Printer are pkg/languages/cpp/libprotoc with its 29.1 version (libprotoc_29.1.c).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "libprotoc.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

#include "grpc_generator.c"

/* GrpcCodeGenerator */
static const LpGenerator grpc_web_generator = {
	.plugin_name = "/protoc-gen-grpc-web", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = grpc_web_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	// Keep synced with protoc-gen-js: https://github.com/protocolbuffers/protobuf-javascript/blob/861c8020a5c0cba9b7cdf915dffde96a4421a1f4/generator/js_generator.h#L157-L158
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2024,
	.version = &lp_version_29_1,
};

/* main: `--version` is not reachable through a request */
static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &grpc_web_generator, out);
}

const UniGenerator gen_grpc_web_v2_1_1 = {
	.name = "grpc/web",
	.version = "v2.1.1",
	.generate = generate,
};
