/* Copyright 2024-2025 The Connect Authors
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

/* connectrpc/dart v2.0.1: a port of protoc-gen-connect-dart from connect-dart v2.0.1
 * (https://github.com/connectrpc/connect-dart/tree/v2.0.1/packages/connect/bin), on the Dart
 * protobuf runtime (pkg/languages/dart/protobuf), package:path (pkg/languages/dart/path) and
 * dart:core (pkg/languages/dart/std). Unity build: the other files of the directory port the
 * upstream files of the same name. */
#include "dart_path.h"
#include "dart_protobuf.h"
#include "dart_std.h"
#include "uni.h"

#include <string.h>

#include "generated_file.c"
#include "run.c"
#include "plugin.c"

static const char *generate_request(PbMsg *req, Str raw, Buf *out) { return run(req, raw, out); }

const UniGenerator gen_connectrpc_dart_v2_0_1 = {
	.name = "connectrpc/dart",
	.version = "v2.0.1",
	.generate = generate_request,
	.parses_raw = true,
};
