/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of the shared packages of github.com/grpc-ecosystem/grpc-gateway/v2 v2.33.0 that its
 * protoc plugins (protoc-gen-grpc-gateway, protoc-gen-openapiv2) are built on:
 *
 *   internal/casing          casing/camel.c        Camel, CamelIdentifier, JSONCamelCase
 *   internal/httprule        httprule/                path template parser and compiler
 *   utilities                utilities/trie.c      DoubleArray (a trie of field paths)
 *   internal/descriptor      descriptor/              Registry: files, messages, enums, services,
 *                                                  methods and HTTP bindings from protogen
 *   internal/codegenerator   (this header)         supported features and editions
 *   protoc-gen-openapiv2/options  options/openapiv2.c  the tables of the openapiv2_* options
 *
 * descriptor also has buf_build.go of bufbuild/plugins' separate_pkg_additional_imports.patch,
 * which buf.build applies to the gateway build (SetSeparatePackage, IncludeAdditionalImports,
 * Service.GRPCFile); without separate_package it changes nothing, so it is harmless for the other
 * plugin. Go names are kept (gw_<package>_<Name>, methods gw_<Type>_<Method>). Strings from
 * descriptors are NUL-terminated (protodesc validated them: identifiers); strings from options
 * (HTTP rules) are Str and may contain any bytes. Not ported: the OpenAPI options of the registry
 * (openapiconfig) and the loading of grpc_api_configuration files (the caller reports what it
 * does instead). */
#ifndef GO_GRPC_GATEWAY_H
#define GO_GRPC_GATEWAY_H

#include "protogen.h"

/* ---- internal/codegenerator: supportedCodeGeneratorFeatures, supportedEditions ---- */

enum {
	GW_SUPPORTED_FEATURES = 3,        /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	GW_EDITIONS_MINIMUM = 1000,       /* EDITION_2023 */
	GW_EDITIONS_MAXIMUM = 1002,       /* EDITION_2026 */
};
/* SetSupportedFeaturesOnPluginGen */
void gw_codegenerator_SetSupportedFeaturesOnPluginGen(PgPlugin *gen);

/* ---- internal/casing ---- */

/* Camel returns the CamelCased name ("_my_field_name_2" -> "XMyFieldName_2"). */
const char *gw_casing_Camel(const char *s);
/* CamelIdentifier camel-cases the part after the last '.' only (if any). */
const char *gw_casing_CamelIdentifier(const char *s);
/* JSONCamelCase converts snake_case to camelCase like the protobuf JSON mapping. */
const char *gw_casing_JSONCamelCase(const char *s);

/* ---- utilities: OpCode and DoubleArray ---- */

enum { GW_OpNop, GW_OpPush, GW_OpLitPush, GW_OpPushM, GW_OpConcatN, GW_OpCapture, GW_OpEnd };

/* DoubleArray is a Double Array implementation of trie on sequences of strings. Encoding is a
 * map from the tokens to their codes 0..nEncoding-1, kept here as the tokens in code order. A
 * nil Base/Check (no sequences) has base_nil set. */
typedef struct {
	const char **Encoding;
	int nEncoding;
	int *Base, *Check;
	int n; /* len(Base) == len(Check) */
	bool nil;
} GwDoubleArray;
/* NewDoubleArray builds a DoubleArray from nseqs sequences of strings (seqs[i] has lens[i]). */
GwDoubleArray *gw_utilities_NewDoubleArray(const char *const *const *seqs, const int *lens, int nseqs);

/* ---- internal/httprule ---- */

/* Template is a compiled representation of path templates. */
typedef struct {
	int Version;
	int *OpCodes;
	int nOpCodes;
	Str *Pool; /* constant pool */
	int nPool;
	Str Verb;
	Str *Fields; /* field paths bound in this template */
	int nFields;
	Str Template; /* the original template */
} GwTemplate;

