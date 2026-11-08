// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/php v36.2: a port of the PHP generator of protoc v36.2
 * (buf.build/protocolbuffers/php, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`php::Generator g; return PluginMain(argc, argv, &g);`) and
 * src/google/protobuf/compiler/php/php_generator.{h,cc} (php_generator.c). The libprotoc parts
 * (plugin.cc, descriptor.cc, retention.cc, io::Printer, compiler/php/names.cc, ...) live in
 * pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_protocolbuffers_php_v36_2), so versions never clash. */
#include "libprotoc.h"
#include "php_names.h"
#include "uni.h"

#include <string.h>

enum {
	FDS_FILE = 1,
	FILEOPT_PHP_NAMESPACE = 41, FILEOPT_PHP_METADATA_NAMESPACE = 44,
	OPT_DEPRECATED = 3, /* MessageOptions, FieldOptions, EnumOptions */
	MSGOPT_MAP_ENTRY = 7,
	EVOPT_JSON = 998, JSON_ENUMVALUE_STRING = 1, /* pb.enumvalue.json, JsonEnumValueOptions.string */
};

#include "php_generator.c"

/* php_generator.h: Generator */
static const LpGenerator php_generator = {
	.plugin_name = "protoc-gen-php",
	.generate = php_generate,
	.generate_all = php_generate_all,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &php_generator, out);
}

const UniGenerator gen_protocolbuffers_php_v36_2 = {
	.name = "protocolbuffers/php",
	.version = "v36.2",
	.generate = generate,
};
