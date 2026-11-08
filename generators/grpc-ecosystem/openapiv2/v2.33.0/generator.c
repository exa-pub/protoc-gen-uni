// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
// github.com/grpc-ecosystem/grpc-gateway).

/* grpc-ecosystem/openapiv2 v2.33.0: a port of protoc-gen-openapiv2 v2.33.0
 * (buf.build/grpc-ecosystem/openapiv2) of grpc-gateway
 * (https://github.com/grpc-ecosystem/grpc-gateway/tree/v2.33.0/protoc-gen-openapiv2): main.go
 * (below), internal/genopenapi/generator.go (below), types.go (genopenapi.h, types.c),
 * template.go and helpers.go (template.c), naming.go (naming.c), format.go (below). The shared
 * packages (internal/descriptor, httprule, casing, codegenerator, the options package) are
 * pkg/languages/go/grpc-gateway; the request is loaded with compiler/protogen of protobuf-go
 * v1.36.12 (pkg/languages/go/protogen); encoding/json of Go 1.27.3 (jsonv2 engine) is in
 * pkg/languages/go/std, go.yaml.in/yaml/v3 in pkg/languages/go/yaml, protojson in
 * pkg/languages/go/protojson.
 *
 * Unity build: this file includes the others, and everything but the generator is static. */
#include "annotations.h"
#include "go_std.h"
#include "grpcgateway.h"
#include "protogen.h"
#include "protojson.h"
#include "uni.h"
#include "yaml.h"

#include <stdlib.h>
#include <string.h>

#define PROGRAM "protoc-gen-openapiv2" /* the binary at the root of the buf.build image */

#include "genopenapi.h"
#include "types.c"
#include "naming.c"
#include "template.c"

/* ---- format.go ---- */

/* Validate: "json" or "yaml" */
static bool format_valid(Str f) { return str_eqc(f, "json") || str_eqc(f, "yaml"); }

/* ---- generator.go ---- */

typedef struct {
	Str fileName;
	openapiSwaggerObject *swagger;
} wrapper;

typedef struct {
	Str Name, Content;
} ResponseFile;

/* x/text cases.Title(language.AmericanEnglish) of an ASCII extension key, with "-" replaced by
 * "_": the field name extensionMarshalJSON gives the extension in its reflect.StructOf. Letters
 * start a word after a character that is not a letter, digit or '_'. NULL if the name is not a
 * valid identifier (StructOf panics) or not ASCII (Unicode title casing is not ported). */
static const char *extension_field_name(Str k)
{
	char *t = xalloc(k.n + 1);
	bool mid = false;
	for (size_t i = 0; i < k.n; i++) {
		char c = k.p[i];
		bool upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z';
		if (upper || lower) {
			if (!mid) {
				c = (char)(lower ? c - 32 : c);
				mid = true;
			} else if (upper) {
				c = (char)(c + 32);
			}
		} else if (c == '-') {
			c = '_';
			mid = false;
		} else if (!((c >= '0' && c <= '9') || c == '_')) {
			return NULL;
		}
		t[i] = c;
	}
	return t;
}

/* The checks of reflect.StructOf in extensionMarshalJSON: valid and distinct field names. */
static void check_extension_fields(const Extensions *ext)
{
	Map seen = {0};
	for (int i = 0; ext && i < ext->n; i++) {
		const char *name = extension_field_name(ext->v[i].key);
		if (!name)
			fail("unsupported by protoc-gen-uni: the extension key %s (encoding it to JSON panics upstream, or it is "
			     "not ASCII)",
			     go_quote(ext->v[i].key).p);
		if (map_has(&seen, name))
			go_panic(xcat("reflect.StructOf: duplicate field ", name));
		map_put(&seen, name, (void *)name);
	}
}

static void check_schema_extensions(const openapiSchemaObject *s, int depth);

static void check_extensions_of(const openapiSwaggerObject *s)
{
	check_extension_fields(&s->extensions);
	check_extension_fields(&s->Info.extensions);
	for (int i = 0; i < s->Tags.n; i++)
		check_extension_fields(&s->Tags.v[i].extensions);
	for (int i = 0; s->SecurityDefinitions && i < s->SecurityDefinitions->n; i++)
		check_extension_fields(&s->SecurityDefinitions->v[i].value.extensions);
	for (int i = 0; i < s->Definitions->n; i++)
		check_schema_extensions(&s->Definitions->v[i].value, 0);
	for (int i = 0; i < s->Paths.n; i++) {
		const openapiPathItemObject *p = &s->Paths.v[i].PathItemObject;
		const openapiOperationObject *ops[] = {p->Get, p->Delete, p->Post, p->Put, p->Patch, p->Head, p->Options};
		for (size_t o = 0; o < sizeof ops / sizeof *ops; o++) {
			if (!ops[o])
				continue;
			check_extension_fields(&ops[o]->extensions);
			for (int k = 0; k < ops[o]->Parameters.n; k++) {
				check_extension_fields(&ops[o]->Parameters.v[k].extensions);
				if (ops[o]->Parameters.v[k].Schema)
					check_schema_extensions(ops[o]->Parameters.v[k].Schema, 0);
			}
			for (int k = 0; ops[o]->Responses && k < ops[o]->Responses->n; k++) {
				check_extension_fields(&ops[o]->Responses->v[k].value.extensions);
				check_schema_extensions(&ops[o]->Responses->v[k].value.Schema, 0);
			}
		}
	}
}

