/* Schemas of the messages protoc_plugin 25.1.0 parses, as its generated code declares them
 * (protoc_plugin/lib/src/gen: google/protobuf/descriptor.pb.dart, compiler/plugin.pb.dart), plus
 * the extensions it registers (Dart_options and google.api Client, registerAllExtensions in
 * code_generator.dart) as fields of the extended options. Closed enums list the values the Dart
 * enums know: other values become unknown fields. */
#include "dart_protobuf.h"
#include "schema/schema.h"

extern const PbMsgDef dpb_DescriptorProto, dpb_ExtensionRange, dpb_ReservedRange, dpb_ExtensionRangeOptions,
	dpb_Declaration, dpb_FieldDescriptorProto, dpb_OneofDescriptorProto, dpb_EnumReservedRange,
	dpb_EnumValueDescriptorProto, dpb_MethodDescriptorProto, dpb_FileOptions, dpb_MessageOptions,
	dpb_FieldOptions, dpb_EditionDefault, dpb_FeatureSupport, dpb_OneofOptions, dpb_EnumOptions,
	dpb_EnumValueOptions, dpb_ServiceOptions, dpb_MethodOptions, dpb_UninterpretedOption, dpb_NamePart,
	dpb_SourceCodeInfo, dpb_Location, dpb_Version, dpb_Imports, dpb_DartMixin;

static const int32_t ev_edition[] = {0, 900, 998, 999, 1000, 1001, 1, 2, 99997, 99998, 99999, 2147483647};
static const int32_t ev_012[] = {0, 1, 2};
static const int32_t ev_01[] = {0, 1};
static const int32_t ev_123[] = {1, 2, 3};
static const int32_t ev_type[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
static const int32_t ev_0123[] = {0, 1, 2, 3};
static const int32_t ev_0_23[] = {0, 2, 3};
static const int32_t ev_01234[] = {0, 1, 2, 3, 4};
static const int32_t ev_targets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

static const PbField f_Version[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT), S(3, INT32, PL_OPT), S(4, STRING, PL_OPT)};
DEF(dpb_Version, f_Version);

static const PbField f_CodeGeneratorRequest[] = {
	S(1, STRING, PL_REP), S(2, STRING, PL_OPT), M(3, PL_OPT, dpb_Version),
	M(15, PL_REP, dpb_FileDescriptorProto), M(17, PL_REP, dpb_FileDescriptorProto),
};
DEF(dpb_CodeGeneratorRequest, f_CodeGeneratorRequest);

static const PbField f_FileDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_REP),
	M(4, PL_REP, dpb_DescriptorProto), M(5, PL_REP, dpb_EnumDescriptorProto),
	M(6, PL_REP, dpb_ServiceDescriptorProto), M(7, PL_REP, dpb_FieldDescriptorProto),
	M(8, PL_OPT, dpb_FileOptions), M(9, PL_OPT, dpb_SourceCodeInfo), S(10, INT32, PL_REP),
	S(11, INT32, PL_REP), S(12, STRING, PL_OPT), E(14, PL_OPT, ev_edition), S(15, STRING, PL_REP),
};
DEF(dpb_FileDescriptorProto, f_FileDescriptorProto);

static const PbField f_DescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, dpb_FieldDescriptorProto), M(3, PL_REP, dpb_DescriptorProto),
	M(4, PL_REP, dpb_EnumDescriptorProto), M(5, PL_REP, dpb_ExtensionRange),
	M(6, PL_REP, dpb_FieldDescriptorProto), M(7, PL_OPT, dpb_MessageOptions),
	M(8, PL_REP, dpb_OneofDescriptorProto), M(9, PL_REP, dpb_ReservedRange), S(10, STRING, PL_REP),
	E(11, PL_OPT, ev_012),
};
DEF(dpb_DescriptorProto, f_DescriptorProto);

static const PbField f_ExtensionRange[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT), M(3, PL_OPT, dpb_ExtensionRangeOptions)};
DEF(dpb_ExtensionRange, f_ExtensionRange);
static const PbField f_ReservedRange[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT)};
DEF(dpb_ReservedRange, f_ReservedRange);
DEF(dpb_EnumReservedRange, f_ReservedRange);

static const PbField f_ExtensionRangeOptions[] = {
	M(2, PL_REP, dpb_Declaration), E(3, PL_OPT, ev_01), M(50, PL_OPT, dpb_FeatureSet),
	M(999, PL_REP, dpb_UninterpretedOption),
};
DEF(dpb_ExtensionRangeOptions, f_ExtensionRangeOptions);
static const PbField f_Declaration[] = {
	S(1, INT32, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_OPT), S(5, BOOL, PL_OPT), S(6, BOOL, PL_OPT),
};
DEF(dpb_Declaration, f_Declaration);

