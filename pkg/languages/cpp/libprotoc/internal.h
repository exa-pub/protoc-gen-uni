// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Internal declarations of the libprotoc 36.2 unity build (libprotoc.c): the pool tables
 * (DescriptorPool::Tables), symbols, and the static functions shared between its files. */
#ifndef LIBPROTOC_INTERNAL_H
#define LIBPROTOC_INTERNAL_H

#include "libprotoc.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Field numbers of descriptor.proto beyond pb.h. */
enum {
	FDP_OPTION_DEPENDENCY = 15,
	DP_RESERVED_RANGE = 9, DP_RESERVED_NAME = 10, DP_VISIBILITY = 11,
	ER_OPTIONS = 3,
	ODP_OPTIONS = 2,
	EDP_RESERVED_RANGE = 4, EDP_RESERVED_NAME = 5, EDP_VISIBILITY = 6,
	SDP_OPTIONS = 3,
	/* *Options */
	FILEOPT_JAVA_STRING_CHECK_UTF8 = 27, FILEOPT_JAVA_MULTIPLE_FILES = 10, FILEOPT_OPTIMIZE_FOR = 9, FILEOPT_CC_GENERIC_SERVICES = 16,
	FILEOPT_JAVA_GENERIC_SERVICES = 17, FILEOPT_RUBY_PACKAGE = 45, FILEOPT_FEATURES = 50,
	MSGOPT_MESSAGE_SET_WIRE_FORMAT = 1, MSGOPT_MAP_ENTRY = 7,
	MSGOPT_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS = 11, MSGOPT_FEATURES = 12,
	FOPT_CTYPE = 1, FOPT_PACKED = 2, FOPT_LAZY = 5, FOPT_JSTYPE = 6, FOPT_WEAK = 10,
	FOPT_UNVERIFIED_LAZY = 15, FOPT_RETENTION = 17, FOPT_FEATURES = 21, FOPT_FEATURE_SUPPORT = 22,
	ONEOFOPT_FEATURES = 1,
	ENUMOPT_ALLOW_ALIAS = 2, ENUMOPT_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS = 6, ENUMOPT_FEATURES = 7,
	EVOPT_FEATURES = 2, EVOPT_FEATURE_SUPPORT = 4, EVOPT_JSON = 998,
	SVCOPT_FEATURES = 34, METHOPT_FEATURES = 35,
	EROPT_DECLARATION = 2, EROPT_VERIFICATION = 3, EROPT_FEATURES = 50,
	DECL_NUMBER = 1, DECL_FULL_NAME = 2, DECL_TYPE = 3, DECL_RESERVED = 5, DECL_REPEATED = 6,
	OPT_UNINTERPRETED = 999,
	/* FieldOptions.FeatureSupport */
	FS_EDITION_INTRODUCED = 1, FS_EDITION_DEPRECATED = 2, FS_DEPRECATION_WARNING = 3,
	FS_EDITION_REMOVED = 4, FS_REMOVAL_ERROR = 5,
	/* FeatureSetDefaults and the FeatureSet values: PROTO_FSD_*, PROTO_PRESENCE_*, ... (proto.h) */
	/* extensions of FeatureSet compiled into protoc_lib */
	FEATURES_PB_CPP = 1000, CPPF_LEGACY_CLOSED_ENUM = 1, CPPF_STRING_TYPE = 2, CPP_STRING_TYPE_CORD = 2,
	FO_CTYPE_CORD = 1,
};

/* Symbol: the symbols of pkg/proto (kinds PROTO_MESSAGE ... PROTO_SYM_SUB_PACKAGE). */
typedef ProtoSymbol LpSymbol;

typedef struct LpMsgDef LpMsgDef;

/* A set of files that every build of a pool reuses (DescriptorBuilder::dependencies_): under the
 * arena, a set per build would take memory quadratic in chains of public imports. */
typedef struct {
	Map marks;      /* file name -> LpFileMark* (descriptor.c) */
	Vec files;      /* the files in the set */
	unsigned stamp; /* the marks with this stamp are in the set */
} LpFileSet;

/* DescriptorPool: the tables of pkg/proto and what libprotoc adds. */
struct LpPool {
	ProtoPool base;              /* files, symbols, extensions (base.extensions keyed by extendee id) */
	PbMsg *feature_set_defaults; /* FeatureSetDefaults */
	const PbMsgDef *feature_set_def;
	PbMsg *empty_features;       /* FeatureSet::default_instance() */
	LpPool *builtin;             /* the generated pool (protoc_lib), NULL for itself */
	const LpVersion *version;    /* the protobuf version (NULL: 36.2) */
	Map defs;                    /* message full name -> LpMsgDef* */
	int defs_files;              /* files in the pool when the cache was filled */
	Vec pending_files;
	LpFileSet dependencies, option_dependencies; /* of the file being built */
};

/* The LpPool of a file (LpFile.pool is its ProtoPool base). */
static inline LpPool *lp_file_pool(const LpFile *f) { return (LpPool *)f->pool; }

/* message.c */
static bool lp_decode(PbMsg *m, const uint8_t *p, size_t n);
static bool lp_is_initialized(const PbMsg *m);
static Str lp_serialize_str(const PbMsg *m);

/* strutil.c */
static char *lp_itoa(int64_t v);
static char *lp_utoa(uint64_t v);
static bool ascii_isdigit(unsigned char c);
static bool ascii_isalpha(unsigned char c);
static bool ascii_isalnum(unsigned char c);
static char ascii_toupper(char c);
static char ascii_tolower(char c);

/* descriptor.c */
static LpPool *lp_pool_new(LpPool *builtin, PbMsg *defaults, const PbMsgDef *feature_set_def);
/* BuildFile; on failure returns NULL and appends upstream's log lines to *log. */
static LpFile *lp_build_file(LpPool *pool, PbMsg *proto, Buf *log);
static LpFile *lp_find_file(const LpPool *pool, const char *name);
static LpSymbol *lp_find_symbol(const LpPool *pool, const char *name);
static const LpMessage *lp_find_message(const LpPool *pool, const char *name);
static const LpMsgDef *lp_msgdef(LpPool *pool, const LpMessage *m);
static const LpMsgDef *lp_msgdef_by_name(LpPool *pool, const char *full_name);
static PbMsg *lp_file_copy_to(const LpFile *file);

/* feature_resolver.c */
typedef struct {
	PbMsg *defaults; /* the FeatureSet of the edition, NULL if Create failed */
} LpFeatureResolver;
static const char *lp_feature_resolver_create(LpFeatureResolver *r, int edition, const PbMsg *defaults,
					      const PbMsgDef *feature_set_def);
static const char *lp_merge_features(const LpFeatureResolver *r, const PbMsg *parent, const PbMsg *child,
				     PbMsg **out);
static PbMsg *lp_compile_defaults(LpPool *builtin, const char *const *extensions, int min_edition, int max_edition,
				  const char **error);
static void lp_validate_feature_lifetimes(int edition, const PbMsg *option, LpPool *pool, Vec *errors);

/* embedded.c: the generated pool of the version (NULL: 36.2) */
static LpPool *lp_builtin_pool(const LpVersion *version);

/* retention.c */
static void lp_strip_message(PbMsg *m, Vec *path, Vec *stripped);

#endif
