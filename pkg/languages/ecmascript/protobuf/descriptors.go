// Copyright 2021-2026 Buf Technologies, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package protobuf is a port of the parts of @bufbuild/protobuf (protobuf-es) that code generators
// need: the descriptor graph built from FileDescriptorProtos, naming helpers, editions feature
// resolution, descriptor embedding, and the codegenv2 runtime symbols.
//
// Ported from packages/protobuf/src of protobuf-es v2.17.0. Each file names its upstream source.
package protobuf

import (
	"google.golang.org/protobuf/types/descriptorpb"
)

// Ported from packages/protobuf/src/descriptors.ts.

// ScalarType is a scalar field type. The numbers match google.protobuf.FieldDescriptorProto.Type.
type ScalarType int32

// Scalar types.
const (
	ScalarDouble   ScalarType = 1
	ScalarFloat    ScalarType = 2
	ScalarInt64    ScalarType = 3
	ScalarUint64   ScalarType = 4
	ScalarInt32    ScalarType = 5
	ScalarFixed64  ScalarType = 6
	ScalarFixed32  ScalarType = 7
	ScalarBool     ScalarType = 8
	ScalarString   ScalarType = 9
	ScalarBytes    ScalarType = 12
	ScalarUint32   ScalarType = 13
	ScalarSfixed32 ScalarType = 15
	ScalarSfixed64 ScalarType = 16
	ScalarSint32   ScalarType = 17
	ScalarSint64   ScalarType = 18
)

var scalarNames = map[ScalarType]string{
	ScalarDouble: "DOUBLE", ScalarFloat: "FLOAT", ScalarInt64: "INT64", ScalarUint64: "UINT64",
	ScalarInt32: "INT32", ScalarFixed64: "FIXED64", ScalarFixed32: "FIXED32", ScalarBool: "BOOL",
	ScalarString: "STRING", ScalarBytes: "BYTES", ScalarUint32: "UINT32", ScalarSfixed32: "SFIXED32",
	ScalarSfixed64: "SFIXED64", ScalarSint32: "SINT32", ScalarSint64: "SINT64",
}

// String returns the enum name, e.g. "INT32", like ScalarType[x] in TypeScript.
func (s ScalarType) String() string {
	return scalarNames[s]
}

// Edition numbers (google.protobuf.Edition).
const (
	EditionProto2   = descriptorpb.Edition_EDITION_PROTO2
	EditionProto3   = descriptorpb.Edition_EDITION_PROTO3
	Edition2023     = descriptorpb.Edition_EDITION_2023
	Edition2024     = descriptorpb.Edition_EDITION_2024
	Edition2026     = descriptorpb.Edition(1002)
	EditionUnstable = descriptorpb.Edition(9999)
)

// MinimumEdition and MaximumEdition are the editions supported by @bufbuild/protobuf.
const (
	MinimumEdition = EditionProto2
	MaximumEdition = Edition2026
)

// Field presence (google.protobuf.FeatureSet.FieldPresence).
const (
	PresenceExplicit       = 1
	PresenceImplicit       = 2
	PresenceLegacyRequired = 3
)

// FieldKind is the kind of a field: "scalar", "enum", "message", "list" or "map".
type FieldKind string

// Field kinds.
const (
	FieldKindScalar  FieldKind = "scalar"
	FieldKindEnum    FieldKind = "enum"
	FieldKindMessage FieldKind = "message"
	FieldKindList    FieldKind = "list"
	FieldKindMap     FieldKind = "map"
)

// Desc is any descriptor: *DescFile, *DescMessage, *DescEnum, *DescEnumValue, *DescField
// (fields and extensions), *DescOneof, *DescService, *DescMethod.
type Desc interface {
	// Kind returns "file", "message", "enum", "enum_value", "field", "extension", "oneof",
	// "service" or "rpc".
	Kind() string
	// String returns a description such as "message foo.Bar".
	String() string
}

// DescFile describes a protobuf source file.
type DescFile struct {
	Proto        *descriptorpb.FileDescriptorProto
	Deprecated   bool
	Edition      descriptorpb.Edition
	Name         string // file name without the .proto suffix
	Dependencies []*DescFile
	Enums        []*DescEnum
	Messages     []*DescMessage
	Extensions   []*DescField
	Services     []*DescService
}

func (*DescFile) Kind() string     { return "file" }
func (d *DescFile) String() string { return "file " + d.Proto.GetName() }

// DescEnum describes an enumeration.
type DescEnum struct {
	Proto        *descriptorpb.EnumDescriptorProto
	Deprecated   bool
	File         *DescFile
	Parent       *DescMessage
	Open         bool
	Name         string
	TypeName     string
	Values       []*DescEnumValue
	SharedPrefix string // empty if none
}

func (*DescEnum) Kind() string     { return "enum" }
func (d *DescEnum) String() string { return "enum " + d.TypeName }

