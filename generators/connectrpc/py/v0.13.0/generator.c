/* Copyright 2025 The Connect Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* connectrpc/py v0.13.0: a port of protoc-gen-connectrpc 0.13.0
 * (protoc-gen-connectrpc/protoc_gen_connectrpc/__init__.py at tag v0.13.0 of
 * https://github.com/connectrpc/connect-py), which generates Connect clients and servers for
 * Python. The plugin framework protobuf.plugin and the descriptor model are protobuf-py v0.7.0,
 * ported in pkg/languages/python/protobuf-py.
 *
 * Unity build: everything but the generator below is static. The async and sync stubs
 * (_generate_async_stubs, _generate_sync_stubs) differ in a few names and lines only, so one
 * function prints both. */
#include "protobuf_py.h"
#include "uni.h"

#include <string.h>

/* The module-level identifiers (Module(...).ident(...)). */
#define ID(var, module, name, type_only) static const PbpyIdent var = {name, module, type_only, NULL}
ID(ASYNC_GENERATOR, "collections.abc", "AsyncGenerator", true);
ID(ASYNC_ITERATOR, "collections.abc", "AsyncIterator", true);
ID(ITERABLE, "collections.abc", "Iterable", true);
ID(ITERATOR, "collections.abc", "Iterator", true);
ID(MAPPING, "collections.abc", "Mapping", true);
ID(PROTOCOL, "typing", "Protocol", false);
ID(DESC_SERVICE, "protobuf", "DescService", false);
ID(PYQWEST_CLIENT, "pyqwest", "Client", false);
ID(PYQWEST_SYNC_CLIENT, "pyqwest", "SyncClient", false);
ID(CONNECT_CLIENT, "connectrpc.client", "ConnectClient", false);
ID(CONNECT_CLIENT_SYNC, "connectrpc.client", "ConnectClientSync", false);
ID(CODE, "connectrpc.code", "Code", false);
ID(CODEC, "connectrpc.codec", "Codec", true);
ID(GOOGLE_PROTOBUF_BINARY_CODEC, "connectrpc.compat", "google_protobuf_binary_codec", false);
ID(GOOGLE_PROTOBUF_CODECS, "connectrpc.compat", "google_protobuf_codecs", false);
ID(COMPRESSION, "connectrpc.compression", "Compression", true);
ID(GZIP_COMPRESSION, "connectrpc.compression.gzip", "GzipCompression", false);
ID(CONNECT_ERROR, "connectrpc.errors", "ConnectError", false);
ID(INTERCEPTOR, "connectrpc.interceptor", "Interceptor", true);
ID(INTERCEPTOR_SYNC, "connectrpc.interceptor", "InterceptorSync", true);
ID(METHOD_INFO, "connectrpc.method", "MethodInfo", false);
ID(IDEMPOTENCY_LEVEL, "connectrpc.method", "IdempotencyLevel", false);
ID(STREAM_TYPE, "connectrpc.method", "StreamType", false);
ID(HEADERS, "connectrpc.request", "Headers", true);
ID(REQUEST_CONTEXT, "connectrpc.request", "RequestContext", true);
ID(PROTOCOL_TYPE, "connectrpc.protocol", "ProtocolType", false);
ID(DEFAULT_READ_MAX_BYTES, "connectrpc.server", "DEFAULT_READ_MAX_BYTES", false);
ID(ENDPOINT, "connectrpc.server", "Endpoint", false);
ID(ENDPOINT_SYNC, "connectrpc.server", "EndpointSync", false);
ID(CONNECT_ASGI_APPLICATION, "connectrpc.server", "ConnectASGIApplication", false);
ID(CONNECT_WSGI_APPLICATION, "connectrpc.server", "ConnectWSGIApplication", false);

/* _KEYWORDS: python_keywords() and the names the service and client share */
static const char *const EXTRA_KEYWORDS[] = {
	"desc", "execute_unary", "execute_client_stream", "execute_server_stream", "execute_bidi_stream", NULL,
};
static const char *keywords[64];