static void check_schema_extensions(const openapiSchemaObject *s, int depth)
{
	if (depth > 10000)
		return;
	check_extension_fields(&s->extensions);
	for (int i = 0; s->Properties && i < s->Properties->n; i++)
		check_schema_extensions(s->Properties->v[i].Value, depth + 1);
	if (s->addKind == ADDPROP_SCHEMA)
		check_schema_extensions(s->AdditionalProperties, depth + 1);
}

/* filepath.Ext */
static Str file_ext(Str name)
{
	for (size_t i = name.n; i > 0 && name.p[i - 1] != '/'; i--)
		if (name.p[i - 1] == '.')
			return (Str){name.p + i - 1, name.n - i + 1};
	return (Str){"", 0};
}

/* encodeOpenAPI converts OpenAPI file obj to a response file */
static ResponseFile encodeOpenAPI(const wrapper *file, Str format)
{
	Buf content = {0};
	if (str_eqc(format, "yaml")) {
		yaml_swagger(&content, file->swagger);
	} else {
		check_extensions_of(file->swagger);
		json_swagger(&content, file->swagger);
	}
	Str name = file->fileName;
	Str ext = file_ext(name);
	Str base = {name.p, name.n - ext.n};
	return (ResponseFile){cat3(base, S(".swagger."), format), buf_view(&content)};
}

/* Merge a lot of OpenAPI file (wrapper) to single one OpenAPI file */
static wrapper *mergeTargetFile(wrapper **targets, int n, Str mergeFileName)
{
	wrapper *mergedTarget = NULL;
	for (int i = 0; i < n; i++) {
		wrapper *f = targets[i];
		if (!mergedTarget) {
			mergedTarget = xalloc(sizeof *mergedTarget);
			mergedTarget->fileName = mergeFileName;
			mergedTarget->swagger = f->swagger;
			continue;
		}
		openapiSwaggerObject *m = mergedTarget->swagger;
		for (int k = 0; k < f->swagger->Definitions->n; k++)
			defs_set(m->Definitions, f->swagger->Definitions->v[k].key, f->swagger->Definitions->v[k].value);
		if (f->swagger->SecurityDefinitions && f->swagger->SecurityDefinitions->n) {
			if (!m->SecurityDefinitions)
				go_panic("assignment to entry in nil map");
			for (int k = 0; k < f->swagger->SecurityDefinitions->n; k++)
				secdefs_set(m->SecurityDefinitions, f->swagger->SecurityDefinitions->v[k].key,
					    f->swagger->SecurityDefinitions->v[k].value);
		}
		/* copy(mergedTarget.swagger.Paths, f.swagger.Paths) */
		for (int k = 0; k < m->Paths.n && k < f->swagger->Paths.n; k++)
			m->Paths.v[k] = f->swagger->Paths.v[k];
		for (int k = 0; k < f->swagger->Security.n; k++) {
			openapiSecurityRequirementObject *v = xalloc(sizeof *v * (size_t)(m->Security.n + 1));
			mcopy(v, m->Security.v, sizeof *v * (size_t)m->Security.n);
			v[m->Security.n] = f->swagger->Security.v[k];
			m->Security.v = v;
			m->Security.n++;
			m->Security.nonnil = true;
		}
	}
	return mergedTarget;
}

static void deprecateFieldsAndMethods(GwFile *file)
{
	for (int i = 0; i < file->Messages.n; i++) {
		GwMessage *msg = file->Messages.v[i];
		for (int k = 0; k < pb_count(msg->proto, DP_FIELD); k++)
			map_put(&deprecatedSet, ptr_key(pb_msg_at(msg->proto, DP_FIELD, k)), &deprecatedSet);
	}
	for (int i = 0; i < file->Services.n; i++) {
		GwService *svc = file->Services.v[i];
		for (int k = 0; k < pb_count(svc->proto, SDP_METHOD); k++)
			map_put(&deprecatedSet, ptr_key(pb_msg_at(svc->proto, SDP_METHOD, k)), &deprecatedSet);
	}
}

static int cmp_path(const void *a, const void *b) { return str_cmp(((const pathData *)a)->Path, ((const pathData *)b)->Path); }

/* sortPathsAlphabetically (the paths are distinct) */
static void sortPathsAlphabetically(openapiSwaggerObject *so) { sort_any(so->Paths.v, (size_t)so->Paths.n, sizeof *so->Paths.v, cmp_path); }

static void vec_concat(Vec *a, const Vec *b)
{
	for (int i = 0; i < b->n; i++)
		vec_push(a, b->v[i]);
}

