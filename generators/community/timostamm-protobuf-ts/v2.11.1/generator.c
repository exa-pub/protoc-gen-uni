/* Copyright (c) 2016 Timo Stamm
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

/* community/timostamm-protobuf-ts v2.11.1: a port of protoc-gen-ts of @protobuf-ts/plugin 2.11.1
 * (buf.build/community/timostamm-protobuf-ts, https://github.com/timostamm/protobuf-ts,
 * packages/plugin/src). The plugin builds TypeScript 3.9 syntax trees and prints them with the
 * TypeScript printer (pkg/languages/ecmascript/formatters/typescript); @bufbuild/protobuf and
 * @bufbuild/protoplugin 2.5.2 are pkg/languages/ecmascript/protobuf and protoplugin (2.17.0 with
 * the differences switched off), the parts of @protobuf-ts/runtime it runs are
 * pkg/languages/ecmascript/protobuf-ts-runtime.
 *
 * This file ports protobufts-plugin.ts and framework/plugin-base.ts; it is a unity build of the
 * other .c files of the directory, in which everything but the generator is static. */
#include "es_std.h"
#include "protoplugin.h"
#include "runtime.h"
#include "typescript.h"
#include "uni.h"

#include <string.h>

#define PLUGIN_VERSION "2.11.1"

#include "options.c"
#include "framework.c"
#include "interpreter.c"

/* The state of one request (reset for every request: the fuzzer runs many in one process). */
static struct {
	const Options *o;
	Registry *registry;
	SymbolTable symbols;
	FileTable files;
	Imports imports;
	Interpreter interp;
	Map index; /* lookup tables, see field_by_number */
} G;

#include "comment_generator.c"
#include "message_interface.c"
#include "message_type.c"
#include "binary_read.c"
#include "binary_write.c"
#include "services.c"
#include "well_known_types.c"

