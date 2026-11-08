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

/* Port of src/compiler/python_generator.{h,cc}, python_private_generator.h,
 * python_generator_helpers.h and the Python parts of protobuf_plugin.h (ProtoBufMethod,
 * ProtoBufService and ProtoBufFile as the Python generator uses them) of grpc v1.85.1.
 *
 * Strings that come from the parameter (the strip prefixes, the grpc version) are Str: a
 * std::string may contain NUL. */

/* GeneratorConfiguration (the default one of python_plugin.cc: grpc_package_root "grpc",
 * beta_package_root "grpc.beta", import_prefix "", grpc_tools_version "") with the
 * prefixes_to_filter of the parameter. */
typedef struct {
	const Str *prefixes_to_filter;
	int n_prefixes_to_filter;
} PyGeneratorConfiguration;

#define PY_GRPC_PACKAGE_ROOT "grpc"
#define PY_BETA_PACKAGE_ROOT "grpc.beta"
#define PY_IMPORT_PREFIX ""

/* PrivateGenerator */
typedef struct {
	const PyGeneratorConfiguration *config;
	const LpFile *file;
	const LpSourceLocations *locs;
	bool generate_in_pb2_grpc;
} PyPrivateGenerator;

/* grpc_python_generator::generator_file_name (a global set by Generate) */
static const char *py_generator_file_name;

/* ---- python_generator_helpers.h ---- */

static COLD const char *py_strip_module_prefixes(const char *raw_module_name, const Str *prefixes, int n)
{
	size_t len = strlen(raw_module_name);
	for (int i = 0; i < n; i++)
		if (prefixes[i].n <= len && !memcmp(raw_module_name, prefixes[i].p, prefixes[i].n))
			return raw_module_name + prefixes[i].n;
	return raw_module_name;
}

/* ModuleName */
static COLD const char *py_module_name(const char *filename, const PyGeneratorConfiguration *config)
{
	char *basename = grpc_strip_proto(filename);
	basename = grpc_string_replace(basename, "-", "_", true);
	basename = grpc_string_replace(basename, "/", ".", true);
	return py_strip_module_prefixes(xcat(PY_IMPORT_PREFIX, basename, "_pb2"), config->prefixes_to_filter,
					config->n_prefixes_to_filter);
}

/* ModuleAlias: dots replaced with _dot_ and underscores doubled (a.b and a_dot_b differ). */
static COLD char *py_module_alias(const char *filename, const PyGeneratorConfiguration *config)
{
	char *module_name = grpc_string_replace(py_module_name(filename, config), "_", "__", true);
	return grpc_string_replace(module_name, ".", "_dot_", true);
}

/* The file name test of Generate and GetModuleAndMessagePath: longer than ".proto" and ending
 * with one of its characters (std::string::find_last_of). */
static bool py_has_proto_suffix(const char *file_name)
{
	enum { PROTO_SUFFIX_LENGTH = 6 }; /* strlen(".proto") */
	size_t n = strlen(file_name);
	return n > PROTO_SUFFIX_LENGTH && strchr(".proto", file_name[n - 1]);
}

/* GetModuleAndMessagePath */
static COLD bool py_get_module_and_message_path(const LpMessage *type, char **out, const char *generator_file_name,
						bool generate_in_pb2_grpc, const PyGeneratorConfiguration *config)
{
	const char *file_name = type->file->name;
	if (!py_has_proto_suffix(file_name))
		return false;

	const char *module;
	if (!str_eq(generator_file_name, file_name) || generate_in_pb2_grpc)
		module = xcat(py_module_alias(file_name, config), ".");
	else
		module = "";
	/* the names of the message path from the outermost type, joined by '.' */
	size_t depth = 0, i;
	for (const LpMessage *t = type; t; t = t->containing_type)
		depth++;
	const char **message_path = xalloc(sizeof(char *) * depth);
	i = depth;
	for (const LpMessage *t = type; t; t = t->containing_type)
		message_path[--i] = t->name;
	Buf message_type = {0};
	buf_adds(&message_type, module);
	for (i = 0; i < depth; i++) {
		if (i)
			buf_addc(&message_type, '.');
		buf_adds(&message_type, message_path[i]);
	}
	*out = xstrdup(buf_str(&message_type));
	return true;
}

