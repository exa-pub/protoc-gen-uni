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

/* Port of src/compiler/objective_c_generator.{h,cc} and objective_c_generator_helpers.h of grpc
 * v1.85.1. The Objective-C names (FilePath, FileClassPrefix, ClassName, ...) are
 * compiler/objectivec/names.cc of libprotoc (objectivec_names.h). OBJC_SKIP_METHODS_WITHOUT_
 * MESSAGE_PREFIX is not defined by the Bazel build: ShouldIncludeMethod is always true. */

/* ---- objective_c_generator.h ---- */

typedef struct {
	/* Do not generate V1 interface and implementation */
	bool no_v1_compatibility;
} Parameters;

/* ---- objective_c_generator_helpers.h ---- */

static char *MessageHeaderName(const LpFile *file) { return xcat(lp_objc_file_path(file), ".pbobjc.h"); }

static bool AsciiIsUpper(char c) { return c >= 'A' && c <= 'Z'; }

static char *ServiceClassName(const LpService *service)
{
	const LpFile *file = service->file;
	const char *prefix = lp_objc_file_class_prefix(file);
	const char *class_name = service->name;
	/* We add the prefix in the cases where the string is missing a prefix. We define "missing
	 * a prefix" as where 'input':
	 * a) Doesn't start with the prefix or
	 * b) Isn't equivalent to the prefix or
	 * c) Has the prefix, but the letter after the prefix is lowercase
	 * This is the same semantics as the Objective-C protoc. */
	if (has_prefix(class_name, prefix)) {
		size_t n = strlen(prefix);
		if (!class_name[n] || !AsciiIsUpper(class_name[n]))
			return xcat(prefix, class_name);
		return xstrdup(class_name);
	}
	return xcat(prefix, class_name);
}

static char *LocalImport(const char *import) { return xcat("#import \"", import, "\"\n"); }

static char *FrameworkImport(const char *import, const char *framework)
{
	/* Flattens the directory structure: grab the file name only */
	const char *slash = strrchr(import, '/');
	const char *filename = slash ? slash + 1 : import;
	return xcat("#import <", framework, "/", filename, ">\n");
}

static char *SystemImport(const char *import) { return xcat("#import <", import, ">\n"); }

static char *PreprocConditional(const char *symbol, bool invert)
{
	return invert ? xcat("!defined(", symbol, ") || !", symbol) : xcat("defined(", symbol, ") && ", symbol);
}

static char *PreprocIfNot(const char *symbol, const char *if_true)
{
	return xcat("#if ", PreprocConditional(symbol, true), "\n", if_true, "#endif\n");
}

static char *PreprocIfElse(const char *symbol, const char *if_true, const char *if_false)
{
	return xcat("#if ", PreprocConditional(symbol, false), "\n", if_true, "#else\n", if_false, "#endif\n");
}

/* ---- objective_c_generator.cc ---- */

/* The map<string, string> vars of a method (GetMethodVars and the keys its callers add); NULL:
 * absent. */
typedef struct {
	const char *method_name, *request_type, *response_type, *request_class, *response_class;
	const char *client_stream, *server_stream, *return_type;
} MethodVars;

/* Printer::Print(vars, text) */
static void print_vars(LpPrinter *printer, const MethodVars *vars, const char *text)
{
	const char *kv[16];
	int n = 0;
#define OBJC_VAR(k)                                                                                \
	if (vars->k) {                                                                             \
		kv[2 * n] = #k;                                                                    \
		kv[2 * n + 1] = vars->k;                                                           \
		n++;                                                                               \
	}
	OBJC_VAR(method_name)
	OBJC_VAR(request_type)
	OBJC_VAR(response_type)
	OBJC_VAR(request_class)
	OBJC_VAR(response_class)
	OBJC_VAR(client_stream)
	OBJC_VAR(server_stream)
	OBJC_VAR(return_type)
#undef OBJC_VAR
	lp_printv(printer, text, n, kv);
}