static int Generate(GwRegistry *reg, Str format, GwFile **targets, int ntargets, ResponseFile **out)
{
	for (int i = 0; i < ntargets; i++) {
		/* Because of how the generator merges definitions, it is simpler to deprecate field and
		 * methods here if the file is deprecated */
		if (pb_int(pb_msg(targets[i]->proto, FDP_OPTIONS), FILE_OPTIONS_DEPRECATED, 0))
			deprecateFieldsAndMethods(targets[i]);
	}
	GwFile *merged[1];
	if (reg->allowMerge) {
		GwFile *mergedTarget = NULL;
		/* try to find proto leader: the first file with file-level OpenAPI options */
		for (int i = 0; i < ntargets; i++)
			if (getFileOpenAPIOption(reg, targets[i])) {
				mergedTarget = targets[i];
				break;
			}
		/* merge protos to leader */
		for (int i = 0; i < ntargets; i++) {
			GwFile *f = targets[i];
			if (!mergedTarget) {
				mergedTarget = f;
			} else if (mergedTarget != f) {
				vec_concat(&mergedTarget->Enums, &f->Enums);
				vec_concat(&mergedTarget->Messages, &f->Messages);
				vec_concat(&mergedTarget->Services, &f->Services);
			}
		}
		if (!mergedTarget)
			go_panic("nil pointer dereference: allow_merge without files to generate");
		merged[0] = mergedTarget;
		targets = merged;
		ntargets = 1;
	}

	wrapper **openapis = xalloc(sizeof *openapis * (size_t)(ntargets + 1));
	for (int i = 0; i < ntargets; i++) {
		GwFile *file = targets[i];
		openapiSwaggerObject *swagger = applyTemplate((param){file, reg});
		openapis[i] = xalloc(sizeof *openapis[i]);
		openapis[i]->fileName = S(file->name);
		openapis[i]->swagger = swagger;
	}

	ResponseFile *files = xalloc(sizeof *files * (size_t)(ntargets + 1));
	int nfiles = 0;
	if (reg->allowMerge) {
		wrapper *targetOpenAPI = mergeTargetFile(openapis, ntargets, reg->mergeFileName);
		if (!reg->preserveRPCOrder)
			sortPathsAlphabetically(targetOpenAPI->swagger);
		files[nfiles++] = encodeOpenAPI(targetOpenAPI, format);
	} else {
		for (int i = 0; i < ntargets; i++) {
			if (!reg->preserveRPCOrder)
				sortPathsAlphabetically(openapis[i]->swagger);
			files[nfiles++] = encodeOpenAPI(openapis[i], format);
		}
	}
	*out = files;
	return nfiles;
}

/* AddErrorDefs adds google.rpc.Status and google.protobuf.Any to the registry (used for
 * error-related API responses): the file descriptors of anypb and genproto's rpc/status as
 * protodesc.ToFileDescriptorProto gives them (with an empty SourceCodeInfo). */
static void put_field(Buf *b, const char *name, int number, int label, int type, const char *typeName, const char *jsonName)
{
	Buf f = {0};
	pb_put_bytes(&f, FD_NAME, S(name));
	pb_put_tag(&f, FD_NUMBER, WT_VARINT);
	pb_put_varint(&f, (uint64_t)number);
	pb_put_tag(&f, FD_LABEL, WT_VARINT);
	pb_put_varint(&f, (uint64_t)label);
	pb_put_tag(&f, FD_TYPE, WT_VARINT);
	pb_put_varint(&f, (uint64_t)type);
	if (typeName)
		pb_put_bytes(&f, FD_TYPE_NAME, S(typeName));
	pb_put_bytes(&f, FD_JSON_NAME, S(jsonName));
	pb_put_bytes(b, DP_FIELD, buf_view(&f));
}

static Str error_defs_request(void)
{
	enum { FILE_OPTIONS_GO_PACKAGE = 11 };
	Buf req = {0};
	{
		Buf file = {0}, msg = {0}, opts = {0};
		pb_put_bytes(&file, FDP_NAME, S("google/protobuf/any.proto"));
		pb_put_bytes(&file, FDP_PACKAGE, S("google.protobuf"));
		pb_put_bytes(&msg, DP_NAME, S("Any"));
		put_field(&msg, "type_url", 1, LABEL_OPTIONAL, PT_STRING, NULL, "typeUrl");
		put_field(&msg, "value", 2, LABEL_OPTIONAL, PT_BYTES, NULL, "value");
		pb_put_bytes(&file, FDP_MESSAGE_TYPE, buf_view(&msg));
		pb_put_bytes(&opts, FILE_OPTIONS_GO_PACKAGE, S("google.golang.org/protobuf/types/known/anypb"));
		pb_put_bytes(&file, FDP_OPTIONS, buf_view(&opts));
		pb_put_bytes(&file, FDP_SOURCE_CODE_INFO, S(""));
		pb_put_bytes(&file, FDP_SYNTAX, S("proto3"));
		pb_put_bytes(&req, CGR_PROTO_FILE, buf_view(&file));
	}
	{
		Buf file = {0}, msg = {0}, opts = {0};
		pb_put_bytes(&file, FDP_NAME, S("google/rpc/status.proto"));
		pb_put_bytes(&file, FDP_PACKAGE, S("google.rpc"));
		pb_put_bytes(&file, FDP_DEPENDENCY, S("google/protobuf/any.proto"));
		pb_put_bytes(&msg, DP_NAME, S("Status"));
		put_field(&msg, "code", 1, LABEL_OPTIONAL, PT_INT32, NULL, "code");
		put_field(&msg, "message", 2, LABEL_OPTIONAL, PT_STRING, NULL, "message");
		put_field(&msg, "details", 3, LABEL_REPEATED, PT_MESSAGE, ".google.protobuf.Any", "details");
		pb_put_bytes(&file, FDP_MESSAGE_TYPE, buf_view(&msg));
		pb_put_bytes(&opts, FILE_OPTIONS_GO_PACKAGE, S("google.golang.org/genproto/googleapis/rpc/status;status"));
		pb_put_bytes(&file, FDP_OPTIONS, buf_view(&opts));
		pb_put_bytes(&file, FDP_SOURCE_CODE_INFO, S(""));
		pb_put_bytes(&file, FDP_SYNTAX, S("proto3"));
		pb_put_bytes(&req, CGR_PROTO_FILE, buf_view(&file));
	}
	return buf_view(&req);
}

