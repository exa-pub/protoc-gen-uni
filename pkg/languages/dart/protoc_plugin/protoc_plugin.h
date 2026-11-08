/* protoc_plugin 25.1.0 (https://github.com/google/protobuf.dart/tree/protoc_plugin-v25.1.0/
 * protoc_plugin), the Dart code generator of protocolbuffers/dart, as a library: the generator
 * itself is bin/protoc_plugin.dart, `CodeGenerator(stdin, stdout).generate()`. */
#ifndef PROTOC_PLUGIN_H
#define PROTOC_PLUGIN_H

#include "util.h"

/* CodeGenerator.generate: parses the serialized CodeGeneratorRequest raw with the schemas of
 * protoc_plugin (the parameter is replaced by `parameter`) and appends the serialized
 * CodeGeneratorResponse to *out. Returns NULL, or an error if the request cannot be parsed. */
const char *protoc_plugin_generate(Str raw, Str parameter, Buf *out);

#endif