static void PrintProtoRpcDeclarationAsPragma(LpPrinter *printer, const LpMethod *method, MethodVars vars)
{
	vars.client_stream = method->client_streaming ? "stream " : "";
	vars.server_stream = method->server_streaming ? "stream " : "";

	print_vars(printer, &vars,
		   "#pragma mark $method_name$($client_stream$$request_type$)"
		   " returns ($server_stream$$response_type$)\n\n");
}

static void PrintAllComments(const LpSourceLocations *locs, const ProtoDesc *desc, LpPrinter *printer,
			     bool deprecated)
{
	Vec comments = {0};
	grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING_DETACHED, &comments);
	grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING, &comments);
	grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_TRAILING, &comments);
	if (comments.n == 0)
		return;
	lp_print(printer, "/**\n", NULL);
	for (int i = 0; i < comments.n; i++) {
		const Str *it = comments.v[i];
		lp_print(printer, " * ", NULL);
		size_t start_pos = 0;
		while (start_pos < it->n && it->p[start_pos] == ' ')
			start_pos++;
		if (start_pos < it->n) { /* PrintRaw(c_str() + start_pos): up to a NUL byte */
			const char *nul = memchr(it->p + start_pos, 0, it->n - start_pos);
			size_t end = nul ? (size_t)(nul - it->p) : it->n;
			lp_write_raw(printer, (Str){it->p + start_pos, end - start_pos});
		}
		lp_print(printer, "\n", NULL);
	}
	if (deprecated) {
		lp_print(printer, " *\n", NULL);
		lp_print(printer,
			 " * This method belongs to a set of APIs that have been deprecated. Using"
			 " the v2 API is recommended.\n",
			 NULL);
	}
	lp_print(printer, " */\n", NULL);
}

static void PrintMethodSignature(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method,
				 const MethodVars *vars)
{
	/* Print comment */
	PrintAllComments(locs, (const ProtoDesc *)method, printer, true);

	print_vars(printer, vars, "- ($return_type$)$method_name$With");
	if (method->client_streaming)
		lp_print(printer, "RequestsWriter:(GRXWriter *)requestWriter", NULL);
	else
		print_vars(printer, vars, "Request:($request_class$ *)request");

	if (method->server_streaming)
		print_vars(printer, vars,
			   " eventHandler:(void(^)(BOOL done, "
			   "$response_class$ *_Nullable response, NSError *_Nullable "
			   "error))eventHandler");
	else
		print_vars(printer, vars,
			   " handler:(void(^)($response_class$ *_Nullable response, "
			   "NSError *_Nullable error))handler");
}

static void PrintSimpleSignature(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method,
				 MethodVars vars)
{
	vars.method_name = grpc_lowercase_first_letter(vars.method_name);
	vars.return_type = "void";
	PrintMethodSignature(locs, printer, method, &vars);
}

static void PrintAdvancedSignature(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method,
				   MethodVars vars)
{
	vars.method_name = xcat("RPCTo", vars.method_name);
	vars.return_type = "GRPCProtoCall *";
	PrintMethodSignature(locs, printer, method, &vars);
}

static void PrintV2Signature(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method,
			     MethodVars vars)
{
	vars.return_type = method->client_streaming ? "GRPCStreamingProtoCall *" : "GRPCUnaryProtoCall *";
	vars.method_name = grpc_lowercase_first_letter(vars.method_name);

	PrintAllComments(locs, (const ProtoDesc *)method, printer, false);

	print_vars(printer, &vars, "- ($return_type$)$method_name$With");
	if (method->client_streaming)
		lp_print(printer, "ResponseHandler:(id<GRPCProtoResponseHandler>)handler", NULL);
	else
		print_vars(printer, &vars,
			   "Message:($request_class$ *)message "
			   "responseHandler:(id<GRPCProtoResponseHandler>)handler");
	lp_print(printer, " callOptions:(GRPCCallOptions *_Nullable)callOptions", NULL);
}

