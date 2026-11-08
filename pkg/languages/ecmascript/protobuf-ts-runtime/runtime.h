/* Port of the parts of @protobuf-ts/runtime 2.11.1 (https://github.com/timostamm/protobuf-ts,
 * packages/runtime/src, Apache-2.0) that @protobuf-ts/plugin runs while generating code: the
 * reflection info of message types (reflection-info.ts), reflectionCreate and
 * reflectionScalarDefault, the reflection binary reader with BinaryReader (custom options are
 * read with it), the reflection JSON writer (options are emitted as JSON) and enum objects
 * (enum-object.ts). Values are the plain JavaScript values of pkg/languages/ecmascript/std.
 *
 * Errors that upstream throws as native errors (malformed data, "cannot convert to safe number")
 * stop with fail() and the error message. */
#ifndef PROTOBUF_TS_RUNTIME_H
#define PROTOBUF_TS_RUNTIME_H

#include "es_std.h"

/* ScalarType uses the numbers of FieldDescriptorProto.Type (DOUBLE = 1 ... SINT64 = 18). */
enum { RT_LONG_BIGINT = 0, RT_LONG_STRING = 1, RT_LONG_NUMBER = 2, RT_LONG_UNSET = -1 };
enum { RT_REPEAT_NO = 0, RT_REPEAT_PACKED = 1, RT_REPEAT_UNPACKED = 2 };
enum { RT_SCALAR = 1, RT_ENUM, RT_MESSAGE, RT_MAP };

typedef struct RtMessageType RtMessageType;
typedef struct RtEnumInfo RtEnumInfo;

/* Resolves the lazy T() of enum and message fields (ref is the target's descriptor). */
typedef struct {
	RtMessageType *(*message)(void *ctx, void *ref);
	RtEnumInfo *(*enm)(void *ctx, void *ref);
	void *ctx;
} RtResolver;

/* A FieldInfo after normalizeFieldInfo. */
typedef struct {
	int no;
	const char *name;
	int kind;
	const char *local_name, *json_name;
	const char *oneof; /* NULL: undefined */
	int repeat;
	bool opt;
	int T, L;     /* scalar: ScalarType and LongType (RT_LONG_UNSET if undefined) */
	void *ref;    /* enum, message, and map values of those kinds: the descriptor for T() */
	int K;        /* map */
	int V_kind, V_T, V_L;
	JsVal *options; /* NULL: undefined */
	void *desc;     /* the plugin's field descriptor */
} RtFieldInfo;

/* A PartialFieldInfo before normalization: fields left NULL / unset take their defaults. */
typedef struct {
	RtFieldInfo f;
	bool has_local_name, has_json_name, has_repeat, has_opt;
} RtPartialFieldInfo;

struct RtMessageType {
	const char *type_name;
	RtFieldInfo **fields;
	int n_fields;
	JsVal *options; /* {} if none */
	const RtResolver *resolver;
	Map *by_no; /* fieldNoToField, created on first read */
};

/* An enum object ({0: "A", A: 0, ...}) with the values in creation order. */
typedef struct {
	const char **names;
	int32_t *numbers;
	int n;
	Map by_number; /* decimal number -> index + 1 of the last value with it */
	Map by_name;   /* name -> index + 1 */
} RtEnumObject;

struct RtEnumInfo {
	const char *type_name;
	RtEnumObject *obj;
	const char *shared_prefix; /* NULL: no third element */
};

/* lower-camel-case.ts */
const char *rt_lower_camel_case(const char *snake_case);

/* new MessageType(name, fields, options): normalizes the field infos (in place). */
RtMessageType *rt_message_type(const char *type_name, RtPartialFieldInfo **fields, int n, JsVal *options,
			       const RtResolver *resolver);
/* reflectionCreate: the message with the default values of all non-optional fields. */
JsVal *rt_create(const RtMessageType *t);
/* reflectionScalarDefault */
JsVal *rt_scalar_default(int type, int long_type);
/* MessageType.fromBinary(bytes, {readUnknownField: false}) */
JsVal *rt_from_binary(RtMessageType *t, Str bytes);
/* MessageType.toJson(message) with the default JsonWriteOptions. */
JsVal *rt_to_json(const RtMessageType *t, const JsVal *message);

/* RuntimeEnumBuilder of the plugin's interpreter and isEnumObject/listEnumValues of the runtime.
 * Fails ("duplicate names", "not a typescript enum object") like upstream. */
RtEnumObject *rt_enum_object(const char **names, const int32_t *numbers, int n);
/* The name for a number (enumObject[number]: the last value with the number), or NULL. */
const char *rt_enum_name(const RtEnumObject *o, int32_t number);

#endif
