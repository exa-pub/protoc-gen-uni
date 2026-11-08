/* Port of the decoding of prost 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0:
 * src/encoding.rs, src/encoding/varint.rs, src/encoding/wire_type.rs, src/error.rs, and the
 * merge_field that prost-derive 0.14.1 generates) for the messages of prost-types 0.14.1
 * (src/protobuf.rs, src/compiler.rs): `CodeGeneratorRequest::decode` of the protoc-gen-prost
 * plugins. Only its errors matter here; the request itself is read with pkg/pb.
 *
 * prost-types 0.14.1 has the descriptor.proto of protobuf 3.x: fields added since (editions,
 * features, option_dependency, source_file_descriptors, ...) are unknown fields to it. */
#include "prost.h"

#include <string.h>

enum { K_VARINT, K_REPEATED_VARINT, K_I64, K_STRING, K_BYTES, K_MESSAGE };

typedef struct {
	uint16_t tag;
	uint8_t kind, msg; /* msg: the message type of K_MESSAGE */
	uint16_t name;     /* in names: stringify! of the Rust field */
} DecodeField;

typedef struct {
	uint16_t name;    /* in names: stringify! of the Rust struct */
	uint8_t first, n; /* fields[first:first + n], sorted by tag */
} DecodeMessage;

/* The messages and fields of prost-types 0.14.1 reachable from CodeGeneratorRequest. */
enum {
	M_CodeGeneratorRequest,
	M_FileDescriptorProto,
	M_DescriptorProto,
	M_FieldDescriptorProto,
	M_FieldOptions,
	M_UninterpretedOption,
	M_NamePart,
	M_EnumDescriptorProto,
	M_EnumValueDescriptorProto,
	M_EnumValueOptions,
	M_EnumOptions,
	M_EnumReservedRange,
	M_ExtensionRange,
	M_ExtensionRangeOptions,
	M_OneofDescriptorProto,
	M_OneofOptions,
	M_MessageOptions,
	M_ReservedRange,
	M_ServiceDescriptorProto,
	M_MethodDescriptorProto,
	M_MethodOptions,
	M_ServiceOptions,
	M_FileOptions,
	M_SourceCodeInfo,
	M_Location,
	M_Version,
};
/* The names, NUL-separated (offsets below). */
static const char names[] =
	"CodeGeneratorRequest\0FileDescriptorProto\0DescriptorProto\0FieldDescriptorProto\0"
	"FieldOptions\0UninterpretedOption\0NamePart\0EnumDescriptorProto\0"
	"EnumValueDescriptorProto\0EnumValueOptions\0EnumOptions\0EnumReservedRange\0"
	"ExtensionRange\0ExtensionRangeOptions\0OneofDescriptorProto\0OneofOptions\0"
	"MessageOptions\0ReservedRange\0ServiceDescriptorProto\0MethodDescriptorProto\0"
	"MethodOptions\0ServiceOptions\0FileOptions\0SourceCodeInfo\0Location\0Version\0"
	"file_to_generate\0parameter\0compiler_version\0proto_file\0name\0package\0dependency\0"
	"message_type\0enum_type\0service\0extension\0options\0source_code_info\0"
	"public_dependency\0weak_dependency\0syntax\0field\0nested_type\0extension_range\0"
	"oneof_decl\0reserved_range\0reserved_name\0extendee\0number\0label\0r#type\0type_name\0"
	"default_value\0oneof_index\0json_name\0proto3_optional\0ctype\0packed\0deprecated\0lazy\0"
	"jstype\0weak\0uninterpreted_option\0identifier_value\0positive_int_value\0"
	"negative_int_value\0double_value\0string_value\0aggregate_value\0name_part\0is_extension\0"
	"value\0allow_alias\0start\0end\0message_set_wire_format\0no_standard_descriptor_accessor\0"
	"map_entry\0method\0input_type\0output_type\0client_streaming\0server_streaming\0"
	"idempotency_level\0java_package\0java_outer_classname\0optimize_for\0java_multiple_files\0"
	"go_package\0cc_generic_services\0java_generic_services\0py_generic_services\0"
	"java_generate_equals_and_hash\0java_string_check_utf8\0cc_enable_arenas\0"
	"objc_class_prefix\0csharp_namespace\0swift_prefix\0php_class_prefix\0php_namespace\0"
	"php_generic_services\0php_metadata_namespace\0ruby_package\0location\0path\0span\0"
	"leading_comments\0trailing_comments\0leading_detached_comments\0major\0minor\0patch\0"
	"suffix\0";