/* method_kind */
enum { UNARY, CLIENT_STREAMING, SERVER_STREAMING, BIDI_STREAMING };
static const char *const ENDPOINT_TYPES[] = {"unary", "client_stream", "server_stream", "bidi_stream"};
static const char *const EXECUTE_METHODS[] = {"execute_unary", "execute_client_stream", "execute_server_stream", "execute_bidi_stream"};
static const char *const STREAM_TYPES[] = {"UNARY", "CLIENT_STREAM", "SERVER_STREAM", "BIDI_STREAM"};
static const char *const IDEMPOTENCY_LEVELS[] = {"UNKNOWN", "NO_SIDE_EFFECTS", "IDEMPOTENT"};

enum { METHODOPT_IDEMPOTENCY_LEVEL = 34, NO_SIDE_EFFECTS = 1 };

typedef struct {
	bool google;
	const char *io; /* NULL, "sync" or "async" */
} Options;

/* A method with what the stubs print of it. */
typedef struct {
	const PbpyDescMethod *desc;
	int kind;
	int64_t idempotency;
	const char *local_name;
	const PbpyIdent *input, *output;
} Method;

/* _module_name */
static const char *module_name(const char *filename)
{
	char *s = str_replace_all(str_trim_suffix(filename, ".proto"), "-", "_");
	return xcat(str_replace_all(s, "/", "."), "_pb2");
}

/* _message_ident */
static const PbpyIdent *message_ident(const PbpyDescMethod *method, const PbpyDescMessage *message, const Options *o)
{
	if (!o->google)
		return pbpy_ident_for_desc(message, false, false);
	const char *name = message->name;
	for (const PbpyDescMessage *p = message->parent; p; p = p->parent)
		name = xcat(p->name, ".", name);
	const char *mod = module_name(message->file->name);
	if (message->file == method->parent->file)
		mod = xcat(".", mod);
	return pbpy_ident(mod, name, false);
}

/* _pascal_to_snake_case: _RE_UPPER_TO_LOWER.sub(r"\1_\2", text), then
 * _RE_LOWER_TO_UPPER.sub(r"\1_\2", s1).lower() (the patterns are ASCII; [^_] matches the last
 * byte of a multi-byte character like the character itself) */
static const char *pascal_to_snake_case(const char *text)
{
	Buf s1 = {0};
	size_t n = strlen(text);
	for (size_t i = 0; i < n;) {
		/* ([^_])([A-Z][a-z]+) */
		if (text[i] != '_' && i + 2 < n && text[i + 1] >= 'A' && text[i + 1] <= 'Z' && text[i + 2] >= 'a' &&
			text[i + 2] <= 'z') {
			buf_addc(&s1, text[i]);
			buf_addc(&s1, '_');
			buf_addc(&s1, text[i + 1]);
			for (i += 2; i < n && text[i] >= 'a' && text[i] <= 'z'; i++)
				buf_addc(&s1, text[i]);
			continue;
		}
		buf_addc(&s1, text[i++]);
	}
	Buf out = {0};
	for (size_t i = 0; i < s1.n; i++) {
		char c = s1.p[i];
		if ((unsigned char)c >= 0x80)
			fail("unsupported by protoc-gen-uni: str.lower() of a non-ASCII method name");
		buf_addc(&out, c >= 'A' && c <= 'Z' ? (char)(c + 32) : c);
		/* ([a-z])([A-Z]) */
		if (c >= 'a' && c <= 'z' && i + 1 < s1.n && s1.p[i + 1] >= 'A' && s1.p[i + 1] <= 'Z') {
			buf_addc(&out, '_');
			buf_addc(&out, (char)(s1.p[++i] + 32));
		}
	}
	return buf_str(&out);
}

