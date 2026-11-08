/*
 * Copyright 2024, gRPC Authors All rights reserved.
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
 */

/* grpc/swift-protobuf v2.5.0: a port of protoc-gen-grpc-swift-2 from grpc-swift-protobuf 2.5.0
 * (https://github.com/grpc/grpc-swift-protobuf, Sources/protoc-gen-grpc-swift-2 and
 * Sources/GRPCProtobufCodeGen). The code generator GRPCCodeGen of grpc-swift-2 2.4.4 is in
 * pkg/languages/swift/grpc-codegen, SwiftProtobufPluginLibrary 1.39.0 in
 * pkg/languages/swift/swift-protobuf.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "grpc_codegen.h"
#include "swift_protobuf.h"
#include "swift_std.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

#include "options.c"
#include "protobuf_code_gen.c"

/* ---- GenerateGRPC.swift ---- */

/* splitPath(pathname:) from protoc-gen-swift's StringUtils.swift: dir is everything up to the last
 * "/" (included), base the last component without its last ".ext". */
static void split_path(const char *pathname, char **dir, char **base)
{
	const char *slash = strrchr(pathname, '/');
	const char *last = slash ? slash + 1 : pathname;
	const char *dot = strrchr(last, '.');
	*dir = xstrndup(pathname, (size_t)(last - pathname));
	*base = dot ? xstrndup(last, (size_t)(dot - last)) : xstrdup(last);
}

/* outputFileName(component:fileDescriptor:fileNamingOption:extension:) */
static char *output_file_name(const char *component, const SwFile *file, FileNaming naming)
{
	char *dir, *base;
	split_path(file->name, &dir, &base);
	const char *ext = xcat(".", component, ".swift");
	switch (naming) {
	case FILE_NAMING_FULL_PATH:
		return xcat(dir, base, ext);
	case FILE_NAMING_PATH_TO_UNDERSCORES:
		return xcat(str_replace_all(dir, "/", "_"), base, ext);
	case FILE_NAMING_DROP_PATH:
		break;
	}
	return xcat(base, ext);
}

/* uniqueOutputFileName: a count of generated files by desired name (generatedFileNames). */
static char *unique_output_file_name(Map *generated_file_names, const SwFile *file, FileNaming naming)
{
	char *default_name = output_file_name("grpc", file, naming);
	intptr_t count = (intptr_t)map_get(generated_file_names, default_name);
	map_put(generated_file_names, default_name, (void *)(count + 1));
	if (count)
		return output_file_name(xfmt("%lld.grpc", (long long)count), file, naming);
	return default_name;
}

static void plugin_generate(SwFile **files, int n_files, Str parameter, SwOutputs *outputs)
{
	Options *o = parse_options(parameter);
	Map generated_file_names = {0};
	for (int i = 0; i < n_files; i++) {
		char *name = unique_output_file_name(&generated_file_names, files[i], o->file_naming);
		Str contents = generate_code(o, files[i]);
		sw_outputs_add(outputs, name, contents);
	}
}

static const SwCodeGenerator grpc_swift_protobuf_plugin = {
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = SW_EDITION_PROTO2,
	.maximum_edition = SW_EDITION_2026,
	.generate = plugin_generate,
};

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	const char *err = sw_check_request(req, false);
	if (err)
		return xcat("protoc-gen-grpc-swift-2: Received an unparsable request from the compiler: ", err);
	sw_generate_code(req, &grpc_swift_protobuf_plugin, out);
	return NULL;
}

const UniGenerator gen_grpc_swift_protobuf_v2_5_0 = {
	.name = "grpc/swift-protobuf",
	.version = "v2.5.0",
	.generate = generate,
};
