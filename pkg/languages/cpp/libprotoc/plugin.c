// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/plugin.cc (PluginMain, GenerateCode,
 * GeneratorResponseContext) and src/google/protobuf/compiler/code_generator.cc
 * (CodeGenerator::GenerateAll, BuildFeatureSetDefaults, ParseGeneratorParameter). */

enum {
	CGRESP_FILE_INSERTION_POINT = 2, CGR_COMPILER_VERSION = 3,
	PROTOC_MINIMUM_EDITION = LP_EDITION_PROTO2, /* ProtocMinimumEdition() */
	MAXIMUM_KNOWN_EDITION = LP_EDITION_2026,    /* MaximumKnownEdition() of 36.2 */
};

typedef struct {
	const char *name;
	const char *insertion_point; /* NULL: Open */
	Buf content;
} LpOutputFile;

struct LpContext {
	Vec files;                     /* LpOutputFile* in Open order */
	const PbMsg *compiler_version; /* CodeGeneratorRequest.compiler_version, NULL if absent */
};

Buf *lp_context_open(LpContext *ctx, const char *filename)
{
	LpOutputFile *f = xalloc(sizeof *f);
	f->name = filename;
	vec_push(&ctx->files, f);
	return &f->content;
}

Buf *lp_context_open_for_insert(LpContext *ctx, const char *filename, const char *insertion_point)
{
	Buf *content = lp_context_open(ctx, filename);
	VEC_AT(ctx->files, LpOutputFile, ctx->files.n - 1)->insertion_point = insertion_point;
	return content;
}

LpCompilerVersion lp_context_compiler_version(const LpContext *ctx)
{
	const PbMsg *v = ctx->compiler_version;
	return (LpCompilerVersion){(int32_t)pb_int(v, 1, 0), (int32_t)pb_int(v, 2, 0), (int32_t)pb_int(v, 3, 0),
				   pb_bytes(v, 4)};
}

int lp_parse_generator_parameter(const char *text, char ***keys, char ***values)
{
	Vec ks = {0}, vs = {0};
	const char *p = text;
	while (*p) {
		const char *e = strchr(p, ',');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		if (n) {
			const char *eq = memchr(p, '=', n);
			if (eq) {
				vec_push(&ks, xstrndup(p, (size_t)(eq - p)));
				vec_push(&vs, xstrndup(eq + 1, n - (size_t)(eq - p) - 1));
			} else {
				vec_push(&ks, xstrndup(p, n));
				vec_push(&vs, xstrdup(""));
			}
		}
		if (!e)
			break;
		p = e + 1;
	}
	*keys = (char **)ks.v;
	*values = (char **)vs.v;
	return ks.n;
}

char *lp_strip_proto(const char *filename)
{
	if (has_suffix(filename, ".protodevel"))
		return str_trim_suffix(filename, ".protodevel");
	return str_trim_suffix(filename, ".proto");
}

bool lp_is_known_feature_proto(const char *filename)
{
	return str_eq(filename, "google/protobuf/cpp_features.proto") ||
	       str_eq(filename, "google/protobuf/java_features.proto");
}

/* CodeGenerator::GenerateAll (the default implementation) */
static bool default_generate_all(const LpGenerator *g, LpFile *const *files, int n, const char *parameter,
				 LpContext *ctx, Buf *error)
{
	bool succeeded = true;
	for (int i = 0; i < n; i++) {
		succeeded = g->generate(g, files[i], parameter, ctx, error);
		if (!succeeded && !error->n)
			buf_adds(error, "Code generator returned false but provided no error description.");
		if (error->n) {
			Buf e = {0};
			buf_fmt(&e, "%s: ", files[i]->name);
			buf_add(&e, error->p, error->n);
			*error = e;
			break;
		}
		if (!succeeded)
			break;
	}
	return succeeded;
}

static void write_error_response(LpPool *builtin, Str error, Buf *out)
{
	PbMsg *resp = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.compiler.CodeGeneratorResponse")->pb);
	proto_msg_set_s(resp, CGRESP_ERROR, error);
	lp_msg_serialize(resp, out);
}

typedef struct {
	Str raw, parameter;
	const LpGenerator *gen;
	Buf *out;
	const char *result;
} PluginMainArgs;

