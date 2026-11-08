/* Copyright (c) 2014 - 2025 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* A port of the parts of SwiftProtobufPluginLibrary (swift-protobuf 1.39.0,
 * Sources/SwiftProtobufPluginLibrary) that protoc-gen-swift and the Connect-Swift plugins use: the
 * descriptor model with feature resolution (descriptor.c), naming (naming.c, namer.c), CodePrinter
 * (printer.c), source comments (comments.c) and the plugin driver CodeGenerator (plugin.c).
 * Older versions differ only in their bundled edition defaults (edition_defaults_<version>.c).
 *
 * Strings: the request is checked to be valid UTF-8 (SwiftProtobuf rejects anything else), and
 * Swift's Unicode scalar loops become loops over UTF-8 sequences. Names (file names, packages,
 * type names, swift_prefix) are NUL-terminated ASCII: Unicode case mapping is ASCII only and
 * strings compare by bytes (Swift compares by canonical equivalence), so a non-ASCII or NUL name
 * fails generation as unsupported (sw_ascii_name); protoc and buf only produce ASCII names. Text
 * that may contain NUL or non-ASCII (comments, default values, generated code) is Str. */
#ifndef SWIFT_PROTOBUF_H
#define SWIFT_PROTOBUF_H

#include "pb.h"

/* google.protobuf.Edition */
enum {
	SW_EDITION_UNKNOWN = 0,
	SW_EDITION_LEGACY = 900,
	SW_EDITION_PROTO2 = 998,
	SW_EDITION_PROTO3 = 999,
	SW_EDITION_2023 = 1000,
	SW_EDITION_2024 = 1001,
	SW_EDITION_2026 = 1002,
	SW_EDITION_UNSTABLE = 9999,
};

/* Resolved google.protobuf.FeatureSet: the values indexed by field number. */
enum {
	SW_FEAT_FIELD_PRESENCE = 1,
	SW_FEAT_ENUM_TYPE = 2,
	SW_FEAT_REPEATED_FIELD_ENCODING = 3,
	SW_FEAT_UTF8_VALIDATION = 4,
	SW_FEAT_MESSAGE_ENCODING = 5,
	SW_FEAT_MAX = 9,
};
enum {
	SW_PRESENCE_EXPLICIT = 1, SW_PRESENCE_IMPLICIT = 2, SW_PRESENCE_LEGACY_REQUIRED = 3,
	SW_ENUM_TYPE_OPEN = 1, SW_ENUM_TYPE_CLOSED = 2,
	SW_REPEATED_PACKED = 1, SW_REPEATED_EXPANDED = 2,
	SW_UTF8_VERIFY = 2, SW_UTF8_NONE = 3,
	SW_ENCODING_LENGTH_PREFIXED = 1, SW_ENCODING_DELIMITED = 2,
};
typedef struct {
	int32_t v[SW_FEAT_MAX + 1];
} SwFeatures;

typedef struct SwFile SwFile;
typedef struct SwMessage SwMessage;
typedef struct SwEnum SwEnum;
typedef struct SwEnumValue SwEnumValue;
typedef struct SwOneof SwOneof;
typedef struct SwField SwField;
typedef struct SwService SwService;
typedef struct SwMethod SwMethod;

typedef struct {
	int32_t start, end; /* end exclusive (enum reserved ranges: inclusive) */
} SwRange;

/* ---- descriptor.c (Descriptor.swift, FeatureResolver.swift, Descriptor+Extensions.swift) ---- */

/* PluginLibEditionDefaults.swift: bundledFeatureSetDefaults, decoded (fixed and overridable
 * features merged per edition, ascending; an unset feature is 0). */
typedef struct {
	int32_t edition;
	SwFeatures features;
} SwEditionDefault;
typedef struct {
	int32_t minimum, maximum; /* minimumEdition, maximumEdition */
	int n;
	const SwEditionDefault *defaults;
} SwEditionDefaults;
/* swift-protobuf 1.39.0 (descriptor.c): PROTO2 to 2026. */
extern const SwEditionDefaults sw_edition_defaults;
/* swift-protobuf 1.37.0 (edition_defaults_1.37.0.c): PROTO2 to 2024. */
extern const SwEditionDefaults sw_edition_defaults_1_37_0;
/* swift-protobuf 1.38.1 (edition_defaults_1.38.1.c): PROTO2 to 2024. */
extern const SwEditionDefaults sw_edition_defaults_1_38_1;

struct SwFile {
	const char *name, *package;
	int32_t edition;
	SwFeatures features;
	PbMsg *proto, *options;
	Vec dependencies, public_dependencies; /* SwFile * */
	Vec enums, messages, extensions, services;
	Map locations; /* path ("1,2,3") -> SourceCodeInfo.Location, built on first use */
	bool locations_built;
	bool deprecated;
};

