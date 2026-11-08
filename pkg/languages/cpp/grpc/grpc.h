/*
 *
 * Copyright 2015 gRPC authors.
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
 *
 */

/* grpc_plugin_support: the part of src/compiler of grpc (https://github.com/grpc/grpc, tag
 * v1.85.1) shared by its protoc plugins (grpc/{cpp,csharp,objc,php,python,ruby} on buf.build),
 * ported to C over libprotoc (pkg/languages/cpp/libprotoc):
 *
 *   - generator_helpers.c: src/compiler/generator_helpers.h (namespace grpc_generator);
 *   - proto_parser_helper.c: src/compiler/proto_parser_helper.{h,cc};
 *   - generator_helpers_1.25.0.c: what generator_helpers.h of grpc v1.25.0 does differently (its
 *     own library cpp_grpc_1.25.0, for grpc/node, whose grpc-tools vendors that version).
 *
 * The generators of each language (ruby_generator.cc, ...) live in their generator directory.
 * std::string values that come from the request (comments) are Str: they may contain NUL. Lines
 * are Vec of Str *. Memory comes from the pkg/core arena. */
#ifndef GRPC_H
#define GRPC_H

#include "libprotoc.h"

/* ---- proto_parser_helper.h ---- */

/* EscapeVariableDelimiters: replaces '$' with "$$" (the result is NUL-terminated). */
Str grpc_escape_variable_delimiters(Str original);

/* ---- generator_helpers.h ---- */

/* ToLower (std::tolower of the "C" locale) */
char *grpc_to_lower(const char *s);
/* StripSuffix: removes suffix (compared case-insensitively) from *filename. */
bool grpc_strip_suffix(char **filename, const char *suffix);
/* StripPrefix */
bool grpc_strip_prefix(char **name, const char *prefix);
/* StripProto: without ".protodevel" or else ".proto" (case-insensitive). */
char *grpc_strip_proto(const char *filename);
/* StringReplace (replace_all false: the first occurrence only; from must not be empty). */
char *grpc_string_replace(const char *str, const char *from, const char *to, bool replace_all);
/* tokenize: input split at any of the delimiters (char * elements; "" gives one ""). */
Vec grpc_tokenize(const char *input, const char *delimiters);
char *grpc_capitalize_first_letter(const char *s);
char *grpc_lowercase_first_letter(const char *s);
char *grpc_lower_underscore_to_upper_camel(const char *s);
/* FileNameInUpperCamel */
char *grpc_file_name_in_upper_camel(const LpFile *file, bool include_package_path);

typedef enum {
	GRPC_METHODTYPE_NO_STREAMING,
	GRPC_METHODTYPE_CLIENT_STREAMING,
	GRPC_METHODTYPE_SERVER_STREAMING,
	GRPC_METHODTYPE_BIDI_STREAMING,
} GrpcMethodType;
GrpcMethodType grpc_get_method_type(const LpMethod *method);

/* Split: the lines of s (std::getline on '\n': a final "\n" ends the last line) appended to
 * append_to. */
void grpc_split(Str s, Vec *append_to);

typedef enum {
	GRPC_COMMENTTYPE_LEADING,
	GRPC_COMMENTTYPE_TRAILING,
	GRPC_COMMENTTYPE_LEADING_DETACHED,
} GrpcCommentType;

/* GetComment: the raw comment lines of desc (a file, message, service, method, ...) appended to
 * out; locs is lp_source_locations of desc's file. A file has the comments of its syntax
 * statement (path [12]) and no trailing comments. */
void grpc_get_comment(const LpSourceLocations *locs, const ProtoDesc *desc, GrpcCommentType type, Vec *out);
/* GenerateCommentsWithPrefix: prefix and newline added to each line (a space after prefix
 * unless the line is empty or starts with one); '$' escaped (NUL-terminated result). */
Str grpc_generate_comments_with_prefix(const Vec *in, const char *prefix);
/* GetPrefixedComments: the leading (detached comments first) or trailing comments of desc. */
Str grpc_get_prefixed_comments(const LpSourceLocations *locs, const ProtoDesc *desc, bool leading, const char *prefix);

/* ---- generator_helpers.h of grpc v1.25.0 (generator_helpers_1.25.0.c, library cpp_grpc_1.25.0) ----
 *
 * The parts that differ from v1.85.1, for the plugins that vendor the older helpers (grpc-node's
 * grpc-tools). */

/* StripProto with a case-sensitive StripSuffix */
char *grpc_strip_proto_1_25_0(const char *filename);
/* GetPrefixedComments without the '$' escaping of GenerateCommentsWithPrefix */
Str grpc_get_prefixed_comments_1_25_0(const LpSourceLocations *locs, const ProtoDesc *desc, bool leading,
				      const char *prefix);

#endif
