// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The classes of src/google/protobuf/compiler/java/{generator_factory.h,generator_common.h,
 * full/field_generator.h,lite/field_generator.h} as C structs: a field generator is a JvFieldGen
 * with the vtable of its class (JvFieldGenOps, full and lite methods together); the message,
 * builder, enum, extension and service generators of full/ and lite/ share their structs, the
 * methods of each class are functions named <Class>_<Method>. */
#ifndef JAVA_GENERATOR_INTERNAL_H
#define JAVA_GENERATOR_INTERNAL_H

#include "libprotoc_java.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

typedef struct JvFieldGen JvFieldGen;

/* ImmutableFieldGenerator (full) and ImmutableFieldLiteGenerator (lite) */
typedef struct {
	int (*GetMessageBitIndex)(const JvFieldGen *g);
	int (*GetBuilderBitIndex)(const JvFieldGen *g);
	int (*GetNumBitsForMessage)(const JvFieldGen *g);
	int (*GetNumBitsForBuilder)(const JvFieldGen *g);
	void (*GenerateInterfaceMembers)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateMembers)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateBuilderMembers)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateInitializationCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateBuilderClearCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateMergingCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateBuildingCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateBuilderParsingCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateSerializedSizeCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateFieldBuilderInitializationCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateBuilderParsingCodeFromPacked)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateEqualsCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateHashCode)(const JvFieldGen *g, LpPrinter *printer);
	const char *(*GetBoxedType)(const JvFieldGen *g);
	void (*GenerateSerializationCode)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateFieldInfo)(const JvFieldGen *g, LpPrinter *printer, JvUtf16 *output);
	void (*GenerateKotlinDslMembers)(const JvFieldGen *g, LpPrinter *printer);
	void (*GenerateKotlinOrNull)(const JvFieldGen *g, LpPrinter *printer);
} JvFieldGenOps;

struct JvFieldGen {
	const JvFieldGenOps *ops;
	const LpField *descriptor;
	int message_bit_index, builder_bit_index;
	LpVars variables;
	JvContext *context;
};

/* field_generators_.get(field).Method(...) */
#define JV_FG(g, field) ((g)->field_generators[(field)->index])
#define JV_FGCALL(g, field, m, ...) (JV_FG(g, field)->ops->m(JV_FG(g, field), __VA_ARGS__))
#define JV_FGCALL0(g, field, m) (JV_FG(g, field)->ops->m(JV_FG(g, field)))

/* MessageGenerator, ImmutableMessageGenerator, ImmutableMessageLiteGenerator,
 * MessageBuilderGenerator, MessageBuilderLiteGenerator */
typedef struct JvMessageGen JvMessageGen;
struct JvMessageGen {
	const LpMessage *descriptor;
	const LpOneof **oneofs; /* oneofs_: the real oneofs by index */
	int n_oneofs;
	JvContext *context;
	JvFieldGen **field_generators; /* by field index */
	bool lite;
};

/* EnumNonLiteGenerator, EnumLiteGenerator */
typedef struct {
	const LpEnumValue *value, *canonical_value;
} JvEnumAlias;

typedef struct {
	const LpEnum *descriptor;
	const LpEnumValue **canonical_values;
	int n_canonical_values;
	JvEnumAlias *aliases;
	int n_aliases;
	bool immutable_api;
	JvContext *context;
} JvEnumGen;

/* ImmutableExtensionGenerator, ImmutableExtensionLiteGenerator */
typedef struct {
	const LpField *descriptor;
	char *scope;
	JvContext *context;
} JvExtensionGen;

/* ImmutableServiceGenerator */
typedef struct {
	const LpService *descriptor;
	JvContext *context;
} JvServiceGen;

/* GeneratorFactory (full/generator_factory.cc, lite/generator_factory.cc) */
typedef struct {
	bool lite;
	JvContext *context;
} JvGeneratorFactory;

/* google::protobuf::internal::IsOss(): the open source release */
#define JV_IS_OSS true

/* checks of the C++ code (ABSL_CHECK fails the request; ABSL_DCHECK is compiled out) */
#define JV_CHECK(cond) ((cond) ? (void)0 : fail("Check failed: %s", #cond))
#define JV_DCHECK(cond) ((void)0)

/* io::Printer::Print(text) with no variables */
#define JV_PRINT(printer, text) lp_print((printer), (text), NULL)

static char *jv_i(int64_t v) { return xfmt("%lld", (long long)v); }
static char *jv_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return r;
}

/* ClassNameResolver::GetImmutableClassName and NestedInFileClass(descriptor) by descriptor
 * type */