static const PbField f_FieldDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, INT32, PL_OPT), E(4, PL_OPT, ev_123),
	E(5, PL_OPT, ev_type), S(6, STRING, PL_OPT), S(7, STRING, PL_OPT), M(8, PL_OPT, dpb_FieldOptions),
	S(9, INT32, PL_OPT), S(10, STRING, PL_OPT), S(17, BOOL, PL_OPT),
};
DEF(dpb_FieldDescriptorProto, f_FieldDescriptorProto);

static const PbField f_OneofDescriptorProto[] = {S(1, STRING, PL_OPT), M(2, PL_OPT, dpb_OneofOptions)};
DEF(dpb_OneofDescriptorProto, f_OneofDescriptorProto);

static const PbField f_EnumDescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, dpb_EnumValueDescriptorProto), M(3, PL_OPT, dpb_EnumOptions),
	M(4, PL_REP, dpb_EnumReservedRange), S(5, STRING, PL_REP), E(6, PL_OPT, ev_012),
};
DEF(dpb_EnumDescriptorProto, f_EnumDescriptorProto);

static const PbField f_EnumValueDescriptorProto[] = {S(1, STRING, PL_OPT), S(2, INT32, PL_OPT), M(3, PL_OPT, dpb_EnumValueOptions)};
DEF(dpb_EnumValueDescriptorProto, f_EnumValueDescriptorProto);

static const PbField f_ServiceDescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, dpb_MethodDescriptorProto), M(3, PL_OPT, dpb_ServiceOptions),
};
DEF(dpb_ServiceDescriptorProto, f_ServiceDescriptorProto);

static const PbField f_MethodDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_OPT), M(4, PL_OPT, dpb_MethodOptions),
	S(5, BOOL, PL_OPT), S(6, BOOL, PL_OPT),
};
DEF(dpb_MethodDescriptorProto, f_MethodDescriptorProto);

static const PbField f_FileOptions[] = {
	S(1, STRING, PL_OPT), S(8, STRING, PL_OPT), E(9, PL_OPT, ev_123), S(10, BOOL, PL_OPT),
	S(11, STRING, PL_OPT), S(16, BOOL, PL_OPT), S(17, BOOL, PL_OPT), S(18, BOOL, PL_OPT),
	S(20, BOOL, PL_OPT), S(23, BOOL, PL_OPT), S(27, BOOL, PL_OPT), S(31, BOOL, PL_OPT),
	S(36, STRING, PL_OPT), S(37, STRING, PL_OPT), S(39, STRING, PL_OPT), S(40, STRING, PL_OPT),
	S(41, STRING, PL_OPT), S(44, STRING, PL_OPT), S(45, STRING, PL_OPT), M(50, PL_OPT, dpb_FeatureSet),
	M(999, PL_REP, dpb_UninterpretedOption),
	M(DPB_EXT_IMPORTS, PL_OPT, dpb_Imports), S(DPB_EXT_DEFAULT_MIXIN, STRING, PL_OPT),
};
DEF(dpb_FileOptions, f_FileOptions);

static const PbField f_MessageOptions[] = {
	S(1, BOOL, PL_OPT), S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(7, BOOL, PL_OPT), S(11, BOOL, PL_OPT),
	M(12, PL_OPT, dpb_FeatureSet), M(999, PL_REP, dpb_UninterpretedOption), S(DPB_EXT_MIXIN, STRING, PL_OPT),
};
DEF(dpb_MessageOptions, f_MessageOptions);