static Method method_of(const PbpyDescMethod *m, const Options *o)
{
	Method r = {m, 0, 0, NULL, NULL, NULL};
	r.kind = m->client_streaming ? (m->server_streaming ? BIDI_STREAMING : CLIENT_STREAMING)
		: m->server_streaming ? SERVER_STREAMING : UNARY;
	/* MethodOptions.idempotency_level: a closed enum, unknown values are unknown fields */
	r.idempotency = pb_int(pb_msg(m->proto, MDP_OPTIONS), METHODOPT_IDEMPOTENCY_LEVEL, 0);
	if (r.idempotency < 0 || r.idempotency > 2)
		fail("ValueError: Unknown idempotency level: %lld", (long long)r.idempotency);
	/* _method_local_name */
	r.local_name = pbpy_escape_public_identifier(pascal_to_snake_case(m->name), keywords);
	r.input = message_ident(m, m->input, o);
	r.output = message_ident(m, m->output, o);
	return r;
}

/* _supports_get */
static bool supports_get(const Method *m) { return m->kind == UNARY && m->idempotency == NO_SIDE_EFFECTS; }

/* The request (request) or response type of _async_signature / _sync_signature. */
static PbpyEl signature_type(const Method *m, bool request, bool sync)
{
	PbpyEls *l = xalloc(sizeof *l);
	bool stream = request ? m->kind == CLIENT_STREAMING || m->kind == BIDI_STREAMING
		: m->kind == SERVER_STREAMING || m->kind == BIDI_STREAMING;
	const PbpyIdent *base = request ? m->input : m->output;
	if (stream) {
		pbpy_els_push(l, PI(sync ? &ITERATOR : &ASYNC_ITERATOR));
		pbpy_els_push(l, PS("["));
		pbpy_els_push(l, PI(base));
		pbpy_els_push(l, PS("]"));
	} else {
		pbpy_els_push(l, PI(base));
	}
	return PL(l);
}

/* _print_docstring */
static void print_docstring(PbpyFile *f, const void *desc)
{
	Str *lines;
	int n = pbpy_comment_lines(desc, &lines);
	if (!n)
		return;
	pbpy_doc_v(f, NULL);
	for (int i = 0; i < n; i++)
		pbpy_print(f, PSN(lines[i]));
	pbpy_doc_end(f);
}

/* _generate_desc_method */
static void generate_desc_method(PbpyFile *f, const PbpyDescService *service, const Options *o)
{
	if (o->google)
		return;
	pbpy_print(f, PS("@classmethod"));
	pbpy_scope(f, PS("def desc(cls) -> "), PI(&DESC_SERVICE), PS(":"));
	pbpy_doc(f, PS("Returns the descriptor for this service."));
	pbpy_doc_end(f);
	pbpy_print(f, PS("return next(s for s in "), PD(service->file), PS(".desc().services if s.type_name == '"),
		PS(service->type_name), PS("')"));
	pbpy_end(f);
}

/* the MethodInfo(...) argument of the endpoints and the client methods */
static void print_method_info(PbpyFile *f, const Method *m)
{
	pbpy_scope(f, PS("method="), PI(&METHOD_INFO), PS("("));
	pbpy_print(f, PS("name=\""), PS(m->desc->name), PS("\","));
	pbpy_print(f, PS("service_name=\""), PS(m->desc->parent->type_name), PS("\","));
	pbpy_print(f, PS("input="), PI(m->input), PS(","));
	pbpy_print(f, PS("output="), PI(m->output), PS(","));
	pbpy_print(f, PS("idempotency_level="), PI(&IDEMPOTENCY_LEVEL), PS("."), PS(IDEMPOTENCY_LEVELS[m->idempotency]), PS(","));
	pbpy_print(f, PS("stream_type="), PI(&STREAM_TYPE), PS("."), PS(STREAM_TYPES[m->kind]), PS(","));
	pbpy_end(f);
	pbpy_print(f, PS("),"));
}

