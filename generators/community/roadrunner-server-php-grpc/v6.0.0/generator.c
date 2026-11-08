/* MIT License
 *
 * Copyright (c) 2022 SpiralScout
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

/* community/roadrunner-server-php-grpc v6.0.0: a port of protoc-gen-php-grpc of
 * roadrunner-server/grpc v6.0.0 (https://github.com/roadrunner-server/grpc/tree/v6.0.0/protoc_plugins,
 * buf.build/community/roadrunner-server-php-grpc), built with Go 1.27.1 and protobuf-go v1.36.12:
 * protoc-gen-php-grpc/main.go (below) and php/{keywords,ns,template,generate}.go (the other .c
 * files). The request is decoded with the protobuf-go semantics of pkg/languages/go/protodesc
 * (no extension types: the plugin links only descriptorpb and pluginpb), the Go 1.27.1 standard
 * library (unicode case mappings) is pkg/languages/go/std.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "go_std.h"
#include "protodesc.h"
#include "uni.h"

#include <string.h>

#include "keywords.c"
#include "ns.c"
#include "generate.c"

typedef struct {
	const Request *req;
	Buf *out;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	Generate(c->req, c->out);
}

static COLD const char *generate_(PbMsg *uni_req, Str raw, Buf *out)
{
	(void)uni_req; /* the parameter is ignored */
	/* readRequest: proto.Unmarshal; an error panics */
	PbMsg *req = pb_new(&gopb_schema_msg(gopb_schema_types(), "google.protobuf.compiler.CodeGeneratorRequest")->pb);
	const char *err = gopb_decode(req, (const uint8_t *)raw.p, raw.n);
	if (err && strcmp(err, GOPB_ERR_TOO_DEEP) == 0) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(err));
		return NULL;
	}
	if (err)
		return xcat("panic: ", err);
	Request r = {0};
	request_init(&r, req);
	RunCtx c = {&r, out};
	if (try_run(run, &c)) {
		/* a panic: upstream crashes with a goroutine trace (exit status 2) */
		out->n = 0;
		Str msg = fail_message();
		if (str_has_prefix(msg, STR_LIT(UNSUPPORTED))) {
			pb_put_bytes(out, CGRESP_ERROR, msg);
			return NULL;
		}
		return xcat("panic: ", xstrndup(msg.p, msg.n));
	}
	return NULL;
}

/* parses_raw: the request is decoded from raw with protobuf-go semantics (10000 levels). */
const UniGenerator gen_community_roadrunner_server_php_grpc_v6_0_0 = {
	.name = "community/roadrunner-server-php-grpc",
	.version = "v6.0.0",
	.generate = generate_,
	.parses_raw = true,
};
