/*
 * Copyright 2019 The gRPC Authors
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

/* Port of compiler/src/java_plugin/cpp/java_generator.{h,cpp} of grpc-java v1.85.1: the Java
 * service classes (<Service>Grpc.java). Part of the unity build of generator.c. The
 * std::map<std::string, std::string> of upstream is an LpVars: values set for one method stay for
 * the next ones, as in the map. GRPC_CODEGEN_FAIL paths are unreachable from Generate and left
 * out. */

/* java_generator.h */
typedef enum { NORMAL, LITE } ProtoFlavor;
typedef enum { OMIT, JAVAX } GeneratedAnnotation;

/* java keywords from: https://docs.oracle.com/javase/specs/jls/se8/html/jls-3.html#jls-3.9 */
static const char *const java_keywords[] = {
	"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const",
	"continue", "default", "do", "double", "else", "enum", "extends", "final", "finally", "float",
	"for", "goto", "if", "implements", "import", "instanceof", "int", "interface", "long", "native",
	"new", "package", "private", "protected", "public", "return", "short", "static", "strictfp",
	"super", "switch", "synchronized", "this", "throw", "throws", "transient", "try", "void",
	"volatile", "while",
	/* additional ones added by us */
	"true", "false", NULL,
};

/* Methods on java.lang.Object that take no arguments. */
static const char *const java_object_methods[] = {
	"clone", "finalize", "getClass", "hashCode", "notify", "notifyAll", "toString", "wait", NULL,
};

static bool in_set(const char *const *set, const char *w)
{
	for (; *set; set++)
		if (str_eq(*set, w))
			return true;
	return false;
}

/* <cctype> in the "C" locale */
static bool c_islower(char c) { return c >= 'a' && c <= 'z'; }
static bool c_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static char c_tolower(char c) { return c_isupper(c) ? (char)(c - 'A' + 'a') : c; }
static char c_toupper(char c) { return c_islower(c) ? (char)(c - 'a' + 'A') : c; }

/* Adjust a method name prefix identifier to follow the JavaBean spec:
 *   - decapitalize the first letter
 *   - remove embedded underscores & capitalize the following letter
 * Finally, if the result is a reserved java keyword or an Object method, append an underscore. */
static char *MixedLower(const char *word, bool mangle_object_methods)
{
	Buf w = {0};
	buf_addc(&w, c_tolower(word[0]));
	bool after_underscore = false;
	for (size_t i = 1; word[0] && word[i]; ++i) {
		if (word[i] == '_') {
			after_underscore = true;
		} else {
			buf_addc(&w, after_underscore ? c_toupper(word[i]) : word[i]);
			after_underscore = false;
		}
	}
	const char *s = buf_str(&w);
	if (in_set(java_keywords, s) || (mangle_object_methods && in_set(java_object_methods, s)))
		return xcat(s, "_");
	return xstrdup(s);
}

/* Converts to the identifier to the ALL_UPPER_CASE format.
 *   - An underscore is inserted where a lower case letter is followed by an upper case letter.
 *   - All letters are converted to upper case */
static char *ToAllUpperCase(const char *word)
{
	Buf w = {0};
	for (size_t i = 0; word[i]; ++i) {
		buf_addc(&w, c_toupper(word[i]));
		if (word[i + 1] && c_islower(word[i]) && c_isupper(word[i + 1]))
			buf_addc(&w, '_');
	}
	return xstrdup(buf_str(&w));
}

static char *LowerMethodName(const LpMethod *method, bool mangle_object_methods)
{
	return MixedLower(method->name, mangle_object_methods);
}

static char *MethodPropertiesFieldName(const LpMethod *method)
{
	return xcat("METHOD_", ToAllUpperCase(method->name));
}

static char *MethodPropertiesGetterName(const LpMethod *method)
{
	return MixedLower(xcat("get_", method->name, "_method"), false);
}

static char *MethodIdFieldName(const LpMethod *method) { return xcat("METHODID_", ToAllUpperCase(method->name)); }

/* protobuf::compiler::java::QualifiedClassName (names.cc) */
static char *MessageFullJavaName(const LpMessage *desc) { return jv_get_class_name_message(desc, true, false); }

/* GrpcEscapeJavadoc */
static Str GrpcEscapeJavadoc(Str input)
{
	Buf result = {0};
	char prev = '*';
	for (size_t i = 0; i < input.n; i++) {
		char c = input.p[i];
		switch (c) {
		case '*':
			/* Avoid "/ *". */
			if (prev == '/')
				buf_adds(&result, "&#42;");
			else
				buf_addc(&result, c);
			break;
		case '/':
			/* Avoid "* /". */
			if (prev == '*')
				buf_adds(&result, "&#47;");
			else
				buf_addc(&result, c);
			break;
		case '@':
			/* '@' starts javadoc tags including the @deprecated tag, which will cause a
			 * compile-time error if inserted before a declaration that does not have a
			 * corresponding @Deprecated annotation. */
			buf_adds(&result, "&#64;");
			break;
		case '<':
			/* Avoid interpretation as HTML. */
			buf_adds(&result, "&lt;");
			break;
		case '>':
			/* Avoid interpretation as HTML. */
			buf_adds(&result, "&gt;");
			break;
		case '&':
			/* Avoid interpretation as HTML. */
			buf_adds(&result, "&amp;");
			break;
		case '\\':
			/* Java interprets Unicode escape sequences anywhere! */
			buf_adds(&result, "&#92;");
			break;
		default:
			buf_addc(&result, c);
			break;
		}
		prev = c;
	}
	return buf_view(&result);
}

