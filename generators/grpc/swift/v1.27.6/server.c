/*
 * Copyright 2018-2021, gRPC Authors All rights reserved.
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
 * limitations under the License.
 */

/* Port of Sources/protoc-gen-grpc-swift/Generator-Server.swift and
 * Generator-Server+AsyncAwait.swift (grpc-swift 1.27.6). printServerInterceptorFactoryProtocolExtension
 * is never called upstream and not ported. */

static void print_server_protocol(Generator *g)
{
	Str comments = service_comments(g);
	if (comments.n) {
		/* Source comments already have the leading '///' */
		println_str(g, comments, false);
		println(g, "///");
	}
	println(g, "/// To build a server, implement a class that conforms to this protocol.");
	printlnf(g, "%s protocol %s: CallHandlerProvider {", g->access, PROVIDER_NAME(g));
	indent(g);
	printlnf(g, "var interceptors: %s? { get }", SERVER_INTERCEPTOR_PROTOCOL_NAME(g));
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		println(g, "");
		println_str(g, method_comments(g), false);
		const char *name = g->method_function_name, *in = g->method_input_name, *out = g->method_output_name;
		switch (g->stream_type) {
		case UNARY:
			printlnf(g, "func %s(request: %s, context: StatusOnlyCallContext) -> EventLoopFuture<%s>", name, in, out);
			break;
		case SERVER_STREAMING:
			printlnf(g,
				 "func %s(request: %s, context: StreamingResponseCallContext<%s>) -> "
				 "EventLoopFuture<GRPCStatus>",
				 name, in, out);
			break;
		case CLIENT_STREAMING:
			printlnf(g,
				 "func %s(context: UnaryResponseCallContext<%s>) -> EventLoopFuture<(StreamEvent<%s>) -> "
				 "Void>",
				 name, out, in);
			break;
		case BIDIRECTIONAL_STREAMING:
			printlnf(g,
				 "func %s(context: StreamingResponseCallContext<%s>) -> "
				 "EventLoopFuture<(StreamEvent<%s>) -> Void>",
				 name, out, in);
			break;
		}
	}
	outdent(g);
	println(g, "}");
}

/* The "handle(method:context:)" function head shared by both server protocol extensions. */
static void print_handle_head(Generator *g)
{
	static const char *const args[] = {"method name: Substring", "context: CallHandlerContext"};
	Function f = {
		.name = "handle",
		.arguments = args,
		.n_arguments = 2,
		.return_type = "GRPCServerHandlerProtocol?",
		.access = g->access,
	};
	print_function(g, &f, true);
	println(g, "switch name {");
}

static void print_handle_default(Generator *g)
{
	/* Default case. */
	println(g, "default:");
	indent(g);
	println(g, "return nil");
	outdent(g);
	println(g, "}"); /* switch */
	end_function(g);
}

static void print_server_protocol_extension(Generator *g)
{
	static const char *const handler_types[] = {
		"UnaryServerHandler", "ClientStreamingServerHandler", "ServerStreamingServerHandler",
		"BidirectionalStreamingServerHandler",
	};
	printlnf(g, "extension %s {", PROVIDER_NAME(g));
	indent(g);
	open_block(g, xcat(g->access, " var serviceName: Substring"), BRACES_CURLY);
	/* This API returns a Substring (hence the '[...]') */
	printlnf(g, "return %s.serviceDescriptor.fullName[...]", SERVICE_SERVER_METADATA(g));
	close_block(g, BRACES_CURLY);
	println(g, "");
	println(g, "/// Determines, calls and returns the appropriate request handler, depending on the request's method.");
	println(g, "/// Returns nil for methods not handled by this service.");
	print_handle_head(g);
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		printlnf(g, "case \"%s\":", g->method->name);
		indent(g);
		printlnf(g, "return %s(", handler_types[g->stream_type]);
		indent(g);
		println(g, "context: context,");
		printlnf(g, "requestDeserializer: ProtobufDeserializer<%s>(),", g->method_input_name);
		printlnf(g, "responseSerializer: ProtobufSerializer<%s>(),", g->method_output_name);
		printlnf(g, "interceptors: self.interceptors?.%s() ?? [],", method_interceptor_factory_name(g));
		if (g->stream_type == UNARY || g->stream_type == SERVER_STREAMING)
			printlnf(g, "userFunction: self.%s(request:context:)", g->method_function_name);
		else
			printlnf(g, "observerFactory: self.%s(context:)", g->method_function_name);
		outdent(g);
		println(g, ")");
		outdent(g);
		println(g, "");
	}
	print_handle_default(g);
	outdent(g);
	println(g, "}");
}

static void print_server_interceptor_factory_protocol(Generator *g)
{
	printlnf(g, "%s protocol %s: Sendable {", g->access, SERVER_INTERCEPTOR_PROTOCOL_NAME(g));
	indent(g);
	/* Method specific interceptors. */
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		printlnf(g, "/// - Returns: Interceptors to use when handling '%s'.", g->method_function_name);
		println(g, "///   Defaults to calling `self.makeInterceptors()`.");
		/* Skip the access, we're defining a protocol. */
		print_method_interceptor_factory(g, "Server");
	}
	outdent(g);
	println(g, "}");
}

