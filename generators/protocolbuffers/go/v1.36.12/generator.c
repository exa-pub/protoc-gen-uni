/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* protocolbuffers/go v1.36.12: a port of protoc-gen-go v1.36.12 (buf.build/protocolbuffers/go),
 * the Go code generator of protobuf-go (https://github.com/protocolbuffers/protobuf-go):
 * cmd/protoc-gen-go/main.go (below) and cmd/protoc-gen-go/internal_gengo (the other .c files of
 * this directory). The plugin framework (compiler/protogen) and the runtime parts it needs live
 * in pkg/languages/go.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. A new version is a copy of the directory with the exported
 * name changed (gen_protocolbuffers_go_v1_36_12), so versions never clash. */
#include "go1_26_5.h"
#include "protogen.h"
#include "strs.h"
#include "tag.h"
#include "uni.h"
#include "genid.h"

#include <string.h>

char *go_itoa(int64_t v);
char *go_utoa(uint64_t v);

#include "init.c"
#include "main.c"
#include "reflect.c"
#include "opaque.c"
#include "well_known_types.c"

#define GRPC_DOC_URL "https://grpc.io/docs/languages/go/quickstart/#regenerate-grpc-code"

/* The flag.FlagSet of protoc-gen-go: -plugins (deprecated) and
 * -experimental_strip_nonfunctional_codegen. */
typedef struct {
	Str plugins;
	bool strip;
} Flags;

/* flag.FlagSet.Set */
static COLD Str set_flag(void *ctx, Str name, Str value)
{
	Flags *fl = ctx;
	if (str_eqc(name, "plugins") && name.n == 7) {
		fl->plugins = value;
		return (Str){NULL, 0};
	}
	if (str_eqc(name, "experimental_strip_nonfunctional_codegen")) {
		/* strconv.ParseBool */
		static const char *const t[] = {"1", "t", "T", "TRUE", "true", "True"};
		static const char *const f[] = {"0", "f", "F", "FALSE", "false", "False"};
		for (int i = 0; i < 6; i++) {
			if (str_eqc(value, t[i])) {
				fl->strip = true;
				return (Str){NULL, 0};
			}
			if (str_eqc(value, f[i])) {
				fl->strip = false;
				return (Str){NULL, 0};
			}
		}
		return STR_LIT("parse error");
	}
	Buf b = {0};
	buf_adds(&b, "no such flag -");
	buf_add_str(&b, name);
	return buf_view(&b);
}

typedef struct {
	PgPlugin *gen;
	Str plugins;
	Buf *out;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	if (c->plugins.n) {
		pg_plugin_error(c->gen, str_c("protoc-gen-go: plugins are not supported; use 'protoc --go-grpc_out=...' to generate "
					      "gRPC\n\nSee " GRPC_DOC_URL " for more information."));
	} else {
		gengo_run(c->gen);
	}
	pg_plugin_response(c->gen, c->out);
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* main.go: proto.Unmarshal of the request (protobuf-go semantics), then the parameter
	 * without the uni-* options. */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req)
		return perr; /* upstream prints the error to stderr and exits with 1 */
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));

	Flags fl = {{"", 0}, false};
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	opts.ParamCtx = &fl;
	opts.InternalStripForEditionsDiff = &fl.strip;
	Str err;
	PgPlugin *gen = pg_options_new(&opts, req, &err);
	if (!gen) {
		/* Upstream prints "protoc-gen-go: <err>" to stderr and exits with 1. */
		Buf msg = {0};
		buf_adds(&msg, "protoc-gen-go: ");
		buf_add_str(&msg, err);
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	RunCtx c = {gen, fl.plugins, out};
	if (try_run(run, &c)) {
		/* a panic of protoc-gen-go */
		out->n = 0;
		Buf msg = {0};
		buf_adds(&msg, "protoc-gen-go: ");
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
	}
	return NULL;
}

const UniGenerator gen_protocolbuffers_go_v1_36_12 = {"protocolbuffers/go", Version, generate};
