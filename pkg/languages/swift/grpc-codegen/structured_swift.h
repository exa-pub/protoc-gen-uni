/* Copyright (c) 2023 Apple Inc. and the SwiftOpenAPIGenerator project authors
 * Licensed under Apache License v2.0
 *
 * See LICENSE.txt for license information
 * See CONTRIBUTORS.txt for the list of SwiftOpenAPIGenerator project authors
 *
 * SPDX-License-Identifier: Apache-2.0 */

/* The StructuredSwift representation of GRPCCodeGen (Internal/StructuredSwiftRepresentation.swift,
 * from swift-openapi-generator) with the nodes and fields that GRPCCodeGen builds: the cases it
 * never uses (switch, if, do, tuples, enum cases, deprecations, getters, ...) are left out. Nodes
 * are built once and rendered by renderer.c (TextBasedRenderer.swift). Internal to the library. */
#ifndef GC_STRUCTURED_SWIFT_H
#define GC_STRUCTURED_SWIFT_H

#include "grpc_codegen.h"

/* AccessModifier; GC_NONE is nil. */
typedef enum { GC_NONE, GC_PUBLIC, GC_PACKAGE, GC_INTERNAL, GC_FILEPRIVATE, GC_PRIVATE } GcAccess;

/* Comment */
typedef enum { GC_INLINE, GC_DOC, GC_MARK, GC_MARK_SECTION, GC_PRE_FORMATTED } GcCommentKind;
typedef struct {
	GcCommentKind kind;
	Str text;
} GcComment;

typedef struct GcType GcType;
typedef struct GcExpr GcExpr;
typedef struct GcDecl GcDecl;

/* FunctionKeyword lists: GRPCCodeGen only uses [.async, .throws] and []. */
typedef enum { GC_KW_NONE, GC_KW_ASYNC_THROWS } GcKeywords;

/* ParameterDescription */
typedef struct {
	const char *label, *name;
	GcType *type;
	GcExpr *default_value;
	bool is_inout;
} GcParam;

/* ClosureSignatureDescription */
typedef struct {
	Vec parameters; /* GcParam * */
	GcKeywords keywords;
	GcExpr *return_type;
	bool sendable, escaping;
} GcClosureSig;

/* ExistingTypeDescription (no optional or dictionaryValue). member holds the components joined
 * with ".", as they render. */
typedef enum { GT_ANY, GT_GENERIC, GT_MEMBER, GT_ARRAY, GT_SOME, GT_CLOSURE } GcTypeKind;
struct GcType {
	GcTypeKind kind;
	const char *member;
	GcType *inner;  /* any, array, some; generic: the wrapper */
	Vec wrapped;    /* generic: GcType * */
	GcClosureSig *closure;
};

/* Expression (literals: string, array, dictionary) */
typedef enum {
	GE_STRING, GE_ARRAY, GE_DICTIONARY, GE_PATTERN, GE_TYPE, GE_MEMBER, GE_CALL, GE_ASSIGN, GE_KEYWORD, GE_CLOSURE,
} GcExprKind;
/* KeywordKind */
typedef enum { GC_RETURN, GC_TRY, GC_AWAIT } GcKeyword;
/* FunctionArgumentDescription */
typedef struct {
	const char *label;
	GcExpr *expr;
} GcArg;
struct GcExpr {
	GcExprKind kind;
	const char *s;       /* string literal, pattern, member access right */
	GcType *type;        /* identifier type */
	GcExpr *left, *right; /* member access left (or NULL); called expression; assignment; keyword operand (or NULL) */
	Vec items;           /* array: GcExpr *; call: GcArg *; closure: argument names */
	Vec *body;           /* closure: GcCodeBlock * (or NULL) */
	GcKeyword keyword;
};

/* CodeBlock: comment and item are optional, item is decl or expr. */
typedef struct {
	GcComment *comment;
	GcDecl *decl;
	GcExpr *expr;
} GcCodeBlock;

/* AvailabilityDescription */
typedef struct {
	const GcPlatform *versions;
	int n;
} GcAvailability;

/* WhereClause with one conformance requirement (all that GRPCCodeGen writes). */
typedef struct {
	const char *left, *right;
} GcWhere;

/* FunctionSignatureDescription; kind: initializer (not failable) when name is NULL. */
typedef struct {
	GcAccess access;
	const char *name;
	Vec generics;   /* GcType * */
	Vec parameters; /* GcParam * */
	GcKeywords keywords;
	GcExpr *return_type;
	GcWhere *where;
} GcSignature;

/* Declaration */
typedef enum {
	GD_COMMENTABLE, GD_GUARDED, GD_VARIABLE, GD_EXTENSION, GD_STRUCT, GD_ENUM, GD_TYPEALIAS, GD_PROTOCOL, GD_FUNCTION,
} GcDeclKind;
struct GcDecl {
	GcDeclKind kind;
	GcComment *comment;          /* commentable (or NULL) */
	const GcAvailability *availability; /* guarded */
	GcDecl *inner;               /* commentable, guarded */
	GcAccess access;             /* variable, extension, struct, enum, typealias, protocol */
	const char *name;            /* struct, enum, typealias, protocol; extension: onType */
	bool is_static, is_let;      /* variable */
	GcExpr *left, *right;        /* variable */
	GcType *type;                /* variable (or NULL), typealias */
	Vec conformances;            /* const char * */
	Vec generics;                /* struct: GcType * */
	GcWhere *where;              /* struct, extension */
	Vec members;                 /* struct, enum, protocol members; extension declarations: GcDecl * */
	GcSignature sig;             /* function */
	Vec *body;                   /* function: GcCodeBlock * (or NULL) */
};