/* ---- Generator-Server+AsyncAwait.swift ---- */

static void print_rpc_protocol_requirement(Generator *g)
{
	/* Print any comments; skip the newline as source comments include them already. */
	println_str(g, method_comments(g), false);
	const char *request = xcat("request: ", g->method_input_name);
	const char *request_stream = xcat("requestStream: GRPCAsyncRequestStream<", g->method_input_name, ">");
	const char *response_stream = xcat("responseStream: GRPCAsyncResponseStreamWriter<", g->method_output_name, ">");
	const char *context = "context: GRPCAsyncServerCallContext";
	const char *args[3];
	int n = 0;
	const char *return_type = NULL;
	switch (g->stream_type) {
	case UNARY:
		args[n++] = request;
		args[n++] = context;
		return_type = g->method_output_name;
		break;
	case CLIENT_STREAMING:
		args[n++] = request_stream;
		args[n++] = context;
		return_type = g->method_output_name;
		break;
	case SERVER_STREAMING:
		args[n++] = request;
		args[n++] = response_stream;
		args[n++] = context;
		break;
	case BIDIRECTIONAL_STREAMING:
		args[n++] = request_stream;
		args[n++] = response_stream;
		args[n++] = context;
		break;
	}
	Function f = {
		.name = g->method_function_name,
		.arguments = args,
		.n_arguments = n,
		.return_type = return_type,
		.async = true,
		.throws = true,
	};
	print_function(g, &f, false);
}

static void print_server_protocol_async_await(Generator *g)
{
	Str comments = service_comments(g);
	if (comments.n) {
		/* Source comments already have the leading '///' */
		println_str(g, comments, false);
		println(g, "///");
	}
	println(g, "/// To implement a server, implement an object which conforms to this protocol.");
	print_availability_for_async_await(g);
	open_block(g, xcat(g->access, " protocol ", ASYNC_PROVIDER_NAME(g), ": CallHandlerProvider, Sendable"),
		   BRACES_CURLY);
	println(g, "static var serviceDescriptor: GRPCServiceDescriptor { get }");
	printlnf(g, "var interceptors: %s? { get }", SERVER_INTERCEPTOR_PROTOCOL_NAME(g));
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		println(g, "");
		print_rpc_protocol_requirement(g);
	}
	close_block(g, BRACES_CURLY);
}

static void print_server_protocol_extension_async_await(Generator *g)
{
	static const char *const wrappings[] = {
		"(request: $0, context: $1) }",
		"(requestStream: $0, context: $1) }",
		"(request: $0, responseStream: $1, context: $2) }",
		"(requestStream: $0, responseStream: $1, context: $2) }",
	};
	const char *access = g->access;
	/* Default extension to provide the service name and routing for methods. */
	print_availability_for_async_await(g);
	open_block(g, xcat("extension ", ASYNC_PROVIDER_NAME(g)), BRACES_CURLY);
	open_block(g, xcat(access, " static var serviceDescriptor: GRPCServiceDescriptor"), BRACES_CURLY);
	printlnf(g, "return %s.serviceDescriptor", SERVICE_SERVER_METADATA(g));
	close_block(g, BRACES_CURLY);
	println(g, "");
	/* This fulfils a requirement from 'CallHandlerProvider' */
	open_block(g, xcat(access, " var serviceName: Substring"), BRACES_CURLY);
	/* This API returns a Substring (hence the '[...]') */
	printlnf(g, "return %s.serviceDescriptor.fullName[...]", SERVICE_SERVER_METADATA(g));
	close_block(g, BRACES_CURLY);
	println(g, "");
	/* Default nil interceptor factory. */
	open_block(g, xcat(access, " var interceptors: ", SERVER_INTERCEPTOR_PROTOCOL_NAME(g), "?"), BRACES_CURLY);
	println(g, "return nil");
	close_block(g, BRACES_CURLY);
	println(g, "");
	print_handle_head(g);
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		open_block(g, xcat("case \"", g->method->name, "\":"), BRACES_NONE);
		open_block(g, "return GRPCAsyncServerHandler", BRACES_ROUND);
		println(g, "context: context,");
		printlnf(g, "requestDeserializer: ProtobufDeserializer<%s>(),", g->method_input_name);
		printlnf(g, "responseSerializer: ProtobufSerializer<%s>(),", g->method_output_name);
		printlnf(g, "interceptors: self.interceptors?.%s() ?? [],", method_interceptor_factory_name(g));
		printlnf(g, "wrapping: { try await self.%s%s", g->method_function_name, wrappings[g->stream_type]);
		close_block(g, BRACES_ROUND);
		close_block(g, BRACES_NONE);
	}
	print_handle_default(g);
	close_block(g, BRACES_CURLY);
}

static void print_server(Generator *g)
{
	if (!g->options->generate_server)
		return;
	print_server_protocol(g);
	println(g, "");
	print_server_protocol_extension(g);
	println(g, "");
	print_server_protocol_async_await(g);
	println(g, "");
	print_server_protocol_extension_async_await(g);
	println(g, "");
	/* Both implementations share definitions for interceptors and metadata. */
	print_server_interceptor_factory_protocol(g);
	println(g, "");
	print_metadata(g, true);
}