static const DecodeField fields[] = {
	{1, K_STRING, 0, 430}, /* CodeGeneratorRequest.file_to_generate */
	{2, K_STRING, 0, 447}, /* CodeGeneratorRequest.parameter */
	{3, K_MESSAGE, M_Version, 457}, /* CodeGeneratorRequest.compiler_version */
	{15, K_MESSAGE, M_FileDescriptorProto, 474}, /* CodeGeneratorRequest.proto_file */
	{1, K_STRING, 0, 485}, /* FileDescriptorProto.name */
	{2, K_STRING, 0, 490}, /* FileDescriptorProto.package */
	{3, K_STRING, 0, 498}, /* FileDescriptorProto.dependency */
	{4, K_MESSAGE, M_DescriptorProto, 509}, /* FileDescriptorProto.message_type */
	{5, K_MESSAGE, M_EnumDescriptorProto, 522}, /* FileDescriptorProto.enum_type */
	{6, K_MESSAGE, M_ServiceDescriptorProto, 532}, /* FileDescriptorProto.service */
	{7, K_MESSAGE, M_FieldDescriptorProto, 540}, /* FileDescriptorProto.extension */
	{8, K_MESSAGE, M_FileOptions, 550}, /* FileDescriptorProto.options */
	{9, K_MESSAGE, M_SourceCodeInfo, 558}, /* FileDescriptorProto.source_code_info */
	{10, K_REPEATED_VARINT, 0, 575}, /* FileDescriptorProto.public_dependency */
	{11, K_REPEATED_VARINT, 0, 593}, /* FileDescriptorProto.weak_dependency */
	{12, K_STRING, 0, 609}, /* FileDescriptorProto.syntax */
	{1, K_STRING, 0, 485}, /* DescriptorProto.name */
	{2, K_MESSAGE, M_FieldDescriptorProto, 616}, /* DescriptorProto.field */
	{3, K_MESSAGE, M_DescriptorProto, 622}, /* DescriptorProto.nested_type */
	{4, K_MESSAGE, M_EnumDescriptorProto, 522}, /* DescriptorProto.enum_type */
	{5, K_MESSAGE, M_ExtensionRange, 634}, /* DescriptorProto.extension_range */
	{6, K_MESSAGE, M_FieldDescriptorProto, 540}, /* DescriptorProto.extension */
	{7, K_MESSAGE, M_MessageOptions, 550}, /* DescriptorProto.options */
	{8, K_MESSAGE, M_OneofDescriptorProto, 650}, /* DescriptorProto.oneof_decl */
	{9, K_MESSAGE, M_ReservedRange, 661}, /* DescriptorProto.reserved_range */
	{10, K_STRING, 0, 676}, /* DescriptorProto.reserved_name */
	{1, K_STRING, 0, 485}, /* FieldDescriptorProto.name */
	{2, K_STRING, 0, 690}, /* FieldDescriptorProto.extendee */
	{3, K_VARINT, 0, 699}, /* FieldDescriptorProto.number */
	{4, K_VARINT, 0, 706}, /* FieldDescriptorProto.label */
	{5, K_VARINT, 0, 712}, /* FieldDescriptorProto.r#type */
	{6, K_STRING, 0, 719}, /* FieldDescriptorProto.type_name */
	{7, K_STRING, 0, 729}, /* FieldDescriptorProto.default_value */
	{8, K_MESSAGE, M_FieldOptions, 550}, /* FieldDescriptorProto.options */
	{9, K_VARINT, 0, 743}, /* FieldDescriptorProto.oneof_index */
	{10, K_STRING, 0, 755}, /* FieldDescriptorProto.json_name */
	{17, K_VARINT, 0, 765}, /* FieldDescriptorProto.proto3_optional */
	{1, K_VARINT, 0, 781}, /* FieldOptions.ctype */
	{2, K_VARINT, 0, 787}, /* FieldOptions.packed */
	{3, K_VARINT, 0, 794}, /* FieldOptions.deprecated */
	{5, K_VARINT, 0, 805}, /* FieldOptions.lazy */
	{6, K_VARINT, 0, 810}, /* FieldOptions.jstype */
	{10, K_VARINT, 0, 817}, /* FieldOptions.weak */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* FieldOptions.uninterpreted_option */
	{2, K_MESSAGE, M_NamePart, 485}, /* UninterpretedOption.name */
	{3, K_STRING, 0, 843}, /* UninterpretedOption.identifier_value */
	{4, K_VARINT, 0, 860}, /* UninterpretedOption.positive_int_value */
	{5, K_VARINT, 0, 879}, /* UninterpretedOption.negative_int_value */
	{6, K_I64, 0, 898}, /* UninterpretedOption.double_value */
	{7, K_BYTES, 0, 911}, /* UninterpretedOption.string_value */
	{8, K_STRING, 0, 924}, /* UninterpretedOption.aggregate_value */
	{1, K_STRING, 0, 940}, /* NamePart.name_part */
	{2, K_VARINT, 0, 950}, /* NamePart.is_extension */
	{1, K_STRING, 0, 485}, /* EnumDescriptorProto.name */
	{2, K_MESSAGE, M_EnumValueDescriptorProto, 963}, /* EnumDescriptorProto.value */
	{3, K_MESSAGE, M_EnumOptions, 550}, /* EnumDescriptorProto.options */
	{4, K_MESSAGE, M_EnumReservedRange, 661}, /* EnumDescriptorProto.reserved_range */
	{5, K_STRING, 0, 676}, /* EnumDescriptorProto.reserved_name */
	{1, K_STRING, 0, 485}, /* EnumValueDescriptorProto.name */
	{2, K_VARINT, 0, 699}, /* EnumValueDescriptorProto.number */
	{3, K_MESSAGE, M_EnumValueOptions, 550}, /* EnumValueDescriptorProto.options */
	{1, K_VARINT, 0, 794}, /* EnumValueOptions.deprecated */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* EnumValueOptions.uninterpreted_option */
	{2, K_VARINT, 0, 969}, /* EnumOptions.allow_alias */
	{3, K_VARINT, 0, 794}, /* EnumOptions.deprecated */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* EnumOptions.uninterpreted_option */
	{1, K_VARINT, 0, 981}, /* EnumReservedRange.start */
	{2, K_VARINT, 0, 987}, /* EnumReservedRange.end */
	{1, K_VARINT, 0, 981}, /* ExtensionRange.start */
	{2, K_VARINT, 0, 987}, /* ExtensionRange.end */
	{3, K_MESSAGE, M_ExtensionRangeOptions, 550}, /* ExtensionRange.options */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* ExtensionRangeOptions.uninterpreted_option */
	{1, K_STRING, 0, 485}, /* OneofDescriptorProto.name */
	{2, K_MESSAGE, M_OneofOptions, 550}, /* OneofDescriptorProto.options */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* OneofOptions.uninterpreted_option */
	{1, K_VARINT, 0, 991}, /* MessageOptions.message_set_wire_format */
	{2, K_VARINT, 0, 1015}, /* MessageOptions.no_standard_descriptor_accessor */
	{3, K_VARINT, 0, 794}, /* MessageOptions.deprecated */
	{7, K_VARINT, 0, 1047}, /* MessageOptions.map_entry */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* MessageOptions.uninterpreted_option */
	{1, K_VARINT, 0, 981}, /* ReservedRange.start */
	{2, K_VARINT, 0, 987}, /* ReservedRange.end */
	{1, K_STRING, 0, 485}, /* ServiceDescriptorProto.name */
	{2, K_MESSAGE, M_MethodDescriptorProto, 1057}, /* ServiceDescriptorProto.method */
	{3, K_MESSAGE, M_ServiceOptions, 550}, /* ServiceDescriptorProto.options */
	{1, K_STRING, 0, 485}, /* MethodDescriptorProto.name */
	{2, K_STRING, 0, 1064}, /* MethodDescriptorProto.input_type */
	{3, K_STRING, 0, 1075}, /* MethodDescriptorProto.output_type */
	{4, K_MESSAGE, M_MethodOptions, 550}, /* MethodDescriptorProto.options */
	{5, K_VARINT, 0, 1087}, /* MethodDescriptorProto.client_streaming */
	{6, K_VARINT, 0, 1104}, /* MethodDescriptorProto.server_streaming */
	{33, K_VARINT, 0, 794}, /* MethodOptions.deprecated */
	{34, K_VARINT, 0, 1121}, /* MethodOptions.idempotency_level */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* MethodOptions.uninterpreted_option */
	{33, K_VARINT, 0, 794}, /* ServiceOptions.deprecated */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* ServiceOptions.uninterpreted_option */
	{1, K_STRING, 0, 1139}, /* FileOptions.java_package */
	{8, K_STRING, 0, 1152}, /* FileOptions.java_outer_classname */
	{9, K_VARINT, 0, 1173}, /* FileOptions.optimize_for */
	{10, K_VARINT, 0, 1186}, /* FileOptions.java_multiple_files */
	{11, K_STRING, 0, 1206}, /* FileOptions.go_package */
	{16, K_VARINT, 0, 1217}, /* FileOptions.cc_generic_services */
	{17, K_VARINT, 0, 1237}, /* FileOptions.java_generic_services */
	{18, K_VARINT, 0, 1259}, /* FileOptions.py_generic_services */
	{20, K_VARINT, 0, 1279}, /* FileOptions.java_generate_equals_and_hash */
	{23, K_VARINT, 0, 794}, /* FileOptions.deprecated */
	{27, K_VARINT, 0, 1309}, /* FileOptions.java_string_check_utf8 */
	{31, K_VARINT, 0, 1332}, /* FileOptions.cc_enable_arenas */
	{36, K_STRING, 0, 1349}, /* FileOptions.objc_class_prefix */
	{37, K_STRING, 0, 1367}, /* FileOptions.csharp_namespace */
	{39, K_STRING, 0, 1384}, /* FileOptions.swift_prefix */
	{40, K_STRING, 0, 1397}, /* FileOptions.php_class_prefix */
	{41, K_STRING, 0, 1414}, /* FileOptions.php_namespace */
	{42, K_VARINT, 0, 1428}, /* FileOptions.php_generic_services */
	{44, K_STRING, 0, 1449}, /* FileOptions.php_metadata_namespace */
	{45, K_STRING, 0, 1472}, /* FileOptions.ruby_package */
	{999, K_MESSAGE, M_UninterpretedOption, 822}, /* FileOptions.uninterpreted_option */
	{1, K_MESSAGE, M_Location, 1485}, /* SourceCodeInfo.location */
	{1, K_REPEATED_VARINT, 0, 1494}, /* Location.path */
	{2, K_REPEATED_VARINT, 0, 1499}, /* Location.span */
	{3, K_STRING, 0, 1504}, /* Location.leading_comments */
	{4, K_STRING, 0, 1521}, /* Location.trailing_comments */
	{6, K_STRING, 0, 1539}, /* Location.leading_detached_comments */
	{1, K_VARINT, 0, 1565}, /* Version.major */
	{2, K_VARINT, 0, 1571}, /* Version.minor */
	{3, K_VARINT, 0, 1577}, /* Version.patch */
	{4, K_STRING, 0, 1583}, /* Version.suffix */
};
static const DecodeMessage messages[] = {
	{0, 0, 4}, /* CodeGeneratorRequest */
	{21, 4, 12}, /* FileDescriptorProto */
	{41, 16, 10}, /* DescriptorProto */
	{57, 26, 11}, /* FieldDescriptorProto */
	{78, 37, 7}, /* FieldOptions */
	{91, 44, 7}, /* UninterpretedOption */
	{111, 51, 2}, /* NamePart */
	{120, 53, 5}, /* EnumDescriptorProto */
	{140, 58, 3}, /* EnumValueDescriptorProto */
	{165, 61, 2}, /* EnumValueOptions */
	{182, 63, 3}, /* EnumOptions */
	{194, 66, 2}, /* EnumReservedRange */
	{212, 68, 3}, /* ExtensionRange */
	{227, 71, 1}, /* ExtensionRangeOptions */
	{249, 72, 2}, /* OneofDescriptorProto */
	{270, 74, 1}, /* OneofOptions */
	{283, 75, 5}, /* MessageOptions */
	{298, 80, 2}, /* ReservedRange */
	{312, 82, 3}, /* ServiceDescriptorProto */
	{335, 85, 6}, /* MethodDescriptorProto */
	{357, 91, 3}, /* MethodOptions */
	{371, 94, 2}, /* ServiceOptions */
	{386, 96, 21}, /* FileOptions */
	{398, 117, 1}, /* SourceCodeInfo */
	{413, 118, 5}, /* Location */
	{422, 123, 4}, /* Version */
};

