/* Copyright 2022-2025 The Connect Authors
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

/* A port of ConnectPluginUtilities (connect-swift 1.2.3, Plugins/ConnectPluginUtilities), the
 * library shared by protoc-gen-connect-swift and protoc-gen-connect-swift-mocks, on
 * SwiftProtobufPluginLibrary 1.37.0 (pkg/languages/swift/swift-protobuf). Prefix cpu_. */
#ifndef CONNECT_PLUGIN_UTILITIES_H
#define CONNECT_PLUGIN_UTILITIES_H

#include "swift_protobuf.h"

/* ---- GeneratorOptions.swift ---- */

typedef enum { CPU_FULL_PATH, CPU_PATH_TO_UNDERSCORES, CPU_DROP_PATH } CpuFileNaming;
typedef enum { CPU_INTERNAL, CPU_PUBLIC, CPU_PACKAGE } CpuVisibility;

typedef struct {
	Vec extra_module_imports; /* Str * */
	CpuFileNaming file_naming;
	bool generate_async_methods, generate_callback_methods, generate_service_metadata;
	bool keep_method_casing;
	SwModuleMappings *proto_to_module_mappings;
	Str swift_protobuf_module_name;
	CpuVisibility visibility;
} CpuOptions;

/* GeneratorOptions(commandLineParameters:): throws with fail(). */
CpuOptions *cpu_parse_options(Str parameter);

/* ---- FilePathComponents.swift ---- */

typedef struct {
	Str directory, base, suffix;
} CpuFilePathComponents;

CpuFilePathComponents cpu_file_path_components(const char *path);
char *cpu_output_file_path(const CpuFilePathComponents *c, const char *path_extension, CpuFileNaming option);

/* ---- MethodDescriptor+Extensions.swift, ServiceDescriptor+Extensions.swift ---- */

char *cpu_method_path(const SwMethod *m);
const char *cpu_method_name(const SwMethod *m, const CpuOptions *o);
char *cpu_callback_signature(const SwMethod *m, SwNamer *namer, bool include_defaults, const CpuOptions *o);
char *cpu_async_await_signature(const SwMethod *m, SwNamer *namer, bool include_defaults, const CpuOptions *o);
char *cpu_service_path(const SwService *s);
char *cpu_implementation_name(const SwService *s, SwNamer *namer);
char *cpu_protocol_name(const SwService *s, SwNamer *namer);

/* ---- Generator.swift ---- */

typedef struct CpuGenerator CpuGenerator;
struct CpuGenerator {
	/* Overridable: outputFileExtension and printContent(for:), which calls cpu_print_content
	 * (super) first. */
	const char *output_file_extension;
	void (*print_content)(CpuGenerator *g, SwFile *descriptor);
	/* "protoc-gen-...": the program name in the errors of an unparsable request. */
	const char *program_name;

	/* The state of the file being printed. */
	SwNamer *namer;
	CpuOptions *options;
	Vec *services; /* SwService *: the services of the file */
	Vec needed_modules;
	SwPrinter printer;
};

/* Generator.printContent(for:): the header. */
void cpu_print_content(CpuGenerator *g, SwFile *descriptor);
void cpu_indent(CpuGenerator *g);
void cpu_outdent(CpuGenerator *g);
/* printLine(_:): "" prints an empty line. */
void cpu_print_line(CpuGenerator *g, const char *line);
void cpu_print_line_str(CpuGenerator *g, Str line);
void cpu_printf_line(CpuGenerator *g, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void cpu_print_comments_if_needed(CpuGenerator *g, SwKind k, const void *entity);
/* printModuleImports(adding:) */
void cpu_print_module_imports(CpuGenerator *g, const char *const *additional, int n_additional);

/* CodeGenerator.main for a Generator subclass: checks the request as SwiftProtobuf decodes it and
 * runs generateCode(request:generator:), which writes the CodeGeneratorResponse to out. Returns
 * NULL, or the error of a request that cannot be decoded. */
const char *cpu_main(CpuGenerator *g, PbMsg *req, Buf *out);

#endif
