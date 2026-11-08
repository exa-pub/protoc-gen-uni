// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/ruby v36.2: a port of the Ruby generator of protoc v36.2
 * (buf.build/protocolbuffers/ruby, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`ruby::Generator g; return PluginMain(argc, argv, &g);`) and
 * src/google/protobuf/compiler/ruby/ruby_generator.{h,cc} (ruby_generator.c). The libprotoc parts
 * (plugin.cc, descriptor.cc, retention.cc, io::Printer, ...) live in pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_protocolbuffers_ruby_v36_2), so versions never clash. */
#include "libprotoc.h"
#include "uni.h"

#include <string.h>

enum { FILEOPT_RUBY_PACKAGE = 45, MSGOPT_MAP_ENTRY = 7 };

#include "ruby_generator.c"

/* ruby_generator.h: Generator */
static const LpGenerator ruby_generator = {
	.plugin_name = "protoc-gen-ruby",
	.generate = ruby_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &ruby_generator, out);
}

const UniGenerator gen_protocolbuffers_ruby_v36_2 = {
	.name = "protocolbuffers/ruby",
	.version = "v36.2",
	.generate = generate,
};
