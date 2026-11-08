/* tonic-build 0.14.2 (https://github.com/hyperium/tonic, MIT, Copyright (c) 2025 Lucio Franco):
 * the generic gRPC client and server code generation (src/lib.rs, src/code_gen.rs, src/client.rs,
 * src/server.rs) that protoc-gen-tonic drives. The generated code is a list of syn items (nodes of
 * prettyplease.h) as `syn::parse2::<syn::File>` parses the tokens tonic-build quotes; the caller
 * prints them with pp_unparse. Strings are arena strings. */
#ifndef TONIC_BUILD_H
#define TONIC_BUILD_H

#include "prettyplease.h"

typedef struct TonicMethod TonicMethod;

/* The Method trait. */
struct TonicMethod {
	const char *name;       /* the Rust method name */
	const char *identifier; /* the proto method name */
	const char *codec_path;
	bool client_streaming, server_streaming;
	Str *comment; /* doc comment lines */
	int n_comment;
	/* request_response_name(proto_path, compile_well_known_types): the request and response types. */
	void (*request_response_name)(const TonicMethod *m, const char *proto_path, bool compile_well_known_types,
				      PpNode **request, PpNode **response);
	void *ctx; /* for request_response_name */
};

/* The Service trait. */
typedef struct {
	const char *name, *package, *identifier;
	TonicMethod *methods;
	int n_methods;
	Str *comment;
	int n_comment;
} TonicService;

/* Attributes: (pattern, attribute) pairs for `mod`, `struct` and `trait` items. */
typedef struct {
	Vec module, structure, trait_attributes; /* const char *: pattern, attribute, pattern, ... */
} TonicAttributes;

void tonic_attributes_push_mod(TonicAttributes *a, const char *pattern, const char *attr);
void tonic_attributes_push_struct(TonicAttributes *a, const char *pattern, const char *attr);
void tonic_attributes_push_trait(TonicAttributes *a, const char *pattern, const char *attr);

/* CodeGenBuilder; tonic_code_gen_builder() gives the defaults (emit_package and build_transport on).
 * disable_comments, use_arc_self and generate_default_stubs are not ported (README.md). */
typedef struct {
	bool emit_package, compile_well_known_types, build_transport;
	TonicAttributes attributes;
} TonicCodeGenBuilder;

TonicCodeGenBuilder tonic_code_gen_builder(void);
/* generate_client / generate_server: appends the items (the client or server module) to *items. */
void tonic_generate_client(const TonicCodeGenBuilder *b, const TonicService *s, const char *proto_path, Vec *items);
void tonic_generate_server(const TonicCodeGenBuilder *b, const TonicService *s, const char *proto_path, Vec *items);

/* lib.rs helpers, exported for tests. */
bool tonic_match_name(const char *pattern, const char *path);
char *tonic_naive_snake_case(const char *name);

#endif
