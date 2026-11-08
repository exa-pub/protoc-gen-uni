/*
 * Copyright 2019 The gRPC Authors
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

/* grpc/java v1.85.1: a port of protoc-gen-grpc-java of grpc-java v1.85.1
 * (buf.build/grpc/java, https://github.com/grpc/grpc-java): compiler/src/java_plugin/cpp/
 * java_plugin.cpp (this file) and java_generator.{h,cpp} (java_generator.c), built with
 * protobuf 36.0 (buildscripts/make_dependencies.sh), which is libprotoc 36.2 for everything the
 * plugin uses. QualifiedClassName of compiler/java/names.cc is
 * pkg/languages/cpp/libprotoc/compiler/java; plugin.cc, descriptor.cc and io::Printer are
 * pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "libprotoc_java.h"
#include "uni.h"

#include <string.h>

/* compiler/build.gradle: cppCompiler.define("GRPC_VERSION", version) */
#define GRPC_VERSION "1.85.1"

#include "java_generator.c"

static char *JavaPackageToDir(const char *package_name)
{
	char *package_dir = xstrdup(package_name);
	for (char *c = package_dir; *c; c++)
		if (*c == '.')
			*c = '/';
	if (*package_dir)
		package_dir = xcat(package_dir, "/");
	return package_dir;
}

/* JavaGrpcGenerator::Generate */
static bool java_grpc_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *context,
			       Buf *error)
{
	(void)gen;
	(void)error;
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);

	ProtoFlavor flavor = NORMAL;
	GeneratedAnnotation generated_annotation = OMIT;

	bool disable_version = false;
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "lite")) {
			flavor = LITE;
		} else if (str_eq(keys[i], "noversion")) {
			disable_version = true;
		} else if (str_eq(keys[i], "@generated")) {
			if (str_eq(values[i], "omit"))
				generated_annotation = OMIT;
			else if (str_eq(values[i], "javax"))
				generated_annotation = JAVAX;
		}
	}

	char *package_name = ServiceJavaPackage(file);
	char *package_filename = JavaPackageToDir(package_name);
	for (int i = 0; i < file->n_services; ++i) {
		const LpService *service = &file->services[i];
		char *filename = xcat(package_filename, ServiceClassName(service), ".java");
		GenerateService(service, lp_context_open(context, filename), flavor, disable_version,
				generated_annotation);
	}
	return true;
}

static const char *const java_grpc_feature_extensions[] = {"pb.java", NULL};

static const LpGenerator java_grpc_generator = {
	.plugin_name = "/protoc-gen-grpc-java", /* argv[0]: the ENTRYPOINT of the bufbuild/plugins image */
	.generate = java_grpc_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
	.feature_extensions = java_grpc_feature_extensions,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &java_grpc_generator, out);
}

const UniGenerator gen_grpc_java_v1_85_1 = {
	.name = "grpc/java",
	.version = "v1.85.1",
	.generate = generate,
};