/* GrpcGetCommentsForDescriptor */
static Str GrpcGetCommentsForDescriptor(const ProtoDesc *descriptor)
{
	LpSourceLocation location;
	if (jv_get_source_location(descriptor, &location))
		return location.leading_comments.n == 0 ? location.trailing_comments : location.leading_comments;
	return (Str){"", 0};
}

typedef struct {
	Str *v;
	int n;
} DocLines;

/* GrpcGetDocLines: GrpcSplit(escaped, "\n") skips empty pieces. */
static DocLines GrpcGetDocLines(Str comments)
{
	DocLines lines = {0};
	if (comments.n == 0)
		return lines;
	/* If the comment itself contains block comment start or end markers, HTML-escape them so
	 * that they don't accidentally close the doc comment. */
	Str escaped = GrpcEscapeJavadoc(comments);
	int cap = 0;
	const char *p = escaped.p, *end = escaped.p + escaped.n;
	while (p != end) {
		if (*p == '\n') {
			++p;
			continue;
		}
		const char *start = p;
		while (++p != end && *p != '\n')
			;
		if (lines.n == cap) {
			int ncap = cap ? 2 * cap : 8;
			lines.v = xrealloc(lines.v, sizeof(Str) * (size_t)cap, sizeof(Str) * (size_t)ncap);
			cap = ncap;
		}
		lines.v[lines.n++] = (Str){start, (size_t)(p - start)};
	}
	return lines;
}

static DocLines GrpcGetDocLinesForDescriptor(const ProtoDesc *descriptor)
{
	return GrpcGetDocLines(GrpcGetCommentsForDescriptor(descriptor));
}

typedef enum {
	ASYNC_INTERFACE = 0,
	BLOCKING_CLIENT_INTERFACE = 1,
	FUTURE_CLIENT_INTERFACE = 2,
	BLOCKING_SERVER_INTERFACE = 3,
	ASYNC_CLIENT_IMPL = 4,
	BLOCKING_CLIENT_IMPL = 5,
	FUTURE_CLIENT_IMPL = 6,
	ABSTRACT_CLASS = 7,
	BLOCKING_V2_CLIENT_IMPL = 8,
	NONE = 999,
} StubType;

typedef enum {
	ASYNC_CALL = 0,
	BLOCKING_CALL = 1,
	FUTURE_CALL = 2,
	BLOCKING_V2_CALL = 3,
} CallType;

/* GrpcWriteDocCommentBody. Printer::Print(" *$line$\n", "line", line) writes the line as is
 * (NUL bytes included). */
static void GrpcWriteDocCommentBody(LpPrinter *printer, DocLines lines, bool surroundWithPreTag)
{
	if (lines.n == 0)
		return;
	if (surroundWithPreTag)
		lp_print(printer, " * <pre>\n", NULL);
	for (int i = 0; i < lines.n; i++) {
		/* Most lines should start with a space.  Watch out for lines that start with a /,
		 * since putting that right after the leading asterisk will close the comment. */
		lp_print(printer, lines.v[i].p[0] == '/' ? " * " : " *", NULL);
		lp_write_raw(printer, lines.v[i]);
		lp_print(printer, "\n", NULL);
	}
	if (surroundWithPreTag)
		lp_print(printer, " * </pre>\n", NULL);
}

static void GrpcWriteDocComment(LpPrinter *printer, const char *comments)
{
	lp_print(printer, "/**\n", NULL);
	GrpcWriteDocCommentBody(printer, GrpcGetDocLines(str_c(comments)), false);
	lp_print(printer, " */\n", NULL);
}

/* For the non-interface classes add a description of use before the description from proto */
static void GrpcWriteServiceDocComment(LpPrinter *printer, const LpService *service, StubType type)
{
	lp_print(printer, "/**\n", NULL);
	const char *text = NULL;
	switch (type) {
	case ASYNC_CLIENT_IMPL:
		text = " * A stub to allow clients to do asynchronous rpc calls to service $service$.\n";
		break;
	case BLOCKING_CLIENT_IMPL:
		text = " * A stub to allow clients to do limited synchronous rpc calls to service $service$.\n";
		break;
	case BLOCKING_V2_CLIENT_IMPL:
		text = " * A stub to allow clients to do synchronous rpc calls to service $service$.\n";
		break;
	case FUTURE_CLIENT_IMPL:
		text = " * A stub to allow clients to do ListenableFuture-style rpc calls to service $service$.\n";
		break;
	case ABSTRACT_CLASS:
		text = " * Base class for the server implementation of the service $service$.\n";
		break;
	default:; /* No extra description */
	}
	if (text)
		lp_print(printer, text, "service", service->name, NULL);
	GrpcWriteDocCommentBody(printer, GrpcGetDocLinesForDescriptor((const ProtoDesc *)service), true);
	lp_print(printer, " */\n", NULL);
}

static void GrpcWriteMethodDocComment(LpPrinter *printer, const LpMethod *method)
{
	/* Deviating from protobuf to avoid extraneous docs
	 * (see https://github.com/google/protobuf/issues/1406); */
	lp_print(printer, "/**\n", NULL);
	GrpcWriteDocCommentBody(printer, GrpcGetDocLinesForDescriptor((const ProtoDesc *)method), true);
	lp_print(printer, " */\n", NULL);
}

