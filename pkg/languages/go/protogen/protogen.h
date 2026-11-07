/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of google.golang.org/protobuf v1.36.12 compiler/protogen (protogen.go,
 * protogen_apilevel.go, protogen_opaque.go): support for writing protoc plugins that generate Go
 * code. The same model as upstream, with Go names kept for fields and functions named
 * pg_<type>_<Method>:
 *
 *	PgPlugin *gen = pg_options_new(&opts, req, &err);    // Options.New
 *	PgGeneratedFile *g = pg_new_generated_file(gen, "x.pb.go", f->GoImportPath);
 *	P(g, "type ", m->GoIdent, " struct {");             // GeneratedFile.P
 *	pg_plugin_response(gen, out);                        // Response, serialized
 *
 * Descriptors come from pkg/languages/go/protodesc (Desc fields). Generated Go files are formatted
 * with the Go 1.26.5 port in pkg/languages/go/stdlib (the toolchain of the buf.build remote
 * plugins), so the output does not depend on anything else.
 *
 * Not ported: ResolvedFeatures (descriptorpb.FeatureSet per declaration; edition features are
 * available as Desc->features), Options.FeatureSetDefaults, Run (the uni generators drive the
 * plugin). */
#ifndef GO_PROTOGEN_H
#define GO_PROTOGEN_H

#include "protodesc.h"

typedef struct PgPlugin PgPlugin;
typedef struct PgFile PgFile;
typedef struct PgEnum PgEnum;
typedef struct PgEnumValue PgEnumValue;
typedef struct PgMessage PgMessage;
typedef struct PgField PgField;
typedef PgField PgExtension;
typedef struct PgOneof PgOneof;
typedef struct PgService PgService;
typedef struct PgMethod PgMethod;
typedef struct PgGeneratedFile PgGeneratedFile;

/* A GoIdent is a Go identifier, consisting of a name and import path. */
typedef struct {
	const char *GoName;
	const char *GoImportPath;
} PgGoIdent;

/* GoImportPath.Ident */
static inline PgGoIdent pg_ident(const char *import_path, const char *name) { return (PgGoIdent){name, import_path}; }
bool pg_ident_eq(PgGoIdent a, PgGoIdent b);

/* A Location is a location in a .proto source file. */
typedef struct {
	const char *SourceFile;
	const int32_t *Path;
	int nPath;
} PgLocation;

/* Comments is a comments string as provided by protoc (may contain any bytes). */
typedef struct {
	Str s;
} PgComments;

/* trailingComment of protoc-gen-go: like Comments, but printed without a trailing newline and
 * not at all if it has several lines. */
typedef struct {
	Str s;
} PgTrailingComment;

static inline PgComments pg_comments(const char *s) { return (PgComments){str_c(s)}; }
/* Comments + s */
PgComments pg_comments_cat(PgComments c, const char *s);
/* Comments.String(): "//" before every line, with a trailing newline; "" for no comments. */
Str pg_comments_string(PgComments c);
static inline bool pg_comments_empty(PgComments c) { return c.s.n == 0; }

/* CommentSet is a set of leading and trailing comments associated with a .proto declaration. */
typedef struct {
	PgComments *LeadingDetached;
	int nLeadingDetached;
	PgComments Leading, Trailing;
} PgCommentSet;

/* GoFeatures.APILevel */
enum { PG_API_LEVEL_UNSPECIFIED = 0, PG_API_OPEN = 1, PG_API_HYBRID = 2, PG_API_OPAQUE = 3 };

