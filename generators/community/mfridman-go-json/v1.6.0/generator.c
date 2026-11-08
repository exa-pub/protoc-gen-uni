/* MIT License
 *
 * Copyright (c) 2019 Mitchell Hashimoto
 * Copyright (c) 2023 Michael Fridman
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

/* community/mfridman-go-json v1.6.0: a port of protoc-gen-go-json v1.6.0
 * (https://github.com/mfridman/protoc-gen-go-json, buf.build/community/mfridman-go-json):
 * main.go and internal/plugin/{plugin,options}.go (below) and internal/gen/template.go
 * (template.c). The plugin framework (github.com/bufbuild/protoplugin and compiler/protogen of
 * protobuf-go v1.36.11, identical to v1.36.12) lives in pkg/languages/go.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "go_std.h"
#include "protogen.h"
#include "goprotoplugin.h"
#include "uni.h"

#include <string.h>

#include "template.c"

/* ---- internal/plugin/options.go ---- */

/* strconv.ParseBool */
static COLD const char *parse_bool(bool *target, Str value)
{
	static const char *const t[] = {"1", "t", "T", "TRUE", "true", "True"};
	static const char *const f[] = {"0", "f", "F", "FALSE", "false", "False"};
	for (int i = 0; i < 6; i++) {
		if (str_eqc(value, t[i])) {
			*target = true;
			return NULL;
		}
		if (str_eqc(value, f[i])) {
			*target = false;
			return NULL;
		}
	}
	return xcat("strconv.ParseBool: parsing ", quote_go(value), ": invalid syntax");
}

/* parseOptions: unknown names are ignored. */
static COLD const char *parse_options(Str raw, Options *opts)
{
	static const struct {
		const char *name;
		size_t offset;
	} supported[] = {
		/* Marshal options */
		{"enums_as_ints", offsetof(Options, EnumsAsInts)},
		{"emit_defaults", offsetof(Options, EmitDefaults)},
		{"emit_defaults_without_null", offsetof(Options, EmitDefaultValues)},
		{"orig_name", offsetof(Options, OrigName)},
		/* Unmarshal options */
		{"allow_unknown", offsetof(Options, AllowUnknownFields)},
		/* Additional options */
		{"emit_scanner_valuer", offsetof(Options, EmitScannerValuer)},
	};
	if (raw.n == 0)
		return NULL;
	Str *all;
	int n = str_split_s(raw, ',', &all);
	for (int i = 0; i < n; i++) {
		Str opt = all[i];
		const char *eq = memchr(opt.p, '=', opt.n);
		if (!eq)
			return xcat("invalid option, must be in the form of name=value: ", xstrndup(opt.p, opt.n));
		Str name = {opt.p, (size_t)(eq - opt.p)};
		Str value = {eq + 1, opt.n - name.n - 1};
		/* Check if the option is supported, but don't error. */
		for (size_t j = 0; j < sizeof supported / sizeof *supported; j++) {
			if (!str_eqc(name, supported[j].name))
				continue;
			const char *err = parse_bool((bool *)((char *)opts + supported[j].offset), value);
			if (err)
				return xcat("invalid value for ", supported[j].name, ": ", err);
		}
	}
	return NULL;
}

/* ---- what the port does not support ---- */

#define UNSUPPORTED "unsupported by protoc-gen-uni: "

static bool has_nul(Str s) { return s.n && memchr(s.p, 0, s.n); }

/* Input that upstream accepts and the port does not support (the protogen port ends file names,
 * go_package and M values at a NUL byte): the error to report, or NULL. Checked after the
 * request validation of protoplugin, which handles NUL bytes as upstream. */
static COLD const char *unsupported(const PbMsg *req)
{
	enum { FILE_OPTIONS_GO_PACKAGE = 11 };
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *fd = pb_msg_at(req, CGR_PROTO_FILE, i);
		Str name = pb_bytes(fd, FDP_NAME);
		if (has_nul(name))
			return xcat(UNSUPPORTED "NUL byte in file name ", quote_go(name));
		for (int j = 0; j < pb_count(fd, FDP_DEPENDENCY); j++) {
			Str dep = pb_bytes_at(fd, FDP_DEPENDENCY, j);
			if (has_nul(dep))
				return xcat(UNSUPPORTED "NUL byte in file name ", quote_go(dep));
		}
		Str pkg = pb_bytes(pb_msg(fd, FDP_OPTIONS), FILE_OPTIONS_GO_PACKAGE);
		if (has_nul(pkg))
			return xcat(UNSUPPORTED "NUL byte in go_package ", quote_go(pkg), " of ", quote_go(name));
	}
	/* compiler/protogen: the parameter elements "M<file>=<import path>" */
	Str *params;
	int n = str_split_s(pb_bytes(req, CGR_PARAMETER), ',', &params);
	for (int i = 0; i < n; i++)
		if (params[i].n && params[i].p[0] == 'M' && has_nul(params[i]))
			return xcat(UNSUPPORTED "NUL byte in parameter ", quote_go(params[i]));
	return NULL;
}

