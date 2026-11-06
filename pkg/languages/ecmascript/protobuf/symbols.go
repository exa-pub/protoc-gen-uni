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

package protobuf

// Ported from packages/protobuf/src/codegenv2/symbols.ts, codegenv2/scalar.ts and
// wkt/wrappers.ts.

// PackageName is the name of the runtime package.
const PackageName = "@bufbuild/protobuf"

const legacyWktBarrel = PackageName + "/wkt"

// WktPublicImportPaths maps the Protobuf file path of well-known types to their import path.
var WktPublicImportPaths = map[string]string{
	"google/protobuf/compiler/plugin.proto":        legacyWktBarrel,
	"google/protobuf/any.proto":                    legacyWktBarrel,
	"google/protobuf/api.proto":                    PackageName + "/wkt/google/protobuf/api_pb.js",
	"google/protobuf/c_sharp_features.proto":       PackageName + "/wkt/google/protobuf/c_sharp_features_pb.js",
	"google/protobuf/cpp_features.proto":           PackageName + "/wkt/google/protobuf/cpp_features_pb.js",
	"google/protobuf/cpp_file_options.proto":       PackageName + "/wkt/google/protobuf/cpp_file_options_pb.js",
	"google/protobuf/cpp_options.proto":            PackageName + "/wkt/google/protobuf/cpp_options_pb.js",
	"google/protobuf/descriptor.proto":             legacyWktBarrel,
	"google/protobuf/duration.proto":               legacyWktBarrel,
	"google/protobuf/empty.proto":                  legacyWktBarrel,
	"google/protobuf/field_mask.proto":             legacyWktBarrel,
	"google/protobuf/go_features.proto":            PackageName + "/wkt/google/protobuf/go_features_pb.js",
	"google/protobuf/java_features.proto":          PackageName + "/wkt/google/protobuf/java_features_pb.js",
	"google/protobuf/json_enumvalue_options.proto": PackageName + "/wkt/google/protobuf/json_enumvalue_options_pb.js",
	"google/protobuf/json_options.proto":           PackageName + "/wkt/google/protobuf/json_options_pb.js",
	"google/protobuf/source_context.proto":         PackageName + "/wkt/google/protobuf/source_context_pb.js",
	"google/protobuf/struct.proto":                 legacyWktBarrel,
	"google/protobuf/timestamp.proto":              legacyWktBarrel,
	"google/protobuf/type.proto":                   PackageName + "/wkt/google/protobuf/type_pb.js",
	"google/protobuf/wrappers.proto":               legacyWktBarrel,
}

// SymbolInfo describes a symbol exported by the runtime.
type SymbolInfo struct {
	Name             string
	TypeOnly         bool
	From             string
	BootstrapWktFrom string
}

// Symbols are the runtime symbols generated code uses (the top-level of `symbols`).
var Symbols = []SymbolInfo{
	{"isMessage", false, PackageName, "../is-message.js"},
	{"Message", true, PackageName, "../types.js"},
	{"create", false, PackageName, "../create.js"},
	{"fromJson", false, PackageName, "../from-json.js"},
	{"fromJsonString", false, PackageName, "../from-json.js"},
	{"fromBinary", false, PackageName, "../from-binary.js"},
	{"toBinary", false, PackageName, "../to-binary.js"},
	{"toJson", false, PackageName, "../to-json.js"},
	{"toJsonString", false, PackageName, "../to-json.js"},
	{"protoInt64", false, PackageName, "../proto-int64.js"},
	{"JsonValue", true, PackageName, "../json-value.js"},
	{"JsonObject", true, PackageName, "../json-value.js"},
	{"UnknownEnum", true, PackageName, "../types.js"},
}

// CodegenSymbols are the symbols of `symbols.codegen`.
var CodegenSymbols = []SymbolInfo{
	{"boot", false, PackageName + "/codegenv2", "../codegenv2/boot.js"},
	{"fileDesc", false, PackageName + "/codegenv2", "../codegenv2/file.js"},
	{"enumDesc", false, PackageName + "/codegenv2", "../codegenv2/enum.js"},
	{"extDesc", false, PackageName + "/codegenv2", "../codegenv2/extension.js"},
	{"messageDesc", false, PackageName + "/codegenv2", "../codegenv2/message.js"},
	{"serviceDesc", false, PackageName + "/codegenv2", "../codegenv2/service.js"},
	{"tsEnum", false, PackageName + "/codegenv2", "../codegenv2/enum.js"},
	{"objEnum", false, PackageName + "/codegenv2", "../codegenv2/enum.js"},
	{"GenFile", true, PackageName + "/codegenv2", "../codegenv2/types.js"},
	{"GenEnum", true, PackageName + "/codegenv2", "../codegenv2/types.js"},
	{"GenExtension", true, PackageName + "/codegenv2", "../codegenv2/types.js"},
	{"GenMessage", true, PackageName + "/codegenv2", "../codegenv2/types.js"},
	{"GenService", true, PackageName + "/codegenv2", "../codegenv2/types.js"},
}

// ScalarTypeScriptType returns the TypeScript type for the given scalar type.
func ScalarTypeScriptType(scalar ScalarType, longAsString bool) string {
	switch scalar {
	case ScalarString:
		return "string"
	case ScalarBool:
		return "boolean"
	case ScalarUint64, ScalarSfixed64, ScalarFixed64, ScalarSint64, ScalarInt64:
		if longAsString {
			return "string"
		}
		return "bigint"
	case ScalarBytes:
		return "Uint8Array"
	default:
		return "number"
	}
}

// ScalarJSONType returns the JSON type for the given scalar type.
func ScalarJSONType(scalar ScalarType) string {
	switch scalar {
	case ScalarDouble, ScalarFloat:
		return `number | "NaN" | "Infinity" | "-Infinity"`
	case ScalarUint64, ScalarSfixed64, ScalarFixed64, ScalarSint64, ScalarInt64:
		return "string"
	case ScalarInt32, ScalarFixed32, ScalarUint32, ScalarSfixed32, ScalarSint32:
		return "number"
	case ScalarString:
		return "string"
	case ScalarBool:
		return "boolean"
	case ScalarBytes:
		return "string"
	}
	return ""
}

var wrapperTypeNames = map[string]bool{
	"google.protobuf.DoubleValue": true,
	"google.protobuf.FloatValue":  true,
	"google.protobuf.Int64Value":  true,
	"google.protobuf.UInt64Value": true,
	"google.protobuf.Int32Value":  true,
	"google.protobuf.UInt32Value": true,
	"google.protobuf.BoolValue":   true,
	"google.protobuf.StringValue": true,
	"google.protobuf.BytesValue":  true,
}

// IsWrapperDesc reports whether the message is a well-known wrapper type, e.g.
// google.protobuf.Int32Value.
func IsWrapperDesc(m *DescMessage) bool {
	if !wrapperTypeNames[m.TypeName] || len(m.Fields) == 0 {
		return false
	}
	f := m.Fields[0]
	return f.FieldKind == FieldKindScalar && f.Name == "value" && f.Number == 1
}
