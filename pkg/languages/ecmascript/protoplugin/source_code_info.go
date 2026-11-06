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

package protoplugin

import (
	"regexp"
	"slices"
	"strconv"
	"strings"

	"google.golang.org/protobuf/encoding/protowire"
	"google.golang.org/protobuf/types/descriptorpb"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoplugin/src/source-code-info.ts.

// DescComments are the comments on an element in the protobuf source.
type DescComments struct {
	LeadingDetached []string
	Leading         *string
	Trailing        *string
	SourcePath      []int32
}

// Field numbers of descriptor.proto used in source paths.
const (
	fileSyntaxNumber      = 12
	filePackageNumber     = 2
	fileMessageTypeNumber = 4
	fileEnumTypeNumber    = 5
	fileServiceNumber     = 6
	fileExtensionNumber   = 7
	msgFieldNumber        = 2
	msgNestedTypeNumber   = 3
	msgEnumTypeNumber     = 4
	msgExtensionNumber    = 6
	msgOneofDeclNumber    = 8
	enumValueNumber       = 2
	serviceMethodNumber   = 2
)

// GetPackageComments returns the comments on the package element in the protobuf source.
func GetPackageComments(desc *protobuf.DescFile) DescComments {
	return findComments(desc.Proto.GetSourceCodeInfo(), []int32{filePackageNumber})
}

// GetSyntaxComments returns the comments on the syntax element in the protobuf source.
func GetSyntaxComments(desc *protobuf.DescFile) DescComments {
	return findComments(desc.Proto.GetSourceCodeInfo(), []int32{fileSyntaxNumber})
}

func appendPath(base []int32, elems ...int) []int32 {
	out := slices.Clone(base)
	for _, e := range elems {
		out = append(out, int32(e))
	}
	return out
}

func protoIndex[T comparable](s []T, v T) int {
	for i, x := range s {
		if x == v {
			return i
		}
	}
	return -1
}

// GetComments returns the comments on the element in the protobuf source.
func GetComments(desc protobuf.Desc) DescComments {
	var path []int32
	var file *protobuf.DescFile
	switch d := desc.(type) {
	case *protobuf.DescEnum:
		if d.Parent != nil {
			path = appendPath(GetComments(d.Parent).SourcePath, msgEnumTypeNumber, protoIndex(d.Parent.Proto.EnumType, d.Proto))
		} else {
			path = appendPath(nil, fileEnumTypeNumber, protoIndex(d.File.Proto.EnumType, d.Proto))
		}
		file = d.File
	case *protobuf.DescOneof:
		path = appendPath(GetComments(d.Parent).SourcePath, msgOneofDeclNumber, protoIndex(d.Parent.Proto.OneofDecl, d.Proto))
		file = d.Parent.File
	case *protobuf.DescMessage:
		if d.Parent != nil {
			path = appendPath(GetComments(d.Parent).SourcePath, msgNestedTypeNumber, protoIndex(d.Parent.Proto.NestedType, d.Proto))
		} else {
			path = appendPath(nil, fileMessageTypeNumber, protoIndex(d.File.Proto.MessageType, d.Proto))
		}
		file = d.File
	case *protobuf.DescEnumValue:
		path = appendPath(GetComments(d.Parent).SourcePath, enumValueNumber, protoIndex(d.Parent.Proto.Value, d.Proto))
		file = d.Parent.File
	case *protobuf.DescField:
		switch {
		case !d.IsExtension:
			path = appendPath(GetComments(d.Parent).SourcePath, msgFieldNumber, protoIndex(d.Parent.Proto.Field, d.Proto))
			file = d.Parent.File
		case d.Parent != nil:
			path = appendPath(GetComments(d.Parent).SourcePath, msgExtensionNumber, protoIndex(d.Parent.Proto.Extension, d.Proto))
			file = d.File
		default:
			path = appendPath(nil, fileExtensionNumber, protoIndex(d.File.Proto.Extension, d.Proto))
			file = d.File
		}
	case *protobuf.DescService:
		path = appendPath(nil, fileServiceNumber, protoIndex(d.File.Proto.Service, d.Proto))
		file = d.File
	case *protobuf.DescMethod:
		path = appendPath(GetComments(d.Parent).SourcePath, serviceMethodNumber, protoIndex(d.Parent.Proto.Method, d.Proto))
		file = d.Parent.File
	}
	return findComments(file.Proto.GetSourceCodeInfo(), path)
}

// featureSetField describes an enum field of google.protobuf.FeatureSet, as known to
// @bufbuild/protobuf v2.17.0.
type featureSetField struct {
	number int32
	name   string
	values map[int32]string
}

var featureSetFields = []featureSetField{
	{1, "field_presence", map[int32]string{0: "FIELD_PRESENCE_UNKNOWN", 1: "EXPLICIT", 2: "IMPLICIT", 3: "LEGACY_REQUIRED"}},
	{2, "enum_type", map[int32]string{0: "ENUM_TYPE_UNKNOWN", 1: "OPEN", 2: "CLOSED"}},
	{3, "repeated_field_encoding", map[int32]string{0: "REPEATED_FIELD_ENCODING_UNKNOWN", 1: "PACKED", 2: "EXPANDED"}},
	{4, "utf8_validation", map[int32]string{0: "UTF8_VALIDATION_UNKNOWN", 2: "VERIFY", 3: "NONE"}},
	{5, "message_encoding", map[int32]string{0: "MESSAGE_ENCODING_UNKNOWN", 1: "LENGTH_PREFIXED", 2: "DELIMITED"}},
	{6, "json_format", map[int32]string{0: "JSON_FORMAT_UNKNOWN", 1: "ALLOW", 2: "LEGACY_BEST_EFFORT"}},
	{7, "enforce_naming_style", map[int32]string{0: "ENFORCE_NAMING_STYLE_UNKNOWN", 1: "STYLE2024", 2: "STYLE_LEGACY", 3: "STYLE2026"}},
	{8, "default_symbol_visibility", map[int32]string{0: "DEFAULT_SYMBOL_VISIBILITY_UNKNOWN", 1: "EXPORT_ALL", 2: "EXPORT_TOP_LEVEL", 3: "LOCAL_ALL", 4: "STRICT"}},
	{9, "enforce_proto_limits", map[int32]string{0: "PROTO_LIMITS_UNKNOWN", 1: "LEGACY_NO_EXPLICIT_LIMITS", 2: "PROTO_LIMITS2026"}},
}

// featureSetValues returns the values of the set enum fields of a FeatureSet, by field number.
// Field 9 (enforce_proto_limits) is unknown to the Go runtime and read from unknown fields.
func featureSetValues(fs *descriptorpb.FeatureSet) map[int32]int32 {
	vals := map[int32]int32{}
	if fs.FieldPresence != nil {
		vals[1] = int32(fs.GetFieldPresence())
	}
	if fs.EnumType != nil {
		vals[2] = int32(fs.GetEnumType())
	}
	if fs.RepeatedFieldEncoding != nil {
		vals[3] = int32(fs.GetRepeatedFieldEncoding())
	}
	if fs.Utf8Validation != nil {
		vals[4] = int32(fs.GetUtf8Validation())
	}
	if fs.MessageEncoding != nil {
		vals[5] = int32(fs.GetMessageEncoding())
	}
	if fs.JsonFormat != nil {
		vals[6] = int32(fs.GetJsonFormat())
	}
	if fs.EnforceNamingStyle != nil {
		vals[7] = int32(fs.GetEnforceNamingStyle())
	}
	if fs.DefaultSymbolVisibility != nil {
		vals[8] = int32(fs.GetDefaultSymbolVisibility())
	}
	for _, uf := range protobuf.UnknownFields(fs) {
		if uf.Number != 9 {
			continue
		}
		switch uf.Type {
		case protowire.VarintType:
			v, n := protowire.ConsumeVarint(uf.Data)
			if n > 0 && featureSetFields[8].values[int32(v)] != "" {
				vals[9] = int32(v)
			}
		case protowire.BytesType:
			b, n := protowire.ConsumeBytes(uf.Data)
			for n > 0 && len(b) > 0 {
				v, m := protowire.ConsumeVarint(b)
				if m < 0 {
					break
				}
				if featureSetFields[8].values[int32(v)] != "" {
					vals[9] = int32(v)
				}
				b = b[m:]
			}
		}
	}
	return vals
}

// GetFeatureOptionStrings returns the feature options set on the element in the protobuf source,
// e.g. "features.field_presence = IMPLICIT".
func GetFeatureOptionStrings(desc protobuf.Desc) []string {
	features := featuresOf(desc)
	if features == nil {
		return nil
	}
	var out []string
	vals := featureSetValues(features)
	for _, f := range featureSetFields {
		v, ok := vals[f.number]
		if !ok {
			continue
		}
		if name, ok := f.values[v]; ok {
			out = append(out, "features."+f.name+" = "+name)
		}
	}
	return out
}

func featuresOf(desc protobuf.Desc) *descriptorpb.FeatureSet {
	switch d := desc.(type) {
	case *protobuf.DescFile:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescMessage:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescEnum:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescEnumValue:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescField:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescOneof:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescService:
		return d.Proto.GetOptions().GetFeatures()
	case *protobuf.DescMethod:
		return d.Proto.GetOptions().GetFeatures()
	}
	return nil
}

// GetDeclarationString returns a string that matches the definition of a field in the protobuf
// source. Does not take custom options into account.
func GetDeclarationString(desc protobuf.Desc) string {
	if v, ok := desc.(*protobuf.DescEnumValue); ok {
		str := v.Name + " = " + strconv.Itoa(int(v.Number))
		if v.Proto.GetOptions().GetDeprecated() {
			str += " [deprecated = true]"
		}
		return str
	}
	field := desc.(*protobuf.DescField)
	typeName := func() string {
		if field.Message != nil {
			return field.Message.TypeName
		}
		if field.Enum != nil {
			return field.Enum.TypeName
		}
		return strings.ToLower(field.Scalar.String())
	}
	var parts []string
	switch field.FieldKind {
	case protobuf.FieldKindScalar, protobuf.FieldKindEnum, protobuf.FieldKindMessage:
		if fieldHasRequiredKeyword(field) {
			parts = append(parts, "required")
		}
		if fieldHasOptionalKeyword(field) {
			parts = append(parts, "optional")
		}
		parts = append(parts, typeName())
	case protobuf.FieldKindList:
		parts = append(parts, "repeated", typeName())
	case protobuf.FieldKindMap:
		parts = append(parts, "map<"+strings.ToLower(field.MapKey.String())+", "+typeName()+">")
	}
	parts = append(parts, field.Name, "=", strconv.Itoa(int(field.Number)))
	var options []string
	protoOptions := field.Proto.GetOptions()
	if protoOptions != nil && protoOptions.Packed != nil {
		options = append(options, "packed = "+strconv.FormatBool(protoOptions.GetPacked()))
	}
	if field.Proto.DefaultValue != nil {
		defaultValue := field.Proto.GetDefaultValue()
		if t := field.Proto.GetType(); t == descriptorpb.FieldDescriptorProto_TYPE_BYTES || t == descriptorpb.FieldDescriptorProto_TYPE_STRING {
			defaultValue = `"` + strings.Replace(defaultValue, `"`, `\"`, 1) + `"`
		}
		options = append(options, "default = "+defaultValue)
	}
	if !field.IsExtension && field.JSONName != protobuf.ProtoCamelCase(field.Name) {
		options = append(options, `json_name = "`+field.JSONName+`"`)
	}
	if protoOptions != nil && protoOptions.Jstype != nil {
		options = append(options, "jstype = "+protoOptions.GetJstype().String())
	}
	if protoOptions != nil && protoOptions.Deprecated != nil {
		options = append(options, "deprecated = true")
	}
	options = append(options, GetFeatureOptionStrings(field)...)
	if len(options) > 0 {
		parts = append(parts, "["+strings.Join(options, ", ")+"]")
	}
	return strings.Join(parts, " ")
}

func fieldHasRequiredKeyword(field *protobuf.DescField) bool {
	return field.DeclaringFile().Edition == protobuf.EditionProto2 &&
		field.Proto.GetLabel() == descriptorpb.FieldDescriptorProto_LABEL_REQUIRED
}

func fieldHasOptionalKeyword(field *protobuf.DescField) bool {
	switch field.DeclaringFile().Edition {
	case protobuf.EditionProto2:
		return field.Oneof == nil && field.Proto.GetLabel() == descriptorpb.FieldDescriptorProto_LABEL_OPTIONAL
	case protobuf.EditionProto3:
		return field.Proto.GetProto3Optional()
	}
	return false
}

func findComments(sourceCodeInfo *descriptorpb.SourceCodeInfo, sourcePath []int32) DescComments {
	if sourceCodeInfo == nil {
		return DescComments{SourcePath: sourcePath}
	}
	for _, location := range sourceCodeInfo.GetLocation() {
		if !slices.Equal(location.GetPath(), sourcePath) {
			continue
		}
		c := DescComments{SourcePath: sourcePath}
		for _, d := range location.GetLeadingDetachedComments() {
			c.LeadingDetached = append(c.LeadingDetached, normalizeLineBreaks(d))
		}
		if location.LeadingComments != nil {
			s := normalizeLineBreaks(location.GetLeadingComments())
			c.Leading = &s
		}
		if location.TrailingComments != nil {
			s := normalizeLineBreaks(location.GetTrailingComments())
			c.Trailing = &s
		}
		return c
	}
	return DescComments{SourcePath: sourcePath}
}

var lineBreakRE = regexp.MustCompile("\r\n|[\r  ]")

func normalizeLineBreaks(comment string) string {
	return lineBreakRE.ReplaceAllString(comment, "\n")
}
