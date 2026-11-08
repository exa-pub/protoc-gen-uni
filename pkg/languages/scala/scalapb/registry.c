/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* The messages as protobuf-java 3.25.8 sees them when protocgen parses the request with the
 * ScalaPB extensions registered (Scalapb.registerAllExtensions): the schemas of
 * google/protobuf/descriptor.proto and compiler/plugin.proto at v25.8 (src/google/protobuf), with
 * the scalapb.proto extensions (field 1020 of the options messages) as known fields. Their
 * serialization is protobuf-java's (jpb_encode, pkg/languages/java/protobuf-java). */

extern const PbMsgDef jd_FileDescriptorProto, jd_DescriptorProto, jd_ExtensionRange, jd_ReservedRange,
	jd_ExtensionRangeOptions, jd_Declaration, jd_FieldDescriptorProto, jd_OneofDescriptorProto,
	jd_EnumDescriptorProto, jd_EnumValueDescriptorProto, jd_ServiceDescriptorProto,
	jd_MethodDescriptorProto, jd_FileOptions, jd_MessageOptions, jd_FieldOptions, jd_EditionDefault,
	jd_OneofOptions, jd_EnumOptions, jd_EnumValueOptions, jd_ServiceOptions, jd_MethodOptions,
	jd_UninterpretedOption, jd_NamePart, jd_FeatureSet, jd_SourceCodeInfo, jd_Location,
	jd_CodeGeneratorRequest, jd_Version;
/* scalapb.proto (scalapb.options in Java) */
extern const PbMsgDef sp_ScalaPbOptions, sp_AuxMessageOptions, sp_AuxFieldOptions, sp_AuxEnumOptions,
	sp_AuxEnumValueOptions, sp_MessageOptions, sp_Collection, sp_FieldOptions, sp_EnumOptions,
	sp_EnumValueOptions, sp_OneofOptions, sp_FieldTransformation;

#define JS(num, t, l) {num, PT_##t, l, NULL, NULL, 0}
#define JM(num, l, d) {num, PT_MESSAGE, l, &d, NULL, 0}
#define JE(num, l, vals) {num, PT_ENUM, l, NULL, vals, (int)(sizeof(vals) / sizeof((vals)[0]))}
#define JDEF(name, fields) const PbMsgDef name = {(int)(sizeof(fields) / sizeof((fields)[0])), fields}

