/* Copyright (c) 2025-2026 Buf Technologies, Inc.
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
 * limitations under the License. */

/* protobuf-py v0.7.0 (https://github.com/bufbuild/protobuf-py, PyPI protobuf-py-pure 0.7.0,
 * package `protobuf`): the parts that protoc plugins built on it rely on.
 *
 *   - schema.c: descriptor.proto and plugin.proto as protobuf-py knows them, and the names of its
 *     descriptor.proto classes for repr() (mkschema.py);
 *   - wire.c: Message.from_binary and to_binary over pkg/pb messages (_wire/_binary_reader.py,
 *     _binary_writer.py, _from_binary.py, _to_binary.py, the required-field check of
 *     _validate.py);
 *   - descriptors.c: the descriptor model and the registry (_descriptors.py, _file_registry.py,
 *     _registry.py, _bootstrap.py, _names.py);
 *   - text_format.c: the bytes default values of _wire/_text_format.py;
 *   - sanitization.c: _sanitization.py;
 *   - plugin/: the protoc plugin framework protobuf.plugin (run, options, schema, the generated
 *     file with its imports, identifiers, map_imports, source code info).
 *
 * Exceptions become fail() with the last line of the Python traceback ("ValueError: ..."). */
#ifndef PROTOBUF_PY_H
#define PROTOBUF_PY_H

#include "pb.h"
#include "python.h"

/* ---- schema.c ---- */

/* Label of the required fields in the schema tables (beside PL_OPT, PL_REP, PL_PACKED). */
#define PBPY_REQUIRED 3

extern const PbMsgDef pbpy_FileDescriptorProto, pbpy_DescriptorProto, pbpy_FieldDescriptorProto,
	pbpy_EnumValueOptions, pbpy_CodeGeneratorRequest;

/* The names of protobuf-py's descriptor.proto classes, for repr() of their messages: the class
 * __qualname__, per field (in the order of the def) the attribute name, repr() of the value of an
 * unset singular scalar or enum field, the enum class and the index of the message type in
 * pbpy_descriptor_info (-1: not a message); per enum the member names. The message type comes
 * from the field, never from the address of a PbMsgDef: -fmerge-all-constants merges defs with
 * the same fields (DescriptorProto.ReservedRange and EnumDescriptorProto.EnumReservedRange). */
typedef struct {
	int32_t number;
	const char *name;
} PbpyEnumName;
typedef struct {
	const char *qualname;
	int n;
	const PbpyEnumName *v;
} PbpyEnumInfo;
typedef struct {
	const char *local_name, *default_repr;
	const PbpyEnumInfo *enm;
	int msg;
} PbpyFieldInfo;
typedef struct {
	const PbMsgDef *def;
	const char *qualname;
	const PbpyFieldInfo *f;
} PbpyMsgInfo;
/* ends with a NULL def */
extern const PbpyMsgInfo pbpy_descriptor_info[];

/* ---- wire.c ---- */

/* Nesting limit of messages and groups (_wire/_binary_reader.py DEPTH_LIMIT). */
#define PBPY_DEPTH_LIMIT 100

/* Message.from_binary: parses data as a def message, failing like protobuf-py on malformed input
 * (fields that overrun their message are read from the enclosing data, as protobuf-py does).
 * Strings must be valid UTF-8. Unknown fields are kept, grouped by number. */
PbMsg *pbpy_from_binary(const PbMsgDef *def, Str data);
/* Message.to_binary: the fields in declaration order (skipping field skip_num of m itself, 0 for
 * none), then the unknown fields by number in the order of their first occurrence. Fails for an
 * unset required field. */
void pbpy_to_binary(const PbMsg *m, uint32_t skip_num, Buf *out);
/* The unknown fields of m with the given number, each tag + value as protobuf-py stores them. */
int pbpy_unknown_fields(const PbMsg *m, uint32_t num, Str **out);
/* get_unknown_field of a message-typed extension: merges one stored unknown field (tag + value)
 * into m, read as a length-delimited message whatever its wire type. */
