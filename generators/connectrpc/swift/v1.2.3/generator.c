/* Copyright 2022-2025 The Connect Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* connectrpc/swift v1.2.3: a port of protoc-gen-connect-swift from connect-swift 1.2.3
 * (https://github.com/connectrpc/connect-swift, Plugins/ConnectSwiftPlugin/ConnectClientGenerator.swift).
 * The Generator base class and its helpers are ConnectPluginUtilities, in
 * pkg/languages/swift/connect-plugin-utilities, on SwiftProtobufPluginLibrary 1.37.0. */
#include "connect_plugin_utilities.h"
#include "uni.h"

static const char *const visibility_names[] = {"internal", "public", "package"};

/* The visibility of the file being printed. */
static const char *visibility;

static const char *deprecation_message = "message: \"This RPC has been marked as deprecated in its `.proto` file.\")";

/* ---- private extension MethodDescriptor ---- */

static const char *spec_stream_type(const SwMethod *m)
{
	if (m->client_streaming && m->server_streaming)
		return ".bidirectionalStream";
	if (m->server_streaming)
		return ".serverStream";
	if (m->client_streaming)
		return ".clientStream";
	return ".unary";
}

/* self.options.idempotencyLevel (MethodOptions field 34, a closed enum: pkg/pb drops unknown
 * values as SwiftProtobuf does). */
static const char *idempotency_level(const SwMethod *m)
{
	switch (pb_int(m->options, 34, 0)) {
	case 1: return "noSideEffects";
	case 2: return "idempotent";
	}
	return "unknown";
}

static void print_callback_availability_annotation(CpuGenerator *g, const SwMethod *m)
{
	if (!m->deprecated)
		return;
	cpu_printf_line(g, "@available(iOS, introduced: 12, deprecated: 12, %s\n"
			   "@available(macOS, introduced: 10.15, deprecated: 10.15, %s\n"
			   "@available(tvOS, introduced: 13, deprecated: 13, %s\n"
			   "@available(watchOS, introduced: 6, deprecated: 6, %s",
			deprecation_message, deprecation_message, deprecation_message, deprecation_message);
}

static void print_async_await_availability_annotation(CpuGenerator *g, const SwMethod *m)
{
	if (m->deprecated)
		cpu_printf_line(g, "@available(iOS, introduced: 13, deprecated: 13, %s", deprecation_message);
	else
		cpu_print_line(g, "@available(iOS 13, *)");
}

static const char *stream_function(const SwMethod *m)
{
	if (m->client_streaming && m->server_streaming)
		return "bidirectionalStream";
	if (m->server_streaming)
		return "serverOnlyStream";
	return "clientOnlyStream";
}

static char *callback_return_value(const SwMethod *m)
{
	if (m->client_streaming || m->server_streaming)
		return xfmt("self.client.%s(path: \"%s\", headers: headers, onResult: onResult)", stream_function(m),
			    cpu_method_path(m));
	return xfmt("self.client.unary(path: \"%s\", idempotencyLevel: .%s, request: request, headers: headers, "
		    "completion: completion)",
		    cpu_method_path(m), idempotency_level(m));
}

static char *async_await_return_value(const SwMethod *m)
{
	if (m->client_streaming || m->server_streaming)
		return xfmt("self.client.%s(path: \"%s\", headers: headers)", stream_function(m), cpu_method_path(m));
	return xfmt("await self.client.unary(path: \"%s\", idempotencyLevel: .%s, request: request, headers: headers)",
		    cpu_method_path(m), idempotency_level(m));
}

/* ---- ConnectClientGenerator ---- */

static void print_callback_method_interface(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	cpu_print_comments_if_needed(g, SW_METHOD, m);
	print_callback_availability_annotation(g, m);
	if (!m->server_streaming && !m->client_streaming)
		cpu_print_line(g, "@discardableResult");
	cpu_print_line(g, cpu_callback_signature(m, g->namer, false, g->options));
}

