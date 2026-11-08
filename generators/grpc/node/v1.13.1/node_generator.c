/*
 *
 * Copyright 2016 gRPC authors.
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

/* Port of packages/grpc-tools/src/node_generator.{h,cc} and node_generator_helpers.h of
 * grpc-node (tag grpc-tools@1.13.1). The std::map variables of Print are LpVars3; the comments
 * are printed with PrintRaw(c_str()), so they end at their first NUL byte. */

/* node_generator.h */
typedef struct {
	/* Generate a package definition object instead of Client classes */
	bool generate_package_definition;
	/* Use pure JavaScript gRPC Client */
	bool grpc_js;
	/* Omit instanceof check for messages in serialize methods */
	bool omit_serialize_instanceof;
} NodeParameters;

/* ---- node_generator_helpers.h ---- */

static char *node_js_service_filename(const char *filename)
{
	return xcat(grpc_strip_proto_1_25_0(filename), "_grpc_pb.js");
}

/* GetNodeComments(desc, leading).c_str(): leading or trailing comments, lines start with "// ". */
static const char *node_comments(const LpSourceLocations *locs, const void *desc, bool leading)
{
	return grpc_get_prefixed_comments_1_25_0(locs, desc, leading, "//").p;
}

/* ---- node_generator.cc ---- */

/* ModuleAlias: the alias we assign to the module of the given .proto filename when importing
 * (copied from js_generator.cc). */
static char *node_module_alias(const char *filename)
{
	char *basename = grpc_strip_proto_1_25_0(filename);
	basename = str_replace_all(basename, "-", "$");
	basename = str_replace_all(basename, "/", "_");
	basename = str_replace_all(basename, ".", "_");
	return xcat(basename, "_pb");
}

/* GetJSMessageFilename: foo/bar/baz.proto -> foo/bar/baz_pb.js */
static char *node_js_message_filename(const char *filename)
{
	return xcat(grpc_strip_proto_1_25_0(filename), "_pb.js");
}

/* GetRootPath: foo/bar/baz.proto -> ../../ */
static char *node_root_path(const char *from_filename, const char *to_filename)
{
	if (has_prefix(to_filename, "google/protobuf")) {
		/* Well-known types (.proto files in the google/protobuf directory) are assumed to come
		 * from the 'google-protobuf' npm package. */
		return "google-protobuf/";
	}
	size_t slashes = 0;
	for (const char *p = from_filename; *p; p++)
		slashes += *p == '/';
	if (slashes == 0)
		return "./";
	Buf result = {0};
	for (size_t i = 0; i < slashes; i++)
		buf_adds(&result, "../");
	return (char *)buf_str(&result);
}

/* GetRelativePath: the relative path to load to_file from the directory containing from_file. */
static char *node_relative_path(const char *from_file, const char *to_file)
{
	return xcat(node_root_path(from_file, to_file), to_file);
}

static int node_cmp_full_name(const void *a, const void *b)
{
	return strcmp((*(const LpMessage *const *)a)->full_name, (*(const LpMessage *const *)b)->full_name);
}

/* GetAllMessages: the message types used in all services in the file, sorted by full name (the
 * std::map). */
static COLD Vec node_all_messages(const LpFile *file)
{
	Vec message_types = {0};
	Map seen = {0};
	for (int service_num = 0; service_num < file->n_services; service_num++) {
		const LpService *service = &file->services[service_num];
		for (int method_num = 0; method_num < service->n_methods; method_num++) {
			const LpMethod *method = &service->methods[method_num];
			const LpMessage *types[2] = {method->input_type, method->output_type};
			for (int i = 0; i < 2; i++)
				if (!map_get(&seen, types[i]->full_name)) {
					map_put(&seen, types[i]->full_name, (void *)types[i]);
					vec_push(&message_types, (void *)types[i]);
				}
		}
	}
	if (message_types.n > 1)
		qsort(message_types.v, (size_t)message_types.n, sizeof(void *), node_cmp_full_name);
	return message_types;
}