typedef struct GwHttpruleTemplate GwHttpruleTemplate; /* parsed template (httprule.template) */

/* Parse parses the string representation of path template; NULL and *err (the error text,
 * InvalidTemplateError) on failure. */
GwHttpruleTemplate *gw_httprule_Parse(Str tmpl, Str *err);
/* template.Compile */
GwTemplate gw_httprule_Compile(const GwHttpruleTemplate *t);
/* template.String */
Str gw_httprule_String(const GwHttpruleTemplate *t);

/* Segment is a top-level segment of a parsed path template (SplitTemplate). */
typedef struct {
	Str FieldPath; /* variable segments only */
	Str Value;     /* literal or wildcard, or the pattern of a variable */
} GwSegment;
/* SplitTemplate parses tmpl and returns its top-level segments and its verb; false and *err on
 * failure. The root template "/" has no segments. */
bool gw_httprule_SplitTemplate(Str tmpl, GwSegment **segments, int *nsegments, Str *verb, Str *err);

/* ---- internal/descriptor ---- */

typedef struct GwRegistry GwRegistry;
typedef struct GwFile GwFile;
typedef struct GwMessage GwMessage;
typedef struct GwEnum GwEnum;
typedef struct GwService GwService;
typedef struct GwMethod GwMethod;
typedef struct GwField GwField;
typedef struct GwBinding GwBinding;

/* GoPackage represents a golang package. */
typedef struct {
	const char *Path;  /* the package path */
	const char *Name;  /* the package name */
	const char *Alias; /* an alias unique within the invocation ("" if none) */
} GwGoPackage;
/* GoPackage == GoPackage */
bool gw_GoPackage_eq(GwGoPackage a, GwGoPackage b);
/* Standard returns whether the import is a golang standard package. */
bool gw_GoPackage_Standard(GwGoPackage p);
/* String: `"path"` or `alias "path"` (an import line). */
const char *gw_GoPackage_String(GwGoPackage p);

/* File wraps the FileDescriptorProto. Names of messages, services and methods are mutable: the
 * gateway generator overwrites them with their camel-cased form (it assigns to *msg.Name). */
struct GwFile {
	PbMsg *proto; /* FileDescriptorProto */
	const char *name;
	const char *package; /* GetPackage(); has_package: Package != nil */
	bool has_package;
	bool proto2; /* Syntax == nil || GetSyntax() == "proto2" */
	GwGoPackage GoPkg;
	const char *GeneratedFilenamePrefix;
	Vec Messages; /* GwMessage *, nested ones too (registration order) */
	Vec Enums;    /* GwEnum * */
	Vec Services; /* GwService * (those with methods) */
};
/* Pkg returns package name or alias if it's present */
const char *gw_File_Pkg(const GwFile *f);

struct GwMessage {
	PbMsg *proto; /* DescriptorProto */
	const char *Name;
	GwFile *File;
	const char **Outers;
	int nOuters;
	Vec Fields; /* GwField * */
	int Index;
	bool ForcePrefixedName;
};
const char *gw_Message_FQMN(const GwMessage *m);
const char *gw_Message_GoType(const GwMessage *m, const char *currentPackage);
/* GetOneofDecl()[i].GetName() */
const char *gw_Message_OneofName(const GwMessage *m, int i);

struct GwEnum {
	PbMsg *proto; /* EnumDescriptorProto */
	const char *Name;
	GwFile *File;
	const char **Outers;
	int nOuters;
	int Index;
	bool ForcePrefixedName;
};
const char *gw_Enum_FQEN(const GwEnum *e);
const char *gw_Enum_GoType(const GwEnum *e, const char *currentPackage);

struct GwService {
	PbMsg *proto; /* ServiceDescriptorProto */
	const char *Name;
	GwFile *File;
	GwFile *GRPCFile; /* separate_package: where the gRPC stubs are (only GoPkg set) */
	Vec Methods; /* GwMethod * */
	bool ForcePrefixedName;
};
const char *gw_Service_FQSN(const GwService *s);
const char *gw_Service_InstanceName(const GwService *s);
const char *gw_Service_ClientConstructorName(const GwService *s);

