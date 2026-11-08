/* Copyright 2024-2025 Buf Technologies, Inc.
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

/* Port of the parts of the Go library github.com/bufbuild/protoplugin at commit 750e09ce93e1
 * (v0.0.0-20250218205857-750e09ce93e1) that a plugin's output depends on: the validation of the
 * CodeGeneratorRequest (NewRequest, validate.go), the ResponseWriter with the validation and
 * normalization of the CodeGeneratorResponse (response_writer.go, validate.go, errors.go) and the
 * marshaling of the response with protobuf-go (proto.Marshal: known fields in field number
 * order).
 *
 *	const char *err = gopp_validate_request(req);   // NewRequest; err: exit 1 upstream
 *	GoppResponseWriter *w = gopp_response_writer_new(NULL, NULL);
 *	gopp_add_file(w, name, content);
 *	gopp_set_feature_proto3_optional(w);
 *	err = gopp_to_response(w, &out);                // ToCodeGeneratorResponse + proto.Marshal
 *
 * Errors are returned as the text of the Go error (NULL: no error). Not ported: Run/Main (the uni
 * generators drive the plugin), the Request accessors (callers use the parsed request and
 * compiler/protogen directly), CompilerVersion and WithSourceRetentionOptions. */
#ifndef GO_PROTOPLUGIN_H
#define GO_PROTOPLUGIN_H

#include "pb.h"

/* CodeGeneratorResponse.Feature */
enum { GOPP_FEATURE_PROTO3_OPTIONAL = 1, GOPP_FEATURE_SUPPORTS_EDITIONS = 2 };

/* validateCodeGeneratorRequest of NewRequest: req is a CodeGeneratorRequest parsed with the
 * protobuf-go schema (pg_parse_request). Returns NULL or the error ("CodeGeneratorRequest: ..."). */
const char *gopp_validate_request(const PbMsg *req);

/* A CodeGeneratorResponse.File. The has_* flags are the presence of the proto2 fields (Go's
 * nil pointers); generated_code_info is kept serialized. */
typedef struct {
	Str name, insertion_point, content, generated_code_info;
	bool has_name, has_insertion_point, has_content, has_generated_code_info;
} GoppFile;

typedef struct GoppResponseWriter GoppResponseWriter;

/* NewResponseWriter. With lenient non-NULL (ResponseWriterWithLenientValidation), unnormalized
 * and duplicate file names are reported to it as warnings instead of failing. */
GoppResponseWriter *gopp_response_writer_new(void (*lenient)(void *ctx, Str warning), void *ctx);
/* AddFile */
void gopp_add_file(GoppResponseWriter *w, Str name, Str content);
/* AddError: empty messages are ignored, a second one is appended after "; ". */
void gopp_add_error(GoppResponseWriter *w, Str message);
/* SetFeatureProto3Optional */
void gopp_set_feature_proto3_optional(GoppResponseWriter *w);
/* SetFeatureSupportsEditions (descriptorpb.Edition values) */
void gopp_set_feature_supports_editions(GoppResponseWriter *w, int32_t minimum, int32_t maximum);
/* AddCodeGeneratorResponseFiles (the files are copied) */
void gopp_add_response_files(GoppResponseWriter *w, const GoppFile *files, int n);
/* SetSupportedFeatures, SetMinimumEdition, SetMaximumEdition (0 clears) */
void gopp_set_supported_features(GoppResponseWriter *w, uint64_t features);
void gopp_set_minimum_edition(GoppResponseWriter *w, int32_t edition);
void gopp_set_maximum_edition(GoppResponseWriter *w, int32_t edition);
/* ToCodeGeneratorResponse followed by proto.Marshal: validates and normalizes the response and
 * appends it serialized to out. Returns NULL or the error ("CodeGeneratorResponse: ..."). Can be
 * called once. */
const char *gopp_to_response(GoppResponseWriter *w, Buf *out);

/* Reads the files and the error of a serialized CodeGeneratorResponse, such as the one of
 * compiler/protogen's Plugin.Response (pg_plugin_response): what a handler passes on with
 * AddCodeGeneratorResponseFiles(response.GetFile()...) and AddError(response.GetError()).
 * Returns false if the bytes cannot be parsed. */
bool gopp_parse_response(Str raw, GoppFile **files, int *n, Str *error);

#endif
