/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of protoc-gen-openapiv2/options of grpc-gateway v2.33.0 (openapiv2.pb.go, annotations.pb.go
 * as compiled into Go): the message tables of the openapiv2_* extensions, with the messages of
 * google/protobuf/struct.proto they use (types/known/structpb), for proto.Unmarshal with
 * protobuf-go semantics (pkg/languages/go/protodesc: proto3 strings are UTF-8, maps keep the last
 * value of a key when read with gw_options_map). */
#include "grpcgateway.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	uint32_t num;
	uint8_t type, label;
	int8_t msg;   /* GW_OA_* of a message field or map entry, -1 */
	uint8_t kind; /* 0, 'm' map, 'o' in the oneof */
	const char *name;
} Spec;

#define S(n, name) {n, PT_STRING, PL_OPT, -1, 0, name}
#define RS(n, name) {n, PT_STRING, PL_REP, -1, 0, name}
#define B(n, name) {n, PT_BOOL, PL_OPT, -1, 0, name}
#define D(n, name) {n, PT_DOUBLE, PL_OPT, -1, 0, name}
#define U(n, name) {n, PT_UINT64, PL_OPT, -1, 0, name}
#define E(n, name) {n, PT_ENUM, PL_OPT, -1, 0, name}
#define RE(n, name) {n, PT_ENUM, PL_PACKED, -1, 0, name}
#define M(n, m, name) {n, PT_MESSAGE, PL_OPT, GW_OA_##m, 0, name}
#define RM(n, m, name) {n, PT_MESSAGE, PL_REP, GW_OA_##m, 0, name}
#define MAP(n, e, name) {n, PT_MESSAGE, PL_REP, GW_OA_##e, 'm', name}

