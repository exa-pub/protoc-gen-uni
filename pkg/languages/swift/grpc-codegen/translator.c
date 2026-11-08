/*
 * Copyright 2023, gRPC Authors All rights reserved.
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

/* Port of Internal/Translator (IDLToStructuredSwiftTranslator, ServerCodeTranslator,
 * ClientCodeTranslator, MetadataTranslator, Docs), CodeGenerator.swift and CodeGenError.swift
 * (grpc-swift-2 2.4.4). */
#include "structured_swift.h"
#include "swift_std.h"

#include <string.h>

/* ---- Docs.swift ---- */

static Str inline_docs_as_note(Str docs)
{
	Buf b = {0};
	buf_adds(&b, "/// > Source IDL Documentation:\n/// >\n");
	/* docs.split(separator: "\n"): the lines of a source comment, which contain no other
	 * newline characters; empty lines are omitted. */
	bool first = true;
	for (size_t i = 0; i < docs.n;) {
		const char *nl = memchr(docs.p + i, '\n', docs.n - i);
		size_t end = nl ? (size_t)(nl - docs.p) : docs.n;
		if (end > i) {
			Str line = {docs.p + i, end - i};
			if (!first)
				buf_addc(&b, '\n');
			first = false;
			/* "/// > " + line.dropFirst(4); trimming whitespace from a string that starts
			 * with "/" changes nothing. */
			buf_adds(&b, "/// > ");
			size_t k = swift_drop_first(line, 4, "a comment line");
			buf_add(&b, line.p + k, line.n - k);
		}
		i = end + 1;
	}
	buf_str(&b);
	return buf_view(&b);
}

COLD Str gc_docs_suffix(Str header, Str footer)
{
	if (!footer.n)
		return header;
	Buf b = {0};
	buf_add_str(&b, header);
	buf_adds(&b, "\n///\n");
	buf_add_str(&b, inline_docs_as_note(footer));
	buf_str(&b);
	return buf_view(&b);
}

COLD Str gc_docs_interpose(Str docs, Str header, Str footer)
{
	Buf b = {0};
	buf_add_str(&b, header);
	buf_adds(&b, "\n///\n");
	if (docs.n) {
		buf_add_str(&b, inline_docs_as_note(docs));
		buf_adds(&b, "\n///\n");
	}
	buf_add_str(&b, footer);
	buf_str(&b);
	return buf_view(&b);
}

static GcDecl *commentable_docs(Str header, Str documentation, GcDecl *d)
{
	return gc_commentable(gc_comment(GC_PRE_FORMATTED, gc_docs_suffix(header, documentation)), d);
}

/* ---- ServerCodeTranslator.swift ---- */

static void translate_server(Vec *blocks, GcAccess access, const GcService *s, const GcAvailability *availability,
			     const GcNamer *namer, const GcRequest *request)
{
	const char *service = s->name.identifying_name, *type_name = s->name.type_name;
	Str streaming_docs = str_c(xcat(
		"/// Streaming variant of the service protocol for the \"", service, "\" service.\n"
		"///\n"
		"/// This protocol is the lowest-level of the service protocols generated for this service\n"
		"/// giving you the most flexibility over the implementation of your service. This comes at\n"
		"/// the cost of more verbose and less strict APIs. Each RPC requires you to implement it in\n"
		"/// terms of a request stream and response stream. Where the RPC expects only a single\n"
		"/// request or response message, you are responsible for ensuring your implementation\n"
		"/// maintains this invariant.\n"
		"///\n"
		"/// Where possible, prefer using the stricter, less-verbose ``ServiceProtocol``\n"
		"/// or ``SimpleServiceProtocol`` instead."));
	Str service_docs = str_c(xcat(
		"/// Service protocol for the \"", service, "\" service.\n"
		"///\n"
		"/// This protocol is higher level than ``StreamingServiceProtocol`` but lower level than\n"
		"/// the ``SimpleServiceProtocol``, it provides access to request and response metadata and\n"
		"/// trailing response metadata. If you don't need these then consider using\n"
		"/// the ``SimpleServiceProtocol``. If you need fine grained control over your RPCs then\n"
		"/// use ``StreamingServiceProtocol``."));
	Str simple_docs = str_c(xcat(
		"/// Simple service protocol for the \"", service, "\" service.\n"
		"///\n"
		"/// This is the highest level protocol for the service. The API is the easiest to use but\n"
		"/// doesn't provide access to request or response metadata. If you need access to these\n"
		"/// then use ``ServiceProtocol`` instead."));
	const char *streaming_protocol = xcat(type_name, ".StreamingServiceProtocol");
	const char *service_protocol = xcat(type_name, ".ServiceProtocol");
	GcDecl *extension = gc_extension(
		type_name,
		gc_vec(commentable_docs(streaming_docs, s->documentation,
					gc_streaming_service_protocol(access, "StreamingServiceProtocol", s, namer)),
		       commentable_docs(service_docs, s->documentation,
					gc_service_protocol(access, "ServiceProtocol", streaming_protocol, s, namer)),
		       commentable_docs(simple_docs, s->documentation,
					gc_simple_service_protocol(access, "SimpleServiceProtocol", service_protocol, s, namer)),
		       NULL));
	vec_push(blocks, gc_block(NULL, gc_guarded(availability, extension), NULL));

	GcDecl *registration = gc_registrable_rpc_service_default_implementation(access, streaming_protocol, type_name, s,
										  namer, request);
	vec_push(blocks, gc_block(gc_comment(GC_INLINE, STR_LIT("Default implementation of 'registerMethods(with:)'.")),
				  gc_guarded(availability, registration), NULL));
	GcDecl *streaming_defaults = gc_streaming_service_protocol_default_implementation(access, service_protocol, s, namer);
	vec_push(blocks,
		 gc_block(gc_comment(GC_INLINE, STR_LIT("Default implementation of streaming methods from 'StreamingServiceProtocol'.")),
			  gc_guarded(availability, streaming_defaults), NULL));
	GcDecl *service_defaults =
		gc_service_protocol_default_implementation(access, xcat(type_name, ".SimpleServiceProtocol"), s, namer);
	vec_push(blocks, gc_block(gc_comment(GC_INLINE, STR_LIT("Default implementation of methods from 'ServiceProtocol'.")),
				  gc_guarded(availability, service_defaults), NULL));
}