/* get_all_comments: leading detached, leading and trailing comment lines (Str *). */
static COLD Vec py_get_all_comments(const LpSourceLocations *locs, const void *descriptor)
{
	Vec comments = {0};
	grpc_get_comment(locs, descriptor, GRPC_COMMENTTYPE_LEADING_DETACHED, &comments);
	grpc_get_comment(locs, descriptor, GRPC_COMMENTTYPE_LEADING, &comments);
	grpc_get_comment(locs, descriptor, GRPC_COMMENTTYPE_TRAILING, &comments);
	return comments;
}

/* Split: s split at delim ("" gives one ""; a final delim adds no element). */
static COLD Vec py_split(Str s, char delim)
{
	Vec append_to = {0};
	Str *elem;
	if (!s.n) {
		elem = xalloc(sizeof *elem);
		*elem = (Str){"", 0};
		vec_push(&append_to, elem);
		return append_to;
	}
	size_t current = 0;
	while (current < s.n) {
		const char *hit = memchr(s.p + current, delim, s.n - current);
		size_t next = hit ? (size_t)(hit - s.p) : s.n;
		elem = xalloc(sizeof *elem);
		*elem = (Str){s.p + current, next - current};
		vec_push(&append_to, elem);
		current = hit ? next + 1 : next;
	}
	return append_to;
}

/* ---- python_generator.cc ---- */

/* IndentScope: two-space tabs are hard-coded in the protocol compiler; doubling the indents and
 * outdents complies with PEP8. */
static void py_indent(LpPrinter *out)
{
	lp_indent(out);
	lp_indent(out);
}

static void py_outdent(LpPrinter *out)
{
	lp_outdent(out);
	lp_outdent(out);
}

static const char *py_arg_name(const LpMethod *method)
{
	return method->client_streaming ? "request_iterator" : "request";
}

/* "<client>_<server>" with the given words for streaming and unary */
static char *py_arity(const LpMethod *method, const char *stream, const char *unary, const char *sep)
{
	return xcat(method->client_streaming ? stream : unary, sep, method->server_streaming ? stream : unary);
}

/* PrintAllComments: the comments as a docstring, each line without its leading spaces. */
static COLD void py_print_all_comments(const Vec *comments, LpPrinter *out)
{
	if (comments->n == 0) {
		/* Python requires code structures like class and def to have a body, even if it is
		 * just "pass" or a docstring: emit a docstring when there is no comment. */
		lp_print(out, "\"\"\"Missing associated documentation comment in .proto file.\"\"\"\n", NULL);
		return;
	}
	lp_print(out, "\"\"\"", NULL);
	for (int i = 0; i < comments->n; i++) {
		const Str *it = comments->v[i];
		size_t start_pos = 0;
		while (start_pos < it->n && it->p[start_pos] == ' ')
			start_pos++;
		if (start_pos < it->n) {
			/* PrintRaw(it->c_str() + start_pos): up to a NUL */
			const char *p = it->p + start_pos;
			const char *nul = memchr(p, 0, it->n - start_pos);
			lp_write_raw(out, (Str){p, nul ? (size_t)(nul - p) : it->n - start_pos});
		}
		lp_print(out, "\n", NULL);
	}
	lp_print(out, "\"\"\"\n", NULL);
}

static COLD void py_print_service_comments(PyPrivateGenerator *g, const void *descriptor, LpPrinter *out)
{
	Vec comments = py_get_all_comments(g->locs, descriptor);
	py_print_all_comments(&comments, out);
}

static COLD bool py_method_paths(PyPrivateGenerator *g, const LpMethod *method, char **input, char **output)
{
	return py_get_module_and_message_path(method->input_type, input, py_generator_file_name,
					      g->generate_in_pb2_grpc, g->config) &&
	       py_get_module_and_message_path(method->output_type, output, py_generator_file_name,
					      g->generate_in_pb2_grpc, g->config);
}

static COLD void py_print_beta_deprecation(LpPrinter *out, bool function)
{
	if (function)
		lp_print(out,
			 "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
			 "\nIt is recommended to use the GA API (classes and functions in this\n"
			 "file not marked beta) for all further purposes. This function was\n"
			 "generated only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0"
			 "\"\"\"\n",
			 NULL);
	else
		lp_print(out,
			 "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
			 "\nIt is recommended to use the GA API (classes and functions in this\n"
			 "file not marked beta) for all further purposes. This class was generated\n"
			 "only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0.\"\"\"\n",
			 NULL);
}

