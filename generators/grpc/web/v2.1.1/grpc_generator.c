/**
 *
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Port of javascript/net/grpc/web/generator/grpc_generator.cc (grpc-web 2.1.1). The
 * std::map<string, string> of variables is an LpVars; a map passed by value is copied
 * (lp_vars_copy) where the callee changes it. */

enum Mode {
	OP = 0,      // first party google3 one platform services
	GRPCWEB = 1, // client using the application/grpc-web wire format
};

enum ImportStyle {
	CLOSURE = 0,    // goog.require("grpc.web.*")
	COMMONJS = 1,   // const grpcWeb = require("grpc-web")
	TYPESCRIPT = 2, // import * as grpcWeb from 'grpc-web'
};

static const char GRPC_PROMISE[] = "grpc.web.promise.GrpcWebPromise";

static const char *const kKeyword[] = {
	"abstract",   "boolean",      "break",      "byte",    "case",
	"catch",      "char",         "class",      "const",   "continue",
	"debugger",   "default",      "delete",     "do",      "double",
	"else",       "enum",         "export",     "extends", "false",
	"final",      "finally",      "float",      "for",     "function",
	"goto",       "if",           "implements", "import",  "in",
	"instanceof", "int",          "interface",  "long",    "native",
	"new",        "null",         "package",    "private", "protected",
	"public",     "return",       "short",      "static",  "super",
	"switch",     "synchronized", "this",       "throw",   "throws",
	"transient",  "try",          "typeof",     "var",     "void",
	"volatile",   "while",        "with",
};

// Edit the version here prior to release
#define GRPC_WEB_VERSION "2.1.1"

/* vars[key] of a map that has the key (operator[] of an absent key reads "") */
static const char *V(const LpVars *vars, const char *key)
{
	const char *v = lp_vars_get(vars, key);
	return v ? v : "";
}

static char *GetProtocVersion(LpContext *context)
{
	LpCompilerVersion v = lp_context_compiler_version(context);
	Buf b = {0};
	buf_fmt(&b, "%d.%d.%d", (int)v.major, (int)v.minor, (int)v.patch);
	buf_add_str(&b, v.suffix);
	buf_addc(&b, 0); /* the suffix may hold NUL bytes: the C string ends at the first */
	return b.p;
}

static bool IsReserved(const char *ident)
{
	for (size_t i = 0; i < sizeof(kKeyword) / sizeof(kKeyword[0]); i++)
		if (str_eq(ident, kKeyword[i]))
			return true;
	return false;
}

static const char *GetModeVar(enum Mode mode) { return mode == OP ? "OP" : "GrpcWeb"; }

static bool is_op_text_mode(const LpVars *vars)
{
	return str_eq(V(vars, "mode"), GetModeVar(OP)) && str_eq(V(vars, "binary"), "false");
}

static const char *GetDeserializeMethodName(const LpVars *vars)
{
	return is_op_text_mode(vars) ? "deserialize" : "deserializeBinary";
}

static const char *GetSerializeMethodName(const LpVars *vars)
{
	return is_op_text_mode(vars) ? "serialize" : "serializeBinary";
}

static const char *GetSerializeMethodReturnType(const LpVars *vars)
{
	return is_op_text_mode(vars) ? "string" : "!Uint8Array";
}

/* ::tolower and ::toupper of the "C" locale */
static char to_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static char to_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

static char *LowercaseFirstLetter(const char *s)
{
	char *r = xstrdup(s);
	r[0] = to_lower(r[0]);
	return r;
}

static char *Lowercase(const char *s)
{
	char *r = xstrdup(s);
	for (char *c = r; *c; c++)
		*c = to_lower(*c);
	return r;
}

static char *UppercaseFirstLetter(const char *s)
{
	char *r = xstrdup(s);
	r[0] = to_upper(r[0]);
	return r;
}

static char *Uppercase(const char *s)
{
	char *r = xstrdup(s);
	for (char *c = r; *c; c++)
		*c = to_upper(*c);
	return r;
}

// Strips the given prefix from the string, as well as the remaining leading dot
// if it exists.
static const char *StripPrefixString(const char *str, const char *prefix)
{
	if (!has_prefix(str, prefix))
		return str;
	const char *remaining_str = str + strlen(prefix);
	if (*remaining_str == '.')
		remaining_str++;
	return remaining_str;
}

static void ReplaceCharacters(char *s, char remove, char replacewith)
{
	for (; *s; s++)
		if (*s == remove)
			*s = replacewith;
}

/* StripProto: lp_strip_proto (cpp_helpers.cc) */

/* ParseLowerUnderscore: the words of input split at '_', lower-cased (ASCII). */
static Vec ParseLowerUnderscore(const char *input)
{
	Vec words = {0};
	Buf running = {0};
	for (const char *c = input;; c++) {
		if (*c == '_' || !*c) {
			if (running.n)
				vec_push(&words, xstrndup(running.p, running.n));
			running.n = 0;
			if (!*c)
				break;
		} else {
			buf_addc(&running, to_lower(*c));
		}
	}
	return words;
}

/* ToUpperCamel of words[first...] */
static void ToUpperCamel(Buf *result, const Vec *words, int first)
{
	for (int i = first; i < words->n; i++) {
		const char *word = words->v[i];
		buf_addc(result, word[0] >= 'a' && word[0] <= 'z' ? (char)(word[0] - 'a' + 'A') : word[0]);
		buf_adds(result, word + 1);
	}
}

static char *UpperCamel(const char *input)
{
	Vec words = ParseLowerUnderscore(input);
	Buf b = {0};
	ToUpperCamel(&b, &words, 0);
	return (char *)buf_str(&b);
}

// Returns the alias we assign to the module of the given .proto filename
// when importing.
static char *ModuleAlias(const char *filename)
{
	char *basename = lp_strip_proto(filename);
	ReplaceCharacters(basename, '-', '$');
	ReplaceCharacters(basename, '/', '_');
	ReplaceCharacters(basename, '.', '_');
	return xcat(basename, "_pb");
}

static const char *JSMessageType(const LpMessage *desc, const LpFile *file)
{
	const char *class_name = StripPrefixString(desc->full_name, desc->file->package);
	if (desc->file == file) {
		// [for protobuf .d.ts files only] Do not add the module prefix for local
		// messages.
		return class_name;
	}
	return xcat(ModuleAlias(desc->file->name), ".", class_name);
}

enum { FOPT_JSTYPE = 6, JS_STRING = 1, MSGOPT_MAP_ENTRY = 7 };

static const char *JSElementType(const LpField *desc, const LpFile *file)
{
	switch (desc->type) {
	case PT_DOUBLE:
	case PT_FLOAT:
	case PT_INT32:
	case PT_UINT32:
	case PT_SINT32:
	case PT_FIXED32:
	case PT_SFIXED32:
		return "number";

	case PT_INT64:
	case PT_UINT64:
	case PT_SINT64:
	case PT_FIXED64:
	case PT_SFIXED64:
		return pb_int(desc->options, FOPT_JSTYPE, 0) == JS_STRING ? "string" : "number";

	case PT_BOOL:
		return "boolean";

	case PT_STRING:
		return "string";

	case PT_BYTES:
		return "Uint8Array | string";

	case PT_ENUM: {
		const LpEnum *e = desc->enum_type;
		const char *name = StripPrefixString(e->full_name, e->file->package);
		if (e->file == file) {
			// [for protobuf .d.ts files only] Do not add the module prefix for
			// local messages.
			return name;
		}
		return xcat(ModuleAlias(e->file->name), ".", name);
	}

	case PT_MESSAGE:
		return JSMessageType(desc->message_type, file);

	default:
		return "{}";
	}
}

