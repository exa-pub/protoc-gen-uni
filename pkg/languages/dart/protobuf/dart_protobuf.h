/* The Dart protobuf runtime (package:protobuf 6.x, the workspace copy of protoc_plugin 25.1.0) as
 * far as the output of protoc_plugin and connect-dart depends on it: the schemas of the messages
 * protoc_plugin reads (its generated descriptor.proto, plugin.proto, dart_options.proto and
 * google/api/client.proto extensions), GeneratedMessage.writeToBuffer and writeToJsonMap;
 * mergeFromBuffer and == (connect-dart, on the pkg/pb descriptor schemas).
 *
 * Messages are pkg/pb PbMsg values. The registered extensions (Dart_options and Client
 * registerAllExtensions) are fields of these schemas: the Dart runtime writes extensions after the
 * fields sorted by tag, and their numbers are above all field numbers. */
#ifndef DART_PROTOBUF_H
#define DART_PROTOBUF_H

#include "pb.h"

extern const PbMsgDef dpb_CodeGeneratorRequest, dpb_FileDescriptorProto, dpb_DescriptorProto,
	dpb_EnumDescriptorProto, dpb_ServiceDescriptorProto, dpb_FeatureSet;

/* Extension numbers (dart_options.proto, google/api/client.proto). */
enum {
	DPB_EXT_IMPORTS = 28125061, DPB_EXT_DEFAULT_MIXIN = 96128839, DPB_EXT_MIXIN = 96128839,
	DPB_EXT_OVERRIDE_GETTER = 28205290, DPB_EXT_OVERRIDE_SETTER = 28937366,
	DPB_EXT_OVERRIDE_HAS_METHOD = 28937461, DPB_EXT_OVERRIDE_CLEAR_METHOD = 28907907,
	DPB_EXT_DART_NAME = 28700919, DPB_EXT_DEFAULT_HOST = 1049, DPB_EXT_OAUTH_SCOPES = 1050,
	DPB_IMPORTS_MIXINS = 1, DPB_MIXIN_NAME = 1, DPB_MIXIN_IMPORT_FROM = 2, DPB_MIXIN_PARENT = 3,
};

/* GeneratedMessage.writeToBuffer: fields by tag (extensions included, see above), then the unknown
 * fields grouped by tag in the order of their first occurrence, per tag varints, fixed32, fixed64,
 * length-delimited values and groups. */
void dpb_write(const PbMsg *m, Buf *out);

/* A JSON value of writeToJsonMap: maps keep insertion order; numbers are kept as their Dart
 * toString text. */
typedef enum { JV_MAP, JV_LIST, JV_STRING, JV_NUMBER, JV_BOOL } JvKind;
typedef struct Jv Jv;
struct Jv {
	JvKind kind;
	Str s;            /* string value, number text */
	bool b;
	int n, cap;       /* map entries or list elements */
	const char **keys;
	Jv **vals;
};

/* GeneratedMessage.writeToJsonMap: keys are tag numbers, unknown fields are left out, int64 and
 * uint64 values are strings, bytes base64, enums numbers. */
Jv *dpb_json_map(const PbMsg *m);

/* GeneratedMessage.mergeFromBuffer: parses raw into m (a message of any pkg/pb schema, e.g.
 * pbd_CodeGeneratorRequest) like the Dart runtime does: recursion limit 100, values of the field
 * types (bool and 32-bit types from the low 32 bits of a varint), a scalar field also accepts a
 * length-delimited tag and then reads its value at the length, unknown values of closed enums
 * and fields with other wire types are kept as unknown fields. Returns NULL, or the toString()
 * of what the runtime throws (InvalidProtocolBufferException, ArgumentError, RangeError).
 * Strings are decoded with replacement characters for malformed UTF-8. */
const char *dpb_merge_from_buffer(PbMsg *m, Str raw);

/* GeneratedMessage ==: the same set fields with equal values (doubles by value, NaN unequal),
 * equal unknown fields per field number. Messages must be read by dpb_merge_from_buffer. */
bool dpb_equals(const PbMsg *a, const PbMsg *b);
/* A hash consistent with dpb_equals; *self_only is set when m holds a NaN (equal only to
 * itself). */
uint64_t dpb_hash(const PbMsg *m, bool *self_only);

#endif