static char *node_message_identifier_name(const char *name) { return str_replace_all(name, ".", "_"); }

static char *node_object_path(const LpMessage *descriptor)
{
	char *module_alias = node_module_alias(descriptor->file->name);
	char *name = (char *)descriptor->full_name;
	grpc_strip_prefix(&name, xcat(descriptor->file->package, "."));
	return xcat(module_alias, ".", name);
}

/* PrintMessageTransformer: the message serializer and deserializer functions */
static COLD void node_print_message_transformer(const LpMessage *descriptor, LpPrinter3 *out,
						 const NodeParameters *params)
{
	LpVars3 template_vars = {0};
	const char *full_name = descriptor->full_name;
	lp3_vars_set(&template_vars, "identifier_name", node_message_identifier_name(full_name));
	lp3_vars_set(&template_vars, "name", full_name);
	lp3_vars_set(&template_vars, "node_name", node_object_path(descriptor));
	/* Print the serializer */
	lp3_print_vars(out, &template_vars, "function serialize_$identifier_name$(arg) {\n");
	lp3_indent(out);
	if (!params->omit_serialize_instanceof) {
		lp3_print_vars(out, &template_vars, "if (!(arg instanceof $node_name$)) {\n");
		lp3_indent(out);
		lp3_print_vars(out, &template_vars, "throw new Error('Expected argument of type $name$');\n");
		lp3_outdent(out);
		lp3_print(out, "}\n", NULL);
	}
	lp3_print(out, "return Buffer.from(arg.serializeBinary());\n", NULL);
	lp3_outdent(out);
	lp3_print(out, "}\n\n", NULL);

	/* Print the deserializer */
	lp3_print_vars(out, &template_vars, "function deserialize_$identifier_name$(buffer_arg) {\n");
	lp3_indent(out);
	lp3_print_vars(out, &template_vars, "return $node_name$.deserializeBinary(new Uint8Array(buffer_arg));\n");
	lp3_outdent(out);
	lp3_print(out, "}\n\n", NULL);
}

static COLD void node_print_method(const LpMethod *method, LpPrinter3 *out)
{
	const LpMessage *input_type = method->input_type;
	const LpMessage *output_type = method->output_type;
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "service_name", method->service->full_name);
	lp3_vars_set(&vars, "name", method->name);
	lp3_vars_set(&vars, "input_type", node_object_path(input_type));
	lp3_vars_set(&vars, "input_type_id", node_message_identifier_name(input_type->full_name));
	lp3_vars_set(&vars, "output_type", node_object_path(output_type));
	lp3_vars_set(&vars, "output_type_id", node_message_identifier_name(output_type->full_name));
	lp3_vars_set(&vars, "client_stream", method->client_streaming ? "true" : "false");
	lp3_vars_set(&vars, "server_stream", method->server_streaming ? "true" : "false");
	lp3_print(out, "{\n", NULL);
	lp3_indent(out);
	lp3_print_vars(out, &vars, "path: '/$service_name$/$name$',\n");
	lp3_print_vars(out, &vars, "requestStream: $client_stream$,\n");
	lp3_print_vars(out, &vars, "responseStream: $server_stream$,\n");
	lp3_print_vars(out, &vars, "requestType: $input_type$,\n");
	lp3_print_vars(out, &vars, "responseType: $output_type$,\n");
	lp3_print_vars(out, &vars, "requestSerialize: serialize_$input_type_id$,\n");
	lp3_print_vars(out, &vars, "requestDeserialize: deserialize_$input_type_id$,\n");
	lp3_print_vars(out, &vars, "responseSerialize: serialize_$output_type_id$,\n");
	lp3_print_vars(out, &vars, "responseDeserialize: deserialize_$output_type_id$,\n");
	lp3_outdent(out);
	lp3_print(out, "}", NULL);
}

