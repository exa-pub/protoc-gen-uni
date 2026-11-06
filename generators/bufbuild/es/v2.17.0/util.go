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

package es

import (
	"strings"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protoplugin"
)

// Ported from packages/protoc-gen-es/src/util.ts.

type printable = protoplugin.Printable

// messageGenType returns a type expression for `GenMessage` from @bufbuild/protobuf/codegenv2,
// with the mandatory `RuntimeShape` parameter, and the optional `OptShapes` parameter providing
// a JSON type, and/or a Valid type.
func messageGenType(desc *protobuf.DescMessage, f *protoplugin.GeneratedFile, opts options) printable {
	var p2 []printable
	if opts.jsonTypes {
		p2 = append(p2, []printable{"jsonType: ", f.ImportJSON(desc)})
	}
	if opts.validTypes.legacyRequired || opts.validTypes.protovalidateRequired {
		p2 = append(p2, []printable{"validType: ", f.ImportValid(desc)})
	}
	var opt printable = []printable{}
	if len(p2) > 0 {
		opt = []printable{", {", commaSeparate(p2), "}"}
	}
	return []printable{f.Runtime.Codegen.GenMessage, "<", f.ImportShape(desc), opt, ">"}
}

func isMapKeyNumber(key protobuf.ScalarType) bool {
	switch key {
	case protobuf.ScalarInt32, protobuf.ScalarFixed32, protobuf.ScalarUint32, protobuf.ScalarSfixed32, protobuf.ScalarSint32:
		return true
	}
	return false
}

func fieldTypeScriptType(field *protobuf.DescField, imports *protoplugin.RuntimeImports, validTypes bool) (typing printable, optional bool) {
	var t []printable
	switch field.FieldKind {
	case protobuf.FieldKindScalar:
		t = append(t, protobuf.ScalarTypeScriptType(field.Scalar, field.LongAsString))
		optional = field.Proto.GetProto3Optional()
	case protobuf.FieldKindMessage:
		t = append(t, messageFieldTypeScriptType(field, imports, validTypes))
		optional = true
	case protobuf.FieldKindEnum:
		t = append(t, protoplugin.ShapeImport{Desc: field.Enum})
		optional = field.Proto.GetProto3Optional()
	case protobuf.FieldKindList:
		optional = false
		switch field.ListKind {
		case protobuf.FieldKindEnum:
			t = append(t, protoplugin.ShapeImport{Desc: field.Enum}, "[]")
		case protobuf.FieldKindScalar:
			t = append(t, protobuf.ScalarTypeScriptType(field.Scalar, field.LongAsString), "[]")
		case protobuf.FieldKindMessage:
			t = append(t, messageFieldTypeScriptType(field, imports, validTypes), "[]")
		}
	case protobuf.FieldKindMap:
		keyType := "string"
		if isMapKeyNumber(field.MapKey) {
			keyType = "number"
		}
		var valueType printable
		switch field.MapKind {
		case protobuf.FieldKindScalar:
			valueType = protobuf.ScalarTypeScriptType(field.Scalar, false)
		case protobuf.FieldKindMessage:
			valueType = messageFieldTypeScriptType(field, imports, validTypes)
		case protobuf.FieldKindEnum:
			valueType = protoplugin.ShapeImport{Desc: field.Enum}
		}
		t = append(t, "{ [key: ", keyType, "]: ", valueType, " }")
		optional = false
	}
	return t, optional
}

func messageFieldTypeScriptType(field *protobuf.DescField, imports *protoplugin.RuntimeImports, validTypes bool) printable {
	if protobuf.IsWrapperDesc(field.Message) && field.Oneof == nil && field.FieldKind == protobuf.FieldKindMessage {
		return protobuf.ScalarTypeScriptType(field.Message.Fields[0].Scalar, false)
	}
	if field.Message.TypeName == "google.protobuf.Struct" && (field.Parent == nil || field.Parent.TypeName != "google.protobuf.Value") {
		return imports.JSONObject
	}
	if _, wkt := protobuf.WktPublicImportPaths[field.Message.File.Proto.GetName()]; validTypes && !wkt {
		return protoplugin.ValidTypeImport{Desc: field.Message}
	}
	return protoplugin.ShapeImport{Desc: field.Message}
}

func fieldJSONType(field *protobuf.DescField) printable {
	switch field.FieldKind {
	case protobuf.FieldKindScalar:
		return protobuf.ScalarJSONType(field.Scalar)
	case protobuf.FieldKindMessage:
		return protoplugin.JSONTypeImport{Desc: field.Message}
	case protobuf.FieldKindEnum:
		return protoplugin.JSONTypeImport{Desc: field.Enum}
	case protobuf.FieldKindList:
		switch field.ListKind {
		case protobuf.FieldKindEnum:
			return []printable{protoplugin.JSONTypeImport{Desc: field.Enum}, "[]"}
		case protobuf.FieldKindScalar:
			t := protobuf.ScalarJSONType(field.Scalar)
			if strings.Contains(t, "|") {
				return []printable{"(", t, ")[]"}
			}
			return []printable{t, "[]"}
		case protobuf.FieldKindMessage:
			return []printable{protoplugin.JSONTypeImport{Desc: field.Message}, "[]"}
		}
	case protobuf.FieldKindMap:
		keyType := "string"
		if isMapKeyNumber(field.MapKey) {
			keyType = "number"
		}
		var valueType printable
		switch field.MapKind {
		case protobuf.FieldKindScalar:
			valueType = protobuf.ScalarJSONType(field.Scalar)
		case protobuf.FieldKindMessage:
			valueType = protoplugin.JSONTypeImport{Desc: field.Message}
		case protobuf.FieldKindEnum:
			valueType = protoplugin.JSONTypeImport{Desc: field.Enum}
		}
		return []printable{"{ [key: ", keyType, "]: ", valueType, " }"}
	}
	return nil
}

func functionCall(fn printable, args []printable) printable {
	return []printable{fn, "(", commaSeparate(args), ")"}
}

func commaSeparate(elements []printable) printable {
	r := make([]printable, 0, 2*len(elements))
	for i, e := range elements {
		r = append(r, e)
		if i < len(elements)-1 {
			r = append(r, ", ")
		}
	}
	return r
}