/* ImportDescription (no item, spi, moduleTypes or preconcurrency: the translator leaves them at
 * their defaults for the dependencies it gets). */
typedef struct {
	GcAccess access;
	const char *module_name;
} GcImport;

/* FileDescription */
typedef struct {
	GcComment *top_comment;
	GcImport *imports; /* NULL: nil */
	int n_imports;
	Vec code_blocks;   /* GcCodeBlock * */
} GcFile;

/* ---- struct.c: conveniences (StructuredSwiftRepresentation.swift) and Namer (Namer.swift) ---- */

Vec gc_vec(void *first, ...); /* up to a NULL sentinel */
GcComment *gc_comment(GcCommentKind kind, Str text);
GcComment *gc_doc(const char *text);

GcType *gc_member(const char *name);
GcType *gc_type(GcTypeKind kind, GcType *inner);
GcType *gc_generic(GcType *wrapper, GcType *wrapped, GcType *wrapped2);
GcType *gc_closure_type(GcType *param_type, const char *return_type);

GcExpr *gc_string(const char *s);
GcExpr *gc_empty_dictionary(void);
GcExpr *gc_array(Vec items);
GcExpr *gc_pattern(const char *s);
GcExpr *gc_identifier_type(GcType *t);
GcExpr *gc_dot(GcExpr *left, const char *right);
GcExpr *gc_call(GcExpr *called, Vec args);
GcArg *gc_arg(const char *label, GcExpr *expr);
GcExpr *gc_assign(GcExpr *left, GcExpr *right);
GcExpr *gc_keyword(GcKeyword kind, GcExpr *expr);
GcExpr *gc_closure(Vec argument_names, Vec *body);
GcParam *gc_param(const char *label, const char *name, GcType *type, GcExpr *default_value);

GcCodeBlock *gc_block(GcComment *comment, GcDecl *decl, GcExpr *expr);
Vec *gc_blocks(Vec v);
GcDecl *gc_decl(GcDeclKind kind);
GcDecl *gc_commentable(GcComment *comment, GcDecl *inner);
GcDecl *gc_guarded(const GcAvailability *availability, GcDecl *inner);
GcDecl *gc_variable(GcAccess access, bool is_static, const char *left, GcType *type, GcExpr *right);
GcDecl *gc_extension(const char *on_type, Vec declarations);
GcDecl *gc_function(GcSignature sig, Vec *body);
GcWhere *gc_where(const char *left, const char *right);

/* Namer: the GRPCCore types. */
typedef struct {
	const char *grpc_core;
} GcNamer;
char *gc_literal_namespaced_type(const GcNamer *n, const char *type);
GcType *gc_grpc_core(const GcNamer *n, const char *type);
GcType *gc_grpc_core_generic(const GcNamer *n, const char *type, const char *over);
GcType *gc_server_request(const GcNamer *n, const char *type, bool streaming);
GcType *gc_server_response(const GcNamer *n, const char *type, bool streaming);
GcType *gc_client_request(const GcNamer *n, const char *type, bool streaming);
GcType *gc_client_response(const GcNamer *n, const char *type, bool streaming);
GcType *gc_rpc_async_sequence(const GcNamer *n, const char *type);

/* ---- service_metadata.c, server.c, client.c: the builders (StructuredSwift+*.swift) ---- */

extern const GcAvailability gc_macos15_aligned;
void gc_service_metadata(Vec *blocks, GcAccess access, const GcService *service, const GcAvailability *availability,
			 const GcNamer *namer);

GcDecl *gc_streaming_service_protocol(GcAccess access, const char *name, const GcService *s, const GcNamer *namer);
GcDecl *gc_service_protocol(GcAccess access, const char *name, const char *streaming_protocol, const GcService *s,
			    const GcNamer *namer);
GcDecl *gc_simple_service_protocol(GcAccess access, const char *name, const char *service_protocol, const GcService *s,
				   const GcNamer *namer);
GcDecl *gc_registrable_rpc_service_default_implementation(GcAccess access, const char *on, const char *service_namespace,
							  const GcService *s, const GcNamer *namer,
							  const GcRequest *request);
GcDecl *gc_streaming_service_protocol_default_implementation(GcAccess access, const char *on, const GcService *s,
							     const GcNamer *namer);
GcDecl *gc_service_protocol_default_implementation(GcAccess access, const char *on, const GcService *s,
						   const GcNamer *namer);

GcDecl *gc_client_protocol(GcAccess access, const char *name, const GcService *s, const GcNamer *namer);
GcDecl *gc_client_struct(GcAccess access, const char *name, const char *service_enum, const char *client_protocol,
			 const GcService *s, const GcNamer *namer);
GcDecl *gc_client_method_signature_with_defaults(GcAccess access, const char *name, const GcService *s,
						 const GcNamer *namer, const GcRequest *request);
GcDecl *gc_exploded_client_methods(GcAccess access, const char *on, const GcService *s, const GcNamer *namer);

/* ---- translator.c: Docs.swift ---- */

Str gc_docs_suffix(Str header, Str footer);
Str gc_docs_interpose(Str docs, Str header, Str footer);

/* ---- renderer.c: TextBasedRenderer.render(structured:) ---- */

Str gc_render(const GcFile *file, int indentation);

#endif