/* WireType (Debug names), indexed by the WT_ values of pb.h. */
static const char *const wire_type_names[] = {"Varint",     "SixtyFourBit", "LengthDelimited",
					      "StartGroup", "EndGroup",     "ThirtyTwoBit"};

/* DecodeContext: RECURSION_LIMIT */
#define RECURSION_LIMIT 100

/* The buffer: like prost's &[u8], nested messages read from the whole rest of the input, and
 * merge_loop checks the limits. */
typedef struct {
	const uint8_t *p, *end;
	const char *description;
	Buf stack; /* "Message.field: " pushed while the error unwinds */
} Decoder;

static bool err(Decoder *d, const char *description)
{
	d->description = description;
	return false;
}

/* decode_varint (both the slice and the slow path) */
static bool decode_varint(Decoder *d, uint64_t *v)
{
	uint64_t value = 0;
	size_t avail = (size_t)(d->end - d->p);
	for (size_t count = 0; count < 10 && count < avail; count++) {
		uint8_t byte = d->p[count];
		value |= (uint64_t)(byte & 0x7f) << (count * 7);
		if (byte <= 0x7f) {
			if (count == 9 && byte >= 0x02)
				break;
			d->p += count + 1;
			*v = value;
			return true;
		}
	}
	return err(d, "invalid varint");
}

