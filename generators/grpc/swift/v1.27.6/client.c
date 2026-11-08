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

/* Port of Sources/protoc-gen-grpc-swift/Generator-Client.swift and
 * Generator-Client+AsyncAwait.swift (grpc-swift 1.27.6). */

/* StreamingType.name */
static const char *const streaming_type_names[] = {"Unary", "Client streaming", "Server streaming", "Bidirectional streaming"};

/* protoSourceComments() of the current service or method. */
static Str service_comments(Generator *g) { return sw_proto_source_comments(SW_SERVICE, g->service, "///", NULL); }
static Str method_comments(Generator *g) { return sw_proto_source_comments(SW_METHOD, g->method, "///", NULL); }

/* MethodDescriptor.documentation(streamingType:) */
static void print_method_documentation(Generator *g)
{
	Str comments = method_comments(g);
	if (comments.n) {
		println_str(g, comments, false); /* already prefixed with "///" */
		return;
	}
	Buf b = {0};
	buf_fmt(&b, "/// %s call to %s\n", streaming_type_names[g->stream_type], g->method->name);
	println_str(g, buf_view(&b), false);
}

/* methodArguments; without_defaults: methodArgumentsWithoutDefaults */
static int method_arguments(Generator *g, bool without_defaults, const char **args)
{
	const char *call_options = without_defaults ? "callOptions: CallOptions?" : "callOptions: CallOptions? = nil";
	const char *request = xcat("_ request: ", g->method_input_name);
	const char *handler = xcat("handler: @escaping (", g->method_output_name, ") -> Void");
	int n = 0;
	switch (g->stream_type) {
	case UNARY:
		args[n++] = request;
		args[n++] = call_options;
		break;
	case SERVER_STREAMING:
		args[n++] = request;
		args[n++] = call_options;
		args[n++] = handler;
		break;
	case CLIENT_STREAMING:
		args[n++] = call_options;
		break;
	case BIDIRECTIONAL_STREAMING:
		args[n++] = call_options;
		args[n++] = handler;
		break;
	}
	return n;
}

/* methodReturnType */
static const char *method_return_type(Generator *g)
{
	static const char *const kinds[] = {"UnaryCall", "ClientStreamingCall", "ServerStreamingCall", "BidirectionalStreamingCall"};
	return xcat(kinds[g->stream_type], "<", g->method_input_name, ", ", g->method_output_name, ">");
}

static void print_service_client_protocol(Generator *g)
{
	Str comments = service_comments(g);
	if (comments.n) {
		/* Source comments already have the leading '///' */
		println_str(g, comments, false);
		println(g, "///");
	}
	printlnf(g, "/// Usage: instantiate `%s`, then call methods of this protocol to make API calls.", CLIENT_CLASS_NAME(g));
	printlnf(g, "%s protocol %s: GRPCClient {", g->access, CLIENT_PROTOCOL_NAME(g));
	indent(g);
	println(g, "var serviceName: String { get }");
	printlnf(g, "var interceptors: %s? { get }", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		const char *args[3];
		Function f = {
			.name = g->method_function_name,
			.arguments = args,
			.n_arguments = method_arguments(g, true, args),
			.return_type = method_return_type(g),
		};
		print_function(g, &f, false);
	}
	outdent(g);
	println(g, "}");
}

static void print_parameters(Generator *g) { println(g, "/// - Parameters:"); }
static void print_request_parameter(Generator *g) { printlnf(g, "///   - request: Request to send to %s.", g->method->name); }
static void print_call_options_parameter(Generator *g) { println(g, "///   - callOptions: Call options."); }
static void print_handler_parameter(Generator *g)
{
	println(g, "///   - handler: A closure called when each response is received from the server.");
}

static void print_client_streaming_details(Generator *g)
{
	println(g, "/// Callers should use the `send` method on the returned object to send messages");
	println(g, "/// to the server. The caller should send an `.end` after the final message has been sent.");
}

/* printUnaryCall, printServerStreamingCall, printClientStreamingCall,
 * printBidirectionalStreamingCall */