/* ---- the extensions linked into the plugin ----
 *
 * proto.Unmarshal of the request decodes the extensions linked into protoc-gen-openapiv2
 * (google.api annotations and visibility, the openapiv2 options) eagerly, so malformed values make
 * the request unparsable; pkg/languages/go/protogen keeps them as unknown fields and they are
 * checked here: google.api.http and the openapiv2 options with their schemas, the string
 * extensions and VisibilityRule for UTF-8, field_behavior for varints, the other message
 * extensions for wire format only. */

static const char *const *linked_extensions(void)
{
	static const char *names[32];
	static const char *const openapiv2[] = {
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_swagger",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_operation",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_schema",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_enum",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_tag",
		"grpc.gateway.protoc_gen_openapiv2.options.openapiv2_field",
	};
	int n = 0;
	for (const char *const *p = genproto_annotations_extensions; *p; p++)
		names[n++] = *p;
	for (const char *const *p = genproto_visibility_extensions; *p; p++)
		names[n++] = *p;
	for (int i = 0; i < 6; i++)
		names[n++] = openapiv2[i];
	names[n] = NULL;
	return names;
}

enum { EXT_HTTP, EXT_STRING, EXT_WIRE, EXT_ENUMS, EXT_VISIBILITY, EXT_OPENAPI };
typedef struct {
	uint32_t num;
	const char *name;
	int kind;
	int msg; /* EXT_OPENAPI: GW_OA_* */
} LinkedExt;

static const char *check_utf8(const uint8_t *p, size_t n, const char *name)
{
	Str s = {(const char *)p, n};
	for (size_t k = 0; k < s.n;) {
		uint32_t rune;
		int w = utf8_decode(s.p + k, s.n - k, &rune);
		if (rune == 0xFFFD && w == 1)
			return xcat(go_err_prefix(), "field ", name, " contains invalid UTF-8");
		k += (size_t)w;
	}
	return NULL;
}

static const char *check_payload(const LinkedExt *x, const uint8_t *p, size_t n)
{
	PbReader r = {p, p + n};
	PbRawField f;
	switch (x->kind) {
	case EXT_HTTP: {
		PbMsg *m = pb_new(&genproto_http_rule()->pb);
		return gopb_decode(m, p, n);
	}
	case EXT_OPENAPI: {
		const char *err;
		gw_options_decode(x->msg, (Str){(const char *)p, n}, &err);
		return err;
	}
	case EXT_STRING:
		return check_utf8(p, n, x->name);
	case EXT_VISIBILITY:
		while (r.p < r.end) {
			if (!pb_next_field(&r, &f))
				return xcat(go_err_prefix(), "cannot parse invalid wire-format data");
			if ((f.num == 1 || f.num == 2) && f.wt == WT_LEN) {
				const char *err = check_utf8(f.val, f.vlen,
							     f.num == 1 ? "google.api.VisibilityRule.selector" : "google.api.VisibilityRule.restriction");
				if (err)
					return err;
			}
		}
		return NULL;
	default:
		while (r.p < r.end)
			if (!pb_next_field(&r, &f))
				return xcat(go_err_prefix(), "cannot parse invalid wire-format data");
		return NULL;
	}
}

static const char *check_options(const PbMsg *opts, const LinkedExt *exts, int n)
{
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		for (int i = 0; i < n; i++) {
			if (exts[i].num != f.num)
				continue;
			if (exts[i].kind == EXT_ENUMS) {
				if (f.wt == WT_LEN) {
					PbReader pr = {f.val, f.val + f.vlen};
					uint64_t x;
					while (pr.p < pr.end)
						if (!pb_read_varint(&pr, &x))
							return xcat(go_err_prefix(), "cannot parse invalid wire-format data");
				}
				continue;
			}
			if (f.wt != WT_LEN)
				continue;
			const char *err = check_payload(&exts[i], f.val, f.vlen);
			if (err)
				return err;
		}
	}
	return NULL;
}

#define VIS(name) {GENPROTO_E_VISIBILITY, "google.api." name, EXT_VISIBILITY, 0}
#define OA(msg) {GW_OA_EXTENSION, "openapiv2", EXT_OPENAPI, GW_OA_##msg}

static const char *check_field_options(const PbMsg *fd)
{
	static const LinkedExt exts[] = {
		{GENPROTO_E_FIELD_BEHAVIOR, "google.api.field_behavior", EXT_ENUMS, 0},
		{GENPROTO_E_FIELD_INFO, "google.api.field_info", EXT_WIRE, 0},
		{GENPROTO_E_RESOURCE_REFERENCE, "google.api.resource_reference", EXT_WIRE, 0},
		VIS("field_visibility"),
		OA(JSONSchema),
	};
	return check_options(pb_msg(fd, FD_OPTIONS), exts, 5);
}

static const char *check_enum(const PbMsg *e)
{
	static const LinkedExt exts[] = {VIS("enum_visibility"), OA(EnumSchema)};
	static const LinkedExt values[] = {VIS("value_visibility")};
	const char *err;
	for (int i = 0; i < pb_count(e, EDP_VALUE); i++)
		if ((err = check_options(pb_msg(pb_msg_at(e, EDP_VALUE, i), EVDP_OPTIONS), values, 1)))
			return err;
	return check_options(pb_msg(e, EDP_OPTIONS), exts, 2);
}

