// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see the license in
// template.c and LICENSE of github.com/grpc-ecosystem/grpc-gateway).

/* grpc-ecosystem/gateway v2.33.0: a port of protoc-gen-grpc-gateway v2.33.0
 * (buf.build/grpc-ecosystem/gateway), the reverse-proxy generator of grpc-gateway
 * (https://github.com/grpc-ecosystem/grpc-gateway/tree/v2.33.0/protoc-gen-grpc-gateway) with
 * bufbuild/plugins' separate_pkg_additional_imports.patch: main.go (below),
 * internal/gengateway/generator.go (gengateway.c) and template.go (template.c). The shared packages
 * (internal/descriptor, httprule, casing, utilities, codegenerator) are
 * pkg/languages/go/grpc-gateway; the plugin framework is compiler/protogen of protobuf-go v1.36.12
 * (pkg/languages/go/protogen); built with Go 1.27.3, which formats like the Go 1.27.1 port of
 * pkg/languages/go/std (the default release; go/format.Source is go_std_format_source).
 *
 * Unity build: this file includes gengateway.c, and everything but the generator below is
 * static. */
#include "annotations.h"
#include "go_std.h"
#include "grpcgateway.h"
#include "protogen.h"
#include "uni.h"

#include <stdarg.h>
#include <string.h>

#define PROGRAM "protoc-gen-grpc-gateway" /* filepath.Base(os.Args[0]) of the buf.build image */

#include "gengateway.c"

/* The flags of main.go (flag.CommandLine), set from the plugin parameters by protogen's
 * ParamFunc (flag.CommandLine.Set). */
typedef struct {
	Str registerFuncSuffix;
	bool useRequestContext;
	bool allowDeleteBody;
	Str grpcAPIConfiguration;
	bool allowRepeatedFieldsInBody; /* deprecated: ignored */
	Str repeatedPathParamSeparator;
	bool allowPatchFeature;
	bool omitPackageDoc;
	bool strictDeepWildcard;
	bool standalone;
	bool versionFlag; /* main reads it before the parameters are set: no effect */
	bool warnOnUnboundMethods;
	bool generateUnboundMethods;
	bool useOpaqueAPI;
	bool separatePackage;
	bool logtostderr; /* no-op */
} Flags;

static COLD Str set_flag(void *ctx, Str name, Str value)
{
	Flags *f = ctx;
	static const struct {
		const char *name;
		size_t off;
		bool isBool;
	} flags[] = {
		{"register_func_suffix", offsetof(Flags, registerFuncSuffix), false},
		{"request_context", offsetof(Flags, useRequestContext), true},
		{"allow_delete_body", offsetof(Flags, allowDeleteBody), true},
		{"grpc_api_configuration", offsetof(Flags, grpcAPIConfiguration), false},
		{"allow_repeated_fields_in_body", offsetof(Flags, allowRepeatedFieldsInBody), true},
		{"repeated_path_param_separator", offsetof(Flags, repeatedPathParamSeparator), false},
		{"allow_patch_feature", offsetof(Flags, allowPatchFeature), true},
		{"omit_package_doc", offsetof(Flags, omitPackageDoc), true},
		{"strict_deep_wildcard", offsetof(Flags, strictDeepWildcard), true},
		{"standalone", offsetof(Flags, standalone), true},
		{"version", offsetof(Flags, versionFlag), true},
		{"warn_on_unbound_methods", offsetof(Flags, warnOnUnboundMethods), true},
		{"generate_unbound_methods", offsetof(Flags, generateUnboundMethods), true},
		{"use_opaque_api", offsetof(Flags, useOpaqueAPI), true},
		{"separate_package", offsetof(Flags, separatePackage), true},
		{"logtostderr", offsetof(Flags, logtostderr), true},
	};
	for (size_t i = 0; i < sizeof flags / sizeof *flags; i++) {
		if (!str_eqc(name, flags[i].name))
			continue;
		void *p = (char *)f + flags[i].off;
		if (!flags[i].isBool) {
			*(Str *)p = value;
			return (Str){NULL, 0};
		}
		/* boolValue.Set: strconv.ParseBool; the value is stored also on error */
		bool v;
		int err = go_std_parse_bool(value.p, value.n, &v);
		*(bool *)p = err ? false : v;
		return err ? STR_LIT("parse error") : (Str){NULL, 0};
	}
	Buf b = {0};
	buf_adds(&b, "no such flag -");
	buf_add_str(&b, name);
	return buf_view(&b);
}

