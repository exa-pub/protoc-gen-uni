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

/* Port of src/compiler/php_generator.{h,cc} and php_generator_helpers.h of grpc v1.85.1.
 * GeneratedClassName (compiler/php/names.cc of protobuf 35.1) is php_names.c of libprotoc. */

enum { FILEOPT_PHP_CLASS_PREFIX = 40, FILEOPT_PHP_NAMESPACE = 41 };

/* ---- php_generator_helpers.h ---- */

/* GetPHPServiceClassname */
static char *php_service_classname(const LpService *service, const char *class_suffix, bool is_server)
{
	return xcat(service->name, *class_suffix ? class_suffix : is_server ? "" : "Client", is_server ? "Stub" : "");
}

/* ReplaceAll: all instances of search (not empty) in s replaced with replace (s may contain NUL;
 * the result is NUL-terminated). */
static Str php_replace_all(Str s, const char *search, const char *replace)
{
	size_t n = strlen(search);
	Buf b = {0};
	size_t i = 0;
	while (i < s.n) {
		if (s.n - i >= n && memcmp(s.p + i, search, n) == 0) {
			buf_adds(&b, replace);
			i += n;
		} else {
			buf_addc(&b, s.p[i++]);
		}
	}
	buf_str(&b);
	return (Str){b.p ? b.p : "", b.n};
}

/* file->options().php_namespace(), or NULL if not set. The port's strings end at NUL: a NUL in
 * php_namespace or php_class_prefix (printed whole upstream) is unsupported. */
static const char *php_namespace_option(const LpFile *file)
{
	if (!pb_has(file->options, FILEOPT_PHP_NAMESPACE))
		return NULL;
	Str ns = pb_bytes(file->options, FILEOPT_PHP_NAMESPACE);
	if (memchr(ns.p, 0, ns.n))
		fail("unsupported by protoc-gen-uni: php_namespace with a NUL byte in \"%s\"", file->name);
	return xstrndup(ns.p, ns.n);
}

/* GetPHPServiceFilename */
static char *php_service_filename(const LpFile *file, const LpService *service, const char *class_suffix,
				  bool is_server)
{
	Buf oss = {0};
	const char *ns = php_namespace_option(file);
	if (ns) {
		buf_adds(&oss, php_replace_all(str_c(ns), "\\", "/").p);
	} else {
		Vec tokens = grpc_tokenize(file->package, ".");
		for (int i = 0; i < tokens.n; i++) {
			buf_adds(&oss, i == 0 ? "" : "/");
			buf_adds(&oss, grpc_capitalize_first_letter(tokens.v[i]));
		}
	}
	if (oss.n)
		buf_addc(&oss, '/');
	buf_adds(&oss, php_service_classname(service, class_suffix, is_server));
	buf_adds(&oss, ".php");
	return xstrdup(buf_str(&oss));
}

/* GetPHPComments: the leading comments (detached first), lines prefixed, "*" "/" escaped. */
static Str php_get_comments(const LpSourceLocations *locs, const void *desc, const char *prefix)
{
	return php_replace_all(grpc_get_prefixed_comments(locs, desc, true, prefix), "*/", "&#42;/");
}

/* ---- php_generator.cc ---- */

static char *php_convert_to_php_namespace(const char *name)
{
	Vec tokens = grpc_tokenize(name, ".");
	Buf oss = {0};
	for (int i = 0; i < tokens.n; i++) {
		buf_adds(&oss, i == 0 ? "" : "\\");
		buf_adds(&oss, grpc_capitalize_first_letter(tokens.v[i]));
	}
	return xstrdup(buf_str(&oss));
}

/* PackageName */
static const char *php_package_name(const LpFile *file)
{
	const char *ns = php_namespace_option(file);
	return ns ? ns : php_convert_to_php_namespace(file->package);
}

