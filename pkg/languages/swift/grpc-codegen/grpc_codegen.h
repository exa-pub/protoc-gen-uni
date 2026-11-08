/*
 * Copyright 2023, gRPC Authors All rights reserved.
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

/* A port of GRPCCodeGen from grpc-swift-2 2.4.4 (Sources/GRPCCodeGen): the IDL-independent code
 * generator of gRPC Swift 2. A CodeGenerationRequest (services and methods with their names and
 * documentation) is translated into the StructuredSwift representation (struct.c and the
 * StructuredSwift+*.swift builders) and rendered by TextBasedRenderer (renderer.c). The API is
 * the public API of the Swift module (prefix gc_); each .c file names the Swift files it ports.
 * Strings are UTF-8; names are NUL-terminated, documentation (from proto comments) is Str. */
#ifndef GRPC_CODEGEN_H
#define GRPC_CODEGEN_H

#include "util.h"

/* CodeGenerator.Config.AccessLevel */
typedef enum { GC_LEVEL_INTERNAL, GC_LEVEL_PUBLIC, GC_LEVEL_PACKAGE } GcAccessLevel;

/* CodeGenerator.Config.AvailabilityAnnotations.Platform */
typedef struct {
	const char *os, *version;
} GcPlatform;

/* CodeGenerator.Config */
typedef struct {
	GcAccessLevel access_level;
	bool access_level_on_imports;
	int indentation;
	bool client, server;
	const char *grpc_core_module_name;
	/* availability: .default (macOS15Aligned) when custom is false, else .custom(platforms) */
	bool custom_availability;
	const GcPlatform *platforms;
	int n_platforms;
} GcConfig;

/* Config(accessLevel:accessLevelOnImports:client:server:indentation:) */
GcConfig gc_config(GcAccessLevel access_level, bool access_level_on_imports, bool client, bool server, int indentation);

/* Dependency: only module and accessLevel (item, spi and preconcurrency keep their defaults:
 * nil, nil, .notRequired). */
typedef struct {
	const char *module;
	GcAccessLevel access_level;
} GcDependency;

/* MethodName, ServiceName */
typedef struct {
	const char *identifying_name, *type_name, *function_name;
} GcMethodName;
typedef struct {
	const char *identifying_name, *type_name, *property_name;
} GcServiceName;

/* MethodDescriptor */
typedef struct {
	Str documentation;
	GcMethodName name;
	bool is_input_streaming, is_output_streaming;
	const char *input_type, *output_type;
} GcMethod;

/* ServiceDescriptor */
typedef struct {
	Str documentation;
	GcServiceName name;
	GcMethod *methods;
	int n_methods;
} GcService;

/* CodeGenerationRequest */
typedef struct {
	const char *file_name;
	Str leading_trivia;
	GcDependency *dependencies;
	int n_dependencies;
	GcService *services;
	int n_services;
	/* makeSerializerCodeSnippet, makeDeserializerCodeSnippet */
	char *(*make_serializer)(void *ctx, const char *message_type);
	char *(*make_deserializer)(void *ctx, const char *message_type);
	void *ctx;
} GcRequest;

/* CodeGenerator.generate(_:): the contents of the SourceFile. Throws (fail()) a CodeGenError. */
Str gc_generate(const GcConfig *config, const GcRequest *request);

#endif