enum { METHOPT_IDEMPOTENCY_LEVEL = 34, NO_SIDE_EFFECTS = 1, IDEMPOTENT = 2 };

static bool service_deprecated(const LpService *s) { return jv_opt_bool((const ProtoDesc *)s, JV_SVCOPT_DEPRECATED); }
static bool method_deprecated(const LpMethod *m) { return jv_opt_bool((const ProtoDesc *)m, JV_METHOPT_DEPRECATED); }

#define SET(k, v) lp_vars_set(vars, (k), (v))
#define PRINT(text) lp_print_vars(p, vars, (text))

static void PrintMethodFields(const LpService *service, LpVars *vars, LpPrinter *p, ProtoFlavor flavor)
{
	lp_print(p, "// Static method descriptors that strictly reflect the proto.\n", NULL);
	SET("service_name", service->name);
	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		SET("arg_in_id", xfmt("%d", 2 * i));
		SET("arg_out_id", xfmt("%d", 2 * i + 1));
		SET("method_name", method->name);
		SET("input_type", MessageFullJavaName(method->input_type));
		SET("output_type", MessageFullJavaName(method->output_type));
		SET("method_field_name", MethodPropertiesFieldName(method));
		SET("method_new_field_name", MethodPropertiesGetterName(method));
		SET("method_method_name", MethodPropertiesGetterName(method));
		bool client_streaming = method->client_streaming;
		bool server_streaming = method->server_streaming;
		if (client_streaming)
			SET("method_type", server_streaming ? "BIDI_STREAMING" : "CLIENT_STREAMING");
		else
			SET("method_type", server_streaming ? "SERVER_STREAMING" : "UNARY");

		SET("ProtoUtils", flavor == LITE ? "io.grpc.protobuf.lite.ProtoLiteUtils" : "io.grpc.protobuf.ProtoUtils");
		PRINT("private static volatile $MethodDescriptor$<$input_type$,\n"
		      "    $output_type$> $method_new_field_name$;\n"
		      "\n"
		      "@$RpcMethod$(\n"
		      "    fullMethodName = SERVICE_NAME + '/' + \"$method_name$\",\n"
		      "    requestType = $input_type$.class,\n"
		      "    responseType = $output_type$.class,\n"
		      "    methodType = $MethodType$.$method_type$)\n"
		      "public static $MethodDescriptor$<$input_type$,\n"
		      "    $output_type$> $method_method_name$() {\n"
		      "  $MethodDescriptor$<$input_type$, $output_type$> $method_new_field_name$;\n"
		      "  if (($method_new_field_name$ = $service_class_name$.$method_new_field_name$) == null) {\n"
		      "    synchronized ($service_class_name$.class) {\n"
		      "      if (($method_new_field_name$ = $service_class_name$.$method_new_field_name$) == null) {\n"
		      "        $service_class_name$.$method_new_field_name$ = $method_new_field_name$ =\n"
		      "            $MethodDescriptor$.<$input_type$, $output_type$>newBuilder()\n"
		      "            .setType($MethodType$.$method_type$)\n"
		      "            .setFullMethodName(generateFullMethodName(SERVICE_NAME, \"$method_name$\"))\n");

		int level = (int)pb_int(method->options, METHOPT_IDEMPOTENCY_LEVEL, 0);
		if (level == NO_SIDE_EFFECTS)
			PRINT("            .setSafe(true)\n");
		else if (level == IDEMPOTENT)
			PRINT("            .setIdempotent(true)\n");

		PRINT("            .setSampledToLocalTracing(true)\n"
		      "            .setRequestMarshaller($ProtoUtils$.marshaller(\n"
		      "                $input_type$.getDefaultInstance()))\n"
		      "            .setResponseMarshaller($ProtoUtils$.marshaller(\n"
		      "                $output_type$.getDefaultInstance()))\n");

		SET("proto_method_descriptor_supplier", xcat(service->name, "MethodDescriptorSupplier"));
		if (flavor == NORMAL)
			PRINT("            .setSchemaDescriptor(new "
			      "$proto_method_descriptor_supplier$(\"$method_name$\"))\n");

		PRINT("            .build();\n");
		PRINT("      }\n"
		      "    }\n"
		      "  }\n"
		      "  return $method_new_field_name$;\n"
		      "}\n"
		      "\n");
	}
}

/* Prints a StubFactory for given service / stub type. */
static void PrintStubFactory(const LpService *service, LpVars *vars, LpPrinter *p, StubType type)
{
	(void)service;
	const char *stub_type_name = "";
	switch (type) {
	case FUTURE_CLIENT_IMPL: stub_type_name = "Future"; break;
	case BLOCKING_CLIENT_IMPL: stub_type_name = "Blocking"; break;
	case BLOCKING_V2_CLIENT_IMPL: stub_type_name = "BlockingV2"; break;
	default: break; /* ASYNC_CLIENT_IMPL */
	}
	SET("stub_full_name", xcat(lp_vars_get(vars, "service_name"), stub_type_name, "Stub"));
	PRINT("$StubFactory$<$stub_full_name$> factory =\n"
	      "  new $StubFactory$<$stub_full_name$>() {\n"
	      "    @$Override$\n"
	      "    public $stub_full_name$ newStub($Channel$ channel, $CallOptions$ callOptions) {\n"
	      "      return new $stub_full_name$(channel, callOptions);\n"
	      "    }\n"
	      "  };\n");
}

