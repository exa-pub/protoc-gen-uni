/* Copyright (c) Stephen Haberman
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

/* community/stephenh-ts-proto v2.18.1: a port of protoc-gen-ts_proto of ts-proto v2.18.1
 * (buf.build/community/stephenh-ts-proto, https://github.com/stephenh/ts-proto, src/plugin.ts here),
 * which builds TypeScript with ts-poet 6.12.0 (pkg/languages/ecmascript/ts-poet) and formats it with
 * dprint-plugin-typescript 0.87.1 (pkg/languages/ecmascript/formatters/dprint-typescript). The port
 * builds the formatter's nodes from ts-proto's templates. See README.md for the options that are not
 * supported.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "case_anything.h"
#include "dprint_typescript.h"
#include "es_std.h"
#include "pb.h"
#include "tpd.h"
#include "ts_poet.h"
#include "uni.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "options.c"
#include "dsl.h"
#include "ctx.c"

static TpImport *utils_long(void);
static void generate_services(void *out, TpdFile *f, void *si);
static void grpc_js_imports(void);
typedef struct FileOut FileOut;
static void generate_nestjs_service(FileOut *out, TpdFile *f, SourceInfo *ssi, TpdService *s);

#include "types.c"
#include "utils.c"
#include "enums.c"
#include "main.c"
#include "main_binary.c"
#include "main_json.c"
#include "extensions.c"
#include "struct_wrappers.c"
#include "services.c"
#include "nestjs.c"
#include "grpc_web.c"
#include "raw.c"
#include "file.c"

/* src/plugin.ts main() */
static void run(void *arg)
{
	void **a = arg;
	PbMsg *req = a[0];
	Buf *out = a[1];
	/* the arena is reset between requests (fuzzer): no pointer may survive from the last one */
	memset(&C, 0, sizeof C);
	memset(&O, 0, sizeof O);
	memset(&G, 0, sizeof G);
	memset(&GW, 0, sizeof GW);
	memset(&oneof_cache, 0, sizeof oneof_cache);
	memset(&enum_prefix_cache, 0, sizeof enum_prefix_cache);
	map_type_depth = 0;
	tp_file_begin(); /* the uses recorded while the utils are made */
	C.req = tpd_request(req);
	C.ts_proto_version = "v2.18.1";
	C.proto_version = C.req->has_compiler_version
				  ? xfmt("v%d.%d.%d", (int)C.req->major, (int)C.req->minor, (int)C.req->patch)
				  : "unknown";
	opt_resolve(options_from_parameter(C.req->parameter));
	create_type_map();
	make_utils();
	/* emitImportedFiles (default true): the files to generate and their imports, unless aliased */
	Map file_set = {0};
	Vec todo = {0};
	for (int i = 0; i < C.req->nfile_to_generate; i++)
		vec_push(&todo, (void *)C.req->file_to_generate[i]);
	bool emit_imported = opt_true(&O.opts, "emitImportedFiles");
	if (emit_imported) {
		/* addFilesUnlessAliased, depth first in the order of the lists */
		Vec stack = {0};
		for (int i = todo.n - 1; i >= 0; i--)
			vec_push(&stack, todo.v[i]);
		while (stack.n) {
			const char *name = stack.v[--stack.n];
			JsVal *m = js_obj_get(O.M, str_c(name));
			if ((m && js_truthy(m)) || map_has(&file_set, name))
				continue;
			map_put(&file_set, name, (void *)1);
			for (int k = 0; k < C.req->nproto_file; k++) {
				TpdFile *f = C.req->proto_file[k];
				if (str_eq(f->name, name)) {
					for (int d = f->ndependency - 1; d >= 0; d--)
						vec_push(&stack, (void *)f->dependency[d]);
					break;
				}
			}
		}
	} else {
		for (int i = 0; i < todo.n; i++) {
			JsVal *m = js_obj_get(O.M, str_c(todo.v[i]));
			if (!(m && js_truthy(m)))
				map_put(&file_set, todo.v[i], (void *)1);
		}
	}
	grpc_js_imports();
	grpc_web_imports();
	for (int k = 0; k < C.req->nproto_file; k++) {
		if (!map_has(&file_set, C.req->proto_file[k]->name))
			continue;
		/* the files are independent: free the memory of each once its text is done */
		ArenaMark mark = arena_mark();
		Buf file = {0};
		generate_file(C.req->proto_file[k], &file);
		char *kept = xalloc_keep(file.n + 1);
		if (file.n)
			memcpy(kept, file.p, file.n);
		size_t n = file.n;
		arena_release(mark);
		memset(&oneof_cache, 0, sizeof oneof_cache);
		memset(&enum_prefix_cache, 0, sizeof enum_prefix_cache);
		buf_add(out, kept, n);
	}
	if (opt_true(&O.opts, "outputTypeRegistry"))
		generate_type_registry(out);
	if (opt_true(&O.opts, "outputIndex")) {
		Vec files = {0};
		for (int k = 0; k < C.req->nproto_file; k++)
			if (map_has(&file_set, C.req->proto_file[k]->name))
				vec_push(&files, C.req->proto_file[k]);
		generate_index_files((TpdFile **)files.v, files.n, out);
	}
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	Buf files = {0};
	void *arg[2] = {req, &files};
	if (try_run(run, arg)) {
		/* Upstream prints "FAILED!", the message and the stack on stderr and exits with status 1. */
		pb_put_bytes(out, CGRESP_ERROR, fail_message());
		return NULL;
	}
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3); /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 998); /* EDITION_PROTO2 */
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 1001); /* EDITION_2024 */
	buf_add(out, files.p, files.n);
	return NULL;
}

const UniGenerator gen_community_stephenh_ts_proto_v2_18_1 = {
	.name = "community/stephenh-ts-proto",
	.version = "v2.18.1",
	.generate = generate,
};
