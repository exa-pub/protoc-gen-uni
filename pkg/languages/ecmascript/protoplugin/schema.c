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

#include <string.h>

/* The message type of field num of def. */
static const PbMsgDef *field_msg(const PbMsgDef *def, uint32_t num)
{
	for (int i = 0; i < def->n; i++)
		if (def->f[i].num == num)
			return def->f[i].msg;
	return NULL;
}

/* The port reads these fields as NUL-terminated strings, so a NUL byte in them is not
 * supported. Upstream has no such limit (protoc and buf never produce one). */
COLD static void assert_no_nul(const PbMsg *m, const PbMsgDef *mdp)
{
	static const struct {
		const PbMsgDef *def;
		uint32_t num;
		const char *name;
	} fields[] = {
		{&pbd_CodeGeneratorRequest, CGR_FILE_TO_GENERATE, "CodeGeneratorRequest.file_to_generate"},
		{&pbd_FileDescriptorProto, FDP_NAME, "FileDescriptorProto.name"},
		{&pbd_FileDescriptorProto, FDP_PACKAGE, "FileDescriptorProto.package"},
		{&pbd_FileDescriptorProto, FDP_DEPENDENCY, "FileDescriptorProto.dependency"},
		{&pbd_FileDescriptorProto, FDP_SYNTAX, "FileDescriptorProto.syntax"},
		{&pbd_FieldDescriptorProto, FD_EXTENDEE, "FieldDescriptorProto.extendee"},
		{&pbd_FieldDescriptorProto, FD_TYPE_NAME, "FieldDescriptorProto.type_name"},
		{NULL, MDP_INPUT_TYPE, "MethodDescriptorProto.input_type"},
		{NULL, MDP_OUTPUT_TYPE, "MethodDescriptorProto.output_type"},
	};
	for (size_t k = 0; k < sizeof fields / sizeof fields[0]; k++) {
		if ((fields[k].def ? fields[k].def : mdp) != m->def)
			continue;
		for (int i = 0; i < pb_count(m, fields[k].num); i++) {
			Str v = pb_bytes_at(m, fields[k].num, i);
			if (memchr(v.p, 0, v.n))
				fail("unsupported by protoc-gen-uni: NUL byte in %s %s", fields[k].name, quote_json(v));
		}
	}
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].type == PT_MESSAGE)
			for (int j = 0; j < m->slots[i].n; j++)
				assert_no_nul(m->slots[i].v[j].m, mdp);
}

/* v2.13.0 validates no names, so a NUL byte can also reach the names (field 1) of messages,
 * fields, oneofs, enums, enum values, services and methods. */
COLD static void assert_no_nul_in_names(const PbMsg *m, const PbMsgDef *const *defs, int n)
{
	for (int k = 0; k < n; k++) {
		Str v = defs[k] == m->def ? pb_bytes(m, 1) : (Str){"", 0};
		if (memchr(v.p, 0, v.n))
			fail("unsupported by protoc-gen-uni: NUL byte in the name %s", quote_json(v));
	}
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].type == PT_MESSAGE)
			for (int j = 0; j < m->slots[i].n; j++)
				assert_no_nul_in_names(m->slots[i].v[j].m, defs, n);
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
	const PbMsgDef *sdp = field_msg(&pbd_FileDescriptorProto, FDP_SERVICE), *mdp = field_msg(sdp, SDP_METHOD);
	assert_no_nul(req, mdp);
	if (V2_13(s)) {
		const PbMsgDef *edp = field_msg(&pbd_FileDescriptorProto, FDP_ENUM_TYPE);
		const PbMsgDef *const names[] = {&pbd_DescriptorProto, &pbd_FieldDescriptorProto,
			field_msg(&pbd_DescriptorProto, DP_ONEOF_DECL), edp, field_msg(edp, EDP_VALUE), sdp, mdp};
		assert_no_nul_in_names(req, names, (int)(sizeof names / sizeof *names));
	}
	const int64_t min_ed = ED_MINIMUM, max_ed = MAX_EDITION(s);
	int n = pb_count(req, CGR_PROTO_FILE);
	Map to_generate = {0}, source_files = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		map_put(&to_generate, pb_str_at(req, CGR_FILE_TO_GENERATE, i), (void *)1);
	for (int i = 0; i < n; i++) {
		PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		if (!map_has(&s->proto_files, pb_str(file, FDP_NAME)))
			map_put(&s->proto_files, pb_str(file, FDP_NAME), file);
	}
	for (int i = pb_count(req, CGR_SOURCE_FILE_DESCRIPTORS) - 1; i >= 0; i--) {
		PbMsg *sf = pb_msg_at(req, CGR_SOURCE_FILE_DESCRIPTORS, i);
		map_put(&source_files, pb_str(sf, FDP_NAME), sf); /* the first one wins */
	}
	Vec missing = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		if (!map_has(&s->proto_files, pb_str_at(req, CGR_FILE_TO_GENERATE, i)))
			vec_push(&missing, (void *)pb_str_at(req, CGR_FILE_TO_GENERATE, i));
	if (missing.n)
		fail("files_to_generate missing in the request: %s", str_join(&missing, ", "));
	for (int i = 0; i < n; i++) {
		const PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		if (!map_has(&to_generate, pb_str(file, FDP_NAME)))
			continue;
		int64_t e = syntax_edition(file);
		/* EDITION_UNSTABLE is a sandbox for in-development features; accept it regardless of
		 * the plugin's min/max bounds. */
		if (e == ED_UNSTABLE)
			continue;
		const char *name = pb_str(file, FDP_NAME);
		if (e < min_ed)
			fail("%s: unsupported edition %s - the earliest supported edition is %s", name,
			     edition_to_string(s, e), edition_to_string(s, min_ed));
		if (e > max_ed)
			fail("%s: unsupported edition %s - the latest supported edition is %s", name,
			     edition_to_string(s, e), edition_to_string(s, max_ed));
	}
	/* Our goal is to provide options with source retention to plugin authors.
	 * CodeGeneratorRequest.proto_file elides options with source retention for files to
	 * generate. For these files, we take the file from source_file_descriptors, which does
	 * include options with source retention. */
	PbMsg **with_source = xalloc(sizeof(PbMsg *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		PbMsg *sf = map_get(&source_files, pb_str(file, FDP_NAME));
		with_source[i] = sf ? sf : file;
	}
	/* v2.13.0 validates no names */
	Registry *reg = registry_create_opts(with_source, n, !V2_13(s), (int)max_ed);
	/* The registry only validates the copies it is given, but plugins may print names from
	 * either copy. For example, protoc-gen-es embeds proto_file. */
	for (int i = 0; i < n && !V2_13(s); i++) {
		const char *err = assert_valid_names(pb_msg_at(req, CGR_PROTO_FILE, i));
		if (err)
			fail("%s", err);
	}
	for (int i = 0; i < reg->files.n; i++) {
		DescFile *f = reg->files.v[i];
		vec_push(&s->all_files, f);
		if (map_has(&to_generate, pb_str(f->h.proto, FDP_NAME))) {
			vec_push(&s->files, f);
			map_put(&s->generating, f->name, (void *)1);
		}
	}
	s->runtime = create_runtime_imports(s->opts.bootstrap_wkt);
}