/* _print_google_compat_client_init */
static void print_google_compat_client_init(PbpyFile *f, const PbpyIdent *interceptor_type, const PbpyIdent *http_client_type)
{
	pbpy_scope(f, PS("def __init__("));
	pbpy_print(f, PS("self,"));
	pbpy_print(f, PS("address: str,"));
	pbpy_print(f, PS("*,"));
	pbpy_print(f, PS("codec: "), PI(&CODEC), PS(" | None = _PROTO_BINARY_CODEC,"));
	pbpy_print(f, PS("protocol: "), PI(&PROTOCOL_TYPE), PS(" = "), PI(&PROTOCOL_TYPE), PS(".CONNECT,"));
	pbpy_print(f, PS("accept_compression: "), PI(&ITERABLE), PS("["), PI(&COMPRESSION), PS("] | None = None,"));
	pbpy_print(f, PS("send_compression: "), PI(&COMPRESSION), PS(" | None = _GZIP_COMPRESSION,"));
	pbpy_print(f, PS("timeout_ms: int | None = None,"));
	pbpy_print(f, PS("read_max_bytes: int | None = None,"));
	pbpy_print(f, PS("interceptors: "), PI(&ITERABLE), PS("["), PI(interceptor_type), PS("] = (),"));
	pbpy_print(f, PS("http_client: "), PI(http_client_type), PS(" | None = None,"));
	pbpy_end(f);
	pbpy_scope(f, PS(") -> None:"));
	pbpy_scope(f, PS("super().__init__("));
	static const char *const args[] = {"address", "codec", "protocol", "accept_compression", "send_compression",
		"timeout_ms", "read_max_bytes", "interceptors", "http_client"};
	for (size_t i = 0; i < sizeof args / sizeof *args; i++)
		pbpy_print(f, PS(args[i]), PS("="), PS(args[i]), PS(","));
	pbpy_end(f);
	pbpy_print(f, PS(")"));
	pbpy_end(f);
}

