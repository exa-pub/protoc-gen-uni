/*
 * Copyright 2024, gRPC Authors All rights reserved.
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

/* Port of Internal/StructuredSwift+Server.swift (grpc-swift-2 2.4.4). */
#include "structured_swift.h"

static const char throws_doc[] =
	"/// - Throws: Any error which occurred during the processing of the request. Thrown errors\n"
	"///     of type `RPCError` are mapped to appropriate statuses. All other errors are converted\n"
	"///     to an internal error.";

static Str handle_summary(const GcMethod *m) { return str_c(xcat("/// Handle the \"", m->name.identifying_name, "\" method.")); }

static GcSignature signature(GcAccess access, const char *name, Vec parameters, GcExpr *return_type)
{
	return (GcSignature){.access = access, .name = name, .parameters = parameters, .keywords = GC_KW_ASYNC_THROWS,
			     .return_type = return_type};
}

/* func <Method>(request: GRPCCore.ServerRequest<Input>, context: GRPCCore.ServerContext) async
 * throws -> GRPCCore.ServerResponse<Output> */
static GcSignature server_method(GcAccess access, const char *name, const char *input, const char *output,
				 bool streaming_input, bool streaming_output, const GcNamer *namer)
{
	return signature(access, name,
			 gc_vec(gc_param("request", NULL, gc_server_request(namer, input, streaming_input), NULL),
				gc_param("context", NULL, gc_grpc_core(namer, "ServerContext"), NULL), NULL),
			 gc_identifier_type(gc_server_response(namer, output, streaming_output)));
}

static GcDecl *protocol(GcAccess access, const char *name, const char *conformance)
{
	GcDecl *p = gc_decl(GD_PROTOCOL);
	p->access = access;
	p->name = name;
	p->conformances = gc_vec((void *)conformance, NULL);
	return p;
}

/* protocol <Name>: GRPCCore.RegistrableRPCService { ... } */
COLD GcDecl *gc_streaming_service_protocol(GcAccess access, const char *name, const GcService *s, const GcNamer *namer)
{
	GcDecl *p = protocol(access, name, gc_literal_namespaced_type(namer, "RegistrableRPCService"));
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		char *parameters = xcat("/// - Parameters:\n"
					"///   - request: A streaming request of `", m->input_type, "` messages.\n"
					"///   - context: Context providing information about the RPC.\n", throws_doc,
					"\n/// - Returns: A streaming response of `", m->output_type, "` messages.");
		Str docs = gc_docs_interpose(m->documentation, handle_summary(m), str_c(parameters));
		GcSignature sig = server_method(GC_NONE, m->name.function_name, m->input_type, m->output_type, true, true, namer);
		vec_push(&p->members, gc_commentable(gc_comment(GC_PRE_FORMATTED, docs), gc_function(sig, NULL)));
	}
	return p;
}

/* self.<name>(request: request, context: context) */
static GcExpr *call_on_self(GcExpr *called, GcExpr *request)
{
	return gc_call(called, gc_vec(gc_arg("request", request), gc_arg("context", gc_pattern("context")), NULL));
}

static GcExpr *self_dot(const char *name) { return gc_dot(gc_pattern("self"), name); }

/* router.registerHandler(forMethod:deserializer:serializer:handler:) */
static GcExpr *register_with_router(const char *service_namespace, const GcMethod *m, const GcRequest *request)
{
	GcExpr *invoke = gc_keyword(GC_TRY, gc_keyword(GC_AWAIT, call_on_self(self_dot(m->name.function_name),
									     gc_pattern("request"))));
	GcExpr *handler = gc_closure(gc_vec("request", "context", NULL), gc_blocks(gc_vec(gc_block(NULL, NULL, invoke), NULL)));
	return gc_call(
		gc_dot(gc_pattern("router"), "registerHandler"),
		gc_vec(gc_arg("forMethod", gc_pattern(xcat(service_namespace, ".Method.", m->name.type_name, ".descriptor"))),
		       gc_arg("deserializer", gc_pattern(request->make_deserializer(request->ctx, m->input_type))),
		       gc_arg("serializer", gc_pattern(request->make_serializer(request->ctx, m->output_type))),
		       gc_arg("handler", handler), NULL));
}

/* extension <On> { func registerMethods<Transport>(with router: inout GRPCCore.RPCRouter<Transport>)
 * where Transport: GRPCCore.ServerTransport { ... } } */
