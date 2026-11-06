// Copyright 2019 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package genid contains constants for declarations in descriptor.proto
// and the well-known types.
//
// Ported from google.golang.org/protobuf v1.36.12 internal/genid: only the
// constants used by compiler/protogen and protoc-gen-go, with the same names
// and values.
package genid

import "google.golang.org/protobuf/reflect/protoreflect"

// Go names of implementation-specific struct fields in generated messages (goname.go, name.go).
const (
	State_goname           = "state"
	SizeCache_goname       = "sizeCache"
	UnknownFields_goname   = "unknownFields"
	ExtensionFields_goname = "extensionFields"
	BuilderSuffix_goname   = "_builder"
)

// Field numbers of google/protobuf/descriptor.proto (descriptor_gen.go).
const (
	FileDescriptorProto_Package_field_number     protoreflect.FieldNumber = 2
	FileDescriptorProto_MessageType_field_number protoreflect.FieldNumber = 4
	FileDescriptorProto_EnumType_field_number    protoreflect.FieldNumber = 5
	FileDescriptorProto_Service_field_number     protoreflect.FieldNumber = 6
	FileDescriptorProto_Extension_field_number   protoreflect.FieldNumber = 7
	FileDescriptorProto_Syntax_field_number      protoreflect.FieldNumber = 12
	DescriptorProto_Field_field_number           protoreflect.FieldNumber = 2
	DescriptorProto_Extension_field_number       protoreflect.FieldNumber = 6
	DescriptorProto_NestedType_field_number      protoreflect.FieldNumber = 3
	DescriptorProto_EnumType_field_number        protoreflect.FieldNumber = 4
	DescriptorProto_OneofDecl_field_number       protoreflect.FieldNumber = 8
	EnumDescriptorProto_Value_field_number       protoreflect.FieldNumber = 2
	ServiceDescriptorProto_Method_field_number   protoreflect.FieldNumber = 2
)

// Names and numbers of google/protobuf/go_features.proto (go_features_gen.go).
const (
	FeatureSet_Go_ext_number                              protoreflect.FieldNumber = 1002
	GoFeatures_LegacyUnmarshalJsonEnum_field_number       protoreflect.FieldNumber = 1
	GoFeatures_ApiLevel_field_number                      protoreflect.FieldNumber = 2
	GoFeatures_StripEnumPrefix_field_number               protoreflect.FieldNumber = 3
	GoFeatures_API_LEVEL_UNSPECIFIED_enum_value                                    = 0
	GoFeatures_API_OPEN_enum_value                                                 = 1
	GoFeatures_API_HYBRID_enum_value                                               = 2
	GoFeatures_API_OPAQUE_enum_value                                               = 3
	GoFeatures_STRIP_ENUM_PREFIX_UNSPECIFIED_enum_value                            = 0
	GoFeatures_STRIP_ENUM_PREFIX_KEEP_enum_value                                   = 1
	GoFeatures_STRIP_ENUM_PREFIX_GENERATE_BOTH_enum_value                          = 2
	GoFeatures_STRIP_ENUM_PREFIX_STRIP_enum_value                                  = 3
)

// Well-known type file names (*_gen.go).
const (
	File_google_protobuf_any_proto        = "google/protobuf/any.proto"
	File_google_protobuf_timestamp_proto  = "google/protobuf/timestamp.proto"
	File_google_protobuf_duration_proto   = "google/protobuf/duration.proto"
	File_google_protobuf_struct_proto     = "google/protobuf/struct.proto"
	File_google_protobuf_field_mask_proto = "google/protobuf/field_mask.proto"
)

// Well-known message full names (*_gen.go).
const (
	Any_message_fullname         protoreflect.FullName = "google.protobuf.Any"
	Timestamp_message_fullname   protoreflect.FullName = "google.protobuf.Timestamp"
	Duration_message_fullname    protoreflect.FullName = "google.protobuf.Duration"
	Struct_message_fullname      protoreflect.FullName = "google.protobuf.Struct"
	ListValue_message_fullname   protoreflect.FullName = "google.protobuf.ListValue"
	Value_message_fullname       protoreflect.FullName = "google.protobuf.Value"
	FieldMask_message_fullname   protoreflect.FullName = "google.protobuf.FieldMask"
	BoolValue_message_fullname   protoreflect.FullName = "google.protobuf.BoolValue"
	Int32Value_message_fullname  protoreflect.FullName = "google.protobuf.Int32Value"
	Int64Value_message_fullname  protoreflect.FullName = "google.protobuf.Int64Value"
	UInt32Value_message_fullname protoreflect.FullName = "google.protobuf.UInt32Value"
	UInt64Value_message_fullname protoreflect.FullName = "google.protobuf.UInt64Value"
	FloatValue_message_fullname  protoreflect.FullName = "google.protobuf.FloatValue"
	DoubleValue_message_fullname protoreflect.FullName = "google.protobuf.DoubleValue"
	StringValue_message_fullname protoreflect.FullName = "google.protobuf.StringValue"
	BytesValue_message_fullname  protoreflect.FullName = "google.protobuf.BytesValue"
)