/* _generate_async_stubs (sync = false) and _generate_sync_stubs (sync = true) */
static void generate_stubs(PbpyFile *f, const PbpyDescService *service, const Options *o, bool sync)
{
	const char *service_name = pbpy_escape_public_identifier(service->name, pbpy_python_keywords);
	const char *default_codecs = o->google ? "_DEFAULT_CODECS" : "None";
	int n = service->methods.n;
	Method *methods = xalloc(sizeof *methods * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		methods[i] = method_of(service->methods.v[i], o);

	pbpy_scope(f, PS("class "), PS(service_name), PS(sync ? "Sync(" : "("), PI(&PROTOCOL), PS("):"));
	print_docstring(f, service);
	if (!n && o->google)
		pbpy_print(f, PS("pass"));
	for (int i = 0; i < n; i++) {
		const Method *m = &methods[i];
		bool async_def = !sync && (m->kind == UNARY || m->kind == CLIENT_STREAMING);
		pbpy_scope(f, PS(async_def ? "async " : ""), PS("def "), PS(m->local_name), PS("(self, request: "),
			signature_type(m, true, sync), PS(", ctx: "), PI(&REQUEST_CONTEXT), PS("["), PI(m->input), PS(", "),
			PI(m->output), PS("], /) -> "), signature_type(m, false, sync), PS(":"));
		print_docstring(f, m->desc);
		pbpy_print(f, PS("raise "), PI(&CONNECT_ERROR), PS("("), PI(&CODE), PS(".UNIMPLEMENTED, 'Not implemented')"));
		pbpy_println(f);
		pbpy_end(f);
	}
	generate_desc_method(f, service, o);
	pbpy_end(f);

	pbpy_println(f);
	if (sync)
		pbpy_scope(f, PS("class "), PS(service_name), PS("WSGIApplication("), PI(&CONNECT_WSGI_APPLICATION), PS("):"));
	else
		pbpy_scope(f, PS("class "), PS(service_name), PS("ASGIApplication("), PI(&CONNECT_ASGI_APPLICATION), PS("["),
			PS(service_name), PS("]):"));
	pbpy_scope(f, PS("def __init__("));
	pbpy_print(f, PS("self,"));
	if (sync) {
		pbpy_print(f, PS("service: "), PS(service_name), PS("Sync,"));
	} else {
		pbpy_print(f, PS("service: "), PS(service_name), PS(" | "), PI(&ASYNC_GENERATOR), PS("["), PS(service_name), PS("],"));
		pbpy_print(f, PS("*,"));
	}
	pbpy_print(f, PS("interceptors: "), PI(&ITERABLE), PS("["), PI(sync ? &INTERCEPTOR_SYNC : &INTERCEPTOR), PS("] = (),"));
	pbpy_print(f, PS("read_max_bytes: int | None = "), PI(&DEFAULT_READ_MAX_BYTES), PS(","));
	pbpy_print(f, PS("compressions: "), PI(&ITERABLE), PS("["), PI(&COMPRESSION), PS("] | None = None,"));
	pbpy_print(f, PS("codecs: "), PI(&ITERABLE), PS("["), PI(&CODEC), PS("] | None = "), PS(default_codecs), PS(","));
	pbpy_end(f);
	pbpy_scope(f, PS(") -> None:"));
	pbpy_scope(f, PS("super().__init__("));
	if (!sync)
		pbpy_print(f, PS("service=service,"));
	pbpy_scope(f, PS(sync ? "endpoints={" : "endpoints=lambda svc: {"));
	for (int i = 0; i < n; i++) {
		const Method *m = &methods[i];
		pbpy_scope(f, PS(xcat("\"/", service->type_name, "/", m->desc->name, "\": ")), PI(sync ? &ENDPOINT_SYNC : &ENDPOINT),
			PS("."), PS(ENDPOINT_TYPES[m->kind]), PS("("));
		print_method_info(f, m);
		pbpy_print(f, PS(sync ? "function=service." : "function=svc."), PS(m->local_name), PS(","));
		pbpy_end(f);
		pbpy_print(f, PS("),"));
	}
	pbpy_end(f);
	pbpy_print(f, PS("},"));
	pbpy_print(f, PS("interceptors=interceptors,"));
	pbpy_print(f, PS("read_max_bytes=read_max_bytes,"));
	pbpy_print(f, PS("compressions=compressions,"));
	pbpy_print(f, PS("codecs=codecs,"));
	pbpy_end(f);
	pbpy_print(f, PS(")"));
	pbpy_end(f);
	pbpy_println(f);
	pbpy_print(f, PS("@property"));
	pbpy_scope(f, PS("def path(self) -> str:"));
	pbpy_doc(f, PS("Returns the URL path to mount the application to when serving multiple applications."));
	pbpy_doc_end(f);
	pbpy_print(f, PS(xcat("return \"/", service->type_name, "\"")));
	pbpy_end(f);
	pbpy_end(f);
	pbpy_println(f);
	pbpy_println(f);

	pbpy_scope(f, PS("class "), PS(service_name), PS(sync ? "ClientSync(" : "Client("),
		PI(sync ? &CONNECT_CLIENT_SYNC : &CONNECT_CLIENT), PS("):"));
	print_docstring(f, service);
	if (o->google)
		print_google_compat_client_init(f, sync ? &INTERCEPTOR_SYNC : &INTERCEPTOR, sync ? &PYQWEST_SYNC_CLIENT : &PYQWEST_CLIENT);
	else if (!n)
		pbpy_print(f, PS("pass"));
	for (int i = 0; i < n; i++) {
		const Method *m = &methods[i];
		bool async_def = !sync && (m->kind == UNARY || m->kind == CLIENT_STREAMING);
		pbpy_scope(f, PS(async_def ? "async " : ""), PS("def "), PS(m->local_name), PS("("));
		pbpy_print(f, PS("self,"));
		pbpy_print(f, PS("request: "), signature_type(m, true, sync), PS(","));
		pbpy_print(f, PS("*,"));
		pbpy_print(f, PS("headers: "), PI(&HEADERS), PS(" | "), PI(&MAPPING), PS("[str, str]"), PS(" | None = None, "));
		pbpy_print(f, PS("timeout_ms: int | None = None,"));
		if (supports_get(m))
			pbpy_print(f, PS("use_get: bool = False,"));
		pbpy_end(f);
		pbpy_scope(f, PS(") -> "), signature_type(m, false, sync), PS(":"));
		print_docstring(f, m->desc);
		pbpy_scope(f, PS("return "), PS(async_def ? "await " : ""), PS("self."), PS(EXECUTE_METHODS[m->kind]), PS("("));
		pbpy_print(f, PS("request=request,"));
		print_method_info(f, m);
		pbpy_print(f, PS("headers=headers,"));
		pbpy_print(f, PS("timeout_ms=timeout_ms,"));
		if (supports_get(m))
			pbpy_print(f, PS("use_get=use_get,"));
		pbpy_end(f);
		pbpy_print(f, PS(")"));
		pbpy_end(f);
	}
	if (!sync)
		pbpy_println(f);
	pbpy_end(f);
}

/* _generate_file */
static void generate_file(PbpyFile *f, const PbpyDescFile *desc, const Options *o)
{
	pbpy_preamble(f, desc);
	if (o->google) {
		pbpy_println(f);
		pbpy_print(f, PS("_DEFAULT_CODECS = "), PI(&GOOGLE_PROTOBUF_CODECS), PS("()"));
		pbpy_print(f, PS("_PROTO_BINARY_CODEC = "), PI(&GOOGLE_PROTOBUF_BINARY_CODEC), PS("()"));
		pbpy_print(f, PS("_GZIP_COMPRESSION = "), PI(&GZIP_COMPRESSION), PS("()"));
		pbpy_println(f);
	}
	for (int i = 0; i < desc->services.n; i++) {
		if (!o->io || str_eq(o->io, "async"))
			generate_stubs(f, desc->services.v[i], o, false);
		if (!o->io || str_eq(o->io, "sync"))
			generate_stubs(f, desc->services.v[i], o, true);
	}
}

/* _generate */
static void generate(PbpySchema *s, const PbpyOptValue *options)
{
	if (!keywords[0]) {
		int n = 0;
		for (const char *const *k = pbpy_python_keywords; *k; k++)
			keywords[n++] = *k;
		for (const char *const *k = EXTRA_KEYWORDS; *k; k++)
			keywords[n++] = *k;
	}
	Options o = {str_eq(options[0].s, "google"), options[1].s};
	const Vec *files = pbpy_files_to_generate(s);
	for (int i = 0; i < files->n; i++) {
		const PbpyDescFile *file = files->v[i];
		if (!file->services.n)
			continue;
		generate_file(pbpy_generate_file_desc(s, file, "_connect.py"), file, &o);
	}
}

/* Options: _ProtobufOption and _IOOption */
static const PbpyOptMember PROTOBUF_OPTION[] = {{"GOOGLE", "google"}, {"PY", "py"}, {NULL, NULL}};
static const PbpyOptMember IO_OPTION[] = {{"SYNC", "sync"}, {"ASYNC", "async"}, {NULL, NULL}};
static const PbpyOptDef OPTIONS[] = {
	{.name = "protobuf", .type = PBPY_OPT_STR_ENUM, .default_str = "py", .enum_name = "_ProtobufOption", .members = PROTOBUF_OPTION},
	{.name = "io", .type = PBPY_OPT_STR_ENUM, .enum_name = "_IOOption", .members = IO_OPTION, .optional = true},
};

static const PbpyPlugin PLUGIN = {
	.name = "protoc-gen-connectrpc-py",
	.version = "0.13.0",
	.options = OPTIONS,
	.n_options = 2,
	.generate = generate,
};

static const char *uni_generate(PbMsg *req, Str raw, Buf *out)
{
	return pbpy_run(&PLUGIN, raw, pb_bytes(req, CGR_PARAMETER), out);
}

const UniGenerator gen_connectrpc_py_v0_13_0 = {
	.name = "connectrpc/py",
	.version = "v0.13.0",
	.generate = uni_generate,
	.parses_raw = true,
};
