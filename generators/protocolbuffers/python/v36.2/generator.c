// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/python v36.2: a port of the Python generator of protoc v36.2
 * (buf.build/protocolbuffers/python, https://github.com/protocolbuffers/protobuf): the plugin
 * main of bufbuild/plugins (src/google/protobuf/compiler/python/plugin_main.cc without
 * GOOGLE_PROTOBUF_RUNTIME_INCLUDE_BASE: `Generator generator; return PluginMain(argc, argv,
 * &generator);`) and src/google/protobuf/compiler/python/generator.{h,cc} (python_generator.c).
 * The helpers and PyiGenerator (pyi_out) are pkg/languages/cpp/libprotoc/compiler/python, the
 * rest of libprotoc pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_protocolbuffers_python_v36_2). */
#include "libprotoc_python.h"
#include "uni.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "python_generator.c"

/* generator.h: Generator */
static const LpGenerator python_generator = {
	.plugin_name = "protoc-gen-python",
	.generate = py_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &python_generator, out);
}

const UniGenerator gen_protocolbuffers_python_v36_2 = {
	.name = "protocolbuffers/python",
	.version = "v36.2",
	.generate = generate,
};