/* Prints a client interface or implementation class, or a server interface. */
static void PrintStub(const LpService *service, LpVars *vars, LpPrinter *p, StubType type)
{
	const char *service_name = service->name;
	SET("service_name", service_name);
	const char *stub_name = service_name;
	const char *stub_base_class_name = "AbstractStub";
	CallType call_type = ASYNC_CALL;
	bool interface = false;
	switch (type) {
	case ASYNC_INTERFACE:
		call_type = ASYNC_CALL;
		interface = true;
		stub_name = "AsyncService";
		break;
	case ASYNC_CLIENT_IMPL:
		call_type = ASYNC_CALL;
		stub_name = xcat(stub_name, "Stub");
		stub_base_class_name = "AbstractAsyncStub";
		break;
	case BLOCKING_CLIENT_IMPL:
		call_type = BLOCKING_CALL;
		stub_name = xcat(stub_name, "BlockingStub");
		stub_base_class_name = "AbstractBlockingStub";
		break;
	case BLOCKING_V2_CLIENT_IMPL:
		call_type = BLOCKING_V2_CALL;
		stub_name = xcat(stub_name, "BlockingV2Stub");
		stub_base_class_name = "AbstractBlockingStub";
		break;
	case FUTURE_CLIENT_IMPL:
		call_type = FUTURE_CALL;
		stub_name = xcat(stub_name, "FutureStub");
		stub_base_class_name = "AbstractFutureStub";
		break;
	default:
		fail("Cannot determine class name for StubType: %d", (int)type);
	}
	SET("stub_name", stub_name);
	SET("stub_base_class_name", lp_vars_get(vars, stub_base_class_name));

	/* Class head */
	GrpcWriteServiceDocComment(p, service, type);

	if (service_deprecated(service))
		PRINT("@$Deprecated$\n");

	if (interface)
		PRINT("public interface $stub_name$ {\n");
	else
		PRINT("public static final class $stub_name$\n"
		      "    extends $stub_base_class_name$<$stub_name$> {\n");
	lp_indent(p);

	/* Constructor and build() method */
	if (!interface) {
		PRINT("private $stub_name$(\n"
		      "    $Channel$ channel, $CallOptions$ callOptions) {"
		      "\n");
		lp_indent(p);
		lp_print(p, "super(channel, callOptions);\n", NULL);
		lp_outdent(p);
		lp_print(p, "}\n\n", NULL);
		PRINT("@$Override$\n"
		      "protected $stub_name$ build(\n"
		      "    $Channel$ channel, $CallOptions$ callOptions) {"
		      "\n");
		lp_indent(p);
		PRINT("return new $stub_name$(channel, callOptions);\n");
		lp_outdent(p);
		lp_print(p, "}\n", NULL);
	}

	/* RPC methods */
	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		SET("input_type", MessageFullJavaName(method->input_type));
		SET("output_type", MessageFullJavaName(method->output_type));
		bool client_streaming = method->client_streaming;
		bool server_streaming = method->server_streaming;
		bool mangle_object_methods = (call_type == BLOCKING_V2_CALL && client_streaming) ||
					     (call_type == BLOCKING_CALL && client_streaming && server_streaming);
		SET("lower_method_name", LowerMethodName(method, mangle_object_methods));
		SET("method_method_name", MethodPropertiesGetterName(method));

		if (call_type == BLOCKING_CALL && client_streaming)
			continue; /* Blocking client interface with client streaming is not available */

		if (call_type == FUTURE_CALL && (client_streaming || server_streaming))
			continue; /* Future interface doesn't support streaming. */

		/* Method signature */
		lp_print(p, "\n", NULL);
		GrpcWriteMethodDocComment(p, method);

		if (method_deprecated(method))
			PRINT("@$Deprecated$\n");

		if ((call_type == BLOCKING_CALL && client_streaming && server_streaming) ||
		    (call_type == BLOCKING_V2_CALL && (client_streaming || server_streaming)))
			PRINT("@io.grpc.ExperimentalApi(\"https://github.com/grpc/grpc-java/issues/10918\")\n");

		lp_print(p, interface ? "default " : "public ", NULL);
		switch (call_type) {
		case BLOCKING_CALL:
			if (client_streaming && server_streaming)
				PRINT("$BlockingClientCall$<$input_type$, $output_type$>\n"
				      "    $lower_method_name$()");
			else if (server_streaming) /* Server streaming */
				PRINT("$Iterator$<$output_type$> $lower_method_name$(\n"
				      "    $input_type$ request)");
			else /* Simple RPC */
				PRINT("$output_type$ $lower_method_name$($input_type$ request)");
			break;
		case BLOCKING_V2_CALL:
			if (client_streaming) { /* Both Bidi and Client Streaming */
				PRINT("$BlockingClientCall$<$input_type$, $output_type$>\n"
				      "    $lower_method_name$()");
			} else if (server_streaming) { /* Server streaming */
				PRINT("$BlockingClientCall$<?, $output_type$>\n"
				      "    $lower_method_name$($input_type$ request)");
			} else { /* Simple RPC */
				SET("throws_decl", " throws io.grpc.StatusException");
				PRINT("$output_type$ $lower_method_name$($input_type$ request)$throws_decl$");
			}
			break;
		case ASYNC_CALL:
			if (client_streaming) /* Bidirectional streaming or client streaming */
				PRINT("$StreamObserver$<$input_type$> $lower_method_name$(\n"
				      "    $StreamObserver$<$output_type$> responseObserver)");
			else /* Server streaming or simple RPC */
				PRINT("void $lower_method_name$($input_type$ request,\n"
				      "    $StreamObserver$<$output_type$> responseObserver)");
			break;
		case FUTURE_CALL:
			PRINT("$ListenableFuture$<$output_type$> $lower_method_name$(\n"
			      "    $input_type$ request)");
			break;
		}

		/* Method body. */
		lp_print(p, " {\n", NULL);
		lp_indent(p);
		if (interface && call_type == ASYNC_CALL) {
			/* NB: Skipping validation of service methods. If something is wrong, we wouldn't
			 * get to this point as compiler would return errors when generating service
			 * interface. */
			if (client_streaming)
				PRINT("return io.grpc.stub.ServerCalls.asyncUnimplementedStreamingCall("
				      "$method_method_name$(), responseObserver);\n");
			else
				PRINT("io.grpc.stub.ServerCalls.asyncUnimplementedUnaryCall("
				      "$method_method_name$(), responseObserver);\n");
		} else if (!interface) {
			switch (call_type) {
			case BLOCKING_CALL:
				SET("calls_method", server_streaming ? "io.grpc.stub.ClientCalls.blockingServerStreamingCall"
								     : "io.grpc.stub.ClientCalls.blockingUnaryCall");
				SET("params", "request");
				PRINT("return $calls_method$(\n"
				      "    getChannel(), $method_method_name$(), getCallOptions(), $params$);\n");
				break;
			case BLOCKING_V2_CALL:
				if (client_streaming) { /* client and bidi streaming */
					SET("calls_method", server_streaming
								    ? "io.grpc.stub.ClientCalls.blockingBidiStreamingCall"
								    : "io.grpc.stub.ClientCalls.blockingClientStreamingCall");
					PRINT("return $calls_method$(\n"
					      "    getChannel(), $method_method_name$(), getCallOptions());\n");
				} else { /* server streaming and unary */
					SET("params", "request");
					if (server_streaming) {
						SET("calls_method", "io.grpc.stub.ClientCalls.blockingV2ServerStreamingCall");
					} else {
						SET("calls_method", "io.grpc.stub.ClientCalls.blockingV2UnaryCall");
						SET("throws_decl", " throws io.grpc.StatusException");
					}
					PRINT("return $calls_method$(\n"
					      "    getChannel(), $method_method_name$(), getCallOptions(), $params$);\n");
				}
				break;
			case ASYNC_CALL:
				if (server_streaming) {
					if (client_streaming) {
						SET("calls_method", "io.grpc.stub.ClientCalls.asyncBidiStreamingCall");
						SET("params", "responseObserver");
					} else {
						SET("calls_method", "io.grpc.stub.ClientCalls.asyncServerStreamingCall");
						SET("params", "request, responseObserver");
					}
				} else {
					if (client_streaming) {
						SET("calls_method", "io.grpc.stub.ClientCalls.asyncClientStreamingCall");
						SET("params", "responseObserver");
					} else {
						SET("calls_method", "io.grpc.stub.ClientCalls.asyncUnaryCall");
						SET("params", "request, responseObserver");
					}
				}
				SET("last_line_prefix", client_streaming ? "return " : "");
				PRINT("$last_line_prefix$$calls_method$(\n"
				      "    getChannel().newCall($method_method_name$(), getCallOptions()), $params$);\n");
				break;
			case FUTURE_CALL:
				SET("calls_method", "io.grpc.stub.ClientCalls.futureUnaryCall");
				PRINT("return $calls_method$(\n"
				      "    getChannel().newCall($method_method_name$(), getCallOptions()), request);\n");
				break;
			}
		}
		lp_outdent(p);
		lp_print(p, "}\n", NULL);
	}

	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);
}

