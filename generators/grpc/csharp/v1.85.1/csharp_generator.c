/*
 *
 * Copyright 2015 gRPC authors.
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
 *
 */

/* Port of src/compiler/csharp_generator.{h,cc} and csharp_generator_helpers.h of grpc v1.85.1.
 * The GRPC_CUSTOM_CSHARP_* macros of config_protobuf.h are the C# names of libprotoc
 * (csharp_names.h). Comments are Str: they may contain NUL, which upstream prints. */

enum { SERVICEOPT_DEPRECATED = 33, METHODOPT_DEPRECATED = 33 };

/* ---- csharp_generator_helpers.h ---- */

/* ServicesFilename: false with *error set (or left empty) when GetOutputFile gives "". */
static COLD bool csg_services_filename(const LpFile *file, const char *file_suffix, bool base_namespace_present,
				       const char *base_namespace, char **out_file, char **error)
{
	/* Support for base_namespace option is **experimental**.
	 *
	 * If base_namespace is provided then slightly different name mangling is used to generate
	 * the service file name. This is because this uses common code with protoc. For most file
	 * names this will not make a difference (only files with punctuation or numbers in the
	 * name.) Otherwise the behavior remains the same as before. */
	if (!base_namespace_present) {
		*out_file = xcat(grpc_file_name_in_upper_camel(file, false), file_suffix);
	} else {
		*out_file = lp_csharp_output_file(file, file_suffix, true, base_namespace, error);
		if (!**out_file)
			return false;
	}
	return true;
}

/* GetCsharpComments: leading or trailing comments, lines start with "// ". Leading detached
 * comments are put in front of leading comments. */
static Str csg_get_csharp_comments(const LpSourceLocations *locs, const void *desc, bool leading)
{
	return grpc_get_prefixed_comments(locs, desc, leading, "//");
}

/* ---- csharp_generator.cc ---- */

/* Appends s to b with '&' and '<' XML-escaped (StringReplace(..., true) twice). */
static void csg_xml_escape(Buf *b, Str s)
{
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] == '&')
			buf_adds(b, "&amp;");
		else if (s.p[i] == '<')
			buf_adds(b, "&lt;");
		else
			buf_addc(b, s.p[i]);
	}
}

/* This function is a massaged version of
 * https://github.com/protocolbuffers/protobuf/blob/master/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc */
static bool csg_generate_doc_comment_body_impl(LpPrinter *printer, const LpSourceLocation *location)
{
	Str comments = location->leading_comments.n ? location->leading_comments : location->trailing_comments;
	if (!comments.n)
		return false;
	/* XML escaping... no need for apostrophes etc as the whole text is going to be a child node
	 * of a summary element, not part of an attribute. */
	Buf escaped = {0};
	csg_xml_escape(&escaped, comments);

	Vec lines = {0};
	grpc_split(buf_view(&escaped), &lines);
	/* TODO: We really should work out which part to put in the summary and which to put in the
	 * remarks... but that needs to be part of a bigger effort to understand the markdown better
	 * anyway. */
	lp_print(printer, "/// <summary>\n", NULL);
	bool last_was_empty = false;
	/* We squash multiple blank lines down to one, and remove any trailing blank lines. We need to
	 * preserve the blank lines themselves, as this is relevant in the markdown. Note that we
	 * can't remove leading or trailing whitespace as *that's* relevant in markdown too. (We don't
	 * skip "just whitespace" lines, either.) */
	for (int i = 0; i < lines.n; i++) {
		Str line = *(Str *)lines.v[i];
		if (!line.n) {
			last_was_empty = true;
		} else {
			if (last_was_empty)
				lp_print(printer, "///\n", NULL);
			last_was_empty = false;
			/* Print("///$line$\n", "line", line): the value is written as is (NUL included).
			 * If the comment has an extra slash at the start then this can cause the C#
			 * compiler to complain when generating the XML documentation
			 * (https://github.com/grpc/grpc/issues/35905). */
			lp_print(printer, "///", NULL);
			if (line.p[0] == '/') {
				lp_write_raw(printer, STR_LIT("&#x2F;"));
				line.p++, line.n--;
			}
			lp_write_raw(printer, line);
			lp_print(printer, "\n", NULL);
		}
	}
	lp_print(printer, "/// </summary>\n", NULL);
	return true;
}

