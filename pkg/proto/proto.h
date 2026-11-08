/* pkg/proto: the descriptor model shared by the plugin libraries (docs/LAYOUT.md). It holds what
 * descriptor.proto and the editions specification define and every protobuf runtime computes the
 * same way:
 *
 *   - desc.c: files, messages, fields, oneofs (synthetic proto3 optional oneofs included), enums,
 *     enum values, services, methods and extension ranges, linked by pointers (type references,
 *     containing types, extension scopes), and the predicates of FieldDescriptor & co. (presence,
 *     packed, map entries, closed enums);
 *   - pool.c: a pool of files with the symbol table by full name, packages and the extensions of
 *     each message;
 *   - features.c: editions feature resolution over FeatureSetDefaults data passed by the library
 *     (GetEditionFeatureSetDefaults, MergeFeatures, the legacy features of proto2/proto3 fields);
 *   - message.c: the generic operations on options and features messages (MergeFrom, copies,
 *     setters) over pkg/pb PbMsg values with any PbMsgDef;
 *   - names.c: json_name and camelcase names (protoc's ToJsonName, ToCamelCase);
 *   - source.c: SourceCodeInfo locations by path and by descriptor (comments);
 *   - wkt.c: the well-known types.
 *
 * What differs between implementations stays in the libraries: building the model from
 * FileDescriptorProto messages with validation and error texts, name resolution rules, supported
 * editions and the edition defaults themselves, edition names, and derived names. The model is
 * the one of protoc (descriptor.h), the reference implementation; a library that needs more per
 * descriptor keeps it behind `user`.
 *
 * Memory comes from the pkg/core arena; nothing is freed. */
#ifndef PROTO_H
#define PROTO_H

#include "pb.h"

/* google.protobuf.Edition */
enum {
	PROTO_EDITION_UNKNOWN = 0, PROTO_EDITION_LEGACY = 900, PROTO_EDITION_PROTO2 = 998,
	PROTO_EDITION_PROTO3 = 999, PROTO_EDITION_2023 = 1000, PROTO_EDITION_2024 = 1001,
	PROTO_EDITION_2026 = 1002, PROTO_EDITION_UNSTABLE = 9999, PROTO_EDITION_MAX = 0x7FFFFFFF,
};

/* google.protobuf.FeatureSet: field numbers and values. */
enum {
	PROTO_FEATURE_FIELD_PRESENCE = 1, PROTO_FEATURE_ENUM_TYPE = 2,
	PROTO_FEATURE_REPEATED_FIELD_ENCODING = 3, PROTO_FEATURE_UTF8_VALIDATION = 4,
	PROTO_FEATURE_MESSAGE_ENCODING = 5, PROTO_FEATURE_JSON_FORMAT = 6,
	PROTO_FEATURE_ENFORCE_NAMING_STYLE = 7, PROTO_FEATURE_DEFAULT_SYMBOL_VISIBILITY = 8,
	PROTO_FEATURE_ENFORCE_PROTO_LIMITS = 9,
};
enum {
	PROTO_PRESENCE_EXPLICIT = 1, PROTO_PRESENCE_IMPLICIT = 2, PROTO_PRESENCE_LEGACY_REQUIRED = 3,
	PROTO_ENUM_TYPE_OPEN = 1, PROTO_ENUM_TYPE_CLOSED = 2,
	PROTO_REPEATED_PACKED = 1, PROTO_REPEATED_EXPANDED = 2,
	PROTO_UTF8_VERIFY = 2, PROTO_UTF8_NONE = 3,
	PROTO_MESSAGE_LENGTH_PREFIXED = 1, PROTO_MESSAGE_DELIMITED = 2,
	PROTO_JSON_ALLOW = 1, PROTO_JSON_LEGACY_BEST_EFFORT = 2,
};

/* Field numbers of descriptor.proto beyond pb.h that the model reads. */
enum {
	PROTO_FSD_DEFAULTS = 1, PROTO_FSD_MINIMUM_EDITION = 4, PROTO_FSD_MAXIMUM_EDITION = 5,
	PROTO_FSED_EDITION = 3, PROTO_FSED_OVERRIDABLE = 4, PROTO_FSED_FIXED = 5,
	PROTO_MSGOPT_MAP_ENTRY = 7, PROTO_FOPT_PACKED = 2,
	PROTO_LOC_SPAN = 2,
};