static MethodVars GetMethodVars(const LpMethod *method)
{
	return (MethodVars){
		.method_name = method->name,
		.request_type = method->input_type->name,
		.response_type = method->output_type->name,
		.request_class = lp_objc_class_name(method->input_type),
		.response_class = lp_objc_class_name(method->output_type),
	};
}

static void PrintMethodDeclarations(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method)
{
	MethodVars vars = GetMethodVars(method);

	PrintProtoRpcDeclarationAsPragma(printer, method, vars);

	PrintSimpleSignature(locs, printer, method, vars);
	lp_print(printer, ";\n\n", NULL);
	PrintAdvancedSignature(locs, printer, method, vars);
	lp_print(printer, ";\n\n\n", NULL);
}

static void PrintV2MethodDeclarations(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method)
{
	MethodVars vars = GetMethodVars(method);

	PrintProtoRpcDeclarationAsPragma(printer, method, vars);

	PrintV2Signature(locs, printer, method, vars);
	lp_print(printer, ";\n\n", NULL);
}

static void PrintSimpleImplementation(LpPrinter *printer, const LpMethod *method, const MethodVars *vars)
{
	lp_print(printer, "{\n", NULL);
	print_vars(printer, vars, "  [[self RPCTo$method_name$With");
	if (method->client_streaming)
		lp_print(printer, "RequestsWriter:requestWriter", NULL);
	else
		lp_print(printer, "Request:request", NULL);
	if (method->server_streaming)
		lp_print(printer, " eventHandler:eventHandler] start];\n", NULL);
	else
		lp_print(printer, " handler:handler] start];\n", NULL);
	lp_print(printer, "}\n", NULL);
}

static void PrintAdvancedImplementation(LpPrinter *printer, const LpMethod *method, const MethodVars *vars)
{
	lp_print(printer, "{\n", NULL);
	print_vars(printer, vars, "  return [self RPCToMethod:@\"$method_name$\"\n");

	lp_print(printer, "            requestsWriter:", NULL);
	if (method->client_streaming)
		lp_print(printer, "requestWriter\n", NULL);
	else
		lp_print(printer, "[GRXWriter writerWithValue:request]\n", NULL);

	print_vars(printer, vars, "             responseClass:[$response_class$ class]\n");

	lp_print(printer, "        responsesWriteable:[GRXWriteable ", NULL);
	if (method->server_streaming)
		lp_print(printer, "writeableWithEventHandler:eventHandler]];\n", NULL);
	else
		lp_print(printer, "writeableWithSingleHandler:handler]];\n", NULL);

	lp_print(printer, "}\n", NULL);
}

static void PrintV2Implementation(LpPrinter *printer, const LpMethod *method, const MethodVars *vars)
{
	lp_print(printer, " {\n", NULL);
	print_vars(printer, vars, "  return [self RPCToMethod:@\"$method_name$\"\n");
	if (!method->client_streaming)
		lp_print(printer, "                   message:message\n", NULL);
	lp_print(printer, "           responseHandler:handler\n", NULL);
	lp_print(printer, "               callOptions:callOptions\n", NULL);
	print_vars(printer, vars, "             responseClass:[$response_class$ class]];\n}\n\n");
}

static void PrintMethodImplementations(const LpSourceLocations *locs, LpPrinter *printer, const LpMethod *method,
				       const Parameters *generator_params)
{
	MethodVars vars = GetMethodVars(method);

	PrintProtoRpcDeclarationAsPragma(printer, method, vars);

	if (!generator_params->no_v1_compatibility) {
		PrintSimpleSignature(locs, printer, method, vars);
		PrintSimpleImplementation(printer, method, &vars);

		lp_print(printer, "// Returns a not-yet-started RPC object.\n", NULL);
		PrintAdvancedSignature(locs, printer, method, vars);
		PrintAdvancedImplementation(printer, method, &vars);
	}

	PrintV2Signature(locs, printer, method, vars);
	PrintV2Implementation(printer, method, &vars);
}

