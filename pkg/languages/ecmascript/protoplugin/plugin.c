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

/* Ported from packages/protoplugin/src/create-es-plugin.ts and plugin.ts. */
#include "internal.h"

typedef struct {
	const EsPlugin *p;
	PbMsg *req;
	Schema *s;
} RunCtx;

static void run(void *arg)
{
	RunCtx *ctx = arg;
	Schema *s = xalloc(sizeof *s);
	ctx->s = s;
	/* fromBinary(CodeGeneratorRequestSchema, data) */
	from_binary(ctx->req);
	s->request = ctx->req;
	s->plugin_name = ctx->p->name;
	s->protoplugin = ctx->p->protoplugin;
	comments_normalize_line_breaks = !V2_13(s);
	parse_parameter(s, ctx->p, pb_bytes(ctx->req, CGR_PARAMETER));
	s->plugin_version = s->opts.elide_plugin_version ? NULL : ctx->p->version;
	create_schema(s, ctx->req);
	static const int order[] = {TARGET_TS, TARGET_JS, TARGET_DTS};
	for (int i = 0; i < 3; i++) {
		if (!has_target(&s->opts, order[i]))
			continue;
		s->target = order[i]; /* prepareGenerate */
		void (*gen)(Schema *) = order[i] == TARGET_TS   ? ctx->p->generate_ts
					: order[i] == TARGET_JS ? ctx->p->generate_js
								: ctx->p->generate_dts;
		gen(s);
	}
}

void es_plugin_run(const EsPlugin *p, PbMsg *req, Buf *out)
{
	RunCtx ctx = {p, req, NULL};
	if (try_run(run, &ctx)) {
		/* Upstream prints the error to stderr, prefixed with the plugin name. */
		Buf msg = {0};
		buf_adds(&msg, xcat(p->name, ": "));
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return;
	}
	Schema *s = ctx.s;
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3); /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, ED_MINIMUM);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, (uint64_t)MAX_EDITION(s));
	for (int i = 0; i < s->generated.n; i++) {
		GeneratedFile *f = s->generated.v[i];
		Str content = generated_file_content(f);
		if (!s->opts.keep_empty_files && !content.n)
			continue;
		Buf c = {0};
		if (f->preamble.p) {
			buf_add_str(&c, f->preamble);
			buf_addc(&c, '\n');
		}
		buf_add_str(&c, content);
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&c));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
}
