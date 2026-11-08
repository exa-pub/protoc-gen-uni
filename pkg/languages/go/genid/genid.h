/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Constants for declarations in descriptor.proto, go_features.proto and the well-known types.
 *
 * Ported from google.golang.org/protobuf v1.36.12 internal/genid: only the constants used by
 * compiler/protogen and protoc-gen-go, with the same names (as GENID_<Go name>) and values. */
#ifndef GO_GENID_H
#define GO_GENID_H

/* Go names of implementation-specific struct fields in generated messages (goname.go, name.go). */
#define GENID_State_goname "state"
#define GENID_SizeCache_goname "sizeCache"
#define GENID_UnknownFields_goname "unknownFields"
#define GENID_ExtensionFields_goname "extensionFields"
#define GENID_BuilderSuffix_goname "_builder"

/* Field numbers of google/protobuf/descriptor.proto (descriptor_gen.go). */
enum {
	GENID_FileDescriptorProto_Name_field_number = 1,
	GENID_FileDescriptorProto_Package_field_number = 2,
	GENID_FileDescriptorProto_Dependency_field_number = 3,
	GENID_FileDescriptorProto_MessageType_field_number = 4,
	GENID_FileDescriptorProto_EnumType_field_number = 5,
	GENID_FileDescriptorProto_Service_field_number = 6,
	GENID_FileDescriptorProto_Extension_field_number = 7,
	GENID_FileDescriptorProto_Options_field_number = 8,
	GENID_FileDescriptorProto_SourceCodeInfo_field_number = 9,
	GENID_FileDescriptorProto_PublicDependency_field_number = 10,
	GENID_FileDescriptorProto_WeakDependency_field_number = 11,
	GENID_FileDescriptorProto_Syntax_field_number = 12,
	GENID_FileDescriptorProto_Edition_field_number = 14,
	GENID_FileDescriptorProto_OptionDependency_field_number = 15,

	GENID_DescriptorProto_Name_field_number = 1,
	GENID_DescriptorProto_Field_field_number = 2,
	GENID_DescriptorProto_NestedType_field_number = 3,
	GENID_DescriptorProto_EnumType_field_number = 4,
	GENID_DescriptorProto_ExtensionRange_field_number = 5,
	GENID_DescriptorProto_Extension_field_number = 6,
	GENID_DescriptorProto_Options_field_number = 7,
	GENID_DescriptorProto_OneofDecl_field_number = 8,
	GENID_DescriptorProto_ReservedRange_field_number = 9,
	GENID_DescriptorProto_ReservedName_field_number = 10,
	GENID_DescriptorProto_Visibility_field_number = 11,
	GENID_DescriptorProto_ExtensionRange_Start_field_number = 1,
	GENID_DescriptorProto_ExtensionRange_End_field_number = 2,
	GENID_DescriptorProto_ExtensionRange_Options_field_number = 3,
	GENID_DescriptorProto_ReservedRange_Start_field_number = 1,
	GENID_DescriptorProto_ReservedRange_End_field_number = 2,

	GENID_FieldDescriptorProto_Name_field_number = 1,
	GENID_FieldDescriptorProto_Extendee_field_number = 2,
	GENID_FieldDescriptorProto_Number_field_number = 3,
	GENID_FieldDescriptorProto_Label_field_number = 4,
	GENID_FieldDescriptorProto_Type_field_number = 5,
	GENID_FieldDescriptorProto_TypeName_field_number = 6,
	GENID_FieldDescriptorProto_DefaultValue_field_number = 7,
	GENID_FieldDescriptorProto_Options_field_number = 8,
	GENID_FieldDescriptorProto_OneofIndex_field_number = 9,
	GENID_FieldDescriptorProto_JsonName_field_number = 10,
	GENID_FieldDescriptorProto_Proto3Optional_field_number = 17,

	GENID_OneofDescriptorProto_Name_field_number = 1,
	GENID_OneofDescriptorProto_Options_field_number = 2,