#define jv_immutable_class_name(d)                                                                      \
	_Generic((d), const LpMessage *: jv_get_class_name_message, LpMessage *: jv_get_class_name_message, \
		 const LpEnum *: jv_get_class_name_enum, LpEnum *: jv_get_class_name_enum,                       \
		 const LpService *: jv_get_class_name_service, LpService *: jv_get_class_name_service,           \
		 const LpFile *: jv_get_class_name_file, LpFile *: jv_get_class_name_file)((d), true, false)
#define jv_nested_in_file_class_impl(d)                                                                       \
	_Generic((d), const LpMessage *: jv_nested_in_file_class_impl_message,                                   \
		 LpMessage *: jv_nested_in_file_class_impl_message, const LpEnum *: jv_nested_in_file_class_impl_enum, \
		 LpEnum *: jv_nested_in_file_class_impl_enum, const LpService *: jv_nested_in_file_class_impl_service, \
		 LpService *: jv_nested_in_file_class_impl_service)(d)


/* descriptor->options().<field>() by descriptor type */
static bool jv_service_deprecated(const LpService *s) { return jv_opt_bool((const ProtoDesc *)s, JV_SVCOPT_DEPRECATED); }
static bool jv_method_deprecated(const LpMethod *m) { return jv_opt_bool((const ProtoDesc *)m, JV_METHOPT_DEPRECATED); }
#define OPT_deprecated(d)                                                                                    \
	_Generic((d), const LpField *: jv_field_deprecated, LpField *: jv_field_deprecated,                       \
		 const LpMessage *: jv_message_deprecated, LpMessage *: jv_message_deprecated,                       \
		 const LpEnum *: jv_enum_deprecated, LpEnum *: jv_enum_deprecated,                                   \
		 const LpEnumValue *: jv_enum_value_deprecated, LpEnumValue *: jv_enum_value_deprecated,             \
		 const LpFile *: jv_file_deprecated, LpFile *: jv_file_deprecated,                                   \
		 const LpService *: jv_service_deprecated, LpService *: jv_service_deprecated,                       \
		 const LpMethod *: jv_method_deprecated, LpMethod *: jv_method_deprecated)(d)
#define OPT_message_set_wire_format(d) jv_opt_bool((const ProtoDesc *)(d), JV_MSGOPT_MESSAGE_SET_WIRE_FORMAT)
#define OPT_no_standard_descriptor_accessor(d) \
	jv_opt_bool((const ProtoDesc *)(d), JV_MSGOPT_NO_STANDARD_DESCRIPTOR_ACCESSOR)
#define OPT_map_entry(d) jv_opt_bool((const ProtoDesc *)(d), JV_MSGOPT_MAP_ENTRY)
#define OPT_packed(d) jv_opt_bool((const ProtoDesc *)(d), 2)

#define jv_get_capitalized_type_(f, immutable, options) jv_get_capitalized_type((f), (immutable))
#define FieldTypeName(t) jv_field_type_name(t)
#define jv_immutable_default_value_(d, options) jv_default_value((d), true, (options))
#define jv_default_value_(d, immutable, options) jv_default_value((d), (immutable), (options))
#define jv_has_descriptor_methods_(d, enforce_lite) jv_has_descriptor_methods(enforce_lite)
#define jv_context_has_generated_methods_(c, d) jv_context_has_generated_methods((c), (d))

/* NestedInFileClass(descriptor) (names.h) and NestedInFileClass(descriptor, immutable)
 * (helpers.h), called with *descriptor */
#define JV_NIFC_SEL(_1, _2, name, ...) name
#define jv_nested_in_file_class_(...) JV_NIFC_SEL(__VA_ARGS__, JV_NIFC2, JV_NIFC1, 0)(__VA_ARGS__)
#define JV_NIFC1(x) jv_nested_in_file_class_impl(&(x))
#define JV_NIFC2(x, immutable)                                                                              \
	_Generic((x), LpMessage: jv_nested_in_file_class_message, LpEnum: jv_nested_in_file_class_enum,         \
		 LpService: jv_nested_in_file_class_service)(&(x), (immutable))
#define jv_is_own_file_(d, immutable)                                                                       \
	_Generic((d), const LpMessage *: jv_is_own_file_message, LpMessage *: jv_is_own_file_message,           \
		 const LpEnum *: jv_is_own_file_enum, LpEnum *: jv_is_own_file_enum,                                 \
		 const LpService *: jv_is_own_file_service, LpService *: jv_is_own_file_service)((d), (immutable))
