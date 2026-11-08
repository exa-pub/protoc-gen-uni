/*
 * Copyright 2024, gRPC Authors All rights reserved.
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

/* Port of Sources/GRPCProtobufCodeGen (CamelCaser.swift, ProtobufCodeGenParser.swift,
 * ProtobufCodeGenerator.swift): the FileDescriptor to CodeGenerationRequest translation of the
 * Protobuf flavor of gRPC Swift 2. Names are ASCII (see swift_protobuf.h). */

/* CamelCaser.toLowerCamelCase: "ImportCSV" -> "importCSV", "FOOBARImportCSV" -> "foobarImportCSV" */
static char *to_lower_camel_case(const char *input)
{
	size_t i = 0;
	while (input[i] && !(input[i] >= 'a' && input[i] <= 'z'))
		i++;
	if (!input[i])
		return sw_ascii_lower(input);
	if (i == 0)
		return xstrdup(input);
	if (i == 1)
		return xcat(sw_ascii_lower(xstrndup(input, 1)), input + 1);
	/* The last upper case character before the first lower case one starts the next word. */
	return xcat(sw_ascii_lower(xstrndup(input, i - 1)), input + i - 1);
}

/* SwiftProtobufNamer.typePrefixProperty(file:): "Foo_Bar_" -> "foo_bar_" */
static char *type_prefix_property(SwNamer *namer, const SwFile *file)
{
	const char *type_prefix = sw_namer_type_prefix(namer, file);
	Vec parts = str_split(type_prefix, '_'), lowercased = {0};
	for (int i = 0; i < parts.n; i++)
		if (*(char *)parts.v[i])
			vec_push(&lowercased, sw_to_lower_camel(parts.v[i]));
	char *joined = str_join(&lowercased, "_");
	/* Add the trailing "_" if it was dropped. */
	if (has_suffix(type_prefix, "_") && !has_suffix(joined, "_"))
		return xcat(joined, "_");
	return joined;
}

/* FileDescriptor.header: the comments of the syntax statement */
static Str file_header(SwFile *file)
{
	static const int32_t syntax_path[] = {12};
	PbMsg *location = sw_file_location(file, syntax_path, 1);
	return location ? sw_as_source_comment(location, "///", "//") : STR_LIT("");
}

static char *make_serializer(void *ctx, const char *message_type)
{
	return xcat(((const ModuleNames *)ctx)->grpc_protobuf, ".ProtobufSerializer<", message_type, ">()");
}

static char *make_deserializer(void *ctx, const char *message_type)
{
	return xcat(((const ModuleNames *)ctx)->grpc_protobuf, ".ProtobufDeserializer<", message_type, ">()");
}

