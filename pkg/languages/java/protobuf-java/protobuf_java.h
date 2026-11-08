/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* protobuf-java 3.25.8 (java/core/src/main/java/com/google/protobuf), and the differences of
 * 4.36.2 (jpool_use_v4, features.c, generator_names.c), as far as the JVM plugins built on it use
 * it: Descriptors (FileDescriptor.buildFrom with its validation and the descriptor
 * pools, descriptors.c) and the serialization of messages (GeneratedMessageV3.writeTo with the
 * UnknownFieldSet, Builder.mergeFrom: message.c). Messages are pkg/pb messages decoded with the
 * schema the plugin registers (the descriptor.proto messages with its extensions).
 *
 * Exceptions are fail() with the text of Throwable.toString, the first line of the stack trace that
 * protocgen.CodeGenApp puts into the response (java_exception, java_std.h). */
#ifndef PROTOBUF_JAVA_H
#define PROTOBUF_JAVA_H

#include "java_std.h"
#include "pb.h"

enum { SYN_PROTO2, SYN_PROTO3, SYN_EDITIONS };
enum { SYM_PACKAGE, SYM_MESSAGE, SYM_ENUM, SYM_FIELD, SYM_ENUM_VALUE, SYM_SERVICE, SYM_METHOD };

/* Java types of fields (FieldDescriptor.JavaType). */
enum { JT_INT, JT_LONG, JT_FLOAT, JT_DOUBLE, JT_BOOLEAN, JT_STRING, JT_BYTE_STRING, JT_ENUM, JT_MESSAGE };

typedef struct JFile JFile;
typedef struct JMsg JMsg;
typedef struct JField JField;
typedef struct JOneof JOneof;
typedef struct JEnum JEnum;
typedef struct JEnumValue JEnumValue;
typedef struct JService JService;
typedef struct JMethod JMethod;

typedef struct JSymEntry JSymEntry;
typedef struct {
	JSymEntry *e;
	size_t cap, n;
} JSymTab;

/* A symbol of a pool (GenericDescriptor). */
typedef struct {
	int kind;
	void *p;
	JFile *file;
	const char *name; /* unqualified name (for error messages) */
} JSym;

struct JFile {
	PbMsg *proto;
	const char *name, *package;
	int syntax;
	Vec deps;      /* JFile*: getDependencies */
	JSymTab syms;  /* full name -> JSym* defined in this pool (own symbols and packages) */
	unsigned pool_mark; /* JPoolIter */
	Vec msgs, enums, services, exts;
	Vec public_deps;
	/* caches of the plugin library (ScalaPB, pkg/languages/scala/scalapb) */
	PbMsg *scala_opts; /* FileOptionsCache result */
	const char *outer_class_name;
	void *descriptor_object;
	void *aux_index[4]; /* AuxIndex* per kind of aux options */
	void *scala_package;
	const char *scala_directory;
	void *loc_index; /* JLocIndex*: SourceCodeInfo locations by path */
	void *features;  /* JFeatures*: resolved features (protobuf-java 4.x, features.c) */
	const char *file_class_name; /* GeneratorNames.getFileClassName (generator_names.c) */
};

struct JMsg {
	PbMsg *proto;
	const char *name, *full_name;
	JFile *file;
	JMsg *parent;
	int index;
	Vec fields, nested, enums, exts, oneofs;
	int real_oneofs;
	JField **by_number; /* fieldsSortedByNumber */
	/* caches of the plugin library (ScalaPB) */
	void *scala_type;
	PbMsg *msg_opts;
	Vec *field_names;
	Map *field_name_set, *ctx_names;
	signed char sealed_type; /* msg_is_sealed_oneof_type: 0 unknown, 1 no, 2 yes */
	Vec *sealed_cases;       /* SealedOneofsCache */
	JMsg *sealed_container;
	unsigned dup_mark; /* ScalaPB ProtoValidation */
	void *features;    /* JFeatures* (features.c) */
};