static const Spec s_Swagger[] = {
	S(1, "swagger"), M(2, Info, "info"), S(3, "host"), S(4, "base_path"), RE(5, "schemes"), RS(6, "consumes"),
	RS(7, "produces"), MAP(10, MapResponse, "responses"), M(11, SecurityDefinitions, "security_definitions"),
	RM(12, SecurityRequirement, "security"), RM(13, Tag, "tags"), M(14, ExternalDocumentation, "external_docs"),
	MAP(15, MapValue, "extensions"),
};
static const Spec s_Operation[] = {
	RS(1, "tags"), S(2, "summary"), S(3, "description"), M(4, ExternalDocumentation, "external_docs"),
	S(5, "operation_id"), RS(6, "consumes"), RS(7, "produces"), MAP(9, MapResponse, "responses"),
	RE(10, "schemes"), B(11, "deprecated"), RM(12, SecurityRequirement, "security"), MAP(13, MapValue, "extensions"),
	M(14, Parameters, "parameters"),
};
static const Spec s_Parameters[] = {RM(1, HeaderParameter, "headers")};
static const Spec s_HeaderParameter[] = {
	S(1, "name"), S(2, "description"), E(3, "type"), S(4, "format"), B(5, "required"),
};
static const Spec s_Header[] = {S(1, "description"), S(2, "type"), S(3, "format"), S(6, "default"), S(13, "pattern")};
static const Spec s_Response[] = {
	S(1, "description"), M(2, Schema, "schema"), MAP(3, MapHeader, "headers"), MAP(4, MapString, "examples"),
	MAP(5, MapValue, "extensions"),
};
static const Spec s_Info[] = {
	S(1, "title"), S(2, "description"), S(3, "terms_of_service"), M(4, Contact, "contact"), M(5, License, "license"),
	S(6, "version"), MAP(7, MapValue, "extensions"),
};
static const Spec s_Contact[] = {S(1, "name"), S(2, "url"), S(3, "email")};
static const Spec s_License[] = {S(1, "name"), S(2, "url")};
static const Spec s_ExternalDocumentation[] = {S(1, "description"), S(2, "url")};
static const Spec s_Schema[] = {
	M(1, JSONSchema, "json_schema"), S(2, "discriminator"), B(3, "read_only"),
	M(5, ExternalDocumentation, "external_docs"), S(6, "example"),
};
static const Spec s_EnumSchema[] = {
	S(1, "description"), S(2, "default"), S(3, "title"), B(4, "required"), B(5, "read_only"),
	M(6, ExternalDocumentation, "external_docs"), S(7, "example"), S(8, "ref"), MAP(9, MapValue, "extensions"),
};
static const Spec s_JSONSchema[] = {
	S(3, "ref"), S(5, "title"), S(6, "description"), S(7, "default"), B(8, "read_only"), S(9, "example"),
	D(10, "multiple_of"), D(11, "maximum"), B(12, "exclusive_maximum"), D(13, "minimum"), B(14, "exclusive_minimum"),
	U(15, "max_length"), U(16, "min_length"), S(17, "pattern"), U(20, "max_items"), U(21, "min_items"),
	B(22, "unique_items"), U(24, "max_properties"), U(25, "min_properties"), RS(26, "required"), RS(34, "array"),
	RE(35, "type"), S(36, "format"), RS(46, "enum"), MAP(48, MapValue, "extensions"),
	M(1001, FieldConfiguration, "field_configuration"),
};
static const Spec s_FieldConfiguration[] = {S(47, "path_param_name"), B(49, "deprecated")};
static const Spec s_Tag[] = {
	S(1, "name"), S(2, "description"), M(3, ExternalDocumentation, "external_docs"), MAP(4, MapValue, "extensions"),
};
static const Spec s_SecurityDefinitions[] = {MAP(1, MapSecurityScheme, "security")};
static const Spec s_SecurityScheme[] = {
	E(1, "type"), S(2, "description"), S(3, "name"), E(4, "in"), E(5, "flow"), S(6, "authorization_url"),
	S(7, "token_url"), M(8, Scopes, "scopes"), MAP(9, MapValue, "extensions"),
};
static const Spec s_SecurityRequirement[] = {MAP(1, MapSecurityRequirementValue, "security_requirement")};
static const Spec s_SecurityRequirementValue[] = {RS(1, "scope")};
static const Spec s_Scopes[] = {MAP(1, MapString, "scope")};
static const Spec s_MapResponse[] = {S(1, "key"), M(2, Response, "value")};
static const Spec s_MapValue[] = {S(1, "key"), M(2, Value, "value")};
static const Spec s_MapHeader[] = {S(1, "key"), M(2, Header, "value")};
static const Spec s_MapString[] = {S(1, "key"), S(2, "value")};
static const Spec s_MapSecurityScheme[] = {S(1, "key"), M(2, SecurityScheme, "value")};
static const Spec s_MapSecurityRequirementValue[] = {S(1, "key"), M(2, SecurityRequirementValue, "value")};
static const Spec s_Value[] = {
	{1, PT_ENUM, PL_OPT, -1, 'o', "null_value"}, {2, PT_DOUBLE, PL_OPT, -1, 'o', "number_value"},
	{3, PT_STRING, PL_OPT, -1, 'o', "string_value"}, {4, PT_BOOL, PL_OPT, -1, 'o', "bool_value"},
	{5, PT_MESSAGE, PL_OPT, GW_OA_Struct, 'o', "struct_value"}, {6, PT_MESSAGE, PL_OPT, GW_OA_ListValue, 'o', "list_value"},
};
static const Spec s_Struct[] = {MAP(1, MapValue, "fields")};
static const Spec s_ListValue[] = {RM(1, Value, "values")};

