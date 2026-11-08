/* Copyright (c) protoc-bridge authors (Nadav Samet and contributors).
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

/* A port of protoc-bridge v0.9.10, module protoc-gen (protoc-gen/src/main/scala/protocgen:
 * CodeGenApp.scala, CodeGenRequest.scala, CodeGenResponse.scala), the higher-level API of the JVM
 * protoc plugins built with protoc-bridge, on the protobuf-java descriptors. */
#ifndef PROTOCGEN_H
#define PROTOCGEN_H

#include "protobuf_java.h"

/* CodeGenRequest */
typedef struct {
	Str parameter;        /* req.getParameter(), a Java string (java_string) */
	Vec files_to_generate; /* JFile* */
	Vec all_protos;       /* JFile*: filesMap.values (request order, the last file of a name wins) */
	PbMsg *as_proto;      /* the request, parsed with the registry */
} CodeGenRequest;

/* A CodeGeneratorResponse.File */
typedef struct {
	const char *name, *content;
} CodeGenFile;

/* CodeGenResponse.succeed(files, supportedFeatures): process fills files (CodeGenFile*) and
 * supported_features (the sum of the Feature numbers). CodeGenResponse.fail(message) and exceptions
 * are fail(): the error of the response is the message, or the stack trace of an exception, of
 * which the first line is reproduced (java_exception). */
typedef struct {
	Vec files;
	uint64_t supported_features;
} CodeGenResponse;

/* CodeGenApp.run for the UniGenerator.generate of a plugin: parses raw with the registry (the
 * CodeGeneratorRequest schema with the extensions the plugin registers, registerExtensions), builds
 * the CodeGenRequest (FileDescriptor.buildFrom of every file) and writes the response of
 * process(request, response, arg) to out. The parameter comes from req, the request without the
 * uni-* options. Returns NULL. */
const char *protocgen_run(PbMsg *req, Str raw, const PbMsgDef *registry,
			  void (*process)(const CodeGenRequest *request, CodeGenResponse *response, void *arg), void *arg,
			  Buf *out);

#endif