struct SwMessage {
	const char *name, *full_name;
	int index;
	SwFile *file;
	SwMessage *containing_type;
	SwFeatures features;
	PbMsg *options;
	bool is_any; /* wellKnownType == .any */
	bool map_entry, message_set_wire_format, deprecated;
	Vec enums, messages, fields, oneofs, real_oneofs, extensions;
	SwRange *extension_ranges; /* messageExtensionRanges */
	int n_extension_ranges;
	SwRange *reserved_ranges;
	int n_reserved_ranges;
	Vec reserved_names; /* const char * */
};

struct SwEnum {
	const char *name, *full_name;
	int index;
	SwFile *file;
	SwMessage *containing_type;
	SwFeatures features;
	PbMsg *options;
	Vec values;
	bool deprecated;
};

struct SwEnumValue {
	const char *name, *full_name;
	int index;
	int32_t number;
	SwFeatures features;
	PbMsg *options;
	SwEnum *enum_type;
	bool deprecated;
	/* The pb.enumvalue.json option (protoc-gen-swift's json_enumvalue_options.proto, extension
	 * 998 of EnumValueOptions): json is its `string` field. */
	bool has_json;
	const char *json;
};

struct SwOneof {
	const char *name;
	int index;
	SwFeatures features;
	PbMsg *options;
	SwMessage *containing_type;
	Vec fields; /* the fields with this oneof_index */
	bool is_synthetic;
};

struct SwField {
	const char *name, *json_name;
	int index;
	SwFeatures features;
	SwFile *file;
	bool is_extension;
	int32_t number;
	int type;  /* FieldDescriptorProto.Type after the feature remapping (PT_*) */
	int label; /* LABEL_* after the feature remapping */
	bool has_default;
	Str default_value;
	SwMessage *containing_type; /* the extendee for extensions */
	bool has_oneof_index;
	int32_t oneof_index;
	SwMessage *extension_scope; /* extensions declared in a message */
	SwMessage *message_type;
	SwEnum *enum_type;
	PbMsg *options;
	bool proto3_optional, deprecated;
	const char *type_name, *extendee; /* resolved by bind */
};

struct SwService {
	const char *name, *full_name;
	int index;
	SwFile *file;
	SwFeatures features;
	PbMsg *options;
	Vec methods;
	bool deprecated;
};

struct SwMethod {
	const char *name;
	int index;
	SwService *service;
	SwFeatures features;
	PbMsg *options;
	SwMessage *input_type, *output_type;
	bool client_streaming, server_streaming, deprecated;
};

typedef struct {
	Vec files;
	Map file_map, message_map, enum_map, service_map; /* registry; message keys are ".full.name" */
} SwDescriptorSet;

/* DescriptorSet(protos:) with the bundled feature set defaults of a version (NULL:
 * sw_edition_defaults). Fails on input that upstream rejects with a fatal error (unknown types,
 * files out of dependency order, an edition out of the defaults' range, ...). */
SwDescriptorSet *sw_descriptor_set(PbMsg *req, const SwEditionDefaults *defaults);
/* String.debugDescription (how errors print their String associated values): NULL if s has a
 * non-ASCII character (Swift escapes some of those by grapheme break rules, not ported). */
char *sw_debug_description(Str s);
/* Names are NUL-terminated ASCII in the port: a non-ASCII character or a NUL byte in s fails with
 * "unsupported by protoc-gen-uni: ... <what> <s>". Returns s.p. */
const char *sw_ascii_name(Str s, const char *what);
/* s as a Swift string literal (\0, \", \\, \u{..} for controls), for error messages. */
char *sw_quote(Str s);
SwFile *sw_file_named(SwDescriptorSet *set, const char *name);

bool sw_enum_is_closed(const SwEnum *e);
bool sw_field_is_required(const SwField *f);
bool sw_field_is_repeated(const SwField *f);
bool sw_field_is_packable_type(int type);
bool sw_field_is_packed(const SwField *f);
bool sw_field_is_map(const SwField *f);
bool sw_field_has_presence(const SwField *f);
SwOneof *sw_field_containing_oneof(const SwField *f);
SwOneof *sw_field_real_containing_oneof(const SwField *f);
char *sw_field_full_name(const SwField *f);
/* The name used for naming: the message name of group-like fields. */
const char *sw_field_naming_base(const SwField *f);
bool sw_field_is_group_like(const SwField *f);
/* Descriptor.mapKeyAndValue: false if the message is not a map entry. */
bool sw_message_map_key_value(const SwMessage *m, SwField **key, SwField **value);

