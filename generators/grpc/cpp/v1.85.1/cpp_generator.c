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

/* Port of src/compiler/cpp_generator.{h,cc} of grpc v1.85.1 (namespace grpc_cpp_generator), with
 * the parts of cpp_generator_helpers.h (ClassName, GetCppComments), protobuf_plugin.h
 * (ProtoBufFile, ProtoBufService, ProtoBufMethod) and config.h it uses. The Get* functions append
 * to output what upstream returns as a string (each with its own printer). */

/* config.h */
static const char kCppGeneratorMessageHeaderExt[] = ".pb.h";
static const char kCppGeneratorServiceHeaderExt[] = ".grpc.pb.h";

enum { SERVICEOPT_DEPRECATED = 33 };

/* Contains all the parameters that are parsed from the command line. */
typedef struct {
	/* Puts the service into a namespace */
	const char *services_namespace;
	/* Use system includes (<>) or local includes ("") */
	bool use_system_headers;
	/* Prefix to any grpc include */
	const char *grpc_search_path;
	/* Generate Google Mock code to facilitate unit testing. */
	bool generate_mock_code;
	/* Google Mock search path, when non-empty, local includes will be used. */
	const char *gmock_search_path;
	/* *EXPERIMENTAL* Additional include files in grpc.pb.h (char *) */
	Vec additional_header_includes;
	/* By default, use "pb.h" */
	const char *message_header_extension;
	/* Whether to include headers corresponding to imports in source file. */
	bool include_import_headers;
	/* Whether to expose synchronous server API. */
	bool allow_sync_server_api;
	/* Whether to generate completion queue API. */
	bool allow_cq_api;
	/* whether to add deprecated warning for services */
	bool allow_deprecated;
} Parameters;

/* ProtoBufFile: the file and its source locations (for the comments). */
typedef struct {
	const LpFile *file;
	const LpSourceLocations *locs;
} ProtoBufFile;

/* The printer of a Get* function (File::CreatePrinter), appending to output. */
static LpPrinter *cg_printer(LpPrinter *p, Buf *output)
{
	lp_printer_init(p, output, '$');
	p->debug_checks = true; /* the Bazel fastbuild of the plugin keeps the debug checks */
	return p;
}

#define P(text) lp_print(printer, (text), NULL)
#define PV(text) lp_print_vars(printer, vars, (text))
#define PRAW(text) lp_write_raw(printer, str_c(text))
#define SET(key, value) lp_vars_set(vars, (key), (value))
/* for (const auto& async_prefix : async_prefixes) over the function's two prefixes */
#define FOR_ASYNC_PREFIX                                                                           \
	for (const AsyncPrefix *async_prefix = async_prefixes; async_prefix < async_prefixes + 2; async_prefix++)

/* ---- cpp_generator_helpers.h ---- */

/* ClassName(descriptor, true): the qualified C++ name of a message ("::" + the top-level message
 * with dots as "::", then the nested names joined with "_"). */
static char *type_name(const LpMessage *descriptor)
{
	/* Find "outer", the descriptor of the top-level message in which "descriptor" is embedded. */
	const LpMessage *outer = descriptor;
	while (outer->containing_type)
		outer = outer->containing_type;
	const char *outer_name = outer->full_name;
	const char *inner_name = descriptor->full_name + strlen(outer_name);
	return xcat("::", grpc_string_replace(outer_name, ".", "::", true),
		    grpc_string_replace(inner_name, ".", "_", true));
}

/* GetCppComments: comment lines start with "//"; the c_str() passed to Print (cut at the first
 * NUL). */
static const char *cpp_comments(const LpSourceLocations *locs, const void *desc, bool leading)
{
	return grpc_get_prefixed_comments(locs, desc, leading, "//").p;
}

/* ---- protobuf_plugin.h (ProtoBufMethod) ---- */

static bool no_streaming(const LpMethod *method) { return !method->client_streaming && !method->server_streaming; }
static bool bidi_streaming(const LpMethod *method) { return method->client_streaming && method->server_streaming; }

/* ---- cpp_generator.cc ---- */

static bool client_only_streaming(const LpMethod *method)
{
	return method->client_streaming && !method->server_streaming;
}

static bool server_only_streaming(const LpMethod *method)
{
	return !method->client_streaming && method->server_streaming;
}

/* FilenameIdentifier: alphanumerics kept, other bytes (char is signed) as _<hex>. */
static char *FilenameIdentifier(const char *filename)
{
	static const char hex[] = "0123456789abcdef";
	Buf result = {0};
	for (const char *p = filename; *p; p++) {
		signed char c = (signed char)*p;
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			buf_addc(&result, c);
		} else {
			buf_addc(&result, '_');
			buf_addc(&result, hex[(c >> 4) & 0xf]);
			buf_addc(&result, hex[c & 0xf]);
		}
	}
	return xstrdup(buf_str(&result));
}

static const char *message_header_ext(const Parameters *params)
{
	return *params->message_header_extension ? params->message_header_extension : kCppGeneratorMessageHeaderExt;
}

static COLD void PrintIncludes(LpPrinter *printer, const char *const *headers, int n_headers, bool use_system_headers,
			  const char *search_path)
{
	LpVars vars_ = {0}, *vars = &vars_;

	const char *l = use_system_headers ? "<" : "\"";
	SET("r", use_system_headers ? ">" : "\"");

	size_t n = strlen(search_path);
	if (n) {
		l = xcat(l, search_path);
		if (search_path[n - 1] != '/')
			l = xcat(l, "/");
	}
	SET("l", l);

	for (int i = 0; i < n_headers; i++) {
		SET("h", headers[i]);
		PV("#include $l$$h$$r$\n");
	}
}

static COLD void GetHeaderPrologue(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	SET("filename", file->file->name);
	SET("filename_identifier", FilenameIdentifier(file->file->name));
	SET("filename_base", grpc_strip_proto(file->file->name));
	SET("message_header_ext", message_header_ext(params));

	PV("// Generated by the gRPC C++ plugin.\n");
	PV("// If you make any local change, they will be lost.\n");
	PV("// source: $filename$\n");
	const char *leading_comments = cpp_comments(file->locs, file->file, true);
	if (*leading_comments) {
		PV("// Original file comments:\n");
		PRAW(leading_comments);
	}
	PV("#ifndef GRPC_$filename_identifier$__INCLUDED\n");
	PV("#define GRPC_$filename_identifier$__INCLUDED\n");
	PV("\n");
	PV("#include \"$filename_base$$message_header_ext$\"\n");
	PV(""); /* file->additional_headers() */
	PV("\n");
}

/* Convert from "a/b/c.proto" to "#include \"a/b/c$message_header_ext$\"\n" */
static char *ImportIncludeFromProtoName(const char *proto_name)
{
	size_t n = strlen(proto_name);
	/* substr(0, size() - 6): the whole name when it is shorter */
	return xcat("#include \"", xstrndup(proto_name, n >= 6 ? n - 6 : n), "$message_header_ext$\"\n");
}

static COLD void print_namespaces_open(LpPrinter *printer, LpVars *vars, const LpFile *file)
{
	Vec parts = grpc_tokenize(file->package, ".");
	for (int i = 0; i < parts.n; i++) {
		SET("part", parts.v[i]);
		PV("namespace $part$ {\n");
	}
}

static COLD void print_import_includes(LpPrinter *printer, LpVars *vars, const LpFile *file)
{
	for (int i = 0; i < file->n_dependencies; i++)
		PV(ImportIncludeFromProtoName(file->dependencies[i]->name));
	PRAW("\n");
}

static COLD void GetHeaderIncludes(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	if (params->additional_header_includes.n)
		PrintIncludes(printer, (const char *const *)params->additional_header_includes.v,
			      params->additional_header_includes.n, false, "");
	static const char *const headers_strs[] = {
		"functional",
		"grpcpp/generic/async_generic_service.h",
		"grpcpp/support/async_stream.h",
		"grpcpp/support/async_unary_call.h",
		"grpcpp/support/client_callback.h",
		"grpcpp/client_context.h",
		"grpcpp/completion_queue.h",
		"grpcpp/support/message_allocator.h",
		"grpcpp/support/method_handler.h",
		"grpcpp/impl/proto_utils.h",
		"grpcpp/impl/rpc_method.h",
		"grpcpp/support/server_callback.h",
		"grpcpp/impl/server_callback_handlers.h",
		"grpcpp/server_context.h",
		"grpcpp/impl/service_type.h",
		"grpcpp/support/status.h",
		"grpcpp/support/stub_options.h",
		"grpcpp/support/sync_stream.h",
		/* ports_def.inc Must be included at last */
		"grpcpp/ports_def.inc",
	};
	PrintIncludes(printer, headers_strs, (int)(sizeof headers_strs / sizeof *headers_strs),
		      params->use_system_headers, params->grpc_search_path);
	PV("\n");

	SET("message_header_ext", message_header_ext(params));

	if (params->include_import_headers)
		print_import_includes(printer, vars, file->file);

	if (*file->file->package) {
		print_namespaces_open(printer, vars, file->file);
		PV("\n");
	}
}