static void csg_generate_generated_code_attribute(LpPrinter *printer)
{
	/* Mark the code as generated using the [GeneratedCode] attribute. */
	lp_print(printer, "[global::System.CodeDom.Compiler.GeneratedCode(\"grpc_csharp_plugin\", null)]\n", NULL);
}

static void csg_generate_obsolete_attribute(LpPrinter *printer, bool is_deprecated)
{
	/* Mark the code deprecated using the [ObsoleteAttribute] attribute. */
	if (is_deprecated)
		lp_print(printer, "[global::System.ObsoleteAttribute]\n", NULL);
}

static bool csg_generate_doc_comment_body(LpPrinter *printer, const LpSourceLocations *locs, const void *descriptor)
{
	LpSourceLocation location;
	if (!lp_get_source_location(locs, descriptor, &location))
		return false;
	return csg_generate_doc_comment_body_impl(printer, &location);
}

static void csg_generate_doc_comment_server_method(LpPrinter *printer, const LpSourceLocations *locs,
						   const LpMethod *method)
{
	if (!csg_generate_doc_comment_body(printer, locs, method))
		return;
	if (method->client_streaming)
		lp_print(printer, "/// <param name=\"requestStream\">Used for reading requests from the client.</param>\n",
			 NULL);
	else
		lp_print(printer, "/// <param name=\"request\">The request received from the client.</param>\n", NULL);
	if (method->server_streaming)
		lp_print(printer,
			 "/// <param name=\"responseStream\">Used for sending responses back to the client.</param>\n",
			 NULL);
	lp_print(printer,
		 "/// <param name=\"context\">The context of the server-side call handler being invoked.</param>\n",
		 NULL);
	if (method->server_streaming)
		lp_print(printer, "/// <returns>A task indicating completion of the handler.</returns>\n", NULL);
	else
		lp_print(printer,
			 "/// <returns>The response to send back to the client (wrapped by a task).</returns>\n",
			 NULL);
}

static void csg_generate_doc_comment_client_method(LpPrinter *printer, const LpSourceLocations *locs,
						   const LpMethod *method, bool is_sync, bool use_call_options)
{
	if (!csg_generate_doc_comment_body(printer, locs, method))
		return;
	if (!method->client_streaming)
		lp_print(printer, "/// <param name=\"request\">The request to send to the server.</param>\n", NULL);
	if (!use_call_options) {
		lp_print(printer,
			 "/// <param name=\"headers\">The initial metadata to send with the call. This parameter "
			 "is optional.</param>\n",
			 NULL);
		lp_print(printer,
			 "/// <param name=\"deadline\">An optional deadline for the call. The call will be "
			 "cancelled if deadline is hit.</param>\n",
			 NULL);
		lp_print(printer,
			 "/// <param name=\"cancellationToken\">An optional token for canceling the "
			 "call.</param>\n",
			 NULL);
	} else {
		lp_print(printer, "/// <param name=\"options\">The options for the call.</param>\n", NULL);
	}
	if (is_sync)
		lp_print(printer, "/// <returns>The response received from the server.</returns>\n", NULL);
	else
		lp_print(printer, "/// <returns>The call object.</returns>\n", NULL);
}

static const char *csg_get_service_class_name(const LpService *service) { return service->name; }

static char *csg_get_client_class_name(const LpService *service) { return xcat(service->name, "Client"); }

static char *csg_get_server_class_name(const LpService *service) { return xcat(service->name, "Base"); }

static const char *csg_get_csharp_method_type(const LpMethod *method)
{
	if (method->client_streaming)
		return method->server_streaming ? "grpc::MethodType.DuplexStreaming"
						: "grpc::MethodType.ClientStreaming";
	return method->server_streaming ? "grpc::MethodType.ServerStreaming" : "grpc::MethodType.Unary";
}