/* ---- comments.c (ProvidesSourceCodeLocation, ProvidesDeprecationComment,
 * Google_Protobuf_SourceCodeInfo+Extensions, Descriptor+Extensions location paths) ---- */

typedef enum {
	SW_FILE, SW_MESSAGE, SW_ENUM, SW_ENUM_VALUE, SW_ONEOF, SW_FIELD, SW_SERVICE, SW_METHOD,
} SwKind;

/* FileDescriptor.sourceCodeInfoLocation(path:): NULL if there is none. */
PbMsg *sw_file_location(SwFile *file, const int32_t *path, int n);
/* Location.asSourceComment(commentPrefix:leadingDetachedPrefix:); detached_prefix may be NULL. */
Str sw_as_source_comment(const PbMsg *loc, const char *prefix, const char *detached_prefix);
/* protoSourceComments of a descriptor (d is the Sw* of kind k). */
Str sw_proto_source_comments(SwKind k, const void *d, const char *prefix, const char *detached_prefix);
/* deprecationComment(commentPrefix:) */
Str sw_deprecation_comment(SwKind k, const void *d, const char *prefix);
/* protoSourceCommentsWithDeprecation */
Str sw_proto_source_comments_with_deprecation(SwKind k, const void *d, const char *prefix, const char *detached_prefix);

/* ---- naming.c (NamingUtils.swift, SwiftLanguage.swift, UnicodeScalar+Extensions.swift) ---- */

bool sw_is_swift_identifier_head(uint32_t c);
bool sw_is_swift_identifier_char(uint32_t c);
bool sw_is_valid_swift_identifier(const char *s, bool allow_quoted);
/* NamingUtils.typePrefix(protoPackage:fileOptions:) */
char *sw_type_prefix(const char *package, const PbMsg *file_options);
/* NamingUtils.PrefixStripper(prefix:).strip(from:): NULL if it can't be stripped. */
char *sw_strip_prefix(const char *prefix, const char *from);
/* sanitize(messageName:/enumName:/oneofName:forbiddenTypeNames: [forbidden]) */
char *sw_sanitize_message_name(const char *s, const char *forbidden);
char *sw_sanitize_enum_name(const char *s, const char *forbidden);
char *sw_sanitize_oneof_name(const char *s, const char *forbidden);
char *sw_sanitize_field_name(const char *s, const char *based_on);
char *sw_sanitize_enum_case_name(const char *s);
char *sw_sanitize_message_scoped_extension_name(const char *s);
char *sw_uppercase_first(const char *s);
char *sw_to_upper_camel(const char *s);
char *sw_to_lower_camel(const char *s);
char *sw_trim_backticks(const char *s);
char *sw_periods_to_underscores(const char *s);
char *sw_to_json_field_name(const char *s);

/* ---- namer.c (SwiftProtobufNamer.swift, ProtoFileToModuleMappings.swift, SwiftProtobufInfo.swift) ---- */

bool sw_is_bundled_proto(const SwFile *file);

typedef struct {
	Map mappings; /* proto file path -> module name */
	bool has_mappings;
	const char *swift_protobuf_module_name;
} SwModuleMappings;

/* ProtoFileToModuleMappings(swiftProtobufModuleName:) (NULL: "SwiftProtobuf"). */
SwModuleMappings *sw_module_mappings(const char *swift_protobuf_module_name);
/* Adds one ModuleMappings.Entry; returns NULL or the LoadError description. */
const char *sw_module_mappings_add(SwModuleMappings *m, int index, const char *module_name, const Vec *proto_file_paths);
const char *sw_module_name_for_file(const SwModuleMappings *m, const SwFile *file);
/* String.debugDescription (how errors print their String associated values). */
char *sw_swift_debug_quote(const char *s);

typedef struct {
	SwModuleMappings *mappings;
	const char *target_module;
	Map file_prefix_cache, enum_value_cache;
} SwNamer;

SwNamer *sw_namer(const SwFile *file, SwModuleMappings *mappings);
const char *sw_namer_swift_protobuf_module_name(const SwNamer *n);
const char *sw_namer_swift_protobuf_module_prefix(const SwNamer *n);
char *sw_relative_name_message(SwNamer *n, const SwMessage *m);
char *sw_full_name_message(SwNamer *n, const SwMessage *m);
char *sw_relative_name_enum(SwNamer *n, const SwEnum *e);
char *sw_full_name_enum(SwNamer *n, const SwEnum *e);
const char *sw_relative_name_enum_value(SwNamer *n, const SwEnumValue *v);
char *sw_full_name_enum_value(SwNamer *n, const SwEnumValue *v);
char *sw_dotted_relative_name_enum_value(SwNamer *n, const SwEnumValue *v);
char *sw_relative_name_oneof(SwNamer *n, const SwOneof *o);
char *sw_full_name_oneof(SwNamer *n, const SwOneof *o);
char *sw_relative_name_extension(SwNamer *n, const SwField *f);
char *sw_full_name_extension(SwNamer *n, const SwField *f);