struct PgFile {
	GoFile *Desc;
	PbMsg *Proto; /* the FileDescriptorProto (re-parsed with the extensions of the request) */
	PgGoIdent GoDescriptorIdent; /* name of Go variable for the file descriptor */
	const char *GoPackageName;   /* name of this file's Go package */
	const char *GoImportPath;    /* import path of this file's Go package */
	PgEnum **Enums;
	int nEnums;
	PgMessage **Messages;
	int nMessages;
	PgExtension **Extensions;
	int nExtensions;
	PgService **Services;
	int nServices;
	bool Generate; /* true if we should generate code for this file */
	/* GeneratedFilenamePrefix is used to construct filenames for generated files associated
	 * with this source file ("dir/foo" for "dir/foo.proto"). */
	const char *GeneratedFilenamePrefix;
	PgLocation location;
	int APILevel; /* which API to generate: PG_API_OPEN, PG_API_HYBRID or PG_API_OPAQUE */
};

struct PgEnum {
	GoEnum *Desc;
	PgGoIdent GoIdent; /* name of the generated Go type */
	PgEnumValue **Values;
	int nValues;
	PgLocation Location;
	PgCommentSet Comments;
};

struct PgEnumValue {
	GoEnumValue *Desc;
	PgGoIdent GoIdent; /* name of the generated Go declaration */
	/* PrefixedAlias is usually empty (GoName NULL), except when the strip_enum_prefix feature
	 * for this enum was set to GENERATE_BOTH, in which case it holds the old name. */
	PgGoIdent PrefixedAlias;
	PgEnum *Parent;
	PgLocation Location;
	PgCommentSet Comments;
};

struct PgMessage {
	GoMessage *Desc;
	PgGoIdent GoIdent; /* name of the generated Go type */
	PgField **Fields;
	int nFields;
	PgOneof **Oneofs;
	int nOneofs;
	PgEnum **Enums;
	int nEnums;
	PgMessage **Messages;
	int nMessages;
	PgExtension **Extensions;
	int nExtensions;
	PgLocation Location;
	PgCommentSet Comments;
	int APILevel;
};

struct PgField {
	GoField *Desc;
	/* GoName is the base name of this field's Go field and methods ("FieldName"). */
	const char *GoName;
	/* GoIdent is the base name of a top-level declaration for this field
	 * ("MessageName_FieldName"). */
	PgGoIdent GoIdent;
	PgMessage *Parent;   /* message in which this field is declared; NULL if top-level extension */
	PgOneof *Oneof;      /* containing oneof; NULL if not part of a oneof */
	PgMessage *Extendee; /* extended message for extension fields; NULL otherwise */
	PgEnum *Enum;        /* type for enum fields; NULL otherwise */
	PgMessage *Message;  /* type for message or group fields; NULL otherwise */
	PgLocation Location;
	PgCommentSet Comments;
	const char *camelCase;
	bool hasConflictHybrid;
};

struct PgOneof {
	GoOneof *Desc;
	const char *GoName;
	PgGoIdent GoIdent;
	PgMessage *Parent;
	PgField **Fields;
	int nFields;
	PgLocation Location;
	PgCommentSet Comments;
	const char *camelCase;
	bool hasConflictHybrid;
};

struct PgService {
	GoService *Desc;
	const char *GoName;
	PgMethod **Methods;
	int nMethods;
	PgLocation Location;
	PgCommentSet Comments;
};

struct PgMethod {
	GoMethod *Desc;
	const char *GoName;
	PgService *Parent;
	PgMessage *Input, *Output;
	PgLocation Location;
	PgCommentSet Comments;
};

typedef struct {
	/* ParamFunc, if set, is called with each unknown generator parameter (name and value may
	 * contain any bytes); it returns an error, or {NULL, 0}. */
	Str (*ParamFunc)(void *ctx, Str name, Str value);
	void *ParamCtx;
	/* ImportRewriteFunc, if set, returns the import path to use for a package. */
	const char *(*ImportRewriteFunc)(void *ctx, const char *import_path);
	void *ImportRewriteCtx;
	/* InternalStripForEditionsDiff: a flag initialized by ParamFunc. */
	const bool *InternalStripForEditionsDiff;
	/* DefaultAPILevel overrides which API to generate by default. */
	int DefaultAPILevel;
} PgOptions;