/* PrintService: the service descriptor object */
static COLD void node_print_service(const LpSourceLocations *locs, const LpService *service, LpPrinter3 *out,
				    const NodeParameters *params)
{
	LpVars3 template_vars = {0};
	lp3_print_raw(out, node_comments(locs, service, true));
	lp3_vars_set(&template_vars, "name", service->name);
	lp3_vars_set(&template_vars, "full_name", service->full_name);
	if (params->generate_package_definition)
		lp3_print_vars(out, &template_vars, "var $name$Service = exports['$full_name$'] = {\n");
	else
		lp3_print_vars(out, &template_vars, "var $name$Service = exports.$name$Service = {\n");
	lp3_indent(out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		char *method_name = grpc_lowercase_first_letter(method->name);
		lp3_print_raw(out, node_comments(locs, method, true));
		lp3_print(out, "$method_name$: ", "method_name", method_name, NULL);
		node_print_method(method, out);
		lp3_print(out, ",\n", NULL);
		lp3_print_raw(out, node_comments(locs, method, false));
	}
	lp3_outdent(out);
	lp3_print(out, "};\n\n", NULL);
	if (!params->generate_package_definition)
		lp3_print_vars(out, &template_vars,
			       "exports.$name$Client = grpc.makeGenericClientConstructor($name$Service, '$name$');\n");
	lp3_print_raw(out, node_comments(locs, service, false));
}

static COLD void node_print_imports(const LpFile *file, LpPrinter3 *out, const NodeParameters *params)
{
	if (!params->generate_package_definition) {
		const char *package = params->grpc_js ? "@grpc/grpc-js" : "grpc";
		lp3_print(out, "var grpc = require('$package$');\n", "package", package, NULL);
	}
	if (file->n_messages > 0) {
		char *file_path = node_relative_path(file->name, node_js_message_filename(file->name));
		lp3_print(out, "var $module_alias$ = require('$file_path$');\n", "module_alias",
			  node_module_alias(file->name), "file_path", file_path, NULL);
	}

	for (int i = 0; i < file->n_dependencies; i++) {
		const char *dependency = file->dependencies[i]->name;
		char *file_path = node_relative_path(file->name, node_js_message_filename(dependency));
		lp3_print(out, "var $module_alias$ = require('$file_path$');\n", "module_alias",
			  node_module_alias(dependency), "file_path", file_path, NULL);
	}
	lp3_print(out, "\n", NULL);
}

static COLD void node_print_transformers(const LpFile *file, LpPrinter3 *out, const NodeParameters *params)
{
	Vec messages = node_all_messages(file);
	for (int i = 0; i < messages.n; i++)
		node_print_message_transformer(messages.v[i], out, params);
	lp3_print(out, "\n", NULL);
}

static COLD void node_print_services(const LpSourceLocations *locs, const LpFile *file, LpPrinter3 *out,
				     const NodeParameters *params)
{
	for (int i = 0; i < file->n_services; i++)
		node_print_service(locs, &file->services[i], out, params);
}

/* GenerateFile */
static COLD void node_generate_file(const LpFile *file, const NodeParameters *params, Buf *output)
{
	if (file->n_services == 0) {
		buf_adds(output, "// GENERATED CODE -- NO SERVICES IN PROTO");
		return;
	}
	LpPrinter3 out;
	lp3_printer_init(&out, output, '$');
	const LpSourceLocations *locs = lp_source_locations(file);

	lp3_print(&out, "// GENERATED CODE -- DO NOT EDIT!\n\n", NULL);

	Str leading_comments = grpc_get_prefixed_comments_1_25_0(locs, (const ProtoDesc *)file, true, "//");
	if (leading_comments.n) {
		lp3_print(&out, "// Original file comments:\n", NULL);
		lp3_print_raw(&out, leading_comments.p);
	}

	lp3_print(&out, "'use strict';\n", NULL);

	node_print_imports(file, &out, params);

	node_print_transformers(file, &out, params);

	node_print_services(locs, file, &out, params);

	lp3_print_raw(&out, node_comments(locs, file, false));
}