static const PbField f_FieldOptions[] = {
	E(1, PL_OPT, ev_012), S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(5, BOOL, PL_OPT),
	E(6, PL_OPT, ev_012), S(10, BOOL, PL_OPT), S(15, BOOL, PL_OPT), S(16, BOOL, PL_OPT),
	E(17, PL_OPT, ev_012), E(19, PL_REP, ev_targets), M(20, PL_REP, dpb_EditionDefault),
	M(21, PL_OPT, dpb_FeatureSet), M(22, PL_OPT, dpb_FeatureSupport), M(999, PL_REP, dpb_UninterpretedOption),
	S(DPB_EXT_OVERRIDE_GETTER, BOOL, PL_OPT), S(DPB_EXT_DART_NAME, STRING, PL_OPT),
	S(DPB_EXT_OVERRIDE_CLEAR_METHOD, BOOL, PL_OPT), S(DPB_EXT_OVERRIDE_SETTER, BOOL, PL_OPT),
	S(DPB_EXT_OVERRIDE_HAS_METHOD, BOOL, PL_OPT),
};
DEF(dpb_FieldOptions, f_FieldOptions);
static const PbField f_EditionDefault[] = {S(2, STRING, PL_OPT), E(3, PL_OPT, ev_edition)};
DEF(dpb_EditionDefault, f_EditionDefault);
static const PbField f_FeatureSupport[] = {
	E(1, PL_OPT, ev_edition), E(2, PL_OPT, ev_edition), S(3, STRING, PL_OPT), E(4, PL_OPT, ev_edition),
};
DEF(dpb_FeatureSupport, f_FeatureSupport);

static const PbField f_OneofOptions[] = {M(1, PL_OPT, dpb_FeatureSet), M(999, PL_REP, dpb_UninterpretedOption)};
DEF(dpb_OneofOptions, f_OneofOptions);
static const PbField f_EnumOptions[] = {
	S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(6, BOOL, PL_OPT), M(7, PL_OPT, dpb_FeatureSet),
	M(999, PL_REP, dpb_UninterpretedOption),
};
DEF(dpb_EnumOptions, f_EnumOptions);
static const PbField f_EnumValueOptions[] = {
	S(1, BOOL, PL_OPT), M(2, PL_OPT, dpb_FeatureSet), S(3, BOOL, PL_OPT), M(4, PL_OPT, dpb_FeatureSupport),
	M(999, PL_REP, dpb_UninterpretedOption),
};
DEF(dpb_EnumValueOptions, f_EnumValueOptions);
static const PbField f_ServiceOptions[] = {
	S(33, BOOL, PL_OPT), M(34, PL_OPT, dpb_FeatureSet), M(999, PL_REP, dpb_UninterpretedOption),
	S(DPB_EXT_DEFAULT_HOST, STRING, PL_OPT), S(DPB_EXT_OAUTH_SCOPES, STRING, PL_OPT),
	S(525000001, STRING, PL_OPT), /* api_version */
};
DEF(dpb_ServiceOptions, f_ServiceOptions);
static const PbField f_MethodOptions[] = {
	S(33, BOOL, PL_OPT), E(34, PL_OPT, ev_012), M(35, PL_OPT, dpb_FeatureSet), M(999, PL_REP, dpb_UninterpretedOption),
	S(1051, STRING, PL_REP), /* method_signature */
};
DEF(dpb_MethodOptions, f_MethodOptions);

static const PbField f_UninterpretedOption[] = {
	M(2, PL_REP, dpb_NamePart), S(3, STRING, PL_OPT), S(4, UINT64, PL_OPT), S(5, INT64, PL_OPT),
	S(6, DOUBLE, PL_OPT), S(7, BYTES, PL_OPT), S(8, STRING, PL_OPT),
};
DEF(dpb_UninterpretedOption, f_UninterpretedOption);
static const PbField f_NamePart[] = {S(1, STRING, PL_OPT), S(2, BOOL, PL_OPT)};
DEF(dpb_NamePart, f_NamePart);

static const PbField f_FeatureSet[] = {
	E(1, PL_OPT, ev_0123), E(2, PL_OPT, ev_012), E(3, PL_OPT, ev_012), E(4, PL_OPT, ev_0_23),
	E(5, PL_OPT, ev_012), E(6, PL_OPT, ev_012), E(7, PL_OPT, ev_012), E(8, PL_OPT, ev_01234),
};
DEF(dpb_FeatureSet, f_FeatureSet);

static const PbField f_SourceCodeInfo[] = {M(1, PL_REP, dpb_Location)};
DEF(dpb_SourceCodeInfo, f_SourceCodeInfo);
static const PbField f_Location[] = {
	S(1, INT32, PL_PACKED), S(2, INT32, PL_PACKED), S(3, STRING, PL_OPT), S(4, STRING, PL_OPT),
	S(6, STRING, PL_REP),
};
DEF(dpb_Location, f_Location);

static const PbField f_Imports[] = {M(DPB_IMPORTS_MIXINS, PL_REP, dpb_DartMixin)};
DEF(dpb_Imports, f_Imports);
static const PbField f_DartMixin[] = {S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_OPT)};
DEF(dpb_DartMixin, f_DartMixin);
