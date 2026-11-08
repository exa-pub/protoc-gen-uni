/* Schema of google/protobuf/descriptor.proto as shipped by @bufbuild/protobuf v2.17.0
 * (packages/protobuf/src/wkt/gen/google/protobuf/descriptor_pb.ts): FeatureSet field 9 is a known
 * field there, so it is one here. Closed enums list their known values. */
#include "schema.h"

extern const PbMsgDef pbd_DescriptorProto, pbd_ExtensionRange, pbd_ReservedRange,
	pbd_ExtensionRangeOptions, pbd_Declaration, pbd_FieldDescriptorProto, pbd_OneofDescriptorProto,
	pbd_EnumDescriptorProto, pbd_EnumReservedRange, pbd_EnumValueDescriptorProto,
	pbd_ServiceDescriptorProto, pbd_MethodDescriptorProto, pbd_FileOptions, pbd_MessageOptions,
	pbd_FieldOptions, pbd_EditionDefault, pbd_FeatureSupport, pbd_OneofOptions, pbd_EnumOptions,
	pbd_EnumValueOptions, pbd_ServiceOptions, pbd_MethodOptions, pbd_UninterpretedOption,
	pbd_NamePart, pbd_FeatureSet, pbd_SourceCodeInfo, pbd_Location;

/* Closed enums of descriptor.proto: unknown values become unknown fields. */
static const int32_t ev_edition[] = {0, 900, 998, 999, 1000, 1001, 1002, 9999, 1, 2, 99997, 99998, 99999, 2147483647};
static const int32_t ev_visibility[] = {0, 1, 2};
static const int32_t ev_label[] = {1, 2, 3};
static const int32_t ev_type[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
static const int32_t ev_verification[] = {0, 1};
static const int32_t ev_optimize[] = {1, 2, 3};
static const int32_t ev_012[] = {0, 1, 2};
static const int32_t ev_0123[] = {0, 1, 2, 3};
static const int32_t ev_0_23[] = {0, 2, 3};
static const int32_t ev_01234[] = {0, 1, 2, 3, 4};
static const int32_t ev_targets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

static const PbField f_FileDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_REP),
	M(4, PL_REP, pbd_DescriptorProto), M(5, PL_REP, pbd_EnumDescriptorProto),
	M(6, PL_REP, pbd_ServiceDescriptorProto), M(7, PL_REP, pbd_FieldDescriptorProto),
	M(8, PL_OPT, pbd_FileOptions), M(9, PL_OPT, pbd_SourceCodeInfo), S(10, INT32, PL_REP),
	S(11, INT32, PL_REP), S(12, STRING, PL_OPT), E(14, PL_OPT, ev_edition), S(15, STRING, PL_REP),
};
DEF(pbd_FileDescriptorProto, f_FileDescriptorProto);

static const PbField f_DescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, pbd_FieldDescriptorProto), M(3, PL_REP, pbd_DescriptorProto),
	M(4, PL_REP, pbd_EnumDescriptorProto), M(5, PL_REP, pbd_ExtensionRange),
	M(6, PL_REP, pbd_FieldDescriptorProto), M(7, PL_OPT, pbd_MessageOptions),
	M(8, PL_REP, pbd_OneofDescriptorProto), M(9, PL_REP, pbd_ReservedRange), S(10, STRING, PL_REP),
	E(11, PL_OPT, ev_visibility),
};
DEF(pbd_DescriptorProto, f_DescriptorProto);

static const PbField f_ExtensionRange[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT), M(3, PL_OPT, pbd_ExtensionRangeOptions)};
DEF(pbd_ExtensionRange, f_ExtensionRange);
static const PbField f_ReservedRange[] = {S(1, INT32, PL_OPT), S(2, INT32, PL_OPT)};
DEF(pbd_ReservedRange, f_ReservedRange);
DEF(pbd_EnumReservedRange, f_ReservedRange);