/* LoadGrpcAPIServiceFromYAML: os.ReadFile of the configuration file. The buf.build plugin runs
 * in an image with nothing but /etc/passwd and the plugin binary, where the file does not exist
 * ("open <path>: no such file or directory"); the port gives that error, and an "unsupported"
 * error for the paths that exist in such a container (the root, its directories and files,
 * pseudo file systems), which the port does not read. */
static COLD Str loadGrpcAPIServiceFromYAML(Str yamlFile)
{
	const char *q = go_std_quote(yamlFile.p, yamlFile.n, NULL);
	if (memchr(yamlFile.p, 0, yamlFile.n)) {
		Buf b = {0};
		buf_fmt(&b, "failed to read gRPC API Configuration description from %s: open ", q);
		buf_add_str(&b, yamlFile);
		buf_adds(&b, ": invalid argument");
		return buf_view(&b);
	}
	/* the working directory of the plugin process is the root of the image */
	const char *path = pg_path_clean(xcat("/", xstrndup(yamlFile.p, yamlFile.n)));
	static const char *const existing[] = {"etc", "proc", "dev", "sys", PROGRAM};
	bool exists = strcmp(path, "/") == 0;
	for (size_t i = 0; i < sizeof existing / sizeof *existing && !exists; i++) {
		size_t n = strlen(existing[i]);
		exists = strncmp(path + 1, existing[i], n) == 0 && (path[1 + n] == 0 || path[1 + n] == '/');
	}
	if (exists)
		return str_c(xcat("unsupported by protoc-gen-uni: grpc_api_configuration=", q,
				  " (the port does not read files; the buf.build plugin reads it from its container)"));
	Buf b = {0};
	buf_fmt(&b, "failed to read gRPC API Configuration description from %s: open ", q);
	buf_add_str(&b, yamlFile);
	buf_adds(&b, ": no such file or directory");
	return buf_view(&b);
}

/* applyFlags */
static COLD Str applyFlags(GwRegistry *reg, const Flags *f)
{
	if (f->grpcAPIConfiguration.n) {
		Str err = loadGrpcAPIServiceFromYAML(f->grpcAPIConfiguration);
		if (err.p)
			return err;
	}
	/* warn_on_unbound_methods with generate_unbound_methods, allow_repeated_fields_in_body: a
	 * warning on stderr only */
	reg->standalone = f->standalone;
	reg->allowDeleteBody = f->allowDeleteBody;
	reg->separatePackage = f->separatePackage;
	reg->omitPackageDoc = f->omitPackageDoc;
	reg->strictDeepWildcard = f->strictDeepWildcard;
	reg->warnOnUnboundMethods = f->warnOnUnboundMethods;
	reg->generateUnboundMethods = f->generateUnboundMethods;
	return gw_Registry_SetRepeatedPathParamSeparator(reg, f->repeatedPathParamSeparator);
}

typedef struct {
	PgPlugin *gen;
	Flags *flags;
	Buf *out;
} RunCtx;

/* The function main passes to protogen.Options.Run. Errors fail(). */
static void run_func(PgPlugin *gen, Flags *f)
{
	GwRegistry *reg = gw_NewRegistry();
	Str err = applyFlags(reg, f);
	if (err.p)
		fail_str(err);
	if (f->separatePackage && !f->standalone)
		fail("option separate_package=true must be specified with standalone=true");
	generator *g = gengateway_New(reg, f->useRequestContext, f->registerFuncSuffix, f->allowPatchFeature, f->standalone,
				      f->useOpaqueAPI, f->separatePackage);
	gw_codegenerator_SetSupportedFeaturesOnPluginGen(gen);

	err = gw_Registry_LoadFromPlugin(reg, gen);
	if (err.p)
		fail_str(err);

	/* UnboundExternalHTTPRules: none (no grpc_api_configuration) */

	int ntargets = pb_count(gen->Request, CGR_FILE_TO_GENERATE);
	GwFile **targets = xalloc(sizeof *targets * (size_t)(ntargets + 1));
	for (int i = 0; i < ntargets; i++) {
		const char *lerr = NULL;
		targets[i] = gw_Registry_LookupFile(reg, pb_str_at(gen->Request, CGR_FILE_TO_GENERATE, i), &lerr);
		if (!targets[i])
			fail("%s", lerr);
	}

	int nfiles;
	ResponseFile *files = gengateway_Generate(g, targets, ntargets, &nfiles);
	for (int i = 0; i < nfiles; i++) {
		PgGeneratedFile *genFile = pg_new_generated_file(gen, files[i].Name, files[i].GoPkgPath);
		pg_write(genFile, str_c(files[i].Content));
	}
}

