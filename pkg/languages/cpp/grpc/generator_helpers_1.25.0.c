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

/* src/compiler/generator_helpers.h of grpc v1.25.0 (the same in v1.20.0 to v1.25.0): what it does
 * differently from v1.85.1 (generator_helpers.c). grpc-node vendors this version as
 * packages/grpc-tools/src/generator_helpers.h (grpc-tools 1.13.1). Library cpp_grpc_1.25.0.
 *
 *   - StripSuffix compares the suffix case-sensitively (no ToLower), so StripProto keeps
 *     ".PROTO";
 *   - GenerateCommentsWithPrefix does not escape '$' (no EscapeVariableDelimiters).
 *
 * The other helpers (tokenize, StringReplace, StripPrefix, LowercaseFirstLetter, Split,
 * GetComment) are the same. */
#include "grpc.h"

#include <string.h>

char *grpc_strip_proto_1_25_0(const char *filename)
{
	if (has_suffix(filename, ".protodevel"))
		return str_trim_suffix(filename, ".protodevel");
	return str_trim_suffix(filename, ".proto");
}

Str grpc_get_prefixed_comments_1_25_0(const LpSourceLocations *locs, const ProtoDesc *desc, bool leading,
				      const char *prefix)
{
	Vec out = {0};
	if (leading) {
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING_DETACHED, &out);
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING, &out);
	} else {
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_TRAILING, &out);
	}
	/* GenerateCommentsWithPrefix */
	Buf b = {0};
	for (int i = 0; i < out.n; i++) {
		const Str *elem = out.v[i];
		buf_adds(&b, prefix);
		if (elem->n && elem->p[0] != ' ')
			buf_addc(&b, ' ');
		buf_add_str(&b, *elem);
		buf_addc(&b, '\n');
	}
	buf_str(&b);
	return (Str){b.p ? b.p : "", b.n};
}