static const char *check_message(const PbMsg *m, int depth)
{
	const char *err;
	if (depth > 1000)
		return NULL; /* protodesc rejects deeper nesting later */
	for (int i = 0; i < pb_count(m, DP_FIELD); i++)
		if ((err = check_field_options(pb_msg_at(m, DP_FIELD, i))))
			return err;
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		if ((err = check_message(pb_msg_at(m, DP_NESTED_TYPE, i), depth + 1)))
			return err;
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		if ((err = check_enum(pb_msg_at(m, DP_ENUM_TYPE, i))))
			return err;
	for (int i = 0; i < pb_count(m, DP_EXTENSION); i++)
		if ((err = check_field_options(pb_msg_at(m, DP_EXTENSION, i))))
			return err;
	static const LinkedExt exts[] = {
		{GENPROTO_E_RESOURCE, "google.api.resource", EXT_WIRE, 0},
		VIS("message_visibility"),
		OA(Schema),
	};
	return check_options(pb_msg(m, DP_OPTIONS), exts, 3);
}

static const char *check_extensions(const PbMsg *req)
{
	const char *err;
	static const LinkedExt method_exts[] = {
		{GENPROTO_E_HTTP, "google.api.http", EXT_HTTP, 0},
		{GENPROTO_E_ROUTING, "google.api.routing", EXT_WIRE, 0},
		{GENPROTO_E_METHOD_SIGNATURE, "google.api.method_signature", EXT_STRING, 0},
		VIS("method_visibility"),
		OA(Operation),
	};
	static const LinkedExt service_exts[] = {
		{GENPROTO_E_DEFAULT_HOST, "google.api.default_host", EXT_STRING, 0},
		{GENPROTO_E_OAUTH_SCOPES, "google.api.oauth_scopes", EXT_STRING, 0},
		{GENPROTO_E_API_VERSION, "google.api.api_version", EXT_STRING, 0},
		VIS("api_visibility"),
		OA(Tag),
	};
	static const LinkedExt file_exts[] = {
		{GENPROTO_E_RESOURCE_DEFINITION, "google.api.resource_definition", EXT_WIRE, 0},
		OA(Swagger),
	};
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *f = pb_msg_at(req, CGR_PROTO_FILE, i);
		for (int k = 0; k < pb_count(f, FDP_MESSAGE_TYPE); k++)
			if ((err = check_message(pb_msg_at(f, FDP_MESSAGE_TYPE, k), 0)))
				return err;
		for (int k = 0; k < pb_count(f, FDP_ENUM_TYPE); k++)
			if ((err = check_enum(pb_msg_at(f, FDP_ENUM_TYPE, k))))
				return err;
		for (int k = 0; k < pb_count(f, FDP_SERVICE); k++) {
			const PbMsg *s = pb_msg_at(f, FDP_SERVICE, k);
			for (int j = 0; j < pb_count(s, SDP_METHOD); j++)
				if ((err = check_options(pb_msg(pb_msg_at(s, SDP_METHOD, j), MDP_OPTIONS), method_exts, 5)))
					return err;
			if ((err = check_options(pb_msg(s, 3), service_exts, 5)))
				return err;
		}
		for (int k = 0; k < pb_count(f, FDP_EXTENSION); k++)
			if ((err = check_field_options(pb_msg_at(f, FDP_EXTENSION, k))))
				return err;
		if ((err = check_options(pb_msg(f, FDP_OPTIONS), file_exts, 2)))
			return err;
	}
	return NULL;
}

/* ---- main.go ---- */

/* The flags of main.go (flag.CommandLine), set from the parameter by parseReqParam. */
typedef struct {
	Str importPrefix, file;
	bool allowDeleteBody;
	Str grpcAPIConfiguration;
	bool allowMerge;
	Str mergeFileName;
	bool useJSONNamesForFields;
	Str repeatedPathParamSeparator;
	bool versionFlag, allowRepeatedFieldsInBody, includePackageInTags, useFQNForOpenAPIName;
	Str openAPINamingStrategy;
	bool useGoTemplate;
	Str *goTemplateArgs;
	int nGoTemplateArgs;
	bool ignoreComments, removeInternalComments, disableDefaultErrors, enumsAsInts, simpleOperationIDs,
		proto3OptionalNullable;
	Str openAPIConfiguration;
	bool generateUnboundMethods;
	int recursiveDepth;
	bool omitEnumDefaultValue;
	Str outputFormat;
	Str *visibilityRestrictionSelectors;
	int nVisibilityRestrictionSelectors;
	bool disableServiceTags, disableDefaultResponses, useAllOfForRefs, omitArrayItemTypeWhenRefSibling,
		allowPatchFeature, preserveRPCOrder, enableRpcDeprecation, enableFieldDeprecation, expandSlashedPathPatterns,
		useProto3FieldSemantics, generateXGoType, ignoreGoPackageOption, logtostderr;
} Flags;

enum { F_STR, F_BOOL, F_INT, F_ARRAY };

static void str_array_append(Str **v, int *n, Str s)
{
	Str *a = xalloc(sizeof *a * (size_t)(*n + 1));
	if (*n)
		mcopy(a, *v, sizeof *a * (size_t)*n);
	a[*n] = s;
	*v = a;
	(*n)++;
}