/* Descriptor kinds (ProtoDesc.kind); also the kinds of symbols, with the packages. */
typedef enum {
	PROTO_FILE = 1, PROTO_MESSAGE, PROTO_FIELD, PROTO_ONEOF, PROTO_ENUM, PROTO_ENUM_VALUE,
	PROTO_SERVICE, PROTO_METHOD, PROTO_EXTENSION_RANGE,
	PROTO_SYM_PACKAGE,     /* the package of a file (desc: that file) */
	PROTO_SYM_SUB_PACKAGE, /* a parent package of it (desc: NULL) */
} ProtoKind;

typedef struct ProtoPool ProtoPool;
typedef struct ProtoDesc ProtoDesc;
typedef struct ProtoFile ProtoFile;
typedef struct ProtoMessage ProtoMessage;
typedef struct ProtoField ProtoField;
typedef struct ProtoOneof ProtoOneof;
typedef struct ProtoEnum ProtoEnum;
typedef struct ProtoEnumValue ProtoEnumValue;
typedef struct ProtoService ProtoService;
typedef struct ProtoMethod ProtoMethod;
typedef struct ProtoExtensionRange ProtoExtensionRange;

/* The head of every descriptor; any descriptor converts to ProtoDesc *.
 *   kind: ProtoKind; index: in the parent's list of that kind; name, full_name: a file has its
 *   path and its package ("" if none), an extension range none (NULL); options: the *Options
 *   message as the descriptor holds it (the library decides what it moves out, protoc moves the
 *   features), NULL for the default instance; proto_features: the features set on the element
 *   (NULL: none); merged_features: the resolved FeatureSet (never NULL once the file is built);
 *   proto: the descriptor proto it was built from.
 * Every descriptor ends with `void *user`, free for the library; files, messages, enums and
 * services have an `id`, unique in the pool when the builder sets it (map keys). */
#define PROTO_DESC_HEAD                                                                            \
	uint8_t kind;                                                                              \
	int index;                                                                                 \
	const char *name, *full_name;                                                              \
	PbMsg *options, *proto_features, *merged_features;                                         \
	PbMsg *proto

struct ProtoDesc {
	PROTO_DESC_HEAD;
};

typedef struct {
	int start, end; /* messages: [start, end); enums: [start, end] */
} ProtoReservedRange;

struct ProtoFile {
	PROTO_DESC_HEAD;
	const char *package; /* = full_name */
	ProtoPool *pool;
	int edition;         /* PROTO2 and PROTO3 for files with syntax */
	ProtoFile **dependencies;
	int n_dependencies;
	int *public_dependencies, n_public_dependencies; /* indexes into dependencies */
	int *weak_dependencies, n_weak_dependencies;
	const char **option_dependencies;
	int n_option_dependencies;
	ProtoMessage *messages;
	int n_messages;
	ProtoEnum *enums;
	int n_enums;
	ProtoService *services;
	int n_services;
	ProtoField *extensions;
	int n_extensions;
	PbMsg *source_code_info; /* NULL if absent */
	int id;
	void *user;
	Map locations;           /* source.c: path -> the first Location with it, built on first use */
	bool locations_built;
	Map fields_by_camelcase; /* desc.c: "<scope>/<camelcase name>" -> field, built on first use */
	bool fields_by_camelcase_built;
};

struct ProtoMessage {
	PROTO_DESC_HEAD;
	ProtoFile *file;
	ProtoMessage *containing_type;
	ProtoField *fields;
	int n_fields;
	ProtoOneof *oneofs;
	int n_oneofs, n_real_oneofs; /* the real (not synthetic) oneofs come first */
	ProtoMessage *nested;
	int n_nested;
	ProtoEnum *enums;
	int n_enums;
	ProtoExtensionRange *extension_ranges;
	int n_extension_ranges;
	ProtoField *extensions;
	int n_extensions;
	ProtoReservedRange *reserved_ranges;
	int n_reserved_ranges;
	const char **reserved_names;
	int n_reserved_names;
	int visibility; /* SymbolVisibility */
	int id;
	void *user;
};

struct ProtoField {
	PROTO_DESC_HEAD;
	const char *json_name;     /* explicit, or proto_json_name(name) */
	ProtoFile *file;
	int number, type, label;   /* type: PT_*; label: LABEL_*, after the features (LEGACY_REQUIRED
				    * makes LABEL_REQUIRED, DELIMITED makes PT_GROUP) */
	bool is_extension, has_json_name, proto3_optional;
	ProtoMessage *containing_type;  /* fields: the message; extensions: the extendee */
	ProtoOneof *containing_oneof;
	ProtoMessage *extension_scope;  /* extensions declared in a message */
	ProtoMessage *message_type;     /* message and group fields */
	ProtoEnum *enum_type;
	bool has_default_value;
	union {
		int64_t i;  /* int32, int64, sint*, sfixed* */
		uint64_t u; /* uint32, uint64, fixed* */
		double d;
		float f;
		bool b;
	} default_value;
	Str default_value_string;       /* string and bytes (bytes: unescaped) */
	ProtoEnumValue *default_value_enum;
	void *user;
};