	GENID_EnumDescriptorProto_Name_field_number = 1,
	GENID_EnumDescriptorProto_Value_field_number = 2,
	GENID_EnumDescriptorProto_Options_field_number = 3,
	GENID_EnumDescriptorProto_ReservedRange_field_number = 4,
	GENID_EnumDescriptorProto_ReservedName_field_number = 5,
	GENID_EnumDescriptorProto_Visibility_field_number = 6,
	GENID_EnumDescriptorProto_EnumReservedRange_Start_field_number = 1,
	GENID_EnumDescriptorProto_EnumReservedRange_End_field_number = 2,

	GENID_EnumValueDescriptorProto_Name_field_number = 1,
	GENID_EnumValueDescriptorProto_Number_field_number = 2,
	GENID_EnumValueDescriptorProto_Options_field_number = 3,

	GENID_ServiceDescriptorProto_Name_field_number = 1,
	GENID_ServiceDescriptorProto_Method_field_number = 2,
	GENID_ServiceDescriptorProto_Options_field_number = 3,

	GENID_MethodDescriptorProto_Name_field_number = 1,
	GENID_MethodDescriptorProto_InputType_field_number = 2,
	GENID_MethodDescriptorProto_OutputType_field_number = 3,
	GENID_MethodDescriptorProto_Options_field_number = 4,
	GENID_MethodDescriptorProto_ClientStreaming_field_number = 5,
	GENID_MethodDescriptorProto_ServerStreaming_field_number = 6,

	GENID_FileOptions_GoPackage_field_number = 11,
	GENID_FileOptions_Deprecated_field_number = 23,
	GENID_FileOptions_Features_field_number = 50,
	GENID_MessageOptions_MessageSetWireFormat_field_number = 1,
	GENID_MessageOptions_Deprecated_field_number = 3,
	GENID_MessageOptions_MapEntry_field_number = 7,
	GENID_MessageOptions_Features_field_number = 12,
	GENID_FieldOptions_Packed_field_number = 2,
	GENID_FieldOptions_Deprecated_field_number = 3,
	GENID_FieldOptions_Lazy_field_number = 5,
	GENID_FieldOptions_Retention_field_number = 17,
	GENID_FieldOptions_Features_field_number = 21,
	GENID_OneofOptions_Features_field_number = 1,
	GENID_EnumOptions_AllowAlias_field_number = 2,
	GENID_EnumOptions_Deprecated_field_number = 3,
	GENID_EnumOptions_Features_field_number = 7,
	GENID_EnumValueOptions_Deprecated_field_number = 1,
	GENID_EnumValueOptions_Features_field_number = 2,
	GENID_ServiceOptions_Deprecated_field_number = 33,
	GENID_ServiceOptions_Features_field_number = 34,
	GENID_MethodOptions_Deprecated_field_number = 33,
	GENID_MethodOptions_Features_field_number = 35,

	GENID_FeatureSet_FieldPresence_field_number = 1,
	GENID_FeatureSet_EnumType_field_number = 2,
	GENID_FeatureSet_RepeatedFieldEncoding_field_number = 3,
	GENID_FeatureSet_Utf8Validation_field_number = 4,
	GENID_FeatureSet_MessageEncoding_field_number = 5,
	GENID_FeatureSet_JsonFormat_field_number = 6,

	GENID_SourceCodeInfo_Location_field_number = 1,
	GENID_SourceCodeInfo_Location_Path_field_number = 1,
	GENID_SourceCodeInfo_Location_Span_field_number = 2,
	GENID_SourceCodeInfo_Location_LeadingComments_field_number = 3,
	GENID_SourceCodeInfo_Location_TrailingComments_field_number = 4,
	GENID_SourceCodeInfo_Location_LeadingDetachedComments_field_number = 6,

	GENID_MapEntry_Key_field_number = 1,
	GENID_MapEntry_Value_field_number = 2,
};

/* Enum values of descriptor.proto. */
enum {
	GENID_FieldOptions_RETENTION_SOURCE_enum_value = 2,
	GENID_FeatureSet_EXPLICIT_enum_value = 1,
	GENID_FeatureSet_IMPLICIT_enum_value = 2,
	GENID_FeatureSet_LEGACY_REQUIRED_enum_value = 3,
	GENID_FeatureSet_OPEN_enum_value = 1,
	GENID_FeatureSet_PACKED_enum_value = 1,
	GENID_FeatureSet_VERIFY_enum_value = 2,
	GENID_FeatureSet_DELIMITED_enum_value = 2,
	GENID_FeatureSet_ALLOW_enum_value = 1,
	GENID_GeneratedCodeInfo_Annotation_SET_enum_value = 1,
};

