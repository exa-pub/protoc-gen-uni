/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protoplugin/src/schema.ts (createSchema, getFilesToGenerate). */
#include "internal.h"

static bool contains_str(const PbMsg *m, uint32_t num, Str s)
{
	for (int i = 0; i < pb_count(m, num); i++)
		if (str_equal(pb_bytes_at(m, num, i), s))
			return true;
	return false;
}

static bool request_has_file(const PbMsg *req, Str name)
{
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++)
		if (str_equal(pb_bytes(pb_msg_at(req, CGR_PROTO_FILE, i), FDP_NAME), name))
			return true;
	return false;
}

static int64_t syntax_edition(const PbMsg *file)
{
	const char *syntax = pb_str(file, FDP_SYNTAX);
	if (!*syntax || str_eq(syntax, "proto2"))
		return ED_PROTO2;
	if (str_eq(syntax, "proto3"))
		return ED_PROTO3;
	if (str_eq(syntax, "editions"))
		return pb_int(file, FDP_EDITION, 0);
	return 0;
}

void create_schema(Schema *s, PbMsg *req)
{
	const int64_t min_ed = ED_MINIMUM, max_ed = ED_MAXIMUM;
	Vec missing = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		if (!request_has_file(req, pb_bytes_at(req, CGR_FILE_TO_GENERATE, i)))
			vec_push(&missing, (void *)pb_str_at(req, CGR_FILE_TO_GENERATE, i));
	if (missing.n)
		fail("files_to_generate missing in the request: %s", str_join(&missing, ", "));
	int n = pb_count(req, CGR_PROTO_FILE);
	for (int i = 0; i < n; i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		if (!contains_str(req, CGR_FILE_TO_GENERATE, pb_bytes(file, FDP_NAME)))
			continue;
		int64_t e = syntax_edition(file);
		/* EDITION_UNSTABLE is a sandbox for in-development features; accept it regardless of
		 * the plugin's min/max bounds. */
		if (e == ED_UNSTABLE)
			continue;
		const char *name = pb_str(file, FDP_NAME);
		if (e < min_ed)
			fail("%s: unsupported edition %s - the earliest supported edition is %s", name,
			     edition_to_string(e), edition_to_string(min_ed));
		if (e > max_ed)
			fail("%s: unsupported edition %s - the latest supported edition is %s", name,
			     edition_to_string(e), edition_to_string(max_ed));
	}
	/* Our goal is to provide options with source retention to plugin authors.
	 * CodeGeneratorRequest.proto_file elides options with source retention for files to
	 * generate. For these files, we take the file from source_file_descriptors, which does
	 * include options with source retention. */
	PbMsg **with_source = xalloc(sizeof(PbMsg *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		with_source[i] = file;
		for (int j = 0; j < pb_count(req, CGR_SOURCE_FILE_DESCRIPTORS); j++) {
			PbMsg *sf = pb_msg_at(req, CGR_SOURCE_FILE_DESCRIPTORS, j);
			if (str_equal(pb_bytes(sf, FDP_NAME), pb_bytes(file, FDP_NAME))) {
				with_source[i] = sf;
				break;
			}
		}
	}
	Registry *reg = registry_create(with_source, n);
	/* The registry only validates the copies it is given, but plugins may print names from
	 * either copy. For example, protoc-gen-es embeds proto_file. */
	for (int i = 0; i < n; i++) {
		const char *err = assert_valid_names(pb_msg_at(req, CGR_PROTO_FILE, i));
		if (err)
			fail("%s", err);
	}
	for (int i = 0; i < reg->files.n; i++) {
		DescFile *f = reg->files.v[i];
		vec_push(&s->all_files, f);
		if (contains_str(req, CGR_FILE_TO_GENERATE, pb_bytes(f->h.proto, FDP_NAME)))
			vec_push(&s->files, f);
	}
	s->runtime = create_runtime_imports(s->opts.bootstrap_wkt);
}
