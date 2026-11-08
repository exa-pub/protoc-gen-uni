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

/* community/nipunn1313-mypy-grpc v5.1.0: protoc-gen-mypy_grpc of mypy-protobuf v5.1.0
 * (buf.build/community/nipunn1313-mypy-grpc, https://github.com/nipunn1313/mypy-protobuf), which
 * generates mypy stubs (_pb2_grpc.pyi) for the code of grpcio-tools: grpc() of
 * mypy_protobuf/main.py, on the port of the rest of main.py in pkg/languages/python/mypy-protobuf. */
#include "mypy_protobuf.h"
#include "uni.h"

/* The traceback of the ValueError of GRPCType.from_parameter, as CPython 3.13 prints it in the
 * buf.build image (the protoc-gen-mypy_grpc script of pip calls grpc()). */
static const char FROM_PARAMETER_TRACEBACK[] =
	"Traceback (most recent call last):\n"
	"  File \"/app/bin/protoc-gen-mypy_grpc\", line 6, in <module>\n"
	"    sys.exit(grpc())\n"
	"             ~~~~^^\n"
	"  File \"/app/lib/python3.13/site-packages/mypy_protobuf/main.py\", line 1560, in grpc\n"
	"    GRPCType.from_parameter(request.parameter),\n"
	"    ~~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^\n"
	"  File \"/app/lib/python3.13/site-packages/mypy_protobuf/main.py\", line 151, in from_parameter\n"
	"    raise ValueError(\"Cannot specify both only_sync and only_async\")\n"
	"ValueError: Cannot specify both only_sync and only_async\n";

typedef struct {
	PbMsg *req;
	Buf files;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	mypy_check_extensions(c->req); /* request.ParseFromString */
	/* generate_mypy_grpc_stubs(Descriptors(request), response, "quiet" in request.parameter, ...,
	 * GRPCType.from_parameter(request.parameter)) */
	MypyDescriptors *d = mypy_descriptors(c->req);
	Str param = pb_bytes(c->req, CGR_PARAMETER);
	MypyOptions o = mypy_options(param);
	MypyGrpcType type;
	if (!mypy_grpc_type_from_parameter(param, &type))
		fail_str((Str){FROM_PARAMETER_TRACEBACK, sizeof FROM_PARAMETER_TRACEBACK - 1});
	mypy_generate_grpc_stubs(d, &o, type, &c->files);
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	RunCtx c = {req, {0}};
	if (try_run(run, &c)) {
		/* Upstream raises an exception: a traceback on stderr and exit status 1. */
		Buf msg = {0};
		if (!str_has_prefix(fail_message(), STR_LIT("unsupported by protoc-gen-uni")) &&
		    !str_has_prefix(fail_message(), STR_LIT("Traceback")))
			buf_adds(&msg, "protoc-gen-mypy_grpc: ");
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	mypy_response_features(out);
	buf_add(out, c.files.p, c.files.n);
	return NULL;
}

const UniGenerator gen_community_nipunn1313_mypy_grpc_v5_1_0 = {
	.name = "community/nipunn1313-mypy-grpc",
	.version = "v5.1.0",
	.generate = generate,
};