static void PrintAbstractClassStub(const LpService *service, LpVars *vars, LpPrinter *p)
{
	SET("service_name", service->name);

	GrpcWriteServiceDocComment(p, service, ABSTRACT_CLASS);
	if (service_deprecated(service))
		PRINT("@$Deprecated$\n");
	PRINT("public static abstract class $service_name$ImplBase\n"
	      "    implements $BindableService$, AsyncService {\n"
	      "\n"
	      "  @$Override$ public final $ServerServiceDefinition$ bindService() {\n"
	      "    return $service_class_name$.bindService(this);\n"
	      "  }\n"
	      "}\n\n");
}

/* Place all method invocations into a single class to reduce memory footprint on Android. */
static void PrintMethodHandlerClass(const LpService *service, LpVars *vars, LpPrinter *p)
{
	/* Sort method ids based on client_streaming() so switch tables are compact: the
	 * stable_sort by client_streaming is a stable partition. */
	int method_id = 0;
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < service->n_methods; ++i) {
			const LpMethod *method = &service->methods[i];
			if (method->client_streaming != (pass == 1))
				continue;
			SET("method_id", xfmt("%d", method_id++));
			SET("method_id_name", MethodIdFieldName(method));
			PRINT("private static final int $method_id_name$ = $method_id$;\n");
		}
	lp_print(p, "\n", NULL);
	PRINT("private static final class MethodHandlers<Req, Resp> implements\n"
	      "    io.grpc.stub.ServerCalls.UnaryMethod<Req, Resp>,\n"
	      "    io.grpc.stub.ServerCalls.ServerStreamingMethod<Req, Resp>,\n"
	      "    io.grpc.stub.ServerCalls.ClientStreamingMethod<Req, Resp>,\n"
	      "    io.grpc.stub.ServerCalls.BidiStreamingMethod<Req, Resp> {\n"
	      "  private final AsyncService serviceImpl;\n"
	      "  private final int methodId;\n"
	      "\n"
	      "  MethodHandlers(AsyncService serviceImpl, int methodId) {\n"
	      "    this.serviceImpl = serviceImpl;\n"
	      "    this.methodId = methodId;\n"
	      "  }\n\n");
	lp_indent(p);
	PRINT("@$Override$\n"
	      "@java.lang.SuppressWarnings(\"unchecked\")\n"
	      "public void invoke(Req request, $StreamObserver$<Resp> responseObserver) {\n"
	      "  switch (methodId) {\n");
	lp_indent(p);
	lp_indent(p);

	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		if (method->client_streaming)
			continue;
		SET("method_id_name", MethodIdFieldName(method));
		SET("lower_method_name", LowerMethodName(method, false));
		SET("input_type", MessageFullJavaName(method->input_type));
		SET("output_type", MessageFullJavaName(method->output_type));
		PRINT("case $method_id_name$:\n"
		      "  serviceImpl.$lower_method_name$(($input_type$) request,\n"
		      "      ($StreamObserver$<$output_type$>) responseObserver);\n"
		      "  break;\n");
	}
	lp_print(p,
		 "default:\n"
		 "  throw new AssertionError();\n",
		 NULL);

	lp_outdent(p);
	lp_outdent(p);
	lp_print(p,
		 "  }\n"
		 "}\n\n",
		 NULL);

	PRINT("@$Override$\n"
	      "@java.lang.SuppressWarnings(\"unchecked\")\n"
	      "public $StreamObserver$<Req> invoke(\n"
	      "    $StreamObserver$<Resp> responseObserver) {\n"
	      "  switch (methodId) {\n");
	lp_indent(p);
	lp_indent(p);

	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		if (!method->client_streaming)
			continue;
		SET("method_id_name", MethodIdFieldName(method));
		SET("lower_method_name", LowerMethodName(method, false));
		SET("input_type", MessageFullJavaName(method->input_type));
		SET("output_type", MessageFullJavaName(method->output_type));
		PRINT("case $method_id_name$:\n"
		      "  return ($StreamObserver$<Req>) serviceImpl.$lower_method_name$(\n"
		      "      ($StreamObserver$<$output_type$>) responseObserver);\n");
	}
	lp_print(p,
		 "default:\n"
		 "  throw new AssertionError();\n",
		 NULL);

	lp_outdent(p);
	lp_outdent(p);
	lp_print(p,
		 "  }\n"
		 "}\n",
		 NULL);

	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);
}