static const char *csg_get_csharp_server_method_type(const LpMethod *method)
{
	if (method->client_streaming)
		return method->server_streaming ? "grpc::DuplexStreamingServerMethod"
						: "grpc::ClientStreamingServerMethod";
	return method->server_streaming ? "grpc::ServerStreamingServerMethod" : "grpc::UnaryServerMethod";
}

static const char *csg_get_service_name_field_name(void) { return "__ServiceName"; }

static char *csg_get_marshaller_field_name(const LpMessage *message)
{
	return xcat("__Marshaller_", str_replace_all(message->full_name, ".", "_"));
}

static char *csg_get_method_field_name(const LpMethod *method) { return xcat("__Method_", method->name); }

static char *csg_get_method_request_param_maybe(const LpMethod *method, bool invocation_param)
{
	if (method->client_streaming)
		return "";
	if (invocation_param)
		return "request, ";
	return xcat(lp_csharp_class_name_message(method->input_type), " request, ");
}

static const char *csg_get_access_level(bool internal_access) { return internal_access ? "internal" : "public"; }

static char *csg_get_method_return_type_client(const LpMethod *method)
{
	char *in = lp_csharp_class_name_message(method->input_type);
	char *out = lp_csharp_class_name_message(method->output_type);
	if (method->client_streaming) {
		if (method->server_streaming)
			return xcat("grpc::AsyncDuplexStreamingCall<", in, ", ", out, ">");
		return xcat("grpc::AsyncClientStreamingCall<", in, ", ", out, ">");
	}
	if (method->server_streaming)
		return xcat("grpc::AsyncServerStreamingCall<", out, ">");
	return xcat("grpc::AsyncUnaryCall<", out, ">");
}

static char *csg_get_method_request_param_server(const LpMethod *method)
{
	char *in = lp_csharp_class_name_message(method->input_type);
	if (method->client_streaming)
		return xcat("grpc::IAsyncStreamReader<", in, "> requestStream");
	return xcat(in, " request");
}

static char *csg_get_method_return_type_server(const LpMethod *method)
{
	if (method->server_streaming)
		return "global::System.Threading.Tasks.Task";
	return xcat("global::System.Threading.Tasks.Task<", lp_csharp_class_name_message(method->output_type), ">");
}

static char *csg_get_method_response_stream_maybe(const LpMethod *method)
{
	if (method->server_streaming)
		return xcat(", grpc::IServerStreamWriter<", lp_csharp_class_name_message(method->output_type),
			    "> responseStream");
	return "";
}

/* GetUsedMessages: all messages used as input or output types, in order of first use (a set by
 * full name, unique in the pool). */
static Vec csg_get_used_messages(const LpService *service)
{
	Map descriptor_set = {0};
	Vec result = {0}; /* vector is to maintain stable ordering */
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		const LpMessage *types[2] = {method->input_type, method->output_type};
		for (int j = 0; j < 2; j++) {
			if (!map_has(&descriptor_set, types[j]->full_name)) {
				map_put(&descriptor_set, types[j]->full_name, (void *)types[j]);
				vec_push(&result, (void *)types[j]);
			}
		}
	}
	return result;
}

/* The method name of the server side, with "Async" appended (unless it ends with it) when
 * append_async_suffix. */
static const char *csg_server_method_name(const LpMethod *method, bool append_async_suffix)
{
	if (append_async_suffix && !has_suffix(method->name, "Async"))
		return xcat(method->name, "Async");
	return method->name;
}