struct ProtoOneof {
	PROTO_DESC_HEAD;
	ProtoMessage *containing_type;
	int first_field, n_fields; /* containing_type->fields[first_field...] */
	void *user;
};

struct ProtoEnum {
	PROTO_DESC_HEAD;
	ProtoFile *file;
	ProtoMessage *containing_type;
	ProtoEnumValue *values;
	int n_values;
	ProtoReservedRange *reserved_ranges;
	int n_reserved_ranges;
	const char **reserved_names;
	int n_reserved_names;
	int visibility;
	int id;
	void *user;
};

struct ProtoEnumValue {
	PROTO_DESC_HEAD; /* full_name: a sibling of the enum (C++ scoping) */
	int number;
	ProtoEnum *type;
	void *user;
};

struct ProtoService {
	PROTO_DESC_HEAD;
	ProtoFile *file;
	ProtoMethod *methods;
	int n_methods;
	int id;
	void *user;
};

struct ProtoMethod {
	PROTO_DESC_HEAD;
	ProtoService *service;
	ProtoMessage *input_type, *output_type;
	bool client_streaming, server_streaming;
	void *user;
};

struct ProtoExtensionRange {
	PROTO_DESC_HEAD;
	int start, end; /* [start, end) */
	ProtoMessage *containing_type;
	void *user;
};

/* ---- desc.c ---- */

/* The parent of a descriptor: the file for top-level ones, the message, the enum (values), the
 * service (methods), the extension scope or file (extensions); NULL for a file. */
ProtoDesc *proto_desc_parent(const ProtoDesc *d);
ProtoFile *proto_desc_file(const ProtoDesc *d);

/* IsTypePackable: scalar types (not string, bytes, message, group). */
bool proto_type_is_packable(int type);
bool proto_field_is_repeated(const ProtoField *f);
bool proto_field_is_required(const ProtoField *f);
bool proto_field_is_packable(const ProtoField *f);
bool proto_field_is_packed(const ProtoField *f);
bool proto_field_has_presence(const ProtoField *f);
bool proto_field_is_map(const ProtoField *f);
bool proto_field_requires_utf8_validation(const ProtoField *f);
/* proto3 optional: the `optional` keyword in a proto3 file. */
bool proto_field_has_optional_keyword(const ProtoField *f);
/* The oneof of a field unless it is synthetic. */
ProtoOneof *proto_field_real_oneof(const ProtoField *f);
bool proto_message_is_map_entry(const ProtoMessage *m);
/* The field with this number or name, or NULL (linear: messages are small). */
ProtoField *proto_message_field_by_number(const ProtoMessage *m, int number);
ProtoField *proto_message_field_by_name(const ProtoMessage *m, const char *name);
/* Descriptor::FindFieldByCamelcaseName: the field of m whose camelcase name
 * (proto_camelcase_name) is camelcase, or NULL. Fields and extensions declared in m share the
 * name space; on a collision the one with the smallest number wins, and NULL is returned if that
 * is an extension. A table per file, built on first use. */
ProtoField *proto_message_field_by_camelcase_name(const ProtoMessage *m, const char *camelcase);
/* The first extension range that contains number, or NULL. */
const ProtoExtensionRange *proto_message_extension_range(const ProtoMessage *m, int number);
bool proto_enum_is_closed(const ProtoEnum *e);
/* The first value with that number or name, or NULL. */
ProtoEnumValue *proto_enum_value_by_number(const ProtoEnum *e, int64_t number);
ProtoEnumValue *proto_enum_value_by_name(const ProtoEnum *e, const char *name);
bool proto_oneof_is_synthetic(const ProtoOneof *o);
/* A field of a oneof by its index in the oneof. */
static inline ProtoField *proto_oneof_field(const ProtoOneof *o, int i) { return &o->containing_type->fields[o->first_field + i]; }

/* ---- pool.c ---- */

/* A symbol of the pool: a descriptor or a package. */
typedef struct {
	uint8_t kind; /* ProtoKind */
	void *desc;
	ProtoFile *file;
	const char *full_name;
} ProtoSymbol;