static COLD void PrintHeaderClientMethodInterfaces(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars, bool is_public)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));

	typedef struct {
		const char *prefix;
		const char *method_params;  /* extra arguments to method */
		const char *raw_args;       /* extra arguments to raw version of method */
	} AsyncPrefix;
	static const AsyncPrefix async_prefixes[] = {{"Async", ", void* tag", ", tag"},
			{"PrepareAsync", "", ""}};

	if (is_public) {
		if (no_streaming(method)) {
			PV("virtual ::grpc::Status $Method$(::grpc::ClientContext* context, "
					"const $Request$& request, $Response$* response) = 0;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					PV("std::unique_ptr< "
							"::grpc::ClientAsyncResponseReaderInterface< $Response$>> "
							"$AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"const $Request$& request, "
							"::grpc::CompletionQueue* cq) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncResponseReaderInterface< $Response$>>("
							"$AsyncPrefix$$Method$Raw(context, request, cq));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (client_only_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientWriterInterface< $Request$>>"
					" $Method$("
					"::grpc::ClientContext* context, $Response$* response) {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< ::grpc::ClientWriterInterface< $Request$>>"
					"($Method$Raw(context, response));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr< ::grpc::ClientAsyncWriterInterface< $Request$>>"
							" $AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"$Response$* "
							"response, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncWriterInterface< $Request$>>("
							"$AsyncPrefix$$Method$Raw(context, response, "
							"cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (server_only_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientReaderInterface< $Response$>>"
					" $Method$(::grpc::ClientContext* context, const $Request$& request)"
					" {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< ::grpc::ClientReaderInterface< $Response$>>"
					"($Method$Raw(context, request));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr< ::grpc::ClientAsyncReaderInterface< "
							"$Response$>> "
							"$AsyncPrefix$$Method$("
							"::grpc::ClientContext* context, const $Request$& request, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncReaderInterface< $Response$>>("
							"$AsyncPrefix$$Method$Raw(context, request, "
							"cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (bidi_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientReaderWriterInterface< "
					"$Request$, $Response$>> "
					"$Method$(::grpc::ClientContext* context) {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< "
					"::grpc::ClientReaderWriterInterface< $Request$, $Response$>>("
					"$Method$Raw(context));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr< "
							"::grpc::ClientAsyncReaderWriterInterface< $Request$, "
							"$Response$>> "
							"$AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncReaderWriterInterface< $Request$, "
							"$Response$>>("
							"$AsyncPrefix$$Method$Raw(context, cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		}
	} else {
		if (no_streaming(method)) {
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					PV("virtual ::grpc::ClientAsyncResponseReaderInterface< "
							"$Response$>* "
							"$AsyncPrefix$$Method$Raw(::grpc::ClientContext* context, "
							"const $Request$& request, "
							"::grpc::CompletionQueue* cq) = 0;\n");
				}
			}
		} else if (client_only_streaming(method)) {
			PV("virtual ::grpc::ClientWriterInterface< $Request$>*"
					" $Method$Raw("
					"::grpc::ClientContext* context, $Response$* response) = 0;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					PV("virtual ::grpc::ClientAsyncWriterInterface< $Request$>*"
							" $AsyncPrefix$$Method$Raw(::grpc::ClientContext* context, "
							"$Response$* response, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) = 0;\n");
				}
			}
		} else if (server_only_streaming(method)) {
			PV("virtual ::grpc::ClientReaderInterface< $Response$>* "
					"$Method$Raw("
					"::grpc::ClientContext* context, const $Request$& request) = 0;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					PV("virtual ::grpc::ClientAsyncReaderInterface< $Response$>* "
							"$AsyncPrefix$$Method$Raw("
							"::grpc::ClientContext* context, const $Request$& request, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) = 0;\n");
				}
			}
		} else if (bidi_streaming(method)) {
			PV("virtual ::grpc::ClientReaderWriterInterface< $Request$, "
					"$Response$>* "
					"$Method$Raw(::grpc::ClientContext* context) = 0;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					PV("virtual ::grpc::ClientAsyncReaderWriterInterface< "
							"$Request$, $Response$>* "
							"$AsyncPrefix$$Method$Raw(::grpc::ClientContext* context, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) = 0;\n");
				}
			}
		}
	}
}

static COLD void PrintHeaderClientMethod(LpPrinter *printer, const LpMethod *method, const Parameters *params,
			LpVars *vars, bool is_public)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	typedef struct {
		const char *prefix;
		const char *method_params;  /* extra arguments to method */
		const char *raw_args;       /* extra arguments to raw version of method */
	} AsyncPrefix;
	static const AsyncPrefix async_prefixes[] = {{"Async", ", void* tag", ", tag"},
			{"PrepareAsync", "", ""}};

	if (is_public) {
		if (no_streaming(method)) {
			PV("::grpc::Status $Method$(::grpc::ClientContext* context, "
					"const $Request$& request, $Response$* response) override;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					PV("std::unique_ptr< ::grpc::ClientAsyncResponseReader< "
							"$Response$>> "
							"$AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"const $Request$& request, "
							"::grpc::CompletionQueue* cq) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncResponseReader< $Response$>>("
							"$AsyncPrefix$$Method$Raw(context, request, cq));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (client_only_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientWriter< $Request$>>"
					" $Method$("
					"::grpc::ClientContext* context, $Response$* response) {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< ::grpc::ClientWriter< $Request$>>"
					"($Method$Raw(context, response));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr< ::grpc::ClientAsyncWriter< $Request$>>"
							" $AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"$Response$* response, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< ::grpc::ClientAsyncWriter< $Request$>>("
							"$AsyncPrefix$$Method$Raw(context, response, "
							"cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (server_only_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientReader< $Response$>>"
					" $Method$(::grpc::ClientContext* context, const $Request$& request)"
					" {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< ::grpc::ClientReader< $Response$>>"
					"($Method$Raw(context, request));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr< ::grpc::ClientAsyncReader< $Response$>> "
							"$AsyncPrefix$$Method$("
							"::grpc::ClientContext* context, const $Request$& request, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< ::grpc::ClientAsyncReader< $Response$>>("
							"$AsyncPrefix$$Method$Raw(context, request, "
							"cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		} else if (bidi_streaming(method)) {
			PV("std::unique_ptr< ::grpc::ClientReaderWriter< $Request$, $Response$>>"
					" $Method$(::grpc::ClientContext* context) {\n");
			lp_indent(printer);
			PV("return std::unique_ptr< "
					"::grpc::ClientReaderWriter< $Request$, $Response$>>("
					"$Method$Raw(context));\n");
			lp_outdent(printer);
			P("}\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("std::unique_ptr<  ::grpc::ClientAsyncReaderWriter< "
							"$Request$, $Response$>> "
							"$AsyncPrefix$$Method$(::grpc::ClientContext* context, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
					lp_indent(printer);
					PV("return std::unique_ptr< "
							"::grpc::ClientAsyncReaderWriter< $Request$, $Response$>>("
							"$AsyncPrefix$$Method$Raw(context, cq$AsyncRawArgs$));\n");
					lp_outdent(printer);
					P("}\n");
				}
			}
		}
	} else {
		if (no_streaming(method)) {
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					PV("::grpc::ClientAsyncResponseReader< $Response$>* "
							"$AsyncPrefix$$Method$Raw(::grpc::ClientContext* context, "
							"const $Request$& request, "
							"::grpc::CompletionQueue* cq) override;\n");
				}
			}
		} else if (client_only_streaming(method)) {
			PV("::grpc::ClientWriter< $Request$>* $Method$Raw("
					"::grpc::ClientContext* context, $Response$* response) "
					"override;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("::grpc::ClientAsyncWriter< $Request$>* $AsyncPrefix$$Method$Raw("
							"::grpc::ClientContext* context, $Response$* response, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) override;\n");
				}
			}
		} else if (server_only_streaming(method)) {
			PV("::grpc::ClientReader< $Response$>* $Method$Raw("
					"::grpc::ClientContext* context, const $Request$& request)"
					" override;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("::grpc::ClientAsyncReader< $Response$>* "
							"$AsyncPrefix$$Method$Raw("
							"::grpc::ClientContext* context, const $Request$& request, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) override;\n");
				}
			}
		} else if (bidi_streaming(method)) {
			PV("::grpc::ClientReaderWriter< $Request$, $Response$>* "
					"$Method$Raw(::grpc::ClientContext* context) override;\n");
			if (params->allow_cq_api) {
				FOR_ASYNC_PREFIX {
					SET("AsyncPrefix", async_prefix->prefix);
					SET("AsyncMethodParams", async_prefix->method_params);
					SET("AsyncRawArgs", async_prefix->raw_args);
					PV("::grpc::ClientAsyncReaderWriter< $Request$, $Response$>* "
							"$AsyncPrefix$$Method$Raw(::grpc::ClientContext* context, "
							"::grpc::CompletionQueue* cq$AsyncMethodParams$) override;\n");
				}
			}
		}
	}
}