// DescEnumValue describes an enumeration value.
type DescEnumValue struct {
	Proto      *descriptorpb.EnumValueDescriptorProto
	Deprecated bool
	Parent     *DescEnum
	Name       string
	LocalName  string
	Number     int32
	JSONName   string
}

func (*DescEnumValue) Kind() string { return "enum_value" }
func (d *DescEnumValue) String() string {
	return "enum value " + d.Parent.TypeName + "." + d.Name
}

// DescMessage describes a message.
type DescMessage struct {
	Proto            *descriptorpb.DescriptorProto
	Deprecated       bool
	File             *DescFile
	Parent           *DescMessage
	Name             string
	TypeName         string
	Fields           []*DescField
	Oneofs           []*DescOneof
	Members          []Desc // *DescField or *DescOneof, in declaration order
	NestedEnums      []*DescEnum
	NestedMessages   []*DescMessage
	NestedExtensions []*DescField
}

func (*DescMessage) Kind() string     { return "message" }
func (d *DescMessage) String() string { return "message " + d.TypeName }

// DescField describes a field of a message, or an extension (IsExtension).
type DescField struct {
	Proto      *descriptorpb.FieldDescriptorProto
	Deprecated bool
	Name       string
	Number     int32
	JSONName   string
	FieldKind  FieldKind
	Presence   int32

	// Scalar is set for scalar fields, lists of scalars and maps with scalar values.
	Scalar ScalarType
	// Message is set for message fields, lists of messages and maps with message values.
	Message *DescMessage
	// Enum is set for enum fields, lists of enums and maps with enum values.
	Enum *DescEnum
	// ListKind is "scalar", "enum" or "message" for list fields.
	ListKind FieldKind
	// MapKind is "scalar", "enum" or "message" for map fields.
	MapKind FieldKind
	// MapKey is the key type of map fields.
	MapKey            ScalarType
	Packed            bool
	DelimitedEncoding bool
	LongAsString      bool

	// Parent is the message the field belongs to. For extensions, it is the message the extension
	// is declared in, or nil.
	Parent *DescMessage
	// Oneof is the oneof group the field belongs to (never set for extensions).
	Oneof *DescOneof
	// LocalName is the property name (not set for extensions).
	LocalName string

	// IsExtension reports whether this is an extension.
	IsExtension bool
	// File is the file an extension is declared in.
	File *DescFile
	// TypeName is the fully qualified name of an extension.
	TypeName string
	// Extendee is the message an extension extends.
	Extendee *DescMessage
}

func (d *DescField) Kind() string {
	if d.IsExtension {
		return "extension"
	}
	return "field"
}

func (d *DescField) String() string {
	if d.IsExtension {
		return "extension " + d.TypeName
	}
	return "field " + d.Parent.TypeName + "." + d.Proto.GetName()
}

// DeclaringFile returns the file the field or extension is declared in.
func (d *DescField) DeclaringFile() *DescFile {
	if d.IsExtension {
		return d.File
	}
	return d.Parent.File
}

// DescOneof describes a oneof group.
type DescOneof struct {
	Proto     *descriptorpb.OneofDescriptorProto
	Parent    *DescMessage
	Fields    []*DescField
	Name      string
	LocalName string
}

func (*DescOneof) Kind() string     { return "oneof" }
func (d *DescOneof) String() string { return "oneof " + d.Parent.TypeName + "." + d.Name }

// DescService describes a service.
type DescService struct {
	Proto      *descriptorpb.ServiceDescriptorProto
	Deprecated bool
	File       *DescFile
	Name       string
	TypeName   string
	Methods    []*DescMethod
}

func (*DescService) Kind() string     { return "service" }
func (d *DescService) String() string { return "service " + d.TypeName }

// DescMethod describes a service method.
type DescMethod struct {
	Proto      *descriptorpb.MethodDescriptorProto
	Deprecated bool
	Parent     *DescService
	Name       string
	LocalName  string
	// MethodKind is "unary", "server_streaming", "client_streaming" or "bidi_streaming".
	MethodKind  string
	Input       *DescMessage
	Output      *DescMessage
	Idempotency descriptorpb.MethodOptions_IdempotencyLevel
}

func (*DescMethod) Kind() string { return "rpc" }
func (d *DescMethod) String() string {
	return "rpc " + d.Parent.TypeName + "." + d.Name
}

// DescFileOf returns the file of a message, enum, extension or service, or the file itself.
func DescFileOf(d Desc) *DescFile {
	switch d := d.(type) {
	case *DescFile:
		return d
	case *DescMessage:
		return d.File
	case *DescEnum:
		return d.File
	case *DescField:
		return d.DeclaringFile()
	case *DescService:
		return d.File
	case *DescEnumValue:
		return d.Parent.File
	case *DescOneof:
		return d.Parent.File
	case *DescMethod:
		return d.Parent.File
	}
	return nil
}
