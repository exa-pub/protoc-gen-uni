/* Hand-written protobuf wire format support: a schema-driven generic message that is parsed from
 * the wire, read through accessors, and serialized again like protobuf-es does (known fields in
 * field number order, then unknown fields in wire order). No generated code, no dependencies.
 *
 * Parsing follows @bufbuild/protobuf fromBinary (protobuf-es v2.17.0): repeated singular message
 * fields are merged, the last scalar wins, packed and unpacked encodings are accepted, unknown
 * values of closed enums become unknown fields, and nesting is limited to 100 messages. A known
 * field with an unexpected wire type is kept as an unknown field (like protobuf-go). */
#ifndef PB_H
#define PB_H

#include "util.h"

/* Field types use the numbers of google.protobuf.FieldDescriptorProto.Type. */
enum {
	PT_DOUBLE = 1, PT_FLOAT = 2, PT_INT64 = 3, PT_UINT64 = 4, PT_INT32 = 5, PT_FIXED64 = 6,
	PT_FIXED32 = 7, PT_BOOL = 8, PT_STRING = 9, PT_GROUP = 10, PT_MESSAGE = 11, PT_BYTES = 12,
	PT_UINT32 = 13, PT_ENUM = 14, PT_SFIXED32 = 15, PT_SFIXED64 = 16, PT_SINT32 = 17, PT_SINT64 = 18,
};
/* Labels: optional (singular), repeated (written unpacked), packed (repeated, written packed). */
enum { PL_OPT = 0, PL_REP = 1, PL_PACKED = 2 };
enum { WT_VARINT = 0, WT_I64 = 1, WT_LEN = 2, WT_SGROUP = 3, WT_EGROUP = 4, WT_I32 = 5 };

/* Maximum nesting of messages (protobuf-es BinaryReadOptions.recursionLimit). */
#define PB_MAX_DEPTH 100

typedef struct PbMsgDef PbMsgDef;
typedef struct {
	uint32_t num;
	uint8_t type, label;
	const PbMsgDef *msg;      /* PT_MESSAGE */
	const int32_t *enum_vals; /* PT_ENUM of a closed enum: known values */
	int n_enum_vals;
} PbField;

struct PbMsgDef {
	int n;
	const PbField *f; /* sorted by number */
};

typedef struct PbMsg PbMsg;
typedef union {
	uint64_t u; /* varint types: canonical value; fixed types: raw bits */
	Str s;      /* string and bytes: NUL-terminated copy (may contain NUL) */
	PbMsg *m;
} PbVal;

typedef struct {
	int n, cap; /* n == 0: not present */
	PbVal *v;
} PbSlot;

struct PbMsg {
	const PbMsgDef *def;
	PbSlot *slots; /* one per def->f */
	Buf unknown;   /* raw unknown fields (tag + value), in wire order */
	uint8_t mark;  /* free for users */
};

PbMsg *pb_new(const PbMsgDef *def);
/* Parses (merges) the wire bytes into m. Returns false on malformed input. */
bool pb_decode(PbMsg *m, const uint8_t *p, size_t n);

/* Serializes m. skip (optional) can drop known fields. */
typedef bool (*PbSkipFn)(void *ctx, const PbMsg *m, uint32_t num);
void pb_encode(const PbMsg *m, Buf *out, PbSkipFn skip, void *ctx);

/* Accessors; m may be NULL (all return defaults then). String accessors return the
 * NUL-terminated copy, which ends early if the value contains NUL; pb_bytes gives the exact
 * value. */
const PbSlot *pb_slot(const PbMsg *m, uint32_t num);
bool pb_has(const PbMsg *m, uint32_t num);
int pb_count(const PbMsg *m, uint32_t num);
int64_t pb_int(const PbMsg *m, uint32_t num, int64_t def);
int64_t pb_int_at(const PbMsg *m, uint32_t num, int i);
const char *pb_str(const PbMsg *m, uint32_t num);
const char *pb_str_at(const PbMsg *m, uint32_t num, int i);
Str pb_bytes(const PbMsg *m, uint32_t num);
Str pb_bytes_at(const PbMsg *m, uint32_t num, int i);
PbMsg *pb_msg(const PbMsg *m, uint32_t num);
PbMsg *pb_msg_at(const PbMsg *m, uint32_t num, int i);

/* Sets (s.p != NULL) or clears a singular string field. */
void pb_set_bytes(PbMsg *m, uint32_t num, Str s);
/* Sets (s != NULL) or clears a singular string field. */
void pb_set_str(PbMsg *m, uint32_t num, const char *s);