/* MessageIdentifierName(GeneratedClassName(type), type->file()) */
static char *php_message_identifier_name(const LpMessage *type)
{
	Str prefix = pb_bytes(type->file->options, FILEOPT_PHP_CLASS_PREFIX);
	if (memchr(prefix.p, 0, prefix.n))
		fail("unsupported by protoc-gen-uni: php_class_prefix with a NUL byte in \"%s\"", type->file->name);
	Vec tokens = grpc_tokenize(lp_php_generated_class_name_message(type), ".");
	const char *package = php_package_name(type->file);
	return xcat(package, *package ? "\\" : "", grpc_capitalize_first_letter(tokens.v[tokens.n - 1]));
}

/* The method's comments, as the c_str() passed to Print (cut at the first NUL). */
static void php_print_comments(LpPrinter *out, const LpSourceLocations *locs, const void *desc)
{
	lp_print(out, php_get_comments(locs, desc, " *").p, NULL);
}

enum { MOPT_DEPRECATED = 33, SOPT_DEPRECATED = 33 };

static COLD void php_print_method(const LpSourceLocations *locs, const LpMethod *method, LpPrinter *out)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "service_name", method->service->full_name);
	lp_vars_set(&vars, "name", method->name);
	lp_vars_set(&vars, "input_type_id", php_message_identifier_name(method->input_type));
	lp_vars_set(&vars, "output_type_id", php_message_identifier_name(method->output_type));

	lp_print(out, "/**\n", NULL);
	if (pb_int(method->options, MOPT_DEPRECATED, 0))
		lp_print(out, " * @deprecated\n", NULL);
	php_print_comments(out, locs, method);
	if (method->client_streaming) {
		lp_vars_set(&vars, "return_type_id",
			    method->server_streaming ? "\\Grpc\\BidiStreamingCall" : "\\Grpc\\ClientStreamingCall");
		lp_print_vars(out, &vars,
			      " * @param array $$metadata metadata\n"
			      " * @param array $$options call options\n"
			      " * @return $return_type_id$\n */\n"
			      "public function $name$($$metadata = [], "
			      "$$options = []) {\n");
		lp_indent(out);
		lp_indent(out);
		if (method->server_streaming)
			lp_print(out, "return $$this->_bidiRequest(", NULL);
		else
			lp_print(out, "return $$this->_clientStreamRequest(", NULL);
		lp_print_vars(out, &vars,
			      "'/$service_name$/$name$',\n"
			      "['\\$output_type_id$','decode'],\n"
			      "$$metadata, $$options);\n");
	} else {
		if (method->server_streaming)
			lp_vars_set(&vars, "return_type_id", "\\Grpc\\ServerStreamingCall");
		else
			lp_vars_set(&vars, "return_type_id",
				    xcat("\\Grpc\\UnaryCall<\\", lp_vars_get(&vars, "output_type_id"), ">"));
		lp_print_vars(out, &vars,
			      " * @param \\$input_type_id$ $$argument input argument\n"
			      " * @param array $$metadata metadata\n"
			      " * @param array $$options call options\n"
			      " * @return $return_type_id$\n */\n"
			      "public function $name$(\\$input_type_id$ $$argument,\n"
			      "  $$metadata = [], $$options = []) {\n");
		lp_indent(out);
		lp_indent(out);
		if (method->server_streaming)
			lp_print(out, "return $$this->_serverStreamRequest(", NULL);
		else
			lp_print(out, "return $$this->_simpleRequest(", NULL);
		lp_print_vars(out, &vars,
			      "'/$service_name$/$name$',\n"
			      "$$argument,\n"
			      "['\\$output_type_id$', 'decode'],\n"
			      "$$metadata, $$options);\n");
	}
	lp_outdent(out);
	lp_outdent(out);
	lp_print(out, "}\n\n", NULL);
}