void pbpy_merge_unknown_message(PbMsg *m, Str record);

/* ---- text_format.c ---- */

/* _text_format._c_unescape: the bytes of a bytes field's default value. */
COLD Str pbpy_c_unescape(Str text);

/* ---- descriptors.c ---- */

/* google.protobuf.Edition values used by the model. */
enum { PBPY_EDITION_PROTO2 = 998, PBPY_EDITION_PROTO3 = 999, PBPY_EDITION_UNSTABLE = 9999 };
/* Supported editions (_bootstrap.py _MINIMUM_EDITION, _MAXIMUM_EDITION). */
enum { PBPY_MINIMUM_EDITION = 998, PBPY_MAXIMUM_EDITION = 1002 };

typedef enum {
	PBPY_FILE = 1, PBPY_MESSAGE, PBPY_FIELD, PBPY_ONEOF, PBPY_ENUM, PBPY_ENUM_VALUE, PBPY_EXTENSION,
	PBPY_SERVICE, PBPY_METHOD,
} PbpyKind;

/* SupportedFieldPresence */
enum { PBPY_EXPLICIT = 1, PBPY_IMPLICIT = 2, PBPY_LEGACY_REQUIRED = 3 };
/* The kinds of DescFieldValue. Scalar types are the PT_* numbers (ScalarType). */
enum { PBPY_VALUE_SCALAR = 1, PBPY_VALUE_MESSAGE, PBPY_VALUE_ENUM, PBPY_VALUE_MAP, PBPY_VALUE_LIST };

typedef struct PbpyDescFile PbpyDescFile;
typedef struct PbpyDescMessage PbpyDescMessage;
typedef struct PbpyDescEnum PbpyDescEnum;
typedef struct PbpyDescOneof PbpyDescOneof;

/* An element of a list or the value of a map: a scalar (scalar != 0), a message or an enum. */
typedef struct {
	int scalar;
	PbpyDescMessage *message;
	PbpyDescEnum *enm;
} PbpyElement;

/* DescFieldValue */
typedef struct {
	int kind;                 /* PBPY_VALUE_* */
	int scalar;               /* SCALAR: the type; MAP: the key type */
	PbpyDescMessage *message; /* MESSAGE */
	PbpyDescEnum *enm;        /* ENUM */
	PbpyElement element;      /* LIST: the element; MAP: the value */
	PbpyDescOneof *oneof;     /* SCALAR, MESSAGE, ENUM: the oneof (not a synthetic one) */
	bool delimited_encoding, packed, packable;
	/* SCALAR and ENUM: the default value (FieldDescriptorProto.default_value parsed like
	 * _text_format.py), if set. default_str: the str or bytes value; default_repr: repr() of an int
	 * or float value. */
	bool has_default, default_bool;
	Str default_str;
	const char *default_repr;
	int32_t default_enum;
} PbpyFieldValue;

/* The descriptors start with their kind. index: the position in the list of the parent proto
 * (for source code info paths). Names cannot contain NUL (a name with NUL is unsupported). */
typedef struct {
	PbpyKind kind;
} PbpyDesc;

struct PbpyDescFile {
	PbpyKind kind;
	int edition;
	const char *name;
	Vec dependencies, enums, messages, extensions, services; /* PbpyDescFile *, ... */
	bool deprecated;
	PbMsg *proto;
	void *loc; /* source code info locations by path (plugin/source_code_info.c) */
	const char *module_pb[2]; /* Module.for_desc(self, "_pb", escape_with_hash) (cache) */
};

struct PbpyDescEnum {
	PbpyKind kind;
	int index;
	const char *type_name, *name;
	PbpyDescFile *file;
	PbpyDescMessage *parent;
	bool open, deprecated;
	Vec values; /* PbpyDescEnumValue * */
	PbMsg *proto;
	const char *local_name, *local_qualname;
	Map values_by_number; /* decimal number -> the last value with it */
	Map values_by_name;   /* name -> the first value with it */
};

