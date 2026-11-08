// Copyright 2021-2026 The Connect Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* connectrpc/go v2.0.2: a port of protoc-gen-connect-go v2.0.2 (buf.build/connectrpc/go), the
 * Connect service generator of connect-go
 * (https://github.com/connectrpc/connect-go/tree/v2.0.2/cmd/protoc-gen-connect-go): main of
 * main.go (below) and the rest of main.go (main.c). The plugin framework is compiler/protogen of
 * protobuf-go v1.36.11 (the version of its go.mod): pkg/languages/go/protogen (v1.36.12) with
 * go_protobuf_v1_36_11 set. Built with Go 1.27.2, which formats like the Go 1.27.1 port of
 * pkg/languages/go/std (the default release).
 *
 * Unity build: this file includes main.c, and everything but the generator below is static. */
#include "genid.h"
#include "go_std.h"
#include "protogen.h"
#include "uni.h"

#include <string.h>

#define PROGRAM "protoc-gen-connect-go" /* filepath.Base(os.Args[0]) of the buf.build image */

#include "main.c"

/* The flag.FlagSet of main: -package_suffix (a string flag, default "connect"). */
static COLD Str set_flag(void *ctx, Str name, Str value)
{
	(void)ctx;
	if (!str_eqc(name, "package_suffix")) {
		Buf b = {0};
		buf_adds(&b, "no such flag -");
		buf_add_str(&b, name);
		return buf_view(&b);
	}
	packageSuffix = value;
	return (Str){NULL, 0};
}

typedef struct {
	PgPlugin *gen;
	Buf *out;
} RunCtx;

/* The function main passes to protogen.Options.Run, and Run's response. */
static void run(void *arg)
{
	RunCtx *c = arg;
	PgPlugin *plugin = c->gen;
	plugin->SupportedFeatures = 3; /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	plugin->SupportedEditionsMinimum = GENID_Edition_EDITION_PROTO2;
	plugin->SupportedEditionsMaximum = GENID_Edition_EDITION_2024;
	for (int i = 0; i < plugin->nFiles; i++) {
		if (plugin->Files[i]->Generate)
			generate(plugin, plugin->Files[i]);
	}
	pg_plugin_response(plugin, c->out);
}

/* An error of Options.New or a panic: upstream prints "protoc-gen-connect-go: <err>" to stderr
 * and exits with status 1 (a panic: a goroutine trace with status 2); the port reports the same
 * text in the CodeGeneratorResponse.error. */
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

	packageSuffix = STR_LIT(defaultPackageSuffix);
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	opts.ImportRewriteFunc = importRewriteFunc;
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

static COLD const char *generate_req(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the buf.build plugin binary: false (one space between the
	 * fields of the .pb.go.meta files, "proto: " in errors); and the protobuf-go v1.36.11
	 * runtime. Go 1.27.2 is the default release of the go_std port. */
	go_detrand = false;
	go_protobuf_v1_36_11 = true;
	const char *err = generate1(uni_req, raw, out);
	go_protobuf_v1_36_11 = false;
	return err;
}

/* parses_raw: generate_req parses raw with protobuf-go semantics (10000 levels) and takes only
 * the parameter from uni_req. */
const UniGenerator gen_connectrpc_go_v2_0_2 = {
	.name = "connectrpc/go",
	.version = "v2.0.2",
	.generate = generate_req,
	.parses_raw = true,
};
