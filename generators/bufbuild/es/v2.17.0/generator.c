/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* bufbuild/es v2.17.0: a port of protoc-gen-es v2.17.0 (buf.build/bufbuild/es), the TypeScript and
 * JavaScript code generator of Protobuf-ES (https://github.com/bufbuild/protobuf-es), from
 * packages/protoc-gen-es/src. The plugin framework and the runtime parts it needs live in
 * pkg/languages/ecmascript.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_bufbuild_es_v2_17_0), so versions never clash. */
#include "protoplugin.h"
#include "uni.h"

#include <string.h>

/* The options of the plugin (protoc-gen-es-plugin.ts). */
typedef struct {
	bool json_types, legacy_required, protovalidate_required, erasable_syntax;
} Options;

#define OPTS(s) ((const Options *)(s)->plugin_options)

static bool has_valid_types(const Options *o) { return o->legacy_required || o->protovalidate_required; }

#include "valid_types.c"
#include "util.c"
#include "plugin.c"

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	es_plugin_run(&protoc_gen_es, req, out);
	return NULL;
}

const UniGenerator gen_bufbuild_es_v2_17_0 = {
	.name = "bufbuild/es",
	.version = "v2.17.0",
	.generate = generate,
};
