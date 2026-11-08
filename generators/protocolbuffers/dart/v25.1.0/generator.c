/* Copyright (c) 2011, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* protocolbuffers/dart v25.1.0: protoc-gen-dart of protoc_plugin 25.1.0 (bin/protoc_plugin.dart,
 * `CodeGenerator(stdin, stdout).generate()`); the port of the protoc_plugin library is
 * pkg/languages/dart/protoc_plugin. */
#include "protoc_plugin.h"
#include "uni.h"

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return protoc_plugin_generate(raw, pb_bytes(req, CGR_PARAMETER), out);
}

const UniGenerator gen_protocolbuffers_dart_v25_1_0 = {
	.name = "protocolbuffers/dart",
	.version = "v25.1.0",
	.generate = generate,
};