struct GwMethod {
	PbMsg *proto; /* MethodDescriptorProto */
	const char *Name;
	bool ClientStreaming, ServerStreaming;
	GwService *Service;
	GwMessage *RequestType, *ResponseType;
	Vec Bindings; /* GwBinding * */
};
const char *gw_Method_FQMN(const GwMethod *m);

/* Field wraps the FieldDescriptorProto. */
struct GwField {
	PbMsg *proto;
	const char *Name;
	int Type, Label;
	const char *TypeName; /* "" if not set */
	bool HasOneofIndex;
	int OneofIndex;
	bool Proto3Optional;
	const char *JsonName;
	GwMessage *Message;
	GwMessage *FieldMessage; /* never set by the registry */
	bool ForcePrefixedName;
};
const char *gw_Field_FQFN(const GwField *f);

/* FieldPathComponent is a path component in FieldPath */
typedef struct {
	const char *Name;
	GwField *Target;
} GwFieldPathComponent;

/* FieldPath is a path to a field from a request message. */
typedef struct {
	GwFieldPathComponent *v;
	int n;
} GwFieldPath;
const char *gw_FieldPath_String(GwFieldPath p);
bool gw_FieldPath_IsNestedProto3(GwFieldPath p);
bool gw_FieldPath_IsOptionalProto3(GwFieldPath p);
const char *gw_FieldPath_AssignableExpr(GwFieldPath p, const char *msgExpr, const char *currentPackage);
const char *gw_FieldPath_AssignableExprPrep(GwFieldPath p, const char *msgExpr, const char *currentPackage);
const char *gw_FieldPath_OpaqueSetterExpr(GwFieldPath p, const char *msgExpr);
/* FieldPathComponent.AssignableExpr, ValueExpr */
const char *gw_FieldPathComponent_AssignableExpr(GwFieldPathComponent c);
const char *gw_FieldPathComponent_ValueExpr(GwFieldPathComponent c);

/* Parameter is a parameter provided in http requests */
typedef struct {
	GwFieldPath FieldPath;
	GwField *Target;
	GwMethod *Method;
} GwParameter;
/* ConvertFuncExpr: the go expression of a converter function; NULL and *err if unsupported. */
const char *gw_Parameter_ConvertFuncExpr(const GwParameter *p, const char **err);
bool gw_Parameter_IsEnum(const GwParameter *p);
bool gw_Parameter_IsRepeated(const GwParameter *p);
bool gw_Parameter_IsProto2(const GwParameter *p);

/* Body describes a http (request|response) body (FieldPath empty: the whole message). */
typedef struct {
	GwFieldPath FieldPath;
} GwBody;

struct GwBinding {
	GwMethod *Method;
	int Index;
	GwTemplate PathTmpl;
	Str HTTPMethod;
	GwParameter *PathParams;
	int nPathParams;
	GwBody *Body;         /* NULL if none */
	GwBody *ResponseBody; /* NULL if none */
};

/* IsWellKnownType returns true if the fully qualified type name is considered 'well-known'. */
bool gw_IsWellKnownType(const char *typeName);

/* buf_build.go */
#define GW_BaseTypePackageSubPath "protocolbuffers/go"
/* TODO: change "v2" to "v3" when v3 of grpc gateway is released, or, even better, stop
 * generating at the extra location. */
#define GW_GatewayPackageSubPath "grpc-ecosystem/gateway/v2"

/* The registry: NewRegistry, then the settings, then LoadFromPlugin. Errors are their text (Str
 * errors may contain any bytes; .p == NULL: success). */