static void run(void *arg)
{
	RunCtx *c = arg;
	run_func(c->gen, c->flags);
}

/* ---- the linked extensions in the request ----
 *
 * proto.Unmarshal of the request decodes the extensions linked into the plugin (eagerly), so
 * malformed values make the request unparsable. pkg/languages/go/protogen keeps them as unknown
 * fields; they are checked here: google.api.http with its schema, the string ones for UTF-8
 * (proto3), and the other message extensions (google.api and the openapiv2 options) for wire
 * format only. */

static const char *const *linked_extensions(void)
{
	static const char *names[24];
	static const char *const openapiv2[] = {
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_swagger",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_operation",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_schema",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_enum",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_tag",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_field",
	};
	int n = 0;
	for (const char *const *p = genproto_annotations_extensions; *p; p++)
		names[n++] = *p;
	for (int i = 0; i < 6; i++)
		names[n++] = openapiv2[i];
	names[n] = NULL;
	return names;
}

enum { EXT_MESSAGE, EXT_STRING, EXT_WIRE, EXT_ENUMS };
typedef struct {
	uint32_t num;
	const char *name;
	int kind;
} LinkedExt;

static const char *check_wire(const uint8_t *p, size_t n)
{
	PbReader r = {p, p + n};
	PbRawField f;
	while (r.p < r.end)
		if (!pb_next_field(&r, &f))
			return xcat(go_err_prefix(), "cannot parse invalid wire-format data");
	return NULL;
}

static const char *check_options(const PbMsg *opts, const LinkedExt *exts, int n)
{
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		for (int i = 0; i < n; i++) {
			if (exts[i].num != f.num)
				continue;
			const char *err = NULL;
			if (exts[i].kind == EXT_ENUMS) {
				if (f.wt == WT_LEN) {
					PbReader pr = {f.val, f.val + f.vlen};
					uint64_t x;
					while (pr.p < pr.end)
						if (!pb_read_varint(&pr, &x))
							return xcat(go_err_prefix(), "cannot parse invalid wire-format data");
				}
				continue;
			}
			if (f.wt != WT_LEN)
				continue;
			if (exts[i].kind == EXT_MESSAGE) {
				PbMsg *m = pb_new(&genproto_http_rule()->pb);
				err = gopb_decode(m, f.val, f.vlen);
			} else if (exts[i].kind == EXT_WIRE) {
				err = check_wire(f.val, f.vlen);
			} else {
				Str s = {(const char *)f.val, f.vlen};
				for (size_t k = 0; k < s.n;) {
					uint32_t rune;
					int w = utf8_decode(s.p + k, s.n - k, &rune);
					if (rune == 0xFFFD && w == 1) {
						err = xcat(go_err_prefix(), "field ", exts[i].name, " contains invalid UTF-8");
						break;
					}
					k += (size_t)w;
				}
			}
			if (err)
				return err;
		}
	}
	return NULL;
}