/* Closed enums (proto2): unknown values become unknown fields. */
static const int32_t jev_edition[] = {0, 998, 999, 1000, 1, 2, 99997, 99998, 99999};
static const int32_t jev_label[] = {1, 2, 3};
static const int32_t jev_type[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
static const int32_t jev_01[] = {0, 1};
static const int32_t jev_optimize[] = {1, 2, 3};
static const int32_t jev_012[] = {0, 1, 2};
static const int32_t jev_0123[] = {0, 1, 2, 3};
static const int32_t jev_targets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

static const PbField jf_FileDescriptorProto[] = {
	JS(1, STRING, PL_OPT), JS(2, STRING, PL_OPT), JS(3, STRING, PL_REP),
	JM(4, PL_REP, jd_DescriptorProto), JM(5, PL_REP, jd_EnumDescriptorProto),
	JM(6, PL_REP, jd_ServiceDescriptorProto), JM(7, PL_REP, jd_FieldDescriptorProto),
	JM(8, PL_OPT, jd_FileOptions), JM(9, PL_OPT, jd_SourceCodeInfo), JS(10, INT32, PL_REP),
	JS(11, INT32, PL_REP), JS(12, STRING, PL_OPT), JE(14, PL_OPT, jev_edition),
};
JDEF(jd_FileDescriptorProto, jf_FileDescriptorProto);

static const PbField jf_DescriptorProto[] = {
	JS(1, STRING, PL_OPT), JM(2, PL_REP, jd_FieldDescriptorProto), JM(3, PL_REP, jd_DescriptorProto),
	JM(4, PL_REP, jd_EnumDescriptorProto), JM(5, PL_REP, jd_ExtensionRange),
	JM(6, PL_REP, jd_FieldDescriptorProto), JM(7, PL_OPT, jd_MessageOptions),
	JM(8, PL_REP, jd_OneofDescriptorProto), JM(9, PL_REP, jd_ReservedRange), JS(10, STRING, PL_REP),
};
JDEF(jd_DescriptorProto, jf_DescriptorProto);

static const PbField jf_ExtensionRange[] = {JS(1, INT32, PL_OPT), JS(2, INT32, PL_OPT), JM(3, PL_OPT, jd_ExtensionRangeOptions)};
JDEF(jd_ExtensionRange, jf_ExtensionRange);
static const PbField jf_ReservedRange[] = {JS(1, INT32, PL_OPT), JS(2, INT32, PL_OPT)};
JDEF(jd_ReservedRange, jf_ReservedRange);

static const PbField jf_ExtensionRangeOptions[] = {
	JM(2, PL_REP, jd_Declaration), JE(3, PL_OPT, jev_01), JM(50, PL_OPT, jd_FeatureSet),
	JM(999, PL_REP, jd_UninterpretedOption),
};
JDEF(jd_ExtensionRangeOptions, jf_ExtensionRangeOptions);
static const PbField jf_Declaration[] = {
	JS(1, INT32, PL_OPT), JS(2, STRING, PL_OPT), JS(3, STRING, PL_OPT), JS(5, BOOL, PL_OPT), JS(6, BOOL, PL_OPT),
};
JDEF(jd_Declaration, jf_Declaration);

static const PbField jf_FieldDescriptorProto[] = {
	JS(1, STRING, PL_OPT), JS(2, STRING, PL_OPT), JS(3, INT32, PL_OPT), JE(4, PL_OPT, jev_label),
	JE(5, PL_OPT, jev_type), JS(6, STRING, PL_OPT), JS(7, STRING, PL_OPT), JM(8, PL_OPT, jd_FieldOptions),
	JS(9, INT32, PL_OPT), JS(10, STRING, PL_OPT), JS(17, BOOL, PL_OPT),
};
JDEF(jd_FieldDescriptorProto, jf_FieldDescriptorProto);

static const PbField jf_OneofDescriptorProto[] = {JS(1, STRING, PL_OPT), JM(2, PL_OPT, jd_OneofOptions)};
JDEF(jd_OneofDescriptorProto, jf_OneofDescriptorProto);

static const PbField jf_EnumDescriptorProto[] = {
	JS(1, STRING, PL_OPT), JM(2, PL_REP, jd_EnumValueDescriptorProto), JM(3, PL_OPT, jd_EnumOptions),
	JM(4, PL_REP, jd_ReservedRange), JS(5, STRING, PL_REP),
};
JDEF(jd_EnumDescriptorProto, jf_EnumDescriptorProto);

static const PbField jf_EnumValueDescriptorProto[] = {JS(1, STRING, PL_OPT), JS(2, INT32, PL_OPT), JM(3, PL_OPT, jd_EnumValueOptions)};
JDEF(jd_EnumValueDescriptorProto, jf_EnumValueDescriptorProto);

static const PbField jf_ServiceDescriptorProto[] = {
	JS(1, STRING, PL_OPT), JM(2, PL_REP, jd_MethodDescriptorProto), JM(3, PL_OPT, jd_ServiceOptions),
};
JDEF(jd_ServiceDescriptorProto, jf_ServiceDescriptorProto);

static const PbField jf_MethodDescriptorProto[] = {
	JS(1, STRING, PL_OPT), JS(2, STRING, PL_OPT), JS(3, STRING, PL_OPT), JM(4, PL_OPT, jd_MethodOptions),
	JS(5, BOOL, PL_OPT), JS(6, BOOL, PL_OPT),
};
JDEF(jd_MethodDescriptorProto, jf_MethodDescriptorProto);

static const PbField jf_FileOptions[] = {
	JS(1, STRING, PL_OPT), JS(8, STRING, PL_OPT), JE(9, PL_OPT, jev_optimize), JS(10, BOOL, PL_OPT),
	JS(11, STRING, PL_OPT), JS(16, BOOL, PL_OPT), JS(17, BOOL, PL_OPT), JS(18, BOOL, PL_OPT),
	JS(20, BOOL, PL_OPT), JS(23, BOOL, PL_OPT), JS(27, BOOL, PL_OPT), JS(31, BOOL, PL_OPT),
	JS(36, STRING, PL_OPT), JS(37, STRING, PL_OPT), JS(39, STRING, PL_OPT), JS(40, STRING, PL_OPT),
	JS(41, STRING, PL_OPT), JS(42, BOOL, PL_OPT), JS(44, STRING, PL_OPT), JS(45, STRING, PL_OPT),
	JM(50, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption),
	JM(1020, PL_OPT, sp_ScalaPbOptions),
};
JDEF(jd_FileOptions, jf_FileOptions);

static const PbField jf_MessageOptions[] = {
	JS(1, BOOL, PL_OPT), JS(2, BOOL, PL_OPT), JS(3, BOOL, PL_OPT), JS(7, BOOL, PL_OPT), JS(11, BOOL, PL_OPT),
	JM(12, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption), JM(1020, PL_OPT, sp_MessageOptions),
};
JDEF(jd_MessageOptions, jf_MessageOptions);

static const PbField jf_FieldOptions[] = {
	JE(1, PL_OPT, jev_012), JS(2, BOOL, PL_OPT), JS(3, BOOL, PL_OPT), JS(5, BOOL, PL_OPT),
	JE(6, PL_OPT, jev_012), JS(10, BOOL, PL_OPT), JS(15, BOOL, PL_OPT), JS(16, BOOL, PL_OPT),
	JE(17, PL_OPT, jev_012), JE(19, PL_REP, jev_targets), JM(20, PL_REP, jd_EditionDefault),
	JM(21, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption), JM(1020, PL_OPT, sp_FieldOptions),
};
JDEF(jd_FieldOptions, jf_FieldOptions);
static const PbField jf_EditionDefault[] = {JS(2, STRING, PL_OPT), JE(3, PL_OPT, jev_edition)};
JDEF(jd_EditionDefault, jf_EditionDefault);

static const PbField jf_OneofOptions[] = {
	JM(1, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption), JM(1020, PL_OPT, sp_OneofOptions),
};
JDEF(jd_OneofOptions, jf_OneofOptions);
static const PbField jf_EnumOptions[] = {
	JS(2, BOOL, PL_OPT), JS(3, BOOL, PL_OPT), JS(6, BOOL, PL_OPT), JM(7, PL_OPT, jd_FeatureSet),
	JM(999, PL_REP, jd_UninterpretedOption), JM(1020, PL_OPT, sp_EnumOptions),
};
JDEF(jd_EnumOptions, jf_EnumOptions);
static const PbField jf_EnumValueOptions[] = {
	JS(1, BOOL, PL_OPT), JM(2, PL_OPT, jd_FeatureSet), JS(3, BOOL, PL_OPT),
	JM(999, PL_REP, jd_UninterpretedOption), JM(1020, PL_OPT, sp_EnumValueOptions),
};
JDEF(jd_EnumValueOptions, jf_EnumValueOptions);
static const PbField jf_ServiceOptions[] = {JS(33, BOOL, PL_OPT), JM(34, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption)};
JDEF(jd_ServiceOptions, jf_ServiceOptions);
static const PbField jf_MethodOptions[] = {
	JS(33, BOOL, PL_OPT), JE(34, PL_OPT, jev_012), JM(35, PL_OPT, jd_FeatureSet), JM(999, PL_REP, jd_UninterpretedOption),
};
JDEF(jd_MethodOptions, jf_MethodOptions);

static const PbField jf_UninterpretedOption[] = {
	JM(2, PL_REP, jd_NamePart), JS(3, STRING, PL_OPT), JS(4, UINT64, PL_OPT), JS(5, INT64, PL_OPT),
	JS(6, DOUBLE, PL_OPT), JS(7, BYTES, PL_OPT), JS(8, STRING, PL_OPT),
};
JDEF(jd_UninterpretedOption, jf_UninterpretedOption);
static const PbField jf_NamePart[] = {JS(1, STRING, PL_OPT), JS(2, BOOL, PL_OPT)};
JDEF(jd_NamePart, jf_NamePart);

static const PbField jf_FeatureSet[] = {
	JE(1, PL_OPT, jev_0123), JE(2, PL_OPT, jev_012), JE(3, PL_OPT, jev_012), JE(4, PL_OPT, jev_012),
	JE(5, PL_OPT, jev_012), JE(6, PL_OPT, jev_012),
};
JDEF(jd_FeatureSet, jf_FeatureSet);

static const PbField jf_SourceCodeInfo[] = {JM(1, PL_REP, jd_Location)};
JDEF(jd_SourceCodeInfo, jf_SourceCodeInfo);
static const PbField jf_Location[] = {
	JS(1, INT32, PL_PACKED), JS(2, INT32, PL_PACKED), JS(3, STRING, PL_OPT), JS(4, STRING, PL_OPT),
	JS(6, STRING, PL_REP),
};
JDEF(jd_Location, jf_Location);

static const PbField jf_Version[] = {JS(1, INT32, PL_OPT), JS(2, INT32, PL_OPT), JS(3, INT32, PL_OPT), JS(4, STRING, PL_OPT)};
JDEF(jd_Version, jf_Version);
static const PbField jf_CodeGeneratorRequest[] = {
	JS(1, STRING, PL_REP), JS(2, STRING, PL_OPT), JM(3, PL_OPT, jd_Version),
	JM(15, PL_REP, jd_FileDescriptorProto), JM(17, PL_REP, jd_FileDescriptorProto),
};
JDEF(jd_CodeGeneratorRequest, jf_CodeGeneratorRequest);

/* ---- scalapb.proto (protobuf/scalapb/scalapb.proto at v0.11.21) ---- */

enum {
	SPO_PACKAGE_NAME = 1, SPO_FLAT_PACKAGE = 2, SPO_IMPORT = 3, SPO_PREAMBLE = 4, SPO_SINGLE_FILE = 5,
	SPO_PRIMITIVE_WRAPPERS = 6, SPO_NO_PRIMITIVE_WRAPPERS = 7, SPO_COLLECTION_TYPE = 8,
	SPO_PRESERVE_UNKNOWN_FIELDS = 9, SPO_OBJECT_NAME = 10, SPO_SCOPE = 11, SPO_LENSES = 12,
	SPO_RETAIN_SOURCE_CODE_INFO = 13, SPO_MAP_TYPE = 14, SPO_NO_DEFAULT_VALUES_IN_CONSTRUCTOR = 15,
	SPO_ENUM_VALUE_NAMING = 16, SPO_ENUM_STRIP_PREFIX = 17, SPO_AUX_MESSAGE_OPTIONS = 18,
	SPO_AUX_FIELD_OPTIONS = 19, SPO_AUX_ENUM_OPTIONS = 20, SPO_BYTES_TYPE = 21,
	SPO_AUX_ENUM_VALUE_OPTIONS = 22, SPO_JAVA_CONVERSIONS = 23, SPO_PREPROCESSORS = 24,
	SPO_FIELD_TRANSFORMATIONS = 25, SPO_IGNORE_ALL_TRANSFORMATIONS = 26, SPO_GETTERS = 27,
	SPO_SCALA3_SOURCES = 28, SPO_PUBLIC_CONSTRUCTOR_PARAMETERS = 29, SPO_TEST_ONLY_NO_JAVA_CONVERSIONS = 999,
	AUX_TARGET = 1, AUX_OPTIONS = 2,
	SMO_EXTENDS = 1, SMO_COMPANION_EXTENDS = 2, SMO_ANNOTATIONS = 3, SMO_TYPE = 4,
	SMO_COMPANION_ANNOTATIONS = 5, SMO_SEALED_ONEOF_EXTENDS = 6, SMO_NO_BOX = 7,
	SMO_UNKNOWN_FIELDS_ANNOTATIONS = 8, SMO_NO_DEFAULT_VALUES_IN_CONSTRUCTOR = 9,
	SMO_SEALED_ONEOF_COMPANION_EXTENDS = 10, SMO_DERIVES = 11, SMO_SEALED_ONEOF_DERIVES = 12,
	SMO_SEALED_ONEOF_EMPTY_EXTENDS = 13,
	SCO_TYPE = 1, SCO_NON_EMPTY = 2, SCO_ADAPTER = 3,
	SFO_TYPE = 1, SFO_SCALA_NAME = 2, SFO_COLLECTION_TYPE = 3, SFO_KEY_TYPE = 4, SFO_VALUE_TYPE = 5,
	SFO_ANNOTATIONS = 6, SFO_MAP_TYPE = 7, SFO_COLLECTION = 8, SFO_NO_DEFAULT_VALUE_IN_CONSTRUCTOR = 9,
	SFO_NO_BOX = 30, SFO_REQUIRED = 31,
	SEO_EXTENDS = 1, SEO_COMPANION_EXTENDS = 2, SEO_TYPE = 3, SEO_BASE_ANNOTATIONS = 4,
	SEO_RECOGNIZED_ANNOTATIONS = 5, SEO_UNRECOGNIZED_ANNOTATIONS = 6,
	SEVO_EXTENDS = 1, SEVO_SCALA_NAME = 2, SEVO_ANNOTATIONS = 3,
	SOO_EXTENDS = 1, SOO_SCALA_NAME = 2,
	SCALAPB_EXT = 1020,
};

static const PbField spf_ScalaPbOptions[] = {
	JS(1, STRING, PL_OPT), JS(2, BOOL, PL_OPT), JS(3, STRING, PL_REP), JS(4, STRING, PL_REP),
	JS(5, BOOL, PL_OPT), JS(6, BOOL, PL_OPT), JS(7, BOOL, PL_OPT), JS(8, STRING, PL_OPT),
	JS(9, BOOL, PL_OPT), JS(10, STRING, PL_OPT), JE(11, PL_OPT, jev_01), JS(12, BOOL, PL_OPT),
	JS(13, BOOL, PL_OPT), JS(14, STRING, PL_OPT), JS(15, BOOL, PL_OPT), JE(16, PL_OPT, jev_01),
	JS(17, BOOL, PL_OPT), JM(18, PL_REP, sp_AuxMessageOptions), JM(19, PL_REP, sp_AuxFieldOptions),
	JM(20, PL_REP, sp_AuxEnumOptions), JS(21, STRING, PL_OPT), JM(22, PL_REP, sp_AuxEnumValueOptions),
	JS(23, BOOL, PL_OPT), JS(24, STRING, PL_REP), JM(25, PL_REP, sp_FieldTransformation),
	JS(26, BOOL, PL_OPT), JS(27, BOOL, PL_OPT), JS(28, BOOL, PL_OPT), JS(29, BOOL, PL_OPT),
	JS(999, BOOL, PL_OPT),
};
JDEF(sp_ScalaPbOptions, spf_ScalaPbOptions);
static const PbField spf_AuxMessageOptions[] = {JS(1, STRING, PL_OPT), JM(2, PL_OPT, sp_MessageOptions)};
JDEF(sp_AuxMessageOptions, spf_AuxMessageOptions);
static const PbField spf_AuxFieldOptions[] = {JS(1, STRING, PL_OPT), JM(2, PL_OPT, sp_FieldOptions)};
JDEF(sp_AuxFieldOptions, spf_AuxFieldOptions);
static const PbField spf_AuxEnumOptions[] = {JS(1, STRING, PL_OPT), JM(2, PL_OPT, sp_EnumOptions)};
JDEF(sp_AuxEnumOptions, spf_AuxEnumOptions);
static const PbField spf_AuxEnumValueOptions[] = {JS(1, STRING, PL_OPT), JM(2, PL_OPT, sp_EnumValueOptions)};
JDEF(sp_AuxEnumValueOptions, spf_AuxEnumValueOptions);

static const PbField spf_MessageOptions[] = {
	JS(1, STRING, PL_REP), JS(2, STRING, PL_REP), JS(3, STRING, PL_REP), JS(4, STRING, PL_OPT),
	JS(5, STRING, PL_REP), JS(6, STRING, PL_REP), JS(7, BOOL, PL_OPT), JS(8, STRING, PL_REP),
	JS(9, BOOL, PL_OPT), JS(10, STRING, PL_REP), JS(11, STRING, PL_REP), JS(12, STRING, PL_REP),
	JS(13, STRING, PL_REP),
};
JDEF(sp_MessageOptions, spf_MessageOptions);
static const PbField spf_Collection[] = {JS(1, STRING, PL_OPT), JS(2, BOOL, PL_OPT), JS(3, STRING, PL_OPT)};
JDEF(sp_Collection, spf_Collection);
static const PbField spf_FieldOptions[] = {
	JS(1, STRING, PL_OPT), JS(2, STRING, PL_OPT), JS(3, STRING, PL_OPT), JS(4, STRING, PL_OPT),
	JS(5, STRING, PL_OPT), JS(6, STRING, PL_REP), JS(7, STRING, PL_OPT), JM(8, PL_OPT, sp_Collection),
	JS(9, BOOL, PL_OPT), JS(30, BOOL, PL_OPT), JS(31, BOOL, PL_OPT),
};
JDEF(sp_FieldOptions, spf_FieldOptions);
static const PbField spf_EnumOptions[] = {
	JS(1, STRING, PL_REP), JS(2, STRING, PL_REP), JS(3, STRING, PL_OPT), JS(4, STRING, PL_REP),
	JS(5, STRING, PL_REP), JS(6, STRING, PL_REP),
};
JDEF(sp_EnumOptions, spf_EnumOptions);
static const PbField spf_EnumValueOptions[] = {JS(1, STRING, PL_REP), JS(2, STRING, PL_OPT), JS(3, STRING, PL_REP)};
JDEF(sp_EnumValueOptions, spf_EnumValueOptions);
static const PbField spf_OneofOptions[] = {JS(1, STRING, PL_REP), JS(2, STRING, PL_OPT)};
JDEF(sp_OneofOptions, spf_OneofOptions);
static const PbField spf_FieldTransformation[] = {
	JM(1, PL_OPT, jd_FieldDescriptorProto), JE(2, PL_OPT, jev_012), JM(3, PL_OPT, jd_FieldOptions),
};
JDEF(sp_FieldTransformation, spf_FieldTransformation);