static void PrintGetServiceDescriptorMethod(const LpService *service, LpVars *vars, LpPrinter *p, ProtoFlavor flavor)
{
	const char *service_name = service->name;
	SET("service_name", service_name);

	if (flavor == NORMAL) {
		SET("proto_base_descriptor_supplier", xcat(service_name, "BaseDescriptorSupplier"));
		SET("proto_file_descriptor_supplier", xcat(service_name, "FileDescriptorSupplier"));
		SET("proto_method_descriptor_supplier", xcat(service_name, "MethodDescriptorSupplier"));
		SET("proto_class_name", jv_get_class_name_file(service->file, true, false));
		PRINT("private static abstract class $proto_base_descriptor_supplier$\n"
		      "    implements $ProtoFileDescriptorSupplier$, $ProtoServiceDescriptorSupplier$ {\n"
		      "  $proto_base_descriptor_supplier$() {}\n"
		      "\n"
		      "  @$Override$\n"
		      "  public com.google.protobuf.Descriptors.FileDescriptor getFileDescriptor() {\n"
		      "    return $proto_class_name$.getDescriptor();\n"
		      "  }\n"
		      "\n"
		      "  @$Override$\n"
		      "  public com.google.protobuf.Descriptors.ServiceDescriptor getServiceDescriptor() {\n"
		      "    return getFileDescriptor().findServiceByName(\"$service_name$\");\n"
		      "  }\n"
		      "}\n"
		      "\n"
		      "private static final class $proto_file_descriptor_supplier$\n"
		      "    extends $proto_base_descriptor_supplier$ {\n"
		      "  $proto_file_descriptor_supplier$() {}\n"
		      "}\n"
		      "\n"
		      "private static final class $proto_method_descriptor_supplier$\n"
		      "    extends $proto_base_descriptor_supplier$\n"
		      "    implements $ProtoMethodDescriptorSupplier$ {\n"
		      "  private final $String$ methodName;\n"
		      "\n"
		      "  $proto_method_descriptor_supplier$($String$ methodName) {\n"
		      "    this.methodName = methodName;\n"
		      "  }\n"
		      "\n"
		      "  @$Override$\n"
		      "  public com.google.protobuf.Descriptors.MethodDescriptor getMethodDescriptor() {\n"
		      "    return getServiceDescriptor().findMethodByName(methodName);\n"
		      "  }\n"
		      "}\n\n");
	}

	PRINT("private static volatile $ServiceDescriptor$ serviceDescriptor;\n\n");

	PRINT("public static $ServiceDescriptor$ getServiceDescriptor() {\n");
	lp_indent(p);
	PRINT("$ServiceDescriptor$ result = serviceDescriptor;\n");
	lp_print(p, "if (result == null) {\n", NULL);
	lp_indent(p);
	PRINT("synchronized ($service_class_name$.class) {\n");
	lp_indent(p);
	lp_print(p, "result = serviceDescriptor;\n", NULL);
	lp_print(p, "if (result == null) {\n", NULL);
	lp_indent(p);

	PRINT("serviceDescriptor = result = $ServiceDescriptor$.newBuilder(SERVICE_NAME)");
	lp_indent(p);
	lp_indent(p);
	if (flavor == NORMAL)
		PRINT("\n.setSchemaDescriptor(new $proto_file_descriptor_supplier$())");
	for (int i = 0; i < service->n_methods; ++i) {
		SET("method_method_name", MethodPropertiesGetterName(&service->methods[i]));
		PRINT("\n.addMethod($method_method_name$())");
	}
	lp_print(p, "\n.build();\n", NULL);
	lp_outdent(p);
	lp_outdent(p);

	lp_outdent(p);
	lp_print(p, "}\n", NULL);
	lp_outdent(p);
	lp_print(p, "}\n", NULL);
	lp_outdent(p);
	lp_print(p, "}\n", NULL);
	lp_print(p, "return result;\n", NULL);
	lp_outdent(p);
	lp_print(p, "}\n", NULL);
}