/* A response with only the error: what the port reports for input it does not support. */
static COLD void unsupported_response(Buf *out, const char *err) { pb_put_bytes(out, CGRESP_ERROR, str_c(err)); }

/* ---- internal/plugin/plugin.go ---- */

#define defaultFilenameSuffix ".pb.json.go"

/* generate */
static void generate_files(PgPlugin *p, const Options *opt)
{
	for (int i = 0; i < p->nFiles; i++) {
		PgFile *f = p->Files[i];
		if (!f->Generate || f->nMessages == 0)
			continue;
		PgGeneratedFile *g = pg_new_generated_file(p, xcat(f->GeneratedFilenamePrefix, defaultFilenameSuffix), f->GoImportPath);
		/* ApplyTemplate cannot fail here: the templates only write to g. */
		apply_template(g, f, opt);
	}
}

typedef struct {
	PbMsg *req;
	Buf *out;
	const char *err; /* returned by Handle or Run: printed to stderr, exit 1 */
} HandleCtx;

/* Handle, and what protoplugin.run does with its result. */
static COLD void handle(void *arg)
{
	HandleCtx *c = arg;
	Str perr;
	PgOptions popts = {0};
	PgPlugin *p = pg_options_new(&popts, c->req, &perr);
	if (!p && str_eqc(perr, GOPB_ERR_TOO_DEEP)) {
		unsupported_response(c->out, GOPB_ERR_TOO_DEEP);
		return;
	}
	if (!p) {
		c->err = xstrndup(perr.p, perr.n);
		return;
	}
	Options opt = {0};
	c->err = parse_options(pb_bytes(c->req, CGR_PARAMETER), &opt);
	if (c->err)
		return;
	generate_files(p, &opt);

	Buf resp = {0};
	pg_plugin_response(p, &resp);
	GoppFile *files;
	int n;
	Str error;
	if (!gopp_parse_response(buf_view(&resp), &files, &n, &error))
		fail("protogen response: cannot parse");
	GoppResponseWriter *w = gopp_response_writer_new(NULL, NULL);
	gopp_add_response_files(w, files, n);
	gopp_add_error(w, error);
	gopp_set_feature_proto3_optional(w);
	gopp_set_feature_supports_editions(w, 999 /* EDITION_PROTO3 */, 0x7FFFFFFF /* EDITION_MAX */);
	c->err = gopp_to_response(w, c->out);
}

static COLD const char *generate1(PbMsg *uni_req, Str raw, Buf *out)
{
	/* protoplugin.run: proto.Unmarshal of the request (protobuf-go semantics), then NewRequest;
	 * the parameter is the one without the uni-* options. Errors are printed by main.go as
	 * "protoc-gen-go-json: <err>" with exit status 1. */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		unsupported_response(out, perr);
		return NULL;
	}
	if (!req)
		return xcat("protoc-gen-go-json: ", perr);
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));
	perr = gopp_validate_request(req);
	if (perr)
		return xcat("protoc-gen-go-json: ", perr);
	perr = unsupported(req);
	if (perr) {
		unsupported_response(out, perr);
		return NULL;
	}
	HandleCtx c = {req, out, NULL};
	const char *panic = try_run(handle, &c);
	if (panic) {
		/* a panic: upstream crashes with a goroutine trace (exit status 2) */
		Str msg = fail_message();
		return xcat("protoc-gen-go-json: ", xstrndup(msg.p, msg.n));
	}
	if (c.err)
		return xcat("protoc-gen-go-json: ", c.err);
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	go_std_use(&go_std_go1_26_5); /* Go 1.26.1 prints like Go 1.26.5 */
	const char *err = generate1(uni_req, raw, out);
	go_std_use(NULL);
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics (10000 levels) and takes only the
 * parameter from uni_req. */
const UniGenerator gen_community_mfridman_go_json_v1_6_0 = {
	.name = "community/mfridman-go-json",
	.version = "v1.6.0",
	.generate = generate,
	.parses_raw = true,
};
