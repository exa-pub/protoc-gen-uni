/* Copyright 2022 Marcus Griep and the protoc-gen-prost contributors (protoc-gen-prost-crate).
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

/* community/neoeinstein-prost-crate v0.5.0: a port of protoc-gen-prost-crate 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, buf.build/community/neoeinstein-prost-crate),
 * which writes the include file (and the Cargo features) of a crate of protoc-gen-prost output.
 * The protoc-gen-prost core it uses as a library lives in pkg/languages/rust/protoc-gen-prost.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "prost.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

#include "index.c"
#include "plugin.c"
#include "include_file.c"
#include "cargo_crate.c"
#include "features.c"

/* The port reads these strings NUL-terminated (pkg/pb), so a NUL byte in them is not supported.
 * Upstream has no such limit; protoc and buf never produce one. */
COLD static void assert_no_nul(Str s, const char *what)
{
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: NUL byte in %s %s", what, quote_json(s));
}

/* What the port does not implement fails generation (docs/RULES.md): NUL bytes in the strings it
 * reads, and non-ASCII package names, whose module names upstream derives with heck's Unicode
 * case conversion (pkg/languages/rust/heck is ASCII only). */
COLD static void assert_supported(const PbMsg *req, bool features)
{
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		assert_no_nul(pb_bytes_at(req, CGR_FILE_TO_GENERATE, i), "CodeGeneratorRequest.file_to_generate");
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		assert_no_nul(pb_bytes(file, FDP_NAME), "FileDescriptorProto.name");
		Str package = pb_bytes(file, FDP_PACKAGE);
		assert_no_nul(package, "FileDescriptorProto.package");
		for (size_t k = 0; k < package.n; k++)
			if ((uint8_t)package.p[k] >= 0x80)
				fail("unsupported by protoc-gen-uni: non-ASCII package %s (upstream derives "
				     "module names with Unicode case conversion)",
				     quote_json(package));
		if (!features) /* only the features read type names */
			continue;
		for (int k = 0; k < pb_count(file, FDP_MESSAGE_TYPE); k++) {
			const PbMsg *message = pb_msg_at(file, FDP_MESSAGE_TYPE, k);
			for (int l = 0; l < pb_count(message, DP_FIELD); l++)
				assert_no_nul(pb_bytes(pb_msg_at(message, DP_FIELD, l), FD_TYPE_NAME),
					      "FieldDescriptorProto.type_name");
		}
		for (int k = 0; k < pb_count(file, FDP_SERVICE); k++) {
			const PbMsg *service = pb_msg_at(file, FDP_SERVICE, k);
			for (int l = 0; l < pb_count(service, SDP_METHOD); l++) {
				const PbMsg *method = pb_msg_at(service, SDP_METHOD, l);
				assert_no_nul(pb_bytes(method, MDP_INPUT_TYPE), "MethodDescriptorProto.input_type");
				assert_no_nul(pb_bytes(method, MDP_OUTPUT_TYPE), "MethodDescriptorProto.output_type");
			}
		}
	}
}

/* src/lib.rs: execute */
static void execute(void *arg)
{
	Run *run = arg;
	Parameters *o = &run->params;
	/* CodeGeneratorRequest::decode: the errors of prost, which pkg/pb does not have (invalid
	 * UTF-8, known fields with another wire type, its recursion limit). */
	run->error = prost_decode_request(run->raw);
	if (run->error)
		return;
	assert_no_nul(pb_bytes(run->req, CGR_PARAMETER), "the parameter");
	run->error = parse_parameters(o, pb_bytes(run->req, CGR_PARAMETER));
	if (run->error)
		return;
	assert_supported(run->req, !o->no_features);
	ProstModuleRequestSet *set =
		prost_module_request_set_new(run->req, run->raw, o->default_package_filename, o->flat_output_dir);
	const char *include_filename = o->include_file ? o->include_file : o->gen_crate ? "src/lib.rs" : "mod.rs";
	const char *package_separator = o->package_separator ? o->package_separator : "-";
	include_file_generate(set, include_filename, &o->only_include, &run->files);
	if (o->gen_crate) {
		run->error = cargo_crate_generate(o->gen_crate_template);
		if (run->error)
			return;
	}
	if (!o->no_features)
		features_generate(set, include_filename, package_separator, &o->only_include, &run->files);
}

/* src/main.rs: the response, or the error (upstream panics abort the process instead). */
static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Run run = {0};
	run.req = req;
	run.raw = raw;
	if (try_run(execute, &run)) {
		prost_write_error(out, fail_message());
		return NULL;
	}
	if (run.error) {
		prost_write_error(out, str_c(run.error));
		return NULL;
	}
	prost_write_response(out, &run.files);
	return NULL;
}

const UniGenerator gen_community_neoeinstein_prost_crate_v0_5_0 = {
	.name = "community/neoeinstein-prost-crate",
	.version = "v0.5.0",
	.generate = generate,
};
