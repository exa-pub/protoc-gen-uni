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

/* Port of Internal/StructuredSwift+Client.swift (grpc-swift-2 2.4.4). */
#include "structured_swift.h"

static const char handle_response_doc[] =
	"///   - handleResponse: A closure which handles the response and returns its result to\n"
	"///       the caller. Returning from the closure will cancel the RPC if it hasn't\n"
	"///       already finished.\n"
	"/// - Returns: The result of `handleResponse`.";

/* { response in try response.message } */
static GcExpr *default_client_unary_response_handler(void)
{
	GcExpr *body = gc_keyword(GC_TRY, gc_dot(gc_pattern("response"), "message"));
	return gc_closure(gc_vec("response", NULL), gc_blocks(gc_vec(gc_block(NULL, NULL, body), NULL)));
}

/* onResponse handleResponse: @Sendable @escaping (GRPCCore.<Response><Output>) async throws -> Result */
static GcParam *on_response(const GcMethod *m, bool with_default, const GcNamer *namer)
{
	GcType *type = gc_closure_type(gc_client_response(namer, m->output_type, m->is_output_streaming), "Result");
	return gc_param("onResponse", "handleResponse", type,
			with_default && !m->is_output_streaming ? default_client_unary_response_handler() : NULL);
}

/* func <Name><Result>(...) async throws -> Result where Result: Sendable */
static GcSignature result_signature(GcAccess access, const GcMethod *m, Vec parameters)
{
	return (GcSignature){.access = access, .name = m->name.function_name, .generics = gc_vec(gc_member("Result"), NULL),
			     .parameters = parameters, .keywords = GC_KW_ASYNC_THROWS, .return_type = gc_pattern("Result"),
			     .where = gc_where("Result", "Sendable")};
}

static void push_serializers(Vec *parameters, const GcMethod *m, const GcNamer *namer)
{
	vec_push(parameters, gc_param("serializer", NULL,
				      gc_type(GT_SOME, gc_grpc_core_generic(namer, "MessageSerializer", m->input_type)), NULL));
	vec_push(parameters, gc_param("deserializer", NULL,
				      gc_type(GT_SOME, gc_grpc_core_generic(namer, "MessageDeserializer", m->output_type)),
				      NULL));
}

/* FunctionSignatureDescription.clientMethod */
static GcSignature client_method_signature(GcAccess access, const GcMethod *m, bool include_defaults,
					   bool include_serializers, const GcNamer *namer)
{
	Vec parameters = gc_vec(gc_param("request", NULL, gc_client_request(namer, m->input_type, m->is_input_streaming), NULL),
				NULL);
	if (include_serializers)
		push_serializers(&parameters, m, namer);
	vec_push(&parameters, gc_param("options", NULL, gc_grpc_core(namer, "CallOptions"),
				       include_defaults ? gc_dot(NULL, "defaults") : NULL));
	vec_push(&parameters, on_response(m, include_defaults, namer));
	return result_signature(access, m, parameters);
}

static GcExpr *self_dot(const char *name) { return gc_dot(gc_pattern("self"), name); }

/* try await <called>(<label>: <label>, ...) for argument labels that name the same identifiers,
 * except onResponse: handleResponse */
static GcExpr *try_await_call(GcExpr *called, Vec args)
{
	vec_push(&args, gc_arg("options", gc_pattern("options")));
	vec_push(&args, gc_arg("onResponse", gc_pattern("handleResponse")));
	return gc_keyword(GC_TRY, gc_keyword(GC_AWAIT, gc_call(called, args)));
}

static GcComment *pre_formatted(Str docs) { return gc_comment(GC_PRE_FORMATTED, docs); }

/* docs(for:serializers:) */
static Str docs(const GcMethod *m, bool include_serializers)
{
	Buf b = {0};
	buf_adds(&b, "/// - Parameters:\n///   - request: ");
	buf_adds(&b, m->is_input_streaming ? xcat("A streaming request producing `", m->input_type, "` messages.")
					   : xcat("A request containing a single `", m->input_type, "` message."));
	buf_addc(&b, '\n');
	if (include_serializers)
		buf_adds(&b, xcat("///   - serializer: A serializer for `", m->input_type, "` messages.\n",
				  "///   - deserializer: A deserializer for `", m->output_type, "` messages.\n"));
	buf_adds(&b, "///   - options: Options to apply to this RPC.\n");
	buf_adds(&b, handle_response_doc);
	return gc_docs_interpose(m->documentation, str_c(xcat("/// Call the \"", m->name.identifying_name, "\" method.")),
				 buf_view(&b));
}