COLD GcDecl *gc_registrable_rpc_service_default_implementation(GcAccess access, const char *on, const char *service_namespace,
							       const GcService *s, const GcNamer *namer,
							       const GcRequest *request)
{
	GcParam *router = gc_param("with", "router", gc_grpc_core_generic(namer, "RPCRouter", "Transport"), NULL);
	router->is_inout = true;
	GcSignature sig = {.access = access, .name = "registerMethods", .generics = gc_vec(gc_member("Transport"), NULL),
			   .parameters = gc_vec(router, NULL),
			   .where = gc_where("Transport", gc_literal_namespaced_type(namer, "ServerTransport"))};
	Vec *body = gc_blocks((Vec){0});
	for (int i = 0; i < s->n_methods; i++)
		vec_push(body, gc_block(NULL, NULL, register_with_router(service_namespace, &s->methods[i], request)));
	return gc_extension(on, gc_vec(gc_function(sig, body), NULL));
}

/* protocol <Name>: <StreamingProtocol> { ... } */
COLD GcDecl *gc_service_protocol(GcAccess access, const char *name, const char *streaming_protocol, const GcService *s,
				 const GcNamer *namer)
{
	GcDecl *p = protocol(access, name, streaming_protocol);
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		char *request = m->is_input_streaming ? xcat("A streaming request of `", m->input_type, "` messages.")
						      : xcat("A request containing a single `", m->input_type, "` message.");
		char *returns = m->is_output_streaming ? xcat("A streaming response of `", m->output_type, "` messages.")
						       : xcat("A response containing a single `", m->output_type, "` message.");
		char *parameters = xcat("/// - Parameters:\n///   - request: ", request,
					"\n///   - context: Context providing information about the RPC.\n", throws_doc,
					"\n/// - Returns: ", returns);
		Str docs = gc_docs_interpose(m->documentation, handle_summary(m), str_c(parameters));
		GcSignature sig = server_method(GC_NONE, m->name.function_name, m->input_type, m->output_type,
						m->is_input_streaming, m->is_output_streaming, namer);
		vec_push(&p->members, gc_commentable(gc_comment(GC_PRE_FORMATTED, docs), gc_function(sig, NULL)));
	}
	return p;
}

/* func <Name>(request: GRPCCore.StreamingServerRequest<Input>, context: GRPCCore.ServerContext)
 * async throws -> GRPCCore.StreamingServerResponse<Output> { let response = ...; return ... } */
static GcDecl *server_streaming_method_calling_method(GcAccess access, const GcMethod *m, const GcNamer *namer)
{
	const char *name = m->name.function_name;
	GcSignature sig = server_method(access, name, m->input_type, m->output_type, true, true, namer);
	GcExpr *request = gc_pattern("request");
	if (!m->is_input_streaming)
		request = gc_call(gc_identifier_type(gc_server_request(namer, NULL, false)),
				  gc_vec(gc_arg("stream", gc_pattern("request")), NULL));
	GcExpr *call = call_on_self(self_dot(name), request);
	GcDecl *response = gc_variable(GC_NONE, false, "response", NULL, gc_keyword(GC_TRY, gc_keyword(GC_AWAIT, call)));
	GcExpr *result = gc_pattern("response");
	if (!m->is_output_streaming)
		result = gc_call(gc_identifier_type(gc_server_response(namer, NULL, true)),
				 gc_vec(gc_arg("single", gc_pattern("response")), NULL));
	return gc_function(sig, gc_blocks(gc_vec(gc_block(NULL, response, NULL),
						 gc_block(NULL, NULL, gc_keyword(GC_RETURN, result)), NULL)));
}

/* extension <On> { <server_streaming_method_calling_method>... } */
COLD GcDecl *gc_streaming_service_protocol_default_implementation(GcAccess access, const char *on, const GcService *s,
								  const GcNamer *namer)
{
	Vec declarations = {0};
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		/* Bidirectional streaming methods don't need a default implementation as their
		 * signatures match across the two protocols. */
		if (m->is_input_streaming && m->is_output_streaming)
			continue;
		vec_push(&declarations, server_streaming_method_calling_method(access, m, namer));
	}
	return gc_extension(on, declarations);
}

/* func <Name>(request: <Input>, [response: GRPCCore.RPCWriter<Output>,] context:
 * GRPCCore.ServerContext) async throws [-> <Output>] */