static void PrintBindServiceMethod(const LpService *service, LpVars *vars, LpPrinter *p)
{
	SET("service_name", service->name);
	PRINT("public static final io.grpc.ServerServiceDefinition "
	      "bindService(AsyncService service) {\n");

	lp_indent(p);
	PRINT("return "
	      "$ServerServiceDefinition$.builder(getServiceDescriptor())\n");
	lp_indent(p);
	lp_indent(p);
	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		SET("lower_method_name", LowerMethodName(method, false));
		SET("method_method_name", MethodPropertiesGetterName(method));
		SET("input_type", MessageFullJavaName(method->input_type));
		SET("output_type", MessageFullJavaName(method->output_type));
		SET("method_id_name", MethodIdFieldName(method));
		bool client_streaming = method->client_streaming;
		bool server_streaming = method->server_streaming;
		if (client_streaming)
			SET("calls_method", server_streaming ? "io.grpc.stub.ServerCalls.asyncBidiStreamingCall"
							     : "io.grpc.stub.ServerCalls.asyncClientStreamingCall");
		else
			SET("calls_method", server_streaming ? "io.grpc.stub.ServerCalls.asyncServerStreamingCall"
							     : "io.grpc.stub.ServerCalls.asyncUnaryCall");
		PRINT(".addMethod(\n");
		lp_indent(p);
		PRINT("$method_method_name$(),\n"
		      "$calls_method$(\n");
		lp_indent(p);
		PRINT("new MethodHandlers<\n"
		      "  $input_type$,\n"
		      "  $output_type$>(\n"
		      "    service, $method_id_name$)))\n");
		lp_outdent(p);
		lp_outdent(p);
	}
	lp_print(p, ".build();\n", NULL);
	lp_outdent(p);
	lp_outdent(p);
	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);
}

static char *ServiceClassName(const LpService *service);

static void PrintService(const LpService *service, LpVars *vars, LpPrinter *p, ProtoFlavor flavor,
			 bool disable_version, GeneratedAnnotation generated_annotation)
{
	SET("service_name", service->name);
	SET("file_name", lp_utf8_safe_cescape(str_c(service->file->name)));
	SET("service_class_name", ServiceClassName(service));
	SET("grpc_version", "");
	if (!disable_version)
		SET("grpc_version", " (version " GRPC_VERSION ")");
	GrpcWriteServiceDocComment(p, service, NONE);

	if (generated_annotation == JAVAX)
		PRINT("@javax.annotation.Generated(\n"
		      "    value = \"by gRPC proto compiler$grpc_version$\",\n"
		      "    comments = \"Source: $file_name$\")\n"
		      "@$GrpcGenerated$\n");
	else /* GeneratedAnnotation::OMIT */
		PRINT("@$GrpcGenerated$\n");

	if (service_deprecated(service))
		PRINT("@$Deprecated$\n");

	PRINT("public final class $service_class_name$ {\n\n");
	lp_indent(p);
	PRINT("private $service_class_name$() {}\n\n");

	PRINT("public static final $String$ SERVICE_NAME = "
	      "\"$Package$$service_name$\";\n\n");

	PrintMethodFields(service, vars, p, flavor);

	GrpcWriteDocComment(p, " Creates a new async stub that supports all call types for the service");
	PRINT("public static $service_name$Stub newStub($Channel$ channel) {\n");
	lp_indent(p);
	PrintStubFactory(service, vars, p, ASYNC_CLIENT_IMPL);
	PRINT("return $service_name$Stub.newStub(factory, channel);\n");
	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);

	GrpcWriteDocComment(p, " Creates a new blocking-style stub that supports all types of calls "
			       "on the service");
	PRINT("public static $service_name$BlockingV2Stub newBlockingV2Stub(\n"
	      "    $Channel$ channel) {\n");
	lp_indent(p);
	PrintStubFactory(service, vars, p, BLOCKING_V2_CLIENT_IMPL);
	PRINT("return $service_name$BlockingV2Stub.newStub(factory, channel);\n");
	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);

	GrpcWriteDocComment(p, " Creates a new blocking-style stub that supports unary and streaming "
			       "output calls on the service");
	PRINT("public static $service_name$BlockingStub newBlockingStub(\n"
	      "    $Channel$ channel) {\n");
	lp_indent(p);
	PrintStubFactory(service, vars, p, BLOCKING_CLIENT_IMPL);
	PRINT("return $service_name$BlockingStub.newStub(factory, channel);\n");
	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);

	GrpcWriteDocComment(p, " Creates a new ListenableFuture-style stub that supports unary calls "
			       "on the service");
	PRINT("public static $service_name$FutureStub newFutureStub(\n"
	      "    $Channel$ channel) {\n");
	lp_indent(p);
	PrintStubFactory(service, vars, p, FUTURE_CLIENT_IMPL);
	PRINT("return $service_name$FutureStub.newStub(factory, channel);\n");
	lp_outdent(p);
	lp_print(p, "}\n\n", NULL);

	PrintStub(service, vars, p, ASYNC_INTERFACE);
	PrintAbstractClassStub(service, vars, p);
	PrintStub(service, vars, p, ASYNC_CLIENT_IMPL);
	PrintStub(service, vars, p, BLOCKING_V2_CLIENT_IMPL);
	PrintStub(service, vars, p, BLOCKING_CLIENT_IMPL);
	PrintStub(service, vars, p, FUTURE_CLIENT_IMPL);

	PrintMethodHandlerClass(service, vars, p);
	PrintBindServiceMethod(service, vars, p);
	PrintGetServiceDescriptorMethod(service, vars, p, flavor);
	lp_outdent(p);
	lp_print(p, "}\n", NULL);
}