static const char *JSFieldType(const LpField *desc, const LpFile *file)
{
	const char *js_field_type = JSElementType(desc, file);
	if (proto_field_is_map(desc)) {
		const char *key_type = JSFieldType(&desc->message_type->fields[0], file);
		const char *value_type = JSFieldType(&desc->message_type->fields[1], file);
		return xcat("jspb.Map<", key_type, ", ", value_type, ">");
	}
	if (proto_field_is_repeated(desc))
		return xcat("Array<", js_field_type, ">");
	return js_field_type;
}

static const char *AsObjectFieldType(const LpField *desc, const LpFile *file)
{
	if (desc->type != PT_MESSAGE)
		return JSFieldType(desc, file);
	if (proto_field_is_map(desc)) {
		const LpMessage *message = desc->message_type;
		const char *key_type = AsObjectFieldType(&message->fields[0], file);
		const char *value_type = AsObjectFieldType(&message->fields[1], file);
		return xcat("Array<[", key_type, ", ", value_type, "]>");
	}
	const char *field_type = xcat(JSMessageType(desc->message_type, file), ".AsObject");
	if (proto_field_is_repeated(desc))
		return xcat("Array<", field_type, ">");
	return field_type;
}

static char *JSElementName(const LpField *desc) { return UpperCamel(desc->name); }

static const char *JSFieldName(const LpField *desc)
{
	char *js_field_name = JSElementName(desc);
	if (proto_field_is_map(desc))
		return xcat(js_field_name, "Map");
	if (proto_field_is_repeated(desc))
		return xcat(js_field_name, "List");
	return js_field_name;
}

// Like JSFieldName, but with first letter not uppercased (ToCamelCase)
static const char *CamelCaseJSFieldName(const LpField *desc)
{
	Vec words = ParseLowerUnderscore(desc->name);
	Buf b = {0};
	if (words.n) {
		buf_adds(&b, words.v[0]);
		ToUpperCamel(&b, &words, 1);
	}
	if (proto_field_is_map(desc))
		buf_adds(&b, "Map");
	else if (proto_field_is_repeated(desc))
		buf_adds(&b, "List");
	return buf_str(&b);
}

// Returns the name of the message with a leading dot and taking into account
// nesting, for example ".OuterMessage.InnerMessage". This function does not
// handle namespacing, only message nesting.
static const char *GetNestedMessageName(const LpMessage *descriptor)
{
	const char *result = StripPrefixString(descriptor->full_name, descriptor->file->package);
	// Add a leading dot if one is not already present.
	if (*result && *result != '.')
		result = xcat(".", result);
	return result;
}

// Given a filename like foo/bar/baz.proto, returns the root directory
// path ../../
static const char *GetRootPath(const char *from_filename, const char *to_filename)
{
	if (has_prefix(to_filename, "google/protobuf")) {
		// Well-known types (.proto files in the google/protobuf directory) are
		// assumed to come from the 'google-protobuf' npm package.  We may want to
		// generalize this exception later by letting others put generated code in
		// their own npm packages.
		return "google-protobuf/";
	}

	Buf result = {0};
	for (const char *c = from_filename; *c; c++)
		if (*c == '/')
			buf_adds(&result, "../");
	return result.n ? buf_str(&result) : "./";
}

// Returns the basename of a file.
static const char *GetBasename(const char *filename)
{
	const char *last_slash = strrchr(filename, '/');
	return last_slash ? last_slash + 1 : filename;
}

// Adds $ suffix to reserved method names to avoid conflicts.
static const char *SafeAccessorName(const char *name)
{
	char *lower_name = Lowercase(name);
	if (str_eq(lower_name, "extension") || str_eq(lower_name, "jspbmessageid"))
		return xcat(name, "$");
	return name;
}

static int cmp_message_full_name(const void *a, const void *b)
{
	return strcmp((*(const LpMessage *const *)a)->full_name, (*(const LpMessage *const *)b)->full_name);
}

// Finds all message types used in all services in the file. Return results as a
// map of full names to descriptors to get sorted results and deterministic
// build outputs. (The messages sorted by full name, without duplicates.)
static Vec GetAllMessages(const LpFile *file)
{
	Vec messages = {0};
	for (int s = 0; s < file->n_services; ++s) {
		const LpService *service = &file->services[s];
		for (int m = 0; m < service->n_methods; ++m) {
			vec_push(&messages, service->methods[m].input_type);
			vec_push(&messages, service->methods[m].output_type);
		}
	}
	if (messages.n)
		qsort(messages.v, (size_t)messages.n, sizeof(void *), cmp_message_full_name);
	int k = 0;
	for (int i = 0; i < messages.n; i++)
		if (!k || !str_eq(((const LpMessage *)messages.v[k - 1])->full_name,
				  ((const LpMessage *)messages.v[i])->full_name))
			messages.v[k++] = messages.v[i];
	messages.n = k;
	return messages;
}

static void PrintClosureDependencies(LpPrinter *printer, const LpFile *file)
{
	Vec messages = GetAllMessages(file);
	for (int i = 0; i < messages.n; i++)
		lp_print(printer, "goog.require('proto.$full_name$');\n", "full_name",
			 ((const LpMessage *)messages.v[i])->full_name, NULL);
}

static void PrintCommonJsMessagesDeps(LpPrinter *printer, const LpFile *file)
{
	LpVars vars = {0};

	for (int i = 0; i < file->n_dependencies; i++) {
		const char *name = file->dependencies[i]->name;
		lp_vars_set(&vars, "alias", ModuleAlias(name));
		lp_vars_set(&vars, "dep_filename", xcat(GetRootPath(file->name, name), lp_strip_proto(name)));
		// we need to give each cross-file import an alias
		lp_print_vars(printer, &vars, "\nvar $alias$ = require('$dep_filename$_pb.js')\n");
	}

	const char *package = file->package;
	lp_vars_set(&vars, "package_name", package);

	if (*package) {
		lp_print_vars(printer, &vars, "const proto = {};\n");

		for (const char *dot = strchr(package, '.'); dot; dot = strchr(dot + 1, '.')) {
			lp_vars_set(&vars, "current_package_ns", xstrndup(package, (size_t)(dot - package)));
			lp_print_vars(printer, &vars, "proto.$current_package_ns$ = {};\n");
		}
	}

	// need to import the messages from our own file
	lp_vars_set(&vars, "filename", GetBasename(lp_strip_proto(file->name)));

	if (*package)
		lp_print_vars(printer, &vars, "proto.$package_name$ = require('./$filename$_pb.js');\n\n");
	else
		lp_print_vars(printer, &vars, "const proto = require('./$filename$_pb.js');\n\n");
}

static void PrintES6Imports(LpPrinter *printer, const LpFile *file)
{
	lp_print(printer, "import * as grpcWeb from 'grpc-web';\n\n", NULL);

	Map imports = {0};
	Vec messages = GetAllMessages(file);
	for (int i = 0; i < messages.n; i++) {
		const char *proto_filename = ((const LpMessage *)messages.v[i])->file->name;
		char *dep_filename = xcat(GetRootPath(file->name, proto_filename), lp_strip_proto(proto_filename));
		if (map_has(&imports, dep_filename))
			continue;
		map_put(&imports, dep_filename, dep_filename);
		// We need to give each cross-file import an alias.
		lp_print(printer,
			 "import * as $alias$ from '$dep_filename$_pb'; // proto import: \"$proto_filename$\"\n",
			 "alias", ModuleAlias(proto_filename), "dep_filename", dep_filename, "proto_filename",
			 proto_filename, NULL);
	}
	lp_print(printer, "\n\n", NULL);
}

