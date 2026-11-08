// Copyright 2022-2026 The Connect Authors
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

/* connectrpc/kotlin v0.10.0: protoc-gen-connect-kotlin of connect-kotlin v0.10.0
 * (https://github.com/connectrpc/connect-kotlin/tree/v0.10.0/protoc-gen-connect-kotlin), the
 * buf.build plugin connectrpc/kotlin. This file ports src/main/kotlin/com/connectrpc/protocgen/
 * connect/Generator.kt; the plugin framework is plugin.c, the options and comments parameters.c.
 * Kotlin is built with KotlinPoet 2.4.0 (pkg/languages/kotlin/formatters/kotlinpoet), descriptors
 * and Java names with protobuf-java 4.36.2 (pkg/languages/java/protobuf-java). */
#include "uni.h"

#include "plugin.c"
#include "parameters.c"

#define CONNECT "com.connectrpc"

static Configuration configuration;

#define ARGS(...) (int)(sizeof((KpArg[]){__VA_ARGS__}) / sizeof(KpArg)), ((KpArg[]){__VA_ARGS__})
#define TYPES(...) (int)(sizeof((KpType *[]){__VA_ARGS__}) / sizeof(KpType *)), ((KpType *[]){__VA_ARGS__})

static KpType *connect_class(const char *name) { return kp_class(CONNECT, name); }

/* sanitizeKdoc() */
static const char *sanitize_kdoc(const char *s)
{
	/* "[^\\S\n]+\n" -> "\n": runs of [ \t\x0B\f\r] before a newline */
	Buf b = {0};
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		size_t j = i;
		while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\x0b' || s[j] == '\f' || s[j] == '\r'))
			j++;
		if (j > i && j < n && s[j] == '\n') {
			i = j;
			continue;
		}
		if (j > i) {
			buf_add(&b, s + i, j - i);
			i = j;
			continue;
		}
		buf_addc(&b, s[i++]);
	}
	/* "\\s+$" -> "": the trailing whitespace, or the whitespace before a final line terminator
	 * that is not \s (U+0085, U+2028, U+2029) */
	const char *t = buf_str(&b);
	size_t len = strlen(t), end = len, term = 0;
	if (len >= 2 && (unsigned char)t[len - 2] == 0xc2 && (unsigned char)t[len - 1] == 0x85)
		term = 2;
	else if (len >= 3 && (unsigned char)t[len - 3] == 0xe2 && (unsigned char)t[len - 2] == 0x80 &&
		 ((unsigned char)t[len - 1] == 0xa8 || (unsigned char)t[len - 1] == 0xa9))
		term = 3;
	end = len - term;
	size_t start = end;
	while (start > 0 && strchr(" \t\n\x0b\f\r", t[start - 1]) && t[start - 1])
		start--;
	const char *r = start < end ? xcat(xstrndup(t, start), t + end) : t;
	r = str_replace_all(r, "*/", "&#42;/");
	r = str_replace_all(r, "/*", "/&#42;");
	r = str_replace_all(r, "[", "&#91;");
	r = str_replace_all(r, "]", "&#93;");
	r = str_replace_all(r, "@", "&#64;");
	return str_replace_all(r, "%", "%%");
}

static const char *lower_camel_case(const char *s)
{
	char *r = xstrdup(s);
	if (r[0] >= 'A' && r[0] <= 'Z')
		r[0] = (char)(r[0] - 'A' + 'a');
	return r;
}

/* classNameFromType */
static KpType *class_name_from_type(const JMsg *m)
{
	const char *package = jnames_file_java_package(m->file);
	const char *q = jnames_qualified_class_name(m);
	if (has_prefix(q, package))
		q += strlen(package);
	if (q[0] == '.')
		q++;
	Vec names = str_split(q, '.');
	return kp_class_name(package, names.n, (const char *const *)names.v);
}

static bool method_deprecated(const JMethod *m) { return jpb_bool(pb_msg(m->proto, MDP_OPTIONS), 33, false); }
static bool service_deprecated(const JService *s) { return jpb_bool(pb_msg(s->proto, 3), 33, false); }
static bool file_deprecated(const JFile *f) { return jpb_bool(pb_msg(f->proto, FDP_OPTIONS), 23, false); }

static KpAnnotation *deprecated(const char *message)
{
	KpAnnotation *a = kp_annotation(kp_class("kotlin", "Deprecated"));
	kp_annotation_add_member(a, "%S", ARGS(kp_arg_str(message)));
	return a;
}

/* addServiceDeprecation */
static void add_service_deprecation(KpTypeSpec *t, const JService *s, const JFile *f)
{
	if (service_deprecated(s))
		vec_push(&t->annotations, deprecated("The service is deprecated in the Protobuf source file."));
	else if (file_deprecated(f))
		vec_push(&t->annotations, deprecated("The Protobuf source file that defines this service is deprecated."));
}