#undef SET
#undef PRINT

static void PrintImports(LpPrinter *p)
{
	lp_print(p,
		 "import static "
		 "io.grpc.MethodDescriptor.generateFullMethodName;\n\n",
		 NULL);
}

static char *ServiceJavaPackage(const LpFile *file);

/* Writes the generated service interface into out. */
static void GenerateService(const LpService *service, Buf *out, ProtoFlavor flavor, bool disable_version,
			    GeneratedAnnotation generated_annotation)
{
	/* All non-generated classes must be referred by fully qualified names to avoid collision
	 * with generated classes. */
	static const char *const builtin[] = {
		"String", "java.lang.String",
		"Deprecated", "java.lang.Deprecated",
		"Override", "java.lang.Override",
		"Channel", "io.grpc.Channel",
		"CallOptions", "io.grpc.CallOptions",
		"MethodType", "io.grpc.MethodDescriptor.MethodType",
		"ServerMethodDefinition", "io.grpc.ServerMethodDefinition",
		"BindableService", "io.grpc.BindableService",
		"ServerServiceDefinition", "io.grpc.ServerServiceDefinition",
		"ServiceDescriptor", "io.grpc.ServiceDescriptor",
		"ProtoFileDescriptorSupplier", "io.grpc.protobuf.ProtoFileDescriptorSupplier",
		"ProtoServiceDescriptorSupplier", "io.grpc.protobuf.ProtoServiceDescriptorSupplier",
		"ProtoMethodDescriptorSupplier", "io.grpc.protobuf.ProtoMethodDescriptorSupplier",
		"AbstractStub", "io.grpc.stub.AbstractStub",
		"AbstractAsyncStub", "io.grpc.stub.AbstractAsyncStub",
		"AbstractFutureStub", "io.grpc.stub.AbstractFutureStub",
		"AbstractBlockingStub", "io.grpc.stub.AbstractBlockingStub",
		"StubFactory", "io.grpc.stub.AbstractStub.StubFactory",
		"RpcMethod", "io.grpc.stub.annotations.RpcMethod",
		"MethodDescriptor", "io.grpc.MethodDescriptor",
		"StreamObserver", "io.grpc.stub.StreamObserver",
		"BlockingClientCall", "io.grpc.stub.BlockingClientCall",
		"Iterator", "java.util.Iterator",
		"GrpcGenerated", "io.grpc.stub.annotations.GrpcGenerated",
		"ListenableFuture", "com.google.common.util.concurrent.ListenableFuture",
		NULL,
	};
	LpVars vars = {0};
	for (int i = 0; builtin[i]; i += 2)
		lp_vars_set(&vars, builtin[i], builtin[i + 1]);

	LpPrinter printer;
	lp_printer_init(&printer, out, '$');
	char *package_name = ServiceJavaPackage(service->file);
	if (*package_name)
		lp_print(&printer, "package $package_name$;\n\n", "package_name", package_name, NULL);
	PrintImports(&printer);

	/* Package string is used to fully qualify method names. */
	const char *package = service->file->package;
	lp_vars_set(&vars, "Package", *package ? xcat(package, ".") : "");
	PrintService(service, &vars, &printer, flavor, disable_version, generated_annotation);
}

/* Returns the package name of the gRPC services defined in the given file. */
static char *ServiceJavaPackage(const LpFile *file)
{
	char *result = xstrdup(jv_get_class_name_file(file, true, false));
	char *last_dot_pos = strrchr(result, '.');
	if (last_dot_pos)
		*last_dot_pos = 0;
	else
		result = "";
	return result;
}

/* Returns the name of the outer class that wraps in all the generated code for the given
 * service. */
static char *ServiceClassName(const LpService *service) { return xcat(service->name, "Grpc"); }
