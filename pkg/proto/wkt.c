/* The well-known types: the messages and enums declared in the google/protobuf/ files that
 * every protobuf runtime ships besides descriptor.proto (protobuf v36.2 src/google/protobuf). */
#include "proto.h"

#include <string.h>

static const struct {
	const char *name; /* after "google.protobuf." */
	uint8_t file;     /* index into files */
} wkts[] = {
	[PROTO_WKT_ANY] = {"Any", 0},
	[PROTO_WKT_API] = {"Api", 1},
	[PROTO_WKT_BOOL_VALUE] = {"BoolValue", 9},
	[PROTO_WKT_BYTES_VALUE] = {"BytesValue", 9},
	[PROTO_WKT_DOUBLE_VALUE] = {"DoubleValue", 9},
	[PROTO_WKT_DURATION] = {"Duration", 2},
	[PROTO_WKT_EMPTY] = {"Empty", 3},
	[PROTO_WKT_ENUM] = {"Enum", 8},
	[PROTO_WKT_ENUM_VALUE] = {"EnumValue", 8},
	[PROTO_WKT_FIELD] = {"Field", 8},
	[PROTO_WKT_FIELD_CARDINALITY] = {"Field.Cardinality", 8},
	[PROTO_WKT_FIELD_KIND] = {"Field.Kind", 8},
	[PROTO_WKT_FIELD_MASK] = {"FieldMask", 4},
	[PROTO_WKT_FLOAT_VALUE] = {"FloatValue", 9},
	[PROTO_WKT_INT32_VALUE] = {"Int32Value", 9},
	[PROTO_WKT_INT64_VALUE] = {"Int64Value", 9},
	[PROTO_WKT_LIST_VALUE] = {"ListValue", 6},
	[PROTO_WKT_METHOD] = {"Method", 1},
	[PROTO_WKT_MIXIN] = {"Mixin", 1},
	[PROTO_WKT_NULL_VALUE] = {"NullValue", 6},
	[PROTO_WKT_OPTION] = {"Option", 8},
	[PROTO_WKT_SOURCE_CONTEXT] = {"SourceContext", 5},
	[PROTO_WKT_STRING_VALUE] = {"StringValue", 9},
	[PROTO_WKT_STRUCT] = {"Struct", 6},
	[PROTO_WKT_SYNTAX] = {"Syntax", 8},
	[PROTO_WKT_TIMESTAMP] = {"Timestamp", 7},
	[PROTO_WKT_TYPE] = {"Type", 8},
	[PROTO_WKT_UINT32_VALUE] = {"UInt32Value", 9},
	[PROTO_WKT_UINT64_VALUE] = {"UInt64Value", 9},
	[PROTO_WKT_VALUE] = {"Value", 6},
};

static const char *const files[] = {
	"google/protobuf/any.proto",        "google/protobuf/api.proto",
	"google/protobuf/duration.proto",   "google/protobuf/empty.proto",
	"google/protobuf/field_mask.proto", "google/protobuf/source_context.proto",
	"google/protobuf/struct.proto",     "google/protobuf/timestamp.proto",
	"google/protobuf/type.proto",       "google/protobuf/wrappers.proto",
};

#define N_WKTS ((int)(sizeof wkts / sizeof *wkts))

ProtoWkt proto_wkt(const char *full_name)
{
	static const char prefix[] = "google.protobuf.";
	if (strncmp(full_name, prefix, sizeof prefix - 1) != 0)
		return PROTO_WKT_NONE;
	for (int i = 1; i < N_WKTS; i++)
		if (strcmp(full_name + sizeof prefix - 1, wkts[i].name) == 0)
			return (ProtoWkt)i;
	return PROTO_WKT_NONE;
}

const char *proto_wkt_full_name(ProtoWkt w)
{
	return w > PROTO_WKT_NONE && (int)w < N_WKTS ? xcat("google.protobuf.", wkts[w].name) : "";
}

const char *proto_wkt_file(ProtoWkt w) { return w > PROTO_WKT_NONE && (int)w < N_WKTS ? files[wkts[w].file] : ""; }

bool proto_is_wkt_file(const char *file_name)
{
	for (size_t i = 0; i < sizeof files / sizeof *files; i++)
		if (strcmp(file_name, files[i]) == 0)
			return true;
	return false;
}

bool proto_wkt_is_wrapper(ProtoWkt w) { return w > PROTO_WKT_NONE && (int)w < N_WKTS && wkts[w].file == 9; }