static void PrintTypescriptFile(LpPrinter *printer, const LpFile *file, LpVars vars)
{
	PrintES6Imports(printer, file);
	for (int service_index = 0; service_index < file->n_services; ++service_index) {
		lp_print(printer, "export class ", NULL);
		const LpService *service = &file->services[service_index];
		lp_vars_set(&vars, "service_name", service->name);
		lp_print_vars(printer, &vars, "$service_name$Client {\n");
		lp_indent(printer);
		lp_print(printer,
			 "client_: grpcWeb.AbstractClientBase;\n"
			 "hostname_: string;\n"
			 "credentials_: null | { [index: string]: string; };\n"
			 "options_: null | { [index: string]: any; };\n\n"
			 "constructor (hostname: string,\n"
			 "             credentials?: null | { [index: string]: string; },\n"
			 "             options?: null | { [index: string]: any; }) {\n",
			 NULL);
		lp_indent(printer);
		lp_print(printer, "if (!options) options = {};\n", NULL);
		lp_print(printer, "if (!credentials) credentials = {};\n", NULL);
		if (str_eq(V(&vars, "mode"), GetModeVar(GRPCWEB)))
			lp_print_vars(printer, &vars, "options['format'] = '$format$';\n\n");
		lp_print_vars(printer, &vars,
			      "this.client_ = new grpcWeb.$mode$ClientBase(options);\n"
			      "this.hostname_ = hostname.replace(/\\/+$$/, '');\n"
			      "this.credentials_ = credentials;\n"
			      "this.options_ = options;\n");
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);

		for (int method_index = 0; method_index < service->n_methods; ++method_index) {
			const LpMethod *method = &service->methods[method_index];
			lp_vars_set(&vars, "js_method_name", LowercaseFirstLetter(method->name));
			lp_vars_set(&vars, "method_name", method->name);
			lp_vars_set(&vars, "input_type", JSMessageType(method->input_type, NULL));
			lp_vars_set(&vars, "output_type", JSMessageType(method->output_type, NULL));
			lp_vars_set(&vars, "serialize_func_name", GetSerializeMethodName(&vars));
			lp_vars_set(&vars, "deserialize_func_name", GetDeserializeMethodName(&vars));
			lp_vars_set(&vars, "method_type",
				    method->server_streaming ? "grpcWeb.MethodType.SERVER_STREAMING"
							     : "grpcWeb.MethodType.UNARY");
			if (method->client_streaming)
				continue;
			lp_print_vars(printer, &vars, "methodDescriptor$method_name$ = new grpcWeb.MethodDescriptor(\n");
			lp_indent(printer);
			lp_print_vars(printer, &vars,
				      "'/$package_dot$$service_name$/$method_name$',\n"
				      "$method_type$,\n"
				      "$input_type$,\n"
				      "$output_type$,\n"
				      "(request: $input_type$) => {\n"
				      "  return request.$serialize_func_name$();\n"
				      "},\n"
				      "$output_type$.$deserialize_func_name$\n");
			lp_outdent(printer);
			lp_print(printer, ");\n\n", NULL);
			if (method->server_streaming) {
				lp_print_vars(printer, &vars, "$js_method_name$(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata?: grpcWeb.Metadata): "
					      "grpcWeb.ClientReadableStream<$output_type$> {\n");
				lp_print_vars(printer, &vars, "return this.client_.serverStreaming(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "this.hostname_ +\n"
					      "  '/$package_dot$$service_name$/$method_name$',\n"
					      "request,\n"
					      "metadata || {},\n"
					      "this.methodDescriptor$method_name$);\n");
				lp_outdent(printer);
				lp_outdent(printer);
				lp_print(printer, "}\n\n", NULL);
			} else {
				lp_print_vars(printer, &vars, "$js_method_name$(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata?: grpcWeb.Metadata | null): "
					      "$promise$<$output_type$>;\n\n");
				lp_outdent(printer);

				lp_print_vars(printer, &vars, "$js_method_name$(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata: grpcWeb.Metadata | null,\n"
					      "callback: (err: grpcWeb.RpcError,\n"
					      "           response: $output_type$) => void): "
					      "grpcWeb.ClientReadableStream<$output_type$>;\n\n");
				lp_outdent(printer);

				lp_print_vars(printer, &vars, "$js_method_name$(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata?: grpcWeb.Metadata | null,\n"
					      "callback?: (err: grpcWeb.RpcError,\n"
					      "           response: $output_type$) => void) {\n");
				lp_print_vars(printer, &vars, "if (callback !== undefined) {\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars, "return this.client_.rpcCall(\n");
				lp_indent(printer);
				lp_print_vars(printer, &vars,
					      "this.hostname_ +\n"
					      "  '/$package_dot$$service_name$/$method_name$',\n"
					      "request,\n"
					      "metadata || {},\n"
					      "this.methodDescriptor$method_name$,\n"
					      "callback);\n");
				lp_outdent(printer);
				lp_outdent(printer);
				lp_print_vars(printer, &vars,
					      "}\n"
					      "return this.client_.unaryCall(\n");
				lp_print_vars(printer, &vars,
					      "this.hostname_ +\n"
					      "  '/$package_dot$$service_name$/$method_name$',\n"
					      "request,\n"
					      "metadata || {},\n"
					      "this.methodDescriptor$method_name$);\n");
				lp_outdent(printer);
				lp_print(printer, "}\n\n", NULL);
			}
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}
}

static void PrintGrpcWebDtsClientClass(LpPrinter *printer, const LpFile *file, const char *client_type)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "client_type", client_type);
	lp_vars_set(&vars, "promise", "Promise");
	for (int service_index = 0; service_index < file->n_services; ++service_index) {
		lp_print(printer, "export class ", NULL);
		const LpService *service = &file->services[service_index];
		lp_vars_set(&vars, "service_name", service->name);
		lp_print_vars(printer, &vars, "$service_name$$client_type$ {\n");
		lp_indent(printer);
		lp_print(printer,
			 "constructor (hostname: string,\n"
			 "             credentials?: null | { [index: string]: string; },\n"
			 "             options?: null | { [index: string]: any; });\n\n",
			 NULL);
		for (int method_index = 0; method_index < service->n_methods; ++method_index) {
			const LpMethod *method = &service->methods[method_index];
			lp_vars_set(&vars, "js_method_name", LowercaseFirstLetter(method->name));
			lp_vars_set(&vars, "input_type", JSMessageType(method->input_type, NULL));
			lp_vars_set(&vars, "output_type", JSMessageType(method->output_type, NULL));
			if (method->client_streaming)
				continue;
			lp_print_vars(printer, &vars, "$js_method_name$(\n");
			lp_indent(printer);
			if (method->server_streaming) {
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata?: grpcWeb.Metadata\n");
				lp_outdent(printer);
				lp_print_vars(printer, &vars, "): grpcWeb.ClientReadableStream<$output_type$>;\n\n");
			} else if (str_eq(client_type, "PromiseClient")) {
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata?: grpcWeb.Metadata\n");
				lp_outdent(printer);
				lp_print_vars(printer, &vars, "): $promise$<$output_type$>;\n\n");
			} else {
				lp_print_vars(printer, &vars,
					      "request: $input_type$,\n"
					      "metadata: grpcWeb.Metadata | undefined,\n"
					      "callback: (err: grpcWeb.RpcError,\n"
					      "           response: $output_type$) => void\n");
				lp_outdent(printer);
				lp_print_vars(printer, &vars, "): grpcWeb.ClientReadableStream<$output_type$>;");
				lp_print(printer, "\n\n", NULL);
			}
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}
}

static void PrintGrpcWebDtsFile(LpPrinter *printer, const LpFile *file)
{
	PrintES6Imports(printer, file);
	PrintGrpcWebDtsClientClass(printer, file, "Client");
	PrintGrpcWebDtsClientClass(printer, file, "PromiseClient");
}

static void PrintProtoDtsEnum(LpPrinter *printer, const LpEnum *desc)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "enum_name", desc->name);

	// Use regular enums for broad TypeScript compatibility. `const enum`
	// triggers TS2748 when `verbatimModuleSyntax` is enabled (default in
	// TypeScript 5.9+), so prefer `enum` here.
	lp_print_vars(printer, &vars, "export enum $enum_name$ {\n");
	lp_indent(printer);
	for (int i = 0; i < desc->n_values; i++) {
		lp_vars_set(&vars, "value_name", Uppercase(desc->values[i].name));
		lp_vars_set(&vars, "value_number", xfmt("%d", desc->values[i].number));
		lp_print_vars(printer, &vars, "$value_name$ = $value_number$,\n");
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void PrintProtoDtsOneofCase(LpPrinter *printer, const LpOneof *desc)
{
	LpVars vars = {0};
	lp_vars_set(&vars, "oneof_name", UpperCamel(desc->name));
	lp_vars_set(&vars, "oneof_name_upper", Uppercase(desc->name));

	// Oneof case enums also use regular enums to avoid ambient `const enum`
	// issues under `verbatimModuleSyntax`.
	lp_print_vars(printer, &vars, "export enum $oneof_name$Case {\n");
	lp_indent(printer);
	lp_print_vars(printer, &vars, "$oneof_name_upper$_NOT_SET = 0,\n");
	for (int i = 0; i < desc->n_fields; i++) {
		const LpField *field = &desc->containing_type->fields[desc->first_field + i];
		lp_vars_set(&vars, "field_name", Uppercase(field->name));
		lp_vars_set(&vars, "field_number", xfmt("%d", field->number));
		lp_print_vars(printer, &vars, "$field_name$ = $field_number$,\n");
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void PrintProtoDtsMessage(LpPrinter *printer, const LpMessage *desc, const LpFile *file)
{
	const char *class_name = desc->name;
	LpVars vars = {0};
	lp_vars_set(&vars, "class_name", class_name);

	lp_print_vars(printer, &vars, "export class $class_name$ extends jspb.Message {\n");
	lp_indent(printer);

	lp_print_vars(printer, &vars, "constructor();\nconstructor(opt_data?: $class_name$.AsObject);\n");

	for (int i = 0; i < desc->n_fields; i++) {
		const LpField *field = &desc->fields[i];
		bool is_repeated = proto_field_is_repeated(field), is_map = proto_field_is_map(field);

		lp_vars_set(&vars, "js_field_name", SafeAccessorName(JSFieldName(field)));
		lp_vars_set(&vars, "js_field_type", JSFieldType(field, file));
		if (field->type != PT_MESSAGE || is_repeated)
			lp_print_vars(printer, &vars, "get$js_field_name$(): $js_field_type$;\n");
		else
			lp_print_vars(printer, &vars, "get$js_field_name$(): $js_field_type$ | undefined;\n");
		if (field->type == PT_BYTES && !is_repeated)
			lp_print_vars(printer, &vars,
				      "get$js_field_name$_asU8(): Uint8Array;\n"
				      "get$js_field_name$_asB64(): string;\n");
		if (!is_map && (field->type != PT_MESSAGE || is_repeated))
			lp_print_vars(printer, &vars, "set$js_field_name$(value: $js_field_type$): $class_name$;\n");
		else if (!is_map)
			lp_print_vars(printer, &vars, "set$js_field_name$(value?: $js_field_type$): $class_name$;\n");
		if (proto_field_has_presence(field))
			lp_print_vars(printer, &vars, "has$js_field_name$(): boolean;\n");
		if (field->type == PT_MESSAGE || proto_field_has_presence(field) || is_repeated || is_map)
			lp_print_vars(printer, &vars, "clear$js_field_name$(): $class_name$;\n");
		if (is_repeated && !is_map) {
			lp_vars_set(&vars, "js_field_name", SafeAccessorName(JSElementName(field)));
			lp_vars_set(&vars, "js_field_type", JSElementType(field, file));
			if (field->type != PT_MESSAGE)
				lp_print_vars(printer, &vars,
					      "add$js_field_name$(value: $js_field_type$, index?: number): $class_name$;\n");
			else
				lp_print_vars(printer, &vars,
					      "add$js_field_name$(value?: $js_field_type$, index?: number): $js_field_type$;\n");
		}

		lp_print(printer, "\n", NULL);
	}

	for (int i = 0; i < desc->n_real_oneofs; i++) {
		lp_vars_set(&vars, "js_oneof_name", UpperCamel(desc->oneofs[i].name));
		lp_print_vars(printer, &vars, "get$js_oneof_name$Case(): $class_name$.$js_oneof_name$Case;\n");
		lp_print(printer, "\n", NULL);
	}

	lp_print_vars(printer, &vars,
		      "serializeBinary(): Uint8Array;\n"
		      "toObject(includeInstance?: boolean): $class_name$.AsObject;\n"
		      "static toObject(includeInstance: boolean, msg: $class_name$): $class_name$.AsObject;\n"
		      "static serializeBinaryToWriter(message: $class_name$, writer: jspb.BinaryWriter): void;\n"
		      "static deserializeBinary(bytes: Uint8Array): $class_name$;\n"
		      "static deserializeBinaryFromReader(message: $class_name$, reader: jspb.BinaryReader): "
		      "$class_name$;\n");
	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);

	lp_print_vars(printer, &vars, "export namespace $class_name$ {\n");
	lp_indent(printer);
	lp_print(printer, "export type AsObject = {\n", NULL);
	lp_indent(printer);
	for (int i = 0; i < desc->n_fields; i++) {
		const LpField *field = &desc->fields[i];

		const char *js_field_name = CamelCaseJSFieldName(field);
		if (IsReserved(js_field_name))
			js_field_name = xcat("pb_", js_field_name);

		lp_vars_set(&vars, "js_field_name", js_field_name);
		lp_vars_set(&vars, "js_field_type", AsObjectFieldType(field, file));
		if (!proto_field_has_presence(field))
			lp_print_vars(printer, &vars, "$js_field_name$: $js_field_type$;\n");
		else
			lp_print_vars(printer, &vars, "$js_field_name$?: $js_field_type$;\n");
	}
	lp_outdent(printer);
	lp_print(printer, "};\n", NULL);

	for (int i = 0; i < desc->n_nested; i++) {
		if (pb_int(desc->nested[i].options, MSGOPT_MAP_ENTRY, 0))
			continue;
		lp_print(printer, "\n", NULL);
		PrintProtoDtsMessage(printer, &desc->nested[i], file);
	}

	for (int i = 0; i < desc->n_enums; i++) {
		lp_print(printer, "\n", NULL);
		PrintProtoDtsEnum(printer, &desc->enums[i]);
	}

	for (int i = 0; i < desc->n_oneofs; i++) {
		lp_print(printer, "\n", NULL);
		PrintProtoDtsOneofCase(printer, &desc->oneofs[i]);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void PrintProtoDtsFile(LpPrinter *printer, const LpFile *file)
{
	lp_print(printer, "import * as jspb from 'google-protobuf'\n\n", NULL);

	for (int i = 0; i < file->n_dependencies; i++) {
		const char *proto_filename = file->dependencies[i]->name;
		// We need to give each cross-file import an alias.
		lp_print(printer,
			 "import * as $alias$ from '$dep_filename$_pb'; // proto import: \"$proto_filename$\"\n",
			 "alias", ModuleAlias(proto_filename), "dep_filename",
			 xcat(GetRootPath(file->name, proto_filename), lp_strip_proto(proto_filename)),
			 "proto_filename", proto_filename, NULL);
	}
	lp_print(printer, "\n\n", NULL);

	for (int i = 0; i < file->n_messages; i++)
		PrintProtoDtsMessage(printer, &file->messages[i], file);

	for (int i = 0; i < file->n_enums; i++)
		PrintProtoDtsEnum(printer, &file->enums[i]);
}

static void PrintFileHeader(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @fileoverview gRPC-Web generated client stub for $package$\n"
		      " * @enhanceable\n"
		      " * @public\n"
		      " */\n\n"
		      "// Code generated by protoc-gen-grpc-web. DO NOT EDIT.\n"
		      "// versions:\n"
		      "// \tprotoc-gen-grpc-web v$version$\n"
		      "// \tprotoc              v$protoc_version$\n"
		      "// source: $source_file$\n\n\n"
		      "/* eslint-disable */\n"
		      "// @ts-nocheck\n\n\n");
}

static COLD void PrintMethodDescriptorFile(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @fileoverview gRPC-Web generated MethodDescriptors for $package$\n");
	if (!*V(vars, "plugins"))
		lp_print(printer, " * @enhanceable\n", NULL);
	/* without the variables: $version$ is undefined (a failed check of the debug build) */
	lp_print(printer,
		 " * @public\n"
		 " */\n\n"
		 "// Code generated by protoc-gen-grpc-web. DO NOT EDIT.\n"
		 "// versions:\n"
		 "// \tprotoc-gen-grpc-web v$version$\n"
		 "// \tprotoc              v$protoc_version$\n"
		 "// source: $source_file$\n\n\n"
		 "/* eslint-disable */\n"
		 "// @ts-nocheck\n\n\n",
		 NULL);

	lp_print_vars(printer, vars, "goog.provide('proto.$package_dot$$class_name$.$method_name$MethodDescriptor');\n\n");
	if (*V(vars, "plugins"))
		lp_print_vars(printer, vars,
			      "goog.require('$plugins$.$package_dot$$class_name$.$method_name$MethodDescriptor');\n");
	lp_print_vars(printer, vars, "goog.require('grpc.web.MethodDescriptor');\n");
	lp_print_vars(printer, vars, "goog.require('grpc.web.MethodType');\n");
	lp_print_vars(printer, vars, "goog.require('$in_type$');\n");
	if (!str_eq(V(vars, "out_type"), V(vars, "in_type")))
		lp_print_vars(printer, vars, "goog.require('$out_type$');\n");
	lp_print_vars(printer, vars, "\n\ngoog.scope(function() {\n\n");

	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @const\n"
		      " * @type {!grpc.web.MethodDescriptor<\n"
		      " *   !proto.$in$,\n"
		      " *   !proto.$out$>}\n"
		      " */\n"
		      "proto.$package_dot$$class_name$.$method_name$MethodDescriptor = \n");
	lp_indent(printer);
	lp_indent(printer);
	lp_print_vars(printer, vars, "new grpc.web.MethodDescriptor(\n");
	lp_indent(printer);
	lp_indent(printer);
	lp_print_vars(printer, vars,
		      "'/$package_dot$$service_name$/$method_name$',\n"
		      "$method_type$,\n"
		      "$in_type$,\n");
	lp_print_vars(printer, vars,
		      "$out_type$,\n"
		      "/**\n"
		      " * @param {!proto.$in$} request\n");
	lp_print(printer, xcat(" * @return {", GetSerializeMethodReturnType(vars), "}\n"), NULL);
	lp_print(printer,
		 " */\n"
		 "function(request) {\n",
		 NULL);
	lp_print(printer, xcat("  return request.", GetSerializeMethodName(vars), "();\n"), NULL);
	lp_print(printer, "},\n", NULL);
	lp_print_vars(printer, vars, xcat("$out_type$.", GetDeserializeMethodName(vars)));
	lp_print_vars(printer, vars, ");\n\n\n");
	lp_outdent(printer);
	lp_outdent(printer);
	lp_outdent(printer);
	lp_outdent(printer);
	lp_print(printer, "}); // goog.scope\n\n", NULL);
}

static void PrintServiceConstructor(LpPrinter *printer, const LpVars *vars_in, bool is_promise)
{
	LpVars vars = lp_vars_copy(vars_in);
	lp_vars_set(&vars, "is_promise", is_promise ? "Promise" : "");
	lp_print_vars(printer, &vars,
		      "/**\n"
		      " * @param {string} hostname\n"
		      " * @param {?Object} credentials\n"
		      " * @param {?grpc.web.ClientOptions} options\n"
		      " * @constructor\n"
		      " * @struct\n"
		      " * @final\n"
		      " */\n"
		      "proto.$package_dot$$service_name$$is_promise$Client =\n"
		      "    function(hostname, credentials, options) {\n"
		      "  if (!options) options = {};\n");
	if (str_eq(V(&vars, "mode"), GetModeVar(GRPCWEB)))
		lp_print_vars(printer, &vars, "  options.format = '$format$';\n\n");
	if (str_eq(V(&vars, "mode"), GetModeVar(OP)))
		lp_print_vars(printer, &vars,
			      "  /**\n"
			      "   * @private @const {!grpc.web.$mode$ClientBase} The client\n"
			      "   */\n"
			      "  this.client_ = new grpc.web.$mode$ClientBase(options, $binary$);\n\n");
	else
		lp_print_vars(printer, &vars,
			      "  /**\n"
			      "   * @private @const {!grpc.web.$mode$ClientBase} The client\n"
			      "   */\n"
			      "  this.client_ = new grpc.web.$mode$ClientBase(options);\n\n");
	lp_write_raw(printer, str_c("  /**\n"
				    "   * @private @const {string} The hostname\n"
				    "   */\n"
				    "  this.hostname_ = hostname.replace(/\\/+$/, '');\n\n"
				    "};\n\n\n"));
}

static void PrintMethodDescriptor(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @const\n"
		      " * @type {!grpc.web.MethodDescriptor<\n"
		      " *   !proto.$in$,\n"
		      " *   !proto.$out$>}\n"
		      " */\n"
		      "const methodDescriptor_$service_name$_$method_name$ = new grpc.web.MethodDescriptor(\n");
	lp_indent(printer);
	lp_print_vars(printer, vars,
		      "'/$package_dot$$service_name$/$method_name$',\n"
		      "$method_type$,\n"
		      "$in_type$,\n");
	lp_print_vars(printer, vars,
		      "$out_type$,\n"
		      "/**\n"
		      " * @param {!proto.$in$} request\n");
	lp_print(printer, xcat(" * @return {", GetSerializeMethodReturnType(vars), "}\n"), NULL);
	lp_print(printer,
		 " */\n"
		 "function(request) {\n",
		 NULL);
	lp_print(printer, xcat("  return request.", GetSerializeMethodName(vars), "();\n"), NULL);
	lp_print(printer, "},\n", NULL);
	lp_print_vars(printer, vars, xcat("$out_type$.", GetDeserializeMethodName(vars), "\n"));
	lp_outdent(printer);
	lp_print_vars(printer, vars, ");\n\n\n");
}

/* The path of a call: '/$rpc/...' in the OP modes. */
static void PrintCallPath(LpPrinter *printer, const LpVars *vars)
{
	if (str_eq(V(vars, "mode"), GetModeVar(OP)))
		lp_print_vars(printer, vars, "'/$$rpc/$package_dot$$service_name$/$method_name$',\n");
	else
		lp_print_vars(printer, vars, "'/$package_dot$$service_name$/$method_name$',\n");
}

static void PrintUnaryCall(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @param {!proto.$in$} request The\n"
		      " *     request proto\n"
		      " * @param {?Object<string, string>} metadata User defined\n"
		      " *     call metadata\n"
		      " * @param {function(?grpc.web.RpcError, ?proto.$out$)}\n"
		      " *     callback The callback function(error, response)\n"
		      " * @return {!grpc.web.ClientReadableStream<!proto.$out$>|undefined}\n"
		      " *     The XHR Node Readable Stream\n"
		      " */\n"
		      "proto.$package_dot$$service_name$Client.prototype.$js_method_name$ =\n");
	lp_indent(printer);
	lp_print_vars(printer, vars,
		      "  function(request, metadata, callback) {\n"
		      "return this.client_.rpcCall(this.hostname_ +\n");
	lp_indent(printer);
	lp_indent(printer);
	PrintCallPath(printer, vars);
	lp_print_vars(printer, vars,
		      "request,\n"
		      "metadata || {},\n"
		      "$method_descriptor$,\n"
		      "callback);\n");
	lp_outdent(printer);
	lp_outdent(printer);
	lp_outdent(printer);
	lp_print(printer, "};\n\n\n", NULL);
}

