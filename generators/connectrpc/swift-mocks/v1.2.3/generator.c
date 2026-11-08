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

/* connectrpc/swift-mocks v1.2.3: a port of protoc-gen-connect-swift-mocks from connect-swift 1.2.3
 * (https://github.com/connectrpc/connect-swift, Plugins/ConnectMocksPlugin/ConnectMockGenerator.swift).
 * The Generator base class and its helpers are ConnectPluginUtilities, in
 * pkg/languages/swift/connect-plugin-utilities, on SwiftProtobufPluginLibrary 1.37.0. */
#include "connect_plugin_utilities.h"
#include "uni.h"

/* propertyVisibility and typeVisibility of the file being printed. */
static const char *property_visibility, *type_visibility;

static const char *deprecation_message = "message: \"This RPC has been marked as deprecated in its `.proto` file.\")";

/* ---- private extension ServiceDescriptor, MethodDescriptor ---- */

static char *mock_name(const SwService *s, SwNamer *namer) { return xcat(cpu_implementation_name(s, namer), "Mock"); }

static char *callback_mock_property_name(const SwMethod *m) { return xcat("mock", sw_to_upper_camel(m->name)); }

static char *async_await_mock_property_name(const SwMethod *m) { return xcat("mockAsync", sw_to_upper_camel(m->name)); }

/* callbackMockPropertyValue (async false) and asyncAwaitMockPropertyValue (async true) */
static char *mock_property_value(const SwMethod *m, SwNamer *namer, bool async)
{
	const char *input_name = sw_full_name_message(namer, m->input_type);
	const char *output_name = sw_full_name_message(namer, m->output_type);
	const char *kind = NULL;
	if (m->client_streaming && m->server_streaming)
		kind = "Bidirectional";
	else if (m->server_streaming)
		kind = "ServerOnly";
	else if (m->client_streaming)
		kind = "ClientOnly";
	if (kind)
		return xfmt("Mock%s%sStream<%s, %s>()", kind, async ? "Async" : "", input_name, output_name);
	return xfmt("{ (_: %s) -> ResponseMessage<%s> in .init(result: .success(.init())) }", input_name, output_name);
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
}

/* ---- ConnectMockGenerator ---- */

static void print_callback_method_mock_implementation(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	print_callback_availability_annotation(g, m);
	if (!m->server_streaming && !m->client_streaming)
		cpu_print_line(g, "@discardableResult");
	cpu_printf_line(g, "%s %s {", type_visibility, cpu_callback_signature(m, g->namer, true, g->options));
	cpu_indent(g);
	const char *mock_property = callback_mock_property_name(m);
	if (m->client_streaming || m->server_streaming) {
		cpu_printf_line(g, "self.%s.$inputs.first { !$0.isEmpty }.sink { _ in self.%s.outputs.forEach(onResult) }"
				   ".store(in: &self.cancellables)",
				mock_property, mock_property);
		cpu_printf_line(g, "return self.%s", mock_property);
	} else {
		cpu_printf_line(g, "completion(self.%s(request))", mock_property);
		cpu_print_line(g, "return Connect.Cancelable {}");
	}
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_async_await_method_mock_implementation(CpuGenerator *g, const SwMethod *m)
{
	cpu_print_line(g, "");
	print_async_await_availability_annotation(g, m);
	cpu_printf_line(g, "%s %s {", type_visibility, cpu_async_await_signature(m, g->namer, true, g->options));
	cpu_indent(g);
	if (m->client_streaming || m->server_streaming)
		cpu_printf_line(g, "return self.%s", async_await_mock_property_name(m));
	else
		cpu_printf_line(g, "return self.%s(request)", async_await_mock_property_name(m));
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_mock_service(CpuGenerator *g, const SwService *service)
{
	const CpuOptions *o = g->options;
	const char *protocol_name = cpu_protocol_name(service, g->namer);
	cpu_printf_line(g, "/// Mock implementation of `%s`.", protocol_name);
	cpu_print_line(g, "///");
	cpu_print_line(g, "/// Production implementations can be substituted with instances of this");
	cpu_print_line(g, "/// class to mock RPC calls. Behavior can be customized");
	cpu_print_line(g, "/// either through the properties on this class or by");
	cpu_print_line(g, "/// subclassing the mock and overriding its methods.");
	cpu_print_line(g, "///");
	cpu_print_line(g, "/// Note: This class does not handle thread-safe locking, but provides");
	cpu_print_line(g, "/// `@unchecked Sendable` conformance to simplify testing and mocking.");
	cpu_print_line(g, "@available(iOS 13, *)");
	cpu_printf_line(g, "%s class %s: %s, @unchecked Sendable {", type_visibility, mock_name(service, g->namer),
			protocol_name);
	cpu_indent(g);
	if (o->generate_callback_methods) {
		cpu_print_line(g, "private var cancellables = [Combine.AnyCancellable]()");
		cpu_print_line(g, "");
	}
	for (int i = 0; i < service->methods.n; i++) {
		const SwMethod *m = service->methods.v[i];
		if (o->generate_callback_methods) {
			cpu_printf_line(g, "/// Mocked for calls to `%s()`.", cpu_method_name(m, o));
			cpu_printf_line(g, "%s var %s = %s", property_visibility, callback_mock_property_name(m),
					mock_property_value(m, g->namer, false));
		}
		if (o->generate_async_methods) {
			cpu_printf_line(g, "/// Mocked for async calls to `%s()`.", cpu_method_name(m, o));
			cpu_printf_line(g, "%s var %s = %s", property_visibility, async_await_mock_property_name(m),
					mock_property_value(m, g->namer, true));
		}
	}
	cpu_print_line(g, "");
	cpu_printf_line(g, "%s init() {}", property_visibility);
	for (int i = 0; i < service->methods.n; i++) {
		if (o->generate_callback_methods)
			print_callback_method_mock_implementation(g, service->methods.v[i]);
		if (o->generate_async_methods)
			print_async_await_method_mock_implementation(g, service->methods.v[i]);
	}
	cpu_outdent(g);
	cpu_print_line(g, "}");
}

static void print_content(CpuGenerator *g, SwFile *descriptor)
{
	cpu_print_content(g, descriptor);
	switch (g->options->visibility) {
	case CPU_INTERNAL: property_visibility = "internal", type_visibility = "internal"; break;
	case CPU_PUBLIC: property_visibility = "public", type_visibility = "open"; break;
	case CPU_PACKAGE: property_visibility = "package", type_visibility = "package"; break;
	}
	static const char *const callback_imports[] = {"Combine", "ConnectMocks"};
	if (g->options->generate_callback_methods)
		cpu_print_module_imports(g, callback_imports, 2);
	else
		cpu_print_module_imports(g, callback_imports + 1, 1);
	for (int i = 0; i < g->services->n; i++) {
		cpu_print_line(g, "");
		print_mock_service(g, g->services->v[i]);
	}
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	CpuGenerator g = {
		.output_file_extension = ".mock.swift",
		.print_content = print_content,
		.program_name = "protoc-gen-connect-swift-mocks",
	};
	return cpu_main(&g, req, out);
}

const UniGenerator gen_connectrpc_swift_mocks_v1_2_3 = {
	.name = "connectrpc/swift-mocks",
	.version = "v1.2.3",
	.generate = generate,
};
