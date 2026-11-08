/* Port of the parts of @bufbuild/protobuf (protobuf-es v2.17.0, packages/protobuf/src) that code
 * generators need: the descriptor model (DescFile, DescMessage, ...) built from
 * FileDescriptorProto messages, feature resolution, names, nested types, embedding of file
 * descriptors and the symbols of generated code. Each .c file names its upstream source; the Go
 * port (git c9b2e00, pkg/languages/ecmascript/protobuf) has the same file split. */
#ifndef ES_PROTOBUF_H
#define ES_PROTOBUF_H

#include "pb.h"

enum DescKind { DK_FILE, DK_MESSAGE, DK_ENUM, DK_ENUM_VALUE, DK_FIELD, DK_ONEOF, DK_SERVICE, DK_METHOD };
enum FieldKind { FK_NONE, FK_SCALAR, FK_ENUM, FK_MESSAGE, FK_LIST, FK_MAP };
enum { ED_PROTO2 = 998, ED_PROTO3 = 999, ED_2023 = 1000, ED_2024 = 1001, ED_2026 = 1002, ED_UNSTABLE = 9999 };
/* The editions supported by @bufbuild/protobuf. */
enum { ED_MINIMUM = ED_PROTO2, ED_MAXIMUM = ED_2026 };
enum { PRESENCE_EXPLICIT = 1, PRESENCE_IMPLICIT = 2, PRESENCE_LEGACY_REQUIRED = 3 };

typedef struct DescFile DescFile;
typedef struct DescMessage DescMessage;
typedef struct DescEnum DescEnum;
typedef struct DescOneof DescOneof;

/* ---- descriptors.c (descriptors.ts) ---- */

/* Common header. The generated names are filled in per file by the plugin framework. */
typedef struct Desc {
	int kind;
	PbMsg *proto;
	bool deprecated;
	int index; /* index of proto in the parent's repeated field (source paths) */
	int pos;   /* index in the parent's list (file->messages, ...; pathInFileDesc) */
	const char *n_shape, *n_desc, *n_json, *n_valid;
} Desc;

struct DescFile {
	Desc h;
	int edition;
	const char *name; /* without .proto */
	Vec deps, enums, messages, extensions, services;
	bool names_done;
	bool locations_done;
	Map locations; /* protoplugin: SourceCodeInfo.Location by path ("4,0,2,1"), the first one */
};

struct DescEnum {
	Desc h;
	DescFile *file;
	DescMessage *parent;
	bool open;
	const char *name, *type_name, *shared_prefix;
	Vec values;
};

typedef struct {
	Desc h;
	DescEnum *parent;
	const char *name, *local_name;
	Str json_name;
	int32_t number;
} DescEnumValue;

struct DescMessage {
	Desc h;
	DescFile *file;
	DescMessage *parent;
	const char *name, *type_name;
	Vec fields, oneofs, members, nested_enums, nested_messages, nested_extensions;
};

/* A field or an extension. */
typedef struct {
	Desc h;
	const char *name, *local_name;
	Str json_name;
	int32_t number;
	int field_kind, presence;
	int scalar;           /* scalar fields, lists of scalars, maps with scalar values */
	DescMessage *message; /* message fields, lists of messages, maps with message values */
	DescEnum *enm;        /* enum fields, lists of enums, maps with enum values */
	int list_kind, map_kind, map_key;
	bool packed, delimited, long_as_string;
	DescMessage *parent; /* for extensions: the message it is declared in, or NULL */
	DescOneof *oneof;
	bool is_extension;
	DescFile *file;        /* extensions */
	const char *type_name; /* extensions */
	DescMessage *extendee; /* extensions */
} DescField;

struct DescOneof {
	Desc h;
	DescMessage *parent;
	Vec fields;
	const char *name, *local_name;
};

typedef struct {
	Desc h;
	DescFile *file;
	const char *name, *type_name;
	Vec methods;
} DescService;

typedef struct {
	Desc h;
	DescService *parent;
	const char *name, *local_name, *method_kind;
	DescMessage *input, *output;
} DescMethod;

const char *desc_kind(const Desc *d);   /* "file", "message", "extension", "rpc", ... */
const char *desc_string(const Desc *d); /* "message foo.Bar" */
DescFile *desc_file_of(const Desc *d);
const char *scalar_name(int scalar); /* "DOUBLE" ... */
PbMsg *desc_options(const Desc *d);
PbMsg *desc_features(const Desc *d); /* the FeatureSet of the options, or NULL */

/* ---- from_binary.c (from-binary.ts, wire/text-encoding.ts) ---- */

/* The string as a TextDecoder without fatal decodes it: every maximal subpart of an ill-formed
 * sequence becomes U+FFFD. Returns s itself if it is well-formed UTF-8. */
Str decode_utf8(Str s);
/* Makes a message parsed by pkg/pb what fromBinary of protobuf-es parses (strings decoded with
 * decode_utf8), recursively. Fails (fail()) on a known field with a mismatching wire type, which
 * upstream reads by its type. */
COLD void from_binary(PbMsg *m);

/* ---- registry.c (registry.ts) ---- */

typedef struct {
	Vec files;
	Map file_names, types;
} Registry;

/* Creates a registry from files in topological order. Fails (fail()) on invalid input, with
 * upstream's error message. */
Registry *registry_create(PbMsg **files, int n);
/* createFileRegistry of @bufbuild/protobuf up to at least 2.13.0 (protobuf-ts uses 2.5.2), which does
 * not validate names. */
Registry *registry_create_unchecked(PbMsg **files, int n);
/* createFileRegistry of an older @bufbuild/protobuf: check_names false up to at least
 * 2.13.0, max_edition
 * its maximumEdition (2.13.0: ED_2024; files of a later edition are unsupported). */
Registry *registry_create_opts(PbMsg **files, int n, bool check_names, int max_edition);
DescMessage *registry_message(Registry *r, const char *type_name);
/* Returns an error message for invalid names in the file, or NULL. */
COLD const char *assert_valid_names(const PbMsg *file);

/* ---- names.c (reflect/names.ts) ---- */

const char *proto_camel_case(const char *snake); /* names are ASCII identifiers */
const char *safe_object_property(const char *name);

/* ---- nested.c (reflect/nested-types.ts) ---- */

Vec nested_types(const Desc *d); /* messages, enums, services, extensions (file or message) */
Desc *desc_parent(const Desc *d);
bool desc_deprecated_with_parents(const Desc *d);

/* ---- symbols.c (codegenv2/symbols.ts, codegenv2/scalar.ts, wkt/wrappers.ts) ---- */

const char *scalar_ts_type(int scalar, bool long_as_string);
const char *scalar_json_type(int scalar);
bool is_wrapper_desc(const DescMessage *m);
const char *wkt_public_import_path(const char *proto_file_name); /* NULL if not a WKT */
const char *wkt_public_import_path_v2_13_0(const char *proto_file_name);

/* ---- embed.c (codegenv2/embed.ts) ---- */

/* The file descriptor without dependencies, source code info and default JSON names, serialized
 * and base64 encoded without padding. */
const char *embed_base64(const PbMsg *file);
/* google/protobuf/descriptor.proto as a plain object in JSON, which can be hydrated with boot()
 * from @bufbuild/protobuf/codegenv2 (createFileDescriptorProtoBoot). */
COLD const char *embed_boot(const PbMsg *file);
bool embed_bootable(const PbMsg *file);
/* The path to a message, enum, extension or service in its file descriptor. */
Vec path_in_file_desc(const Desc *d); /* (intptr_t) elements */

#endif