GwRegistry *gw_NewRegistry(void);
Str gw_Registry_LoadFromPlugin(GwRegistry *r, PgPlugin *gen);
GwMessage *gw_Registry_LookupMsg(GwRegistry *r, const char *location, const char *name, const char **err);
GwEnum *gw_Registry_LookupEnum(GwRegistry *r, const char *location, const char *name, const char **err);
GwFile *gw_Registry_LookupFile(GwRegistry *r, const char *name, const char **err);
/* ReserveGoPackageAlias reserves the unique alias of go package; false if it is already taken by
 * another package. */
bool gw_Registry_ReserveGoPackageAlias(GwRegistry *r, const char *alias, const char *pkgpath);
Str gw_Registry_SetRepeatedPathParamSeparator(GwRegistry *r, Str name);
int32_t gw_Registry_GetRepeatedPathParamSeparator(const GwRegistry *r);
/* CheckDuplicateAnnotation: NULL or "duplicate annotation: method=..., template=...". */
Str gw_Registry_CheckDuplicateAnnotation(GwRegistry *r, Str httpMethod, Str httpTemplate, const GwService *svc);
/* UnboundExternalHTTPRules: the methods of the external HTTP rules without a matching method
 * (none: external rules come only from grpc_api_configuration files, which are not ported). */
int gw_Registry_UnboundExternalHTTPRules(GwRegistry *r, const char ***out);
/* buf_build.go: GetAdditionalImports */
int gw_Registry_GetAdditionalImports(GwRegistry *r, GwGoPackage goPkg, const char ***out);

/* ---- protoc-gen-openapiv2/options (options/openapiv2.c) ---- */

/* The message tables (google.protobuf.Value, Struct and ListValue included). */
enum {
	GW_OA_Swagger, GW_OA_Operation, GW_OA_Parameters, GW_OA_HeaderParameter, GW_OA_Header, GW_OA_Response,
	GW_OA_Info, GW_OA_Contact, GW_OA_License, GW_OA_ExternalDocumentation, GW_OA_Schema, GW_OA_EnumSchema,
	GW_OA_JSONSchema, GW_OA_FieldConfiguration, GW_OA_Tag, GW_OA_SecurityDefinitions, GW_OA_SecurityScheme,
	GW_OA_SecurityRequirement, GW_OA_SecurityRequirementValue, GW_OA_Scopes, GW_OA_MapResponse, GW_OA_MapValue,
	GW_OA_MapHeader, GW_OA_MapString, GW_OA_MapSecurityScheme, GW_OA_MapSecurityRequirementValue, GW_OA_Value,
	GW_OA_Struct, GW_OA_ListValue, GW_OA_COUNT
};
/* The extension number of every openapiv2_* option (on File, Method, Message, Enum, Service and
 * Field options). */
