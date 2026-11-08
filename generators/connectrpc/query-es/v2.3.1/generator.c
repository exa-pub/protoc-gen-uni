/* Copyright 2021-2023 The Connect Authors
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

/* connectrpc/query-es v2.3.1: a port of protoc-gen-connect-query 2.3.1
 * (buf.build/connectrpc/query-es), the code generator of Connect-Query
 * (https://github.com/connectrpc/connect-query-es), from packages/protoc-gen-connect-query/src
 * (protoc-gen-connect-query-plugin.ts, generateTs.ts, generateDts.ts) at tag v2.3.1. It runs on
 * @bufbuild/protoplugin v2.13.0, the version of the buf.build image, ported in
 * pkg/languages/ecmascript/protoplugin (PROTOPLUGIN_V2_13_0). */
#include "protoplugin.h"
#include "uni.h"

/* method.localName. Upstream lowercases the first UTF-16 code unit of the name with
 * String.prototype.toLowerCase; @bufbuild/protobuf v2.13.0 does not validate names, so it can be
 * any character. The port knows the ASCII ones only. */
static const char *local_name(const DescMethod *m)
{
	if ((unsigned char)m->name[0] >= 0x80)
		fail("unsupported by protoc-gen-uni: method name %s starts with a non-ASCII character",
		     quote_json(str_c(m->name)));
	return m->local_name;
}

/* generateTs.ts: one file per service with the unary methods, for the targets ts and js (the
 * TypeScript output is valid JavaScript). */
static void generate_ts(Schema *schema)
{
	const char *extension = schema->target == TARGET_TS ? "ts" : "js";
	for (int i = 0; i < schema->files.n; i++) {
		DescFile *proto_file = schema->files.v[i];
		for (int j = 0; j < proto_file->services.n; j++) {
			DescService *service = proto_file->services.v[j];
			GeneratedFile *f = gf_new(schema, xfmt("%s-%s_connectquery.%s", proto_file->name,
							      service->name, extension));
			gf_preamble(f, proto_file);
			Vec unary = {0};
			for (int k = 0; k < service->methods.n; k++) {
				DescMethod *m = service->methods.v[k];
				if (str_eq(m->method_kind, "unary"))
					vec_push(&unary, m);
			}
			for (int k = 0; k < unary.n; k++) {
				DescMethod *method = unary.v[k];
				gf_jsdoc(f, &method->h, "");
				gf_nl(f);
				const char *name = local_name(method);
				gf_export(f, "const", safe_identifier(name));
				gf_s(f, " = ");
				gf_sym(f, gf_import_schema(f, &service->h, false));
				gf_s(f, xcat(".method.", name, ";\n"));
				if (k != unary.n - 1)
					gf_nl(f);
			}
		}
	}
}

/* generateDts.ts: the declarations of the unary methods, without blank lines between them. */
static void generate_dts(Schema *schema)
{
	for (int i = 0; i < schema->files.n; i++) {
		DescFile *proto_file = schema->files.v[i];
		for (int j = 0; j < proto_file->services.n; j++) {
			DescService *service = proto_file->services.v[j];
			GeneratedFile *f = gf_new(schema, xfmt("%s-%s_connectquery.d.ts", proto_file->name,
							      service->name));
			gf_preamble(f, proto_file);
			for (int k = 0; k < service->methods.n; k++) {
				DescMethod *method = service->methods.v[k];
				if (!str_eq(method->method_kind, "unary"))
					continue;
				gf_jsdoc(f, &method->h, "");
				gf_nl(f);
				const char *name = local_name(method);
				gf_export(f, "const", safe_identifier(name));
				gf_s(f, ": typeof ");
				gf_sym(f, gf_import_schema(f, &service->h, false));
				gf_s(f, xcat("[\"method\"][\"", name, "\"];\n"));
			}
		}
	}
}

/* protoc-gen-connect-query-plugin.ts */
static const EsPlugin protoc_gen_connect_query = {
	.name = "protoc-gen-connect-query",
	.version = "v2.3.1",
	.protoplugin = PROTOPLUGIN_V2_13_0,
	.generate_ts = generate_ts,
	.generate_js = generate_ts,
	.generate_dts = generate_dts,
};

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	es_plugin_run(&protoc_gen_connect_query, req, out);
	return NULL;
}

const UniGenerator gen_connectrpc_query_es_v2_3_1 = {
	.name = "connectrpc/query-es",
	.version = "v2.3.1",
	.generate = generate,
};