static void csg_generate_marshaller_fields(LpPrinter *out, const LpService *service)
{
	Vec used_messages = csg_get_used_messages(service);
	if (used_messages.n != 0) {
		/* Generate static helper methods for serialization/deserialization */
		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "static void __Helper_SerializeMessage(global::Google.Protobuf.IMessage message, "
			 "grpc::SerializationContext context)\n"
			 "{\n",
			 NULL);
		lp_indent(out);
		lp_print(out,
			 "#if !GRPC_DISABLE_PROTOBUF_BUFFER_SERIALIZATION\n"
			 "if (message is global::Google.Protobuf.IBufferMessage)\n"
			 "{\n",
			 NULL);
		lp_indent(out);
		lp_print(out,
			 "context.SetPayloadLength(message.CalculateSize());\n"
			 "global::Google.Protobuf.MessageExtensions.WriteTo(message, context.GetBufferWriter());\n"
			 "context.Complete();\n"
			 "return;\n",
			 NULL);
		lp_outdent(out);
		lp_print(out,
			 "}\n"
			 "#endif\n",
			 NULL);
		lp_print(out, "context.Complete(global::Google.Protobuf.MessageExtensions.ToByteArray(message));\n",
			 NULL);
		lp_outdent(out);
		lp_print(out, "}\n\n", NULL);

		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "static class __Helper_MessageCache<T>\n"
			 "{\n",
			 NULL);
		lp_indent(out);
		lp_print(out,
			 "public static readonly bool IsBufferMessage = "
			 "global::System.Reflection.IntrospectionExtensions.GetTypeInfo(typeof("
			 "global::Google.Protobuf.IBufferMessage)).IsAssignableFrom(typeof(T));\n",
			 NULL);
		lp_outdent(out);
		lp_print(out, "}\n\n", NULL);

		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "static T __Helper_DeserializeMessage<T>(grpc::DeserializationContext context, "
			 "global::Google.Protobuf.MessageParser<T> parser) where T : "
			 "global::Google.Protobuf.IMessage<T>\n"
			 "{\n",
			 NULL);
		lp_indent(out);
		lp_print(out,
			 "#if !GRPC_DISABLE_PROTOBUF_BUFFER_SERIALIZATION\n"
			 "if (__Helper_MessageCache<T>.IsBufferMessage)\n"
			 "{\n",
			 NULL);
		lp_indent(out);
		lp_print(out, "return parser.ParseFrom(context.PayloadAsReadOnlySequence());\n", NULL);
		lp_outdent(out);
		lp_print(out,
			 "}\n"
			 "#endif\n",
			 NULL);
		lp_print(out, "return parser.ParseFrom(context.PayloadAsNewBuffer());\n", NULL);
		lp_outdent(out);
		lp_print(out, "}\n\n", NULL);
	}

	for (int i = 0; i < used_messages.n; i++) {
		const LpMessage *message = used_messages.v[i];
		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "static readonly grpc::Marshaller<$type$> $fieldname$ = "
			 "grpc::Marshallers.Create(__Helper_SerializeMessage, "
			 "context => __Helper_DeserializeMessage(context, $type$.Parser));\n",
			 "fieldname", csg_get_marshaller_field_name(message), "type",
			 lp_csharp_class_name_message(message), NULL);
	}
	lp_print(out, "\n", NULL);
}

static void csg_generate_static_method_field(LpPrinter *out, const LpMethod *method)
{
	csg_generate_generated_code_attribute(out);
	lp_print(out,
		 "static readonly grpc::Method<$request$, $response$> $fieldname$ = new "
		 "grpc::Method<$request$, $response$>(\n",
		 "fieldname", csg_get_method_field_name(method), "request",
		 lp_csharp_class_name_message(method->input_type), "response",
		 lp_csharp_class_name_message(method->output_type), NULL);
	lp_indent(out);
	lp_indent(out);
	lp_print(out, "$methodtype$,\n", "methodtype", csg_get_csharp_method_type(method), NULL);
	lp_print(out, "$servicenamefield$,\n", "servicenamefield", csg_get_service_name_field_name(), NULL);
	lp_print(out, "\"$methodname$\",\n", "methodname", method->name, NULL);
	lp_print(out, "$requestmarshaller$,\n", "requestmarshaller", csg_get_marshaller_field_name(method->input_type),
		 NULL);
	lp_print(out, "$responsemarshaller$);\n", "responsemarshaller",
		 csg_get_marshaller_field_name(method->output_type), NULL);
	lp_print(out, "\n", NULL);
	lp_outdent(out);
	lp_outdent(out);
}