static const char *check_field_options(const PbMsg *fd)
{
	static const LinkedExt exts[] = {
		{GENPROTO_E_FIELD_BEHAVIOR, "google.api.field_behavior", EXT_ENUMS},
		{GENPROTO_E_FIELD_INFO, "google.api.field_info", EXT_WIRE},
		{GENPROTO_E_RESOURCE_REFERENCE, "google.api.resource_reference", EXT_WIRE},
		{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_field", EXT_WIRE},
	};
	return check_options(pb_msg(fd, FD_OPTIONS), exts, 4);
}

static const char *check_enum(const PbMsg *e)
{
	static const LinkedExt exts[] = {{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_enum", EXT_WIRE}};
	return check_options(pb_msg(e, EDP_OPTIONS), exts, 1);
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
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		if ((err = check_enum(pb_msg_at(m, DP_ENUM_TYPE, i))))
			return err;
	for (int i = 0; i < pb_count(m, DP_EXTENSION); i++)
		if ((err = check_field_options(pb_msg_at(m, DP_EXTENSION, i))))
			return err;
	static const LinkedExt exts[] = {
		{GENPROTO_E_RESOURCE, "google.api.resource", EXT_WIRE},
		{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_schema", EXT_WIRE},
	};
	return check_options(pb_msg(m, DP_OPTIONS), exts, 2);
}

static const char *check_extensions(const PbMsg *req)
{
	const char *err;
	static const LinkedExt method_exts[] = {
		{GENPROTO_E_HTTP, "google.api.http", EXT_MESSAGE},
		{GENPROTO_E_ROUTING, "google.api.routing", EXT_WIRE},
		{GENPROTO_E_METHOD_SIGNATURE, "google.api.method_signature", EXT_STRING},
		{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_operation", EXT_WIRE},
	};
	static const LinkedExt service_exts[] = {
		{GENPROTO_E_DEFAULT_HOST, "google.api.default_host", EXT_STRING},
		{GENPROTO_E_OAUTH_SCOPES, "google.api.oauth_scopes", EXT_STRING},
		{GENPROTO_E_API_VERSION, "google.api.api_version", EXT_STRING},
		{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_tag", EXT_WIRE},
	};
	static const LinkedExt file_exts[] = {
		{GENPROTO_E_RESOURCE_DEFINITION, "google.api.resource_definition", EXT_WIRE},
		{1042, "grpc.gateway.protoc_gen_openapiv2.options.openapiv2_swagger", EXT_WIRE},
	};
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *f = pb_msg_at(req, CGR_PROTO_FILE, i);
		for (int k = 0; k < pb_count(f, FDP_MESSAGE_TYPE); k++)
			if ((err = check_message(pb_msg_at(f, FDP_MESSAGE_TYPE, k), 0)))
				return err;
		for (int k = 0; k < pb_count(f, FDP_ENUM_TYPE); k++)
			if ((err = check_enum(pb_msg_at(f, FDP_ENUM_TYPE, k))))
				return err;
		for (int k = 0; k < pb_count(f, FDP_SERVICE); k++) {
			const PbMsg *s = pb_msg_at(f, FDP_SERVICE, k);
			for (int j = 0; j < pb_count(s, SDP_METHOD); j++)
				if ((err = check_options(pb_msg(pb_msg_at(s, SDP_METHOD, j), MDP_OPTIONS), method_exts, 4)))
					return err;
			if ((err = check_options(pb_msg(s, 3), service_exts, 4)))
				return err;
		}
		for (int k = 0; k < pb_count(f, FDP_EXTENSION); k++)
			if ((err = check_field_options(pb_msg_at(f, FDP_EXTENSION, k))))
				return err;
		if ((err = check_options(pb_msg(f, FDP_OPTIONS), file_exts, 2)))
			return err;
	}
	return NULL;
}

/* An error of Options.New: upstream prints "protoc-gen-grpc-gateway: <err>" to stderr and exits
 * with status 1; the port reports the same text in the CodeGeneratorResponse.error. */
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
	genproto_annotations_init();
	fieldNamesCache = (Map){0};
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

	Flags f = {0};
	f.registerFuncSuffix = STR_LIT("Handler");
	f.useRequestContext = true;
	f.allowRepeatedFieldsInBody = true;
	f.repeatedPathParamSeparator = STR_LIT("csv");
	f.allowPatchFeature = true;
	PgOptions opts = {0};
	opts.ParamFunc = set_flag;
	opts.ParamCtx = &f;
	opts.LinkedExtensions = linked_extensions();
	Str err;
	PgPlugin *gen = pg_options_new(&opts, req, &err);
	if (!gen) {
		error_response(out, err);
		return NULL;
	}
	RunCtx c = {gen, &f, out};
	if (try_run(run, &c)) {
		/* Errors from the plugin function are reported in the CodeGeneratorResponse. */
		Str msg = fail_message();
		pg_plugin_error(gen, msg);
	}
	pg_plugin_response(gen, out);
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the plugin binary: true ("proto:" with a no-break space in
	 * errors), computed from the binary built like the buf.build image (Dockerfile of
	 * bufbuild/plugins: Go 1.27.3, -trimpath -ldflags="-s -w", linux/amd64). */
	go_detrand = true;
	const char *err = generate1(uni_req, raw, out);
	go_detrand = false;
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics (10000 levels) and takes only the
 * parameter from uni_req. */
const UniGenerator gen_grpc_ecosystem_gateway_v2_33_0 = {
	.name = "grpc-ecosystem/gateway",
	.version = "v2.33.0",
	.generate = generate,
	.parses_raw = true,
};
