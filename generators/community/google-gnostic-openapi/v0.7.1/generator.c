/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* community/google-gnostic-openapi v0.7.1: a port of protoc-gen-openapi of gnostic v0.7.1
 * (https://github.com/google/gnostic, buf.build/community/google-gnostic-openapi), built with Go
 * 1.25.0: cmd/protoc-gen-openapi/main.go (below) and generator/ (generator.go, reflector.go,
 * utils.go, wellknown/). The plugin framework is compiler/protogen of protobuf-go v1.36.7
 * (pkg/languages/go/protogen with PgOptions.Version = &pg_v1_36_7); the OpenAPI model and its
 * YAML output are gnostic-models v0.7.0 and go.yaml.in/yaml/v3 v3.0.4
 * (pkg/languages/go/gnostic-models, pkg/languages/go/yaml); google.api annotations come from
 * genproto (pkg/languages/go/genproto).
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "annotations.h"
#include "go_std.h"
#include "openapiv3.h"
#include "protogen_v1.36.7.h"
#include "uni.h"

#include <string.h>

#define PROGRAM "protoc-gen-openapi"

/* generator.Configuration (the flags of main.go) */
typedef struct {
	Str Version, Title, Description, Naming;
	bool FQSchemaNaming;
	Str EnumType;
	int64_t CircularDepth;
	bool DefaultResponse;
	Str OutputMode;
} Configuration;

typedef struct Gen Gen;
static void require_schema(Gen *g, Str name);

/* A nil pointer dereference of upstream (a Go panic: a goroutine trace and exit status 2). */
static NORETURN COLD void nil_panic(const char *where, const GoMessage *m)
{
	fail("unsupported by protoc-gen-uni: protoc-gen-openapi panics (nil pointer dereference in %s%s%s)", where,
	     m ? " for " : "", m ? m->d.full_name : "");
}

#include "generator/wellknown/mediatypes.c"
#include "generator/wellknown/schemas.c"

/* A message in walk order: the files of plugin.Files, nested messages before their parent. */
typedef struct {
	const PgMessage *message;
	Str name; /* formatMessageName */
} Walked;

typedef struct {
	int *pos; /* positions in walk order */
	int n;
	int last_pass; /* the pass in which the name was last added to the list; -2: never */
	bool generated;
} NameInfo;

typedef struct {
	int pos;
	NameInfo *info;
} Pending;

/* A min-heap of pending generations by position. */
typedef struct {
	Pending *v;
	int n, cap;
} Heap;

struct Gen {
	const Configuration *conf;
	PgPlugin *plugin;
	PgFile **inputFiles;
	int nInputFiles;
	/* the required schemas (see generator/generator.c) */
	Vec walk;  /* Walked *, in walk order */
	Map names; /* schema name -> NameInfo * */
	int pass, cur_pos;
	Heap cur, next;
	/* the path items of the document by name */
	Map path_items;
	int paths_indexed;
};

#include "generator/utils.c"
#include "generator/reflector.c"

#include "generator/generator.c"

/* ---- main.go ---- */

typedef struct {
	Configuration conf;
} Flags;

/* flag.FlagSet.Set of the flags of main.go (the errors of flag 1.25: "no such flag -%v", and
 * errParse / errRange of the int and bool values). */
static Str set_flag(void *ctx, Str name, Str value)
{
	Configuration *c = &((Flags *)ctx)->conf;
	if (str_eqc(name, "version")) {
		c->Version = value;
	} else if (str_eqc(name, "title")) {
		c->Title = value;
	} else if (str_eqc(name, "description")) {
		c->Description = value;
	} else if (str_eqc(name, "naming")) {
		c->Naming = value;
	} else if (str_eqc(name, "fq_schema_naming") || str_eqc(name, "default_response")) {
		bool v;
		int err = go_std_parse_bool(value.p, value.n, &v);
		*(str_eqc(name, "fq_schema_naming") ? &c->FQSchemaNaming : &c->DefaultResponse) = v;
		if (err)
			return STR_LIT("parse error");
	} else if (str_eqc(name, "enum_type")) {
		c->EnumType = value;
	} else if (str_eqc(name, "depth")) {
		int64_t v;
		int err = go_std_parse_int(value.p, value.n, 0, 64, &v);
		c->CircularDepth = v;
		if (err == GO_STD_ERR_RANGE)
			return STR_LIT("value out of range");
		if (err)
			return STR_LIT("parse error");
	} else if (str_eqc(name, "output_mode")) {
		c->OutputMode = value;
	} else {
		Buf e = {0};
		buf_adds(&e, "no such flag -");
		buf_add_str(&e, name);
		return buf_view(&e);
	}
	return (Str){NULL, 0};
}

/* NewOpenAPIv3Generator */
static Gen *new_generator(PgPlugin *plugin, const Configuration *conf, PgFile **input_files, int n)
{
	Gen *g = xalloc(sizeof *g);
	g->conf = conf;
	g->plugin = plugin;
	g->inputFiles = input_files;
	g->nInputFiles = n;
	for (int i = 0; i < plugin->nFiles; i++)
		walk_messages(g, plugin->Files[i]->Messages, plugin->Files[i]->nMessages);
	for (int i = 0; i < g->walk.n; i++) {
		const Walked *w = g->walk.v[i];
		NameInfo *ni = name_info(g, w->name);
		ni->pos = xrealloc(ni->pos, sizeof *ni->pos * (size_t)ni->n, sizeof *ni->pos * (size_t)(ni->n + 1));
		ni->pos[ni->n++] = i;
	}
	return g;
}

/* filepath.Ext */
static Str file_ext(const char *path)
{
	size_t n = strlen(path);
	for (size_t i = n; i-- > 0 && path[i] != '/';)
		if (path[i] == '.')
			return (Str){path + i, n - i};
	return (Str){"", 0};
}

typedef struct {
	PgPlugin *plugin;
	Flags *fl;
	Buf *out;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	PgPlugin *plugin = c->plugin;
	const Configuration *conf = &c->fl->conf;
	/* Enable "optional" keyword in front of type (e.g. optional string label = 1;) */
	plugin->SupportedFeatures = 1; /* FEATURE_PROTO3_OPTIONAL */
	Str err = {NULL, 0};
	if (str_eqc(conf->OutputMode, "source_relative")) {
		for (int i = 0; i < plugin->nFiles && !err.p; i++) {
			PgFile *file = plugin->Files[i];
			if (!file->Generate)
				continue;
			const char *path = file->Desc->d.name;
			Str ext = file_ext(path);
			size_t n = strlen(path);
			if (ext.n && n >= ext.n)
				n -= ext.n;
			const char *outfile = xcat(xstrndup(path, n), ".openapi.yaml");
			PgGeneratedFile *output_file = pg_new_generated_file(plugin, outfile, "");
			Gen *gen = new_generator(plugin, conf, &plugin->Files[i], 1);
			err = generator_run(gen, output_file);
		}
	} else {
		PgGeneratedFile *output_file = pg_new_generated_file(plugin, "openapi.yaml", "");
		err = generator_run(new_generator(plugin, conf, plugin->Files, plugin->nFiles), output_file);
	}
	/* Errors from the plugin function are reported in the CodeGeneratorResponse. */
	if (err.p)
		pg_plugin_error(plugin, err);
	pg_plugin_response(plugin, c->out);
}

/* The extensions in protoregistry.GlobalTypes of protoc-gen-openapi besides protogen's: genproto
 * annotations and gnostic-models openapiv3. */
static const char *const *linked_extensions(void)
{
	static const char *names[24];
	int n = 0;
	for (const char *const *p = genproto_annotations_extensions; *p; p++)
		names[n++] = *p;
	names[n++] = "openapi.v3.document";
	names[n++] = "openapi.v3.operation";
	names[n++] = "openapi.v3.schema";
	names[n++] = "openapi.v3.property";
	names[n] = NULL;
	return names;
}

/* ---- the linked extensions in the request ----
 *
 * proto.Unmarshal of the request decodes the extensions linked into the plugin (eagerly: lazy
 * extension decoding is off in protobuf-go builds without protolegacy), so malformed values make
 * the request unparsable. pkg/languages/go/protogen keeps them as unknown fields; they are
 * checked here, in the order of the descriptor fields. */

typedef struct {
	uint32_t num;
	const char *name;
	int kind; /* 0: message (def), 1: string (UTF-8) */
	const GoMsgDef *def;
} LinkedExt;

static const char *check_options(const PbMsg *opts, const LinkedExt *exts, int n)
{
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		for (int i = 0; i < n; i++) {
			if (exts[i].num != f.num || f.wt != WT_LEN)
				continue;
			if (exts[i].kind == 0) {
				PbMsg *m = pb_new(&exts[i].def->pb);
				const char *err = gopb_decode(m, f.val, f.vlen);
				if (err)
					return err;
			} else {
				Str s = {(const char *)f.val, f.vlen};
				for (size_t k = 0; k < s.n;) {
					uint32_t rune;
					int w = utf8_decode(s.p + k, s.n - k, &rune);
					if (rune == 0xFFFD && w == 1)
						return xcat(go_err_prefix(), "field ", exts[i].name, " contains invalid UTF-8");
					k += (size_t)w;
				}
			}
		}
	}
	return NULL;
}