static const PbField f_ExtensionRangeOptions[] = {
	M(2, PL_REP, pbd_Declaration), E(3, PL_OPT, ev_verification), M(50, PL_OPT, pbd_FeatureSet),
	M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_ExtensionRangeOptions, f_ExtensionRangeOptions);
static const PbField f_Declaration[] = {
	S(1, INT32, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_OPT), S(5, BOOL, PL_OPT), S(6, BOOL, PL_OPT),
};
DEF(pbd_Declaration, f_Declaration);

static const PbField f_FieldDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, INT32, PL_OPT), E(4, PL_OPT, ev_label),
	E(5, PL_OPT, ev_type), S(6, STRING, PL_OPT), S(7, STRING, PL_OPT), M(8, PL_OPT, pbd_FieldOptions),
	S(9, INT32, PL_OPT), S(10, STRING, PL_OPT), S(17, BOOL, PL_OPT),
};
DEF(pbd_FieldDescriptorProto, f_FieldDescriptorProto);

static const PbField f_OneofDescriptorProto[] = {S(1, STRING, PL_OPT), M(2, PL_OPT, pbd_OneofOptions)};
DEF(pbd_OneofDescriptorProto, f_OneofDescriptorProto);

static const PbField f_EnumDescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, pbd_EnumValueDescriptorProto), M(3, PL_OPT, pbd_EnumOptions),
	M(4, PL_REP, pbd_EnumReservedRange), S(5, STRING, PL_REP), E(6, PL_OPT, ev_visibility),
};
DEF(pbd_EnumDescriptorProto, f_EnumDescriptorProto);

static const PbField f_EnumValueDescriptorProto[] = {S(1, STRING, PL_OPT), S(2, INT32, PL_OPT), M(3, PL_OPT, pbd_EnumValueOptions)};
DEF(pbd_EnumValueDescriptorProto, f_EnumValueDescriptorProto);

static const PbField f_ServiceDescriptorProto[] = {
	S(1, STRING, PL_OPT), M(2, PL_REP, pbd_MethodDescriptorProto), M(3, PL_OPT, pbd_ServiceOptions),
};
DEF(pbd_ServiceDescriptorProto, f_ServiceDescriptorProto);

static const PbField f_MethodDescriptorProto[] = {
	S(1, STRING, PL_OPT), S(2, STRING, PL_OPT), S(3, STRING, PL_OPT), M(4, PL_OPT, pbd_MethodOptions),
	S(5, BOOL, PL_OPT), S(6, BOOL, PL_OPT),
};
DEF(pbd_MethodDescriptorProto, f_MethodDescriptorProto);

static const PbField f_FileOptions[] = {
	S(1, STRING, PL_OPT), S(8, STRING, PL_OPT), E(9, PL_OPT, ev_optimize), S(10, BOOL, PL_OPT),
	S(11, STRING, PL_OPT), S(16, BOOL, PL_OPT), S(17, BOOL, PL_OPT), S(18, BOOL, PL_OPT),
	S(20, BOOL, PL_OPT), S(23, BOOL, PL_OPT), S(27, BOOL, PL_OPT), S(31, BOOL, PL_OPT),
	S(36, STRING, PL_OPT), S(37, STRING, PL_OPT), S(39, STRING, PL_OPT), S(40, STRING, PL_OPT),
	S(41, STRING, PL_OPT), S(44, STRING, PL_OPT), S(45, STRING, PL_OPT), M(50, PL_OPT, pbd_FeatureSet),
	M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_FileOptions, f_FileOptions);

static const PbField f_MessageOptions[] = {
	S(1, BOOL, PL_OPT), S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(7, BOOL, PL_OPT), S(11, BOOL, PL_OPT),
	M(12, PL_OPT, pbd_FeatureSet), M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_MessageOptions, f_MessageOptions);