static void print_call(Generator *g)
{
	static const char *const returns[] = {
		"/// - Returns: A `UnaryCall` with futures for the metadata, status and response.",
		"/// - Returns: A `ClientStreamingCall` with futures for the metadata, status and response.",
		"/// - Returns: A `ServerStreamingCall` with futures for the metadata and status.",
		"/// - Returns: A `ClientStreamingCall` with futures for the metadata and status.",
	};
	static const char *const makers[] = {
		"makeUnaryCall", "makeClientStreamingCall", "makeServerStreamingCall", "makeBidirectionalStreamingCall",
	};
	StreamingType t = g->stream_type;
	bool streams_requests = t == CLIENT_STREAMING || t == BIDIRECTIONAL_STREAMING;
	bool streams_responses = t == SERVER_STREAMING || t == BIDIRECTIONAL_STREAMING;
	print_method_documentation(g);
	println(g, "///");
	if (streams_requests) {
		print_client_streaming_details(g);
		println(g, "///");
	}
	print_parameters(g);
	if (!streams_requests)
		print_request_parameter(g);
	print_call_options_parameter(g);
	if (streams_responses)
		print_handler_parameter(g);
	println(g, returns[t]);
	const char *args[3];
	Function f = {
		.name = g->method_function_name,
		.arguments = args,
		.n_arguments = method_arguments(g, false, args),
		.return_type = method_return_type(g),
		.access = g->access,
	};
	print_function(g, &f, true);
	printlnf(g, "return self.%s(", makers[t]);
	indent(g);
	printlnf(g, "path: %s,", g->method_path_using_client_metadata);
	if (!streams_requests)
		println(g, "request: request,");
	println(g, "callOptions: callOptions ?? self.defaultCallOptions,");
	printlnf(g, "interceptors: self.interceptors?.%s() ?? []%s", method_interceptor_factory_name(g),
		 streams_responses ? "," : "");
	if (streams_responses)
		println(g, "handler: handler");
	outdent(g);
	println(g, ")");
	end_function(g);
}

static void print_client_protocol_extension(Generator *g)
{
	printlnf(g, "extension %s {", CLIENT_PROTOCOL_NAME(g));
	indent(g);
	/* Service name. */
	printlnf(g, "%s var serviceName: String {", g->access);
	indent(g);
	printlnf(g, "return \"%s\"", g->service_path);
	outdent(g);
	println(g, "}");
	/* Default method implementations: printMethods */
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		print_call(g);
	}
	outdent(g);
	println(g, "}");
}

/* printMethodInterceptorFactory, without a body */
static void print_method_interceptor_factory(Generator *g, const char *kind)
{
	Function f = {
		.name = method_interceptor_factory_name(g),
		.return_type = xcat("[", kind, "Interceptor<", g->method_input_name, ", ", g->method_output_name, ">]"),
	};
	print_function(g, &f, false);
}

static void print_service_client_interceptor_factory_protocol(Generator *g)
{
	printlnf(g, "%s protocol %s: Sendable {", g->access, CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	indent(g);
	/* Method specific interceptors. */
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		printlnf(g, "/// - Returns: Interceptors to use when invoking '%s'.", g->method_function_name);
		/* Skip the access, we're defining a protocol. */
		print_method_interceptor_factory(g, "Client");
	}
	outdent(g);
	println(g, "}");
}

