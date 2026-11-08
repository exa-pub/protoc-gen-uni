/*
 * Copyright 2017, gRPC Authors All rights reserved.
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

/* Port of Sources/protoc-gen-grpc-swift/Generator.swift, Generator-Names.swift,
 * Generator-Metadata.swift, Types.swift and StreamingType.swift (grpc-swift 1.27.6).
 *
 * Upstream recomputes the names of the current service and method (self.service, self.method) on
 * every use; set_service and set_method compute them once. Closures passed to
 * withIndentation(_:braces:_:) and printFunction become open_block/close_block and
 * print_function/end_function around the body. */

typedef enum { UNARY, CLIENT_STREAMING, SERVER_STREAMING, BIDIRECTIONAL_STREAMING } StreamingType;

typedef struct {
	GeneratorOptions *options;
	SwPrinter printer;
	SwFile *file;
	SwNamer *protobuf_namer;
	const char *access;

	/* The service being printed and its names. */
	SwService *service;
	const char *package_service; /* nameForPackageService */
	const char *service_path;

	/* The method being printed and its names. */
	SwMethod *method;
	StreamingType stream_type;
	const char *method_function_name, *method_input_name, *method_output_name;
	const char *method_path_using_client_metadata;
} Generator;

/* ---- StreamingType.swift ---- */

static StreamingType streaming_type(const SwMethod *m)
{
	if (m->client_streaming)
		return m->server_streaming ? BIDIRECTIONAL_STREAMING : CLIENT_STREAMING;
	return m->server_streaming ? SERVER_STREAMING : UNARY;
}

/* asGRPCCallTypeCase */
static const char *const grpc_call_type_cases[] = {
	"GRPCCallType.unary", "GRPCCallType.clientStreaming", "GRPCCallType.serverStreaming",
	"GRPCCallType.bidirectionalStreaming",
};

/* ---- Types.swift ---- */

/* Types.call(for:withGRPCPrefix: false); the GRPC prefix is printed by the callers. */
static const char *const call_types[] = {
	"AsyncUnaryCall", "AsyncClientStreamingCall", "AsyncServerStreamingCall", "AsyncBidirectionalStreamingCall",
};

/* ---- Generator.swift: printing ---- */

static void println_str(Generator *g, Str text, bool newline)
{
	sw_print_str(&g->printer, text);
	if (newline)
		sw_print(&g->printer, "\n");
}

static void println(Generator *g, const char *text) { println_str(g, str_c(text), true); }

