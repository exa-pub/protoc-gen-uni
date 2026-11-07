/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* The parts of the protobuf-go v1.36.12 runtime that compiler/protogen relies on, with the same
 * semantics:
 *
 *   - wire.c: proto.Unmarshal / proto.MarshalOptions{Deterministic: true} of descriptor protos
 *     with extensions (the pb.go feature extension linked into protoc-gen-go, and dynamicpb
 *     extensions declared in the request's files), dynamic messages, maps and groups; and the
 *     retention=SOURCE stripping of protoc-gen-go (protorange over the message tree).
 *   - schema.c: the message tables of descriptor.proto, plugin.proto and go_features.proto as
 *     compiled into protobuf-go v1.36.12 (descriptorpb, pluginpb, gofeaturespb).
 *   - desc.c, validate.c: reflect/protodesc (NewFile: name resolution, editions features, JSON
 *     names, default values, validation), internal/filedesc (HasPresence, IsPacked, ...) and the
 *     protoregistry.Files registration.
 *
 * Messages are pkg/pb PbMsg values (the accessors of pb.h work on them) whose definition is a
 * GoMsgDef. Unlike pkg/pb, parsing follows protobuf-go: closed enums keep unknown values,
 * extensions and dynamic messages are typed, and serialization is deterministic Go order
 * (extensions first). */
#ifndef GO_PROTODESC_H
#define GO_PROTODESC_H

#include "gofeatures.h"
#include "pb.h"

/* ---- wire.c, schema.c ---- */

typedef struct GoDesc GoDesc;
typedef struct GoFile GoFile;
typedef struct GoMessage GoMessage;
typedef struct GoField GoField;
typedef struct GoOneof GoOneof;
typedef struct GoEnum GoEnum;
typedef struct GoEnumValue GoEnumValue;
typedef struct GoService GoService;
typedef struct GoMethod GoMethod;

/* Per-field properties beyond PbField. */
enum {
	GOF_EXT = 1,        /* an extension field */
	GOF_IMPLICIT = 2,   /* no presence: zero values are not serialized (dynamic messages) */
	GOF_MAP = 4,        /* a map field (msg is the map entry) */
	GOF_SOURCE = 8,     /* [retention = RETENTION_SOURCE] */
	GOF_UTF8 = 16,      /* string with UTF-8 validation */
};

/* A message definition: pb.def points here (PbMsg.def == &GoMsgDef.pb). */
typedef struct GoMsgDef GoMsgDef;
struct GoMsgDef {
	PbMsgDef pb;            /* fields sorted by number */
	const char *full_name;
	const uint8_t *flags;   /* per field: GOF_* */
	const int *oneof;       /* per field: index of the containing (non-synthetic) oneof + 1, or 0 */
	const char **names;     /* per field: full name (for errors), may be NULL */
	const int *order;       /* serialization order (indexes into pb.f) */
	bool dynamic;           /* dynamicpb message: extensions only within ranges */
	bool has_ext;           /* has extension ranges */
	const GoMessage *desc;  /* dynamic: the descriptor (extension ranges) */
};

/* A set of message definitions: descriptor.proto, plugin.proto and go_features.proto, the
 * extension fields resolved by it, and the dynamic messages they need. */
typedef struct GoSchema GoSchema;

/* The schema of protoc-gen-go: protoregistry.GlobalTypes (only the pb.go extension). Built on
 * every call (arena memory). */
GoSchema *gopb_schema_base(void);
/* The base schema plus dynamic extension types of xs (n extension descriptors, in registration
 * order; an extension whose extendee/number is already taken locally is an error reported by
 * the caller), as protogen's extensionRegistry resolves them. */
GoSchema *gopb_schema_with_extensions(GoField *const *xs, int n);
/* The definition of a message of the schema by full name ("google.protobuf.FileDescriptorProto",
 * "google.protobuf.compiler.CodeGeneratorRequest", ...), or NULL. */
const GoMsgDef *gopb_schema_msg(const GoSchema *s, const char *full_name);

/* proto.Unmarshal (merges into m). Returns NULL or the error text ("proto: ..."). */
const char *gopb_decode(PbMsg *m, const uint8_t *p, size_t n);
/* proto.MarshalOptions{AllowPartial: true, Deterministic: true}.Marshal. */
void gopb_encode(const PbMsg *m, Buf *out);
/* Clears every populated field declared with [retention = RETENTION_SOURCE], recursively
 * (protoc-gen-go's stripSourceRetentionFieldsFromMessage). */
void gopb_strip_source_retention(PbMsg *m);
/* proto.Clone */
PbMsg *gopb_clone(const PbMsg *m);
/* Clears one field. */
void gopb_clear(PbMsg *m, uint32_t num);

/* ---- desc.c: descriptors ---- */

/* protoreflect.Syntax */
enum { GO_PROTO2 = 2, GO_PROTO3 = 3, GO_EDITIONS = 99 };
/* protoreflect.Cardinality */
enum { GO_OPTIONAL = 1, GO_REQUIRED = 2, GO_REPEATED = 3 };
/* protoreflect.Kind: the numbers of FieldDescriptorProto.Type (PT_* of pb.h). */

typedef enum { GD_FILE, GD_MESSAGE, GD_FIELD, GD_ONEOF, GD_ENUM, GD_ENUM_VALUE, GD_SERVICE, GD_METHOD } GoDescKind;

/* protoreflect.Descriptor */
struct GoDesc {
	GoDescKind kind;
	const char *name, *full_name; /* a file: its path and package */
	int index;                    /* in the parent's list of that kind */
	GoDesc *parent;               /* NULL for files; enum values: the enum */
	GoFile *file;
	PbMsg *options;               /* the *Options message, NULL if unset */
	void *user;                   /* free for users (protogen) */
};

typedef struct {
	GoFile *file;
	bool is_public, is_weak;
} GoImport;

/* A source location (protoreflect.SourceLocation). */
typedef struct {
	const int32_t *path;
	int n_path;
	Str leading, trailing;
	Str *detached;
	int n_detached;
} GoSourceLocation;

struct GoFile {
	GoDesc d; /* d.name: path (File.Path), d.full_name: package */
	PbMsg *proto;
	int syntax, edition;
	GoImport *imports;
	int n_imports;
	GoEnum **enums;
	int n_enums;
	GoMessage **messages;
	int n_messages;
	GoField **extensions;
	int n_extensions;
	GoService **services;
	int n_services;
	GoEditionFeatures features;
	GoSourceLocation *locations;
	int n_locations;
	Map locs_by_path; /* key: path encoded as text -> GoSourceLocation* (the first one) */
};

struct GoMessage {
	GoDesc d;
	PbMsg *proto;
	bool is_map_entry, is_message_set;
	int32_t (*ext_ranges)[2]; /* [start, end) */
	int n_ext_ranges;
	GoField **fields;
	int n_fields;
	GoOneof **oneofs;
	int n_oneofs;
	GoEnum **enums;
	int n_enums;
	GoMessage **messages;
	int n_messages;
	GoField **extensions;
	int n_extensions;
	GoEditionFeatures features;
	int visibility;
};

typedef union {
	bool b;
	int64_t i; /* int32, int64, enum kinds */
	uint64_t u;
	double f;  /* float values are stored converted from float32 */
	Str s;     /* string, bytes */
} GoValue;

struct GoField {
	GoDesc d; /* fields: parent is the message; extensions: the declaring file or message */
	PbMsg *proto;
	int32_t number;
	int cardinality, kind;
	bool is_extension, is_proto3_optional, is_lazy, has_json_name;
	Str json_name;              /* JSONName(): explicit, JSONCamelCase, or "[full.name]" */
	GoMessage *containing;      /* ContainingMessage(): the parent message or the extendee */
	GoOneof *oneof;             /* ContainingOneof() */
	GoEnum *enum_type;          /* Enum() */
	GoMessage *message_type;    /* Message() */
	bool has_default;
	GoValue default_value;
	GoEnumValue *default_enum;  /* DefaultEnumValue() */
	GoEditionFeatures features;
};

struct GoOneof {
	GoDesc d;
	GoField **fields;
	int n_fields;
	GoEditionFeatures features;
};

struct GoEnum {
	GoDesc d;
	GoEnumValue **values;
	int n_values;
	GoEditionFeatures features;
	int visibility;
};

struct GoEnumValue {
	GoDesc d;
	int32_t number;
};

struct GoService {
	GoDesc d;
	GoMethod **methods;
	int n_methods;
};

struct GoMethod {
	GoDesc d;
	GoMessage *input, *output;
	bool client_streaming, server_streaming;
};

/* filedesc methods */
static inline int go_field_syntax(const GoField *f) { return f->d.file->syntax; }
static inline bool go_field_is_map(const GoField *f) { return f->message_type && f->message_type->is_map_entry; }
static inline bool go_field_is_list(const GoField *f) { return f->cardinality == GO_REPEATED && !go_field_is_map(f); }
bool go_field_has_presence(const GoField *f);
bool go_field_is_packed(const GoField *f);
bool go_field_has_optional_keyword(const GoField *f);
static inline bool go_enum_is_closed(const GoEnum *e) { return !e->features.IsOpenEnum; }
bool go_oneof_is_synthetic(const GoOneof *o);
/* EnumValues.ByNumber / ByName: the first value with that number or name, or NULL. */
GoEnumValue *go_enum_value_by_number(const GoEnum *e, int32_t n);
GoEnumValue *go_enum_value_by_name(const GoEnum *e, const char *name);
/* SourceLocations.ByPath / ByDescriptor; an empty location if none. */
GoSourceLocation go_source_location_by_path(const GoFile *f, const int32_t *path, int n);
GoSourceLocation go_source_location_by_desc(const GoFile *f, const GoDesc *d);
/* The source path of a declaration (as ByDescriptor computes it); *n receives its length. */
int32_t *go_desc_source_path(const GoDesc *d, int *n);
/* Kind names as protoreflect.Kind.String() ("int32", "message", ...). */
const char *go_kind_string(int kind);
/* Cardinality names ("optional", "required", "repeated"). */
const char *go_cardinality_string(int c);
/* descriptorpb.Edition.String() ("EDITION_2023", or the number). */
const char *go_edition_string(int edition);

/* protoregistry.Files with protodesc.NewFile. */
typedef struct GoRegistry GoRegistry;
GoRegistry *go_registry_new(void);
/* protodesc.NewFile(fd, r) followed by r.RegisterFile: returns the file or NULL and the error
 * text in *err (protodesc's "proto: ..." errors, or the registry's). */
GoFile *go_registry_new_file(GoRegistry *r, PbMsg *fd, const char **err);
GoFile *go_registry_find_file(const GoRegistry *r, const char *path);

#endif