/* addMethodDeprecation */
static void add_method_deprecation(KpFun *f, const JMethod *m)
{
	if (method_deprecated(m))
		vec_push(&f->annotations, deprecated("The method is deprecated in the Protobuf source file."));
}

static KpFun *method_fun(const char *name, const char *kdoc, const JMethod *deprecation, KpModifiers modifiers)
{
	KpFun *f = kp_fun(name);
	kp_fun_add_kdoc(f, kdoc);
	if (deprecation)
		add_method_deprecation(f, deprecation);
	f->modifiers = modifiers;
	return f;
}

static KpType *response_message(KpType *out) { return kp_parameterized(connect_class("ResponseMessage"), TYPES(out)); }

static KpType *callback_type(KpType *out)
{
	return kp_lambda(1, (KpParam *[]){kp_param("", response_message(out))}, kp_unit());
}

static KpType *stream_type(const JMethod *m, KpType *in, KpType *out)
{
	bool cs = jpb_bool(m->proto, MDP_CLIENT_STREAMING, false), ss = jpb_bool(m->proto, MDP_SERVER_STREAMING, false);
	const char *name = cs && ss ? "BidirectionalStreamInterface" : ss ? "ServerOnlyStreamInterface" : "ClientOnlyStreamInterface";
	return kp_parameterized(connect_class(name), TYPES(in, out));
}

static void push_fun(Vec *funs, KpFun *f)
{
	kp_fun_build(f);
	vec_push(funs, f);
}

/* interfaceMethods */
static Vec interface_methods(const JService *s, const SourceCodeHelper *h)
{
	Vec funs = {0};
	KpType *headers = connect_class("Headers");
	KpParam *header_param = kp_param("headers", headers);
	header_param->default_value = kp_code_of("%L", ARGS(kp_arg_s("emptyMap()")));
	for (int i = 0; i < s->methods.n; i++) {
		JMethod *m = VEC_AT(s->methods, JMethod, i);
		const char *comment = source_comment(h, xfmt("%d.%d", s->index, i));
		KpType *in = class_name_from_type(m->input), *out = class_name_from_type(m->output);
		bool cs = jpb_bool(m->proto, MDP_CLIENT_STREAMING, false), ss = jpb_bool(m->proto, MDP_SERVER_STREAMING, false);
		const char *name = lower_camel_case(m->name);
		if (cs || ss) {
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_ABSTRACT) | KM(KM_SUSPEND));
			vec_push(&f->params, header_param);
			f->ret = stream_type(m, in, out);
			push_fun(&funs, f);
			continue;
		}
		if (configuration.generate_coroutine_methods) {
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_ABSTRACT) | KM(KM_SUSPEND));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, header_param);
			f->ret = response_message(out);
			push_fun(&funs, f);
		}
		if (configuration.generate_callback_methods) {
			KpType *cb = callback_type(out);
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_ABSTRACT));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, header_param);
			vec_push(&f->params, kp_param("onResult", cb));
			f->ret = kp_class(CONNECT ".http", "Cancelable");
			push_fun(&funs, f);
		}
		if (configuration.generate_blocking_unary_methods) {
			KpFun *f = method_fun(xcat(name, "Blocking"), sanitize_kdoc(comment), NULL, KM(KM_ABSTRACT));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, header_param);
			f->ret = kp_parameterized(connect_class("UnaryBlockingCall"), TYPES(out));
			push_fun(&funs, f);
		}
	}
	return funs;
}

/* serviceClientInterface */
static KpTypeSpec *service_client_interface(const char *package, const JService *s, const JFile *f, const SourceCodeHelper *h)
{
	KpTypeSpec *t = kp_interface_builder(xcat(s->name, "ClientInterface"));
	(void)package;
	Vec funs = interface_methods(s, h);
	add_service_deprecation(t, s, f);
	kp_type_add_kdoc(t, sanitize_kdoc(source_comment(h, xfmt("%d", s->index))));
	t->funs = funs;
	kp_type_build(t);
	return t;
}

/* the CodeBlock of a client call: name(, the leading lines, the MethodSpec block, the trailing
 * lines, ) */
static KpCode *client_call(const char *call, bool request, KpCode *method_spec, bool on_result)
{
	KpCode *b = kp_code();
	kp_add_statement0(b, xcat(call, "("));
	kp_indent(b);
	if (request)
		kp_add_statement0(b, "request,");
	kp_add_statement0(b, "headers,");
	kp_add_code(b, method_spec);
	if (on_result)
		kp_add_statement0(b, "onResult");
	kp_unindent(b);
	kp_add_statement0(b, ")");
	return b;
}