static const char *check_field_options(const PbMsg *fd)
{
	const LinkedExt exts[] = {{1143, "openapi.v3.property", 0, oa_def(OA_Schema)}};
	return check_options(pb_msg(fd, FD_OPTIONS), exts, 1);
}

static const char *check_message(const PbMsg *m, int depth)
{
	const char *err;
	if (depth > 1000)
		return NULL; /* protodesc rejects deeper nesting later */
	for (int i = 0; i < pb_count(m, DP_FIELD); i++)
		if ((err = check_field_options(pb_msg_at(m, DP_FIELD, i))))
			return err;
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		if ((err = check_message(pb_msg_at(m, DP_NESTED_TYPE, i), depth + 1)))
			return err;
	for (int i = 0; i < pb_count(m, DP_EXTENSION); i++)
		if ((err = check_field_options(pb_msg_at(m, DP_EXTENSION, i))))
			return err;
	const LinkedExt exts[] = {{1143, "openapi.v3.schema", 0, oa_def(OA_Schema)}};
	return check_options(pb_msg(m, DP_OPTIONS), exts, 1);
}

static const char *check_extensions(const PbMsg *req)
{
	const char *err;
	const LinkedExt method_exts[] = {
		{GENPROTO_E_HTTP, "google.api.http", 0, genproto_http_rule()},
		{1143, "openapi.v3.operation", 0, oa_def(OA_Operation)},
		{GENPROTO_E_METHOD_SIGNATURE, "google.api.method_signature", 1, NULL},
	};
	const LinkedExt service_exts[] = {
		{GENPROTO_E_DEFAULT_HOST, "google.api.default_host", 1, NULL},
		{GENPROTO_E_OAUTH_SCOPES, "google.api.oauth_scopes", 1, NULL},
		{GENPROTO_E_API_VERSION, "google.api.api_version", 1, NULL},
	};
	const LinkedExt file_exts[] = {{1143, "openapi.v3.document", 0, oa_def(OA_Document)}};
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *f = pb_msg_at(req, CGR_PROTO_FILE, i);
		for (int k = 0; k < pb_count(f, FDP_MESSAGE_TYPE); k++)
			if ((err = check_message(pb_msg_at(f, FDP_MESSAGE_TYPE, k), 0)))
				return err;
		for (int k = 0; k < pb_count(f, FDP_SERVICE); k++) {
			const PbMsg *s = pb_msg_at(f, FDP_SERVICE, k);
			for (int j = 0; j < pb_count(s, SDP_METHOD); j++)
				if ((err = check_options(pb_msg(pb_msg_at(s, SDP_METHOD, j), MDP_OPTIONS), method_exts, 3)))
					return err;
			if ((err = check_options(pb_msg(s, 3), service_exts, 3)))
				return err;
		}
		for (int k = 0; k < pb_count(f, FDP_EXTENSION); k++)
			if ((err = check_field_options(pb_msg_at(f, FDP_EXTENSION, k))))
				return err;
		if ((err = check_options(pb_msg(f, FDP_OPTIONS), file_exts, 1)))
			return err;
	}
	return NULL;
}

