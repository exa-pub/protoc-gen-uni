/* Copyright (c) 2022 Nipunn Koorapati
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

/* community/nipunn1313-mypy v5.1.0: protoc-gen-mypy of mypy-protobuf v5.1.0
 * (buf.build/community/nipunn1313-mypy, https://github.com/nipunn1313/mypy-protobuf), which
 * generates mypy stubs (.pyi) for the Python protobuf runtime: main() of mypy_protobuf/main.py, on
 * the port of the rest of main.py in pkg/languages/python/mypy-protobuf. */
#include "mypy_protobuf.h"
#include "uni.h"

typedef struct {
	PbMsg *req;
	Buf files;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	mypy_check_extensions(c->req); /* request.ParseFromString */
	/* generate_mypy_stubs(Descriptors(request), response, "quiet" in request.parameter, ...) */
	MypyDescriptors *d = mypy_descriptors(c->req);
	MypyOptions o = mypy_options(pb_bytes(c->req, CGR_PARAMETER));
	mypy_generate_stubs(d, &o, &c->files);
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	RunCtx c = {req, {0}};
	if (try_run(run, &c)) {
		/* Upstream raises an exception: a traceback on stderr and exit status 1. */
		Buf msg = {0};
		if (!str_has_prefix(fail_message(), STR_LIT("unsupported by protoc-gen-uni")))
			buf_adds(&msg, "protoc-gen-mypy: ");
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	mypy_response_features(out);
	buf_add(out, c.files.p, c.files.n);
	return NULL;
}

const UniGenerator gen_community_nipunn1313_mypy_v5_1_0 = {
	.name = "community/nipunn1313-mypy",
	.version = "v5.1.0",
	.generate = generate,
};