static void PrintPromiseUnaryCall(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @param {!proto.$in$} request The\n"
		      " *     request proto\n"
		      " * @param {?Object<string, string>=} metadata User defined\n"
		      " *     call metadata\n"
		      " * @return {!$promise$<!proto.$out$>}\n"
		      " *     Promise that resolves to the response\n"
		      " */\n"
		      "proto.$package_dot$$service_name$PromiseClient.prototype.$js_method_name$ =\n");
	lp_indent(printer);
	lp_print_vars(printer, vars,
		      "  function(request, metadata) {\n"
		      "return this.client_.unaryCall(this.hostname_ +\n");
	lp_indent(printer);
	lp_indent(printer);
	PrintCallPath(printer, vars);
	lp_print_vars(printer, vars,
		      "request,\n"
		      "metadata || {},\n"
		      "$method_descriptor$);\n");
	lp_outdent(printer);
	lp_outdent(printer);
	lp_outdent(printer);
	lp_print(printer, "};\n\n\n", NULL);
}

static void PrintServerStreamingCall(LpPrinter *printer, const LpVars *vars)
{
	lp_print_vars(printer, vars,
		      "/**\n"
		      " * @param {!proto.$in$} request The request proto\n"
		      " * @param {?Object<string, string>=} metadata User defined\n"
		      " *     call metadata\n"
		      " * @return {!grpc.web.ClientReadableStream<!proto.$out$>}\n"
		      " *     The XHR Node Readable Stream\n"
		      " */\n"
		      "proto.$package_dot$$service_name$$client_type$.prototype.$js_method_name$ =\n");
	lp_indent(printer);
	lp_print(printer,
		 "  function(request, metadata) {\n"
		 "return this.client_.serverStreaming(this.hostname_ +\n",
		 NULL);
	lp_indent(printer);
	lp_indent(printer);
	PrintCallPath(printer, vars);
	lp_print_vars(printer, vars,
		      "request,\n"
		      "metadata || {},\n"
		      "$method_descriptor$);\n");
	lp_outdent(printer);
	lp_outdent(printer);
	lp_outdent(printer);
	lp_print(printer, "};\n\n\n", NULL);
}