static COLD bool py_print_beta_servicer(PyPrivateGenerator *g, const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, "class Beta$Service$Servicer:\n", "Service", service->name, NULL);
	py_indent(out);
	py_print_beta_deprecation(out, false);
	py_print_service_comments(g, service, out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_print(out, "def $Method$(self, $ArgName$, context):\n", "Method", method->name, "ArgName",
			 py_arg_name(method), NULL);
		py_indent(out);
		py_print_service_comments(g, method, out);
		lp_print(out, "context.code(beta_interfaces.StatusCode.UNIMPLEMENTED)\n", NULL);
		py_outdent(out);
	}
	py_outdent(out);
	return true;
}

static COLD bool py_print_beta_stub(PyPrivateGenerator *g, const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, "class Beta$Service$Stub:\n", "Service", service->name, NULL);
	py_indent(out);
	py_print_beta_deprecation(out, false);
	py_print_service_comments(g, service, out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_print(out,
			 "def $Method$(self, $ArgName$, timeout, metadata=None, with_call=False, "
			 "protocol_options=None):\n",
			 "Method", method->name, "ArgName", py_arg_name(method), NULL);
		py_indent(out);
		py_print_service_comments(g, method, out);
		lp_print(out, "raise NotImplementedError()\n", NULL);
		py_outdent(out);
		if (!method->server_streaming)
			lp_print(out, "$Method$.future = None\n", "Method", method->name, NULL);
	}
	py_outdent(out);
	return true;
}

/* The std::map<std::string, std::string> of the beta factories, keyed by method name: the
 * methods sorted by name (unique in a service). */
typedef struct {
	const LpMethod *method;
	char *input, *output;
} PyMethodEntry;

static int py_method_entry_cmp(const void *a, const void *b)
{
	return strcmp(((const PyMethodEntry *)a)->method->name, ((const PyMethodEntry *)b)->method->name);
}

static COLD PyMethodEntry *py_sorted_methods(PyPrivateGenerator *g, const LpService *service)
{
	PyMethodEntry *entries = xalloc(sizeof *entries * (size_t)(service->n_methods + 1));
	for (int i = 0; i < service->n_methods; i++) {
		entries[i].method = &service->methods[i];
		if (!py_method_paths(g, entries[i].method, &entries[i].input, &entries[i].output))
			return NULL;
	}
	qsort(entries, (size_t)service->n_methods, sizeof *entries, py_method_entry_cmp);
	return entries;
}