static int cmp_strings(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Returns forward declaration of classes in the generated header file (a std::set: sorted, each
 * class once). */
static char *GetAllMessageClasses(const LpFile *file)
{
	Vec classes = {0};
	for (int i = 0; i < file->n_services; i++) {
		const LpService *service = &file->services[i];
		for (int j = 0; j < service->n_methods; j++) {
			const LpMethod *method = &service->methods[j];
			vec_push(&classes, lp_objc_class_name(method->input_type));
			vec_push(&classes, lp_objc_class_name(method->output_type));
		}
	}
	if (classes.n)
		qsort(classes.v, (size_t)classes.n, sizeof(void *), cmp_strings);
	Buf output = {0};
	for (int i = 0; i < classes.n; i++)
		if (i == 0 || !str_eq(classes.v[i], classes.v[i - 1]))
			buf_adds(&output, xcat("@class ", (const char *)classes.v[i], ";\n"));
	return (char *)buf_str(&output);
}

/* Returns the content to be included defining the @protocol segment at the insertion point of
 * the generated implementation file. This interface is legacy and for backwards compatibility. */
static char *GetProtocol(const LpSourceLocations *locs, const LpService *service, const Parameters *generator_params)
{
	if (generator_params->no_v1_compatibility)
		return "";

	Buf output = {0};
	LpPrinter printer;
	lp_printer_init(&printer, &output, '$');

	const char *service_class = ServiceClassName(service);

	lp_print(&printer,
		 "/**\n"
		 " * The methods in this protocol belong to a set of old APIs that have been deprecated. They do not\n"
		 " * recognize call options provided in the initializer. Using the v2 protocol is recommended.\n"
		 " */\n",
		 "service_class", service_class, NULL);
	lp_print(&printer, "@protocol $service_class$ <NSObject>\n\n", "service_class", service_class, NULL);
	for (int i = 0; i < service->n_methods; i++)
		PrintMethodDeclarations(locs, &printer, &service->methods[i]);
	lp_print(&printer, "@end\n\n", NULL);

	return (char *)buf_str(&output);
}

/* Returns the content to be included defining the @protocol segment at the insertion point of
 * the generated implementation file. */
static char *GetV2Protocol(const LpSourceLocations *locs, const LpService *service)
{
	Buf output = {0};
	LpPrinter printer;
	lp_printer_init(&printer, &output, '$');

	lp_print(&printer, "@protocol $service_class$ <NSObject>\n\n", "service_class",
		 xcat(ServiceClassName(service), "2"), NULL);
	for (int i = 0; i < service->n_methods; i++)
		PrintV2MethodDeclarations(locs, &printer, &service->methods[i]);
	lp_print(&printer, "@end\n\n", NULL);

	return (char *)buf_str(&output);
}

/* Returns the content to be included defining the @interface segment at the insertion point of
 * the generated implementation file. */
static char *GetInterface(const LpService *service, const Parameters *generator_params)
{
	Buf output = {0};
	LpPrinter printer;
	lp_printer_init(&printer, &output, '$');

	const char *service_class = ServiceClassName(service);

	lp_print(&printer,
		 "/**\n"
		 " * Basic service implementation, over gRPC, that only does\n"
		 " * marshalling and parsing.\n"
		 " */\n",
		 "service_class", service_class, NULL);
	lp_print(&printer, "@interface $service_class$ : GRPCProtoService<$service_class$2", "service_class",
		 service_class, NULL);
	if (!generator_params->no_v1_compatibility)
		lp_print(&printer, ", $service_class$", "service_class", service_class, NULL);
	lp_print(&printer, ">\n", NULL);
	lp_print(&printer,
		 "- (instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions "
		 "*_Nullable)callOptions NS_DESIGNATED_INITIALIZER;\n",
		 NULL);
	lp_print(&printer,
		 "+ (instancetype)serviceWithHost:(NSString *)host callOptions:(GRPCCallOptions "
		 "*_Nullable)callOptions;\n",
		 NULL);
	if (!generator_params->no_v1_compatibility) {
		lp_print(&printer, "// The following methods belong to a set of old APIs that have been deprecated.\n",
			 NULL);
		lp_print(&printer, "- (instancetype)initWithHost:(NSString *)host;\n", NULL);
		lp_print(&printer, "+ (instancetype)serviceWithHost:(NSString *)host;\n", NULL);
	}
	lp_print(&printer, "@end\n", NULL);

	return (char *)buf_str(&output);
}

/* Returns the content to be included in the "global_scope" insertion point of the generated
 * implementation file. */
static char *GetSource(const LpSourceLocations *locs, const LpService *service, const Parameters *generator_params)
{
	Buf output = {0};
	LpPrinter printer;
	lp_printer_init(&printer, &output, '$');

	const char *service_name = service->name, *service_class = ServiceClassName(service),
		   *package = service->file->package;

	lp_print(&printer,
		 "@implementation $service_class$\n\n"
		 "#pragma clang diagnostic push\n"
		 "#pragma clang diagnostic ignored \"-Wobjc-designated-initializers\"\n\n"
		 "// Designated initializer\n"
		 "- (instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *_Nullable)callOptions {\n"
		 "  return [super initWithHost:host\n"
		 "                 packageName:@\"$package$\"\n"
		 "                 serviceName:@\"$service_name$\"\n"
		 "                 callOptions:callOptions];\n"
		 "}\n\n",
		 "service_name", service_name, "service_class", service_class, "package", package, NULL);
	if (!generator_params->no_v1_compatibility)
		lp_print(&printer,
			 "- (instancetype)initWithHost:(NSString *)host {\n"
			 "  return [super initWithHost:host\n"
			 "                 packageName:@\"$package$\"\n"
			 "                 serviceName:@\"$service_name$\"];\n"
			 "}\n\n",
			 "service_name", service_name, "service_class", service_class, "package", package, NULL);
	lp_print(&printer, "#pragma clang diagnostic pop\n\n", NULL);

	if (!generator_params->no_v1_compatibility)
		lp_print(&printer,
			 "// Override superclass initializer to disallow different package and service names.\n"
			 "- (instancetype)initWithHost:(NSString *)host\n"
			 "                 packageName:(NSString *)packageName\n"
			 "                 serviceName:(NSString *)serviceName {\n"
			 "  return [self initWithHost:host];\n"
			 "}\n\n",
			 NULL);
	lp_print(&printer,
		 "- (instancetype)initWithHost:(NSString *)host\n"
		 "                 packageName:(NSString *)packageName\n"
		 "                 serviceName:(NSString *)serviceName\n"
		 "                 callOptions:(GRPCCallOptions *)callOptions {\n"
		 "  return [self initWithHost:host callOptions:callOptions];\n"
		 "}\n\n",
		 NULL);

	lp_print(&printer, "#pragma mark - Class Methods\n\n", NULL);
	if (!generator_params->no_v1_compatibility)
		lp_print(&printer,
			 "+ (instancetype)serviceWithHost:(NSString *)host {\n"
			 "  return [[self alloc] initWithHost:host];\n"
			 "}\n\n",
			 NULL);
	lp_print(&printer,
		 "+ (instancetype)serviceWithHost:(NSString *)host callOptions:(GRPCCallOptions *_Nullable)callOptions {\n"
		 "  return [[self alloc] initWithHost:host callOptions:callOptions];\n"
		 "}\n\n",
		 NULL);

	lp_print(&printer, "#pragma mark - Method Implementations\n\n", NULL);

	for (int i = 0; i < service->n_methods; i++)
		PrintMethodImplementations(locs, &printer, &service->methods[i], generator_params);

	lp_print(&printer, "@end\n", NULL);
	return (char *)buf_str(&output);
}