/* flag.FlagSet.Set: NULL or the error */
static COLD const char *set_flag(Flags *f, Str name, Str value)
{
#define FL(n, field, kind) {n, offsetof(Flags, field), kind}
	static const struct {
		const char *name;
		size_t off;
		int kind;
	} flags[] = {
		FL("import_prefix", importPrefix, F_STR),
		FL("file", file, F_STR),
		FL("allow_delete_body", allowDeleteBody, F_BOOL),
		FL("grpc_api_configuration", grpcAPIConfiguration, F_STR),
		FL("allow_merge", allowMerge, F_BOOL),
		FL("merge_file_name", mergeFileName, F_STR),
		FL("json_names_for_fields", useJSONNamesForFields, F_BOOL),
		FL("repeated_path_param_separator", repeatedPathParamSeparator, F_STR),
		FL("version", versionFlag, F_BOOL),
		FL("allow_repeated_fields_in_body", allowRepeatedFieldsInBody, F_BOOL),
		FL("include_package_in_tags", includePackageInTags, F_BOOL),
		FL("fqn_for_openapi_name", useFQNForOpenAPIName, F_BOOL),
		FL("openapi_naming_strategy", openAPINamingStrategy, F_STR),
		FL("use_go_templates", useGoTemplate, F_BOOL),
		FL("go_template_args", goTemplateArgs, F_ARRAY),
		FL("ignore_comments", ignoreComments, F_BOOL),
		FL("remove_internal_comments", removeInternalComments, F_BOOL),
		FL("disable_default_errors", disableDefaultErrors, F_BOOL),
		FL("enums_as_ints", enumsAsInts, F_BOOL),
		FL("simple_operation_ids", simpleOperationIDs, F_BOOL),
		FL("proto3_optional_nullable", proto3OptionalNullable, F_BOOL),
		FL("openapi_configuration", openAPIConfiguration, F_STR),
		FL("generate_unbound_methods", generateUnboundMethods, F_BOOL),
		FL("recursive-depth", recursiveDepth, F_INT),
		FL("omit_enum_default_value", omitEnumDefaultValue, F_BOOL),
		FL("output_format", outputFormat, F_STR),
		FL("visibility_restriction_selectors", visibilityRestrictionSelectors, F_ARRAY),
		FL("disable_service_tags", disableServiceTags, F_BOOL),
		FL("disable_default_responses", disableDefaultResponses, F_BOOL),
		FL("use_allof_for_refs", useAllOfForRefs, F_BOOL),
		FL("omit_array_item_type_when_ref_sibling", omitArrayItemTypeWhenRefSibling, F_BOOL),
		FL("allow_patch_feature", allowPatchFeature, F_BOOL),
		FL("preserve_rpc_order", preserveRPCOrder, F_BOOL),
		FL("enable_rpc_deprecation", enableRpcDeprecation, F_BOOL),
		FL("enable_field_deprecation", enableFieldDeprecation, F_BOOL),
		FL("expand_slashed_path_patterns", expandSlashedPathPatterns, F_BOOL),
		FL("use_proto3_field_semantics", useProto3FieldSemantics, F_BOOL),
		FL("generate_x_go_type", generateXGoType, F_BOOL),
		FL("ignore_go_package_option", ignoreGoPackageOption, F_BOOL),
		FL("logtostderr", logtostderr, F_BOOL),
	};
#undef FL
	for (size_t i = 0; i < sizeof flags / sizeof *flags; i++) {
		if (!str_eqc(name, flags[i].name))
			continue;
		void *p = (char *)f + flags[i].off;
		switch (flags[i].kind) {
		case F_STR:
			*(Str *)p = value;
			return NULL;
		case F_ARRAY:
			if (p == (void *)&f->goTemplateArgs)
				str_array_append(&f->goTemplateArgs, &f->nGoTemplateArgs, value);
			else
				str_array_append(&f->visibilityRestrictionSelectors, &f->nVisibilityRestrictionSelectors, value);
			return NULL;
		case F_BOOL: {
			/* boolValue.Set: strconv.ParseBool; the value is stored also on error */
			bool v;
			int err = go_std_parse_bool(value.p, value.n, &v);
			*(bool *)p = err ? false : v;
			return err ? "parse error" : NULL;
		}
		default: {
			/* intValue.Set: strconv.ParseInt(s, 0, strconv.IntSize) */
			int64_t v;
			int err = go_std_parse_int(value.p, value.n, 0, 64, &v);
			*(int *)p = err ? 0 : (int)v;
			if (!err && (v > 2147483647 || v < -2147483647 - 1))
				return "unsupported by protoc-gen-uni: recursive-depth beyond the range of int32";
			return err == GO_STD_ERR_RANGE ? "value out of range" : err ? "parse error" : NULL;
		}
		}
	}
	return xcat("no such flag -", xstrndup(name.p, name.n));
}