struct PgPlugin {
	PbMsg *Request; /* the CodeGeneratorRequest, parsed with protobuf-go semantics */
	/* Files is the set of files to generate and everything they import, in topological
	 * order. */
	PgFile **Files;
	int nFiles;
	Map FilesByPath;
	uint64_t SupportedFeatures;
	int SupportedEditionsMinimum, SupportedEditionsMaximum;

	GoRegistry *fileReg;
	Map enumsByName, messagesByName;
	bool annotateCode;
	int pathType;
	Str module;
	Vec genFiles;
	PgOptions opts;
	bool has_err;
	Str err;
};

/* proto.Unmarshal of a serialized CodeGeneratorRequest with the types linked into protoc-gen-go
 * (protobuf-go semantics). Returns NULL and the error if it cannot be parsed. */
PbMsg *pg_parse_request(const uint8_t *req, size_t n, const char **err);
/* Options.New. Returns NULL and the error text on failure (upstream prints it to stderr and
 * exits). */
PgPlugin *pg_options_new(const PgOptions *opts, PbMsg *req, Str *err);
/* Error records an error in code generation (the first one wins). */
void pg_plugin_error(PgPlugin *gen, Str err);
bool pg_plugin_internal_strip_for_editions_diff(const PgPlugin *gen);
/* Response: the serialized CodeGeneratorResponse. */
void pg_plugin_response(PgPlugin *gen, Buf *out);

/* Field.MethodName("Get"/"Set"/"Has"/"Clear"): the (possibly mangled) name of the accessor, and
 * the backwards-compatible name in *compat (NULL if none). */
const char *pg_field_method_name(const PgField *f, const char *method, const char **compat);
/* Field.BuilderFieldName */
static inline const char *pg_field_builder_field_name(const PgField *f) { return f->camelCase; }
/* Oneof.MethodName("Has"/"Clear"/"Which") */
const char *pg_oneof_method_name(const PgOneof *o, const char *method);

/* ---- generated files ---- */

typedef struct {
	PgLocation Location;
	int Semantic; /* 0: none, else GeneratedCodeInfo.Annotation.Semantic + 1 */
} PgAnnotation;

/* NewGeneratedFile creates a new generated file with the given filename and import path. */
PgGeneratedFile *pg_new_generated_file(PgPlugin *gen, const char *filename, const char *go_import_path);
/* QualifiedGoIdent returns the string to use for a Go identifier, adding an import if needed. */
const char *pg_qualified_go_ident(PgGeneratedFile *g, PgGoIdent ident);
/* Import ensures a package is imported by the generated file. */
void pg_import(PgGeneratedFile *g, const char *import_path);
void pg_skip(PgGeneratedFile *g);
void pg_unskip(PgGeneratedFile *g);
bool pg_generated_file_internal_strip_for_editions_diff(const PgGeneratedFile *g);
/* AnnotateSymbol associates a symbol ("T" or "T.sel") with a location in a .proto file. */
void pg_annotate_symbol(PgGeneratedFile *g, const char *symbol, PgAnnotation info);
/* Content returns the contents of the generated file (formatted for .go files). Returns false
 * and the error in *err on failure. */
bool pg_content(PgGeneratedFile *g, Str *out, Str *err);
/* Write appends raw bytes. */
void pg_write(PgGeneratedFile *g, Str s);

/* The arguments of P, converted by type like fmt.Print: strings, Str, GoIdent (qualified),
 * integers, Comments and trailingComment (their String methods). */