typedef struct {
	PbpyKind kind;
	int index;
	const char *name, *local_name;
	PbpyDescEnum *parent;
	int32_t number;
	bool deprecated;
	PbMsg *proto;
} PbpyDescEnumValue;

struct PbpyDescMessage {
	PbpyKind kind;
	int index;
	const char *type_name, *name;
	PbpyDescFile *file;
	PbpyDescMessage *parent;
	/* fields; oneofs (no synthetic ones, none without fields); members (fields outside oneofs and
	 * oneofs, in declaration order); nested enums, messages (no map entries), extensions */
	Vec fields, oneofs, members, nested_enums, nested_messages, nested_extensions;
	bool deprecated;
	PbMsg *proto;
	const char *local_name, *local_qualname;
};

typedef struct {
	PbpyKind kind;
	int index;
	const char *name, *local_name;
	PbpyFieldValue value;
	PbpyDescMessage *parent;
	int32_t number;
	Str json_name;
	bool deprecated;
	int presence;
	PbMsg *proto;
} PbpyDescField;

typedef struct {
	PbpyKind kind;
	int index;
	const char *name;
	PbpyFieldValue value; /* not MAP */
	const char *type_name;
	PbpyDescFile *file;
	PbpyDescMessage *parent, *extendee;
	int32_t number;
	const char *json_name;
	bool deprecated;
	int presence;
	PbMsg *proto;
} PbpyDescExtension;

struct PbpyDescOneof {
	PbpyKind kind;
	int index;
	const char *name, *local_name;
	PbpyDescMessage *parent;
	Vec fields; /* PbpyDescField * */
	PbMsg *proto;
};

typedef struct {
	PbpyKind kind;
	int index;
	const char *type_name, *name;
	PbpyDescFile *file;
	Vec methods; /* PbpyDescMethod * */
	bool deprecated;
	PbMsg *proto;
} PbpyDescService;

typedef struct {
	PbpyKind kind;
	int index;
	const char *name;
	const PbpyDescService *parent;
	PbpyDescMessage *input, *output;
	bool client_streaming, server_streaming, deprecated;
	PbMsg *proto;
} PbpyDescMethod;

/* Registry: files by name and types (messages, enums, extensions, services) by type name; the
 * last one added wins. */
typedef struct {
	Map files, types;
} PbpyRegistry;

PbpyDescFile *pbpy_registry_file(const PbpyRegistry *r, const char *name);
/* The type with this name if it is of the given kind, or NULL. */
void *pbpy_registry_type(const PbpyRegistry *r, const char *type_name, PbpyKind kind);
/* _file_registry.add_file: builds the descriptors of proto (its dependencies must be in reg). */
COLD PbpyDescFile *pbpy_add_file(PbpyRegistry *reg, PbMsg *proto);
/* _file_registry._get_file_edition */
COLD int pbpy_file_edition(const PbMsg *proto);
/* The name of a descriptor of any kind as declared (DescFile: its path). */
const char *pbpy_desc_name(const void *desc);
/* The value of a field or extension. */
const PbpyFieldValue *pbpy_field_value(const void *field);
/* ScalarType.name.lower() of a scalar type ("double", ..., "sint64"). */
const char *pbpy_scalar_name(int scalar);

/* _names.py proto_camel_case */
COLD char *pbpy_proto_camel_case(const char *snake);

/* ---- sanitization.c ---- */

/* escape_proto_module_component: one component of a module path; escape_with_hash appends a hash
 * of the original when characters were replaced. */