static int compare_strings(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* codeDependencies(file:) */
static void code_dependencies(GcRequest *r, const Options *o, SwFile *file)
{
	if (!file->services.n)
		return;
	GcAccessLevel access = o->access_level;
	r->dependencies = xalloc(sizeof(GcDependency) * (size_t)(o->extra_module_imports.n + 2));
	r->dependencies[r->n_dependencies++] = (GcDependency){o->module_names.grpc_protobuf, GC_LEVEL_INTERNAL};
	/* If there's a dependency on a bundled proto then add the SwiftProtobuf import (only needed
	 * if a bundled proto is the input or output type of an RPC). */
	bool needs_protobuf_import = false;
	for (int i = 0; i < file->services.n; i++) {
		const SwService *s = file->services.v[i];
		for (int j = 0; j < s->methods.n; j++) {
			const SwMethod *m = s->methods.v[j];
			if (sw_is_bundled_proto(m->input_type->file) || sw_is_bundled_proto(m->output_type->file))
				needs_protobuf_import = true;
		}
	}
	if (needs_protobuf_import)
		r->dependencies[r->n_dependencies++] = (GcDependency){o->module_names.swift_protobuf, access};
	/* protoToModuleMappings.neededModules(forFile:) is nil: the mappings never have entries
	 * (loading a mappings file fails, see options.c). */
	/* The extra imports, sorted (Swift string order is byte order for ASCII). */
	const char **extra = xalloc(sizeof(char *) * (size_t)(o->extra_module_imports.n + 1));
	for (int i = 0; i < o->extra_module_imports.n; i++) {
		extra[i] = o->extra_module_imports.v[i];
		if (o->extra_module_imports.n > 1)
			sw_ascii_name(str_c(extra[i]), "parameter ExtraModuleImports (sorted by Swift string order)");
	}
	qsort(extra, (size_t)o->extra_module_imports.n, sizeof *extra, compare_strings);
	for (int i = 0; i < o->extra_module_imports.n; i++)
		r->dependencies[r->n_dependencies++] = (GcDependency){extra[i], access};
}

/* ProtobufCodeGenParser.parse(descriptor:) */
static GcRequest *parse(const Options *o, SwFile *file)
{
	SwNamer *namer = sw_namer(file, o->proto_to_module_mappings);
	GcRequest *r = xalloc(sizeof *r);
	Buf trivia = {0};
	Str header = file_header(file);
	buf_add_str(&trivia, header);
	/* Ensuring there is a blank line after the header. */
	if (header.n && !str_has_suffix(header, STR_LIT("\n\n")))
		buf_addc(&trivia, '\n');
	buf_adds(&trivia, "// DO NOT EDIT.\n"
			  "// swift-format-ignore-file\n"
			  "// swiftlint:disable all\n"
			  "//\n"
			  "// Generated by the gRPC Swift generator plugin for the protocol buffer compiler.\n"
			  "// Source: ");
	buf_adds(&trivia, file->name);
	buf_adds(&trivia, "\n"
			  "//\n"
			  "// For information on using the generated types, please see the documentation:\n"
			  "//   https://github.com/grpc/grpc-swift-2\n");
	r->file_name = file->name;
	r->leading_trivia = buf_view(&trivia);

	r->n_services = file->services.n;
	r->services = xalloc(sizeof(GcService) * (size_t)(r->n_services + 1));
	const char *type_prefix = sw_namer_type_prefix(namer, file);
	const char *property_prefix = r->n_services ? type_prefix_property(namer, file) : "";
	for (int i = 0; i < r->n_services; i++) {
		const SwService *s = file->services.v[i];
		GcService *gs = &r->services[i];
		gs->name = (GcServiceName){s->full_name, xcat(type_prefix, s->name), xcat(property_prefix, s->name)};
		gs->documentation = sw_proto_source_comments(SW_SERVICE, s, "///", NULL);
		gs->n_methods = s->methods.n;
		gs->methods = xalloc(sizeof(GcMethod) * (size_t)(gs->n_methods + 1));
		for (int j = 0; j < gs->n_methods; j++) {
			const SwMethod *m = s->methods.v[j];
			gs->methods[j] = (GcMethod){
				.documentation = sw_proto_source_comments(SW_METHOD, m, "///", NULL),
				.name = {m->name, m->name, to_lower_camel_case(m->name)},
				.is_input_streaming = m->client_streaming,
				.is_output_streaming = m->server_streaming,
				.input_type = sw_full_name_message(namer, m->input_type),
				.output_type = sw_full_name_message(namer, m->output_type),
			};
		}
	}
	code_dependencies(r, o, file);
	r->make_serializer = make_serializer;
	r->make_deserializer = make_deserializer;
	r->ctx = (void *)&o->module_names;
	return r;
}

/* ProtobufCodeGenerator.generateCode(fileDescriptor:protoFileModuleMappings:extraModuleImports:
 * availabilityOverrides:) */
static Str generate_code(const Options *o, SwFile *file)
{
	GcConfig config = gc_config(o->access_level, o->access_level_on_imports, o->generate_client, o->generate_server,
				    o->indentation);
	config.grpc_core_module_name = o->module_names.grpc_core;
	config.custom_availability = o->n_availability_overrides > 0;
	config.platforms = o->availability_overrides;
	config.n_platforms = o->n_availability_overrides;
	return gc_generate(&config, parse(o, file));
}