static COLD bool py_print_beta_server_factory(PyPrivateGenerator *g, const char *package_qualified_service_name,
					      const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out,
		 "def beta_create_$Service$_server(servicer, pool=None, pool_size=None, default_timeout=None, "
		 "maximum_timeout=None):\n",
		 "Service", service->name, NULL);
	py_indent(out);
	py_print_beta_deprecation(out, true);
	PyMethodEntry *methods = py_sorted_methods(g, service);
	if (!methods)
		return false;
	const char *pq = package_qualified_service_name;
	lp_print(out, "request_deserializers = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		py_indent(out);
		lp_print(out, "('$PackageQualifiedServiceName$', '$MethodName$'): $InputTypeModuleAndClass$.FromString,\n",
			 "PackageQualifiedServiceName", pq, "MethodName", methods[i].method->name,
			 "InputTypeModuleAndClass", methods[i].input, NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out, "response_serializers = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		py_indent(out);
		lp_print(out,
			 "('$PackageQualifiedServiceName$', '$MethodName$'): "
			 "$OutputTypeModuleAndClass$.SerializeToString,\n",
			 "PackageQualifiedServiceName", pq, "MethodName", methods[i].method->name,
			 "OutputTypeModuleAndClass", methods[i].output, NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out, "method_implementations = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = methods[i].method;
		py_indent(out);
		lp_print(out,
			 "('$PackageQualifiedServiceName$', '$Method$'): "
			 "face_utilities.$Constructor$(servicer.$Method$),\n",
			 "PackageQualifiedServiceName", pq, "Method", method->name, "Constructor",
			 xcat(py_arity(method, "stream_", "unary_", ""), "inline"), NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out,
		 "server_options = beta_implementations.server_options(request_deserializers=request_deserializers, "
		 "response_serializers=response_serializers, thread_pool=pool, thread_pool_size=pool_size, "
		 "default_timeout=default_timeout, maximum_timeout=maximum_timeout)\n",
		 NULL);
	lp_print(out, "return beta_implementations.server(method_implementations, options=server_options)\n", NULL);
	py_outdent(out);
	return true;
}

static COLD bool py_print_beta_stub_factory(PyPrivateGenerator *g, const char *package_qualified_service_name,
					    const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out,
		 "def beta_create_$Service$_stub(channel, host=None, metadata_transformer=None, pool=None, "
		 "pool_size=None):\n",
		 "Service", service->name, NULL);
	py_indent(out);
	py_print_beta_deprecation(out, true);
	PyMethodEntry *methods = py_sorted_methods(g, service);
	if (!methods)
		return false;
	const char *pq = package_qualified_service_name;
	lp_print(out, "request_serializers = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		py_indent(out);
		lp_print(out,
			 "('$PackageQualifiedServiceName$', '$MethodName$'): "
			 "$InputTypeModuleAndClass$.SerializeToString,\n",
			 "PackageQualifiedServiceName", pq, "MethodName", methods[i].method->name,
			 "InputTypeModuleAndClass", methods[i].input, NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out, "response_deserializers = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		py_indent(out);
		lp_print(out, "('$PackageQualifiedServiceName$', '$MethodName$'): $OutputTypeModuleAndClass$.FromString,\n",
			 "PackageQualifiedServiceName", pq, "MethodName", methods[i].method->name,
			 "OutputTypeModuleAndClass", methods[i].output, NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out, "cardinalities = {\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = methods[i].method;
		py_indent(out);
		lp_print(out, "'$Method$': cardinality.Cardinality.$Cardinality$,\n", "Method", method->name,
			 "Cardinality", py_arity(method, "STREAM", "UNARY", "_"), NULL);
		py_outdent(out);
	}
	lp_print(out, "}\n", NULL);
	lp_print(out,
		 "stub_options = beta_implementations.stub_options(host=host, "
		 "metadata_transformer=metadata_transformer, request_serializers=request_serializers, "
		 "response_deserializers=response_deserializers, thread_pool=pool, thread_pool_size=pool_size)\n",
		 NULL);
	lp_print(out,
		 "return beta_implementations.dynamic_stub(channel, '$PackageQualifiedServiceName$', "
		 "cardinalities, options=stub_options)\n",
		 "PackageQualifiedServiceName", pq, NULL);
	py_outdent(out);
	return true;
}

static COLD bool py_print_stub(PyPrivateGenerator *g, const char *package_qualified_service_name,
			       const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, "class $Service$Stub:\n", "Service", service->name, NULL);
	py_indent(out);
	py_print_service_comments(g, service, out);
	lp_print(out, "\n", NULL);
	lp_print(out, "def __init__(self, channel):\n", NULL);
	py_indent(out);
	lp_print(out, "\"\"\"Constructor.\n", NULL);
	lp_print(out, "\n", NULL);
	lp_print(out, "Args:\n", NULL);
	py_indent(out);
	lp_print(out, "channel: A grpc.Channel.\n", NULL);
	py_outdent(out);
	lp_print(out, "\"\"\"\n", NULL);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		char *request_module_and_class, *response_module_and_class;
		if (!py_method_paths(g, method, &request_module_and_class, &response_module_and_class))
			return false;
		lp_print(out, "self.$Method$ = channel.$MultiCallableConstructor$(\n", "Method", method->name,
			 "MultiCallableConstructor", py_arity(method, "stream", "unary", "_"), NULL);
		py_indent(out);
		py_indent(out);
		lp_print(out, "'/$PackageQualifiedService$/$Method$',\n", "Method", method->name,
			 "PackageQualifiedService", package_qualified_service_name, NULL);
		lp_print(out, "request_serializer=$RequestModuleAndClass$.SerializeToString,\n",
			 "RequestModuleAndClass", request_module_and_class, NULL);
		lp_print(out, "response_deserializer=$ResponseModuleAndClass$.FromString,\n", "ResponseModuleAndClass",
			 response_module_and_class, NULL);
		lp_print(out, "_registered_method=True)\n", NULL);
		py_outdent(out);
		py_outdent(out);
	}
	py_outdent(out);
	py_outdent(out);
	return true;
}

static COLD bool py_print_servicer(PyPrivateGenerator *g, const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, "class $Service$Servicer:\n", "Service", service->name, NULL);
	py_indent(out);
	py_print_service_comments(g, service, out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_print(out, "\n", NULL);
		lp_print(out, "def $Method$(self, $ArgName$, context):\n", "Method", method->name, "ArgName",
			 py_arg_name(method), NULL);
		py_indent(out);
		py_print_service_comments(g, method, out);
		lp_print(out, "context.set_code(grpc.StatusCode.UNIMPLEMENTED)\n", NULL);
		lp_print(out, "context.set_details('Method not implemented!')\n", NULL);
		lp_print(out, "raise NotImplementedError('Method not implemented!')\n", NULL);
		py_outdent(out);
	}
	py_outdent(out);
	return true;
}