/* The doc comment and init(...) shared by the class and struct backed clients. */
static void print_client_init_head(Generator *g)
{
	const char *access = g->access;
	printlnf(g, "/// Creates a client for the %s service.", g->service_path);
	println(g, "///");
	print_parameters(g);
	println(g, "///   - channel: `GRPCChannel` to the service host.");
	println(g, "///   - defaultCallOptions: Options to use for each service call if the user doesn't provide them.");
	println(g, "///   - interceptors: A factory providing interceptors for each RPC.");
	printlnf(g, "%s init(", access);
	indent(g);
	println(g, "channel: GRPCChannel,");
	println(g, "defaultCallOptions: CallOptions = CallOptions(),");
	printlnf(g, "interceptors: %s? = nil", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	outdent(g);
	println(g, ") {");
	indent(g);
	println(g, "self.channel = channel");
}

static void print_class_backed_service_client_implementation(Generator *g)
{
	const char *access = g->access, *client_class_name = CLIENT_CLASS_NAME(g);
	const char *interceptor_protocol = CLIENT_INTERCEPTOR_PROTOCOL_NAME(g);
	println(g, "@available(*, deprecated)");
	printlnf(g, "extension %s: @unchecked Sendable {}", client_class_name);
	println(g, "");
	printlnf(g, "@available(*, deprecated, renamed: \"%s\")", CLIENT_STRUCT_NAME(g));
	printlnf(g, "%s final class %s: %s {", access, client_class_name, CLIENT_PROTOCOL_NAME(g));
	indent(g);
	println(g, "private let lock = Lock()");
	println(g, "private var _defaultCallOptions: CallOptions");
	printlnf(g, "private var _interceptors: %s?", interceptor_protocol);
	printlnf(g, "%s let channel: GRPCChannel", access);
	printlnf(g, "%s var defaultCallOptions: CallOptions {", access);
	indent(g);
	println(g, "get { self.lock.withLock { return self._defaultCallOptions } }");
	println(g, "set { self.lock.withLockVoid { self._defaultCallOptions = newValue } }");
	outdent(g);
	println(g, "}");
	printlnf(g, "%s var interceptors: %s? {", access, interceptor_protocol);
	indent(g);
	println(g, "get { self.lock.withLock { return self._interceptors } }");
	println(g, "set { self.lock.withLockVoid { self._interceptors = newValue } }");
	outdent(g);
	println(g, "}");
	println(g, "");
	print_client_init_head(g);
	println(g, "self._defaultCallOptions = defaultCallOptions");
	println(g, "self._interceptors = interceptors");
	outdent(g);
	println(g, "}");
	outdent(g);
	println(g, "}");
}

static void print_struct_backed_service_client_implementation(Generator *g)
{
	const char *access = g->access;
	printlnf(g, "%s struct %s: %s {", access, CLIENT_STRUCT_NAME(g), CLIENT_PROTOCOL_NAME(g));
	indent(g);
	printlnf(g, "%s var channel: GRPCChannel", access);
	printlnf(g, "%s var defaultCallOptions: CallOptions", access);
	printlnf(g, "%s var interceptors: %s?", access, CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	println(g, "");
	print_client_init_head(g);
	println(g, "self.defaultCallOptions = defaultCallOptions");
	println(g, "self.interceptors = interceptors");
	outdent(g);
	println(g, "}");
	outdent(g);
	println(g, "}");
}

/* ---- Generator-Client+AsyncAwait.swift ---- */

static void print_async_service_client_protocol(Generator *g)
{
	Str comments = service_comments(g);
	if (comments.n)
		println_str(g, comments, false); /* Source comments already have the leading '///' */
	print_availability_for_async_await(g);
	printlnf(g, "%s protocol %s: GRPCClient {", g->access, ASYNC_CLIENT_PROTOCOL_NAME(g));
	indent(g);
	println(g, "static var serviceDescriptor: GRPCServiceDescriptor { get }");
	printlnf(g, "var interceptors: %s? { get }", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		const char *args[2];
		int n = 0;
		if (g->stream_type == UNARY || g->stream_type == SERVER_STREAMING)
			args[n++] = xcat("_ request: ", g->method_input_name);
		args[n++] = "callOptions: CallOptions?";
		Function f = {
			.name = method_make_function_call_name(g),
			.arguments = args,
			.n_arguments = n,
			.return_type = xcat("GRPC", call_types[g->stream_type], "<", g->method_input_name, ", ",
					    g->method_output_name, ">"),
		};
		print_function(g, &f, false);
	}
	outdent(g);
	println(g, "}"); /* protocol */
}

/* printRPCFunction up to the body */
static void print_rpc_function(Generator *g, const char *name)
{
	const char *args[2];
	int n = 0;
	if (g->stream_type == UNARY || g->stream_type == SERVER_STREAMING)
		args[n++] = xcat("_ request: ", g->method_input_name);
	args[n++] = "callOptions: CallOptions? = nil";
	Function f = {
		.name = name,
		.arguments = args,
		.n_arguments = n,
		.return_type =
			xcat("GRPC", call_types[g->stream_type], "<", g->method_input_name, ", ", g->method_output_name, ">"),
		.access = g->access,
	};
	print_function(g, &f, true);
}

static void print_rpc_function_implementation(Generator *g)
{
	print_rpc_function(g, method_make_function_call_name(g));
	open_block(g, xcat("return self.make", call_types[g->stream_type]), BRACES_ROUND);
	printlnf(g, "path: %s,", g->method_path_using_client_metadata);
	if (g->stream_type == UNARY || g->stream_type == SERVER_STREAMING)
		println(g, "request: request,");
	println(g, "callOptions: callOptions ?? self.defaultCallOptions,");
	printlnf(g, "interceptors: self.interceptors?.%s() ?? []", method_interceptor_factory_name(g));
	close_block(g, BRACES_ROUND);
	end_function(g);
}

static void print_rpc_function_wrapper(Generator *g)
{
	const char *function_name = method_make_function_call_name(g);
	const char *function_wrapper_name = method_make_function_call_wrapper_name(g);
	if (str_eq(function_name, function_wrapper_name))
		return;
	println(g, "");
	print_rpc_function(g, function_wrapper_name);
	open_block(g, xcat("return self.", function_name), BRACES_ROUND);
	if (g->stream_type == UNARY || g->stream_type == SERVER_STREAMING)
		println(g, "request,");
	println(g, "callOptions: callOptions");
	close_block(g, BRACES_ROUND);
	end_function(g);
}

static void print_async_client_protocol_extension(Generator *g)
{
	const char *access = g->access;
	print_availability_for_async_await(g);
	open_block(g, xcat("extension ", ASYNC_CLIENT_PROTOCOL_NAME(g)), BRACES_CURLY);
	/* Service descriptor. */
	open_block(g, xcat(access, " static var serviceDescriptor: GRPCServiceDescriptor"), BRACES_CURLY);
	printlnf(g, "return %s.serviceDescriptor", SERVICE_CLIENT_METADATA(g));
	close_block(g, BRACES_CURLY);
	println(g, "");
	/* Interceptor factory. */
	open_block(g, xcat(access, " var interceptors: ", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g), "?"), BRACES_CURLY);
	println(g, "return nil");
	close_block(g, BRACES_CURLY);
	/* 'Unsafe' calls. */
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		print_rpc_function_implementation(g);
		print_rpc_function_wrapper(g);
	}
	close_block(g, BRACES_CURLY);
}