static COLD void PrintHeaderClientMethodCallbackInterfacesStart(LpPrinter *printer, LpVars *vars)
{
	/* This declares the interface for the callback-based API. The components */
	/* are pure; even though this is new (post-1.0) API, it can be pure because */
	/* it is an entirely new interface that happens to be scoped within */
	/* StubInterface, not new additions to StubInterface itself */
	P("class async_interface {\n");
	/* All methods in this new interface are public. There is no need for private */
	/* "Raw" methods since the callback-based API returns unowned raw pointers */
	P(" public:\n");
	lp_indent(printer);
	P("virtual ~async_interface() {}\n");
}

static COLD void PrintHeaderClientMethodCallbackInterfaces(LpPrinter *printer, const LpMethod *method,
			LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));

	if (no_streaming(method)) {
		PV("virtual void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"std::function<void(::grpc::Status)>) = 0;\n");
		PV("virtual void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"::grpc::ClientUnaryReactor* reactor) = 0;\n");
	} else if (client_only_streaming(method)) {
		PV("virtual void $Method$(::grpc::ClientContext* context, "
				"$Response$* response, "
				"::grpc::ClientWriteReactor< $Request$>* "
				"reactor) = 0;\n");
	} else if (server_only_streaming(method)) {
		PV("virtual void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, "
				"::grpc::ClientReadReactor< $Response$>* "
				"reactor) = 0;\n");
	} else if (bidi_streaming(method)) {
		PV("virtual void $Method$(::grpc::ClientContext* context, "
				"::grpc::ClientBidiReactor< "
				"$Request$,$Response$>* reactor) = 0;\n");
	}
}

static COLD void PrintHeaderClientMethodCallbackInterfacesEnd(LpPrinter *printer, LpVars *vars)
{
	lp_outdent(printer);
	P("};\n");
	/* TODO: Remove typedef when all uses of experimental_async are migrated off. */
	P("typedef class async_interface experimental_async_interface;\n");

	/* Declare a function to give the async stub contents. It can't be pure */
	/* since this is a new API in StubInterface, but it is meaningless by default */
	/* (since any stub that wants to use it must have its own implementation of */
	/* the callback functions therein), so make the default return value nullptr. */
	/* Intentionally include the word "class" to avoid possible shadowing. */
	/* TODO: Remove experimental_async call when possible, replace with nullptr. */
	P("virtual class async_interface* async() { return nullptr; }\n");

	/* TODO: Remove experimental_async call when possible. */
	P("class async_interface* experimental_async() { return async(); }\n");
}

static COLD void PrintHeaderClientMethodCallbackStart(LpPrinter *printer, LpVars *vars)
{
	/* This declares the stub entry for the callback-based API. */
	P("class async final :\n");
	P("  public StubInterface::async_interface {\n");
	P(" public:\n");
	lp_indent(printer);
}

static COLD void PrintHeaderClientMethodCallback(LpPrinter *printer, const LpMethod *method, LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));

	if (no_streaming(method)) {
		PV("void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"std::function<void(::grpc::Status)>) override;\n");
		PV("void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"::grpc::ClientUnaryReactor* reactor) override;\n");
	} else if (client_only_streaming(method)) {
		PV("void $Method$(::grpc::ClientContext* context, "
				"$Response$* response, "
				"::grpc::ClientWriteReactor< $Request$>* "
				"reactor) override;\n");
	} else if (server_only_streaming(method)) {
		PV("void $Method$(::grpc::ClientContext* context, "
				"const $Request$* request, "
				"::grpc::ClientReadReactor< $Response$>* "
				"reactor) override;\n");

	} else if (bidi_streaming(method)) {
		PV("void $Method$(::grpc::ClientContext* context, "
				"::grpc::ClientBidiReactor< "
				"$Request$,$Response$>* reactor) override;\n");
	}
}

static COLD void PrintHeaderClientMethodCallbackEnd(LpPrinter *printer, LpVars *vars)
{
	lp_outdent(printer);
	P(" private:\n");
	lp_indent(printer);
	P("friend class Stub;\n");
	P("explicit async(Stub* stub): stub_(stub) { }\n");
	/* include a function with a phony use of stub_ to avoid an unused */
	/* private member warning for service with no methods */
	P("Stub* stub() { return stub_; }\n");
	P("Stub* stub_;\n");
	lp_outdent(printer);
	P("};\n");

	P("class async* async() override { "
			"return &async_stub_; }\n");
}

static COLD void PrintHeaderClientMethodData(LpPrinter *printer, const LpMethod *method, LpVars *vars)
{
	SET("Method", method->name);
	PV("const ::grpc::internal::RpcMethod rpcmethod_$Method$_;\n");
}

static COLD void PrintHeaderServerMethodSync(LpPrinter *printer, const LpSourceLocations *locs,
			const LpMethod *method, const Parameters *params, LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	P(cpp_comments(locs, method, true));
	if (params->allow_sync_server_api) {
		P("virtual ");
	}
	if (no_streaming(method)) {
		PV("::grpc::Status $Method$("
				"::grpc::ServerContext* context, const $Request$* request, "
				"$Response$* response);\n");
	} else if (client_only_streaming(method)) {
		PV("::grpc::Status $Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerReader< $Request$>* reader, "
				"$Response$* response);\n");
	} else if (server_only_streaming(method)) {
		PV("::grpc::Status $Method$("
				"::grpc::ServerContext* context, const $Request$* request, "
				"::grpc::ServerWriter< $Response$>* writer);\n");
	} else if (bidi_streaming(method)) {
		PV("::grpc::Status $Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerReaderWriter< $Response$, $Request$>* stream);"
				"\n");
	}
	P(cpp_comments(locs, method, false));
}

/* Helper generator. Disables the sync API for Request and Response, then adds */
/* in an async API for RealRequest and RealResponse types. This is to be used */
/* to generate async and raw async APIs. */
static COLD void PrintHeaderServerAsyncMethodsHelper(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	if (no_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("void Request$Method$("
				"::grpc::ServerContext* context, $RealRequest$* request, "
				"::grpc::ServerAsyncResponseWriter< $RealResponse$>* response, "
				"::grpc::CompletionQueue* new_call_cq, "
				"::grpc::ServerCompletionQueue* notification_cq, void *tag) {\n");
		PV("  ::grpc::Service::RequestAsyncUnary($Idx$, context, "
				"request, response, new_call_cq, notification_cq, tag);\n");
		P("}\n");
	} else if (client_only_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReader< $Request$>* /*reader*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("void Request$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerAsyncReader< $RealResponse$, $RealRequest$>* reader, "
				"::grpc::CompletionQueue* new_call_cq, "
				"::grpc::ServerCompletionQueue* notification_cq, void *tag) {\n");
		PV("  ::grpc::Service::RequestAsyncClientStreaming($Idx$, "
				"context, reader, new_call_cq, notification_cq, tag);\n");
		P("}\n");
	} else if (server_only_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"::grpc::ServerWriter< $Response$>* /*writer*/) override "
					"{\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("void Request$Method$("
				"::grpc::ServerContext* context, $RealRequest$* request, "
				"::grpc::ServerAsyncWriter< $RealResponse$>* writer, "
				"::grpc::CompletionQueue* new_call_cq, "
				"::grpc::ServerCompletionQueue* notification_cq, void *tag) {\n");
		PV("  ::grpc::Service::RequestAsyncServerStreaming($Idx$, "
				"context, request, writer, new_call_cq, notification_cq, tag);\n");
		P("}\n");
	} else if (bidi_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReaderWriter< $Response$, $Request$>* /*stream*/) "
					" override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("void Request$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerAsyncReaderWriter< $RealResponse$, $RealRequest$>* "
				"stream, "
				"::grpc::CompletionQueue* new_call_cq, "
				"::grpc::ServerCompletionQueue* notification_cq, void *tag) {\n");
		PV("  ::grpc::Service::RequestAsyncBidiStreaming($Idx$, "
				"context, stream, new_call_cq, notification_cq, tag);\n");
		P("}\n");
	}
}

static COLD void PrintHeaderServerMethodAsync(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	SET("Method", method->name);
	/* These will be disabled */
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	/* These will be used for the async API */
	SET("RealRequest", type_name(method->input_type));
	SET("RealResponse", type_name(method->output_type));
	PV("template <class BaseClass>\n");
	PV("class WithAsyncMethod_$Method$ : public BaseClass {\n");
	P(" private:\n"
			"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
			"{}\n");
	P(" public:\n");
	lp_indent(printer);
	PV("WithAsyncMethod_$Method$() {\n"
			"  ::grpc::Service::MarkMethodAsync($Idx$);\n"
			"}\n");
	PV("~WithAsyncMethod_$Method$() override {\n"
			"  BaseClassMustBeDerivedFromService(this);\n"
			"}\n");
	PrintHeaderServerAsyncMethodsHelper(printer, method, params, vars);
	lp_outdent(printer);
	PV("};\n");
}

