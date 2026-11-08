/* Copyright (c) protoc-bridge authors (Nadav Samet and contributors).
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

/* A port of protoc-gen/src/main/scala/protocgen/{CodeGenApp,CodeGenRequest,CodeGenResponse}.scala
 * of protoc-bridge v0.9.10. */
#include "protocgen.h"

typedef struct {
	PbMsg *req;
	Str parameter;
	void (*process)(const CodeGenRequest *, CodeGenResponse *, void *);
	void *arg;
	CodeGenResponse response;
} RunCtx;

/* CodeGenRequest.apply */
static void run(void *arg)
{
	RunCtx *c = arg;
	CodeGenRequest r = {0};
	r.as_proto = c->req;
	r.parameter = java_string(c->parameter);
	jpool_reset(); /* fuzzing runs many requests in one process */
	/* fileDescriptorsByName: every proto_file is built from the files before it */
	Map byname = {0}, index = {0}; /* index: name -> 1 + position in all_protos */
	for (int i = 0; i < pb_count(c->req, CGR_PROTO_FILE); i++) {
		JFile *f = jfile_build(pb_msg_at(c->req, CGR_PROTO_FILE, i), &byname);
		intptr_t at = (intptr_t)map_get(&index, f->name);
		if (!at) {
			vec_push(&r.all_protos, f);
			map_put(&index, f->name, (void *)(intptr_t)r.all_protos.n);
		} else {
			r.all_protos.v[at - 1] = f;
		}
		map_put(&byname, f->name, f);
	}
	for (int i = 0; i < pb_count(c->req, CGR_FILE_TO_GENERATE); i++) {
		const char *name = jstr_at(c->req, CGR_FILE_TO_GENERATE, i);
		JFile *f = map_get(&byname, name);
		if (!f)
			java_exception("java.util.NoSuchElementException", xfmt("key not found: %s", name));
		vec_push(&r.files_to_generate, f);
	}
	c->process(&r, &c->response, c->arg);
}

const char *protocgen_run(PbMsg *req, Str raw, const PbMsgDef *registry,
			  void (*process)(const CodeGenRequest *, CodeGenResponse *, void *), void *arg, Buf *out)
{
	RunCtx c = {0};
	c.parameter = pb_bytes(req, CGR_PARAMETER);
	c.process = process;
	c.arg = arg;
	/* CodeGeneratorRequest.parseFrom(input, registry); the uni-* options of the original parameter
	 * are already removed from req */
	c.req = pb_new(registry);
	if (!pb_decode(c.req, (const uint8_t *)raw.p, raw.n)) {
		pb_put_bytes(out, CGRESP_ERROR, STR_LIT("com.google.protobuf.InvalidProtocolBufferException: Protocol message is invalid."));
		return NULL;
	}
	if (try_run(run, &c)) {
		pb_put_bytes(out, CGRESP_ERROR, fail_message());
		return NULL;
	}
	/* CodeGenResponse.toCodeGeneratorResponse */
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, c.response.supported_features);
	for (int i = 0; i < c.response.files.n; i++) {
		CodeGenFile *f = c.response.files.v[i];
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, str_c(f->content));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
	return NULL;
}