static COLD bool py_print_add_servicer_to_server(PyPrivateGenerator *g, const char *package_qualified_service_name,
						 const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, "def add_$Service$Servicer_to_server(servicer, server):\n", "Service", service->name, NULL);
	py_indent(out);
	lp_print(out, "rpc_method_handlers = {\n", NULL);
	py_indent(out);
	py_indent(out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		char *request_module_and_class, *response_module_and_class;
		if (!py_method_paths(g, method, &request_module_and_class, &response_module_and_class))
			return false;
		lp_print(out, "'$Method$': grpc.$MethodHandlerConstructor$(\n", "Method", method->name,
			 "MethodHandlerConstructor", xcat(py_arity(method, "stream", "unary", "_"), "_rpc_method_handler"),
			 NULL);
		py_indent(out);
		py_indent(out);
		lp_print(out, "servicer.$Method$,\n", "Method", method->name, NULL);
		lp_print(out, "request_deserializer=$RequestModuleAndClass$.FromString,\n", "RequestModuleAndClass",
			 request_module_and_class, NULL);
		lp_print(out, "response_serializer=$ResponseModuleAndClass$.SerializeToString,\n",
			 "ResponseModuleAndClass", response_module_and_class, NULL);
		py_outdent(out);
		py_outdent(out);
		lp_print(out, "),\n", NULL);
	}
	py_outdent(out);
	py_outdent(out);
	const char *pq = package_qualified_service_name;
	lp_print(out, "}\n", NULL);
	lp_print(out, "generic_handler = grpc.method_handlers_generic_handler(\n", NULL);
	py_indent(out);
	py_indent(out);
	lp_print(out, "'$PackageQualifiedServiceName$', rpc_method_handlers)\n", "PackageQualifiedServiceName", pq,
		 NULL);
	py_outdent(out);
	py_outdent(out);
	lp_print(out, "server.add_generic_rpc_handlers((generic_handler,))\n", NULL);
	lp_print(out, "server.add_registered_method_handlers('$PackageQualifiedServiceName$', rpc_method_handlers)\n",
		 "PackageQualifiedServiceName", pq, NULL);
	py_outdent(out);
	return true;
}

/* PrintServiceClass: a class named after the service, a container for static methods (never
 * instantiated). */