/* explodedDocs(for:) */
static Str exploded_docs(const GcMethod *m)
{
	Buf b = {0};
	buf_adds(&b, "/// - Parameters:");
	if (!m->is_input_streaming)
		buf_adds(&b, "\n///   - message: request message to send.");
	buf_adds(&b, "\n///   - metadata: Additional metadata to send, defaults to empty."
		     "\n///   - options: Options to apply to this RPC, defaults to `.defaults`.");
	if (m->is_input_streaming)
		buf_adds(&b, "\n///   - producer: A closure producing request messages to send to the server. Returning"
			     "\n///       from the closure closes the request stream.");
	buf_addc(&b, '\n');
	buf_adds(&b, handle_response_doc);
	return gc_docs_interpose(m->documentation, str_c(xcat("/// Call the \"", m->name.identifying_name, "\" method.")),
				 buf_view(&b));
}

/* protocol <Name>: Sendable { func foo<Result>(...) async throws -> Result where Result: Sendable } */
COLD GcDecl *gc_client_protocol(GcAccess access, const char *name, const GcService *s, const GcNamer *namer)
{
	GcDecl *p = gc_decl(GD_PROTOCOL);
	p->access = access;
	p->name = name;
	p->conformances = gc_vec("Sendable", NULL);
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		vec_push(&p->members, gc_commentable(pre_formatted(docs(m, true)),
						     gc_function(client_method_signature(GC_NONE, m, false, true, namer), NULL)));
	}
	return p;
}

/* extension <Name> { <clientMethodWithDefaults>... } */
COLD GcDecl *gc_client_method_signature_with_defaults(GcAccess access, const char *name, const GcService *s,
						      const GcNamer *namer, const GcRequest *request)
{
	Vec declarations = {0};
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		Vec args = gc_vec(gc_arg("request", gc_pattern("request")),
				  gc_arg("serializer", gc_pattern(request->make_serializer(request->ctx, m->input_type))),
				  gc_arg("deserializer", gc_pattern(request->make_deserializer(request->ctx, m->output_type))),
				  NULL);
		Vec *body = gc_blocks(gc_vec(gc_block(NULL, NULL, try_await_call(self_dot(m->name.function_name), args)), NULL));
		GcDecl *f = gc_function(client_method_signature(access, m, true, false, namer), body);
		vec_push(&declarations, gc_commentable(pre_formatted(docs(m, false)), f));
	}
	return gc_extension(name, declarations);
}

/* func foo<Result>(_ message: <Input>, metadata: GRPCCore.Metadata = [:], options:
 * GRPCCore.CallOptions = .defaults, ...) async throws -> Result where Result: Sendable { ... } */
static GcDecl *client_method_exploded(GcAccess access, const GcMethod *m, const GcNamer *namer)
{
	const char *input = m->input_type;
	bool streaming = m->is_input_streaming;
	Vec parameters = {0};
	if (!streaming)
		vec_push(&parameters, gc_param("_", "message", gc_member(input), NULL));
	vec_push(&parameters, gc_param("metadata", NULL, gc_grpc_core(namer, "Metadata"), gc_empty_dictionary()));
	vec_push(&parameters, gc_param("options", NULL, gc_grpc_core(namer, "CallOptions"), gc_dot(NULL, "defaults")));
	if (streaming)
		vec_push(&parameters, gc_param("requestProducer", "producer",
					       gc_closure_type(gc_grpc_core_generic(namer, "RPCWriter", input), "Void"), NULL));
	vec_push(&parameters, on_response(m, true, namer));

	Vec args = streaming ? gc_vec(gc_arg("metadata", gc_pattern("metadata")), gc_arg("producer", gc_pattern("producer")), NULL)
			     : gc_vec(gc_arg("message", gc_pattern("message")), gc_arg("metadata", gc_pattern("metadata")), NULL);
	GcDecl *request = gc_variable(GC_NONE, false, "request", NULL,
				      gc_call(gc_identifier_type(gc_client_request(namer, input, streaming)), args));
	GcExpr *ret = gc_keyword(GC_RETURN, try_await_call(self_dot(m->name.function_name),
							   gc_vec(gc_arg("request", gc_pattern("request")), NULL)));
	return gc_function(result_signature(access, m, parameters),
			   gc_blocks(gc_vec(gc_block(NULL, request, NULL), gc_block(NULL, NULL, ret), NULL)));
}