static LpPrinter *open_printer(LpContext *context, const char *filename)
{
	LpPrinter *p = xalloc(sizeof *p);
	lp_printer_init(p, lp_context_open(context, filename), '$');
	p->debug_checks = true; /* the Bazel fastbuild of the plugin keeps the debug checks */
	return p;
}

/* The key of method_descriptors (service.method) and its value. */
typedef struct {
	const char *key, *value;
} MethodDescriptorEntry;

static int cmp_method_descriptor(const void *a, const void *b)
{
	return strcmp(((const MethodDescriptorEntry *)a)->key, ((const MethodDescriptorEntry *)b)->key);
}

static COLD void PrintMultipleFilesMode(const LpFile *file, const char *file_name, LpContext *context, LpVars vars)
{
	Vec method_descriptors = {0}; /* MethodDescriptorEntry*, sorted by key below */
	Map by_key = {0};
	bool has_server_streaming = false;

	// Print MethodDescriptor files.
	for (int i = 0; i < file->n_services; ++i) {
		const LpService *service = &file->services[i];
		lp_vars_set(&vars, "service_name", service->name);
		lp_vars_set(&vars, "class_name", LowercaseFirstLetter(service->name));

		for (int method_index = 0; method_index < service->n_methods; ++method_index) {
			const LpMethod *method = &service->methods[method_index];
			char *method_file_name =
				xcat(Lowercase(service->name), "_", Lowercase(method->name), "_methoddescriptor.js");
			if (method->server_streaming)
				has_server_streaming = true;
			LpPrinter *printer = open_printer(context, method_file_name);

			lp_vars_set(&vars, "method_name", method->name);
			lp_vars_set(&vars, "in", method->input_type->full_name);
			lp_vars_set(&vars, "in_type", xcat("proto.", method->input_type->full_name));
			lp_vars_set(&vars, "out", method->output_type->full_name);
			lp_vars_set(&vars, "out_type", xcat("proto.", method->output_type->full_name));
			lp_vars_set(&vars, "method_type",
				    method->server_streaming ? "grpc.web.MethodType.SERVER_STREAMING"
							     : "grpc.web.MethodType.UNARY");

			PrintMethodDescriptorFile(printer, &vars);
			MethodDescriptorEntry *e = xalloc(sizeof *e);
			e->key = xcat(service->name, ".", method->name);
			e->value = xcat("proto.", V(&vars, "package_dot"), V(&vars, "class_name"), ".",
					V(&vars, "method_name"), "MethodDescriptor");
			MethodDescriptorEntry *prev = map_get(&by_key, e->key);
			if (prev) {
				prev->value = e->value;
			} else {
				map_put(&by_key, e->key, e);
				vec_push(&method_descriptors, e);
			}
		}
	}
	MethodDescriptorEntry *sorted = xalloc(sizeof *sorted * (size_t)(method_descriptors.n + 1));
	for (int i = 0; i < method_descriptors.n; i++)
		sorted[i] = *(MethodDescriptorEntry *)method_descriptors.v[i];
	qsort(sorted, (size_t)method_descriptors.n, sizeof *sorted, cmp_method_descriptor);

	LpPrinter *printer1 = open_printer(context, xcat(file_name, "_client_pb.js"));
	LpPrinter *printer2 = open_printer(context, xcat(file_name, "_promise_client_pb.js"));

	PrintFileHeader(printer1, &vars);
	PrintFileHeader(printer2, &vars);

	// Print the Promise and callback client.
	for (int i = 0; i < file->n_services; ++i) {
		lp_vars_set(&vars, "service_name", file->services[i].name);
		lp_print_vars(printer1, &vars, "goog.provide('proto.$package_dot$$service_name$Client');\n\n");
		lp_print_vars(printer2, &vars, "goog.provide('proto.$package_dot$$service_name$PromiseClient');\n\n");
	}

	if (str_eq(V(&vars, "promise"), GRPC_PROMISE))
		lp_print_vars(printer2, &vars, "goog.require('grpc.web.promise');\n");
	for (int i = 0; i < method_descriptors.n; i++) {
		lp_vars_set(&vars, "import_mtd", sorted[i].value);
		lp_print_vars(printer1, &vars, "goog.require('$import_mtd$');\n");
		lp_print_vars(printer2, &vars, "goog.require('$import_mtd$');\n");
	}
	lp_print_vars(printer1, &vars, "goog.require('grpc.web.$mode$ClientBase');\n");
	lp_print_vars(printer1, &vars, "goog.require('grpc.web.ClientReadableStream');\n");
	lp_print_vars(printer1, &vars, "goog.require('grpc.web.RpcError');\n");
	lp_print_vars(printer2, &vars, "goog.require('grpc.web.$mode$ClientBase');\n");
	if (has_server_streaming)
		lp_print_vars(printer2, &vars, "goog.require('grpc.web.ClientReadableStream');\n");

	PrintClosureDependencies(printer1, file);
	PrintClosureDependencies(printer2, file);

	lp_print_vars(printer1, &vars, "\ngoog.requireType('grpc.web.ClientOptions');\n");
	lp_print_vars(printer2, &vars, "\ngoog.requireType('grpc.web.ClientOptions');\n");

	lp_print(printer1, "\n\n\n", NULL);
	lp_print(printer2, "\n\n\n", NULL);
	lp_print(printer1, "goog.scope(function() {\n\n", NULL);
	lp_print(printer2, "goog.scope(function() {\n\n", NULL);

	for (int service_index = 0; service_index < file->n_services; ++service_index) {
		const LpService *service = &file->services[service_index];
		lp_vars_set(&vars, "service_name", service->name);
		PrintServiceConstructor(printer1, &vars, false);
		PrintServiceConstructor(printer2, &vars, true);

		for (int method_index = 0; method_index < service->n_methods; ++method_index) {
			const LpMethod *method = &service->methods[method_index];
			const LpMessage *input_type = method->input_type;
			const LpMessage *output_type = method->output_type;
			lp_vars_set(&vars, "js_method_name", LowercaseFirstLetter(method->name));
			lp_vars_set(&vars, "method_name", method->name);
			lp_vars_set(&vars, "in", input_type->full_name);
			lp_vars_set(&vars, "out", output_type->full_name);
			MethodDescriptorEntry *e = map_get(&by_key, xcat(service->name, ".", method->name));
			lp_vars_set(&vars, "method_descriptor", e->value);
			lp_vars_set(&vars, "in_type", xcat("proto.", input_type->full_name));
			lp_vars_set(&vars, "out_type", xcat("proto.", output_type->full_name));

			// Client streaming is not supported yet
			if (method->client_streaming)
				continue;
			if (method->server_streaming) {
				lp_vars_set(&vars, "method_type", "grpc.web.MethodType.SERVER_STREAMING");
				lp_vars_set(&vars, "client_type", "Client");
				PrintServerStreamingCall(printer1, &vars);
				lp_vars_set(&vars, "client_type", "PromiseClient");
				PrintServerStreamingCall(printer2, &vars);
			} else {
				lp_vars_set(&vars, "method_type", "grpc.web.MethodType.UNARY");
				PrintUnaryCall(printer1, &vars);
				PrintPromiseUnaryCall(printer2, &vars);
			}
		}
	}
	lp_print(printer1, "}); // goog.scope\n\n", NULL);
	lp_print(printer2, "}); // goog.scope\n\n", NULL);
}