enum { GW_OA_EXTENSION = 1042 };
/* Field numbers. */
enum {
	OA_Swagger_swagger = 1, OA_Swagger_info = 2, OA_Swagger_host = 3, OA_Swagger_base_path = 4,
	OA_Swagger_schemes = 5, OA_Swagger_consumes = 6, OA_Swagger_produces = 7, OA_Swagger_responses = 10,
	OA_Swagger_security_definitions = 11, OA_Swagger_security = 12, OA_Swagger_tags = 13,
	OA_Swagger_external_docs = 14, OA_Swagger_extensions = 15,
	OA_Operation_tags = 1, OA_Operation_summary = 2, OA_Operation_description = 3, OA_Operation_external_docs = 4,
	OA_Operation_operation_id = 5, OA_Operation_consumes = 6, OA_Operation_produces = 7,
	OA_Operation_responses = 9, OA_Operation_schemes = 10, OA_Operation_deprecated = 11,
	OA_Operation_security = 12, OA_Operation_extensions = 13, OA_Operation_parameters = 14,
	OA_Parameters_headers = 1,
	OA_HeaderParameter_name = 1, OA_HeaderParameter_description = 2, OA_HeaderParameter_type = 3,
	OA_HeaderParameter_format = 4, OA_HeaderParameter_required = 5,
	OA_Header_description = 1, OA_Header_type = 2, OA_Header_format = 3, OA_Header_default = 6,
	OA_Header_pattern = 13,
	OA_Response_description = 1, OA_Response_schema = 2, OA_Response_headers = 3, OA_Response_examples = 4,
	OA_Response_extensions = 5,
	OA_Info_title = 1, OA_Info_description = 2, OA_Info_terms_of_service = 3, OA_Info_contact = 4,
	OA_Info_license = 5, OA_Info_version = 6, OA_Info_extensions = 7,
	OA_Contact_name = 1, OA_Contact_url = 2, OA_Contact_email = 3,
	OA_License_name = 1, OA_License_url = 2,
	OA_ExternalDocumentation_description = 1, OA_ExternalDocumentation_url = 2,
	OA_Schema_json_schema = 1, OA_Schema_discriminator = 2, OA_Schema_read_only = 3, OA_Schema_external_docs = 5,
	OA_Schema_example = 6,
	OA_EnumSchema_description = 1, OA_EnumSchema_default = 2, OA_EnumSchema_title = 3, OA_EnumSchema_required = 4,
	OA_EnumSchema_read_only = 5, OA_EnumSchema_external_docs = 6, OA_EnumSchema_example = 7, OA_EnumSchema_ref = 8,
	OA_EnumSchema_extensions = 9,
	OA_JSONSchema_ref = 3, OA_JSONSchema_title = 5, OA_JSONSchema_description = 6, OA_JSONSchema_default = 7,
	OA_JSONSchema_read_only = 8, OA_JSONSchema_example = 9, OA_JSONSchema_multiple_of = 10,
	OA_JSONSchema_maximum = 11, OA_JSONSchema_exclusive_maximum = 12, OA_JSONSchema_minimum = 13,
	OA_JSONSchema_exclusive_minimum = 14, OA_JSONSchema_max_length = 15, OA_JSONSchema_min_length = 16,
	OA_JSONSchema_pattern = 17, OA_JSONSchema_max_items = 20, OA_JSONSchema_min_items = 21,
	OA_JSONSchema_unique_items = 22, OA_JSONSchema_max_properties = 24, OA_JSONSchema_min_properties = 25,
	OA_JSONSchema_required = 26, OA_JSONSchema_array = 34, OA_JSONSchema_type = 35, OA_JSONSchema_format = 36,
	OA_JSONSchema_enum = 46, OA_JSONSchema_extensions = 48, OA_JSONSchema_field_configuration = 1001,
	OA_FieldConfiguration_path_param_name = 47, OA_FieldConfiguration_deprecated = 49,
	OA_Tag_name = 1, OA_Tag_description = 2, OA_Tag_external_docs = 3, OA_Tag_extensions = 4,
	OA_SecurityDefinitions_security = 1,
	OA_SecurityScheme_type = 1, OA_SecurityScheme_description = 2, OA_SecurityScheme_name = 3,
	OA_SecurityScheme_in = 4, OA_SecurityScheme_flow = 5, OA_SecurityScheme_authorization_url = 6,
	OA_SecurityScheme_token_url = 7, OA_SecurityScheme_scopes = 8, OA_SecurityScheme_extensions = 9,
	OA_SecurityRequirement_security_requirement = 1, OA_SecurityRequirementValue_scope = 1, OA_Scopes_scope = 1,
	OA_Value_null_value = 1, OA_Value_number_value = 2, OA_Value_string_value = 3, OA_Value_bool_value = 4,
	OA_Value_struct_value = 5, OA_Value_list_value = 6, OA_Struct_fields = 1, OA_ListValue_values = 1,
};
/* Builds the tables (call once per request, after arena_reset). */
void gw_options_init(void);
/* proto.Unmarshal of a payload into a new message of table msg (GW_OA_*): NULL and *err (the
 * error text) if it is malformed. */