static void csg_generate_service_descriptor_property(LpPrinter *out, const LpService *service)
{
	lp_print(out, "/// <summary>Service descriptor</summary>\n", NULL);
	lp_print(out, "public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor\n", NULL);
	lp_print(out, "{\n", NULL);
	lp_print(out, "  get { return $umbrella$.Descriptor.Services[$index$]; }\n", "umbrella",
		 lp_csharp_reflection_class_name(service->file), "index", xfmt("%d", service->index), NULL);
	lp_print(out, "}\n", NULL);
	lp_print(out, "\n", NULL);
}

static void csg_generate_server_class(LpPrinter *out, const LpSourceLocations *locs, const LpService *service,
				      bool append_async_suffix)
{
	lp_print(out, "/// <summary>Base class for server-side implementations of $servicename$</summary>\n",
		 "servicename", csg_get_service_class_name(service), NULL);
	csg_generate_obsolete_attribute(out, pb_int(service->options, SERVICEOPT_DEPRECATED, 0));
	lp_print(out, "[grpc::BindServiceMethod(typeof($classname$), \"BindService\")]\n", "classname",
		 csg_get_service_class_name(service), NULL);
	lp_print(out, "public abstract partial class $name$\n", "name", csg_get_server_class_name(service), NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		const char *method_name = csg_server_method_name(method, append_async_suffix);

		csg_generate_doc_comment_server_method(out, locs, method);
		csg_generate_obsolete_attribute(out, pb_int(method->options, METHODOPT_DEPRECATED, 0));
		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "public virtual $returntype$ $methodname$($request$$response_stream_maybe$, "
			 "grpc::ServerCallContext context)\n",
			 "methodname", method_name, "returntype", csg_get_method_return_type_server(method), "request",
			 csg_get_method_request_param_server(method), "response_stream_maybe",
			 csg_get_method_response_stream_maybe(method), NULL);
		lp_print(out, "{\n", NULL);
		lp_indent(out);
		lp_print(out, "throw new grpc::RpcException(new grpc::Status(grpc::StatusCode.Unimplemented, \"\"));\n",
			 NULL);
		lp_outdent(out);
		lp_print(out, "}\n\n", NULL);
	}
	lp_outdent(out);
	lp_print(out, "}\n", NULL);
	lp_print(out, "\n", NULL);
}