static COLD bool py_print_service_class(PyPrivateGenerator *g, const char *package_qualified_service_name,
					const LpService *service, LpPrinter *out)
{
	lp_print(out, "\n\n", NULL);
	lp_print(out, " # This class is part of an EXPERIMENTAL API.\n", NULL);
	lp_print(out, "class $Service$:\n", "Service", service->name, NULL);
	py_indent(out);
	py_print_service_comments(g, service, out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		char *request_module_and_class, *response_module_and_class;
		if (!py_method_paths(g, method, &request_module_and_class, &response_module_and_class))
			return false;
		lp_print(out, "\n", NULL);
		lp_print(out, "@staticmethod\n", NULL);
		lp_print(out, "def $Method$(", "Method", method->name, NULL);
		const char *request_parameter = py_arg_name(method);
		py_indent(out);
		py_indent(out);
		lp_print(out, "$RequestParameter$,\n", "RequestParameter", request_parameter, NULL);
		lp_print(out, "target,\n", NULL);
		lp_print(out, "options=(),\n", NULL);
		lp_print(out, "channel_credentials=None,\n", NULL);
		lp_print(out, "call_credentials=None,\n", NULL);
		lp_print(out, "insecure=False,\n", NULL);
		lp_print(out, "compression=None,\n", NULL);
		lp_print(out, "wait_for_ready=None,\n", NULL);
		lp_print(out, "timeout=None,\n", NULL);
		lp_print(out, "metadata=None):\n", NULL);
		py_outdent(out);
		py_outdent(out);
		py_indent(out);
		lp_print(out, "return grpc.experimental.$ArityMethodName$(\n", "ArityMethodName",
			 py_arity(method, "stream", "unary", "_"), NULL);
		py_indent(out);
		lp_print(out, "$RequestParameter$,\n", "RequestParameter", request_parameter, NULL);
		lp_print(out, "target,\n", NULL);
		lp_print(out, "'/$PackageQualifiedService$/$Method$',\n", "PackageQualifiedService",
			 package_qualified_service_name, "Method", method->name, NULL);
		lp_print(out, "$RequestModuleAndClass$.SerializeToString,\n", "RequestModuleAndClass",
			 request_module_and_class, NULL);
		lp_print(out, "$ResponseModuleAndClass$.FromString,\n", "ResponseModuleAndClass",
			 response_module_and_class, NULL);
		lp_print(out, "options,\n", NULL);
		lp_print(out, "channel_credentials,\n", NULL);
		lp_print(out, "insecure,\n", NULL);
		lp_print(out, "call_credentials,\n", NULL);
		lp_print(out, "compression,\n", NULL);
		lp_print(out, "wait_for_ready,\n", NULL);
		lp_print(out, "timeout,\n", NULL);
		lp_print(out, "metadata,\n", NULL);
		lp_print(out, "_registered_method=True)\n", NULL);
		py_outdent(out);
		py_outdent(out);
	}
	py_outdent(out);
	return true;
}

static COLD void py_print_beta_preamble(LpPrinter *out)
{
	lp_print(out, "from $Package$ import implementations as beta_implementations\n", "Package",
		 PY_BETA_PACKAGE_ROOT, NULL);
	lp_print(out, "from $Package$ import interfaces as beta_interfaces\n", "Package", PY_BETA_PACKAGE_ROOT, NULL);
	lp_print(out, "from grpc.framework.common import cardinality\n", NULL);
	lp_print(out, "from grpc.framework.interfaces.face import utilities as face_utilities\n", NULL);
}

static int py_strcmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* PrintPreamble. grpc_tools_version is empty for this plugin: no "import warnings" and no
 * version check. */
static COLD void py_print_preamble(PyPrivateGenerator *g, LpPrinter *out)
{
	lp_print(out, "import $Package$\n", "Package", PY_GRPC_PACKAGE_ROOT, NULL);
	if (!g->generate_in_pb2_grpc)
		return;
	lp_print(out, "\n", NULL);
	/* the std::set of (module name, module alias): the alias follows from the name, so the
	 * set is the sorted unique module names */
	Vec module_names = {0};
	for (int i = 0; i < g->file->n_services; i++) {
		const LpService *service = &g->file->services[i];
		for (int j = 0; j < service->n_methods; j++) {
			const LpMethod *method = &service->methods[j];
			vec_push(&module_names, (void *)method->input_type->file->name);
			vec_push(&module_names, (void *)method->output_type->file->name);
		}
	}
	for (int i = 0; i < module_names.n; i++)
		module_names.v[i] = (void *)py_module_name(module_names.v[i], g->config);
	if (module_names.n)
		qsort(module_names.v, (size_t)module_names.n, sizeof(void *), py_strcmp);
	for (int i = 0; i < module_names.n; i++) {
		const char *module_name = module_names.v[i];
		if (i && str_eq(module_name, module_names.v[i - 1]))
			continue;
		/* ModuleAlias of the file name is the alias of its module name */
		char *alias = grpc_string_replace(grpc_string_replace(module_name, "_", "__", true), ".", "_dot_", true);
		const char *last_dot = strrchr(module_name, '.');
		char *import_statement = last_dot ? xcat("from ", xstrndup(module_name, (size_t)(last_dot - module_name)),
							 " import ", last_dot + 1)
						  : xcat("import ", module_name);
		lp_print(out, "$ImportStatement$ as $ModuleAlias$\n", "ModuleAlias", alias, "ImportStatement",
			 import_statement, NULL);
	}
}

