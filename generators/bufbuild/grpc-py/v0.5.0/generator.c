/* Copyright (c) 2026 Buf Technologies, Inc.
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

/* bufbuild/grpc-py v0.5.0: a port of protoc-gen-grpc-py 0.5.0
 * (src/protoc_gen_grpc_py/__init__.py at tag v0.5.0 of
 * https://github.com/bufbuild/protoc-gen-grpc-py, the same file as in the PyPI wheel), which
 * generates gRPC Python stubs for protobuf-py messages. The plugin framework protobuf.plugin and
 * the descriptor model are protobuf-py v0.7.0, ported in pkg/languages/python/protobuf-py.
 *
 * Unity build: everything but the generator below is static. */
#include "protobuf_py.h"
#include "uni.h"

#include <string.h>

/* The module-level identifiers (Module(...).ident(...)). */
#define ID(var, module, name, type_only) static const PbpyIdent var = {name, module, type_only, NULL}
#define COLLECTIONS_ABC "collections.abc"
ID(ASYNC_ITERATOR, COLLECTIONS_ABC, "AsyncIterator", true);
ID(ITERATOR, COLLECTIONS_ABC, "Iterator", true);
ID(SEQUENCE, COLLECTIONS_ABC, "Sequence", true);

ID(STATUS_CODE, "grpc", "StatusCode", false);
ID(UNARY_UNARY_HANDLER, "grpc", "unary_unary_rpc_method_handler", false);
ID(UNARY_STREAM_HANDLER, "grpc", "unary_stream_rpc_method_handler", false);
ID(STREAM_UNARY_HANDLER, "grpc", "stream_unary_rpc_method_handler", false);
ID(STREAM_STREAM_HANDLER, "grpc", "stream_stream_rpc_method_handler", false);
ID(GENERIC_HANDLER, "grpc", "method_handlers_generic_handler", false);

ID(CHANNEL, "grpc", "Channel", true);
ID(SERVER, "grpc", "Server", true);
ID(SERVICER_CONTEXT, "grpc", "ServicerContext", true);
ID(CALL_CREDENTIALS, "grpc", "CallCredentials", true);
ID(COMPRESSION, "grpc", "Compression", true);
ID(CALL_ITERATOR, "grpc", "_CallIterator", true);
ID(AIO_METADATA, "grpc", "aio.Metadata", true);
ID(AIO_CHANNEL, "grpc", "aio.Channel", true);
ID(AIO_SERVER, "grpc", "aio.Server", true);
ID(AIO_SERVICER_CONTEXT, "grpc", "aio.ServicerContext", true);
ID(AIO_UNARY_UNARY_CALL, "grpc", "aio.UnaryUnaryCall", true);
ID(AIO_UNARY_STREAM_CALL, "grpc", "aio.UnaryStreamCall", true);
ID(AIO_STREAM_UNARY_CALL, "grpc", "aio.StreamUnaryCall", true);
ID(AIO_STREAM_STREAM_CALL, "grpc", "aio.StreamStreamCall", true);

ID(DEPRECATED, "typing_extensions", "deprecated", false);
/* Use a private alias to prevent collisions with method names in class scope. */
static const char DEPRECATED_ALIAS[] = "_deprecated";

static const char UNIMPLEMENTED_DETAILS[] = "Method not implemented!";

/* _RESERVED_METHOD_NAMES = python_keywords() | {"add_to_server"} */
static const char *const RESERVED_METHOD_NAMES[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue",
	"def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in",
	"is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
	"add_to_server", NULL,
};

/* _IOOption(str, Enum) and _Options */
static const PbpyOptMember IO_OPTION[] = {{"SYNC", "sync"}, {"ASYNC", "async"}, {NULL, NULL}};
static const PbpyOptDef OPTIONS[] = {
	{"io", PBPY_OPT_STR_ENUM, false, NULL, "_IOOption", IO_OPTION, true},
};

static bool request_stream(const PbpyDescMethod *m) { return m->client_streaming; }
static bool response_stream(const PbpyDescMethod *m) { return m->server_streaming; }

/* _pascal_to_snake_case: _RE_UPPER_TO_LOWER.sub(r"\1_\2", text), then
 * _RE_LOWER_TO_UPPER.sub(r"\1_\2", s1).lower(). The patterns are ASCII, and [^_] before an ASCII
 * letter matches the last byte of a multi-byte character like the character itself. */
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

