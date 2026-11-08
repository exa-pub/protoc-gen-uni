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

/* Port of src/compiler/ruby_generator.{h,cc}, ruby_generator_helpers-inl.h,
 * ruby_generator_map-inl.h (ListToDict: the variables of each Print) and
 * ruby_generator_string-inl.h of grpc v1.85.1. */

/* ---- ruby_generator_string-inl.h ---- */

/* Split: std::getline on delim (a final delim ends the last element; "" gives none). */
static Vec rb_split(const char *s, char delim)
{
	Vec elems = {0};
	const char *p = s;
	while (*p) {
		const char *e = strchr(p, delim);
		size_t n = e ? (size_t)(e - p) : strlen(p);
		vec_push(&elems, xstrndup(p, n));
		p += e ? n + 1 : n;
	}
	return elems;
}

/* Replace: the first occurrence of from. */
static char *rb_replace(const char *s, const char *from, const char *to)
{
	const char *hit = strstr(s, from);
	if (!hit)
		return xstrdup(s);
	return xcat(xstrndup(s, (size_t)(hit - s)), to, hit + strlen(from));
}

/* ReplacePrefix: from replaced with to if s starts with from. */
static bool rb_replace_prefix(char **s, const char *from, const char *to)
{
	if (!has_prefix(*s, from))
		return false;
	*s = xcat(to, *s + strlen(from));
	return true;
}

static char rb_toupper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

/* Modularize: a ruby module compatible name (underscores dropped, the letter after one upper
 * case, the first character upper case). */
static char *rb_modularize(const char *s)
{
	if (!*s)
		return xstrdup(s);
	Buf b = {0};
	bool was_last_underscore = false;
	buf_addc(&b, rb_toupper(s[0]));
	for (size_t i = 1; s[i]; i++) {
		if (was_last_underscore && s[i] != '_')
			buf_addc(&b, rb_toupper(s[i]));
		else if (s[i] != '_')
			buf_addc(&b, s[i]);
		was_last_underscore = s[i] == '_';
	}
	return xstrdup(buf_str(&b));
}

/* RubyPackage: ruby_package (with "::" as ".") or else the proto package. */
static char *rb_ruby_package(const LpFile *file)
{
	if (pb_has(file->options, FILEOPT_RUBY_PACKAGE)) {
		Str package = pb_bytes(file->options, FILEOPT_RUBY_PACKAGE);
		if (memchr(package.p, 0, package.n))
			fail("unsupported by protoc-gen-uni: ruby_package with a NUL byte in \"%s\"", file->name);
		return str_replace_all(xstrndup(package.p, package.n), "::", ".");
	}
	return xstrdup(file->package);
}

static char *rb_rubify_constant(const char *name);

/* RubyTypeOf: the ruby name of a message type. */
static char *rb_ruby_type_of(const LpMessage *descriptor)
{
	char *proto_type = xstrdup(descriptor->full_name);
	const LpFile *file = descriptor->file;
	if (pb_has(file->options, FILEOPT_RUBY_PACKAGE)) {
		rb_replace_prefix(&proto_type, file->package, ""); /* the leading package */
		rb_replace_prefix(&proto_type, ".", "");           /* the leading . (no package) */
		proto_type = xcat(rb_ruby_package(file), ".", proto_type);
	}
	/* res starts with '.': res.find('.') is never npos */
	Vec prefixes_and_type = rb_split(xcat(".", proto_type), '.');
	Buf res = {0};
	for (int i = 0; i < prefixes_and_type.n; i++) {
		if (i != 0)
			buf_adds(&res, "::");
		if (i < prefixes_and_type.n - 1)
			buf_adds(&res, rb_modularize(prefixes_and_type.v[i]));
		else
			buf_adds(&res, rb_rubify_constant(prefixes_and_type.v[i]));
	}
	return xstrdup(buf_str(&res));
}

/* ---- ruby_generator_helpers-inl.h ---- */

/* ServicesFilename: false with the error text in *file_name_or_error if the name does not end
 * with one of the characters of ".proto" (std::string::find_last_of) or is too short. */
static bool rb_services_filename(const LpFile *file, char **file_name_or_error)
{
	enum { PROTO_SUFFIX_LENGTH = 6 }; /* length of ".proto" */
	size_t n = strlen(file->name);
	if (n > PROTO_SUFFIX_LENGTH && strchr(".proto", file->name[n - 1])) {
		*file_name_or_error = xcat(xstrndup(file->name, n - PROTO_SUFFIX_LENGTH), "_services_pb.rb");
		return true;
	}
	*file_name_or_error = "Invalid proto file name:  must end with .proto";
	return false;
}

static char *rb_messages_require_name(const LpFile *file) { return rb_replace(file->name, ".proto", "_pb"); }

/* GetRubyComments: the comment lines prefixed with "#", as the c_str() passed to Print (cut at
 * the first NUL). */
static const char *rb_get_ruby_comments(const LpSourceLocations *locs, const void *desc, bool leading)
{
	return grpc_get_prefixed_comments(locs, desc, leading, "#").p;
}

/* ---- ruby_generator.cc ---- */

