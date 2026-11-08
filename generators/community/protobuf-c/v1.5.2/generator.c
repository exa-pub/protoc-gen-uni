// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Modified to implement C code by Dave Benson.

/* community/protobuf-c v1.5.2: a port of protoc-gen-c of protobuf-c v1.5.2
 * (buf.build/community/protobuf-c, https://github.com/protobuf-c/protobuf-c, tag v1.5.2), built
 * by bufbuild/plugins against the libprotoc 3.21.12 of Debian bookworm: protoc-gen-c/main.cc
 * (this file) and the other files of protoc-gen-c/ (c_*.c, internal.h), and the options of
 * protobuf-c/protobuf-c.proto (protobuf-c.pb.c). The libprotoc parts (plugin.cc, descriptor.cc,
 * io::Printer of 3.21.12, ...) live in pkg/languages/cpp/libprotoc (LpVersion 3.21.12).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "internal.h"
#include "uni.h"

#include <math.h>

#include "protobuf-c.pb.c"
#include "c_helpers.c"
#include "c_field.c"
#include "c_primitive_field.c"
#include "c_string_field.c"
#include "c_bytes_field.c"
#include "c_enum_field.c"
#include "c_message_field.c"
#include "c_enum.c"
#include "c_message.c"
#include "c_service.c"
#include "c_file.c"
#include "c_generator.c"

/* c_generator.h: CGenerator. GetSupportedFeatures and the editions range are only overridden
 * with protobuf >= 26 (GOOGLE_PROTOBUF_VERSION >= 5026000): no features, no editions. */
static const LpGenerator c_generator = {
	.plugin_name = "protoc-gen-c",
	.generate = c_generate,
	.supported_features = 0,
	.version = &lp_version_3_21_12,
};

/* main.cc: PluginMain(argc, argv, &c_generator) (the protoc-c mode is not reachable as a plugin) */
static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Str parameter = pb_bytes(req, CGR_PARAMETER);
	/* The extensions of protobuf-c.proto are parsed with the request. */
	if (!pbc_request_parses(raw))
		return "protoc-gen-c: protoc sent unparseable request to plugin.";
	if (memchr(parameter.p, 0, parameter.n)) {
		pb_put_bytes(out, CGRESP_ERROR, STR_LIT("unsupported by protoc-gen-uni: a NUL byte in the parameter"));
		return NULL;
	}
	return lp_plugin_main(raw, parameter, &c_generator, out);
}

const UniGenerator gen_community_protobuf_c_v1_5_2 = {
	.name = "community/protobuf-c",
	.version = "v1.5.2",
	.generate = generate,
};