typedef struct {
	char *name, *prefixed, *has, *clear;
} SwFieldNames;
SwFieldNames sw_message_property_names(SwNamer *n, const SwField *f, const char *prefixed, bool include_has_and_clear);
SwFieldNames sw_message_property_name_oneof(SwNamer *n, const SwOneof *o, const char *prefixed);
/* messagePropertyNames(extensionField:): name = value. */
SwFieldNames sw_message_property_names_extension(SwNamer *n, const SwField *f);
const char *sw_namer_type_prefix(SwNamer *n, const SwFile *file);
const char *sw_namer_module_prefix(SwNamer *n, const SwFile *file);

/* ---- printer.c (CodePrinter.swift) ---- */

typedef struct {
	Buf content;
	int indent;       /* in units of the single indent */
	int indent_width; /* the single indent: that many spaces */
	bool at_line_start, newlines;
} SwPrinter;

/* CodePrinter(addNewlines:) */
void sw_printer_init(SwPrinter *p, bool newlines);
/* CodePrinter(indent:addNewlines:) with an indent of width spaces. */
void sw_printer_init_indent(SwPrinter *p, int width, bool newlines);
/* CodePrinter(parent): same indentation and newlines, empty content. */
void sw_printer_init_from(SwPrinter *p, const SwPrinter *parent);
/* print(t) with the printer's newlines setting; sw_print_nl with an explicit one. */
void sw_print(SwPrinter *p, const char *s);
void sw_print_str(SwPrinter *p, Str s);
void sw_print_nl(SwPrinter *p, Str s, bool newline);
void sw_printf(SwPrinter *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* print() with no arguments: a newline. */
void sw_print_blank(SwPrinter *p);
/* printIndented(t) */
void sw_print_indented(SwPrinter *p, const char *s);
void sw_indent(SwPrinter *p);
void sw_outdent(SwPrinter *p);
void sw_append(SwPrinter *p, const SwPrinter *other, bool indenting);
bool sw_printer_is_empty(const SwPrinter *p);

/* ---- plugin.c (CodeGenerator.swift, CodeGeneratorParameter.swift) ---- */

typedef struct {
	Str key, value;
} SwParamPair;

/* InternalCodeGeneratorParameter.parsedPairs */
int sw_parse_parameter(Str parameter, SwParamPair **pairs);

typedef struct {
	Vec names, contents; /* char *, Str * */
	Map seen;            /* the names */
} SwOutputs;
/* GeneratorOutputs.add(fileName:contents:): fails on a duplicate name. */
void sw_outputs_add(SwOutputs *o, const char *name, Str content);

typedef struct {
	/* SupportedFeatures and the edition range of the response. */
	uint64_t supported_features;
	int32_t minimum_edition, maximum_edition;
	/* The bundled edition defaults of the library version (NULL: sw_edition_defaults). */
	const SwEditionDefaults *edition_defaults;
	/* Throws with fail(); the message becomes CodeGeneratorResponse.error. */
	void (*generate)(SwFile **files, int n_files, Str parameter, SwOutputs *outputs);
} SwCodeGenerator;

/* Checks what SwiftProtobuf checks when it decodes the request: UTF-8 of every string field and
 * required fields, and of the pb.enumvalue.json option when it is one of the generator's
 * customOptionExtensions (enum_value_json). Returns NULL or the decoding error name. */
const char *sw_check_request(const PbMsg *req, bool enum_value_json);
/* generateCode(request:generator:): writes the serialized CodeGeneratorResponse. */
void sw_generate_code(PbMsg *req, const SwCodeGenerator *gen, Buf *out);

/* ---- strings ---- */

/* Decodes the scalar at s (valid UTF-8, NUL-terminated); returns its length. */
int sw_next_scalar(const char *s, size_t n, uint32_t *c);
/* CharacterSet.whitespaces / .whitespacesAndNewlines / .newlines membership. */
bool sw_is_whitespace(uint32_t c);
bool sw_is_whitespace_or_newline(uint32_t c);
bool sw_is_newline(uint32_t c);
/* trimmingCharacters(in: .whitespacesAndNewlines) */
Str sw_trim_whitespace(Str s);
/* String.lowercased() for ASCII. */
char *sw_ascii_lower(const char *s);

#endif