/* Helper generator. Disables the sync API for Request and Response, then adds */
/* in a callback API for RealRequest and RealResponse types. This is to be used */
/* to generate callback and raw callback APIs. */
static COLD void PrintHeaderServerCallbackMethodsHelper(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	if (no_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("virtual ::grpc::ServerUnaryReactor* $Method$(\n"
				"  ::grpc::CallbackServerContext* /*context*/, "
				"const $RealRequest$* /*request*/, "
				"$RealResponse$* /*response*/)"
				"  { return nullptr; }\n");
	} else if (client_only_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReader< $Request$>* /*reader*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("virtual ::grpc::ServerReadReactor< "
				"$RealRequest$>* $Method$(\n"
				"  ::grpc::CallbackServerContext* "
				"/*context*/, $RealResponse$* /*response*/)"
				"  { return nullptr; }\n");
	} else if (server_only_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"::grpc::ServerWriter< $Response$>* /*writer*/) override "
					"{\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("virtual ::grpc::ServerWriteReactor< $RealResponse$>* $Method$(\n"
				"  ::grpc::CallbackServerContext* "
				"/*context*/, const $RealRequest$* /*request*/)"
				"  { return nullptr; }\n");
	} else if (bidi_streaming(method)) {
		if (params->allow_sync_server_api) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReaderWriter< $Response$, $Request$>* /*stream*/) "
					" override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
		PV("virtual ::grpc::ServerBidiReactor< $RealRequest$, $RealResponse$>* "
				"$Method$(\n"
				"  ::grpc::CallbackServerContext* /*context*/)\n"
				"  { return nullptr; }\n");
	}
}

static COLD void PrintHeaderServerMethodCallback(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	SET("Method", method->name);
	/* These will be disabled */
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	/* These will be used for the callback API */
	SET("RealRequest", type_name(method->input_type));
	SET("RealResponse", type_name(method->output_type));
	PV("template <class BaseClass>\n");
	PV("class WithCallbackMethod_$Method$ : public BaseClass {\n");
	P(" private:\n"
			"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
			"{}\n");
	P(" public:\n");
	lp_indent(printer);
	PV("WithCallbackMethod_$Method$() {\n");
	if (no_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackUnaryHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"const $RealRequest$* "
				"request, "
				"$RealResponse$* response) { "
				"return this->$Method$(context, request, response); }));}\n");
		PV("void SetMessageAllocatorFor_$Method$(\n"
				"    ::grpc::MessageAllocator< "
				"$RealRequest$, $RealResponse$>* allocator) {\n"
				"  ::grpc::internal::MethodHandler* const handler = "
				"::grpc::Service::GetHandler($Idx$);\n"
				"  static_cast<::grpc::internal::CallbackUnaryHandler< "
				"$RealRequest$, $RealResponse$>*>(handler)\n"
				"          ->SetMessageAllocator(allocator);\n");
	} else if (client_only_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackClientStreamingHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"$RealResponse$* "
				"response) { "
				"return this->$Method$(context, response); }));\n");
	} else if (server_only_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackServerStreamingHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"const $RealRequest$* "
				"request) { "
				"return this->$Method$(context, request); }));\n");
	} else if (bidi_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackBidiHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context) "
				"{ return this->$Method$(context); }));\n");
	}
	PV("}\n");
	PV("~WithCallbackMethod_$Method$() override {\n"
			"  BaseClassMustBeDerivedFromService(this);\n"
			"}\n");
	PrintHeaderServerCallbackMethodsHelper(printer, method, params, vars);
	lp_outdent(printer);
	PV("};\n");
}

static COLD void PrintHeaderServerMethodRawCallback(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	SET("Method", method->name);
	/* These will be disabled */
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	/* These will be used for raw API */
	SET("RealRequest", "::grpc::ByteBuffer");
	SET("RealResponse", "::grpc::ByteBuffer");
	PV("template <class BaseClass>\n");
	PV("class WithRawCallbackMethod_$Method$ : public "
			"BaseClass {\n");
	P(" private:\n"
			"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
			"{}\n");
	P(" public:\n");
	lp_indent(printer);
	PV("WithRawCallbackMethod_$Method$() {\n");
	if (no_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodRawCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackUnaryHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"const $RealRequest$* "
				"request, "
				"$RealResponse$* response) { return "
				"this->$Method$(context, request, response); }));\n");
	} else if (client_only_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodRawCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackClientStreamingHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"$RealResponse$* response) "
				"{ return this->$Method$(context, response); }));\n");
	} else if (server_only_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodRawCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackServerStreamingHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context, "
				"const"
				"$RealRequest$* request) { return "
				"this->$Method$(context, request); }));\n");
	} else if (bidi_streaming(method)) {
		PV("  ::grpc::Service::MarkMethodRawCallback($Idx$,\n"
				"      new ::grpc::internal::CallbackBidiHandler< "
				"$RealRequest$, $RealResponse$>(\n"
				"        [this](\n"
				"               ::grpc::CallbackServerContext* context) "
				"{ return this->$Method$(context); }));\n");
	}
	PV("}\n");
	PV("~WithRawCallbackMethod_$Method$() override {\n"
			"  BaseClassMustBeDerivedFromService(this);\n"
			"}\n");
	PrintHeaderServerCallbackMethodsHelper(printer, method, params, vars);
	lp_outdent(printer);
	PV("};\n");
}

static COLD void PrintHeaderServerMethodStreamedUnary(LpPrinter *printer, const LpMethod *method, LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	if (no_streaming(method)) {
		PV("template <class BaseClass>\n");
		PV("class WithStreamedUnaryMethod_$Method$ : "
				"public BaseClass {\n");
		P(" private:\n"
				"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
				"{}\n");
		P(" public:\n");
		lp_indent(printer);
		PV("WithStreamedUnaryMethod_$Method$() {\n"
				"  ::grpc::Service::MarkMethodStreamed($Idx$,\n"
				"    new ::grpc::internal::StreamedUnaryHandler<\n"
				"      $Request$, $Response$>(\n"
				"        [this](::grpc::ServerContext* context,\n"
				"               ::grpc::ServerUnaryStreamer<\n"
				"                 $Request$, $Response$>* streamer) {\n"
				"                   return this->Streamed$Method$(context,\n"
				"                     streamer);\n"
				"              }));\n"
				"}\n");
		PV("~WithStreamedUnaryMethod_$Method$() override {\n"
				"  BaseClassMustBeDerivedFromService(this);\n"
				"}\n");
		PV("// disable regular version of this method\n"
				"::grpc::Status $Method$("
				"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
				"$Response$* /*response*/) override {\n"
				"  abort();\n"
				"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
				"}\n");
		PV("// replace default version of method with streamed unary\n"
				"virtual ::grpc::Status Streamed$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerUnaryStreamer< "
				"$Request$,$Response$>* server_unary_streamer)"
				" = 0;\n");
		lp_outdent(printer);
		PV("};\n");
	}
}

static COLD void PrintHeaderServerMethodSplitStreaming(LpPrinter *printer, const LpMethod *method, LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	if (server_only_streaming(method)) {
		PV("template <class BaseClass>\n");
		PV("class WithSplitStreamingMethod_$Method$ : "
				"public BaseClass {\n");
		P(" private:\n"
				"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
				"{}\n");
		P(" public:\n");
		lp_indent(printer);
		PV("WithSplitStreamingMethod_$Method$() {\n"
				"  ::grpc::Service::MarkMethodStreamed($Idx$,\n"
				"    new ::grpc::internal::SplitServerStreamingHandler<\n"
				"      $Request$, $Response$>(\n"
				"        [this](::grpc::ServerContext* context,\n"
				"               ::grpc::ServerSplitStreamer<\n"
				"                 $Request$, $Response$>* streamer) {\n"
				"                   return this->Streamed$Method$(context,\n"
				"                     streamer);\n"
				"              }));\n"
				"}\n");
		PV("~WithSplitStreamingMethod_$Method$() override {\n"
				"  BaseClassMustBeDerivedFromService(this);\n"
				"}\n");
		PV("// disable regular version of this method\n"
				"::grpc::Status $Method$("
				"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
				"::grpc::ServerWriter< $Response$>* /*writer*/) override "
				"{\n"
				"  abort();\n"
				"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
				"}\n");
		PV("// replace default version of method with split streamed\n"
				"virtual ::grpc::Status Streamed$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerSplitStreamer< "
				"$Request$,$Response$>* server_split_streamer)"
				" = 0;\n");
		lp_outdent(printer);
		PV("};\n");
	}
}

static COLD void PrintHeaderServerMethodGeneric(LpPrinter *printer, const LpMethod *method,
			const Parameters *params, LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	PV("template <class BaseClass>\n");
	PV("class WithGenericMethod_$Method$ : public BaseClass {\n");
	P(" private:\n"
			"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
			"{}\n");
	P(" public:\n");
	lp_indent(printer);
	PV("WithGenericMethod_$Method$() {\n"
			"  ::grpc::Service::MarkMethodGeneric($Idx$);\n"
			"}\n");
	PV("~WithGenericMethod_$Method$() override {\n"
			"  BaseClassMustBeDerivedFromService(this);\n"
			"}\n");
	if (params->allow_sync_server_api) {
		if (no_streaming(method)) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		} else if (client_only_streaming(method)) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReader< $Request$>* /*reader*/, "
					"$Response$* /*response*/) override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		} else if (server_only_streaming(method)) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, const $Request$* /*request*/, "
					"::grpc::ServerWriter< $Response$>* /*writer*/) override "
					"{\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		} else if (bidi_streaming(method)) {
			PV("// disable synchronous version of this method\n"
					"::grpc::Status $Method$("
					"::grpc::ServerContext* /*context*/, "
					"::grpc::ServerReaderWriter< $Response$, $Request$>* /*stream*/) "
					" override {\n"
					"  abort();\n"
					"  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, \"\");\n"
					"}\n");
		}
	}
	lp_outdent(printer);
	PV("};\n");
}