static COLD bool py_print_ga_services(PyPrivateGenerator *g, LpPrinter *out)
{
	const char *package = g->file->package;
	if (*package)
		package = xcat(package, ".");
	for (int i = 0; i < g->file->n_services; i++) {
		const LpService *service = &g->file->services[i];
		char *package_qualified_service_name = xcat(package, service->name);
		if (!(py_print_stub(g, package_qualified_service_name, service, out) &&
		      py_print_servicer(g, service, out) &&
		      py_print_add_servicer_to_server(g, package_qualified_service_name, service, out) &&
		      py_print_service_class(g, package_qualified_service_name, service, out)))
			return false;
	}
	return true;
}

static COLD bool py_print_beta_services(PyPrivateGenerator *g, LpPrinter *out)
{
	const char *package = g->file->package;
	if (*package)
		package = xcat(package, ".");
	for (int i = 0; i < g->file->n_services; i++) {
		const LpService *service = &g->file->services[i];
		char *package_qualified_service_name = xcat(package, service->name);
		if (!(py_print_beta_servicer(g, service, out) && py_print_beta_stub(g, service, out) &&
		      py_print_beta_server_factory(g, package_qualified_service_name, service, out) &&
		      py_print_beta_stub_factory(g, package_qualified_service_name, service, out)))
			return false;
	}
	return true;
}

/* GetGrpcServices: false if a message type is in a file whose name fails the ".proto" test. */
static COLD bool py_get_grpc_services(PyPrivateGenerator *g, Buf *output)
{
	LpPrinter printer, *out = &printer;
	lp_printer_init(out, output, '$');
	if (g->generate_in_pb2_grpc) {
		lp_print(out,
			 "# Generated by the gRPC Python protocol compiler plugin. DO NOT EDIT!\n\"\"\""
			 "Client and server classes corresponding to protobuf-defined services.\"\"\"\n",
			 NULL);
		py_print_preamble(g, out);
		return py_print_ga_services(g, out);
	}
	lp_print(out, "try:\n", NULL);
	py_indent(out);
	lp_print(out,
		 "# THESE ELEMENTS WILL BE DEPRECATED.\n"
		 "# Please use the generated *_pb2_grpc.py files instead.\n",
		 NULL);
	py_print_preamble(g, out);
	py_print_beta_preamble(out);
	if (!py_print_ga_services(g, out) || !py_print_beta_services(g, out))
		return false;
	py_outdent(out);
	lp_print(out, "except ImportError:\n", NULL);
	py_indent(out);
	lp_print(out, "pass", NULL);
	py_outdent(out);
	return true;
}

/* GenerateGrpc: the _pb2_grpc.py file, or the insertion into the module_scope of _pb2.py. */
static COLD bool py_generate_grpc(LpContext *context, PyPrivateGenerator *generator, const char *file_name,
				  bool generate_in_pb2_grpc)
{
	Buf *output = generate_in_pb2_grpc ? lp_context_open(context, file_name)
					   : lp_context_open_for_insert(context, file_name, "module_scope");
	generator->generate_in_pb2_grpc = generate_in_pb2_grpc;
	Buf grpc_code = {0};
	if (!py_get_grpc_services(generator, &grpc_code))
		return false;
	buf_add(output, grpc_code.p, grpc_code.n);
	return true;
}

/* ParseParameters: the grpc version and at most one strip prefix. */
static COLD bool py_parse_parameters(Str parameter, Str *grpc_version, PyGeneratorConfiguration *config, Buf *error)
{
	Vec comma_delimited_parameters = py_split(parameter, ',');
	Str **params = (Str **)comma_delimited_parameters.v;
	if (comma_delimited_parameters.n == 1 && params[0]->n == 0) {
		*grpc_version = str_c("grpc_2_0");
	} else if (comma_delimited_parameters.n == 1) {
		*grpc_version = *params[0];
	} else if (comma_delimited_parameters.n == 2) {
		*grpc_version = *params[0];
		config->prefixes_to_filter = params[1];
		config->n_prefixes_to_filter = 1;
	} else {
		buf_adds(error, "--grpc_python_out received too many comma-delimited parameters.");
		return false;
	}
	return true;
}
