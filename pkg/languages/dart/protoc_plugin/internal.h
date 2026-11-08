/* Internal header of the protoc_plugin 25.1.0 port (unity build, see protoc_plugin.c).
 *
 * Files mirror protoc_plugin/lib: names.c (names.dart), options.c (src/options.dart), ...; the
 * Dart classes become structs with the same names and the same field and method names
 * (MessageGenerator.generateConstants -> msg_generateConstants).
 *
 * Output: upstream prints text through IndentingWriter and formats it with dart_style; here every
 * printed declaration is built as dart_style nodes (dart_style.h), placed in the order and with
 * the line structure upstream prints (Out below), then linearized (the unformatted text of the
 * `generate_kythe_info` option) and formatted. Descriptors are pkg/pb messages with the schemas
 * of pkg/languages/dart/protobuf. */
#ifndef PROTOC_PLUGIN_INTERNAL_H
#define PROTOC_PLUGIN_INTERNAL_H

#include <stdarg.h>
#include <string.h>

#include "dart_path.h"
#include "dart_protobuf.h"
#include "dart_std.h"
#include "dart_style.h"
#include "protoc_plugin.h"

/* Field numbers of descriptor.proto beyond pb.h. */
enum {
	SDP_OPTIONS = 3, DP_RESERVED_NAME = 10, DP_NESTED_TYPE_TAG = 3, DP_ENUM_TYPE_TAG = 4,
	MO_MESSAGE_SET_WIRE_FORMAT = 1, MO_DEPRECATED = 3, MO_MAP_ENTRY = 7, MO_FEATURES = 12,
	FO_FEATURES = 21, FILEO_FEATURES = 50, EO_DEPRECATED = 3, EO_FEATURES = 7, EVO_DEPRECATED = 1,
	OO_FEATURES = 1, SO_DEPRECATED = 33, MTHO_DEPRECATED = 33,
	TYPE_DOUBLE = 1, TYPE_FLOAT = 2, TYPE_INT64 = 3, TYPE_UINT64 = 4, TYPE_INT32 = 5, TYPE_FIXED64 = 6,
	TYPE_FIXED32 = 7, TYPE_BOOL = 8, TYPE_STRING = 9, TYPE_GROUP = 10, TYPE_MESSAGE = 11, TYPE_BYTES = 12,
	TYPE_UINT32 = 13, TYPE_ENUM = 14, TYPE_SFIXED32 = 15, TYPE_SFIXED64 = 16, TYPE_SINT32 = 17,
	TYPE_SINT64 = 18,
	EDITION_UNKNOWN = 0, EDITION_PROTO2 = 998, EDITION_PROTO3 = 999, EDITION_2023 = 1000,
	FS_FIELD_PRESENCE = 1, FS_REPEATED_FIELD_ENCODING = 3, FS_MESSAGE_ENCODING = 5,
	LEGACY_REQUIRED = 3, PACKED = 1, EXPANDED = 2, DELIMITED = 2,
};

typedef struct {
	int *v;
	int n;
} IntList;

/* FeatureSet: the values of fields 1..8 (0: not set). Extensions do not matter here. */
typedef struct {
	int v[9];
} Features;

/* GenerationOptions */
typedef struct {
	bool useGrpc, generateMetadata, disableConstructorArgs;
} Options;

/* A Dart type as protoc_plugin spells it: [prefix.]name[<args>][?]. */
typedef struct DType DType;
struct DType {
	const char *prefix, *name;
	const DType *args[2];
	int nargs;
};

typedef struct FileGen FileGen;
typedef struct MsgGen MsgGen;
typedef struct EnumGen EnumGen;
typedef struct ExtGen ExtGen;
typedef struct Field Field;
typedef struct PbMixin PbMixin;
typedef struct ServiceGen ServiceGen;

/* ProtobufContainer */
typedef enum { PC_FILE, PC_MESSAGE, PC_ENUM } PcKind;
typedef struct Pc Pc;
struct Pc {
	PcKind kind;
	const char *classname, *fullName, *package;
	IntList fieldPath;
	FileGen *fileGen;
	Pc *parent;
	Features features;
};

/* names.dart */
typedef struct {
	PbMsg *descriptor;
	int index, sourcePosition; /* -1 for extensions */
	const char *fieldName, *hasMethodName, *clearMethodName, *ensureMethodName;
} FieldNames;

typedef struct {
	PbMsg *descriptor;
	int index;
	const char *whichOneofMethodName, *clearMethodName, *oneofEnumName, *byTagMapName;
} OneofNames;

/* mixins.dart */
struct PbMixin {
	const char *name, *importFrom;
	const PbMixin *parent;
	const char *const *reservedNames;
	const char *wellKnownType;
	int helper; /* index of the injected helpers (well_known_types.c), -1: none */
};