static void printlnf(Generator *g, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void printlnf(Generator *g, const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	println_str(g, buf_view(&b), true);
}

static void indent(Generator *g) { sw_indent(&g->printer); }
static void outdent(Generator *g) { sw_outdent(&g->printer); }

typedef enum { BRACES_NONE, BRACES_CURLY, BRACES_ROUND } Braces;

/* withIndentation(_:braces:_:) up to the body. */
static void open_block(Generator *g, const char *header, Braces braces)
{
	static const char *const open[] = {"", " {", "("};
	printlnf(g, "%s%s", header, open[braces]);
	indent(g);
}

static void close_block(Generator *g, Braces braces)
{
	static const char *const close[] = {"", "}", ")"};
	outdent(g);
	println(g, close[braces]);
}

static void print_availability_for_async_await(Generator *g)
{
	println(g, "@available(macOS 10.15, iOS 13, tvOS 13, watchOS 6, *)");
}

/* ---- Generator-Names.swift ---- */

/* nameForPackageService */
static const char *name_for_package_service(const SwFile *file, const SwService *service)
{
	if (*file->package)
		return xcat(sw_type_prefix(file->package, file->options), service->name);
	return service->name;
}

/* quotableFieldNames: the Swift keywords used in declarations, statements, expressions and
 * types. */
static const char *const quotable_field_names[] = {
	"associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func", "import", "init", "inout",
	"internal", "let", "open", "operator", "private", "protocol", "public", "static", "struct", "subscript",
	"typealias", "var", "break", "case", "continue", "default", "defer", "do", "else", "fallthrough", "for",
	"guard", "if", "in", "repeat", "return", "switch", "where", "while", "as", "Any", "catch", "false", "is",
	"nil", "rethrows", "super", "self", "Self", "throw", "throws", "true", "try",
};

/* sanitize(fieldName:) */
static const char *sanitize_field_name(const char *s)
{
	for (size_t i = 0; i < sizeof quotable_field_names / sizeof quotable_field_names[0]; i++)
		if (str_eq(s, quotable_field_names[i]))
			return xcat("`", s, "`");
	return s;
}

/* The first Character of name uppercased or lowercased, the rest unchanged (names are ASCII). */
static char *with_first(const char *name, bool upper)
{
	char *s = xstrdup(name);
	if (*s >= 'a' && *s <= 'z' && upper)
		*s = (char)(*s - 'a' + 'A');
	else if (*s >= 'A' && *s <= 'Z' && !upper)
		*s = (char)(*s - 'A' + 'a');
	return s;
}

static const char *pkg_name(Generator *g, const char *suffix) { return xcat(g->package_service, suffix); }

#define PROVIDER_NAME(g) pkg_name(g, "Provider")
#define ASYNC_PROVIDER_NAME(g) pkg_name(g, "AsyncProvider")
#define CLIENT_CLASS_NAME(g) pkg_name(g, "Client")
#define CLIENT_STRUCT_NAME(g) pkg_name(g, "NIOClient")
#define ASYNC_CLIENT_STRUCT_NAME(g) pkg_name(g, "AsyncClient")
#define TEST_CLIENT_CLASS_NAME(g) pkg_name(g, "TestClient")
#define CLIENT_PROTOCOL_NAME(g) pkg_name(g, "ClientProtocol")
#define ASYNC_CLIENT_PROTOCOL_NAME(g) pkg_name(g, "AsyncClientProtocol")
#define CLIENT_INTERCEPTOR_PROTOCOL_NAME(g) pkg_name(g, "ClientInterceptorFactoryProtocol")
#define SERVER_INTERCEPTOR_PROTOCOL_NAME(g) pkg_name(g, "ServerInterceptorFactoryProtocol")
#define SERVICE_SERVER_METADATA(g) pkg_name(g, "ServerMetadata")
#define SERVICE_CLIENT_METADATA(g) pkg_name(g, "ClientMetadata")

/* methodFunctionName */
static const char *method_function_name(Generator *g, const SwMethod *m)
{
	const char *name = m->name;
	if (!g->options->keep_method_casing)
		name = with_first(name, false);
	return sanitize_field_name(name);
}

/* methodMakeFunctionCallName */
static const char *method_make_function_call_name(Generator *g)
{
	const char *name = g->options->keep_method_casing ? g->method->name : sw_to_upper_camel(g->method->name);
	return sanitize_field_name(xcat("make", name, "Call"));
}

/* methodComposableName */
static const char *method_composable_name(Generator *g)
{
	if (g->options->keep_method_casing)
		return g->method->name;
	return with_first(g->method->name, true);
}

/* methodMakeFunctionCallWrapperName */
static const char *method_make_function_call_wrapper_name(Generator *g)
{
	return xcat("make", method_composable_name(g), "Call");
}

/* methodInterceptorFactoryName */
static const char *method_interceptor_factory_name(Generator *g) { return xcat("make", g->method->name, "Interceptors"); }

/* methodPath */
static const char *method_path(Generator *g) { return xcat("/", g->service_path, "/", g->method->name); }

static void set_service(Generator *g, SwService *service)
{
	g->service = service;
	g->package_service = name_for_package_service(g->file, service);
	g->service_path = *g->file->package ? xcat(g->file->package, ".", service->name) : service->name;
}

static void set_method(Generator *g, SwMethod *method)
{
	g->method = method;
	g->stream_type = streaming_type(method);
	g->method_function_name = method_function_name(g, method);
	g->method_input_name = sw_full_name_message(g->protobuf_namer, method->input_type);
	g->method_output_name = sw_full_name_message(g->protobuf_namer, method->output_type);
	/* methodPathUsingClientMetadata */
	g->method_path_using_client_metadata =
		xcat(SERVICE_CLIENT_METADATA(g), ".Methods.", g->method_function_name, ".path");
}

/* ---- Generator-Metadata.swift ---- */

static void print_metadata(Generator *g, bool server)
{
	const char *enum_name = server ? SERVICE_SERVER_METADATA(g) : SERVICE_CLIENT_METADATA(g);
	const char *access = g->access;
	open_block(g, xcat(access, " enum ", enum_name), BRACES_CURLY);
	printlnf(g, "%s static let serviceDescriptor = GRPCServiceDescriptor(", access);
	indent(g);
	printlnf(g, "name: \"%s\",", g->service->name);
	printlnf(g, "fullName: \"%s\",", g->service_path);
	println(g, "methods: [");
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		indent(g);
		printlnf(g, "%s.Methods.%s,", enum_name, g->method_function_name);
		outdent(g);
	}
	println(g, "]");
	outdent(g);
	println(g, ")");
	println(g, "");

	open_block(g, xcat(access, " enum Methods"), BRACES_CURLY);
	for (int i = 0; i < g->service->methods.n; i++) {
		set_method(g, g->service->methods.v[i]);
		printlnf(g, "%s static let %s = GRPCMethodDescriptor(", access, g->method_function_name);
		indent(g);
		printlnf(g, "name: \"%s\",", g->method->name);
		printlnf(g, "path: \"%s\",", method_path(g));
		printlnf(g, "type: %s", grpc_call_type_cases[g->stream_type]);
		outdent(g);
		println(g, ")");
		if (i + 1 < g->service->methods.n)
			println(g, "");
	}
	close_block(g, BRACES_CURLY);
	close_block(g, BRACES_CURLY);
}

