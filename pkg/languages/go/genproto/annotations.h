/* Copyright 2025 Google LLC. Licensed under the Apache License, Version 2.0 (see LICENSE of
 * google.golang.org/genproto). */

/* Port of google.golang.org/genproto/googleapis/api/annotations (v0.0.0-20250811160224): the
 * extensions it registers in protoregistry.GlobalTypes (google/api/annotations.proto, client.proto,
 * field_behavior.proto, field_info.proto, resource.proto, routing.proto) and the messages of
 * google/api/http.proto as compiled into Go (protobuf-go semantics, pkg/languages/go/protodesc). */
#ifndef GENPROTO_ANNOTATIONS_H
#define GENPROTO_ANNOTATIONS_H

#include "protodesc.h"

/* Extension field numbers. */
enum {
	GENPROTO_E_HTTP = 72295728,          /* MethodOptions: HttpRule */
	GENPROTO_E_ROUTING = 72295729,       /* MethodOptions: RoutingRule */
	GENPROTO_E_METHOD_SIGNATURE = 1051,  /* MethodOptions: repeated string */
	GENPROTO_E_DEFAULT_HOST = 1049,      /* ServiceOptions: string */
	GENPROTO_E_OAUTH_SCOPES = 1050,      /* ServiceOptions: string */
	GENPROTO_E_API_VERSION = 525000001,  /* ServiceOptions: string */
	GENPROTO_E_FIELD_BEHAVIOR = 1052,    /* FieldOptions: repeated FieldBehavior */
	GENPROTO_E_FIELD_INFO = 291403980,   /* FieldOptions: FieldInfo */
	GENPROTO_E_RESOURCE_REFERENCE = 1055, /* FieldOptions: ResourceReference */
	GENPROTO_E_RESOURCE_DEFINITION = 1053, /* FileOptions: repeated ResourceDescriptor */
	GENPROTO_E_RESOURCE = 1053,          /* MessageOptions: ResourceDescriptor */
};

/* The full names of these extensions (NULL-terminated). */
extern const char *const genproto_annotations_extensions[];

/* google.golang.org/genproto/googleapis/api/visibility (google/api/visibility.proto): the
 * google.api.VisibilityRule extensions, all with the same number (on Enum, EnumValue, Field,
 * Message, Method and Service options), and the fields of VisibilityRule (proto3). */
enum { GENPROTO_E_VISIBILITY = 72295727, GENPROTO_VisibilityRule_selector = 1, GENPROTO_VisibilityRule_restriction = 2 };
extern const char *const genproto_visibility_extensions[]; /* NULL-terminated */

/* google.api.FieldBehavior */
enum {
	GENPROTO_FIELD_BEHAVIOR_UNSPECIFIED = 0,
	GENPROTO_FIELD_BEHAVIOR_OPTIONAL = 1,
	GENPROTO_FIELD_BEHAVIOR_REQUIRED = 2,
	GENPROTO_FIELD_BEHAVIOR_OUTPUT_ONLY = 3,
	GENPROTO_FIELD_BEHAVIOR_INPUT_ONLY = 4,
	GENPROTO_FIELD_BEHAVIOR_IMMUTABLE = 5,
	GENPROTO_FIELD_BEHAVIOR_UNORDERED_LIST = 6,
	GENPROTO_FIELD_BEHAVIOR_NON_EMPTY_DEFAULT = 7,
	GENPROTO_FIELD_BEHAVIOR_IDENTIFIER = 8,
};

/* google.api.HttpRule and google.api.CustomHttpPattern field numbers. */
enum {
	GENPROTO_HttpRule_selector = 1,
	GENPROTO_HttpRule_get = 2,
	GENPROTO_HttpRule_put = 3,
	GENPROTO_HttpRule_post = 4,
	GENPROTO_HttpRule_delete = 5,
	GENPROTO_HttpRule_patch = 6,
	GENPROTO_HttpRule_body = 7,
	GENPROTO_HttpRule_custom = 8,
	GENPROTO_HttpRule_additional_bindings = 11,
	GENPROTO_HttpRule_response_body = 12,
	GENPROTO_CustomHttpPattern_kind = 1,
	GENPROTO_CustomHttpPattern_path = 2,
};

/* The definition of google.api.HttpRule (built on the first call after arena_reset; call
 * genproto_annotations_init once per request). */
void genproto_annotations_init(void);
const GoMsgDef *genproto_http_rule(void);

/* proto.GetExtension of an extension linked into the plugin, read from the unknown fields of
 * an options message (protogen leaves the extensions of protoregistry.GlobalTypes there):
 *   - genproto_ext_message: the concatenated payloads of the field (a message extension merges
 *     all its occurrences); *present is false if the field does not occur;
 *   - genproto_ext_string: the last occurrence;
 *   - genproto_ext_enums: the values of a repeated enum (packed and unpacked).
 * Occurrences with another wire type are unknown fields to protobuf-go and are skipped. */
Str genproto_ext_message(const PbMsg *opts, uint32_t num, bool *present);
Str genproto_ext_string(const PbMsg *opts, uint32_t num);
int genproto_ext_enums(const PbMsg *opts, uint32_t num, int32_t **out);

#endif