/* extension <Name> { <clientMethodExploded>... } */
COLD GcDecl *gc_exploded_client_methods(GcAccess access, const char *on, const GcService *s, const GcNamer *namer)
{
	Vec declarations = {0};
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		vec_push(&declarations, gc_commentable(pre_formatted(exploded_docs(m)), client_method_exploded(access, m, namer)));
	}
	return gc_extension(on, declarations);
}

/* FunctionDescription.clientMethod: the method of the Client struct calling self.client.<rpc>. */
static GcDecl *client_method(GcAccess access, const GcMethod *m, const char *service_enum, const GcNamer *namer)
{
	static const char *const underlying[] = {"unary", "serverStreaming", "clientStreaming", "bidirectionalStreaming"};
	Vec parameters = gc_vec(gc_param("request", NULL, gc_client_request(namer, m->input_type, m->is_input_streaming), NULL),
				NULL);
	push_serializers(&parameters, m, namer);
	vec_push(&parameters, gc_param("options", NULL, gc_grpc_core(namer, "CallOptions"), gc_dot(NULL, "defaults")));
	vec_push(&parameters, on_response(m, true, namer));
	GcExpr *descriptor = gc_dot(gc_dot(gc_dot(gc_pattern(service_enum), "Method"), m->name.type_name), "descriptor");
	Vec args = gc_vec(gc_arg("request", gc_pattern("request")), gc_arg("descriptor", descriptor),
			  gc_arg("serializer", gc_pattern("serializer")), gc_arg("deserializer", gc_pattern("deserializer")),
			  NULL);
	GcExpr *call = try_await_call(
		gc_dot(self_dot("client"), underlying[m->is_input_streaming * 2 + m->is_output_streaming]), args);
	return gc_function(result_signature(access, m, parameters), gc_blocks(gc_vec(gc_block(NULL, NULL, call), NULL)));
}

/* struct <Name><Transport>: <ClientProtocol> where Transport: GRPCCore.ClientTransport { ... } */
COLD GcDecl *gc_client_struct(GcAccess access, const char *name, const char *service_enum, const char *client_protocol,
			      const GcService *s, const GcNamer *namer)
{
	GcDecl *st = gc_decl(GD_STRUCT);
	st->access = access;
	st->name = name;
	st->generics = gc_vec(gc_member("Transport"), NULL);
	st->conformances = gc_vec((void *)client_protocol, NULL);
	st->where = gc_where("Transport", gc_literal_namespaced_type(namer, "ClientTransport"));
	vec_push(&st->members, gc_variable(GC_PRIVATE, false, "client",
					   gc_grpc_core_generic(namer, "GRPCClient", "Transport"), NULL));
	const char *grpc_client = gc_literal_namespaced_type(namer, "GRPCClient");
	char *init_docs = xcat("/// Creates a new client wrapping the provided `", grpc_client, "`.\n"
			       "///\n"
			       "/// - Parameters:\n"
			       "///   - client: A `", grpc_client, "` providing a communication channel to the service.");
	GcSignature sig = {.access = access,
			   .parameters = gc_vec(gc_param("wrapping", "client",
							 gc_grpc_core_generic(namer, "GRPCClient", "Transport"), NULL),
						NULL)};
	GcExpr *assign = gc_assign(self_dot("client"), gc_pattern("client"));
	vec_push(&st->members, gc_commentable(pre_formatted(str_c(init_docs)),
					      gc_function(sig, gc_blocks(gc_vec(gc_block(NULL, NULL, assign), NULL)))));
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		vec_push(&st->members,
			 gc_commentable(pre_formatted(docs(m, true)), client_method(access, m, service_enum, namer)));
	}
	return st;
}