/* _method_local_name */
static const char *method_local_name(const PbpyDescMethod *m)
{
	return pbpy_escape_public_identifier(pascal_to_snake_case(m->name), RESERVED_METHOD_NAMES);
}

/* _service_name / _client_name */
static const char *stub_name(const PbpyDescService *s, const char *kind, bool is_async)
{
	return xcat(pbpy_escape_public_identifier(s->name, pbpy_python_keywords), kind, is_async ? "" : "Sync");
}

/* _generate_docstring */
static void generate_docstring(PbpyFile *f, const void *desc)
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

/* _maybe_generate_deprecated */
static void maybe_generate_deprecated(PbpyFile *f, const PbpyDescService *s, const PbpyDescMethod *m)
{
	if (!(m ? m->deprecated : s->deprecated))
		return;
	const char *name = m ? xcat(m->parent->type_name, ".", m->name) : s->type_name;
	/* category=None only marks the element for type checkers, without emitting a
	 * DeprecationWarning at runtime. */
	pbpy_print(f, PS("@"), PS(DEPRECATED_ALIAS), PS("(\""), PS(name), PS(" is deprecated.\", category=None)"));
}

/* _request_type / _service_return_type: the message, or an (async) iterator of it */
static PbpyEls *stream_type(const PbpyDescMessage *msg, bool stream, bool is_async)
{
	PbpyEls *l = xalloc(sizeof *l);
	if (stream) {
		pbpy_els_push(l, PI(is_async ? &ASYNC_ITERATOR : &ITERATOR));
		pbpy_els_push(l, PS("["));
		pbpy_els_push(l, PD(msg));
		pbpy_els_push(l, PS("]"));
	} else {
		pbpy_els_push(l, PD(msg));
	}
	return l;
}

static PbpyEls *request_type(const PbpyDescMethod *m, bool is_async) { return stream_type(m->input, request_stream(m), is_async); }

/* _client_return_type */
static PbpyEls *client_return_type(const PbpyDescMethod *m, bool is_async)
{
	PbpyEls *l = xalloc(sizeof *l);
	if (is_async) {
		/* _aio_call_ident */
		static const PbpyIdent *const calls[2][2] = {
			{&AIO_UNARY_UNARY_CALL, &AIO_UNARY_STREAM_CALL},
			{&AIO_STREAM_UNARY_CALL, &AIO_STREAM_STREAM_CALL},
		};
		pbpy_els_push(l, PI(calls[request_stream(m)][response_stream(m)]));
		pbpy_els_push(l, PS("["));
		pbpy_els_push(l, PD(m->input));
		pbpy_els_push(l, PS(", "));
		pbpy_els_push(l, PD(m->output));
		pbpy_els_push(l, PS("]"));
	} else if (response_stream(m)) {
		pbpy_els_push(l, PI(&CALL_ITERATOR));
		pbpy_els_push(l, PS("["));
		pbpy_els_push(l, PD(m->output));
		pbpy_els_push(l, PS("]"));
	} else {
		pbpy_els_push(l, PD(m->output));
	}
	return l;
}

/* _handler_factory */
static const PbpyIdent *handler_factory(const PbpyDescMethod *m)
{
	static const PbpyIdent *const handlers[2][2] = {
		{&UNARY_UNARY_HANDLER, &UNARY_STREAM_HANDLER},
		{&STREAM_UNARY_HANDLER, &STREAM_STREAM_HANDLER},
	};
	return handlers[request_stream(m)][response_stream(m)];
}

/* _channel_method */
static const char *channel_method(const PbpyDescMethod *m)
{
	return xcat(request_stream(m) ? "stream" : "unary", "_", response_stream(m) ? "stream" : "unary");
}