static COLD void PrintHeaderServerMethodRaw(LpPrinter *printer, const LpMethod *method, const Parameters *params,
			LpVars *vars)
{
	SET("Method", method->name);
	/* These will be disabled */
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	/* These will be used for raw API */
	SET("RealRequest", "::grpc::ByteBuffer");
	SET("RealResponse", "::grpc::ByteBuffer");
	PV("template <class BaseClass>\n");
	PV("class WithRawMethod_$Method$ : public BaseClass {\n");
	P(" private:\n"
			"  void BaseClassMustBeDerivedFromService(const Service* /*service*/) "
			"{}\n");
	P(" public:\n");
	lp_indent(printer);
	PV("WithRawMethod_$Method$() {\n"
			"  ::grpc::Service::MarkMethodRaw($Idx$);\n"
			"}\n");
	PV("~WithRawMethod_$Method$() override {\n"
			"  BaseClassMustBeDerivedFromService(this);\n"
			"}\n");
	PrintHeaderServerAsyncMethodsHelper(printer, method, params, vars);
	lp_outdent(printer);
	PV("};\n");
}

static COLD void PrintHeaderService(LpPrinter *printer, const LpSourceLocations *locs, const LpService *service,
			const Parameters *params, LpVars *vars)
{
	SET("Service", service->name);

	P(cpp_comments(locs, service, true));
	if (params->allow_deprecated && pb_int(service->options, SERVICEOPT_DEPRECATED, 0)) {
		PV("class [[deprecated]] $Service$ final {\n"
				" public:\n");
	} else {
		PV("class $Service$ final {\n"
				" public:\n");
	}
	lp_indent(printer);

	/* Service metadata */
	PV("static constexpr char const* service_full_name() {\n"
			"  return \"$Package$$Service$\";\n"
			"}\n");

	/* Client side */
	P("class StubInterface {\n"
			" public:\n");
	lp_indent(printer);
	P("virtual ~StubInterface() {}\n");
	for (int i = 0; i < service->n_methods; ++i) {
		P(cpp_comments(locs, &service->methods[i], true));
		PrintHeaderClientMethodInterfaces(printer, &service->methods[i], params,
				vars, true);
		P(cpp_comments(locs, &service->methods[i], false));
	}
	PrintHeaderClientMethodCallbackInterfacesStart(printer, vars);
	for (int i = 0; i < service->n_methods; ++i) {
		P(cpp_comments(locs, &service->methods[i], true));
		PrintHeaderClientMethodCallbackInterfaces(printer, &service->methods[i],
				vars);
		P(cpp_comments(locs, &service->methods[i], false));
	}
	PrintHeaderClientMethodCallbackInterfacesEnd(printer, vars);
	lp_outdent(printer);
	P(" private:\n");
	lp_indent(printer);
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderClientMethodInterfaces(printer, &service->methods[i], params,
				vars, false);
	}
	lp_outdent(printer);
	P("};\n");
	P("class Stub final : public StubInterface"
			" {\n public:\n");
	lp_indent(printer);
	P("Stub(const std::shared_ptr< ::grpc::ChannelInterface>& "
			"channel, const ::grpc::StubOptions& options = "
			"::grpc::StubOptions());\n");
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderClientMethod(printer, &service->methods[i], params, vars,
				true);
	}
	PrintHeaderClientMethodCallbackStart(printer, vars);
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderClientMethodCallback(printer, &service->methods[i], vars);
	}
	PrintHeaderClientMethodCallbackEnd(printer, vars);
	lp_outdent(printer);
	P("\n private:\n");
	lp_indent(printer);
	P("std::shared_ptr< ::grpc::ChannelInterface> channel_;\n");
	P("class async async_stub_{this};\n");
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderClientMethod(printer, &service->methods[i], params, vars,
				false);
	}
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderClientMethodData(printer, &service->methods[i], vars);
	}
	lp_outdent(printer);
	P("};\n");
	P("static std::unique_ptr<Stub> NewStub(const std::shared_ptr< "
			"::grpc::ChannelInterface>& channel, "
			"const ::grpc::StubOptions& options = ::grpc::StubOptions());\n");

	P("\n");

	/* Server side - base */
	P("class Service : public ::grpc::Service {\n"
			" public:\n");
	lp_indent(printer);
	P("Service();\n");
	P("virtual ~Service();\n");
	for (int i = 0; i < service->n_methods; ++i) {
		PrintHeaderServerMethodSync(printer, locs, &service->methods[i], params,
				vars);
	}
	lp_outdent(printer);
	P("};\n");

	if (params->allow_cq_api) {
		/* Server side - Asynchronous */
		for (int i = 0; i < service->n_methods; ++i) {
			SET("Idx", xfmt("%d", i));
			PrintHeaderServerMethodAsync(printer, &service->methods[i], params,
					vars);
		}

		P("typedef ");

		for (int i = 0; i < service->n_methods; ++i) {
			SET("method_name", service->methods[i].name);
			PV("WithAsyncMethod_$method_name$<");
		}
		P("Service");
		for (int i = 0; i < service->n_methods; ++i) {
			P(" >");
		}
		P(" AsyncService;\n");
	}

	/* Server side - Callback */
	for (int i = 0; i < service->n_methods; ++i) {
		SET("Idx", xfmt("%d", i));
		PrintHeaderServerMethodCallback(printer, &service->methods[i], params,
				vars);
	}

	P("typedef ");

	for (int i = 0; i < service->n_methods; ++i) {
		SET("method_name", service->methods[i].name);
		PV("WithCallbackMethod_$method_name$<");
	}
	P("Service");
	for (int i = 0; i < service->n_methods; ++i) {
		P(" >");
	}
	P(" CallbackService;\n");

	/* TODO: Remove following typedef once all uses of ExperimentalCallbackService */
	/*       are migrated to CallbackService */
	P("typedef CallbackService ExperimentalCallbackService;\n");

	/* Server side - Generic */
	for (int i = 0; i < service->n_methods; ++i) {
		SET("Idx", xfmt("%d", i));
		PrintHeaderServerMethodGeneric(printer, &service->methods[i], params,
				vars);
	}

	/* Server side - Raw Async */
	if (params->allow_cq_api) {
		for (int i = 0; i < service->n_methods; ++i) {
			SET("Idx", xfmt("%d", i));
			PrintHeaderServerMethodRaw(printer, &service->methods[i], params,
					vars);
		}
	}

	/* Server side - Raw Callback */
	for (int i = 0; i < service->n_methods; ++i) {
		SET("Idx", xfmt("%d", i));
		PrintHeaderServerMethodRawCallback(printer, &service->methods[i],
				params, vars);
	}

	if (params->allow_sync_server_api) {
		/* Server side - Streamed Unary */
		for (int i = 0; i < service->n_methods; ++i) {
			SET("Idx", xfmt("%d", i));
			PrintHeaderServerMethodStreamedUnary(printer, &service->methods[i],
					vars);
		}

		P("typedef ");
		for (int i = 0; i < service->n_methods; ++i) {
			SET("method_name", service->methods[i].name);
			if (no_streaming(&service->methods[i])) {
				PV("WithStreamedUnaryMethod_$method_name$<");
			}
		}
		P("Service");
		for (int i = 0; i < service->n_methods; ++i) {
			if (no_streaming(&service->methods[i])) {
				P(" >");
			}
		}
		P(" StreamedUnaryService;\n");

		/* Server side - controlled server-side streaming */
		for (int i = 0; i < service->n_methods; ++i) {
			SET("Idx", xfmt("%d", i));
			PrintHeaderServerMethodSplitStreaming(printer, &service->methods[i],
					vars);
		}

		P("typedef ");
		for (int i = 0; i < service->n_methods; ++i) {
			SET("method_name", service->methods[i].name);
			const LpMethod *method = &service->methods[i];
			if (server_only_streaming(method)) {
				PV("WithSplitStreamingMethod_$method_name$<");
			}
		}
		P("Service");
		for (int i = 0; i < service->n_methods; ++i) {
			const LpMethod *method = &service->methods[i];
			if (server_only_streaming(method)) {
				P(" >");
			}
		}
		P(" SplitStreamedService;\n");

		/* Server side - typedef for controlled both unary and server-side streaming */
		P("typedef ");
		for (int i = 0; i < service->n_methods; ++i) {
			SET("method_name", service->methods[i].name);
			const LpMethod *method = &service->methods[i];
			if (server_only_streaming(method)) {
				PV("WithSplitStreamingMethod_$method_name$<");
			}
			if (no_streaming(&service->methods[i])) {
				PV("WithStreamedUnaryMethod_$method_name$<");
			}
		}
		P("Service");
		for (int i = 0; i < service->n_methods; ++i) {
			const LpMethod *method = &service->methods[i];
			if (no_streaming(&service->methods[i]) ||
					server_only_streaming(method)) {
				P(" >");
			}
		}
		P(" StreamedService;\n");
	}

	lp_outdent(printer);
	P("};\n");
	P(cpp_comments(locs, service, false));
}