static bool decode_key(Decoder *d, uint32_t *tag, int *wire_type)
{
	uint64_t key;
	if (!decode_varint(d, &key))
		return false;
	if (key > UINT32_MAX) {
		char t[21];
		int i = 20;
		t[i] = 0;
		do
			t[--i] = (char)('0' + key % 10);
		while (key /= 10);
		return err(d, xcat("invalid key value: ", t + i));
	}
	if ((key & 7) > 5)
		return err(d, xfmt("invalid wire type value: %d", (int)(key & 7)));
	*wire_type = (int)(key & 7);
	*tag = (uint32_t)key >> 3;
	if (*tag < 1)
		return err(d, "invalid tag value: 0");
	return true;
}

static bool check_wire_type(Decoder *d, int expected, int actual)
{
	if (expected == actual)
		return true;
	return err(d, xfmt("invalid wire type: %s (expected %s)", wire_type_names[actual], wire_type_names[expected]));
}

/* recurse_count: the DecodeContext */
static bool limit_reached(Decoder *d, int recurse_count)
{
	return recurse_count ? true : err(d, "recursion limit reached");
}

static bool skip_field(Decoder *d, int wire_type, uint32_t tag, int ctx)
{
	if (!limit_reached(d, ctx))
		return false;
	uint64_t len = 0;
	switch (wire_type) {
	case WT_VARINT:
		if (!decode_varint(d, &len))
			return false;
		len = 0;
		break;
	case WT_I32:
		len = 4;
		break;
	case WT_I64:
		len = 8;
		break;
	case WT_LEN:
		if (!decode_varint(d, &len))
			return false;
		break;
	case WT_SGROUP:
		for (;;) {
			uint32_t inner_tag;
			int inner_wire_type;
			if (!decode_key(d, &inner_tag, &inner_wire_type))
				return false;
			if (inner_wire_type == WT_EGROUP) {
				if (inner_tag != tag)
					return err(d, "unexpected end group tag");
				break;
			}
			if (!skip_field(d, inner_wire_type, inner_tag, ctx - 1))
				return false;
		}
		break;
	default:
		return err(d, "unexpected end group tag");
	}
	if (len > (uint64_t)(d->end - d->p))
		return err(d, "buffer underflow");
	d->p += len;
	return true;
}

