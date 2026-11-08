/* Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you use
 *    this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution. */

/* community/nanopb v0.4.91: a port of protoc-gen-nanopb of nanopb 0.4.9.1 (PyPI nanopb==0.4.9.1,
 * generator/nanopb_generator.py, buf.build/community/nanopb, https://github.com/nanopb/nanopb),
 * which generates .pb.h and .pb.c files for the nanopb C runtime. The Python semantics it relies
 * on (shlex, optparse, %-formatting, str.strip, sorted) live in pkg/languages/python/std, the
 * parts of python-protobuf (text_format) in pkg/languages/python/protobuf.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "python_protobuf.h"
#include "python.h"
#include "uni.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool protoc_insertion_points;

#include "model.c"
#include "tb.c"
#include "nanopb_generator.c"
#include "protofile.c"
#include "files.c"
#include "plugin.c"
#include "settings.c"

typedef struct {
	PbMsg *req;
	Buf files;
	bool exit0;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	if (parse_parameter(pb_bytes(c->req, CGR_PARAMETER)) == RUN_EXIT0) {
		c->exit0 = true;
		return;
	}
	run_plugin(c->req, &c->files);
}

/* The module state of a fresh process (the fuzzer runs many requests in one). */
static COLD void reset(void)
{
	c_style = protoc_insertion_points = false;
	nb_stderr = (Buf){0};
	nb_stderr_truncated = false;
	tb_depth = 0;
	deps_gen = 0;
	recursion_work = 0;
	recursion_gen = 0;
	walk_gen = 0;
	memset(&walk_tmp, 0, sizeof walk_tmp);
	import_work = 0;
	cmd = (CmdOpts){0};
	other_files = all_longnames = (Map){0};
	toplevel_options = NULL;
	init_bytes = 0;
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	reset();
	RunCtx c = {req, {0}, false};
	if (try_run(run, &c)) {
		/* Upstream exits with status 1 or 2 after writing to stderr: the error is all of it. */
		Str m = fail_message();
		Buf msg = {0};
		bool unsupported = str_has_prefix(m, STR_LIT("unsupported by protoc-gen-uni"));
		if (!unsupported && (nb_stderr_truncated || cmd.verbose)) {
			buf_adds(&msg, "unsupported by protoc-gen-uni: the error output of a failing run with -v or a long log: ");
		} else if (!unsupported) {
			buf_add(&msg, nb_stderr.p ? nb_stderr.p : "", nb_stderr.n);
		}
		buf_add_str(&msg, m);
		while (msg.n && msg.p[msg.n - 1] == '\n')
			msg.n--;
		buf_addc(&msg, '\n');
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	if (c.exit0)
		return NULL; /* -V: the version on stderr and no response */
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 1); /* FEATURE_PROTO3_OPTIONAL */
	buf_add(out, c.files.p, c.files.n);
	return NULL;
}

const UniGenerator gen_community_nanopb_v0_4_91 = {
	.name = "community/nanopb",
	.version = "v0.4.91",
	.generate = generate,
};