struct ProtoPool {
	Map files;           /* name -> ProtoFile* */
	Vec file_list;       /* in the order they were added */
	Map symbols;         /* full name -> ProtoSymbol* */
	Map extensions;      /* "<extendee id>:<number>" -> ProtoField* */
	Map extension_lists; /* extendee full name -> Vec* of ProtoField*, in the order they were added */
	int next_id;
};

ProtoSymbol *proto_symbol_new(int kind, void *desc, ProtoFile *file, const char *full_name);
void proto_pool_add_file(ProtoPool *pool, ProtoFile *file);
ProtoFile *proto_pool_find_file(const ProtoPool *pool, const char *name);
ProtoSymbol *proto_pool_find_symbol(const ProtoPool *pool, const char *full_name);
/* Adds the symbol unless its full name is taken; returns whether it was added. */
bool proto_pool_add_symbol(ProtoPool *pool, ProtoSymbol *s);
ProtoMessage *proto_pool_find_message(const ProtoPool *pool, const char *full_name);
ProtoEnum *proto_pool_find_enum(const ProtoPool *pool, const char *full_name);
/* Registers an extension unless its extendee and number are taken; returns whether it was
 * added. An extension without extendee (containing_type NULL, an invalid input) takes the
 * number but is in no list. */
bool proto_pool_add_extension(ProtoPool *pool, ProtoField *ext);
ProtoField *proto_pool_find_extension(const ProtoPool *pool, const ProtoMessage *extendee, int number);
/* The extensions of a message in the order they were added; NULL if none. */
const Vec *proto_pool_extensions(const ProtoPool *pool, const char *extendee_full_name);
/* Symbol kind predicates (NULL is none of them). */
bool proto_symbol_is_type(const ProtoSymbol *s);
bool proto_symbol_is_package(const ProtoSymbol *s);
/* A type, a package or a service: a symbol that can contain others. */
bool proto_symbol_is_aggregate(const ProtoSymbol *s);
/* Whether a file's package is package_name or a subpackage of it. */
bool proto_file_in_package(const ProtoFile *file, const char *package_name);

/* ---- message.c: options and features messages (any PbMsgDef) ---- */

/* The index of a field in a definition (binary search), or -1. */
int proto_msg_field_index(const PbMsgDef *def, uint32_t num);
void proto_slot_push(PbSlot *s, PbVal v);
/* No field set and no unknown fields (NULL is empty). */
bool proto_msg_is_empty(const PbMsg *m);
/* A deep copy (NULL: NULL). */
PbMsg *proto_msg_clone(const PbMsg *m);
/* Message::MergeFrom for messages without oneofs (options, features): set scalars overwrite,
 * singular messages merge, repeated fields and unknown fields append. src may be NULL. */
void proto_msg_merge(PbMsg *dst, const PbMsg *src);
void proto_msg_clear(PbMsg *m, uint32_t num);
/* Setters by field number; the field must exist in the definition (else fail()). */
void proto_msg_set_u(PbMsg *m, uint32_t num, uint64_t v);
void proto_msg_add_u(PbMsg *m, uint32_t num, uint64_t v);
void proto_msg_set_s(PbMsg *m, uint32_t num, Str v);
void proto_msg_set_cs(PbMsg *m, uint32_t num, const char *v);
void proto_msg_add_s(PbMsg *m, uint32_t num, Str v);
void proto_msg_set_msg(PbMsg *m, uint32_t num, PbMsg *v); /* v is not copied */
PbMsg *proto_msg_mutable(PbMsg *m, uint32_t num);         /* mutable_<field>() */
PbMsg *proto_msg_add(PbMsg *m, uint32_t num);             /* add_<field>() */

/* ---- features.c ---- */

/* The value of an enum or bool field of a FeatureSet (0 if unset or features is NULL). */
int proto_feature(const PbMsg *features, uint32_t number);
/* A FeatureSetEditionDefault entry as a FeatureSet: fixed_features merged with
 * overridable_features (both use feature_set_def, which may know extensions). */
PbMsg *proto_edition_default_features(const PbMsg *entry, const PbMsgDef *feature_set_def);
/* GetEditionFeatureSetDefaults: the entry of defaults (a FeatureSetDefaults message) with the
 * latest edition <= edition (entries are in increasing order), or NULL. */