static COLD void php_print_server_method(const LpSourceLocations *locs, const LpMethod *method, LpPrinter *out)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "service_name", method->service->full_name);
	lp_vars_set(&vars, "method_name", method->name);
	lp_vars_set(&vars, "input_type_id", php_message_identifier_name(method->input_type));
	lp_vars_set(&vars, "output_type_id", php_message_identifier_name(method->output_type));

	lp_print(out, "/**\n", NULL);
	if (pb_int(method->options, MOPT_DEPRECATED, 0))
		lp_print(out, " * @deprecated\n", NULL);
	php_print_comments(out, locs, method);

	const char *method_template;
	if (method->client_streaming && method->server_streaming) {
		method_template =
			" * @param \\Grpc\\ServerCallReader $$reader read client request data "
			"of \\$input_type_id$\n"
			" * @param \\Grpc\\ServerCallWriter $$writer write response data of "
			"\\$output_type_id$\n"
			" * @param \\Grpc\\ServerContext $$context server request context\n"
			" * @return void\n"
			" */\n"
			"public function $method_name$(\n"
			"    \\Grpc\\ServerCallReader $$reader,\n"
			"    \\Grpc\\ServerCallWriter $$writer,\n"
			"    \\Grpc\\ServerContext $$context\n"
			"): void {\n"
			"    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
			"    $$writer->finish();\n"
			"}\n\n";
	} else if (method->client_streaming) {
		method_template =
			" * @param \\Grpc\\ServerCallReader $$reader read client request data "
			"of \\$input_type_id$\n"
			" * @param \\Grpc\\ServerContext $$context server request context\n"
			" * @return \\$output_type_id$ for response data, null if if error "
			"occurred\n"
			" *     initial metadata (if any) and status (if not ok) should be set "
			"to $$context\n"
			" */\n"
			"public function $method_name$(\n"
			"    \\Grpc\\ServerCallReader $$reader,\n"
			"    \\Grpc\\ServerContext $$context\n"
			"): ?\\$output_type_id$ {\n"
			"    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
			"    return null;\n"
			"}\n\n";
	} else if (method->server_streaming) {
		method_template =
			" * @param \\$input_type_id$ $$request client request\n"
			" * @param \\Grpc\\ServerCallWriter $$writer write response data of "
			"\\$output_type_id$\n"
			" * @param \\Grpc\\ServerContext $$context server request context\n"
			" * @return void\n"
			" */\n"
			"public function $method_name$(\n"
			"    \\$input_type_id$ $$request,\n"
			"    \\Grpc\\ServerCallWriter $$writer,\n"
			"    \\Grpc\\ServerContext $$context\n"
			"): void {\n"
			"    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
			"    $$writer->finish();\n"
			"}\n\n";
	} else {
		method_template =
			" * @param \\$input_type_id$ $$request client request\n"
			" * @param \\Grpc\\ServerContext $$context server request context\n"
			" * @return \\$output_type_id$ for response data, null if if error "
			"occurred\n"
			" *     initial metadata (if any) and status (if not ok) should be set "
			"to $$context\n"
			" */\n"
			"public function $method_name$(\n"
			"    \\$input_type_id$ $$request,\n"
			"    \\Grpc\\ServerContext $$context\n"
			"): ?\\$output_type_id$ {\n"
			"    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
			"    return null;\n"
			"}\n\n";
	}
	lp_print_vars(out, &vars, method_template);
}