static COLD void GetHeaderServices(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;
	/* Package string is empty or ends with a dot. It is used to fully qualify method names. */
	SET("Package", *file->file->package ? xcat(file->file->package, ".") : "");

	if (*params->services_namespace) {
		SET("services_namespace", params->services_namespace);
		PV("\nnamespace $services_namespace$ {\n\n");
	}

	for (int i = 0; i < file->file->n_services; ++i) {
		PrintHeaderService(printer, file->locs, &file->file->services[i], params, vars);
		P("\n");
	}

	if (*params->services_namespace)
		PV("}  // namespace $services_namespace$\n\n");
}

/* The closing namespaces, innermost first, and a blank line (when there is a package). */
static COLD void print_namespaces_close(LpPrinter *printer, LpVars *vars, const LpFile *file)
{
	if (!*file->package)
		return;
	Vec parts = grpc_tokenize(file->package, ".");
	for (int i = parts.n - 1; i >= 0; i--) {
		SET("part", parts.v[i]);
		PV("}  // namespace $part$\n");
	}
	PV("\n");
}

static COLD void GetHeaderEpilogue(Buf *output, const ProtoBufFile *file)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	SET("filename", file->file->name);
	SET("filename_identifier", FilenameIdentifier(file->file->name));

	print_namespaces_close(printer, vars, file->file);

	PV("\n");

	/* Must be included at end of file */
	P("#include <grpcpp/ports_undef.inc>\n");
	PV("#endif  // GRPC_$filename_identifier$__INCLUDED\n");

	P(cpp_comments(file->locs, file->file, false));
}

static COLD void GetSourcePrologue(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	SET("filename", file->file->name);
	SET("filename_base", grpc_strip_proto(file->file->name));
	SET("message_header_ext", message_header_ext(params));
	SET("service_header_ext", kCppGeneratorServiceHeaderExt);

	PV("// Generated by the gRPC C++ plugin.\n");
	PV("// If you make any local change, they will be lost.\n");
	PV("// source: $filename$\n\n");

	PV("#include \"$filename_base$$message_header_ext$\"\n");
	PV("#include \"$filename_base$$service_header_ext$\"\n");
	PV("\n");
}

static COLD void GetSourceIncludes(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;
	static const char *const headers_strs[] = {
		"functional", "grpcpp/support/async_stream.h",
		"grpcpp/support/async_unary_call.h", "grpcpp/impl/channel_interface.h",
		"grpcpp/impl/client_unary_call.h", "grpcpp/support/client_callback.h",
		"grpcpp/support/message_allocator.h", "grpcpp/support/method_handler.h",
		"grpcpp/impl/rpc_service_method.h", "grpcpp/support/server_callback.h",
		"grpcpp/impl/server_callback_handlers.h", "grpcpp/server_context.h",
		"grpcpp/impl/service_type.h", "grpcpp/support/sync_stream.h",
		/* ports_def.inc Must be included as last */
		"grpcpp/ports_def.inc"};
	PrintIncludes(printer, headers_strs, (int)(sizeof headers_strs / sizeof *headers_strs),
		      params->use_system_headers, params->grpc_search_path);

	if (*file->file->package)
		print_namespaces_open(printer, vars, file->file);

	PV("\n");
}

static COLD void PrintSourceClientMethod(LpPrinter *printer, const LpMethod *method, const Parameters *params,
			LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	typedef struct {
		const char *prefix;
		const char *start;          /* bool literal expressed as string */
		const char *method_params;  /* extra arguments to method */
		const char *create_args;    /* extra arguments to creator */
	} AsyncPrefix;
	static const AsyncPrefix async_prefixes[] = {{"Async", "true", ", void* tag", ", tag"},
			{"PrepareAsync", "false", "", ", nullptr"}};
	if (no_streaming(method)) {
		PV("::grpc::Status $ns$$Service$::Stub::$Method$("
				"::grpc::ClientContext* context, "
				"const $Request$& request, $Response$* response) {\n");
		PV("  return ::grpc::internal::BlockingUnaryCall"
				"< $Request$, $Response$, ::grpc::protobuf::MessageLite, "
				"::grpc::protobuf::MessageLite>"
				"(channel_.get(), rpcmethod_$Method$_, "
				"context, request, response);\n}\n\n");

		PV("void $ns$$Service$::Stub::async::$Method$("
				"::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"std::function<void(::grpc::Status)> f) {\n");
		PV("  ::grpc::internal::CallbackUnaryCall"
				"< $Request$, $Response$, ::grpc::protobuf::MessageLite, "
				"::grpc::protobuf::MessageLite>"
				"(stub_->channel_.get(), stub_->rpcmethod_$Method$_, "
				"context, request, response, std::move(f));\n}\n\n");

		PV("void $ns$$Service$::Stub::async::$Method$("
				"::grpc::ClientContext* context, "
				"const $Request$* request, $Response$* response, "
				"::grpc::ClientUnaryReactor* reactor) {\n");
		PV("  ::grpc::internal::ClientCallbackUnaryFactory::Create"
				"< ::grpc::protobuf::MessageLite, "
				"::grpc::protobuf::MessageLite>"
				"(stub_->channel_.get(), stub_->rpcmethod_$Method$_, "
				"context, request, response, reactor);\n}\n\n");

		if (params->allow_cq_api) {
			PV("::grpc::ClientAsyncResponseReader< $Response$>* "
					"$ns$$Service$::Stub::PrepareAsync$Method$Raw(::grpc::"
					"ClientContext* context, "
					"const $Request$& request, "
					"::grpc::CompletionQueue* cq) {\n");
			PV("  return "
					"::grpc::internal::ClientAsyncResponseReaderHelper::Create"
					"< $Response$, $Request$, ::grpc::protobuf::MessageLite, "
					"::grpc::protobuf::MessageLite>"
					"(channel_.get(), cq, rpcmethod_$Method$_, "
					"context, request);\n"
					"}\n\n");
			PV("::grpc::ClientAsyncResponseReader< $Response$>* "
					"$ns$$Service$::Stub::Async$Method$Raw(::grpc::"
					"ClientContext* context, "
					"const $Request$& request, "
					"::grpc::CompletionQueue* cq) {\n");
			PV("  auto* result =\n"
					"    this->PrepareAsync$Method$Raw(context, request, cq);\n"
					"  result->StartCall();\n"
					"  return result;\n"
					"}\n\n");
		}
	} else if (client_only_streaming(method)) {
		PV("::grpc::ClientWriter< $Request$>* "
				"$ns$$Service$::Stub::$Method$Raw("
				"::grpc::ClientContext* context, $Response$* response) {\n");
		PV("  return ::grpc::internal::ClientWriterFactory< "
				"$Request$>::Create("
				"channel_.get(), "
				"rpcmethod_$Method$_, "
				"context, response);\n"
				"}\n\n");

		PV("void $ns$$Service$::"
				"Stub::async::$Method$(::grpc::ClientContext* context, "
				"$Response$* response, "
				"::grpc::ClientWriteReactor< $Request$>* reactor) {\n");
		PV("  ::grpc::internal::ClientCallbackWriterFactory< "
				"$Request$>::Create("
				"stub_->channel_.get(), "
				"stub_->rpcmethod_$Method$_, "
				"context, response, reactor);\n"
				"}\n\n");

		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncStart", async_prefix->start);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("AsyncCreateArgs", async_prefix->create_args);
				PV("::grpc::ClientAsyncWriter< $Request$>* "
						"$ns$$Service$::Stub::$AsyncPrefix$$Method$Raw("
						"::grpc::ClientContext* context, $Response$* response, "
						"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
				PV("  return ::grpc::internal::ClientAsyncWriterFactory< $Request$>"
						"::Create(channel_.get(), cq, "
						"rpcmethod_$Method$_, "
						"context, response, $AsyncStart$$AsyncCreateArgs$);\n"
						"}\n\n");
			}
		}
	} else if (server_only_streaming(method)) {
		PV("::grpc::ClientReader< $Response$>* "
				"$ns$$Service$::Stub::$Method$Raw("
				"::grpc::ClientContext* context, const $Request$& request) {\n");
		PV("  return ::grpc::internal::ClientReaderFactory< "
				"$Response$>::Create("
				"channel_.get(), "
				"rpcmethod_$Method$_, "
				"context, request);\n"
				"}\n\n");

		PV("void $ns$$Service$::Stub::async::$Method$(::grpc::"
				"ClientContext* context, "
				"const $Request$* request, "
				"::grpc::ClientReadReactor< $Response$>* reactor) {\n");
		PV("  ::grpc::internal::ClientCallbackReaderFactory< "
				"$Response$>::Create("
				"stub_->channel_.get(), "
				"stub_->rpcmethod_$Method$_, "
				"context, request, reactor);\n"
				"}\n\n");

		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncStart", async_prefix->start);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("AsyncCreateArgs", async_prefix->create_args);
				PV("::grpc::ClientAsyncReader< $Response$>* "
						"$ns$$Service$::Stub::$AsyncPrefix$$Method$Raw("
						"::grpc::ClientContext* context, const $Request$& request, "
						"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
				PV("  return ::grpc::internal::ClientAsyncReaderFactory< "
						"$Response$>"
						"::Create(channel_.get(), cq, "
						"rpcmethod_$Method$_, "
						"context, request, $AsyncStart$$AsyncCreateArgs$);\n"
						"}\n\n");
			}
		}
	} else if (bidi_streaming(method)) {
		PV("::grpc::ClientReaderWriter< $Request$, $Response$>* "
				"$ns$$Service$::Stub::$Method$Raw(::grpc::ClientContext* context) {\n");
		PV("  return ::grpc::internal::ClientReaderWriterFactory< "
				"$Request$, $Response$>::Create("
				"channel_.get(), "
				"rpcmethod_$Method$_, "
				"context);\n"
				"}\n\n");

		PV("void $ns$$Service$::Stub::async::$Method$(::grpc::"
				"ClientContext* context, "
				"::grpc::ClientBidiReactor< $Request$,$Response$>* "
				"reactor) {\n");
		PV("  ::grpc::internal::ClientCallbackReaderWriterFactory< "
				"$Request$,$Response$>::Create("
				"stub_->channel_.get(), "
				"stub_->rpcmethod_$Method$_, "
				"context, reactor);\n"
				"}\n\n");

		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncStart", async_prefix->start);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("AsyncCreateArgs", async_prefix->create_args);
				PV("::grpc::ClientAsyncReaderWriter< $Request$, $Response$>* "
						"$ns$$Service$::Stub::$AsyncPrefix$$Method$Raw(::grpc::"
						"ClientContext* context, "
						"::grpc::CompletionQueue* cq$AsyncMethodParams$) {\n");
				PV("  return "
						"::grpc::internal::ClientAsyncReaderWriterFactory< "
						"$Request$, $Response$>::Create("
						"channel_.get(), cq, "
						"rpcmethod_$Method$_, "
						"context, $AsyncStart$$AsyncCreateArgs$);\n"
						"}\n\n");
			}
		}
	}
}