const PbMsg *proto_edition_defaults_entry(const PbMsg *defaults, int edition);
/* The FeatureSet of an edition (proto_edition_default_features of its entry), or NULL. */
PbMsg *proto_edition_features(const PbMsg *defaults, int edition, const PbMsgDef *feature_set_def);
/* MergeFeatures: a copy of edition_features with parent, then child merged into it. */
PbMsg *proto_merge_features(const PbMsg *edition_features, const PbMsg *parent, const PbMsg *child);
/* InferLegacyProtoFeatures of a field in a proto2/proto3 file (edition < 2023): required is
 * LEGACY_REQUIRED, a group DELIMITED, [packed = true] PACKED and, in proto3,
 * [packed = false] EXPANDED. Sets them in features. */
void proto_infer_legacy_field_features(PbMsg *features, const PbMsg *field_proto, const PbMsg *options, int edition);
/* The features a field inherits: its oneof's, its extension scope's (message or file), or its
 * message's (descriptor.cc). */
const PbMsg *proto_field_parent_features(const ProtoField *f);
/* The definition of FeatureSetDefaults over a FeatureSet definition (a new one per call), and
 * the parse of serialized FeatureSetDefaults data as runtimes embed it
 * (protoc --edition_defaults_out). Returns NULL on malformed data. */
const PbMsgDef *proto_feature_set_defaults_def(const PbMsgDef *feature_set_def);
PbMsg *proto_parse_feature_set_defaults(Str data, const PbMsgDef *feature_set_def);

/* ---- names.c ---- */

/* ToJsonName (descriptor.cc): underscores are dropped and the character after one is upper-cased
 * (ASCII). protobuf-es protoCamelCase gives the same; protobuf-go JSONCamelCase differs. */
char *proto_json_name(const char *name);
/* ToCamelCase (descriptor.cc): like proto_json_name, then the first character is lower-cased
 * (lower_first) or upper-cased (ASCII). FieldDescriptor::camelcase_name() is
 * proto_to_camel_case(name, true). */
char *proto_to_camel_case(const char *name, bool lower_first);

/* ---- source.c ---- */

/* The path of a descriptor in its FileDescriptorProto (SourceCodeInfo.Location.path); *n
 * receives its length. A file has the empty path. */
int32_t *proto_desc_source_path(const ProtoDesc *d, int *n);
/* The first SourceCodeInfo.Location of the file with exactly this path, or NULL. */
const PbMsg *proto_file_location(ProtoFile *f, const int32_t *path, int n);
/* The location of a descriptor (proto_file_location of its source path), or NULL. */
const PbMsg *proto_desc_location(const ProtoDesc *d);

/* ---- wkt.c ---- */

/* The well-known types: the messages and enums of the google/protobuf/ files of the
 * protobuf runtimes besides descriptor.proto (any, api, duration, empty, field_mask,
 * source_context, struct, timestamp, type, wrappers). */
typedef enum {
	PROTO_WKT_NONE, PROTO_WKT_ANY, PROTO_WKT_API, PROTO_WKT_BOOL_VALUE, PROTO_WKT_BYTES_VALUE,
	PROTO_WKT_DOUBLE_VALUE, PROTO_WKT_DURATION, PROTO_WKT_EMPTY, PROTO_WKT_ENUM, PROTO_WKT_ENUM_VALUE,
	PROTO_WKT_FIELD, PROTO_WKT_FIELD_CARDINALITY, PROTO_WKT_FIELD_KIND, PROTO_WKT_FIELD_MASK,
	PROTO_WKT_FLOAT_VALUE, PROTO_WKT_INT32_VALUE, PROTO_WKT_INT64_VALUE, PROTO_WKT_LIST_VALUE,
	PROTO_WKT_METHOD, PROTO_WKT_MIXIN, PROTO_WKT_NULL_VALUE, PROTO_WKT_OPTION,
	PROTO_WKT_SOURCE_CONTEXT, PROTO_WKT_STRING_VALUE, PROTO_WKT_STRUCT, PROTO_WKT_SYNTAX,
	PROTO_WKT_TIMESTAMP, PROTO_WKT_TYPE, PROTO_WKT_UINT32_VALUE, PROTO_WKT_UINT64_VALUE,
	PROTO_WKT_VALUE,
} ProtoWkt;

/* The well-known type with this full name ("google.protobuf.Any"), or PROTO_WKT_NONE. */
ProtoWkt proto_wkt(const char *full_name);
/* Its full name ("" for NONE) and the file that declares it ("google/protobuf/any.proto"). */
const char *proto_wkt_full_name(ProtoWkt w);
const char *proto_wkt_file(ProtoWkt w);
/* Whether a file name is one of the well-known type files. */
bool proto_is_wkt_file(const char *file_name);
/* The wrappers of wrappers.proto (DoubleValue ... BytesValue). */
bool proto_wkt_is_wrapper(ProtoWkt w);

#endif