/* Prints out the method using the ruby gRPC DSL. */
static void rb_print_method(const LpSourceLocations *locs, const LpMethod *method, LpPrinter *out)
{
	char *input_type = rb_ruby_type_of(method->input_type);
	if (method->client_streaming)
		input_type = xcat("stream(", input_type, ")");
	char *output_type = rb_ruby_type_of(method->output_type);
	if (method->server_streaming)
		output_type = xcat("stream(", output_type, ")");
	lp_print(out, rb_get_ruby_comments(locs, method, true), NULL);
	lp_print(out, "rpc :$mth.name$, $input.type$, $output.type$\n", "input.type", input_type, "mth.name",
		 method->name, "output.type", output_type, NULL);
	lp_print(out, rb_get_ruby_comments(locs, method, false), NULL);
}

/* Prints out the service using the ruby gRPC DSL. */
static void rb_print_service(const LpSourceLocations *locs, const LpService *service, LpPrinter *out)
{
	if (service->n_methods == 0)
		return;

	/* Begin the service module */
	lp_print(out, "module $module.name$\n", "module.name", rb_modularize(service->name), NULL);
	lp_indent(out);

	lp_print(out, rb_get_ruby_comments(locs, service, true), NULL);
	lp_print(out, "class Service\n", NULL);

	/* Write the indented class body. */
	lp_indent(out);
	lp_print(out, "\n", NULL);
	lp_print(out, "include ::GRPC::GenericService\n", NULL);
	lp_print(out, "\n", NULL);
	lp_print(out, "self.marshal_class_method = :encode\n", NULL);
	lp_print(out, "self.unmarshal_class_method = :decode\n", NULL);
	lp_print(out, "self.service_name = '$service_full_name$'\n", "service_full_name", service->full_name, NULL);
	lp_print(out, "\n", NULL);
	for (int i = 0; i < service->n_methods; ++i)
		rb_print_method(locs, &service->methods[i], out);
	lp_outdent(out);

	lp_print(out, "end\n", NULL);
	lp_print(out, "\n", NULL);
	lp_print(out, "Stub = Service.rpc_stub_class\n", NULL);

	/* End the service module */
	lp_outdent(out);
	lp_print(out, "end\n", NULL);
	lp_print(out, rb_get_ruby_comments(locs, service, false), NULL);
}

/* The following functions are copied directly from the source for the protoc ruby generator
 * (with the exception of int and string type changes). */

static bool rb_is_lower(char ch) { return ch >= 'a' && ch <= 'z'; }
static bool rb_is_upper(char ch) { return ch >= 'A' && ch <= 'Z'; }
static bool rb_is_alpha(char ch) { return rb_is_lower(ch) || rb_is_upper(ch); }

/* PackageToModule: foo_bar_baz -> FooBarBaz */
static char *rb_package_to_module(const char *name)
{
	bool next_upper = true;
	Buf b = {0};
	for (size_t i = 0; name[i]; i++) {
		if (name[i] == '_') {
			next_upper = true;
		} else {
			buf_addc(&b, next_upper ? rb_toupper(name[i]) : name[i]);
			next_upper = false;
		}
	}
	return xstrdup(buf_str(&b));
}

/* RubifyConstant: a valid Ruby constant (first letter upper case, "PB_" before a name that does
 * not start with a letter). */
static char *rb_rubify_constant(const char *name)
{
	char *ret = xstrdup(name);
	if (*ret) {
		if (rb_is_lower(ret[0]))
			ret[0] = rb_toupper(ret[0]);
		else if (!rb_is_alpha(ret[0]))
			ret = xcat("PB_", ret);
	}
	return ret;
}

/* GetServices: the services file of file ("" if it has no services). */
static COLD char *rb_get_services(const LpFile *file)
{
	Buf output = {0};
	LpPrinter out;
	lp_printer_init(&out, &output, '$');

	/* Don't write out any output if there no services, to avoid empty service files being
	 * generated for proto files that don't declare any. */
	if (file->n_services == 0)
		return "";

	const LpSourceLocations *locs = lp_source_locations(file);
	char *package_name = rb_ruby_package(file);

	/* Write out a file header. */
	lp_print(&out, "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n", NULL);
	lp_print(&out, "# Source: $file.name$ for package '$file.package$'\n", "file.name", file->name, "file.package",
		 package_name, NULL);

	const char *leading_comments = rb_get_ruby_comments(locs, file, true);
	if (*leading_comments) {
		lp_print(&out, "# Original file comments:\n", NULL);
		lp_write_raw(&out, str_c(leading_comments));
	}

	lp_print(&out, "\n", NULL);
	lp_print(&out, "require 'grpc'\n", NULL);
	/* Write out require statemment to import the separately generated file that defines the
	 * messages used by the service. This is generated by the main ruby plugin. */
	lp_print(&out, "require '$dep.name$'\n", "dep.name", rb_messages_require_name(file), NULL);

	/* Write out services within the modules */
	lp_print(&out, "\n", NULL);
	Vec modules = rb_split(package_name, '.');
	for (int i = 0; i < modules.n; ++i) {
		lp_print(&out, "module $module.name$\n", "module.name", rb_package_to_module(modules.v[i]), NULL);
		lp_indent(&out);
	}
	for (int i = 0; i < file->n_services; ++i)
		rb_print_service(locs, &file->services[i], &out);
	for (int i = 0; i < modules.n; ++i) {
		lp_outdent(&out);
		lp_print(&out, "end\n", NULL);
	}

	lp_print(&out, rb_get_ruby_comments(locs, file, false), NULL);
	return (char *)buf_str(&output);
}
