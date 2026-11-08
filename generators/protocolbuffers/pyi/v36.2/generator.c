// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/pyi v36.2: a port of the Python stub (.pyi) generator of protoc v36.2
 * (buf.build/protocolbuffers/pyi, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (src/google/protobuf/compiler/python/pyi_plugin_main.cc:
 * `PyiGenerator generator; return PluginMain(argc, argv, &generator);`). PyiGenerator itself
 * (pyi_generator.{h,cc}) is shared with the Python generator's pyi_out option and lives in
 * pkg/languages/cpp/libprotoc/compiler/python, the rest of libprotoc in
 * pkg/languages/cpp/libprotoc.
 *
 * Unity build: everything but the generator below is static. A new version is a copy of the
 * directory with the exported name changed (gen_protocolbuffers_pyi_v36_2). */
#include "libprotoc_python.h"
#include "uni.h"

/* pyi_generator.h: PyiGenerator */
static const LpGenerator pyi_generator = {
	.plugin_name = "protoc-gen-pyi",
	.generate = lpy_pyi_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &pyi_generator, out);
}

const UniGenerator gen_protocolbuffers_pyi_v36_2 = {
	.name = "protocolbuffers/pyi",
	.version = "v36.2",
	.generate = generate,
};