/* ---- Generator-Client.swift: printFunction ---- */

typedef struct {
	const char *name;
	const char *const *arguments;
	int n_arguments;
	const char *return_type; /* NULL: none */
	const char *access;      /* NULL: none */
	bool sendable, async, throws;
	const char *generic_where_clause; /* NULL: none */
} Function;

/* printFunction up to the body: with a body, prints " {" and indents; end_function closes it. */
static void print_function(Generator *g, const Function *f, bool has_body)
{
	Buf head = {0};
	if (f->access)
		buf_fmt(&head, "%s ", f->access);
	if (f->sendable)
		buf_adds(&head, "@Sendable ");
	Buf tail = {0};
	if (f->async)
		buf_adds(&tail, " async");
	if (f->throws)
		buf_adds(&tail, " throws");
	if (f->return_type)
		buf_fmt(&tail, " -> %s", f->return_type);
	if (f->generic_where_clause)
		buf_fmt(&tail, " %s", f->generic_where_clause);
	if (!f->n_arguments) {
		Buf line = {0};
		buf_fmt(&line, "%sfunc %s()%s", buf_str(&head), f->name, buf_str(&tail));
		println_str(g, buf_view(&line), !has_body);
	} else {
		printlnf(g, "%sfunc %s(", buf_str(&head), f->name);
		indent(g);
		for (int i = 0; i < f->n_arguments; i++)
			printlnf(g, "%s%s", f->arguments[i], i + 1 < f->n_arguments ? "," : "");
		outdent(g);
		Buf line = {0};
		buf_fmt(&line, ")%s", buf_str(&tail));
		println_str(g, buf_view(&line), !has_body);
	}
	if (has_body) {
		println(g, " {");
		indent(g);
	}
}

static void end_function(Generator *g)
{
	outdent(g);
	println(g, "}");
}

/* ---- Generator.swift: printMain ---- */

static void print_client(Generator *g);
static void print_server(Generator *g);

/* Swift's String < for the module names: by Unicode scalars, which is by bytes for ASCII. Other
 * strings compare after normalization (not ported): print_main checks they are ASCII. */
static int compare_modules(const void *a, const void *b)
{
	const Str *x = *(Str *const *)a, *y = *(Str *const *)b;
	size_t n = x->n < y->n ? x->n : y->n;
	int c = memcmp(x->p, y->p, n);
	if (c)
		return c;
	return x->n < y->n ? -1 : x->n > y->n;
}

static void print_main(Generator *g)
{
	println(g, "//");
	println(g, "// DO NOT EDIT.");
	println(g, "// swift-format-ignore-file");
	println(g, "//");
	println(g, "// Generated by the protocol buffer compiler.");
	printlnf(g, "// Source: %s", g->file->name);
	println(g, "//");

	Vec modules = {0};
	vec_push(&modules, &g->options->grpc_module_name);
	vec_push(&modules, new_str(STR_LIT("NIO")));
	vec_push(&modules, new_str(STR_LIT("NIOConcurrencyHelpers")));
	vec_push(&modules, &g->options->swift_protobuf_module_name);
	for (int i = 0; i < g->options->extra_module_imports.n; i++)
		vec_push(&modules, g->options->extra_module_imports.v[i]);
	for (int i = 0; i < modules.n; i++) {
		const Str *s = modules.v[i];
		for (size_t j = 0; j < s->n; j++)
			if ((unsigned char)s->p[j] >= 0x80)
				fail("unsupported by protoc-gen-uni: non-ASCII character in module name %s", sw_quote(*s));
	}
	/* sorted() is stable; equal names are equal bytes here. */
	qsort(modules.v, (size_t)modules.n, sizeof modules.v[0], compare_modules);
	for (int i = 0; i < modules.n; i++) {
		Buf line = {0};
		buf_adds(&line, "import ");
		buf_add_str(&line, *(Str *)modules.v[i]);
		println_str(g, buf_view(&line), true);
	}
	/* moduleMappings.neededModules(forFile:) ?? []: the mappings have no entries besides the
	 * bundled ones (no mappings file is read), so there are none. */
	println(g, "");

	/* We defer the check for printing clients to `printClient()` since this could be the 'real'
	 * client or the test client. */
	for (int i = 0; i < g->file->services.n; i++) {
		set_service(g, g->file->services.v[i]);
		print_client(g);
	}
	println(g, "");

	if (g->options->generate_server) {
		for (int i = 0; i < g->file->services.n; i++) {
			set_service(g, g->file->services.v[i]);
			print_server(g);
		}
	}
}

/* Generator(_:options:).code */
static Str generator_code(SwFile *file, GeneratorOptions *options)
{
	Generator g = {0};
	g.file = file;
	g.options = options;
	g.access = visibility_snippets[options->visibility];
	sw_printer_init(&g.printer, false);
	g.protobuf_namer = sw_namer(file, options->proto_to_module_mappings);
	print_main(&g);
	return buf_view(&g.printer.content);
}