/* base_type.dart */
typedef struct {
	int descriptor;
	const char *typeConstantSuffix;
	DType unprefixed;
	const char *setter;
	Pc *generator;
} BaseType;

/* protobuf_field.dart */
struct Field {
	PbMsg *descriptor;
	FieldNames *memberNames; /* NULL for extensions */
	const char *fullName;
	BaseType baseType;
	Pc *parent;
	Features features;
};

struct EnumGen {
	Pc pc;
	PbMsg *descriptor;
	Vec canonicalValues;   /* EnumValueDescriptorProto */
	IntList originalCanonicalIndices;
	Vec aliases;           /* EnumValueDescriptorProto pairs: value, canonical */
	IntList originalAliasIndices;
	Map dartNames;         /* value name -> Dart name */
};

struct MsgGen {
	Pc pc;
	PbMsg *descriptor;
	const PbMixin *mixin;
	Vec enumGenerators, messageGenerators, extensionGenerators;
	Vec *oneofFields; /* per real oneof: Field* */
	Features *oneofFeatures;
	int noneofs;
	OneofNames **oneofNames;
	int noneofNames;
	Vec fieldList; /* Field* */
	bool resolved;
	Map *usedTopLevelNames;
};

struct ExtGen {
	PbMsg *descriptor;
	Pc *parent;
	Field *field;
	bool resolved;
	const char *extensionName, *extendedFullName;
	IntList fieldPath;
};

/* service_generator.dart, client_generator.dart, grpc_generator.dart */
struct ServiceGen {
	PbMsg *descriptor;
	FileGen *fileGen;
	Map deps, transitiveDeps, undefinedDeps; /* insertion ordered through the key vectors */
	Vec depKeys, transitiveKeys, undefinedKeys;
	const char *classname;
	int serviceIndex;
	/* grpc */
	const char *fullServiceName, *clientClassname, *serviceClassname;
	Vec methods;
};

typedef struct {
	ServiceGen *service;
	const char *className;
	Map usedMethodNames;
	int repeatedFieldIndex;
} ClientApiGen;

struct FileGen {
	Pc pc;
	PbMsg *descriptor;
	const Options *options;
	const char *protoFileUri;
	Vec enumGenerators, messageGenerators, extensionGenerators, clientApiGenerators, serviceGenerators,
		grpcGenerators;
	Map usedTopLevelNames, usedTopLevelServiceNames, usedExtensionNames;
	bool linked;
	int edition;
	Map importPrefixes;
	int nImportPrefixes;
	Map *locations; /* SourceCodeInfo locations by path (shared.c) */
};

/* linker.dart GenerationContext */
typedef struct {
	const Options *options;
	Map files, typeRegistry;
} GenerationContext;

/* ---- the IndentingWriter of a generated file ---------------------------------------------------
 * Declarations are added to the current list (directives, top-level declarations or members of a
 * body) with the number of newlines that upstream's text has before them: nls counts the newlines
 * printed since the last line with text, like IndentingWriter's buffer would end. */
typedef struct {
	Token *tok;
	IntList path;
} Annotation;

typedef struct {
	const char *fileName;
	bool generateMetadata;
	NodeList directives, declarations;
	NodeList *cur;
	int nls;
	Vec annotations;    /* Annotation* in the order upstream adds them */
	const char *suffixes[4]; /* ConditionalConstDefinition env names, sorted on output */
	int nsuffixes;
	Token *pcomments;   /* comment lines printed before the next declaration */
	Node *unit;
	Buf text;           /* the unformatted source (after out_emitSource) */
} Out;

typedef struct {
	NodeList *cur;
	int nls;
} OutBody;

/* indenting_writer.c */
static void out_init(Out *o, const char *fileName, bool generateMetadata);
static Node *out_add(Out *o, Node *n);
static void out_comment(Out *o, const char *text, int sp);
static void out_blank(Out *o);
static OutBody out_begin_body(Out *o, NodeList *members);
static void out_end_body(Out *o, OutBody saved, Token *rb);
static Annotation *out_annotate(Out *o, Token *tok, IntList path);
static void out_addSuffix(Out *o, const char *envName);
static Str out_emitSource(Out *o, bool format);

/* nodes.c: helpers over the dart_style node API */
#define NL(...) nl_of(__VA_ARGS__, (Node *)0)
static NodeList nl_of(Node *first, ...);
static const NodeList NONE;
static Node *dt_node(const DType *t, bool nullable);
static Node *dt_expr(const DType *t);
static const char *dt_str(const DType *t);
static const DType *dt_new(const char *prefix, const char *name);
static const DType *dt_gen(const char *prefix, const char *name, const DType *a0, const DType *a1);
static Node *core_type(const char *name);
static Node *pb_type(const char *name);
static Node *annot_call(const char *prefix, const char *name, Node *arg);
static Node *quoted_node(Str s);

/* string_escape.c */
static char *quoted(Str input);

#endif
