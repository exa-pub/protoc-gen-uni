// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://protobuf.dev/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/* protocolbuffers/js v4.0.3: a port of protoc-gen-js of protobuf-javascript v4.0.3
 * (buf.build/protocolbuffers/js, https://github.com/protocolbuffers/protobuf-javascript): the
 * plugin main generator/protoc-gen-js.cc (PluginMain with js::Generator), generator/js_generator.cc
 * (js_generator.c) and generator/well_known_types_embed.cc (well_known_types_embed.c). The plugin
 * links libprotoc 33.4 (MODULE.bazel: protobuf 33.4): pkg/languages/cpp/libprotoc with its 33.4
 * version (lp_version_33_4).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "libprotoc.h"
#include "uni.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* descriptor.proto option fields read by the generator */
enum {
	MSGOPT_MESSAGE_SET_WIRE_FORMAT = 1,
	FOPT_JSTYPE = 6, JSTYPE_JS_STRING = 1,
	ENUMOPT_ALLOW_ALIAS = 2,
};

#include "well_known_types_embed.c"
#include "js_generator.c"

/* js_generator.h: Generator */
static const LpGenerator js_generator = {
	.plugin_name = "protoc-gen-js",
	.generate = js_generate,
	.generate_all = js_generate_all,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2024,
	.version = &lp_version_33_4,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &js_generator, out);
}

const UniGenerator gen_protocolbuffers_js_v4_0_3 = {
	.name = "protocolbuffers/js",
	.version = "v4.0.3",
	.generate = generate,
};