static GcSignature simple_server_method(const GcMethod *m, const GcNamer *namer)
{
	const char *input = m->input_type, *output = m->output_type;
	Vec parameters = gc_vec(gc_param("request", NULL,
					 m->is_input_streaming ? gc_rpc_async_sequence(namer, input) : gc_member(input), NULL),
				NULL);
	if (m->is_output_streaming)
		vec_push(&parameters, gc_param("response", NULL, gc_grpc_core_generic(namer, "RPCWriter", output), NULL));
	vec_push(&parameters, gc_param("context", NULL, gc_grpc_core(namer, "ServerContext"), NULL));
	return signature(GC_NONE, m->name.function_name, parameters, m->is_output_streaming ? NULL : gc_pattern(output));
}

/* protocol SimpleServiceProtocol: <ServiceProtocol> { ... } */
COLD GcDecl *gc_simple_service_protocol(GcAccess access, const char *name, const char *service_protocol, const GcService *s,
					const GcNamer *namer)
{
	GcDecl *p = protocol(access, name, service_protocol);
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		Buf parameters = {0};
		buf_adds(&parameters, "/// - Parameters:\n///   - request: ");
		buf_adds(&parameters, m->is_input_streaming ? xcat("A stream of `", m->input_type, "` messages.")
							    : xcat("A `", m->input_type, "` message."));
		if (m->is_output_streaming)
			buf_adds(&parameters, xcat("\n///   - response: A response stream of `", m->output_type, "` messages."));
		buf_adds(&parameters, "\n///   - context: Context providing information about the RPC.\n");
		buf_adds(&parameters, throws_doc);
		if (!m->is_output_streaming)
			buf_adds(&parameters, xcat("\n/// - Returns: A `", m->output_type, "` to respond with."));
		Str docs = gc_docs_interpose(m->documentation, handle_summary(m), buf_view(&parameters));
		vec_push(&p->members,
			 gc_commentable(gc_comment(GC_PRE_FORMATTED, docs), gc_function(simple_server_method(m, namer), NULL)));
	}
	return p;
}

/* try await self.<Name>(request: request.message, [response: writer,] context: context) */
static GcExpr *service_method_calling_simple_method(const GcMethod *m)
{
	Vec args = gc_vec(gc_arg("request", gc_dot(gc_pattern("request"), m->is_input_streaming ? "messages" : "message")),
			  NULL);
	if (m->is_output_streaming)
		vec_push(&args, gc_arg("response", gc_pattern("writer")));
	vec_push(&args, gc_arg("context", gc_pattern("context")));
	return gc_call(gc_keyword(GC_TRY, gc_keyword(GC_AWAIT, self_dot(m->name.function_name))), args);
}

/* func <Name>(request: GRPCCore.ServerRequest<Input>, context: GRPCCore.ServerContext) async throws
 * -> GRPCCore.ServerResponse<Output> { return GRPCCore.ServerResponse<Output>(...) } */
static GcDecl *service_protocol_default_implementation(GcAccess access, const GcMethod *m, const GcNamer *namer)
{
	Vec args;
	if (m->is_output_streaming) {
		Vec *body = gc_blocks(gc_vec(gc_block(NULL, NULL, service_method_calling_simple_method(m)),
					     gc_block(NULL, NULL, gc_keyword(GC_RETURN, gc_empty_dictionary())), NULL));
		args = gc_vec(gc_arg("metadata", gc_empty_dictionary()),
			      gc_arg("producer", gc_closure(gc_vec("writer", NULL), body)), NULL);
	} else {
		args = gc_vec(gc_arg("message", service_method_calling_simple_method(m)),
			      gc_arg("metadata", gc_empty_dictionary()), NULL);
	}
	GcExpr *ret = gc_call(
		gc_keyword(GC_RETURN, gc_identifier_type(gc_server_response(namer, m->output_type, m->is_output_streaming))),
		args);
	GcSignature sig = server_method(access, m->name.function_name, m->input_type, m->output_type, m->is_input_streaming,
					m->is_output_streaming, namer);
	return gc_function(sig, gc_blocks(gc_vec(gc_block(NULL, NULL, ret), NULL)));
}

/* extension <On> { <service_protocol_default_implementation>... } */
COLD GcDecl *gc_service_protocol_default_implementation(GcAccess access, const char *on, const GcService *s,
							const GcNamer *namer)
{
	Vec declarations = {0};
	for (int i = 0; i < s->n_methods; i++)
		vec_push(&declarations, service_protocol_default_implementation(access, &s->methods[i], namer));
	return gc_extension(on, declarations);
}