/* Raw wire helpers. */
typedef struct {
	const uint8_t *p, *end;
} PbReader;
bool pb_read_varint(PbReader *r, uint64_t *v);
bool pb_read_tag(PbReader *r, uint32_t *num, int *wt);
/* Skips a field value; for length-delimited values, val and vlen receive the payload. Groups are
 * skipped up to PB_MAX_DEPTH - depth levels deep. */
bool pb_skip_value(PbReader *r, uint32_t num, int wt, const uint8_t **val, size_t *vlen, int depth);
/* Iterates over the fields of raw wire bytes: returns false at the end or on malformed input. For
 * length-delimited values, val/vlen receive the payload; for varints, *v the value. */
typedef struct {
	uint32_t num;
	int wt;
	uint64_t v;
	const uint8_t *val;
	size_t vlen;
} PbRawField;
bool pb_next_field(PbReader *r, PbRawField *f);
/* A reader over the unknown fields of m (m may be NULL). */
PbReader pb_unknown_fields(const PbMsg *m);
void pb_put_varint(Buf *b, uint64_t v);
void pb_put_tag(Buf *b, uint32_t num, int wt);
void pb_put_bytes(Buf *b, uint32_t num, Str s); /* tag, length, bytes */

/* Schemas of descriptor.proto (as shipped by @bufbuild/protobuf v2.17.0, schema/descriptor.c)
 * and plugin.proto (schema/plugin.c). */
extern const PbMsgDef pbd_FileDescriptorProto, pbd_DescriptorProto, pbd_FieldDescriptorProto,
	pbd_FeatureSet, pbd_CodeGeneratorRequest;

/* Field numbers of descriptor.proto and plugin.proto used by the C code. */
enum {
	FDP_NAME = 1, FDP_PACKAGE = 2, FDP_DEPENDENCY = 3, FDP_MESSAGE_TYPE = 4, FDP_ENUM_TYPE = 5,
	FDP_SERVICE = 6, FDP_EXTENSION = 7, FDP_OPTIONS = 8, FDP_SOURCE_CODE_INFO = 9,
	FDP_PUBLIC_DEPENDENCY = 10, FDP_WEAK_DEPENDENCY = 11, FDP_SYNTAX = 12, FDP_EDITION = 14,
	DP_NAME = 1, DP_FIELD = 2, DP_NESTED_TYPE = 3, DP_ENUM_TYPE = 4, DP_EXTENSION_RANGE = 5,
	DP_EXTENSION = 6, DP_OPTIONS = 7, DP_ONEOF_DECL = 8,
	ER_START = 1, ER_END = 2,
	FD_NAME = 1, FD_EXTENDEE = 2, FD_NUMBER = 3, FD_LABEL = 4, FD_TYPE = 5, FD_TYPE_NAME = 6,
	FD_DEFAULT_VALUE = 7, FD_OPTIONS = 8, FD_ONEOF_INDEX = 9, FD_JSON_NAME = 10, FD_PROTO3_OPTIONAL = 17,
	LABEL_OPTIONAL = 1, LABEL_REQUIRED = 2, LABEL_REPEATED = 3,
	FO_PACKED = 2, FO_DEPRECATED = 3, FO_JSTYPE = 6, FO_RETENTION = 17, FO_TARGETS = 19,
	FO_EDITION_DEFAULTS = 20,
	ED_VALUE = 2, ED_EDITION = 3, /* FieldOptions.EditionDefault */
	EDP_NAME = 1, EDP_VALUE = 2, EDP_OPTIONS = 3, /* EnumDescriptorProto */
	EVDP_NAME = 1, EVDP_NUMBER = 2, EVDP_OPTIONS = 3,
	SDP_NAME = 1, SDP_METHOD = 2,
	MDP_NAME = 1, MDP_INPUT_TYPE = 2, MDP_OUTPUT_TYPE = 3, MDP_OPTIONS = 4,
	MDP_CLIENT_STREAMING = 5, MDP_SERVER_STREAMING = 6,
	ODP_NAME = 1,
	SCI_LOCATION = 1, LOC_PATH = 1, LOC_LEADING = 3, LOC_TRAILING = 4, LOC_LEADING_DETACHED = 6,
	CGR_FILE_TO_GENERATE = 1, CGR_PARAMETER = 2, CGR_PROTO_FILE = 15, CGR_SOURCE_FILE_DESCRIPTORS = 17,
	CGRESP_ERROR = 1, CGRESP_SUPPORTED_FEATURES = 2, CGRESP_MINIMUM_EDITION = 3,
	CGRESP_MAXIMUM_EDITION = 4, CGRESP_FILE = 15, CGRESP_FILE_NAME = 1, CGRESP_FILE_CONTENT = 15,
};

#endif