#define PKG "grpc.gateway.protoc_gen_openapiv2.options."
#define MSG(name, full) {full, s_##name, (int)(sizeof s_##name / sizeof *s_##name)}
static const struct {
	const char *full_name;
	const Spec *f;
	int n;
} msgs[GW_OA_COUNT] = {
	MSG(Swagger, PKG "Swagger"),
	MSG(Operation, PKG "Operation"),
	MSG(Parameters, PKG "Parameters"),
	MSG(HeaderParameter, PKG "HeaderParameter"),
	MSG(Header, PKG "Header"),
	MSG(Response, PKG "Response"),
	MSG(Info, PKG "Info"),
	MSG(Contact, PKG "Contact"),
	MSG(License, PKG "License"),
	MSG(ExternalDocumentation, PKG "ExternalDocumentation"),
	MSG(Schema, PKG "Schema"),
	MSG(EnumSchema, PKG "EnumSchema"),
	MSG(JSONSchema, PKG "JSONSchema"),
	MSG(FieldConfiguration, PKG "JSONSchema.FieldConfiguration"),
	MSG(Tag, PKG "Tag"),
	MSG(SecurityDefinitions, PKG "SecurityDefinitions"),
	MSG(SecurityScheme, PKG "SecurityScheme"),
	MSG(SecurityRequirement, PKG "SecurityRequirement"),
	MSG(SecurityRequirementValue, PKG "SecurityRequirement.SecurityRequirementValue"),
	MSG(Scopes, PKG "Scopes"),
	/* the map entries of the same key and value types share a table (full names only show in
	 * errors of the request, which upstream reports with a timestamp) */
	MSG(MapResponse, PKG "Swagger.ResponsesEntry"),
	MSG(MapValue, PKG "Swagger.ExtensionsEntry"),
	MSG(MapHeader, PKG "Response.HeadersEntry"),
	MSG(MapString, PKG "Response.ExamplesEntry"),
	MSG(MapSecurityScheme, PKG "SecurityDefinitions.SecurityEntry"),
	MSG(MapSecurityRequirementValue, PKG "SecurityRequirement.SecurityRequirementEntry"),
	MSG(Value, "google.protobuf.Value"),
	MSG(Struct, "google.protobuf.Struct"),
	MSG(ListValue, "google.protobuf.ListValue"),
};

static GoMsgDef *defs;

COLD void gw_options_init(void)
{
	defs = xalloc(sizeof *defs * GW_OA_COUNT);
	for (int m = 0; m < GW_OA_COUNT; m++) {
		int n = msgs[m].n;
		PbField *f = xalloc(sizeof *f * (size_t)n);
		uint8_t *flags = xalloc((size_t)n);
		int *oneof = xalloc(sizeof *oneof * (size_t)n);
		const char **names = xalloc(sizeof *names * (size_t)n);
		int *order = xalloc(sizeof *order * (size_t)n);
		for (int i = 0; i < n; i++) {
			const Spec *s = &msgs[m].f[i];
			f[i] = (PbField){s->num, s->type, s->label, s->msg >= 0 ? &defs[s->msg].pb : NULL, NULL, 0};
			if (s->type == PT_STRING)
				flags[i] |= GOF_UTF8; /* proto3 */
			if (s->kind == 'm')
				flags[i] |= GOF_MAP;
			if (s->kind == 'o')
				oneof[i] = 1;
			else if (s->label == PL_OPT && s->type != PT_MESSAGE)
				flags[i] |= GOF_IMPLICIT;
			names[i] = xcat(msgs[m].full_name, ".", s->name);
			order[i] = i;
		}
		defs[m] = (GoMsgDef){.pb = {n, f}, .full_name = msgs[m].full_name, .flags = flags, .oneof = oneof,
				     .names = names, .order = order};
	}
}

PbMsg *gw_options_decode(int msg, Str payload, const char **err)
{
	PbMsg *m = pb_new(&defs[msg].pb);
	*err = gopb_decode(m, (const uint8_t *)payload.p, payload.n);
	return *err ? NULL : m;
}

static int str_cmp(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	int c = n ? memcmp(a.p, b.p, n) : 0;
	return c ? c : (a.n > b.n) - (a.n < b.n);
}

static int cmp_entry(const void *x, const void *y)
{
	const GwMapEntry *a = x, *b = y;
	int c = str_cmp(a->key, b->key);
	return c ? c : (a->index > b->index) - (a->index < b->index);
}

int gw_options_map(const PbMsg *m, uint32_t num, GwMapEntry **out)
{
	const PbSlot *s = pb_slot(m, num);
	int n = s ? s->n : 0;
	GwMapEntry *v = xalloc(sizeof *v * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		v[i] = (GwMapEntry){pb_bytes(s->v[i].m, 1), s->v[i].m, i};
	qsort(v, (size_t)n, sizeof *v, cmp_entry);
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (i + 1 < n && str_equal(v[i].key, v[i + 1].key))
			continue; /* a later entry with the same key wins */
		v[k++] = v[i];
	}
	*out = v;
	return k;
}
