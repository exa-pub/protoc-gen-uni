// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
// github.com/grpc-ecosystem/grpc-gateway).

/* Port of protoc-gen-openapiv2/internal/genopenapi/types.go of grpc-gateway v2.33.0: the OpenAPI
 * v2 objects. Go value semantics are kept: objects are copied by value (struct assignment), a
 * slice shares its backing array until append outgrows it (GoStrs), a nil slice, map or interface
 * differs from an empty one where the Go code tests it. Strings are Str (any bytes). Included by
 * generator.c (unity build). */

/* []string with Go's slice semantics: v == NULL is nil; append reallocates only past cap. */
typedef struct {
	Str *v;
	int len, cap;
} GoStrs;

/* interface{} values of Enum and Default: nil, string, int, []string, []int. */
enum { ANY_NIL, ANY_STRING, ANY_INT, ANY_STRINGS, ANY_INTS };
typedef struct {
	int kind;
	Str s;
	long long i;
	GoStrs strs;
	long long *ints;
	int nints;
} Any;

/* RawExample / json.RawMessage: p == NULL is nil (len 0 is empty for omitempty either way). */
typedef Str RawExample;

typedef struct {
	Str key;
	Str value; /* json.RawMessage */
} extension;

/* []extension (nonnil: the slice is not nil) */
typedef struct {
	extension *v;
	int n;
	bool nonnil;
} Extensions;

typedef struct {
	Str Description, URL;
} openapiExternalDocumentationObject;

typedef struct {
	Str Name, URL, Email;
} openapiContactObject;

typedef struct {
	Str Name, URL;
} openapiLicenseObject;

typedef struct {
	Str Title, Description, TermsOfService, Version;
	openapiContactObject *Contact;
	openapiLicenseObject *License;
	Extensions extensions;
} openapiInfoObject;

typedef struct {
	Str Name, Description;
	openapiExternalDocumentationObject *ExternalDocs;
	Extensions extensions;
} openapiTagObject;

typedef struct openapiSchemaObject openapiSchemaObject;
typedef openapiSchemaObject openapiItemsObject;

typedef struct {
	Str Type, Format, Ref;
	bool XNullable;
	RawExample Example;
	openapiItemsObject *Items;
	Any Enum, Default;
} schemaCore;

typedef struct {
	Str Key;
	openapiSchemaObject *Value; /* keyVal.Value: always an openapiSchemaObject here */
} keyVal;

/* openapiSchemaObjectProperties ([]keyVal) */
typedef struct {
	keyVal *v;
	int n, cap;
} openapiSchemaObjectProperties;

enum { ADDPROP_NONE, ADDPROP_ALLOWED, ADDPROP_SCHEMA };

struct openapiSchemaObject {
	schemaCore core;
	openapiSchemaObjectProperties *Properties;
	/* openapiAdditionalProperties: nil, openapiAdditionalPropertiesAllowed(true) or a schema */
	int addKind;
	openapiSchemaObject *AdditionalProperties;
	Str Description, Title;
	openapiExternalDocumentationObject *ExternalDocs;
	bool ReadOnly;
	double MultipleOf, Maximum;
	bool ExclusiveMaximum;
	double Minimum;
	bool ExclusiveMinimum;
	uint64_t MaxLength, MinLength;
	Str Pattern;
	uint64_t MaxItems, MinItems;
	bool UniqueItems;
	uint64_t MaxProperties, MinProperties;
	GoStrs Required;
	Extensions extensions;
	Str *AllOf; /* allOfEntry.Ref; nil if nAllOf == 0 */
	int nAllOf;
};

typedef struct {
	Str Description, Type, Format;
	RawExample Default;
	Str Pattern;
} openapiHeaderObject;

typedef struct {
	Str key;
	openapiHeaderObject value;
} headerEntry;

/* openapiHeadersObject (map[string]openapiHeaderObject); nonnil: the map is not nil */
typedef struct {
	headerEntry *v;
	int n;
	bool nonnil;
} openapiHeadersObject;