static void csg_generate_client_stub(LpPrinter *out, const LpSourceLocations *locs, const LpService *service)
{
	const char *name = csg_get_client_class_name(service);
	lp_print(out, "/// <summary>Client for $servicename$</summary>\n", "servicename",
		 csg_get_service_class_name(service), NULL);
	csg_generate_obsolete_attribute(out, pb_int(service->options, SERVICEOPT_DEPRECATED, 0));
	lp_print(out, "public partial class $name$ : grpc::ClientBase<$name$>\n", "name", name, NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);

	/* constructors */
	lp_print(out,
		 "/// <summary>Creates a new client for $servicename$</summary>\n"
		 "/// <param name=\"channel\">The channel to use to make remote calls.</param>\n",
		 "servicename", csg_get_service_class_name(service), NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "public $name$(grpc::ChannelBase channel) : base(channel)\n", "name", name, NULL);
	lp_print(out, "{\n", NULL);
	lp_print(out, "}\n", NULL);
	lp_print(out,
		 "/// <summary>Creates a new client for $servicename$ that uses a custom "
		 "<c>CallInvoker</c>.</summary>\n"
		 "/// <param name=\"callInvoker\">The callInvoker to use to make remote calls.</param>\n",
		 "servicename", csg_get_service_class_name(service), NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "public $name$(grpc::CallInvoker callInvoker) : base(callInvoker)\n", "name", name, NULL);
	lp_print(out, "{\n", NULL);
	lp_print(out, "}\n", NULL);
	lp_print(out, "/// <summary>Protected parameterless constructor to allow creation of test doubles.</summary>\n",
		 NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "protected $name$() : base()\n", "name", name, NULL);
	lp_print(out, "{\n", NULL);
	lp_print(out, "}\n", NULL);
	lp_print(out,
		 "/// <summary>Protected constructor to allow creation of configured clients.</summary>\n"
		 "/// <param name=\"configuration\">The client configuration.</param>\n",
		 NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "protected $name$(ClientBaseConfiguration configuration) : base(configuration)\n", "name", name,
		 NULL);
	lp_print(out, "{\n", NULL);
	lp_print(out, "}\n\n", NULL);

	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		const bool is_deprecated = pb_int(method->options, METHODOPT_DEPRECATED, 0);
		const bool unary = !method->client_streaming && !method->server_streaming;
		char *request = lp_csharp_class_name_message(method->input_type);
		char *response = lp_csharp_class_name_message(method->output_type);
		char *methodfield = csg_get_method_field_name(method);
		if (unary) {
			/* unary calls have an extra synchronous stub method */
			csg_generate_doc_comment_client_method(out, locs, method, true, false);
			csg_generate_obsolete_attribute(out, is_deprecated);
			csg_generate_generated_code_attribute(out);
			lp_print(out,
				 "public virtual $response$ $methodname$($request$ request, grpc::Metadata headers = "
				 "null, global::System.DateTime? deadline = null, "
				 "global::System.Threading.CancellationToken cancellationToken = "
				 "default(global::System.Threading.CancellationToken))\n",
				 "methodname", method->name, "request", request, "response", response, NULL);
			lp_print(out, "{\n", NULL);
			lp_indent(out);
			lp_print(out,
				 "return $methodname$(request, new grpc::CallOptions(headers, deadline, "
				 "cancellationToken));\n",
				 "methodname", method->name, NULL);
			lp_outdent(out);
			lp_print(out, "}\n", NULL);

			/* overload taking CallOptions as a param */
			csg_generate_doc_comment_client_method(out, locs, method, true, true);
			csg_generate_obsolete_attribute(out, is_deprecated);
			csg_generate_generated_code_attribute(out);
			lp_print(out, "public virtual $response$ $methodname$($request$ request, grpc::CallOptions options)\n",
				 "methodname", method->name, "request", request, "response", response, NULL);
			lp_print(out, "{\n", NULL);
			lp_indent(out);
			lp_print(out, "return CallInvoker.BlockingUnaryCall($methodfield$, null, options, request);\n",
				 "methodfield", methodfield, NULL);
			lp_outdent(out);
			lp_print(out, "}\n", NULL);
		}

		const char *method_name = method->name;
		if (unary)
			method_name = xcat(method_name, "Async"); /* prevent name clash with synchronous method. */
		csg_generate_doc_comment_client_method(out, locs, method, false, false);
		csg_generate_obsolete_attribute(out, is_deprecated);
		csg_generate_generated_code_attribute(out);
		lp_print(out,
			 "public virtual $returntype$ $methodname$($request_maybe$grpc::Metadata headers = null, "
			 "global::System.DateTime? deadline = null, global::System.Threading.CancellationToken "
			 "cancellationToken = default(global::System.Threading.CancellationToken))\n",
			 "methodname", method_name, "request_maybe", csg_get_method_request_param_maybe(method, false),
			 "returntype", csg_get_method_return_type_client(method), NULL);
		lp_print(out, "{\n", NULL);
		lp_indent(out);

		lp_print(out,
			 "return $methodname$($request_maybe$new grpc::CallOptions(headers, deadline, "
			 "cancellationToken));\n",
			 "methodname", method_name, "request_maybe", csg_get_method_request_param_maybe(method, true),
			 NULL);
		lp_outdent(out);
		lp_print(out, "}\n", NULL);

		/* overload taking CallOptions as a param */
		csg_generate_doc_comment_client_method(out, locs, method, false, true);
		csg_generate_obsolete_attribute(out, is_deprecated);
		csg_generate_generated_code_attribute(out);
		lp_print(out, "public virtual $returntype$ $methodname$($request_maybe$grpc::CallOptions options)\n",
			 "methodname", method_name, "request_maybe", csg_get_method_request_param_maybe(method, false),
			 "returntype", csg_get_method_return_type_client(method), NULL);
		lp_print(out, "{\n", NULL);
		lp_indent(out);
		const char *call;
		if (unary)
			call = "return CallInvoker.AsyncUnaryCall($methodfield$, null, options, request);\n";
		else if (!method->server_streaming) /* Client Streaming Only */
			call = "return CallInvoker.AsyncClientStreamingCall($methodfield$, null, options);\n";
		else if (!method->client_streaming) /* Server Streaming Only */
			call = "return CallInvoker.AsyncServerStreamingCall($methodfield$, null, options, request);\n";
		else /* Bi-Directional Streaming */
			call = "return CallInvoker.AsyncDuplexStreamingCall($methodfield$, null, options);\n";
		lp_print(out, call, "methodfield", methodfield, NULL);
		lp_outdent(out);
		lp_print(out, "}\n", NULL);
	}

	/* override NewInstance method */
	lp_print(out,
		 "/// <summary>Creates a new instance of client from given "
		 "<c>ClientBaseConfiguration</c>.</summary>\n",
		 NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "protected override $name$ NewInstance(ClientBaseConfiguration configuration)\n", "name", name,
		 NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);
	lp_print(out, "return new $name$(configuration);\n", "name", name, NULL);
	lp_outdent(out);
	lp_print(out, "}\n", NULL);

	lp_outdent(out);
	lp_print(out, "}\n", NULL);
	lp_print(out, "\n", NULL);
}