/* The bytes of a length-delimited value (bytes::merge_one_copy without the wire type check). */
static bool take(Decoder *d, const uint8_t **p, size_t *n)
{
	uint64_t len;
	if (!decode_varint(d, &len))
		return false;
	if (len > (uint64_t)(d->end - d->p))
		return err(d, "buffer underflow");
	*p = d->p, *n = (size_t)len;
	d->p += len;
	return true;
}

/* str::from_utf8 */
static bool is_utf8(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n;) {
		uint32_t r;
		int w = utf8_decode((const char *)p + i, n - i, &r);
		if (r == 0xfffd && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

static bool merge_field(Decoder *d, const DecodeMessage *m, uint32_t tag, int wire_type, int ctx);

/* merge_loop of message::merge: a length, then the fields of m up to it. */
static bool merge_loop_message(Decoder *d, const DecodeMessage *m, int ctx)
{
	uint64_t len;
	if (!decode_varint(d, &len))
		return false;
	if (len > (uint64_t)(d->end - d->p))
		return err(d, "buffer underflow");
	const uint8_t *limit = d->p + len;
	while (d->p < limit) {
		uint32_t tag;
		int wire_type;
		if (!decode_key(d, &tag, &wire_type) || !merge_field(d, m, tag, wire_type, ctx))
			return false;
	}
	return d->p == limit || err(d, "delimited length exceeded");
}

/* merge_loop of merge_repeated_numeric: packed varints. */
static bool merge_loop_varints(Decoder *d)
{
	uint64_t len, v;
	if (!decode_varint(d, &len))
		return false;
	if (len > (uint64_t)(d->end - d->p))
		return err(d, "buffer underflow");
	const uint8_t *limit = d->p + len;
	while (d->p < limit)
		if (!decode_varint(d, &v))
			return false;
	return d->p == limit || err(d, "delimited length exceeded");
}

static const DecodeField *find_field(const DecodeMessage *m, uint32_t tag)
{
	for (int i = m->first; i < m->first + m->n; i++)
		if (fields[i].tag == tag)
			return &fields[i];
	return NULL;
}

/* Message::merge_field as prost-derive generates it: a known field by its type (the error gets
 * "Message.field" pushed), others skipped. */
static bool merge_field(Decoder *d, const DecodeMessage *m, uint32_t tag, int wire_type, int ctx)
{
	const DecodeField *f = find_field(m, tag);
	if (!f)
		return skip_field(d, wire_type, tag, ctx);
	uint64_t v;
	const uint8_t *p;
	size_t n;
	bool ok = false;
	switch (f->kind) {
	case K_VARINT:
		ok = check_wire_type(d, WT_VARINT, wire_type) && decode_varint(d, &v);
		break;
	case K_REPEATED_VARINT:
		ok = wire_type == WT_LEN ? merge_loop_varints(d)
					  : check_wire_type(d, WT_VARINT, wire_type) && decode_varint(d, &v);
		break;
	case K_I64:
		ok = check_wire_type(d, WT_I64, wire_type) &&
		     (d->end - d->p >= 8 ? (d->p += 8, true) : err(d, "buffer underflow"));
		break;
	case K_STRING:
		ok = check_wire_type(d, WT_LEN, wire_type) && take(d, &p, &n) &&
		     (is_utf8(p, n) || err(d, "invalid string value: data is not UTF-8 encoded"));
		break;
	case K_BYTES:
		ok = check_wire_type(d, WT_LEN, wire_type) && take(d, &p, &n);
		break;
	case K_MESSAGE:
		ok = check_wire_type(d, WT_LEN, wire_type) && limit_reached(d, ctx) &&
		     merge_loop_message(d, &messages[f->msg], ctx - 1);
		break;
	}
	if (!ok) {
		buf_adds(&d->stack, names + m->name);
		buf_addc(&d->stack, '.');
		buf_adds(&d->stack, names + f->name);
		buf_adds(&d->stack, ": ");
	}
	return ok;
}

COLD const char *prost_decode_request(Str raw)
{
	Decoder d = {(const uint8_t *)raw.p, (const uint8_t *)raw.p + raw.n, NULL, {0}};
	const DecodeMessage *m = &messages[M_CodeGeneratorRequest];
	while (d.p < d.end) {
		uint32_t tag;
		int wire_type;
		if (!decode_key(&d, &tag, &wire_type) || !merge_field(&d, m, tag, wire_type, RECURSION_LIMIT))
			return xcat("failed to decode Protobuf message: ", buf_str(&d.stack), d.description);
	}
	return NULL;
}