/* map[string]interface{} of examples: RawExample (raw) or string */
typedef struct {
	Str key;
	bool raw;
	Str value;
} exampleEntry;

typedef struct {
	exampleEntry *v;
	int n;
} Examples;

typedef struct {
	Str Description;
	openapiSchemaObject Schema;
	Examples Examples;
	openapiHeadersObject Headers;
	Extensions extensions;
} openapiResponseObject;

/* openapiResponsesObject (map[string]openapiResponseObject); a Go map is a reference: the object is
 * shared through the pointer. */
typedef struct {
	Str key;
	openapiResponseObject value;
} responseEntry;
typedef struct {
	responseEntry *v;
	int n, cap;
} openapiResponsesObject;

typedef struct {
	Str Name, Description, In;
	bool Required, Deprecated;
	Str Type, Format;
	bool UniqueItems;
	openapiItemsObject *Items;
	Any Enum;
	Str CollectionFormat;
	Any Default;
	int *MinItems, *MaxItems;
	Str Pattern;
	RawExample XExample;
	openapiSchemaObject *Schema;
	Extensions extensions;
} openapiParameterObject;

typedef struct {
	openapiParameterObject *v;
	int n, cap;
} openapiParametersObject;

/* openapiSecurityRequirementObject (map[string][]string) */
typedef struct {
	Str key;
	GoStrs value;
} secReqEntry;
typedef struct {
	secReqEntry *v;
	int n;
} openapiSecurityRequirementObject;

typedef struct {
	openapiSecurityRequirementObject *v;
	int n;
	bool nonnil;
} SecurityRequirements;

typedef struct {
	Str Summary, Description, OperationID;
	openapiResponsesObject *Responses;
	openapiParametersObject Parameters;
	GoStrs Tags;
	bool Deprecated;
	GoStrs Consumes, Produces;
	SecurityRequirements *Security; /* *[]openapiSecurityRequirementObject */
	openapiExternalDocumentationObject *ExternalDocs;
	Extensions extensions;
} openapiOperationObject;

typedef struct {
	openapiOperationObject *Get, *Delete, *Post, *Put, *Patch, *Head, *Options;
} openapiPathItemObject;

typedef struct {
	Str Path;
	openapiPathItemObject PathItemObject;
} pathData;

/* openapiPathsObject ([]pathData) */
typedef struct {
	pathData *v;
	int n, cap;
} openapiPathsObject;

/* openapiScopesObject (map[string]string) */
typedef struct {
	Str key, value;
} scopeEntry;
typedef struct {
	scopeEntry *v;
	int n;
	bool nonnil;
} openapiScopesObject;

typedef struct {
	Str Type, Description, Name, In, Flow, AuthorizationURL, TokenURL;
	openapiScopesObject Scopes;
	Extensions extensions;
} openapiSecuritySchemeObject;

typedef struct {
	Str key;
	openapiSecuritySchemeObject value;
} secDefEntry;
/* openapiSecurityDefinitionsObject: a map (a reference: shared through the pointer) */
typedef struct {
	secDefEntry *v;
	int n, cap;
} openapiSecurityDefinitionsObject;

typedef struct {
	Str key;
	openapiSchemaObject value;
} defEntry;
/* openapiDefinitionsObject (map[string]openapiSchemaObject): a reference */
typedef struct {
	defEntry *v;
	int n, cap;
	Map index; /* key (hex) -> entry index + 1 */
} openapiDefinitionsObject;

typedef struct {
	openapiTagObject *v;
	int n;
} Tags;

typedef struct {
	Str Swagger;
	openapiInfoObject Info;
	Tags Tags;
	Str Host, BasePath;
	GoStrs Schemes, Consumes, Produces;
	openapiPathsObject Paths;
	openapiDefinitionsObject *Definitions;
	openapiSecurityDefinitionsObject *SecurityDefinitions;
	SecurityRequirements Security;
	openapiExternalDocumentationObject *ExternalDocs;
	Extensions extensions;
} openapiSwaggerObject;