static void add_return(KpFun *f, KpCode *block) { kp_add_statement(f->body, "return %L", ARGS(kp_arg_code(block))); }

/* implementationMethods */
static Vec implementation_methods(const JService *s, const SourceCodeHelper *h)
{
	Vec funs = {0};
	KpType *headers = connect_class("Headers");
	for (int i = 0; i < s->methods.n; i++) {
		JMethod *m = VEC_AT(s->methods, JMethod, i);
		const char *comment = source_comment(h, xfmt("%d.%d", s->index, i));
		KpType *in = class_name_from_type(m->input), *out = class_name_from_type(m->output);
		bool cs = jpb_bool(m->proto, MDP_CLIENT_STREAMING, false), ss = jpb_bool(m->proto, MDP_SERVER_STREAMING, false);
		KpCode *spec = kp_code();
		kp_add_statement0(spec, "MethodSpec(");
		kp_add_statement0(spec, xcat("\"", s->full_name, "/", m->name, "\","));
		kp_indent(spec);
		kp_add_statement0(spec, xcat(kp_type_to_string(in), "::class,"));
		kp_add_statement0(spec, xcat(kp_type_to_string(out), "::class,"));
		kp_add_statement0(spec, cs && ss ? "StreamType.BIDI," : cs ? "StreamType.CLIENT," : ss ? "StreamType.SERVER," : "StreamType.UNARY,");
		switch (pb_int(pb_msg(m->proto, MDP_OPTIONS), 34, 0)) {
		case 1: kp_add_statement0(spec, "idempotency = Idempotency.NO_SIDE_EFFECTS,"); break;
		case 2: kp_add_statement0(spec, "idempotency = Idempotency.IDEMPOTENT,"); break;
		}
		kp_unindent(spec);
		kp_add_statement0(spec, "),");
		const char *name = lower_camel_case(m->name);
		if (cs || ss) {
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_OVERRIDE) | KM(KM_SUSPEND));
			vec_push(&f->params, kp_param("headers", headers));
			f->ret = stream_type(m, in, out);
			add_return(f, client_call(cs && ss ? "client.stream" : ss ? "client.serverStream" : "client.clientStream", false, spec,
						  false));
			push_fun(&funs, f);
			continue;
		}
		if (configuration.generate_coroutine_methods) {
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_SUSPEND) | KM(KM_OVERRIDE));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, kp_param("headers", headers));
			f->ret = response_message(out);
			add_return(f, client_call("client.unary", true, spec, false));
			push_fun(&funs, f);
		}
		if (configuration.generate_callback_methods) {
			KpType *cb = callback_type(out);
			KpFun *f = method_fun(name, sanitize_kdoc(comment), m, KM(KM_OVERRIDE));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, kp_param("headers", headers));
			vec_push(&f->params, kp_param("onResult", cb));
			f->ret = kp_class(CONNECT ".http", "Cancelable");
			add_return(f, client_call("client.unary", true, spec, true));
			push_fun(&funs, f);
		}
		if (configuration.generate_blocking_unary_methods) {
			KpFun *f = method_fun(xcat(name, "Blocking"), sanitize_kdoc(comment), NULL, KM(KM_OVERRIDE));
			vec_push(&f->params, kp_param("request", in));
			vec_push(&f->params, kp_param("headers", headers));
			f->ret = kp_parameterized(connect_class("UnaryBlockingCall"), TYPES(out));
			add_return(f, client_call("client.unaryBlocking", true, spec, false));
			push_fun(&funs, f);
		}
	}
	return funs;
}

/* serviceClientImplementation */
static KpTypeSpec *service_client_implementation(const char *package, const JService *s, const JFile *f,
						 const SourceCodeHelper *h)
{
	KpTypeSpec *t = kp_class_builder(xcat(s->name, "Client"));
	kp_type_add_superinterface(t, kp_class(package, xcat(s->name, "ClientInterface")));
	KpType *pci = connect_class("ProtocolClientInterface");
	KpFun *ctor = kp_constructor();
	vec_push(&ctor->params, kp_param("client", pci));
	kp_fun_build(ctor);
	kp_type_primary_constructor(t, ctor);
	KpProperty *p = kp_property("client", pci, KM(KM_PRIVATE));
	p->initializer = kp_code_of("client", 0, NULL);
	vec_push(&t->properties, p);
	Vec funs = implementation_methods(s, h);
	kp_type_add_kdoc(t, sanitize_kdoc(source_comment(h, xfmt("%d", s->index))));
	add_service_deprecation(t, s, f);
	t->funs = funs;
	kp_type_build(t);
	return t;
}

/* the file comments and the @file:Suppress("DEPRECATION") of suppressDeprecationWarnings */
static KpFile *file_spec(const char *package, const JFile *f)
{
	KpFile *fs = kp_file(package, f->name);
	return fs;
}

