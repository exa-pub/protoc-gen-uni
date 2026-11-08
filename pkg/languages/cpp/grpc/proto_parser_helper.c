// Copyright 2023 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* Port of src/compiler/proto_parser_helper.cc (grpc v1.85.1). */

Str grpc_escape_variable_delimiters(Str original)
{
	Buf b = {0};
	for (size_t i = 0; i < original.n; i++) {
		if (original.p[i] == '$')
			buf_addc(&b, '$');
		buf_addc(&b, original.p[i]);
	}
	buf_str(&b);
	return (Str){b.p ? b.p : "", b.n};
}