/* parseReqParam parses a CodeGeneratorRequest parameter into the flags; NULL or the error. */
static COLD const char *parseReqParam(Str param, Flags *f)
{
	if (param.n == 0)
		return NULL;
	Str *ps;
	int n = str_split_s(param, ',', &ps);
	for (int i = 0; i < n; i++) {
		Str p = ps[i];
		const char *eq = memchr(p.p, '=', p.n);
		const char *err;
		if (!eq) {
			static const char *const trueFlags[] = {"allow_delete_body", "allow_merge", "allow_repeated_fields_in_body",
								"include_package_in_tags"};
			bool isTrue = false;
			for (size_t k = 0; k < 4; k++)
				isTrue |= str_eqc(p, trueFlags[k]);
			err = set_flag(f, p, isTrue ? S("true") : S(""));
		} else {
			Str name = {p.p, (size_t)(eq - p.p)}, value = {eq + 1, p.n - (size_t)(eq - p.p) - 1};
			if (name.n && name.p[0] == 'M')
				continue; /* pkgMap: read by nothing */
			err = set_flag(f, name, value);
		}
		if (err) {
			if (strncmp(err, "unsupported", 11) == 0)
				return err;
			Buf b = {0};
			buf_adds(&b, "cannot set flag ");
			buf_add_str(&b, p);
			buf_fmt(&b, ": %s", err);
			return buf_str(&b);
		}
	}
	return NULL;
}

static void put_varint_field(Buf *b, uint32_t num, uint64_t v)
{
	pb_put_tag(b, num, WT_VARINT);
	pb_put_varint(b, v);
}

/* emitFiles: the files with the supported features (SetSupportedFeaturesOnCodeGeneratorResponse) */
static void emitFiles(Buf *out, const ResponseFile *files, int n)
{
	put_varint_field(out, CGRESP_SUPPORTED_FEATURES, GW_SUPPORTED_FEATURES);
	put_varint_field(out, CGRESP_MINIMUM_EDITION, GW_EDITIONS_MINIMUM);
	put_varint_field(out, CGRESP_MAXIMUM_EDITION, GW_EDITIONS_MAXIMUM);
	for (int i = 0; i < n; i++) {
		Buf f = {0};
		pb_put_bytes(&f, CGRESP_FILE_NAME, files[i].Name);
		pb_put_bytes(&f, CGRESP_FILE_CONTENT, files[i].Content);
		pb_put_bytes(out, CGRESP_FILE, buf_view(&f));
	}
}

static void emitError(Buf *out, Str err)
{
	out->n = 0;
	pb_put_bytes(out, CGRESP_ERROR, err);
}

typedef struct {
	PbMsg *req;
	Flags *f;
	Buf *out;
	const char *fatal; /* grpclog.Fatal: stderr and exit status 1 */
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	Flags *f = c->f;
	PbMsg *req = c->req;
	GwRegistry *reg = gw_NewRegistry();

	reg->allowDeleteBody = f->allowDeleteBody;
	reg->allowMerge = f->allowMerge;
	reg->ignoreGoPackageOption = f->ignoreGoPackageOption;
	reg->mergeFileName = f->mergeFileName;
	reg->useJSONNamesForFields = f->useJSONNamesForFields;
	reg->useProto3FieldSemantics = f->useProto3FieldSemantics;
	/* allow_repeated_fields_in_body: a deprecation warning on stderr */
	reg->includePackageInTags = f->includePackageInTags;

	/* Set the naming strategy either directly from the flag, or via the value of the legacy
	 * fqn_for_openapi_name flag. */
	Str namingStrategy = f->openAPINamingStrategy;
	if (f->useFQNForOpenAPIName) {
		if (namingStrategy.n) {
			c->fatal = "The deprecated `fqn_for_openapi_name` flag must remain unset if `openapi_naming_strategy` is set.";
			return;
		}
		/* grpclog.Warning: the flag is deprecated */
		namingStrategy = S("fqn");
	} else if (!namingStrategy.n) {
		namingStrategy = S("legacy");
	}
	if (!LookupNamingStrategy(namingStrategy)) {
		emitError(c->out, cat3(S("invalid naming strategy "), go_quote(namingStrategy), S("")));
		return;
	}

	if (f->useGoTemplate && f->ignoreComments) {
		emitError(c->out, S("`ignore_comments` and `use_go_templates` are mutually exclusive and cannot be enabled at the "
				    "same time"));
		return;
	}
	reg->useGoTemplate = f->useGoTemplate;
	reg->ignoreComments = f->ignoreComments;
	reg->removeInternalComments = f->removeInternalComments;

	if (f->nGoTemplateArgs > 0 && !f->useGoTemplate) {
		emitError(c->out, S("`go_template_args` requires `use_go_templates` to be enabled"));
		return;
	}

	reg->openAPINamingStrategy = xstrndup(namingStrategy.p, namingStrategy.n);
	if (memchr(namingStrategy.p, 0, namingStrategy.n))
		fail("unsupported by protoc-gen-uni: NUL byte in openapi_naming_strategy");
	reg->enumsAsInts = f->enumsAsInts;
	reg->disableDefaultErrors = f->disableDefaultErrors;
	reg->simpleOperationIDs = f->simpleOperationIDs;
	reg->proto3OptionalNullable = f->proto3OptionalNullable;
	reg->generateUnboundMethods = f->generateUnboundMethods;
	reg->recursiveDepth = f->recursiveDepth;
	reg->omitEnumDefaultValue = f->omitEnumDefaultValue;
	gw_Registry_SetVisibilityRestrictionSelectors(reg, f->visibilityRestrictionSelectors, f->nVisibilityRestrictionSelectors);
	reg->disableServiceTags = f->disableServiceTags;
	reg->disableDefaultResponses = f->disableDefaultResponses;
	reg->useAllOfForRefs = f->useAllOfForRefs;
	reg->omitArrayItemTypeWhenRefSibling = f->omitArrayItemTypeWhenRefSibling;
	reg->allowPatchFeature = f->allowPatchFeature;
	reg->preserveRPCOrder = f->preserveRPCOrder;
	reg->enableRpcDeprecation = f->enableRpcDeprecation;
	reg->enableFieldDeprecation = f->enableFieldDeprecation;
	reg->expandSlashedPathPatterns = f->expandSlashedPathPatterns;
	reg->generateXGoType = f->generateXGoType;

	Str err = gw_Registry_SetRepeatedPathParamSeparator(reg, f->repeatedPathParamSeparator);
	if (err.p) {
		emitError(c->out, err);
		return;
	}

	if (f->grpcAPIConfiguration.n) {
		emitError(c->out, gw_ReadConfigFile(f->grpcAPIConfiguration, "gRPC API Configuration", PROGRAM, "grpc_api_configuration"));
		return;
	}

	if (!format_valid(f->outputFormat)) {
		emitError(c->out, cat(S("unknown format: "), f->outputFormat));
		return;
	}

	PgOptions opts = {0};
	opts.LinkedExtensions = linked_extensions();

	/* AddErrorDefs */
	Str defs = error_defs_request();
	const char *perr;
	PbMsg *defsReq = pg_parse_request((const uint8_t *)defs.p, defs.n, &perr);
	if (!defsReq)
		fail("%s", perr);
	err = gw_Registry_Load(reg, defsReq, &opts);
	if (err.p) {
		emitError(c->out, err);
		return;
	}

	err = gw_Registry_Load(reg, req, &opts);
	if (err.p) {
		emitError(c->out, err);
		return;
	}

	if (f->openAPIConfiguration.n) {
		emitError(c->out, gw_ReadConfigFile(f->openAPIConfiguration, "OpenAPI Configuration", PROGRAM, "openapi_configuration"));
		return;
	}

	int ntargets = pb_count(req, CGR_FILE_TO_GENERATE);
	GwFile **targets = xalloc(sizeof *targets * (size_t)(ntargets + 1));
	for (int i = 0; i < ntargets; i++) {
		const char *lerr = NULL;
		targets[i] = gw_Registry_LookupFile(reg, pb_str_at(req, CGR_FILE_TO_GENERATE, i), &lerr);
		if (!targets[i]) {
			c->fatal = lerr;
			return;
		}
	}

	if (f->useGoTemplate)
		fail("unsupported by protoc-gen-uni: use_go_templates (the comments as Go text/template templates)");

	ResponseFile *files;
	int nfiles = Generate(reg, f->outputFormat, targets, ntargets, &files);
	emitFiles(c->out, files, nfiles);
}