struct JField {
	PbMsg *proto;
	const char *name, *full_name;
	JFile *file;
	int index, number, label, type;
	bool is_ext, proto3_optional, has_type;
	JMsg *containing_type, *ext_scope;
	JOneof *oneof;
	JMsg *msg_type;
	JEnum *enum_type;
	/* default value (by Java type) */
	int64_t def_i; /* INT, LONG, BOOLEAN */
	double def_d;  /* FLOAT, DOUBLE */
	Str def_s;     /* STRING (UTF-8), BYTE_STRING */
	JEnumValue *def_e;
	/* caches of the plugin library (ScalaPB) */
	const char *scala_name;
	PbMsg *field_opts;
	void *features; /* JFeatures* (features.c) */
};

struct JOneof {
	PbMsg *proto;
	const char *name, *full_name;
	JMsg *containing;
	int index;
	Vec fields;
};

struct JEnum {
	PbMsg *proto;
	const char *name, *full_name;
	JFile *file;
	JMsg *parent;
	int index;
	Vec values;
	void *scala_type;
	PbMsg *enum_opts;
	const char **is_names; /* per value index */
	JEnumValue **by_number; /* values sorted by (number, index): enum_by_number */
	int *by_number_pos;     /* per value index: its position in by_number */
};

struct JEnumValue {
	PbMsg *proto;
	const char *name, *full_name;
	int number, index;
	JEnum *type;
	const char *scala_name;
	PbMsg *value_opts;
};

struct JService {
	PbMsg *proto;
	const char *name, *full_name;
	JFile *file;
	int index;
	Vec methods;
};

struct JMethod {
	PbMsg *proto;
	const char *name, *full_name;
	JService *service;
	JFile *file;
	int index;
	JMsg *input, *output;
};

/* DescriptorPool.descriptorsByName. Keys are (pointer, length) views, so that the packages of a
 * name ("a", "a.b", "a.b.c") share its bytes and their hashes are computed incrementally: memory
 * and time stay linear in the length of a name. */
struct JSymEntry {
	const char *key;
	size_t len;
	uint64_t hash;
	JSym *sym;
};

#define JHASH0 0xcbf29ce484222325u

/* FNV-1a of p[0:n], extending the hash h (JHASH0 for the empty string). */
uint64_t jhash_add(uint64_t h, const char *p, size_t n);
/* The symbol of the key p1 + p2 (n1 + n2 bytes, hash h), or NULL. */
JSym *jsymtab_get(const JSymTab *t, const char *p1, size_t n1, const char *p2, size_t n2, uint64_t h);
void jsymtab_put(JSymTab *t, const char *key, size_t len, uint64_t h, JSym *sym);

/* ---- descriptors.c ---- */

/* A string field as Java sees it (java_cstr). */
const char *jstr(const PbMsg *m, uint32_t num);
const char *jstr_at(const PbMsg *m, uint32_t num, int i);

/* GeneratedMessageV3.getField of a bool field with a default. */
static inline bool jpb_bool(const PbMsg *m, uint32_t num, bool def) { return pb_has(m, num) ? pb_int(m, num, 0) != 0 : def; }

/* FieldDescriptor.getJavaType of a field type (PT_*). */
int java_type_of(int type);
static inline int jfield_java_type(const JField *f) { return java_type_of(f->type); }
static inline bool jfield_is_repeated(const JField *f) { return f->label == LABEL_REPEATED; }
static inline bool jfield_is_required(const JField *f) { return f->label == LABEL_REQUIRED; }
static inline bool jfield_is_optional(const JField *f) { return f->label == LABEL_OPTIONAL; }
static inline PbMsg *jfield_options(const JField *f) { return pb_msg(f->proto, FD_OPTIONS); }

static inline bool joneof_is_synthetic(const JOneof *o)
{
	return o->fields.n == 1 && VEC_AT(o->fields, JField, 0)->proto3_optional;
}

/* getRealContainingOneof */
static inline JOneof *jfield_real_oneof(const JField *f) { return f->oneof && !joneof_is_synthetic(f->oneof) ? f->oneof : NULL; }

static inline bool jmsg_is_map_entry(const JMsg *m) { return jpb_bool(pb_msg(m->proto, DP_OPTIONS), 7, false); }
static inline bool jmsg_is_extendable(const JMsg *m) { return pb_count(m->proto, DP_EXTENSION_RANGE) > 0; }

static inline bool jfield_is_map_field(const JField *f)
{
	return f->type == PT_MESSAGE && jfield_is_repeated(f) && f->msg_type && jmsg_is_map_entry(f->msg_type);
}