static COLD void php_print_server_method_descriptors(const LpService *service, LpPrinter *out)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "service_name", service->full_name);

	lp_print(out, "/**\n", NULL);
	if (pb_int(service->options, SOPT_DEPRECATED, 0))
		lp_print(out, " * @deprecated\n", NULL);
	lp_print(out,
		 " * Get the method descriptors of the service for server registration\n"
		 " *\n"
		 " * @return array of \\Grpc\\MethodDescriptor for the service methods\n"
		 " */\n"
		 "public final function getMethodDescriptors(): array\n{\n",
		 NULL);
	lp_indent(out);
	lp_indent(out);
	lp_print(out, "return [\n", NULL);
	lp_indent(out);
	lp_indent(out);
	for (int i = 0; i < service->n_methods; i++) {
		const LpMethod *method = &service->methods[i];
		lp_vars_set(&vars, "method_name", method->name);
		lp_vars_set(&vars, "input_type_id", php_message_identifier_name(method->input_type));
		const char *call_type;
		switch (grpc_get_method_type(method)) {
		case GRPC_METHODTYPE_BIDI_STREAMING: call_type = "BIDI_STREAMING_CALL"; break;
		case GRPC_METHODTYPE_CLIENT_STREAMING: call_type = "CLIENT_STREAMING_CALL"; break;
		case GRPC_METHODTYPE_SERVER_STREAMING: call_type = "SERVER_STREAMING_CALL"; break;
		default: call_type = "UNARY_CALL"; break;
		}
		lp_vars_set(&vars, "call_type", call_type);
		lp_print_vars(out, &vars,
			      "'/$service_name$/$method_name$' => new \\Grpc\\MethodDescriptor(\n"
			      "    $$this,\n"
			      "    '$method_name$',\n"
			      "    '\\$input_type_id$',\n"
			      "    \\Grpc\\MethodDescriptor::$call_type$\n"
			      "),\n");
	}
	lp_outdent(out);
	lp_outdent(out);
	lp_print(out, "];\n", NULL);
	lp_outdent(out);
	lp_outdent(out);
	lp_print(out, "}\n\n", NULL);
}

/* Prints out the service descriptor object */
static COLD void php_print_service(const LpSourceLocations *locs, const LpService *service, const char *class_suffix,
				   bool is_server, LpPrinter *out)
{
	LpVars vars = {0};
	lp_print(out, "/**\n", NULL);
	if (pb_int(service->options, SOPT_DEPRECATED, 0))
		lp_print(out, " * @deprecated\n", NULL);
	php_print_comments(out, locs, service);
	lp_print(out, " */\n", NULL);
	lp_vars_set(&vars, "name", php_service_classname(service, class_suffix, is_server));
	lp_vars_set(&vars, "extends", is_server ? "" : "extends \\Grpc\\BaseStub ");
	lp_print_vars(out, &vars, "class $name$ $extends${\n\n");
	lp_indent(out);
	lp_indent(out);
	if (!is_server) {
		lp_print(out,
			 "/**\n * @param string $$hostname hostname\n"
			 " * @param array $$opts channel options\n"
			 " * @param \\Grpc\\Channel $$channel (optional) re-use channel object\n"
			 " */\n"
			 "public function __construct($$hostname, $$opts, "
			 "$$channel = null) {\n",
			 NULL);
		lp_indent(out);
		lp_indent(out);
		lp_print(out, "parent::__construct($$hostname, $$opts, $$channel);\n", NULL);
		lp_outdent(out);
		lp_outdent(out);
		lp_print(out, "}\n\n", NULL);
	}
	for (int i = 0; i < service->n_methods; i++) {
		if (is_server)
			php_print_server_method(locs, &service->methods[i], out);
		else
			php_print_method(locs, &service->methods[i], out);
	}
	if (is_server)
		php_print_server_method_descriptors(service, out);
	lp_outdent(out);
	lp_outdent(out);
	lp_print(out, "}\n", NULL);
}

/* GenerateFile */
static COLD char *php_generate_file(const LpFile *file, const LpService *service, const char *class_suffix,
				    bool is_server)
{
	Buf output = {0};
	LpPrinter out;
	lp_printer_init(&out, &output, '$');
	const LpSourceLocations *locs = lp_source_locations(file);

	lp_print(&out, "<?php\n", NULL);
	lp_print(&out, "// GENERATED CODE -- DO NOT EDIT!\n\n", NULL);

	Str leading_comments = php_get_comments(locs, file, "//");
	if (leading_comments.n) {
		lp_print(&out, "// Original file comments:\n", NULL);
		lp_write_raw(&out, str_c(leading_comments.p));
	}

	LpVars vars = {0};
	lp_vars_set(&vars, "package", php_package_name(file));
	lp_print_vars(&out, &vars, "namespace $package$;\n\n");

	php_print_service(locs, service, class_suffix, is_server, &out);
	return (char *)buf_str(&output);
}