/* An error of Options.New or a panic: upstream prints "protoc-gen-openapi: <err>" to stderr and
 * exits with status 1; the port reports the same text in the CodeGeneratorResponse.error. */
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
	oa_init();
	genproto_annotations_init();
	/* protogen.Options.Run: proto.Unmarshal of the request (protobuf-go semantics) and
	 * Options.New. The parameter is the one without the uni-* options. */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		/* a request upstream accepts and the port does not support */
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (req && (perr = check_extensions(req)) && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (!req || perr)
		return xcat(PROGRAM ": ", perr); /* unparsable: stderr and exit status 1, as upstream */
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));
	if ((perr = pg_unsupported(req))) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}

	Flags fl = {0};
	fl.conf.Version = STR_LIT("0.0.1");
	fl.conf.Title = (Str){"", 0};
	fl.conf.Description = (Str){"", 0};
	fl.conf.Naming = STR_LIT("json");
	fl.conf.FQSchemaNaming = false;
	fl.conf.EnumType = STR_LIT("integer");
	fl.conf.CircularDepth = 2;
	fl.conf.DefaultResponse = true;
	fl.conf.OutputMode = STR_LIT("merged");
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	opts.ParamCtx = &fl;
	opts.LinkedExtensions = linked_extensions();
	opts.Version = &pg_v1_36_7;
	Str err;
	PgPlugin *plugin = pg_options_new(&opts, req, &err);
	if (!plugin) {
		error_response(out, err);
		return NULL;
	}
	RunCtx c = {plugin, &fl, out};
	const char *ferr = try_run(run, &c);
	if (ferr) {
		Str msg = fail_message();
		out->n = 0;
		if (str_has_prefix(msg, STR_LIT("unsupported by protoc-gen-uni")))
			pb_put_bytes(out, CGRESP_ERROR, msg); /* not an upstream error */
		else
			error_response(out, msg);
	}
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the buf.build plugin binary: true ("proto:" with a no-break
	 * space in errors). */
	go_detrand = true;
	const char *err = generate1(uni_req, raw, out);
	go_detrand = false;
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics (10000 levels) and takes only the
 * parameter from uni_req. */
const UniGenerator gen_community_google_gnostic_openapi_v0_7_1 = {
	.name = "community/google-gnostic-openapi",
	.version = "v0.7.1",
	.generate = generate,
	.parses_raw = true,
};