/* "Simple, but safe" call wrappers. */
static void print_async_client_protocol_safe_wrappers_extension(Generator *g)
{
	print_availability_for_async_await(g);
	open_block(g, xcat("extension ", ASYNC_CLIENT_PROTOCOL_NAME(g)), BRACES_CURLY);
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		StreamingType t = g->stream_type;
		bool streams_responses = t == SERVER_STREAMING || t == BIDIRECTIONAL_STREAMING;
		bool streams_requests = t == CLIENT_STREAMING || t == BIDIRECTIONAL_STREAMING;
		/* (protocol, requires sendable) */
		static const char *const sequence_protocols[] = {"Sequence", "AsyncSequence & Sendable"};
		int n_sequences = streams_requests ? 2 : 1;
		for (int j = 0; j < n_sequences; j++) {
			/* Print a new line if this is not the first function in the extension. */
			if (i > 0 || j > 0)
				println(g, "");
			const char *function_name = streams_requests ? xcat(g->method_function_name, "<RequestStream>")
								     : g->method_function_name;
			const char *request_param_name = streams_requests ? "requests" : "request";
			const char *request_param_type = streams_requests ? "RequestStream" : g->method_input_name;
			const char *return_type = streams_responses
							  ? xcat("GRPCAsyncResponseStream<", g->method_output_name, ">")
							  : g->method_output_name;
			const char *where = NULL;
			if (streams_requests)
				where = xcat("where RequestStream: ", sequence_protocols[j], ", RequestStream.Element == ",
					     g->method_input_name);
			const char *args[] = {
				xcat("_ ", request_param_name, ": ", request_param_type),
				"callOptions: CallOptions? = nil",
			};
			Function f = {
				.name = function_name,
				.arguments = args,
				.n_arguments = 2,
				.return_type = return_type,
				.access = g->access,
				.async = !streams_responses,
				.throws = !streams_responses,
				.generic_where_clause = where,
			};
			print_function(g, &f, true);
			open_block(g,
				   xcat("return", !streams_responses ? " try await" : "", " self.perform",
					call_types[t]),
				   BRACES_ROUND);
			printlnf(g, "path: %s,", g->method_path_using_client_metadata);
			printlnf(g, "%s: %s,", request_param_name, request_param_name);
			println(g, "callOptions: callOptions ?? self.defaultCallOptions,");
			printlnf(g, "interceptors: self.interceptors?.%s() ?? []", method_interceptor_factory_name(g));
			close_block(g, BRACES_ROUND);
			end_function(g);
		}
	}
	close_block(g, BRACES_CURLY);
}