static COLD void PrintSourceServerMethod(LpPrinter *printer, const LpMethod *method, const Parameters *params,
			LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));
	if (no_streaming(method)) {
		PV("::grpc::Status $ns$$Service$::Service::$Method$("
				"::grpc::ServerContext* context, "
				"const $Request$* request, $Response$* response) {\n");
		if (!params->allow_sync_server_api) {
			P("  abort();\n");
		}
		P("  (void) context;\n");
		P("  (void) request;\n");
		P("  (void) response;\n");
		P("  return ::grpc::Status("
				"::grpc::StatusCode::UNIMPLEMENTED, \"\");\n");
		P("}\n\n");
	} else if (client_only_streaming(method)) {
		PV("::grpc::Status $ns$$Service$::Service::$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerReader< $Request$>* reader, "
				"$Response$* response) {\n");
		if (!params->allow_sync_server_api) {
			P("  abort();\n");
		}
		P("  (void) context;\n");
		P("  (void) reader;\n");
		P("  (void) response;\n");
		P("  return ::grpc::Status("
				"::grpc::StatusCode::UNIMPLEMENTED, \"\");\n");
		P("}\n\n");
	} else if (server_only_streaming(method)) {
		PV("::grpc::Status $ns$$Service$::Service::$Method$("
				"::grpc::ServerContext* context, "
				"const $Request$* request, "
				"::grpc::ServerWriter< $Response$>* writer) {\n");
		if (!params->allow_sync_server_api) {
			P("  abort();\n");
		}
		P("  (void) context;\n");
		P("  (void) request;\n");
		P("  (void) writer;\n");
		P("  return ::grpc::Status("
				"::grpc::StatusCode::UNIMPLEMENTED, \"\");\n");
		P("}\n\n");
	} else if (bidi_streaming(method)) {
		PV("::grpc::Status $ns$$Service$::Service::$Method$("
				"::grpc::ServerContext* context, "
				"::grpc::ServerReaderWriter< $Response$, $Request$>* "
				"stream) {\n");
		if (!params->allow_sync_server_api) {
			P("  abort();\n");
		}
		P("  (void) context;\n");
		P("  (void) stream;\n");
		P("  return ::grpc::Status("
				"::grpc::StatusCode::UNIMPLEMENTED, \"\");\n");
		P("}\n\n");
	}
}

static COLD void PrintSourceService(LpPrinter *printer, const LpService *service, const Parameters *params,
			LpVars *vars)
{
	SET("Service", service->name);

	if (service->n_methods > 0) {
		PV("static const char* $prefix$$Service$_method_names[] = {\n");
		for (int i = 0; i < service->n_methods; ++i) {
			SET("Method", service->methods[i].name);
			PV("  \"/$Package$$Service$/$Method$\",\n");
		}
		PV("};\n\n");
	}

	PV("std::unique_ptr< $ns$$Service$::Stub> $ns$$Service$::NewStub("
			"const std::shared_ptr< ::grpc::ChannelInterface>& channel, "
			"const ::grpc::StubOptions& options) {\n"
			"  (void)options;\n"
			"  std::unique_ptr< $ns$$Service$::Stub> stub(new "
			"$ns$$Service$::Stub(channel, options));\n"
			"  return stub;\n"
			"}\n\n");
	PV("$ns$$Service$::Stub::Stub(const std::shared_ptr< "
			"::grpc::ChannelInterface>& channel, const "
			"::grpc::StubOptions& options)\n");
	lp_indent(printer);
	P(": channel_(channel)");
	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		SET("Method", method->name);
		SET("Idx", xfmt("%d", i));
		if (no_streaming(method)) {
			SET("StreamingType", "NORMAL_RPC");
			/* NOTE: There is no reason to consider streamed-unary as a separate */
			/* category here since this part is setting up the client-side stub */
			/* and this appears as a NORMAL_RPC from the client-side. */
		} else if (client_only_streaming(method)) {
			SET("StreamingType", "CLIENT_STREAMING");
		} else if (server_only_streaming(method)) {
			SET("StreamingType", "SERVER_STREAMING");
		} else {
			SET("StreamingType", "BIDI_STREAMING");
		}
		PV(", rpcmethod_$Method$_("
				"$prefix$$Service$_method_names[$Idx$], options.suffix_for_stats(),"
				"::grpc::internal::RpcMethod::$StreamingType$, "
				"channel"
				")\n");
	}
	P("{}\n\n");
	lp_outdent(printer);

	for (int i = 0; i < service->n_methods; ++i) {
		SET("Idx", xfmt("%d", i));
		PrintSourceClientMethod(printer, &service->methods[i], params, vars);
	}

	PV("$ns$$Service$::Service::Service() {\n");
	lp_indent(printer);
	for (int i = 0; i < service->n_methods; ++i) {
		const LpMethod *method = &service->methods[i];
		SET("Idx", xfmt("%d", i));
		SET("Method", method->name);
		SET("Request", type_name(method->input_type));
		SET("Response", type_name(method->output_type));
		if (no_streaming(method)) {
			PV("AddMethod(new ::grpc::internal::RpcServiceMethod(\n"
					"    $prefix$$Service$_method_names[$Idx$],\n"
					"    ::grpc::internal::RpcMethod::NORMAL_RPC,\n"
					"    new ::grpc::internal::RpcMethodHandler< $ns$$Service$::Service, "
					"$Request$, $Response$, ::grpc::protobuf::MessageLite, "
					"::grpc::protobuf::MessageLite>(\n"
					"        []($ns$$Service$::Service* service,\n"
					"           ::grpc::ServerContext* ctx,\n"
					"           const $Request$* req,\n"
					"           $Response$* resp) {\n"
					"             return service->$Method$(ctx, req, resp);\n"
					"           }, this)));\n");
		} else if (client_only_streaming(method)) {
			PV("AddMethod(new ::grpc::internal::RpcServiceMethod(\n"
					"    $prefix$$Service$_method_names[$Idx$],\n"
					"    ::grpc::internal::RpcMethod::CLIENT_STREAMING,\n"
					"    new ::grpc::internal::ClientStreamingHandler< "
					"$ns$$Service$::Service, $Request$, $Response$>(\n"
					"        []($ns$$Service$::Service* service,\n"
					"           ::grpc::ServerContext* ctx,\n"
					"           ::grpc::ServerReader<$Request$>* reader,\n"
					"           $Response$* resp) {\n"
					"             return service->$Method$(ctx, reader, resp);\n"
					"           }, this)));\n");
		} else if (server_only_streaming(method)) {
			PV("AddMethod(new ::grpc::internal::RpcServiceMethod(\n"
					"    $prefix$$Service$_method_names[$Idx$],\n"
					"    ::grpc::internal::RpcMethod::SERVER_STREAMING,\n"
					"    new ::grpc::internal::ServerStreamingHandler< "
					"$ns$$Service$::Service, $Request$, $Response$>(\n"
					"        []($ns$$Service$::Service* service,\n"
					"           ::grpc::ServerContext* ctx,\n"
					"           const $Request$* req,\n"
					"           ::grpc::ServerWriter<$Response$>* writer) {\n"
					"             return service->$Method$(ctx, req, writer);\n"
					"           }, this)));\n");
		} else if (bidi_streaming(method)) {
			PV("AddMethod(new ::grpc::internal::RpcServiceMethod(\n"
					"    $prefix$$Service$_method_names[$Idx$],\n"
					"    ::grpc::internal::RpcMethod::BIDI_STREAMING,\n"
					"    new ::grpc::internal::BidiStreamingHandler< "
					"$ns$$Service$::Service, $Request$, $Response$>(\n"
					"        []($ns$$Service$::Service* service,\n"
					"           ::grpc::ServerContext* ctx,\n"
					"           ::grpc::ServerReaderWriter<$Response$,\n"
					"           $Request$>* stream) {\n"
					"             return service->$Method$(ctx, stream);\n"
					"           }, this)));\n");
		}
	}
	lp_outdent(printer);
	PV("}\n\n");
	PV("$ns$$Service$::Service::~Service() {\n"
			"}\n\n");
	for (int i = 0; i < service->n_methods; ++i) {
		SET("Idx", xfmt("%d", i));
		PrintSourceServerMethod(printer, &service->methods[i], params, vars);
	}
}