COLD char *pbpy_escape_proto_module_component(const char *ident, bool escape_with_hash);
COLD char *pbpy_escape_public_identifier(const char *ident, const char *const *reserved);
COLD char *pbpy_escape_message_attr(const char *attr);
COLD char *pbpy_escape_enum_attr(const char *attr);
COLD char *pbpy_escape_class_name(const char *name);
COLD char *pbpy_escape_extension_name(const char *name);
COLD char *pbpy_escape_identifier(const char *ident);
/* python_keywords(): NULL-terminated */
extern const char *const pbpy_python_keywords[];

/* ---- plugin/ident.c ---- */

/* Ident: a name in a module. A module path that starts with "." is relative to the generation
 * root. desc is the descriptor the ident was derived from (Ident._desc), if any. Two idents are
 * equal when their names and modules are. */
typedef struct {
	const char *name, *module;
	bool type_only;
	const void *desc;
} PbpyIdent;

PbpyIdent *pbpy_ident(const char *module, const char *name, bool type_only);
/* Module.module: a child module (the name escaped like escape_identifier). */
const char *pbpy_module_child(const char *module, const char *name);
/* Module.for_desc */
const char *pbpy_module_for_desc(const PbpyDescFile *desc, const char *suffix, bool escape_with_hash);
/* Ident.for_desc of a file, message, enum or extension. */
PbpyIdent *pbpy_ident_for_desc(const void *desc, bool type_only, bool escape_module_with_hash);

/* ---- plugin/file.c ---- */

/* The elements of File.print(): text (PS: NUL-terminated, PSN: Str), an ident, a descriptor (file, message, enum, extension:
 * the imported symbol), a scalar type (its Python type name), repr() of an int, bool or bytes,
 * or a nested list (flattened). */
typedef enum { PE_END = 0, PE_STR, PE_STRN, PE_IDENT, PE_DESC, PE_SCALAR, PE_INT, PE_BOOL, PE_BYTES, PE_LIST } PbpyElKind;
typedef struct PbpyEls PbpyEls;
typedef struct {
	PbpyElKind kind;
	union {
		const char *s;
		Str str;
		const PbpyIdent *ident;
		const void *desc;
		int scalar;
		int64_t i;
		bool b;
		Str bytes;
		const PbpyEls *list;
	} u;
} PbpyEl;
struct PbpyEls {
	PbpyEl *v;
	int n, cap;
};
void pbpy_els_push(PbpyEls *l, PbpyEl e);

#define PS(x) ((PbpyEl){PE_STR, {.s = (x)}})
#define PSN(x) ((PbpyEl){PE_STRN, {.str = (x)}})
#define PI(x) ((PbpyEl){PE_IDENT, {.ident = (x)}})
#define PD(x) ((PbpyEl){PE_DESC, {.desc = (x)}})
#define PT(x) ((PbpyEl){PE_SCALAR, {.scalar = (x)}})
#define PN(x) ((PbpyEl){PE_INT, {.i = (x)}})
#define PB(x) ((PbpyEl){PE_BOOL, {.b = (x)}})
#define PX(x) ((PbpyEl){PE_BYTES, {.bytes = (x)}})
#define PL(x) ((PbpyEl){PE_LIST, {.list = (x)}})
#define PBPY_ELS(...) ((const PbpyEl[]){__VA_ARGS__, {PE_END, {0}}})

typedef struct PbpySchema PbpySchema;
typedef struct PbpyFile PbpyFile;