/* ---- ClientCodeTranslator.swift ---- */

static void translate_client(Vec *blocks, GcAccess access, const GcService *s, const GcAvailability *availability,
			     const GcNamer *namer, const GcRequest *request)
{
	const char *service = s->name.identifying_name, *type_name = s->name.type_name;
	Str protocol_docs = str_c(xcat("/// Generated client protocol for the \"", service, "\" service.\n"
				       "///\n"
				       "/// You don't need to implement this protocol directly, use the generated\n"
				       "/// implementation, ``Client``."));
	Str client_docs = str_c(xcat("/// Generated client for the \"", service, "\" service.\n"
				     "///\n"
				     "/// The ``Client`` provides an implementation of ``ClientProtocol`` which wraps\n"
				     "/// a `", namer->grpc_core, ".GRPCCClient`. The underlying `GRPCClient` provides the long-lived\n"
				     "/// means of communication with the remote peer."));
	GcDecl *extension = gc_extension(
		type_name,
		gc_vec(commentable_docs(protocol_docs, s->documentation, gc_client_protocol(access, "ClientProtocol", s, namer)),
		       commentable_docs(client_docs, s->documentation,
					gc_client_struct(access, "Client", type_name, "ClientProtocol", s, namer)),
		       NULL));
	vec_push(blocks, gc_block(NULL, gc_guarded(availability, extension), NULL));

	const char *client_protocol = xcat(type_name, ".ClientProtocol");
	GcDecl *defaults = gc_client_method_signature_with_defaults(access, client_protocol, s, namer, request);
	vec_push(blocks, gc_block(gc_comment(GC_INLINE, STR_LIT("Helpers providing default arguments to 'ClientProtocol' methods.")),
				  gc_guarded(availability, defaults), NULL));
	GcDecl *exploded = gc_exploded_client_methods(access, client_protocol, s, namer);
	vec_push(blocks, gc_block(gc_comment(GC_INLINE, STR_LIT("Helpers providing sugared APIs for 'ClientProtocol' methods.")),
				  gc_guarded(availability, exploded), NULL));
}

/* ---- CodeGenError.swift ---- */

/* CodeGenError.description: "\(code): \"\(message)\"", where the Code struct prints its private
 * enum through a context descriptor whose runtime address Swift shows ("$55eb92e088b8"), which
 * differs per run: the port shows "$0". */
static NORETURN COLD void code_gen_error(const char *code, const char *message)
{
	fail("Code(value: GRPCCodeGen.CodeGenError.Code.(unknown context at $0).Value.%s): \"%s\"", code, message);
}

/* ---- IDLToStructuredSwiftTranslator.swift ---- */

/* [String].getFirstDuplicate() over the name at offset off of each method */
static const char *first_duplicate(const GcService *s, size_t off)
{
	Map seen = {0};
	for (int i = 0; i < s->n_methods; i++) {
		const char *name = *(const char *const *)((const char *)&s->methods[i].name + off);
		if (map_has(&seen, name))
			return name;
		map_put(&seen, name, (void *)name);
	}
	return NULL;
}

