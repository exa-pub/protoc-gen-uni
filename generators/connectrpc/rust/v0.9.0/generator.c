/* Copyright 2026 The Connect Authors. Licensed under the Apache License, Version 2.0.
 *
 * connectrpc/rust v0.9.0: a port of protoc-gen-connect-rust from connectrpc-codegen 0.9.0
 * (https://github.com/connectrpc/connect-rust/tree/v0.9.0/connectrpc-codegen, buf.build/connectrpc/rust):
 * src/bin/protoc-gen-connect-rust.rs (this file), src/codegen.rs (codegen.c) and src/comments.rs
 * (comments.c). Type paths, request decoding and file names come from buffa-codegen 0.9.1
 * (pkg/languages/rust/buffa-codegen), the layout from prettyplease 0.2.37.
 *
 * Unity build: everything but the generator below is static. */
#include "buffa_codegen.h"
#include "heck.h"
#include "prettyplease.h"
#include "proc_macro2.h"
#include "rust_std.h"
#include "uni.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "comments.c"
#include "codegen.c"

enum { FEATURE_PROTO3_OPTIONAL = 1, FEATURE_SUPPORTS_EDITIONS = 2 };
enum { EDITION_2023 = 1000, EDITION_2024 = 1001 };

typedef struct {
	PbMsg *req;
	Str raw;
	Vec files;
} Run;

static Str parameter(const PbMsg *req)
{
	const PbSlot *s = pb_slot(req, CGR_PARAMETER);
	return s && s->n ? s->v[s->n - 1].s : (Str){"", 0};
}

/* main: decode_request, then codegen::generate. */
COLD static void run(void *arg)
{
	Run *r = arg;
	const char *err = bc_decode_request(r->raw, parameter(r->req));
	if (err)
		fail_str(str_c(err));
	Options o = {0};
	parse_options(parameter(r->req), &o);
	generate_services(&r->files, r->req, &o);
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	Run r = {0};
	r.req = req;
	r.raw = raw;
	if (try_run(run, &r)) {
		/* anyhow: `Error: {err:?}` on stderr, exit status 1 */
		Buf e = {0};
		buf_adds(&e, "Error: ");
		buf_add_str(&e, fail_message());
		buf_addc(&e, '\n');
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&e));
		return NULL;
	}
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS);
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, EDITION_2023);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, EDITION_2024);
	for (int i = 0; i < r.files.n; i++) {
		const GenFile *f = r.files.v[i];
		Buf b = {0};
		pb_put_bytes(&b, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&b, CGRESP_FILE_CONTENT, str_c(f->content));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&b));
	}
	return NULL;
}

const UniGenerator gen_connectrpc_rust_v0_9_0 = {
	.name = "connectrpc/rust",
	.version = "v0.9.0",
	.generate = generate,
};