/* google.protobuf.Edition values. */
enum {
	GENID_Edition_EDITION_UNKNOWN = 0,
	GENID_Edition_EDITION_LEGACY = 900,
	GENID_Edition_EDITION_PROTO2 = 998,
	GENID_Edition_EDITION_PROTO3 = 999,
	GENID_Edition_EDITION_2023 = 1000,
	GENID_Edition_EDITION_2024 = 1001,
	GENID_Edition_EDITION_UNSTABLE = 9999,
};

/* Names and numbers of google/protobuf/go_features.proto (go_features_gen.go). */
enum {
	GENID_FeatureSet_Go_ext_number = 1002,
	GENID_GoFeatures_LegacyUnmarshalJsonEnum_field_number = 1,
	GENID_GoFeatures_ApiLevel_field_number = 2,
	GENID_GoFeatures_StripEnumPrefix_field_number = 3,
	GENID_GoFeatures_API_LEVEL_UNSPECIFIED_enum_value = 0,
	GENID_GoFeatures_API_OPEN_enum_value = 1,
	GENID_GoFeatures_API_HYBRID_enum_value = 2,
	GENID_GoFeatures_API_OPAQUE_enum_value = 3,
	GENID_GoFeatures_STRIP_ENUM_PREFIX_UNSPECIFIED_enum_value = 0,
	GENID_GoFeatures_STRIP_ENUM_PREFIX_KEEP_enum_value = 1,
	GENID_GoFeatures_STRIP_ENUM_PREFIX_GENERATE_BOTH_enum_value = 2,
	GENID_GoFeatures_STRIP_ENUM_PREFIX_STRIP_enum_value = 3,
};

/* Fields of google/protobuf/compiler/plugin.proto. */
enum {
	GENID_CodeGeneratorRequest_FileToGenerate_field_number = 1,
	GENID_CodeGeneratorRequest_Parameter_field_number = 2,
	GENID_CodeGeneratorRequest_CompilerVersion_field_number = 3,
	GENID_CodeGeneratorRequest_ProtoFile_field_number = 15,
	GENID_Version_Major_field_number = 1,
	GENID_Version_Minor_field_number = 2,
	GENID_Version_Patch_field_number = 3,
	GENID_Version_Suffix_field_number = 4,
};

/* Well-known type file names (*_gen.go). */
#define GENID_File_google_protobuf_any_proto "google/protobuf/any.proto"
#define GENID_File_google_protobuf_timestamp_proto "google/protobuf/timestamp.proto"
#define GENID_File_google_protobuf_duration_proto "google/protobuf/duration.proto"
#define GENID_File_google_protobuf_struct_proto "google/protobuf/struct.proto"
#define GENID_File_google_protobuf_field_mask_proto "google/protobuf/field_mask.proto"

/* Well-known message full names (*_gen.go). */
#define GENID_Any_message_fullname "google.protobuf.Any"
#define GENID_Timestamp_message_fullname "google.protobuf.Timestamp"
#define GENID_Duration_message_fullname "google.protobuf.Duration"
#define GENID_Struct_message_fullname "google.protobuf.Struct"
#define GENID_ListValue_message_fullname "google.protobuf.ListValue"
#define GENID_Value_message_fullname "google.protobuf.Value"
#define GENID_FieldMask_message_fullname "google.protobuf.FieldMask"
#define GENID_BoolValue_message_fullname "google.protobuf.BoolValue"
#define GENID_Int32Value_message_fullname "google.protobuf.Int32Value"
#define GENID_Int64Value_message_fullname "google.protobuf.Int64Value"
#define GENID_UInt32Value_message_fullname "google.protobuf.UInt32Value"
#define GENID_UInt64Value_message_fullname "google.protobuf.UInt64Value"
#define GENID_FloatValue_message_fullname "google.protobuf.FloatValue"
#define GENID_DoubleValue_message_fullname "google.protobuf.DoubleValue"
#define GENID_StringValue_message_fullname "google.protobuf.StringValue"
#define GENID_BytesValue_message_fullname "google.protobuf.BytesValue"

#endif