PbMsg *gw_options_decode(int msg, Str payload, const char **err);
/* The entries of a map field as a Go map holds them: the last entry of every key, sorted by key
 * (Go maps have no order; the callers sort or do not depend on it). value is the entry message
 * (field 2 is the value). Returns the count. */
typedef struct {
	Str key;
	const PbMsg *value;
	int index;
} GwMapEntry;
int gw_options_map(const PbMsg *m, uint32_t num, GwMapEntry **out);

/* The settings (the Set and Get methods of registry.go that the plugins use). */
struct GwRegistry {
	Map msgs, enums, files, meths;
	Map pkgAliases; /* alias -> path */
	bool allowDeleteBody;
	bool standalone;
	bool warnOnUnboundMethods;
	bool generateUnboundMethods;
	bool omitPackageDoc;
	bool strictDeepWildcard;
	bool allowPatchFeature;
	bool separatePackage; /* buf_build.go */
	Str repeatedPathParamSeparatorName;
	int32_t repeatedPathParamSeparator;
	int recursiveDepth;
	Map annotationMap;
	Map additionalImports; /* path -> Vec of paths (buf_build.go) */
	/* The settings of protoc-gen-openapiv2 (prefix and pkgMap are set by its main but read by
	 * nothing). */
	bool allowMerge, ignoreGoPackageOption, includePackageInTags, useJSONNamesForFields,
		useProto3FieldSemantics, useGoTemplate, ignoreComments, removeInternalComments, enumsAsInts,
		omitEnumDefaultValue, disableDefaultErrors, simpleOperationIDs, proto3OptionalNullable,
		disableServiceTags, disableDefaultResponses, useAllOfForRefs, omitArrayItemTypeWhenRefSibling,
		preserveRPCOrder, enableRpcDeprecation, enableFieldDeprecation, expandSlashedPathPatterns,
		generateXGoType;
	Str mergeFileName;
	const char *openAPINamingStrategy; /* "legacy" by default */
	Str *visibilityRestrictionSelectors; /* the set (trimmed), no duplicates */
	int nVisibilityRestrictionSelectors;
};

/* Load loads definitions of services, methods, messages, enumerations and fields from "req":
 * protogen.Options{}.New(req) with opts (ParamFunc NULL: unknown parameters are ignored; the
 * extensions linked into the plugin), then LoadFromPlugin. With ignoreGoPackageOption, the files
 * of req without go_package get a placeholder first (fillGoPackageOptionIfMissing). */
Str gw_Registry_Load(GwRegistry *r, PbMsg *req, const PgOptions *opts);
/* GetAllFQMNs, GetAllFQENs, GetAllFQMethNs: the keys of the maps (in no particular order, as Go
 * map iteration). */
int gw_Registry_GetAllFQMNs(const GwRegistry *r, const char ***out);
int gw_Registry_GetAllFQENs(const GwRegistry *r, const char ***out);
int gw_Registry_GetAllFQMethNs(const GwRegistry *r, const char ***out);
/* os.ReadFile of a configuration file (LoadGrpcAPIServiceFromYAML: what "gRPC API Configuration",
 * LoadOpenAPIConfigFromYAML: "OpenAPI Configuration") in the container of the buf.build plugin
 * (program: the binary at its root): the error upstream gives there, or an "unsupported" error
 * naming option=file for a path that exists in it. */
Str gw_ReadConfigFile(Str yamlFile, const char *what, const char *program, const char *option);
/* SetVisibilityRestrictionSelectors (the selectors are trimmed with strings.TrimSpace) */
void gw_Registry_SetVisibilityRestrictionSelectors(GwRegistry *r, const Str *selectors, int n);
bool gw_Registry_IsVisibilityRestrictionSelector(const GwRegistry *r, Str selector);
/* FieldName: the JSON name with useJSONNamesForFields, else the proto name. */
const char *gw_Registry_FieldName(const GwRegistry *r, const GwField *f);

#endif