#define jv_get_java_immutable_class_name_(d)                                                              \
	_Generic((d), const LpMessage *: jv_get_java_immutable_class_name_message,                            \
		 LpMessage *: jv_get_java_immutable_class_name_message,                                               \
		 const LpEnum *: jv_get_java_immutable_class_name_enum, LpEnum *: jv_get_java_immutable_class_name_enum, \
		 const LpService *: jv_get_java_immutable_class_name_service,                                         \
		 LpService *: jv_get_java_immutable_class_name_service)(d)
/* MaybePrintGeneratedAnnotation(context, printer, descriptor, immutable[, suffix]) */
#define JV_MPGA_SEL(_1, _2, _3, _4, _5, name, ...) name
#define jv_maybe_print_generated_annotation_(...) \
	JV_MPGA_SEL(__VA_ARGS__, JV_MPGA5, JV_MPGA4, 0, 0, 0)(__VA_ARGS__)
#define JV_MPGA4(c, p, d, immutable) JV_MPGA5(c, p, d, immutable, "")
#define JV_MPGA5(c, p, d, immutable, suffix)                                                              \
	_Generic((d), const LpMessage *: jv_maybe_print_generated_annotation_message,                         \
		 LpMessage *: jv_maybe_print_generated_annotation_message,                                            \
		 const LpEnum *: jv_maybe_print_generated_annotation_enum, LpEnum *: jv_maybe_print_generated_annotation_enum, \
		 const LpService *: jv_maybe_print_generated_annotation_service,                                      \
		 LpService *: jv_maybe_print_generated_annotation_service)((c), (p), (d), (immutable), (suffix))

/* ClassNameResolver::GetClassName(descriptor, immutable) by descriptor type */
#define jv_get_class_name_gen(d, immutable)                                                             \
	_Generic((d), const LpMessage *: jv_get_class_name_message, LpMessage *: jv_get_class_name_message, \
		 const LpEnum *: jv_get_class_name_enum, LpEnum *: jv_get_class_name_enum,                       \
		 const LpService *: jv_get_class_name_service, LpService *: jv_get_class_name_service,           \
		 const LpFile *: jv_get_class_name_file, LpFile *: jv_get_class_name_file)((d), (immutable), false)

/* Descriptor::map_key(), map_value() */
static inline const LpField *jv_map_key_of(const LpMessage *m) { return jv_is_map_entry(m) && m->n_fields >= 2 ? &m->fields[0] : NULL; }
static inline const LpField *jv_map_value_of(const LpMessage *m) { return jv_is_map_entry(m) && m->n_fields >= 2 ? &m->fields[1] : NULL; }

#define JV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define JV_MAX(a, b) ((a) > (b) ? (a) : (b))

/* FieldGenerator::GenerateSerializationCode through the vtable (message_serialization.h) */
static void jv_fg_serialize(const void *gen, LpPrinter *printer)
{
	const JvFieldGen *g = gen;
	g->ops->GenerateSerializationCode(g, printer);
}

/* OneofDescriptor::field(i) */
#define ONEOF_FIELD(o, i) proto_oneof_field((o), (i))

/* WireFormatLite */
enum {
	JV_WIRETYPE_VARINT = 0, JV_WIRETYPE_FIXED64 = 1, JV_WIRETYPE_LENGTH_DELIMITED = 2,
	JV_WIRETYPE_START_GROUP = 3, JV_WIRETYPE_END_GROUP = 4, JV_WIRETYPE_FIXED32 = 5,
};
static uint32_t jv_wire_format_lite_make_tag(int number, int wire_type) { return ((uint32_t)number << 3) | (uint32_t)wire_type; }

/* WireFormat::MakeTag / TagSize (wire_format.h, wire_format_lite.h) */
static int jv_wire_type_for_field_type(int type)
{
	switch (type) {
	case PT_INT32:
	case PT_INT64:
	case PT_UINT32:
	case PT_UINT64:
	case PT_SINT32:
	case PT_SINT64:
	case PT_BOOL:
	case PT_ENUM: return 0;
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_DOUBLE: return 1;
	case PT_FIXED32:
	case PT_SFIXED32:
	case PT_FLOAT: return 5;
	case PT_GROUP: return 3;
	default: return 2;
	}
}
static int32_t jv_make_tag(const LpField *f)
{
	return (int32_t)(((uint32_t)f->number << 3) | (proto_field_is_packed(f) ? 2u : (uint32_t)jv_wire_type_for_field_type(f->type)));
}
static int jv_varint_size32(uint32_t v)
{
	int n = 1;
	while (v >= 0x80) {
		v >>= 7;
		n++;
	}
	return n;
}
static int jv_tag_size(int number, int type)
{
	int result = jv_varint_size32((uint32_t)number << 3);
	if (type == PT_GROUP)
		return result * 2; /* groups have both a start and an end tag */
	return result;
}

#endif