/* WellKnownTypes.protoFilenames */
static bool is_wkt_proto(const char *name)
{
	static const char *const names[] = {
		"google/protobuf/any.proto", "google/protobuf/api.proto", "google/protobuf/descriptor.proto",
		"google/protobuf/duration.proto", "google/protobuf/empty.proto", "google/protobuf/field_mask.proto",
		"google/protobuf/source_context.proto", "google/protobuf/struct.proto", "google/protobuf/timestamp.proto",
		"google/protobuf/type.proto", "google/protobuf/wrappers.proto", "google/protobuf/compiler/plugin.proto",
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (str_eq(names[i], name))
			return true;
	return false;
}

/* getClientStyles / getServerStyles: the styles of a service, from the options of the plugin and
 * the service options ts.client / ts.server. */
static Vec get_styles(const DescService *s, bool server)
{
	Vec opt = get_option_enums(desc_options(&s->h), server ? 777702 : 777701);
	bool has_none = false, has_other = false;
	for (int i = 0; i < opt.n; i++) {
		if ((intptr_t)opt.v[i] == 0)
			has_none = true;
		else
			has_other = true;
	}
	if (has_none && has_other)
		fail_str(str_c(xcat("You provided invalid options for ", s->type_name, ". If you set (ts.", server ? "server" : "client",
				    ") = ", server ? "NO_SERVER" : "NO_CLIENT", ", you cannot set additional ",
				    server ? "server" : "client", " styles.")));
	int forced = server ? G.o->forced_server_style : G.o->forced_client_style;
	Vec r = {0};
	if (forced >= 0) {
		vec_push(&r, (void *)(intptr_t)forced);
		return r;
	}
	if (opt.n) {
		Map seen = {0};
		for (int i = 0; i < opt.n; i++) {
			const char *k = xfmt("%d", (int)(intptr_t)opt.v[i]);
			if ((intptr_t)opt.v[i] == 0 || map_has(&seen, k))
				continue;
			map_put(&seen, k, (void *)1);
			vec_push(&r, opt.v[i]);
		}
		return r;
	}
	vec_push(&r, (void *)(intptr_t)(server ? G.o->normal_server_style : G.o->normal_client_style));
	return r;
}

static bool has_style(const Vec *styles, int style)
{
	for (int i = 0; i < styles->n; i++)
		if ((intptr_t)styles->v[i] == style)
			return true;
	return false;
}

/* The type names used by messages (fields) and services (inputs, outputs) of the files: isTypeUsed
 * for every type, with one pass over the files instead of one per type. */
static void collect_used(const Vec *files, Map *used)
{
	for (int i = 0; i < files->n; i++) {
		OutFile *of = files->v[i];
		Vec types = nested_types(&of->desc_file->h);
		for (int j = 0; j < types.n; j++) {
			Desc *d = types.v[j];
			if (d->kind == DK_SERVICE) {
				DescService *s = (DescService *)d;
				for (int k = 0; k < s->methods.n; k++) {
					DescMethod *m = s->methods.v[k];
					map_put(used, xcat("m", m->input->type_name), (void *)1);
					map_put(used, xcat("m", m->output->type_name), (void *)1);
				}
			} else if (d->kind == DK_MESSAGE) {
				DescMessage *m = (DescMessage *)d;
				for (int k = 0; k < m->fields.n; k++) {
					DescField *f = m->fields.v[k];
					if (f->message)
						map_put(used, xcat("m", f->message->type_name), (void *)1);
					if (f->enm)
						map_put(used, xcat("e", f->enm->type_name), (void *)1);
				}
			}
		}
	}
}

/* isFileUsed */
static bool is_file_used(const DescFile *file, const Map *used)
{
	Vec types = nested_types(&file->h);
	for (int i = 0; i < types.n; i++) {
		Desc *d = types.v[i];
		/* a message is used by method inputs/outputs and message fields, an enum by message fields;
		 * services and extensions are never used */
		if (d->kind == DK_MESSAGE && map_has(used, xcat("m", ((DescMessage *)d)->type_name)))
			return true;
		if (d->kind == DK_ENUM && map_has(used, xcat("e", ((DescEnum *)d)->type_name)))
			return true;
	}
	return false;
}

/* The names of messages, enums, services and their members must be identifiers: other names
 * (accepted by @bufbuild/protobuf 2.5.2, never written by protoc) are not supported. */
COLD static void check_name(const char *kind, const char *name)
{
	bool ok = *name && !(*name >= '0' && *name <= '9');
	for (const char *p = name; ok && *p; p++)
		ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_';
	if (!ok)
		fail("unsupported by protoc-gen-uni: %s name \"%s\" is not an identifier", kind, name);
}

COLD static void check_names(const Desc *d)
{
	Vec types = nested_types(d);
	for (int i = 0; i < types.n; i++) {
		Desc *t = types.v[i];
		switch (t->kind) {
		case DK_MESSAGE: {
			DescMessage *m = (DescMessage *)t;
			check_name("message", m->name);
			for (int k = 0; k < m->fields.n; k++)
				check_name("field", ((DescField *)m->fields.v[k])->name);
			for (int k = 0; k < m->oneofs.n; k++)
				check_name("oneof", ((DescOneof *)m->oneofs.v[k])->name);
			break;
		}
		case DK_ENUM: {
			DescEnum *e = (DescEnum *)t;
			check_name("enum", e->name);
			for (int k = 0; k < e->values.n; k++)
				check_name("enum value", ((DescEnumValue *)e->values.v[k])->name);
			break;
		}
		case DK_SERVICE: {
			DescService *s = (DescService *)t;
			check_name("service", s->name);
			for (int k = 0; k < s->methods.n; k++)
				check_name("method", ((DescMethod *)s->methods.v[k])->name);
			break;
		}
		case DK_FIELD: check_name("extension", ((DescField *)t)->name); break;
		}
	}
	if (d->kind == DK_FILE) {
		Vec parts = str_split(pb_str(d->proto, FDP_PACKAGE), '.');
		for (int i = 0; i < parts.n && *pb_str(d->proto, FDP_PACKAGE); i++)
			check_name("package", parts.v[i]);
	}
}

typedef struct {
	PbMsg *req;
	Vec out; /* OutFile* to write */
} Run;

/* ProtobuftsPlugin.generate */
static void generate(void *arg)
{
	Run *run = arg;
	PbMsg *req = run->req;
	memset(&G, 0, sizeof G);
	comments_normalize_line_breaks = false; /* @bufbuild/protoplugin 2.5.2 */
	Str parameter = pb_bytes(req, CGR_PARAMETER);
	const char *credit = xcat("by protobuf-ts " PLUGIN_VERSION, parameter.n ? " with parameter " : "",
				  parameter.n ? xstrndup(parameter.p, parameter.n) : "");
	G.o = parse_options(parameter, credit);
	int n = pb_count(req, CGR_PROTO_FILE);
	PbMsg **files = xalloc(sizeof *files * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		files[i] = pb_msg_at(req, CGR_PROTO_FILE, i);
		/* @bufbuild/protobuf 2.5.2 knows the editions up to 2023 */
		int64_t e = pb_int(files[i], FDP_EDITION, 0);
		if (str_eq(pb_str(files[i], FDP_SYNTAX), "editions") && e != ED_PROTO2 && e != ED_PROTO3 && e != ED_2023)
			fail("Error: %s: unsupported edition", pb_str(files[i], FDP_NAME));
	}
	G.registry = registry_create_unchecked(files, n);
	Registry *reg = G.registry;
	for (int i = 0; i < reg->files.n; i++)
		check_names(reg->files.v[i]);
	G.files.options = G.o;
	G.imports.symbols = &G.symbols;
	G.imports.registry = reg;
	interpreter_init(&G.interp, reg, G.o);

	const char *suffix = G.o->add_pb_suffix ? "_pb" : "";
	for (int i = 0; i < reg->files.n; i++) {
		DescFile *f = reg->files.v[i];
		file_table_register(&G.files, xcat(f->name, suffix, ".ts"), f, "default");
	}
	for (int i = 0; i < reg->files.n; i++) {
		DescFile *f = reg->files.v[i];
		const char *base = xcat(f->name, suffix);
		file_table_register(&G.files, xcat(base, ".server.ts"), f, "generic-server");
		file_table_register(&G.files, xcat(base, ".grpc-server.ts"), f, "grpc1-server");
		file_table_register(&G.files, xcat(base, ".client.ts"), f, "client");
		file_table_register(&G.files, xcat(base, ".promise-client.ts"), f, "promise-client");
		file_table_register(&G.files, xcat(base, ".rx-client.ts"), f, "rx-client");
		file_table_register(&G.files, xcat(base, ".grpc-client.ts"), f, "grpc1-client");
	}
	for (int i = 0; i < reg->files.n; i++) {
		DescFile *file = reg->files.v[i];
		OutFile *out_main = file_table_create(&G.files, file, "default");
		OutFile *out_server_generic = file_table_create(&G.files, file, "generic-server");
		OutFile *out_server_grpc = file_table_create(&G.files, file, "grpc1-server");
		OutFile *out_client_call = file_table_create(&G.files, file, "client");
		OutFile *out_client_grpc = file_table_create(&G.files, file, "grpc1-client");
		Vec types = nested_types(&file->h);
		for (int j = 0; j < types.n; j++) {
			Desc *d = types.v[j];
			switch (d->kind) {
			case DK_ENUM: enum_register_symbols(out_main, (DescEnum *)d); break;
			case DK_MESSAGE: message_interface_register_symbols(out_main, (DescMessage *)d); break;
			case DK_SERVICE:
				service_type_register_symbols(out_main, (DescService *)d);
				client_register_symbols(&client_generic, out_client_call, (DescService *)d);
				client_register_symbols(&client_grpc, out_client_grpc, (DescService *)d);
				server_generic_register_symbols(out_server_generic, (DescService *)d);
				server_grpc_register_symbols(out_server_grpc, (DescService *)d);
				break;
			}
		}
		for (int j = 0; j < types.n; j++) {
			Desc *d = types.v[j];
			if (d->kind == DK_MESSAGE)
				generate_message_interface(out_main, (DescMessage *)d);
			else if (d->kind == DK_ENUM)
				generate_enum(out_main, (DescEnum *)d);
		}
		for (int j = 0; j < types.n; j++) {
			Desc *d = types.v[j];
			if (d->kind == DK_MESSAGE) {
				generate_message_type(out_main, (DescMessage *)d, get_optimize_mode(G.o, file));
			} else if (d->kind == DK_SERVICE) {
				DescService *s = (DescService *)d;
				if (G.o->force_disable_services)
					continue;
				generate_service_type(out_main, s);
				Vec cs = get_styles(s, false);
				if (has_style(&cs, GENERIC_CLIENT)) {
					client_generate_interface(&client_generic, out_client_call, s);
					client_generic.generate_implementation_class(&client_generic, out_client_call, s);
				}
				if (has_style(&cs, GRPC1_CLIENT)) {
					client_generate_interface(&client_grpc, out_client_grpc, s);
					client_grpc.generate_implementation_class(&client_grpc, out_client_grpc, s);
				}
				Vec ss = get_styles(s, true);
				if (has_style(&ss, GENERIC_SERVER))
					server_generic_generate_interface(out_server_generic, s);
				if (has_style(&ss, GRPC1_SERVER)) {
					server_grpc_generate_interface(out_server_grpc, s);
					server_grpc_generate_definition(out_server_grpc, s);
				}
			}
		}
	}

	/* the files to generate, the well-known types, and files used by them */
	Map to_generate = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		map_put(&to_generate, pb_str_at(req, CGR_FILE_TO_GENERATE, i), (void *)1);
	Vec ts_files = {0};
	for (int i = 0; i < G.files.out_files.n; i++) {
		OutFile *of = G.files.out_files.v[i];
		const char *name = pb_str(of->desc_file->h.proto, FDP_NAME);
		if (G.o->generate_dependencies || map_has(&to_generate, name) || is_wkt_proto(name))
			vec_push(&ts_files, of);
	}
	Map used = {0};
	collect_used(&ts_files, &used);
	for (int i = 0; i < ts_files.n; i++) {
		OutFile *of = ts_files.v[i];
		if (map_has(&to_generate, pb_str(of->desc_file->h.proto, FDP_NAME)) || is_file_used(of->desc_file, &used))
			vec_push(&run->out, of);
	}
	/* transpile */
	if (G.o->transpile_target)
		fail("unsupported by protoc-gen-uni: option %s (JavaScript output needs the TypeScript compiler)",
		     G.o->transpile_target);
}

typedef struct {
	PbMsg *req;
} Parse;

static void parse(void *arg) { from_binary(((Parse *)arg)->req); }

static const char *generate_protobuf_ts(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	Parse p = {req};
	const char *err = try_run(parse, &p);
	if (err)
		return xcat("Plugin failed to read CodeGeneratorRequest from stdin or write CodeGeneratorResponse to stdout.\n", err);
	Run run = {req, {0}};
	if (try_run(generate, &run)) {
		comments_normalize_line_breaks = true;
		pb_put_bytes(out, CGRESP_ERROR, fail_message());
		return NULL;
	}
	/* createResponse */
	Buf files = {0};
	for (int i = 0; i < run.out.n; i++) {
		OutFile *of = run.out.v[i];
		Str content = out_file_content(of);
		if (!content.n)
			continue;
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(of->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, content);
		pb_put_bytes(&files, CGRESP_FILE, buf_view(&file));
	}
	comments_normalize_line_breaks = true;
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 1); /* FEATURE_PROTO3_OPTIONAL */
	buf_add_str(out, buf_view(&files));
	return NULL;
}

const UniGenerator gen_community_timostamm_protobuf_ts_v2_11_1 = {
	.name = "community/timostamm-protobuf-ts",
	.version = "v2.11.1",
	.generate = generate_protobuf_ts,
};