/* _generate_add_to_server */
static void generate_add_to_server(PbpyFile *f, const PbpyDescService *s, bool is_async)
{
	pbpy_scope(f, PS("def add_to_server(self, server: "), PI(is_async ? &AIO_SERVER : &SERVER), PS(") -> None:"));
	pbpy_doc(f, PS("Register this service's RPC handlers with a grpc server."));
	pbpy_doc_end(f);
	pbpy_scope(f, PS("rpc_method_handlers = {"));
	for (int i = 0; i < s->methods.n; i++) {
		const PbpyDescMethod *m = s->methods.v[i];
		pbpy_scope(f, PS("\""), PS(m->name), PS("\": "), PI(handler_factory(m)), PS("("));
		pbpy_print(f, PS("self."), PS(method_local_name(m)), PS(","));
		pbpy_print(f, PS("request_deserializer="), PD(m->input), PS(".from_binary,"));
		pbpy_print(f, PS("response_serializer="), PD(m->output), PS(".to_binary,"));
		pbpy_end(f);
		pbpy_print(f, PS("),"));
	}
	pbpy_end(f);
	pbpy_print(f, PS("}"));
	pbpy_scope(f, PS("generic_handler = "), PI(&GENERIC_HANDLER), PS("("));
	pbpy_print(f, PS("\""), PS(s->type_name), PS("\","));
	pbpy_print(f, PS("rpc_method_handlers,"));
	pbpy_end(f);
	pbpy_print(f, PS(")"));
	pbpy_print(f, PS("server.add_generic_rpc_handlers((generic_handler,))"));
	pbpy_end(f);
	pbpy_println(f);
}

/* _generate_service */
static void generate_service(PbpyFile *f, const PbpyDescService *s, bool is_async)
{
	const PbpyIdent *context = is_async ? &AIO_SERVICER_CONTEXT : &SERVICER_CONTEXT;
	maybe_generate_deprecated(f, s, NULL);
	pbpy_scope(f, PS("class "), PS(stub_name(s, "Service", is_async)), PS(":"));
	generate_docstring(f, s);
	for (int i = 0; i < s->methods.n; i++) {
		const PbpyDescMethod *m = s->methods.v[i];
		/* Unary-response handlers are coroutines; streaming handlers are plain functions
		 * returning an (async) iterator */
		maybe_generate_deprecated(f, NULL, m);
		pbpy_scope(f, PS(is_async && !response_stream(m) ? "async " : ""), PS("def "), PS(method_local_name(m)),
			PS("(self, request"), PS(request_stream(m) ? "_iterator" : ""), PS(": "), PL(request_type(m, is_async)),
			PS(", context: "), PI(context), PS(") -> "), PL(stream_type(m->output, response_stream(m), is_async)), PS(":"));
		generate_docstring(f, m);
		pbpy_print(f, PS("context.set_code("), PI(&STATUS_CODE), PS(".UNIMPLEMENTED)"));
		pbpy_print(f, PS("context.set_details(\""), PS(UNIMPLEMENTED_DETAILS), PS("\")"));
		pbpy_print(f, PS("raise NotImplementedError(\""), PS(UNIMPLEMENTED_DETAILS), PS("\")"));
		pbpy_end(f);
		pbpy_println(f);
	}
	generate_add_to_server(f, s, is_async);
	pbpy_end(f);
	pbpy_println(f);
}

/* _generate_client_method */
static void generate_client_method(PbpyFile *f, const PbpyDescMethod *m, bool is_async)
{
	const char *request_arg = request_stream(m) ? "request_iterator" : "request";
	maybe_generate_deprecated(f, NULL, m);
	pbpy_scope(f, PS("def "), PS(method_local_name(m)), PS("("));
	pbpy_print(f, PS("self,"));
	pbpy_print(f, PS(request_arg), PS(": "), PL(request_type(m, is_async)), PS(","));
	pbpy_print(f, PS("*,"));
	/* These per-call options mirror grpc's *MultiCallable.__call__ methods. */
	pbpy_print(f, PS("timeout: float | None = None,"));
	/* _metadata_type */
	if (is_async)
		pbpy_print(f, PS("metadata: "), PI(&AIO_METADATA), PS(" | "), PI(&SEQUENCE),
			PS("[tuple[str, str | bytes]]"), PS(" | None = None,"));
	else
		pbpy_print(f, PS("metadata: "), PS("tuple[tuple[str, str | bytes], ...]"), PS(" | None = None,"));
	pbpy_print(f, PS("credentials: "), PI(&CALL_CREDENTIALS), PS(" | None = None,"));
	pbpy_print(f, PS("wait_for_ready: bool | None = None,"));
	pbpy_print(f, PS("compression: "), PI(&COMPRESSION), PS(" | None = None,"));
	pbpy_end(f);
	pbpy_scope(f, PS(") -> "), PL(client_return_type(m, is_async)), PS(":"));
	generate_docstring(f, m);
	pbpy_print(f, PS("return self._"), PS(method_local_name(m)), PS("("), PS(request_arg),
		PS(", timeout=timeout, metadata=metadata, credentials=credentials,"
		   " wait_for_ready=wait_for_ready, compression=compression)"));
	pbpy_end(f);
	pbpy_println(f);
}

