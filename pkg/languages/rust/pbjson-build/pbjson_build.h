/* Port of pbjson-build 0.8.0 (https://github.com/influxdata/pbjson, MIT, Copyright (c) 2020
 * InfluxData): serde Serialize and Deserialize implementations for the types prost-build
 * generates, used by protoc-gen-prost-serde (generators/community/neoeinstein-prost-serde).
 * Upstream panics become fail(); results are arena strings. README.md lists the files. */
#ifndef PBJSON_BUILD_H
#define PBJSON_BUILD_H

#include "pb.h"

/* Builder: Builder::new() is pbjson_builder_new, the option setters set the fields. */
typedef struct {
	struct PbjsonDescriptorSet *descriptors;
	Vec exclude;      /* const char *: Builder::exclude */
	Vec extern_paths; /* pairs of proto path, rust path: Builder::extern_path */
	bool retain_enum_prefix, ignore_unknown_fields, emit_fields, use_integers_for_enums;
	bool preserve_proto_field_names;
	Vec btree_map_paths; /* const char *: Builder::btree_map */
} PbjsonBuilder;

/* The output of one package: its Display (snake case identifiers joined with ".") and code. */
typedef struct {
	const char *package;
	Buf code;
} PbjsonPackageOutput;

PbjsonBuilder *pbjson_builder_new(void);
/* Builder::register_file_descriptor */
void pbjson_builder_register_file_descriptor(PbjsonBuilder *b, PbMsg *file);
/* Builder::generate with a writer per package: PbjsonPackageOutput *, in package order. */
Vec pbjson_builder_generate(PbjsonBuilder *b, const Vec *prefixes);

#endif