/* File.path */
const char *pbpy_file_path(const PbpyFile *f);
/* File.print(*els); els ends with PE_END. pbpy_println is print() without arguments. */
void pbpy_print_v(PbpyFile *f, const PbpyEl *els);
#define pbpy_print(f, ...) pbpy_print_v(f, PBPY_ELS(__VA_ARGS__))
void pbpy_println(PbpyFile *f);
/* File.ident */
PbpyIdent *pbpy_file_ident(PbpyFile *f, const char *name, bool type_only);
/* with File.scope(*els): ... -> pbpy_scope(f, ...); ...; pbpy_end(f) */
void pbpy_scope_v(PbpyFile *f, const PbpyEl *els);
#define pbpy_scope(f, ...) pbpy_scope_v(f, PBPY_ELS(__VA_ARGS__))
void pbpy_end(PbpyFile *f);
/* with File.type_checking(): ... -> pbpy_type_checking(f); ...; pbpy_type_checking_end(f) */
void pbpy_type_checking(PbpyFile *f);
void pbpy_type_checking_end(PbpyFile *f);
/* with File.doc(*els): ... -> pbpy_doc_v(f, els) (els may be PBPY_ELS of nothing: NULL); ...;
 * pbpy_doc_end(f) */
void pbpy_doc_v(PbpyFile *f, const PbpyEl *els);
#define pbpy_doc(f, ...) pbpy_doc_v(f, PBPY_ELS(__VA_ARGS__))
void pbpy_doc_end(PbpyFile *f);
/* File.preamble */
void pbpy_preamble(PbpyFile *f, const PbpyDescFile *desc);

/* ---- plugin/source_code_info.c ---- */

/* DescComments.lines() of get_comments(desc) for an enum, enum value, extension, field, message,
 * method, oneof or service. */
int pbpy_comment_lines(const void *desc, Str **out);

/* ---- plugin/options.c, plugin/run.c, plugin/schema.c ---- */

/* A field of an options dataclass: bool, str, dict[str, str] or a StrEnum. */
typedef enum { PBPY_OPT_BOOL, PBPY_OPT_STR, PBPY_OPT_DICT_STR, PBPY_OPT_STR_ENUM } PbpyOptType;
/* A member of a StrEnum: its name and value. */
typedef struct {
	const char *name, *value;
} PbpyOptMember;
typedef struct {
	const char *name;
	PbpyOptType type;
	bool default_bool;
	/* STR: the default (NULL: required); STR_ENUM: the value of the default member (NULL: None if
	 * optional, else required) */
	const char *default_str;
	/* STR_ENUM: the class name, its members (in definition order, ending with a NULL name) and
	 * whether the type is `X | None` */
	const char *enum_name;
	const PbpyOptMember *members;
	bool optional;
} PbpyOptDef;
typedef struct {
	bool b;
	const char *s; /* STR; STR_ENUM: the value of the member (NULL: None) */
	Vec keys, values; /* DICT_STR, in insertion order */
} PbpyOptValue;

typedef struct {
	const char *name, *version;
	/* The plugin options dataclass (n_options fields), or none (n_options = -1: no options
	 * accepted). */
	const PbpyOptDef *options;
	int n_options;
	void (*generate)(PbpySchema *schema, const PbpyOptValue *options);
	/* run(minimum_edition=, maximum_edition=); 0: the runtime's supported editions */
	int minimum_edition, maximum_edition;
} PbpyPlugin;

/* plugin.run: parses the raw request (a parse error is returned: the plugin exits with an
 * exception), takes the parameter from param (the request's parameter without protoc-gen-uni's
 * options), generates and writes the serialized CodeGeneratorResponse to out (a generation error
 * becomes CodeGeneratorResponse.error). */
const char *pbpy_run(const PbpyPlugin *p, Str raw, Str param, Buf *out);

/* Schema.files_to_generate, Schema.all_files (PbpyDescFile *) */
const Vec *pbpy_files_to_generate(const PbpySchema *s);
const Vec *pbpy_all_files(const PbpySchema *s);
/* Schema.generate_file(desc, suffix) and Schema.generate_file(path) */
PbpyFile *pbpy_generate_file_desc(PbpySchema *s, const PbpyDescFile *desc, const char *suffix);
PbpyFile *pbpy_generate_file_path(PbpySchema *s, const char *path);
/* Whether generate_file was called with this path. */
bool pbpy_schema_has_file(const PbpySchema *s, const char *path);

#endif