static void validate_input(const GcRequest *r)
{
	/* checkServiceDescriptorsAreUnique */
	Map names = {0};
	for (int i = 0; i < r->n_services; i++) {
		const char *name = r->services[i].name.identifying_name;
		if (map_has(&names, name))
			code_gen_error("nonUniqueServiceName",
				       xcat("Services must have unique descriptors. ", name,
					    " is the descriptor of at least two different services."));
		map_put(&names, name, (void *)name);
	}
	/* checkServiceEnumNamesAreUnique iterates a Dictionary (random order) of the services
	 * grouped by typeName and reports the first group with more than one service. */
	Map type_names = {0};
	for (int i = 0; i < r->n_services; i++) {
		const char *name = r->services[i].name.type_name;
		if (map_has(&type_names, name))
			code_gen_error("nonUniqueServiceName",
				       xcat("There must be a unique (namespace, service_name) pair for each service. ", name,
					    " is used as a <namespace>_<service_name> construction for multiple services."));
		map_put(&type_names, name, (void *)name);
	}
	for (int i = 0; i < r->n_services; i++) {
		const GcService *s = &r->services[i];
		const char *service = s->name.identifying_name, *d;
		if ((d = first_duplicate(s, offsetof(GcMethodName, identifying_name))))
			code_gen_error("nonUniqueMethodName",
				       xcat("Methods of a service must have unique base names. ", d,
					    " is used as a base name for multiple methods of the ", service, " service."));
		if ((d = first_duplicate(s, offsetof(GcMethodName, type_name))))
			code_gen_error("nonUniqueMethodName",
				       xcat("Methods of a service must have unique generated upper case names. ", d,
					    " is used as a generated upper case name for multiple methods of the ", service,
					    " service."));
		if ((d = first_duplicate(s, offsetof(GcMethodName, function_name))))
			code_gen_error("nonUniqueMethodName",
				       xcat("Methods of a service must have unique lower case names. ", d,
					    " is used as a signature name for multiple methods of the ", service, " service."));
	}
}

static GcAccess access_modifier(GcAccessLevel level)
{
	static const GcAccess levels[] = {GC_INTERNAL, GC_PUBLIC, GC_PACKAGE};
	return levels[level];
}

COLD GcConfig gc_config(GcAccessLevel access_level, bool access_level_on_imports, bool client, bool server, int indentation)
{
	return (GcConfig){.access_level = access_level, .access_level_on_imports = access_level_on_imports,
			  .indentation = indentation, .client = client, .server = server, .grpc_core_module_name = "GRPCCore"};
}

/* CodeGenerator.generate: IDLToStructuredSwiftTranslator.translate, then TextBasedRenderer. */
COLD Str gc_generate(const GcConfig *config, const GcRequest *r)
{
	GcAvailability custom = {config->platforms, config->n_platforms};
	const GcAvailability *availability = config->custom_availability ? &custom : &gc_macos15_aligned;
	validate_input(r);
	GcAccess access = access_modifier(config->access_level);
	GcNamer namer = {config->grpc_core_module_name};
	GcFile file = {0};
	for (int i = 0; i < r->n_services; i++) {
		const GcService *s = &r->services[i];
		const char *service = s->name.identifying_name;
		vec_push(&file.code_blocks, gc_block(gc_comment(GC_MARK_SECTION, str_c(service)), NULL, NULL));
		gc_service_metadata(&file.code_blocks, access, s, availability, &namer);
		if (config->server) {
			vec_push(&file.code_blocks, gc_block(gc_comment(GC_MARK, str_c(xcat(service, " (server)"))), NULL, NULL));
			translate_server(&file.code_blocks, access, s, availability, &namer, r);
		}
		if (config->client) {
			vec_push(&file.code_blocks, gc_block(gc_comment(GC_MARK, str_c(xcat(service, " (client)"))), NULL, NULL));
			translate_client(&file.code_blocks, access, s, availability, &namer, r);
		}
	}
	if (!r->n_services) {
		vec_push(&file.code_blocks, gc_block(gc_comment(GC_INLINE, STR_LIT("This file contained no services.")), NULL, NULL));
	} else {
		/* makeImports: GRPCCore, then the dependencies */
		GcAccess import_access = config->access_level_on_imports ? access : GC_NONE;
		file.n_imports = r->n_dependencies + 1;
		file.imports = xalloc(sizeof(GcImport) * (size_t)file.n_imports);
		file.imports[0] = (GcImport){import_access, config->grpc_core_module_name};
		for (int i = 0; i < r->n_dependencies; i++)
			file.imports[i + 1] = (GcImport){
				config->access_level_on_imports ? access_modifier(r->dependencies[i].access_level) : GC_NONE,
				r->dependencies[i].module};
	}
	file.top_comment = gc_comment(GC_PRE_FORMATTED, r->leading_trivia);
	/* NamedFileDescription(name: String(fileName.split(separator: ".")[0])): the name is not
	 * used, but a file name without a character other than "." traps (index out of range). */
	if (!r->file_name[strspn(r->file_name, ".")])
		fail("Fatal error: Index out of range");
	return gc_render(&file, config->indentation);
}
