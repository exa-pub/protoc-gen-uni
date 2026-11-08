/* Copyright (c) 2022 Nipunn Koorapati
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

/* mypy-protobuf v5.1.0 (https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/mypy_protobuf/main.py):
 * the code shared by its two plugins, protoc-gen-mypy (main()) and protoc-gen-mypy_grpc (grpc()).
 * The entry points themselves (and how a Python exception is reported) belong to the generators.
 * Every function raises Python exceptions with fail(). */
#ifndef MYPY_PROTOBUF_H
#define MYPY_PROTOBUF_H

#include "pb.h"

/* The options: substrings of the whole parameter ("quiet" in request.parameter). quiet only
 * silences messages on stderr. */
typedef struct {
	bool readable_stubs, relax_strict_optional_primitives, use_default_deprecation_warnings,
		generate_concrete_servicer_stubs;
} MypyOptions;

/* GRPCType */
typedef enum { MYPY_GRPC_SYNC, MYPY_GRPC_ASYNC, MYPY_GRPC_BOTH } MypyGrpcType;

typedef struct MypyDescriptors MypyDescriptors;

/* request.ParseFromString with mypy_protobuf/extensions.proto registered: the checks upb makes
 * on the extensions beyond what pkg/pb parsed (DecodeError). */
void mypy_check_extensions(const PbMsg *req);

/* Descriptors(request) */
MypyDescriptors *mypy_descriptors(PbMsg *req);

/* The "<option>" in request.parameter tests (a TypeError when upb returns the parameter as
 * bytes). */
MypyOptions mypy_options(Str parameter);

/* GRPCType.from_parameter: false where upstream raises
 * ValueError("Cannot specify both only_sync and only_async"). */
bool mypy_grpc_type_from_parameter(Str parameter, MypyGrpcType *type);

/* The fields of the response that code_generation() sets: supported_features and the editions. */
void mypy_response_features(Buf *out);

/* generate_mypy_stubs and generate_mypy_grpc_stubs: append the CodeGeneratorResponse.file
 * fields to files. */
void mypy_generate_stubs(MypyDescriptors *d, const MypyOptions *o, Buf *files);
void mypy_generate_grpc_stubs(MypyDescriptors *d, const MypyOptions *o, MypyGrpcType type, Buf *files);

#endif