static void print_async_service_client_implementation(Generator *g)
{
	const char *access = g->access;
	print_availability_for_async_await(g);
	open_block(g, xcat(access, " struct ", ASYNC_CLIENT_STRUCT_NAME(g), ": ", ASYNC_CLIENT_PROTOCOL_NAME(g)),
		   BRACES_CURLY);
	printlnf(g, "%s var channel: GRPCChannel", access);
	printlnf(g, "%s var defaultCallOptions: CallOptions", access);
	printlnf(g, "%s var interceptors: %s?", access, CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	println(g, "");
	printlnf(g, "%s init(", access);
	indent(g);
	println(g, "channel: GRPCChannel,");
	println(g, "defaultCallOptions: CallOptions = CallOptions(),");
	printlnf(g, "interceptors: %s? = nil", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	outdent(g);
	println(g, ") {");
	indent(g);
	println(g, "self.channel = channel");
	println(g, "self.defaultCallOptions = defaultCallOptions");
	println(g, "self.interceptors = interceptors");
	outdent(g);
	println(g, "}");
	close_block(g, BRACES_CURLY);
}

/* ---- Generator-Client.swift: the test client ---- */

/* printResponseStream */
static void print_response_stream(Generator *g, bool is_unary)
{
	const char *type = is_unary ? "FakeUnaryResponse" : "FakeStreamingResponse";
	const char *factory = is_unary ? "makeFakeUnaryResponse" : "makeFakeStreamingResponse";
	printlnf(g, "/// Make a %s response for the %s RPC. This must be called", is_unary ? "unary" : "streaming",
		 g->method->name);
	printlnf(g, "/// before calling '%s'. See also '%s'.", g->method_function_name, type);
	println(g, "///");
	println(g, "/// - Parameter requestHandler: a handler for request parts sent by the RPC.");
	const char *args[] = {
		xcat("_ requestHandler: @escaping (FakeRequestPart<", g->method_input_name, ">) -> () = { _ in }"),
	};
	Function f = {
		.name = xcat("make", g->method->name, "ResponseStream"),
		.arguments = args,
		.n_arguments = 1,
		.return_type = xcat(type, "<", g->method_input_name, ", ", g->method_output_name, ">"),
		.access = g->access,
	};
	print_function(g, &f, true);
	printlnf(g, "return self.fakeChannel.%s(path: %s, requestHandler: requestHandler)", factory,
		 g->method_path_using_client_metadata);
	end_function(g);
}

/* printEnqueueUnaryResponse */
static void print_enqueue_unary_response(Generator *g, bool is_unary)
{
	const char *args[] = {
		is_unary ? xcat("_ response: ", g->method_output_name) : xcat("_ responses: [", g->method_output_name, "]"),
		xcat("_ requestHandler: @escaping (FakeRequestPart<", g->method_input_name, ">) -> () = { _ in }"),
	};
	Function f = {
		.name = xcat("enqueue", g->method->name, is_unary ? "Response" : "Responses"),
		.arguments = args,
		.n_arguments = 2,
		.access = g->access,
	};
	print_function(g, &f, true);
	printlnf(g, "let stream = self.make%sResponseStream(requestHandler)", g->method->name);
	if (is_unary) {
		println(g, "// This is the only operation on the stream; try! is fine.");
		println(g, "try! stream.sendMessage(response)");
	} else {
		println(g, "// These are the only operation on the stream; try! is fine.");
		println(g, "responses.forEach { try! stream.sendMessage($0) }");
		println(g, "try! stream.sendEnd()");
	}
	end_function(g);
}

static void print_has_response_stream_enqueued(Generator *g)
{
	printlnf(g, "/// Returns true if there are response streams enqueued for '%s'", g->method->name);
	printlnf(g, "%s var has%sResponsesRemaining: Bool {", g->access, g->method->name);
	indent(g);
	printlnf(g, "return self.fakeChannel.hasFakeResponseEnqueued(forPath: %s)", g->method_path_using_client_metadata);
	outdent(g);
	println(g, "}");
}

static void print_test_client(Generator *g)
{
	const char *access = g->access, *test_client_class_name = TEST_CLIENT_CLASS_NAME(g);
	println(g, "@available(swift, deprecated: 5.6)");
	printlnf(g, "extension %s: @unchecked Sendable {}", test_client_class_name);
	println(g, "");
	println(g, "@available(swift, deprecated: 5.6, message: \"Test clients are not Sendable but the 'GRPCClient' "
		   "API requires clients to be Sendable. Using a localhost client and server is the recommended "
		   "alternative.\")");
	printlnf(g, "%s final class %s: %s {", access, test_client_class_name, CLIENT_PROTOCOL_NAME(g));
	indent(g);
	println(g, "private let fakeChannel: FakeChannel");
	printlnf(g, "%s var defaultCallOptions: CallOptions", access);
	printlnf(g, "%s var interceptors: %s?", access, CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	println(g, "");
	printlnf(g, "%s var channel: GRPCChannel {", access);
	indent(g);
	println(g, "return self.fakeChannel");
	outdent(g);
	println(g, "}");
	println(g, "");
	printlnf(g, "%s init(", access);
	indent(g);
	println(g, "fakeChannel: FakeChannel = FakeChannel(),");
	println(g, "defaultCallOptions callOptions: CallOptions = CallOptions(),");
	printlnf(g, "interceptors: %s? = nil", CLIENT_INTERCEPTOR_PROTOCOL_NAME(g));
	outdent(g);
	println(g, ") {");
	indent(g);
	println(g, "self.fakeChannel = fakeChannel");
	println(g, "self.defaultCallOptions = callOptions");
	println(g, "self.interceptors = interceptors");
	outdent(g);
	println(g, "}");
	/* printFakeResponseStreams */
	for (int i = 0; i < g->service->methods.n; i++) {
		println(g, "");
		set_method(g, g->service->methods.v[i]);
		bool is_unary = g->stream_type == UNARY || g->stream_type == CLIENT_STREAMING;
		print_response_stream(g, is_unary);
		println(g, "");
		print_enqueue_unary_response(g, is_unary);
		println(g, "");
		print_has_response_stream_enqueued(g);
	}
	outdent(g);
	println(g, "}"); /* end class */
}

static void print_client(Generator *g)
{
	if (g->options->generate_client) {
		println(g, "");
		print_service_client_protocol(g);
		println(g, "");
		print_client_protocol_extension(g);
		println(g, "");
		print_class_backed_service_client_implementation(g);
		println(g, "");
		print_struct_backed_service_client_implementation(g);
		println(g, "");
		print_async_service_client_protocol(g);
		println(g, "");
		print_async_client_protocol_extension(g);
		println(g, "");
		print_async_client_protocol_safe_wrappers_extension(g);
		println(g, "");
		print_async_service_client_implementation(g);
		println(g, "");
		/* Both implementations share definitions for interceptors and metadata. */
		print_service_client_interceptor_factory_protocol(g);
		println(g, "");
		print_metadata(g, false);
	}
	if (g->options->generate_test_client) {
		println(g, "");
		print_test_client(g);
	}
}