static COLD void PrintGrpcWebClosureES6File(LpPrinter *printer, const LpFile *file)
{
	const char *package_dot = *file->package ? xcat(file->package, ".") : "";

	/* $version$ is undefined (a failed check of the debug build) */
	lp_print(printer,
		 "/**\n"
		 " * @fileoverview gRPC-Web generated client stub for '$file$'\n"
		 " */\n"
		 "\n"
		 "// Code generated by protoc-gen-grpc-web. DO NOT EDIT.\n"
		 "// versions:\n"
		 "// \tprotoc-gen-grpc-web v$version$\n"
		 "// \tprotoc              v$protoc_version$\n"
		 "// source: $source_file$\n"
		 "\n"
		 "\n",
		 "file", file->name, NULL);

	// PrintClosureES6Imports
	for (int i = 0; i < file->n_services; ++i) {
		const char *name = file->services[i].name;
		char *service_namespace = xcat("proto.", package_dot, name);
		lp_print(printer, "import $service_name$Client_import from 'goog:$namespace$';\n", "service_name", name,
			 "namespace", xcat(service_namespace, "Client"), NULL);
		lp_print(printer, "import $service_name$PromiseClient_import from 'goog:$namespace$';\n", "service_name",
			 name, "namespace", xcat(service_namespace, "PromiseClient"), NULL);
	}
	lp_print(printer, "\n\n\n", NULL);

	for (int i = 0; i < file->n_services; ++i) {
		const char *name = file->services[i].name;
		lp_print(printer, "export const $name$Client = $name$Client_import;\n", "name", name, NULL);
		lp_print(printer, "export const $name$PromiseClient = $name$PromiseClient_import;\n", "name", name, NULL);
	}
}