static void print_async_await_method_interface(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	cpu_print_comments_if_needed(g, SW_METHOD, m);
	print_async_await_availability_annotation(g, m);
	cpu_print_line(g, cpu_async_await_signature(m, g->namer, false, g->options));
}

static void print_callback_method_implementation(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	print_callback_availability_annotation(g, m);
	if (!m->server_streaming && !m->client_streaming)
		cpu_print_line(g, "@discardableResult");
	cpu_printf_line(g, "%s %s {", visibility, cpu_callback_signature(m, g->namer, true, g->options));
	cpu_indent(g);
	cpu_printf_line(g, "return %s", callback_return_value(m));
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_async_await_method_implementation(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	print_async_await_availability_annotation(g, m);
	cpu_printf_line(g, "%s %s {", visibility, cpu_async_await_signature(m, g->namer, true, g->options));
	cpu_indent(g);
	cpu_printf_line(g, "return %s", async_await_return_value(m));
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_specs(CpuGenerator *g, const SwService *service)
{
	cpu_print_line(g, "");
	cpu_printf_line(g, "%s enum Metadata {", visibility);
	cpu_indent(g);
	cpu_printf_line(g, "%s enum Methods {", visibility);
	cpu_indent(g);
	for (int i = 0; i < service->methods.n; i++) {
		const SwMethod *m = service->methods.v[i];
		cpu_printf_line(g, "%s static let %s = Connect.MethodSpec(name: \"%s\", service: \"%s\", type: %s)",
				visibility, cpu_method_name(m, g->options), m->name, cpu_service_path(m->service),
				spec_stream_type(m));
	}
	cpu_outdent(g);
	cpu_print_line(g, "}");
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_service(CpuGenerator *g, const SwService *service)
{
	const CpuOptions *o = g->options;
	cpu_print_comments_if_needed(g, SW_SERVICE, service);

	const char *protocol_name = cpu_protocol_name(service, g->namer);
	cpu_printf_line(g, "%s protocol %s: Sendable {", visibility, protocol_name);
	cpu_indent(g);
	for (int i = 0; i < service->methods.n; i++) {
		if (o->generate_callback_methods)
			print_callback_method_interface(g, service->methods.v[i]);
		if (o->generate_async_methods)
			print_async_await_method_interface(g, service->methods.v[i]);
	}
	cpu_outdent(g);
	cpu_print_line(g, "}");

	cpu_print_line(g, "");

	const char *class_name = cpu_implementation_name(service, g->namer);
	cpu_printf_line(g, "/// Concrete implementation of `%s`.", protocol_name);
	cpu_printf_line(g, "%s final class %s: %s, Sendable {", visibility, class_name, protocol_name);
	cpu_indent(g);
	cpu_print_line(g, "private let client: Connect.ProtocolClientInterface");
	cpu_print_line(g, "");
	cpu_printf_line(g, "%s init(client: Connect.ProtocolClientInterface) {", visibility);
	cpu_indent(g);
	cpu_print_line(g, "self.client = client");
	cpu_outdent(g);
	cpu_print_line(g, "}");
	for (int i = 0; i < service->methods.n; i++) {
		if (o->generate_callback_methods)
			print_callback_method_implementation(g, service->methods.v[i]);
		if (o->generate_async_methods)
			print_async_await_method_implementation(g, service->methods.v[i]);
	}
	if (o->generate_service_metadata)
		print_specs(g, service);
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_content(CpuGenerator *g, SwFile *descriptor)
{
	cpu_print_content(g, descriptor);
	visibility = visibility_names[g->options->visibility];
	cpu_print_module_imports(g, NULL, 0);
	for (int i = 0; i < g->services->n; i++) {
		cpu_print_line(g, "");
		print_service(g, g->services->v[i]);
	}
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	CpuGenerator g = {
		.output_file_extension = ".connect.swift",
		.print_content = print_content,
		.program_name = "protoc-gen-connect-swift",
	};
	return cpu_main(&g, req, out);
}

const UniGenerator gen_connectrpc_swift_v1_2_3 = {
	.name = "connectrpc/swift",
	.version = "v1.2.3",
	.generate = generate,
};