static void csg_generate_bind_service_method(LpPrinter *out, const LpService *service, bool append_async_suffix)
{
	lp_print(out, "/// <summary>Creates service definition that can be registered with a server</summary>\n",
		 NULL);
	lp_print(out,
		 "/// <param name=\"serviceImpl\">An object implementing the server-side handling "
		 "logic.</param>\n",
		 NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "public static grpc::ServerServiceDefinition BindService($implclass$ serviceImpl)\n", "implclass",
		 csg_get_server_class_name(service), NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);

	lp_print(out, "return grpc::ServerServiceDefinition.CreateBuilder()", NULL);
	lp_indent(out);
	lp_indent(out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_print(out, "\n.AddMethod($methodfield$, serviceImpl.$methodname$)", "methodfield",
			 csg_get_method_field_name(method), "methodname",
			 csg_server_method_name(method, append_async_suffix), NULL);
	}
	lp_print(out, ".Build();\n", NULL);
	lp_outdent(out);
	lp_outdent(out);

	lp_outdent(out);
	lp_print(out, "}\n", NULL);
	lp_print(out, "\n", NULL);
}

static void csg_generate_bind_service_with_binder_method(LpPrinter *out, const LpService *service,
							 bool append_async_suffix)
{
	lp_print(out,
		 "/// <summary>Register service method with a service binder with or without implementation. "
		 "Useful when customizing the service binding logic.\n"
		 "/// Note: this method is part of an experimental API that can change or be removed without "
		 "any prior notice.</summary>\n",
		 NULL);
	lp_print(out,
		 "/// <param name=\"serviceBinder\">Service methods will be bound by calling <c>AddMethod</c> "
		 "on this object.</param>\n",
		 NULL);
	lp_print(out,
		 "/// <param name=\"serviceImpl\">An object implementing the server-side handling "
		 "logic.</param>\n",
		 NULL);
	csg_generate_generated_code_attribute(out);
	lp_print(out, "public static void BindService(grpc::ServiceBinderBase serviceBinder, $implclass$ serviceImpl)\n",
		 "implclass", csg_get_server_class_name(service), NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);

	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_print(out,
			 "serviceBinder.AddMethod($methodfield$, serviceImpl == null ? null : new "
			 "$servermethodtype$<$inputtype$, $outputtype$>(serviceImpl.$methodname$));\n",
			 "methodfield", csg_get_method_field_name(method), "servermethodtype",
			 csg_get_csharp_server_method_type(method), "inputtype",
			 lp_csharp_class_name_message(method->input_type), "outputtype",
			 lp_csharp_class_name_message(method->output_type), "methodname",
			 csg_server_method_name(method, append_async_suffix), NULL);
	}

	lp_outdent(out);
	lp_print(out, "}\n", NULL);
	lp_print(out, "\n", NULL);
}