/* GeneratorOptions */
typedef struct {
	const char *file_name;
	const char *mode;
	const char *plugins;
	enum ImportStyle import_style;
	bool generate_dts;
	bool generate_closure_es6;
	bool multiple_files;
	bool goog_promise;
} GeneratorOptions;

/* GeneratorOptions::ParseFromOptions */
static bool ParseFromOptions(GeneratorOptions *o, const char *parameter, Buf *error)
{
	*o = (GeneratorOptions){.file_name = "", .mode = "", .plugins = "", .import_style = CLOSURE};
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	for (int i = 0; i < n; i++) {
		const char *key = keys[i], *value = values[i];
		if (str_eq(key, "out")) {
			o->file_name = value;
		} else if (str_eq(key, "mode")) {
			o->mode = value;
		} else if (str_eq(key, "import_style")) {
			if (str_eq(value, "closure")) {
				o->import_style = CLOSURE;
			} else if (str_eq(value, "experimental_closure_es6")) {
				o->import_style = CLOSURE;
				o->generate_closure_es6 = true;
			} else if (str_eq(value, "commonjs")) {
				o->import_style = COMMONJS;
			} else if (str_eq(value, "commonjs+dts")) {
				o->import_style = COMMONJS;
				o->generate_dts = true;
			} else if (str_eq(value, "typescript")) {
				o->import_style = TYPESCRIPT;
				o->generate_dts = true;
			} else {
				*error = (Buf){0};
				buf_adds(error, "options: invalid import_style - ");
				buf_adds(error, value);
				return false;
			}
		} else if (str_eq(key, "multiple_files")) {
			o->multiple_files = str_eq(value, "True");
		} else if (str_eq(key, "plugins")) {
			o->plugins = value;
		} else if (str_eq(key, "goog_promise")) {
			o->goog_promise = str_eq(value, "True");
		} else {
			*error = (Buf){0};
			buf_adds(error, "unsupported option: ");
			buf_adds(error, key);
			return false;
		}
	}

	if (!*o->mode) {
		*error = (Buf){0};
		buf_adds(error, "options: mode is required");
		return false;
	}
	return true;
}

/* GeneratorOptions::OutputFile */
static const char *OutputFile(const GeneratorOptions *o, const char *proto_file)
{
	if (o->import_style == TYPESCRIPT) {
		// Never use the value from the 'out' option when generating TypeScript.
		const char *basename = GetBasename(proto_file);
		return xcat(xstrndup(proto_file, (size_t)(basename - proto_file)),
			    UppercaseFirstLetter(lp_strip_proto(basename)), "ServiceClientPb.ts");
	}
	if (*o->file_name)
		return o->file_name;
	return xcat(lp_strip_proto(proto_file), "_grpc_web_pb.js");
}