static const char *plugin_main(Str raw, Str parameter, const LpGenerator *gen, Buf *out)
{
	LpPool *builtin = lp_builtin_pool(gen->version);
	PbMsg *request = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.compiler.CodeGeneratorRequest")->pb);
	if (!lp_decode(request, (const uint8_t *)raw.p, raw.n) || !lp_is_initialized(request))
		return xcat(gen->plugin_name, ": protoc sent unparseable request to plugin.");

	/* GenerateCode */
	const char *err;
	int min_edition = PROTOC_MINIMUM_EDITION,
	    max_edition = gen->version ? gen->version->maximum_known_edition : MAXIMUM_KNOWN_EDITION;
	if (gen->version && gen->version->generator_edition_defaults && (gen->supported_features & 2)) {
		min_edition = gen->minimum_edition; /* FEATURE_SUPPORTS_EDITIONS */
		max_edition = gen->maximum_edition;
	}
	PbMsg *defaults = lp_compile_defaults(builtin, gen->feature_extensions, min_edition, max_edition, &err);
	if (!defaults)
		return xcat(gen->plugin_name, ": error generating feature defaults: ", err);
	LpPool *pool = lp_pool_new(builtin, defaults, &lp_msgdef_by_name(builtin, "google.protobuf.FeatureSet")->pb);
	for (int i = 0; i < pb_count(request, CGR_PROTO_FILE); i++) {
		Buf log = {0};
		if (!lp_build_file(pool, pb_msg_at(request, CGR_PROTO_FILE, i), &log)) {
			/* upstream logs the errors and exits with status 1 */
			if (!log.n)
				return "";
			write_error_response(builtin, (Str){log.p, log.n - 1}, out);
			return NULL;
		}
	}

	int n = pb_count(request, CGR_FILE_TO_GENERATE);
	LpFile **parsed_files = xalloc(sizeof(LpFile *) * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		parsed_files[i] = lp_find_file(pool, pb_str_at(request, CGR_FILE_TO_GENERATE, i));
		if (!parsed_files[i]) {
			/* upstream prints "<argv0>: <error>" and exits with status 1 */
			write_error_response(builtin,
					     str_c(xcat(gen->plugin_name,
							": protoc asked plugin to generate a file but did not provide a "
							"descriptor for the file: ",
							pb_str_at(request, CGR_FILE_TO_GENERATE, i))),
					     out);
			return NULL;
		}
	}

	LpContext ctx = {0};
	ctx.compiler_version = pb_msg(request, CGR_COMPILER_VERSION);
	Buf error = {0};
	char *parameter_s = xstrndup(parameter.p ? parameter.p : "", parameter.n);
	bool succeeded = gen->generate_all ? gen->generate_all(gen, parsed_files, n, parameter_s, &ctx, &error)
					   : default_generate_all(gen, parsed_files, n, parameter_s, &ctx, &error);

	PbMsg *resp = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.compiler.CodeGeneratorResponse")->pb);
	if (!succeeded || error.n)
		ctx.files.n = 0; /* upstream also lists the files opened before the error */
	for (int i = 0; i < ctx.files.n; i++) {
		LpOutputFile *f = VEC_AT(ctx.files, LpOutputFile, i);
		PbMsg *fm = proto_msg_add(resp, CGRESP_FILE);
		proto_msg_set_cs(fm, CGRESP_FILE_NAME, f->name);
		if (f->insertion_point)
			proto_msg_set_cs(fm, CGRESP_FILE_INSERTION_POINT, f->insertion_point);
		proto_msg_set_s(fm, CGRESP_FILE_CONTENT, (Str){f->content.p ? f->content.p : "", f->content.n});
	}
	proto_msg_set_u(resp, CGRESP_SUPPORTED_FEATURES, gen->supported_features);
	if (proto_msg_field_index(resp->def, CGRESP_MINIMUM_EDITION) >= 0) { /* not before protobuf 26 */
		proto_msg_set_u(resp, CGRESP_MINIMUM_EDITION, (uint64_t)(int64_t)gen->minimum_edition);
		proto_msg_set_u(resp, CGRESP_MAXIMUM_EDITION, (uint64_t)(int64_t)gen->maximum_edition);
	}
	if (!succeeded && !error.n)
		buf_adds(&error, "Code generator returned false but provided no error description.");
	if (error.n)
		proto_msg_set_s(resp, CGRESP_ERROR, buf_view(&error));
	lp_msg_serialize(resp, out);
	return NULL;
}

static void plugin_main_run(void *arg)
{
	PluginMainArgs *a = arg;
	a->result = plugin_main(a->raw, a->parameter, a->gen, a->out);
}

const char *lp_plugin_main(Str raw, Str parameter, const LpGenerator *gen, Buf *out)
{
	PluginMainArgs a = {raw, parameter, gen, out, NULL};
	if (try_run(plugin_main_run, &a)) {
		/* an internal error (upstream: a failed check) */
		out->n = 0;
		Buf msg = {0};
		buf_fmt(&msg, "%s: ", gen->plugin_name);
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	return a.result;
}