/* _method_url */
static const char *method_url(const PbpyDescMethod *m) { return xcat("/", m->parent->type_name, "/", m->name); }

/* _generate_client */
static void generate_client(PbpyFile *f, const PbpyDescService *s, bool is_async)
{
	maybe_generate_deprecated(f, s, NULL);
	pbpy_scope(f, PS("class "), PS(stub_name(s, "Client", is_async)), PS(":"));
	generate_docstring(f, s);
	pbpy_scope(f, PS("def __init__(self, channel: "), PI(is_async ? &AIO_CHANNEL : &CHANNEL), PS(") -> None:"));
	if (!s->methods.n)
		pbpy_print(f, PS("pass"));
	for (int i = 0; i < s->methods.n; i++) {
		const PbpyDescMethod *m = s->methods.v[i];
		pbpy_scope(f, PS("self._"), PS(method_local_name(m)), PS(" = channel."), PS(channel_method(m)), PS("("));
		pbpy_print(f, PS("\""), PS(method_url(m)), PS("\","));
		pbpy_print(f, PS("request_serializer="), PD(m->input), PS(".to_binary,"));
		pbpy_print(f, PS("response_deserializer="), PD(m->output), PS(".from_binary,"));
		pbpy_end(f);
		pbpy_print(f, PS(")"));
	}
	pbpy_end(f);
	pbpy_println(f);
	for (int i = 0; i < s->methods.n; i++)
		generate_client_method(f, s->methods.v[i], is_async);
	pbpy_end(f);
	pbpy_println(f);
}

/* _generate_file */
static void generate_file(PbpyFile *f, const PbpyDescFile *desc, const char *io)
{
	pbpy_preamble(f, desc);
	bool deprecated = false;
	for (int i = 0; i < desc->services.n && !deprecated; i++) {
		const PbpyDescService *s = desc->services.v[i];
		deprecated = s->deprecated;
		for (int j = 0; j < s->methods.n && !deprecated; j++)
			deprecated = ((const PbpyDescMethod *)s->methods.v[j])->deprecated;
	}
	if (deprecated) {
		pbpy_print(f, PS(DEPRECATED_ALIAS), PS(" = "), PI(&DEPRECATED));
		pbpy_println(f);
	}
	for (int i = 0; i < desc->services.n; i++) {
		const PbpyDescService *s = desc->services.v[i];
		if (!io || str_eq(io, "async")) {
			generate_service(f, s, true);
			generate_client(f, s, true);
		}
		if (!io || str_eq(io, "sync")) {
			generate_service(f, s, false);
			generate_client(f, s, false);
		}
	}
}

/* _generate */
static void generate(PbpySchema *s, const PbpyOptValue *options)
{
	const Vec *files = pbpy_files_to_generate(s);
	for (int i = 0; i < files->n; i++) {
		const PbpyDescFile *file = files->v[i];
		if (file->services.n)
			generate_file(pbpy_generate_file_desc(s, file, "_pb_grpc.py"), file, options[0].s);
	}
}

static const PbpyPlugin PLUGIN = {
	.name = "protoc-gen-grpc-py",
	.version = "0.5.0",
	.options = OPTIONS,
	.n_options = 1,
	.generate = generate,
};

static const char *uni_generate(PbMsg *req, Str raw, Buf *out)
{
	return pbpy_run(&PLUGIN, raw, pb_bytes(req, CGR_PARAMETER), out);
}

const UniGenerator gen_bufbuild_grpc_py_v0_5_0 = {
	.name = "bufbuild/grpc-py",
	.version = "v0.5.0",
	.generate = uni_generate,
	.parses_raw = true,
};