/* GrpcCodeGenerator::Generate */
static bool grpc_web_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *context,
			      Buf *error)
{
	(void)gen;
	GeneratorOptions generator_options;
	if (!ParseFromOptions(&generator_options, parameter, error))
		return false;

	LpVars vars = {0};
	const char *package = file->package;
	lp_vars_set(&vars, "package", package);
	lp_vars_set(&vars, "package_dot", *package ? xcat(package, ".") : "");
	lp_vars_set(&vars, "promise", "Promise");
	lp_vars_set(&vars, "plugins", generator_options.plugins);

	const char *mode = generator_options.mode;
	if (str_eq(mode, "binary")) {
		lp_vars_set(&vars, "mode", GetModeVar(OP));
		lp_vars_set(&vars, "binary", "true");
	} else if (str_eq(mode, "grpcweb")) {
		lp_vars_set(&vars, "mode", GetModeVar(GRPCWEB));
		lp_vars_set(&vars, "format", "binary");
	} else if (str_eq(mode, "grpcwebtext")) {
		lp_vars_set(&vars, "mode", GetModeVar(GRPCWEB));
		lp_vars_set(&vars, "format", "text");
	} else if (str_eq(mode, "jspb")) {
		lp_vars_set(&vars, "mode", GetModeVar(OP));
		lp_vars_set(&vars, "binary", "false");
		if (generator_options.goog_promise)
			lp_vars_set(&vars, "promise", GRPC_PROMISE);
	} else {
		*error = (Buf){0};
		buf_adds(error, "options: invalid mode - ");
		buf_adds(error, mode);
		return false;
	}

	if (generator_options.generate_dts) {
		LpPrinter *proto_dts_printer = open_printer(context, xcat(lp_strip_proto(file->name), "_pb.d.ts"));
		PrintProtoDtsFile(proto_dts_printer, file);
	}

	if (!file->n_services) {
		// No services, nothing to do.
		return true;
	}

	lp_vars_set(&vars, "version", GRPC_WEB_VERSION);
	lp_vars_set(&vars, "protoc_version", GetProtocVersion(context));
	lp_vars_set(&vars, "source_file", file->name);

	const char *file_name = OutputFile(&generator_options, file->name);
	if (generator_options.multiple_files && generator_options.import_style == CLOSURE) {
		PrintMultipleFilesMode(file, file_name, context, lp_vars_copy(&vars));
		return true;
	}

	LpPrinter *printer = open_printer(context, file_name);
	PrintFileHeader(printer, &vars);

	if (generator_options.import_style == TYPESCRIPT) {
		PrintTypescriptFile(printer, file, lp_vars_copy(&vars));
		return true;
	}

	for (int i = 0; i < file->n_services; ++i) {
		lp_vars_set(&vars, "service_name", file->services[i].name);
		if (generator_options.import_style == CLOSURE) {
			lp_print_vars(printer, &vars, "goog.provide('proto.$package_dot$$service_name$Client');\n");
			lp_print_vars(printer, &vars, "goog.provide('proto.$package_dot$$service_name$PromiseClient');\n");
		}
	}
	lp_print(printer, "\n", NULL);

	if (generator_options.import_style == CLOSURE) {
		if (str_eq(V(&vars, "promise"), GRPC_PROMISE))
			lp_print_vars(printer, &vars, "goog.require('grpc.web.promise');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.MethodDescriptor');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.MethodType');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.$mode$ClientBase');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.AbstractClientBase');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.ClientReadableStream');\n");
		lp_print_vars(printer, &vars, "goog.require('grpc.web.RpcError');\n");

		PrintClosureDependencies(printer, file);
		lp_print_vars(printer, &vars, "\ngoog.requireType('grpc.web.ClientOptions');\n");
		lp_print(printer, "\n\n\n", NULL);
		lp_print(printer, "goog.scope(function() {\n\n", NULL);
	} else { /* COMMONJS */
		lp_print_vars(printer, &vars, "const grpc = {};\n");
		lp_print_vars(printer, &vars, "grpc.web = require('grpc-web');\n\n");
		PrintCommonJsMessagesDeps(printer, file);
	}

	for (int service_index = 0; service_index < file->n_services; ++service_index) {
		const LpService *service = &file->services[service_index];
		lp_vars_set(&vars, "service_name", service->name);
		PrintServiceConstructor(printer, &vars, false);
		PrintServiceConstructor(printer, &vars, true);

		for (int method_index = 0; method_index < service->n_methods; ++method_index) {
			const LpMethod *method = &service->methods[method_index];
			const LpMessage *input_type = method->input_type;
			const LpMessage *output_type = method->output_type;
			lp_vars_set(&vars, "js_method_name", LowercaseFirstLetter(method->name));
			lp_vars_set(&vars, "method_name", method->name);
			lp_vars_set(&vars, "in", input_type->full_name);
			lp_vars_set(&vars, "out", output_type->full_name);
			lp_vars_set(&vars, "method_descriptor", xcat("methodDescriptor_", service->name, "_", method->name));

			// Cross-file ref in CommonJS needs to use the module alias instead
			// of the global name.
			if (generator_options.import_style == COMMONJS && input_type->file != file)
				lp_vars_set(&vars, "in_type",
					    xcat(ModuleAlias(input_type->file->name), GetNestedMessageName(input_type)));
			else
				lp_vars_set(&vars, "in_type", xcat("proto.", input_type->full_name));
			if (generator_options.import_style == COMMONJS && output_type->file != file)
				lp_vars_set(&vars, "out_type",
					    xcat(ModuleAlias(output_type->file->name), GetNestedMessageName(output_type)));
			else
				lp_vars_set(&vars, "out_type", xcat("proto.", output_type->full_name));

			// Client streaming is not supported yet
			if (method->client_streaming)
				continue;
			if (method->server_streaming) {
				lp_vars_set(&vars, "method_type", "grpc.web.MethodType.SERVER_STREAMING");
				PrintMethodDescriptor(printer, &vars);
				lp_vars_set(&vars, "client_type", "Client");
				PrintServerStreamingCall(printer, &vars);
				lp_vars_set(&vars, "client_type", "PromiseClient");
				PrintServerStreamingCall(printer, &vars);
			} else {
				lp_vars_set(&vars, "method_type", "grpc.web.MethodType.UNARY");
				PrintMethodDescriptor(printer, &vars);
				PrintUnaryCall(printer, &vars);
				PrintPromiseUnaryCall(printer, &vars);
			}
		}
	}

	if (generator_options.import_style == CLOSURE) {
		lp_print(printer, "}); // goog.scope\n\n", NULL);
	} else if (*V(&vars, "package")) {
		lp_print_vars(printer, &vars, "module.exports = proto.$package$;\n\n");
	} else {
		lp_print_vars(printer, &vars, "module.exports = proto;\n\n");
	}

	if (generator_options.generate_dts) {
		LpPrinter *grpcweb_dts_printer =
			open_printer(context, xcat(lp_strip_proto(file->name), "_grpc_web_pb.d.ts"));
		PrintGrpcWebDtsFile(grpcweb_dts_printer, file);
	}

	if (generator_options.generate_closure_es6) {
		LpPrinter *es6_printer = open_printer(context, xcat(lp_strip_proto(file->name), ".pb.grpc-web.js"));
		PrintGrpcWebClosureES6File(es6_printer, file);
	}

	return true;
}