static void csg_generate_service(LpPrinter *out, const LpSourceLocations *locs, const LpService *service,
				 bool generate_client, bool generate_server, bool internal_access,
				 bool append_async_suffix)
{
	csg_generate_doc_comment_body(out, locs, service);

	csg_generate_obsolete_attribute(out, pb_int(service->options, SERVICEOPT_DEPRECATED, 0));
	lp_print(out, "$access_level$ static partial class $classname$\n", "access_level",
		 csg_get_access_level(internal_access), "classname", csg_get_service_class_name(service), NULL);
	lp_print(out, "{\n", NULL);
	lp_indent(out);
	lp_print(out, "static readonly string $servicenamefield$ = \"$servicename$\";\n", "servicenamefield",
		 csg_get_service_name_field_name(), "servicename", service->full_name, NULL);
	lp_print(out, "\n", NULL);

	csg_generate_marshaller_fields(out, service);
	for (int i = 0; i < service->n_methods; i++)
		csg_generate_static_method_field(out, &service->methods[i]);
	csg_generate_service_descriptor_property(out, service);

	if (generate_server)
		csg_generate_server_class(out, locs, service, append_async_suffix);
	if (generate_client)
		csg_generate_client_stub(out, locs, service);
	if (generate_server) {
		csg_generate_bind_service_method(out, service, append_async_suffix);
		csg_generate_bind_service_with_binder_method(out, service, append_async_suffix);
	}

	lp_outdent(out);
	lp_print(out, "}\n", NULL);
}

/* GetServices: the services file of file (empty if it has no services). A Str, not a C string:
 * comments may contain NUL bytes, which upstream writes out with the rest of the std::string. */
static COLD Str csg_get_services(const LpFile *file, bool generate_client, bool generate_server,
				 bool internal_access, bool append_async_suffix)
{
	Buf output = {0};
	LpPrinter out;
	lp_printer_init(&out, &output, '$');

	/* Don't write out any output if there no services, to avoid empty service files being
	 * generated for proto files that don't declare any. */
	if (file->n_services == 0)
		return (Str){0};

	const LpSourceLocations *locs = lp_source_locations(file);

	/* Write out a file header. */
	lp_print(&out, "// <auto-generated>\n", NULL);
	lp_print(&out, "//     Generated by the protocol buffer compiler.  DO NOT EDIT!\n", NULL);
	lp_print(&out, "//     source: $filename$\n", "filename", file->name, NULL);
	lp_print(&out, "// </auto-generated>\n", NULL);

	/* use C++ style as there are no file-level XML comments in .NET */
	Str leading_comments = csg_get_csharp_comments(locs, file, true);
	if (leading_comments.n) {
		lp_print(&out, "// Original file comments:\n", NULL);
		lp_write_raw(&out, str_c(leading_comments.p)); /* PrintRaw(c_str()) */
	}

	lp_print(&out, "#pragma warning disable 0414, 1591, 8981, 0612\n", NULL);

	lp_print(&out, "#region Designer generated code\n", NULL);
	lp_print(&out, "\n", NULL);
	lp_print(&out, "using grpc = global::Grpc.Core;\n", NULL);
	lp_print(&out, "\n", NULL);

	char *file_namespace = lp_csharp_file_namespace(file);
	if (*file_namespace) {
		lp_print(&out, "namespace $namespace$ {\n", "namespace", file_namespace, NULL);
		lp_indent(&out);
	}
	for (int i = 0; i < file->n_services; i++)
		csg_generate_service(&out, locs, &file->services[i], generate_client, generate_server, internal_access,
				     append_async_suffix);
	if (*file_namespace) {
		lp_outdent(&out);
		lp_print(&out, "}\n", NULL);
	}
	lp_print(&out, "#endregion\n", NULL);
	return buf_view(&output);
}