static COLD void GetSourceServices(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;
	/* Package string is empty or ends with a dot. It is used to fully qualify method names. */
	SET("Package", *file->file->package ? xcat(file->file->package, ".") : "");
	if (*params->services_namespace) {
		SET("ns", xcat(params->services_namespace, "::"));
		SET("prefix", params->services_namespace);
	} else {
		SET("ns", "");
		SET("prefix", "");
	}

	for (int i = 0; i < file->file->n_services; ++i) {
		PrintSourceService(printer, &file->file->services[i], params, vars);
		P("\n");
	}
}

static COLD void GetSourceEpilogue(Buf *output, const ProtoBufFile *file)
{
	if (*file->file->package) {
		Vec parts = grpc_tokenize(file->file->package, ".");
		for (int i = 0; i < parts.n; i++) {
			buf_adds(output, "}  // namespace ");
			buf_adds(output, parts.v[i]);
			buf_adds(output, "\n");
		}
		/* Must be included at end of file */
		buf_adds(output, "#include <grpcpp/ports_undef.inc>\n");
		buf_adds(output, "\n");
	}
}

static COLD void GetMockPrologue(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	SET("filename", file->file->name);
	SET("filename_identifier", FilenameIdentifier(file->file->name));
	SET("filename_base", grpc_strip_proto(file->file->name));
	SET("message_header_ext", message_header_ext(params));
	SET("service_header_ext", kCppGeneratorServiceHeaderExt);

	PV("// Generated by the gRPC C++ plugin.\n");
	PV("// If you make any local change, they will be lost.\n");
	PV("// source: $filename$\n\n");

	PV("#ifndef GRPC_MOCK_$filename_identifier$__INCLUDED\n");
	PV("#define GRPC_MOCK_$filename_identifier$__INCLUDED\n");
	PV("\n");
	PV("#include \"$filename_base$$message_header_ext$\"\n");
	PV("#include \"$filename_base$$service_header_ext$\"\n");
	if (params->include_import_headers)
		print_import_includes(printer, vars, file->file);
	PV(""); /* file->additional_headers() */
	PV("\n");
}

static COLD void GetMockIncludes(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;

	static const char *const headers_strs[] = {
		"grpcpp/support/async_stream.h",
		"grpcpp/support/sync_stream.h",
	};
	PrintIncludes(printer, headers_strs, 2, params->use_system_headers, params->grpc_search_path);

	if (!*params->gmock_search_path) {
		static const char *const gmock_header[] = {"gmock/gmock.h"};
		PrintIncludes(printer, gmock_header, 1, params->use_system_headers, params->grpc_search_path);
	} else {
		static const char *const gmock_header[] = {"gmock.h"};
		/* We use local includes when a gmock_search_path is given */
		PrintIncludes(printer, gmock_header, 1, false, params->gmock_search_path);
	}

	if (*file->file->package)
		print_namespaces_open(printer, vars, file->file);

	PV("\n");
}

static COLD void PrintMockClientMethods(LpPrinter *printer, const LpMethod *method, const Parameters *params,
			LpVars *vars)
{
	SET("Method", method->name);
	SET("Request", type_name(method->input_type));
	SET("Response", type_name(method->output_type));

	typedef struct {
		const char *prefix;
		const char *method_params;  /* extra arguments to method */
		int extra_method_param_count;
	} AsyncPrefix;
	static const AsyncPrefix async_prefixes[] = {{"Async", ", void* tag", 1}, {"PrepareAsync", "", 0}};

	if (no_streaming(method)) {
		PV("MOCK_METHOD3($Method$, ::grpc::Status(::grpc::ClientContext* context, "
				"const $Request$& request, $Response$* response));\n");
		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				PV("MOCK_METHOD3($AsyncPrefix$$Method$Raw, "
						"::grpc::ClientAsyncResponseReaderInterface< $Response$>*"
						"(::grpc::ClientContext* context, const $Request$& request, "
						"::grpc::CompletionQueue* cq));\n");
			}
		}
	} else if (client_only_streaming(method)) {
		PV("MOCK_METHOD2($Method$Raw, "
				"::grpc::ClientWriterInterface< $Request$>*"
				"(::grpc::ClientContext* context, $Response$* response));\n");
		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("MockArgs", xfmt("%d", 3 + async_prefix->extra_method_param_count));
				PV("MOCK_METHOD$MockArgs$($AsyncPrefix$$Method$Raw, "
						"::grpc::ClientAsyncWriterInterface< $Request$>*"
						"(::grpc::ClientContext* context, $Response$* response, "
						"::grpc::CompletionQueue* cq$AsyncMethodParams$));\n");
			}
		}
	} else if (server_only_streaming(method)) {
		PV("MOCK_METHOD2($Method$Raw, "
				"::grpc::ClientReaderInterface< $Response$>*"
				"(::grpc::ClientContext* context, const $Request$& request));\n");
		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("MockArgs", xfmt("%d", 3 + async_prefix->extra_method_param_count));
				PV("MOCK_METHOD$MockArgs$($AsyncPrefix$$Method$Raw, "
						"::grpc::ClientAsyncReaderInterface< $Response$>*"
						"(::grpc::ClientContext* context, const $Request$& request, "
						"::grpc::CompletionQueue* cq$AsyncMethodParams$));\n");
			}
		}
	} else if (bidi_streaming(method)) {
		PV("MOCK_METHOD1($Method$Raw, "
				"::grpc::ClientReaderWriterInterface< $Request$, $Response$>*"
				"(::grpc::ClientContext* context));\n");
		if (params->allow_cq_api) {
			FOR_ASYNC_PREFIX {
				SET("AsyncPrefix", async_prefix->prefix);
				SET("AsyncMethodParams", async_prefix->method_params);
				SET("MockArgs", xfmt("%d", 2 + async_prefix->extra_method_param_count));
				PV("MOCK_METHOD$MockArgs$($AsyncPrefix$$Method$Raw, "
						"::grpc::ClientAsyncReaderWriterInterface<$Request$, "
						"$Response$>*"
						"(::grpc::ClientContext* context, ::grpc::CompletionQueue* cq"
						"$AsyncMethodParams$));\n");
			}
		}
	}
}

static COLD void PrintMockService(LpPrinter *printer, const LpService *service, const Parameters *params,
			LpVars *vars)
{
	SET("Service", service->name);

	PV("class Mock$Service$Stub : public $Service$::StubInterface {\n"
			" public:\n");
	lp_indent(printer);
	for (int i = 0; i < service->n_methods; ++i) {
		PrintMockClientMethods(printer, &service->methods[i], params, vars);
	}
	lp_outdent(printer);
	P("};\n");
}

static COLD void GetMockServices(Buf *output, const ProtoBufFile *file, const Parameters *params)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;
	/* Package string is empty or ends with a dot. It is used to fully qualify method names. */
	SET("Package", *file->file->package ? xcat(file->file->package, ".") : "");

	if (*params->services_namespace) {
		SET("services_namespace", params->services_namespace);
		PV("\nnamespace $services_namespace$ {\n\n");
	}

	for (int i = 0; i < file->file->n_services; i++) {
		PrintMockService(printer, &file->file->services[i], params, vars);
		P("\n");
	}

	if (*params->services_namespace)
		PV("} // namespace $services_namespace$\n\n");
}

static COLD void GetMockEpilogue(Buf *output, const ProtoBufFile *file)
{
	LpPrinter printer_, *printer = cg_printer(&printer_, output);
	LpVars vars_ = {0}, *vars = &vars_;
	SET("filename_identifier", FilenameIdentifier(file->file->name));

	print_namespaces_close(printer, vars, file->file);

	PV("\n");
	PV("#endif  // GRPC_MOCK_$filename_identifier$__INCLUDED\n");
}