enum { PGA_S, PGA_STR, PGA_ID, PGA_I, PGA_COMMENTS, PGA_TRAILING };
typedef struct {
	int kind;
	union {
		const char *s;
		Str str;
		PgGoIdent id;
		long long i;
	} u;
} PgArg;
static inline PgArg pg_arg_s(const char *s) { return (PgArg){PGA_S, {.s = s}}; }
static inline PgArg pg_arg_str(Str s) { return (PgArg){PGA_STR, {.str = s}}; }
static inline PgArg pg_arg_id(PgGoIdent id) { return (PgArg){PGA_ID, {.id = id}}; }
static inline PgArg pg_arg_i(long long i) { return (PgArg){PGA_I, {.i = i}}; }
static inline PgArg pg_arg_comments(PgComments c) { return (PgArg){PGA_COMMENTS, {.str = c.s}}; }
static inline PgArg pg_arg_trailing(PgTrailingComment c) { return (PgArg){PGA_TRAILING, {.str = c.s}}; }
#define PG_ARG(x)                                                                                              \
	_Generic((x), char *: pg_arg_s, const char *: pg_arg_s, Str: pg_arg_str, PgGoIdent: pg_arg_id, int: pg_arg_i, \
		 long: pg_arg_i, long long: pg_arg_i, unsigned: pg_arg_i, PgComments: pg_arg_comments,                \
		 PgTrailingComment: pg_arg_trailing)(x)

#define PG_N_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, \
	      _24, N, ...)                                                                                              \
	N
#define PG_N(...) PG_N_(__VA_ARGS__, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define PG_M1(x) PG_ARG(x)
#define PG_M2(x, ...) PG_ARG(x), PG_M1(__VA_ARGS__)
#define PG_M3(x, ...) PG_ARG(x), PG_M2(__VA_ARGS__)
#define PG_M4(x, ...) PG_ARG(x), PG_M3(__VA_ARGS__)
#define PG_M5(x, ...) PG_ARG(x), PG_M4(__VA_ARGS__)
#define PG_M6(x, ...) PG_ARG(x), PG_M5(__VA_ARGS__)
#define PG_M7(x, ...) PG_ARG(x), PG_M6(__VA_ARGS__)
#define PG_M8(x, ...) PG_ARG(x), PG_M7(__VA_ARGS__)
#define PG_M9(x, ...) PG_ARG(x), PG_M8(__VA_ARGS__)
#define PG_M10(x, ...) PG_ARG(x), PG_M9(__VA_ARGS__)
#define PG_M11(x, ...) PG_ARG(x), PG_M10(__VA_ARGS__)
#define PG_M12(x, ...) PG_ARG(x), PG_M11(__VA_ARGS__)
#define PG_M13(x, ...) PG_ARG(x), PG_M12(__VA_ARGS__)
#define PG_M14(x, ...) PG_ARG(x), PG_M13(__VA_ARGS__)
#define PG_M15(x, ...) PG_ARG(x), PG_M14(__VA_ARGS__)
#define PG_M16(x, ...) PG_ARG(x), PG_M15(__VA_ARGS__)
#define PG_M17(x, ...) PG_ARG(x), PG_M16(__VA_ARGS__)
#define PG_M18(x, ...) PG_ARG(x), PG_M17(__VA_ARGS__)
#define PG_M19(x, ...) PG_ARG(x), PG_M18(__VA_ARGS__)
#define PG_M20(x, ...) PG_ARG(x), PG_M19(__VA_ARGS__)
#define PG_M21(x, ...) PG_ARG(x), PG_M20(__VA_ARGS__)
#define PG_M22(x, ...) PG_ARG(x), PG_M21(__VA_ARGS__)
#define PG_M23(x, ...) PG_ARG(x), PG_M22(__VA_ARGS__)
#define PG_M24(x, ...) PG_ARG(x), PG_M23(__VA_ARGS__)
#define PG_CAT_(a, b) a##b
#define PG_CAT(a, b) PG_CAT_(a, b)

/* P prints a line to the generated output (GeneratedFile.P); P(g, "") prints an empty line. */
void pg_p(PgGeneratedFile *g, const PgArg *args, int n);
#define P(g, ...) pg_p((g), (const PgArg[]){PG_CAT(PG_M, PG_N(__VA_ARGS__))(__VA_ARGS__)}, PG_N(__VA_ARGS__))

/* GoIdent.String-free helpers used by generators */
/* cleanPackageName converts a string to a valid Go package name. */
const char *pg_clean_package_name(const char *name);

#endif