static void file_header(KpFile *fs, const JFile *f)
{
	kp_file_add_comment(fs, "Code generated by connect-kotlin. DO NOT EDIT.\n");
	kp_file_add_comment(fs, "\n");
	kp_file_add_comment(fs, xcat("Source: ", f->name, "\n"));
	bool deprecated_any = file_deprecated(f);
	for (int i = 0; i < f->services.n && !deprecated_any; i++) {
		JService *s = VEC_AT(f->services, JService, i);
		deprecated_any = service_deprecated(s);
		for (int j = 0; j < s->methods.n && !deprecated_any; j++)
			deprecated_any = method_deprecated(VEC_AT(s->methods, JMethod, j));
	}
	if (deprecated_any) {
		KpAnnotation *a = kp_annotation(kp_class("kotlin", "Suppress"));
		kp_annotation_add_member(a, "%S", ARGS(kp_arg_str("DEPRECATION")));
		kp_file_add_annotation(fs, a);
	}
}

typedef struct {
	KpFile *spec;
	const char *content;
} ToString;

static void file_to_string(void *arg)
{
	ToString *t = arg;
	t->content = kp_file_to_string(t->spec);
}

/* packageToDirectory */
static const char *package_to_directory(const char *s)
{
	char *dir = str_replace_all(s, ".", "/");
	return dir[0] == '/' ? dir + 1 : dir;
}

/* parseFile, and the files of Generator.generate */
static void parse_file(PluginRun *r, const JFile *file, const PbMsg *proto)
{
	SourceCodeHelper *h = source_code_helper(proto);
	const char *package = jnames_file_java_package(file);
	Vec names = {0}, specs = {0};
	for (int i = 0; i < file->services.n; i++) {
		JService *s = VEC_AT(file->services, JService, i);
		KpFile *iface = file_spec(package, file);
		file_header(iface, file);
		kp_file_add_type(iface, service_client_interface(package, s, file, h));
		vec_push(&names, (void *)kp_canonical_name(kp_class(package, xcat(s->name, "ClientInterface"))));
		vec_push(&specs, iface);

		KpFile *impl = file_spec(package, file);
		kp_file_add_import(impl, CONNECT, "MethodSpec");
		kp_file_add_import(impl, CONNECT, "StreamType");
		file_header(impl, file);
		kp_file_add_type(impl, service_client_implementation(package, s, file, h));
		for (int j = 0; j < s->methods.n; j++)
			if (pb_has(pb_msg(VEC_AT(s->methods, JMethod, j)->proto, MDP_OPTIONS), 34)) {
				kp_file_add_import(impl, CONNECT, "Idempotency");
				break;
			}
		vec_push(&names, (void *)kp_canonical_name(kp_class(package, xcat(s->name, "Client"))));
		vec_push(&specs, impl);
	}
	for (int i = 0; i < specs.n; i++) {
		ToString t = {specs.v[i], NULL};
		if (try_run(file_to_string, &t)) {
			Str msg = fail_message();
			if (str_has_prefix(msg, STR_LIT(UNSUPPORTED)))
				fail_str(msg);
			plugin_throw(xcat("java.lang.Throwable: failure on generating ", file->name), NULL);
		}
		PluginFile *pf = xalloc(sizeof *pf);
		pf->name = xcat(package_to_directory(names.v[i]), ".kt");
		pf->content = t.content;
		vec_push(&r->out, pf);
	}
}

/* Generator.generate */
static void connect_generate(PluginRun *r)
{
	configuration = parse_configuration(java_string(pb_bytes(r->req, CGR_PARAMETER)));
	Map protos = {0}; /* protoFileMap */
	for (int i = 0; i < pb_count(r->req, CGR_PROTO_FILE); i++) {
		PbMsg *p = pb_msg_at(r->req, CGR_PROTO_FILE, i);
		map_put(&protos, jstr(p, FDP_NAME), p);
	}
	for (int i = 0; i < pb_count(r->req, CGR_FILE_TO_GENERATE); i++) {
		const char *name = jstr_at(r->req, CGR_FILE_TO_GENERATE, i);
		JFile *file = map_get(&r->files, name);
		if (!file)
			plugin_throw("java.lang.RuntimeException: no descriptor sources found.",
				     "\tat com.connectrpc.protocgen.connect.Generator.generate(Generator.kt:82)\n\tat " PLUGIN_CLASS
				     ".run(Plugin.kt:123)\n" FRAMES_RUN);
		if (!file->services.n)
			continue;
		parse_file(r, file, map_get(&protos, file->name));
	}
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	return plugin_main(req, out);
}

const UniGenerator gen_connectrpc_kotlin_v0_10_0 = {
	.name = "connectrpc/kotlin",
	.version = "v0.10.0",
	.generate = generate,
};
