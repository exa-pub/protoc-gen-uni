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

/* Port of Internal/StructuredSwift+ServiceMetadata.swift (grpc-swift-2 2.4.4). */
#include "structured_swift.h"

static const GcPlatform macos15_aligned[] = {
	{"macOS", "15.0"}, {"iOS", "18.0"}, {"watchOS", "11.0"}, {"tvOS", "18.0"}, {"visionOS", "2.0"},
};
const GcAvailability gc_macos15_aligned = {macos15_aligned, 5};

/* GRPCCore.ServiceDescriptor(fullyQualifiedService: "<name>") */
static GcExpr *service_descriptor_call(const char *name, const GcNamer *namer)
{
	return gc_call(gc_identifier_type(gc_grpc_core(namer, "ServiceDescriptor")),
		       gc_vec(gc_arg("fullyQualifiedService", gc_string(name)), NULL));
}

/* enum <Method>: Sendable { typealias Input; typealias Output; static let descriptor } */
static GcDecl *method_namespace(GcAccess access, const GcMethod *m, const char *service, const GcNamer *namer)
{
	static const char *const rpc_types[] = {"unary", "serverStreaming", "clientStreaming", "bidirectionalStreaming"};
	const char *literal = m->name.identifying_name;
	GcDecl *input = gc_decl(GD_TYPEALIAS), *output = gc_decl(GD_TYPEALIAS);
	input->access = output->access = access;
	input->name = "Input";
	input->type = gc_member(m->input_type);
	output->name = "Output";
	output->type = gc_member(m->output_type);
	const char *rpc_type = rpc_types[m->is_input_streaming * 2 + m->is_output_streaming];
	GcExpr *descriptor = gc_call(gc_identifier_type(gc_grpc_core(namer, "MethodDescriptor")),
				     gc_vec(gc_arg("service", service_descriptor_call(service, namer)),
					    gc_arg("method", gc_string(literal)), gc_arg("type", gc_dot(NULL, rpc_type)), NULL));
	GcDecl *e = gc_decl(GD_ENUM);
	e->access = access;
	e->name = m->name.type_name;
	e->conformances = gc_vec("Sendable", NULL);
	e->members = gc_vec(gc_commentable(gc_doc(xcat("Request type for \"", literal, "\".")), input),
			    gc_commentable(gc_doc(xcat("Response type for \"", literal, "\".")), output),
			    gc_commentable(gc_doc(xcat("Descriptor for \"", literal, "\".")),
					   gc_variable(access, true, "descriptor", NULL, descriptor)),
			    NULL);
	return e;
}

/* enum Method: Sendable { <method namespaces>; static let descriptors } */
static GcDecl *methods_namespace(GcAccess access, const GcService *s, const GcNamer *namer)
{
	const char *service = s->name.identifying_name;
	GcDecl *e = gc_decl(GD_ENUM);
	e->access = access;
	e->name = "Method";
	e->conformances = gc_vec("Sendable", NULL);
	Vec names = {0};
	for (int i = 0; i < s->n_methods; i++) {
		const GcMethod *m = &s->methods[i];
		vec_push(&e->members, gc_commentable(gc_doc(xcat("Namespace for \"", m->name.type_name, "\" metadata.")),
						     method_namespace(access, m, service, namer)));
		vec_push(&names, gc_dot(gc_pattern(m->name.type_name), "descriptor"));
	}
	GcDecl *descriptors = gc_variable(access, true, "descriptors",
					  gc_type(GT_ARRAY, gc_grpc_core(namer, "MethodDescriptor")), gc_array(names));
	vec_push(&e->members,
		 gc_commentable(gc_doc(xcat("Descriptors for all methods in the \"", service, "\" service.")), descriptors));
	return e;
}

/* enum <Name>: Sendable { static let descriptor; enum Method } */
static GcDecl *service_namespace(GcAccess access, const GcService *s, const GcNamer *namer)
{
	const char *service = s->name.identifying_name;
	GcDecl *e = gc_decl(GD_ENUM);
	e->access = access;
	e->name = s->name.type_name;
	e->conformances = gc_vec("Sendable", NULL);
	e->members = gc_vec(gc_commentable(gc_doc(xcat("Service descriptor for the \"", service, "\" service.")),
					   gc_variable(access, true, "descriptor", NULL, service_descriptor_call(service, namer))),
			    gc_commentable(gc_doc("Namespace for method metadata."), methods_namespace(access, s, namer)),
			    NULL);
	return e;
}

/* [CodeBlock].serviceMetadata: the service namespace and the GRPCCore.ServiceDescriptor
 * extension. */
COLD void gc_service_metadata(Vec *blocks, GcAccess access, const GcService *s, const GcAvailability *availability,
			      const GcNamer *namer)
{
	const char *service = s->name.identifying_name;
	vec_push(blocks, gc_block(gc_doc(xcat("Namespace containing generated types for the \"", service, "\" service.")),
				  gc_guarded(availability, service_namespace(access, s, namer)), NULL));
	GcDecl *property = gc_variable(access, true, s->name.property_name, NULL, service_descriptor_call(service, namer));
	GcDecl *extension = gc_extension(
		gc_literal_namespaced_type(namer, "ServiceDescriptor"),
		gc_vec(gc_commentable(gc_doc(xcat("Service descriptor for the \"", service, "\" service.")), property), NULL));
	vec_push(blocks, gc_block(NULL, gc_guarded(availability, extension), NULL));
}