static const PbField f_FieldOptions[] = {
	E(1, PL_OPT, ev_012), S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(5, BOOL, PL_OPT),
	E(6, PL_OPT, ev_012), S(10, BOOL, PL_OPT), S(15, BOOL, PL_OPT), S(16, BOOL, PL_OPT),
	E(17, PL_OPT, ev_012), E(19, PL_REP, ev_targets), M(20, PL_REP, pbd_EditionDefault),
	M(21, PL_OPT, pbd_FeatureSet), M(22, PL_OPT, pbd_FeatureSupport), M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_FieldOptions, f_FieldOptions);
static const PbField f_EditionDefault[] = {S(2, STRING, PL_OPT), E(3, PL_OPT, ev_edition)};
DEF(pbd_EditionDefault, f_EditionDefault);
static const PbField f_FeatureSupport[] = {
	E(1, PL_OPT, ev_edition), E(2, PL_OPT, ev_edition), S(3, STRING, PL_OPT), E(4, PL_OPT, ev_edition),
	S(5, STRING, PL_OPT),
};
DEF(pbd_FeatureSupport, f_FeatureSupport);

static const PbField f_OneofOptions[] = {M(1, PL_OPT, pbd_FeatureSet), M(999, PL_REP, pbd_UninterpretedOption)};
DEF(pbd_OneofOptions, f_OneofOptions);
static const PbField f_EnumOptions[] = {
	S(2, BOOL, PL_OPT), S(3, BOOL, PL_OPT), S(6, BOOL, PL_OPT), M(7, PL_OPT, pbd_FeatureSet),
	M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_EnumOptions, f_EnumOptions);
static const PbField f_EnumValueOptions[] = {
	S(1, BOOL, PL_OPT), M(2, PL_OPT, pbd_FeatureSet), S(3, BOOL, PL_OPT), M(4, PL_OPT, pbd_FeatureSupport),
	M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_EnumValueOptions, f_EnumValueOptions);
static const PbField f_ServiceOptions[] = {S(33, BOOL, PL_OPT), M(34, PL_OPT, pbd_FeatureSet), M(999, PL_REP, pbd_UninterpretedOption)};
DEF(pbd_ServiceOptions, f_ServiceOptions);
static const PbField f_MethodOptions[] = {
	S(33, BOOL, PL_OPT), E(34, PL_OPT, ev_012), M(35, PL_OPT, pbd_FeatureSet), M(999, PL_REP, pbd_UninterpretedOption),
};
DEF(pbd_MethodOptions, f_MethodOptions);

static const PbField f_UninterpretedOption[] = {
	M(2, PL_REP, pbd_NamePart), S(3, STRING, PL_OPT), S(4, UINT64, PL_OPT), S(5, INT64, PL_OPT),
	S(6, DOUBLE, PL_OPT), S(7, BYTES, PL_OPT), S(8, STRING, PL_OPT),
};
DEF(pbd_UninterpretedOption, f_UninterpretedOption);
static const PbField f_NamePart[] = {S(1, STRING, PL_OPT), S(2, BOOL, PL_OPT)};
DEF(pbd_NamePart, f_NamePart);

static const PbField f_FeatureSet[] = {
	E(1, PL_OPT, ev_0123), E(2, PL_OPT, ev_012), E(3, PL_OPT, ev_012), E(4, PL_OPT, ev_0_23),
	E(5, PL_OPT, ev_012), E(6, PL_OPT, ev_012), E(7, PL_OPT, ev_0123), E(8, PL_OPT, ev_01234),
	E(9, PL_OPT, ev_012),
};
DEF(pbd_FeatureSet, f_FeatureSet);

static const PbField f_SourceCodeInfo[] = {M(1, PL_REP, pbd_Location)};
DEF(pbd_SourceCodeInfo, f_SourceCodeInfo);
static const PbField f_Location[] = {
	S(1, INT32, PL_PACKED), S(2, INT32, PL_PACKED), S(3, STRING, PL_OPT), S(4, STRING, PL_OPT),
	S(6, STRING, PL_REP),
};
DEF(pbd_Location, f_Location);