bool jfield_is_packable(const JField *f);
bool jfield_is_packed(const JField *f);
JField *jmsg_find_field_by_name(const JMsg *m, const char *name);
/* Comparator of JField* by number. */
int jfield_cmp_number(const void *a, const void *b);

/* Resets the state of the descriptor pools of a previous request (and selects protobuf-java
 * 3.25.8). */
void jpool_reset(void);
/* Selects the Descriptors of protobuf-java 4.36.2 for the builds after it: extensions of
 * MessageSets are checked by validateFeatures, and jfile_build resolves the features of the file
 * (jfile_resolve_features). */
void jpool_use_v4(void);
/* FileDescriptor.buildFrom(proto, deps), deps looked up by name in byname (name -> JFile*, the files
 * built so far). Validation errors fail() with the DescriptorValidationException. */
JFile *jfile_build(PbMsg *proto, Map *byname);

/* ---- message.c ---- */

/* writeTo: known fields by number, then the unknown fields as UnknownFieldSet writes them. */
void jpb_encode(const PbMsg *m, Buf *out);
PbMsg *jpb_copy(const PbMsg *m);
void jpb_set_msgs(PbMsg *m, uint32_t num, PbMsg **msgs, int n);
void jpb_clear(PbMsg *m, uint32_t num);
/* a.toBuilder.mergeFrom(b).build() of two messages of the type def. */
PbMsg *jpb_merge(const PbMsgDef *def, const PbMsg *a, const PbMsg *b);

/* ---- features.c: feature resolution of protobuf-java 4.36.2 ---- */

/* Edition numbers (descriptor.proto Edition). */
enum { JED_UNKNOWN = 0, JED_LEGACY = 900, JED_PROTO2 = 998, JED_PROTO3 = 999, JED_2023 = 1000, JED_2024 = 1001,
       JED_2026 = 1002, JED_UNSTABLE = 9999 };
/* A resolved FeatureSet: the fields 1..9 of google.protobuf.FeatureSet, then the fields 1..5 of
 * pb.JavaFeatures (extension 1001), as enum numbers (bools 0 or 1). */
enum {
	JF_FIELD_PRESENCE, JF_ENUM_TYPE, JF_REPEATED_FIELD_ENCODING, JF_UTF8_VALIDATION, JF_MESSAGE_ENCODING,
	JF_JSON_FORMAT, JF_ENFORCE_NAMING_STYLE, JF_DEFAULT_SYMBOL_VISIBILITY, JF_9,
	JF_JAVA_LEGACY_CLOSED_ENUM, JF_JAVA_UTF8_VALIDATION, JF_JAVA_LARGE_ENUM,
	JF_JAVA_USE_OLD_OUTER_CLASSNAME_DEFAULT, JF_JAVA_NEST_IN_FILE_CLASS, JF_N
};
typedef struct {
	int32_t v[JF_N];
} JFeatures;

/* FileDescriptor.getEdition() */
int jfile_edition(const JFile *f);
/* Edition.toString() */
const char *jedition_name(int edition);
/* FileDescriptor.resolveAllFeaturesInternal: resolves and validates the features of the file and
 * all its descriptors, like buildFrom of protobuf-java 4.36.2 after cross-linking. Errors fail()
 * like jfile_build (IllegalArgumentException of getEditionDefaults, DescriptorValidationException). */
void jfile_resolve_features(JFile *f);
/* getFeatures() of a file, message or field after jfile_resolve_features */
static inline const JFeatures *jfile_features(const JFile *f) { return f->features; }
static inline const JFeatures *jmsg_features(const JMsg *m) { return m->features; }
static inline const JFeatures *jfield_features(const JField *f) { return f->features; }
/* FieldDescriptor.getType() of protobuf-java 4.x: PT_GROUP for a delimited message field. */
int jfield_type_v4(const JField *f);

/* ---- generator_names.c: com.google.protobuf.GeneratorNames (protobuf-java 4.36.2) ---- */

/* getFileJavaPackage(file) */
const char *jnames_file_java_package(const JFile *f);
/* getFileClassName(file) */
const char *jnames_file_class_name(const JFile *f);
/* getNestInFileClass(message) */
bool jnames_nest_in_file_class(const JMsg *m);
/* getQualifiedClassName(message) */
const char *jnames_qualified_class_name(const JMsg *m);

#endif
