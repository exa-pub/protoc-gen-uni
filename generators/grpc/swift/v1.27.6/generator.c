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

/* grpc/swift v1.27.6: a port of protoc-gen-grpc-swift from grpc-swift 1.27.6
 * (https://github.com/grpc/grpc-swift, Sources/protoc-gen-grpc-swift); this file ports
 * GenerateGRPC.swift. The plugin library is SwiftProtobufPluginLibrary of swift-protobuf 1.38.1
 * (pkg/languages/swift/swift-protobuf), the reflection data is serialized like SwiftProtobuf's
 * binary encoder (pkg/pb) and encoded with Foundation's Data.base64EncodedString(). */
#include "swift_protobuf.h"
#include "uni.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "options.c"
#include "grpc_generator.c"
#include "client.c"
#include "server.c"

/* A count of generated files by desired name (actual name may differ to avoid collisions). */
static Map generated_file_names;

/* splitPath(pathname:), from swift-protobuf's protoc-gen-swift/StringUtils.swift (names are
 * ASCII, so Characters are bytes). */
static void split_path(const char *pathname, Buf *dir, Buf *base)
{
	Buf suffix = {0};
	for (const char *p = pathname; *p; p++) {
		if (*p == '/') {
			buf_add(dir, base->p, base->n);
			buf_add(dir, suffix.p, suffix.n);
			buf_addc(dir, '/');
			base->n = 0;
			suffix.n = 0;
		} else if (*p == '.') {
			buf_add(base, suffix.p, suffix.n);
			suffix.n = 0;
			buf_addc(&suffix, '.');
		} else {
			buf_addc(&suffix, *p);
		}
	}
	bool valid_suffix = !suffix.n || suffix.p[0] == '.';
	if (!valid_suffix)
		buf_add(base, suffix.p, suffix.n);
}

/* outputFileName(component:fileDescriptor:fileNamingOption:extension:) */
static char *output_file_name(const char *component, const SwFile *file, FileNaming naming, const char *extension)
{
	Buf dir = {0}, base = {0};
	split_path(file->name, &dir, &base);
	const char *ext = xcat(".", component, ".", extension);
	switch (naming) {
	case FILE_NAMING_FULL_PATH:
		return xcat(buf_str(&dir), buf_str(&base), ext);
	case FILE_NAMING_PATH_TO_UNDERSCORES:
		return xcat(str_replace_all(buf_str(&dir), "/", "_"), buf_str(&base), ext);
	case FILE_NAMING_DROP_PATH:
		break;
	}
	return xcat(buf_str(&base), ext);
}

/* uniqueOutputFileName(fileDescriptor:fileNamingOption:component:extension:) */
static char *unique_output_file_name(const SwFile *file, FileNaming naming, const char *extension)
{
	char *default_name = output_file_name("grpc", file, naming, extension);
	int *count = map_get(&generated_file_names, default_name);
	if (count) {
		char *name = output_file_name(xfmt("%d.grpc", *count), file, naming, extension);
		++*count;
		return name;
	}
	count = xalloc(sizeof *count);
	*count = 1;
	map_put(&generated_file_names, default_name, count);
	return default_name;
}

/* generateReflectionData */
static void generate_reflection_data(SwFile *descriptor, GeneratorOptions *options, SwOutputs *outputs)
{
	char *file_name = unique_output_file_name(descriptor, options->file_naming, "reflection");
	/* extractProto(options:) with includeSourceCodeInfo: the proto as received. */
	Buf serialized = {0};
	pb_encode(descriptor->proto, &serialized, NULL, NULL);
	/* Data.base64EncodedString(): the standard alphabet with padding. */
	Buf reflection_data = {0};
	buf_adds(&reflection_data, base64_raw_std((const uint8_t *)serialized.p, serialized.n));
	while (reflection_data.n % 4)
		buf_addc(&reflection_data, '=');
	sw_outputs_add(outputs, file_name, buf_view(&reflection_data));
}

static void generate(SwFile **files, int n_files, Str parameter, SwOutputs *outputs)
{
	GeneratorOptions *options = parse_options(parameter);
	generated_file_names = (Map){0};
	for (int i = 0; i < n_files; i++) {
		SwFile *descriptor = files[i];
		if (options->generate_reflection_data)
			generate_reflection_data(descriptor, options, outputs);
		if (!descriptor->services.n)
			continue;
		if (options->generate_client || options->generate_server || options->generate_test_client) {
			/* generateV1Stubs */
			char *file_name = unique_output_file_name(descriptor, options->file_naming, "swift");
			sw_outputs_add(outputs, file_name, generator_code(descriptor, options));
		}
	}
}

static const SwCodeGenerator code_generator = {
	.supported_features = 3, /* .proto3Optional, .supportsEditions */
	.minimum_edition = SW_EDITION_PROTO2,
	.maximum_edition = SW_EDITION_2023,
	.edition_defaults = &sw_edition_defaults_1_38_1,
	.generate = generate,
};

static const char *generate_request(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	/* No customOptionExtensions: the pb.enumvalue.json option is not decoded. */
	const char *err = sw_check_request(req, false);
	if (err)
		return xcat("protoc-gen-grpc-swift: Received an unparsable request from the compiler: ", err);
	sw_generate_code(req, &code_generator, out);
	return NULL;
}

const UniGenerator gen_grpc_swift_v1_27_6 = {
	.name = "grpc/swift",
	.version = "v1.27.6",
	.generate = generate_request,
};
