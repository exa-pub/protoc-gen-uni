/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* grpc/go v1.6.2: a port of protoc-gen-go-grpc v1.6.2 (buf.build/grpc/go), the gRPC service
 * generator of grpc-go (https://github.com/grpc/grpc-go/tree/cmd/protoc-gen-go-grpc/v1.6.2):
 * cmd/protoc-gen-go-grpc/main.go (below) and grpc.go (grpc.c), with bufbuild/plugins'
 * separate-package.patch. The plugin framework is compiler/protogen of protobuf-go v1.36.11 (the
 * version of its go.mod): pkg/languages/go/protogen (v1.36.12) with go_protobuf_v1_36_11 set,
 * which gives the runtime behaviour of v1.36.11 where it differs. Built with Go 1.26.3, which
 * formats like Go 1.26.5 (go_std_go1_26_5: git diff go1.26.3 go1.26.5 changes only go/build).
 *
 * Unity build: this file includes grpc.c, and everything but the generator below is static. */
#include "genid.h"
#include "go_std.h"
#include "protogen.h"
#include "uni.h"

#include <string.h>

char *go_itoa(int64_t v);

#define PROGRAM "protoc-gen-go-grpc" /* filepath.Base(os.Args[0]) of the buf.build image */
#define GRPC_GO_VERSION "1.6.2"

#include "grpc.c"

/* The flag.FlagSet of main: -require_unimplemented_servers (default true) and, from the patch,
 * -separate_package (default false). */
static COLD Str set_flag(void *ctx, Str name, Str value)
{
	(void)ctx;
	bool *flag;
	if (str_eqc(name, "require_unimplemented_servers"))
		flag = &requireUnimplemented;
	else if (str_eqc(name, "separate_package"))
		flag = &separatePackage;
	else {
		Buf b = {0};
		buf_adds(&b, "no such flag -");
		buf_add_str(&b, name);
		return buf_view(&b);
	}
	/* boolValue.Set: strconv.ParseBool, the value is stored also on error */
	static const char *const t[] = {"1", "t", "T", "TRUE", "true", "True"};
	static const char *const f[] = {"0", "f", "F", "FALSE", "false", "False"};
	for (int i = 0; i < 6; i++) {
		if (str_eqc(value, t[i])) {
			*flag = true;
			return (Str){NULL, 0};
		}
		if (str_eqc(value, f[i])) {
			*flag = false;
			return (Str){NULL, 0};
		}
	}
	*flag = false;
	return STR_LIT("parse error");
}

typedef struct {
	PgPlugin *gen;
	Buf *out;
} RunCtx;

/* The function main passes to protogen.Options.Run, and Run's response. */
static void run(void *arg)
{
	RunCtx *c = arg;
	PgPlugin *gen = c->gen;
	gen->SupportedFeatures = 3; /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	gen->SupportedEditionsMinimum = GENID_Edition_EDITION_PROTO2;
	gen->SupportedEditionsMaximum = GENID_Edition_EDITION_2024;
	for (int i = 0; i < gen->nFiles; i++) {
		if (!gen->Files[i]->Generate)
			continue;
		generate_file(gen, gen->Files[i]);
	}
	pg_plugin_response(gen, c->out);
}

/* An error of Options.New or a panic: upstream prints "protoc-gen-go-grpc: <err>" to stderr and
 * exits with status 1 (a panic: a goroutine trace with status 2); the port reports the same text
 * in the CodeGeneratorResponse.error. */
static COLD void error_response(Buf *out, Str err)
{
	Buf b = {0};
	buf_adds(&b, PROGRAM ": ");
	buf_add_str(&b, err);
	out->n = 0;
	pb_put_bytes(out, CGRESP_ERROR, buf_view(&b));
}

static COLD const char *generate1(PbMsg *uni_req, Str raw, Buf *out)
{
	/* protogen.Options.Run: proto.Unmarshal of the request (protobuf-go semantics) and
	 * Options.New. The parameter is the one without the uni-* options. */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		/* a request upstream accepts and the port does not support */
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (!req)
		return xcat(PROGRAM ": ", perr); /* unparsable: stderr and exit status 1, as upstream */
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));
	if ((perr = pg_unsupported(req))) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}

	requireUnimplemented = true;
	separatePackage = false;
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	Str err;
	PgPlugin *gen = pg_options_new(&opts, req, &err);
	if (!gen) {
		error_response(out, err);
		return NULL;
	}
	RunCtx c = {gen, out};
	if (try_run(run, &c))
		error_response(out, fail_message());
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the buf.build plugin binary: true ("proto:" with a no-break
	 * space in errors, two spaces between the fields of the .pb.go.meta files); and the
	 * protobuf-go v1.36.11 runtime. */
	go_detrand = true;
	go_protobuf_v1_36_11 = true;
	go_std_use(&go_std_go1_26_5); /* Go 1.26.3, the toolchain of the buf.build plugin */
	const char *err = generate1(uni_req, raw, out);
	go_std_use(NULL);
	go_detrand = false;
	go_protobuf_v1_36_11 = false;
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics (10000 levels) and takes only the
 * parameter from uni_req. */
const UniGenerator gen_grpc_go_v1_6_2 = {
	.name = "grpc/go",
	.version = "v1.6.2",
	.generate = generate,
	.parses_raw = true,
};