static COLD const char *generate1(PbMsg *uni_req, Str raw, Buf *out)
{
	genproto_annotations_init();
	gw_options_init();
	{
		const char *e;
		jsonSchemaDef = gw_options_decode(GW_OA_JSONSchema, (Str){"", 0}, &e)->def;
	}
	registriesSeen = NULL;
	reset_caches();
	deprecatedSet = (Map){0};
	queryParamsWork = 0;

	/* codegenerator.ParseRequest: proto.Unmarshal of the request (protobuf-go semantics) */
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)raw.p, raw.n, &perr);
	if (!req && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (req && (perr = check_extensions(req)) && strcmp(perr, GOPB_ERR_TOO_DEEP) == 0) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}
	if (!req || perr)
		return xcat(PROGRAM ": ", perr); /* grpclog.Fatal: stderr and exit status 1 */
	pb_set_bytes(req, CGR_PARAMETER, pb_bytes(uni_req, CGR_PARAMETER));
	if ((perr = pg_unsupported(req))) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(perr));
		return NULL;
	}

	Flags f = {0};
	f.file = S("-");
	f.mergeFileName = S("apidocs");
	f.useJSONNamesForFields = true;
	f.repeatedPathParamSeparator = S("csv");
	f.allowRepeatedFieldsInBody = true;
	f.recursiveDepth = 1000;
	f.outputFormat = S("json");
	f.allowPatchFeature = true;
	if (pb_has(req, CGR_PARAMETER)) {
		const char *err = parseReqParam(pb_bytes(req, CGR_PARAMETER), &f);
		if (err) {
			if (strncmp(err, "unsupported", 11) == 0) {
				pb_put_bytes(out, CGRESP_ERROR, str_c(err));
				return NULL;
			}
			return xcat(PROGRAM ": Error parsing flags: ", err); /* grpclog.Fatalf */
		}
	}
	/* file and version: main reads them before the parameter is parsed (no effect) */

	RunCtx c = {req, &f, out, NULL};
	if (try_run(run, &c)) {
		emitError(out, fail_message());
		return NULL;
	}
	if (c.fatal)
		return xcat(PROGRAM ": ", c.fatal);
	return NULL;
}

static COLD const char *generate(PbMsg *uni_req, Str raw, Buf *out)
{
	/* internal/detrand.Bool() of the plugin binary (see README.md) */
	go_detrand = true;
	const char *err = generate1(uni_req, raw, out);
	go_detrand = false;
	return err;
}

/* parses_raw: generate parses raw with protobuf-go semantics and takes only the parameter from
 * uni_req. */
const UniGenerator gen_grpc_ecosystem_openapiv2_v2_33_0 = {
	.name = "grpc-ecosystem/openapiv2",
	.version = "v2.33.0",
	.generate = generate,
	.parses_raw = true,
};
