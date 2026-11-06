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

import "github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"

// Ported from packages/protoplugin/src/runtime-imports.ts.

// RuntimeImports provides symbols from the runtime library @bufbuild/protobuf.
type RuntimeImports struct {
	IsMessage      *ImportSymbol
	Message        *ImportSymbol
	Create         *ImportSymbol
	FromJSON       *ImportSymbol
	FromJSONString *ImportSymbol
	FromBinary     *ImportSymbol
	ToBinary       *ImportSymbol
	ToJSON         *ImportSymbol
	ToJSONString   *ImportSymbol
	ProtoInt64     *ImportSymbol
	JSONValue      *ImportSymbol
	JSONObject     *ImportSymbol
	UnknownEnum    *ImportSymbol
	Codegen        CodegenImports
}

// CodegenImports are the symbols of @bufbuild/protobuf/codegenv2.
type CodegenImports struct {
	Boot         *ImportSymbol
	FileDesc     *ImportSymbol
	EnumDesc     *ImportSymbol
	ExtDesc      *ImportSymbol
	MessageDesc  *ImportSymbol
	ServiceDesc  *ImportSymbol
	TsEnum       *ImportSymbol
	ObjEnum      *ImportSymbol
	GenFile      *ImportSymbol
	GenEnum      *ImportSymbol
	GenExtension *ImportSymbol
	GenMessage   *ImportSymbol
	GenService   *ImportSymbol
}

func createRuntimeImports(bootstrapWkt bool) *RuntimeImports {
	sym := func(list []protobuf.SymbolInfo) map[string]*ImportSymbol {
		m := map[string]*ImportSymbol{}
		for _, s := range list {
			from := s.From
			if bootstrapWkt {
				from = s.BootstrapWktFrom
			}
			m[s.Name] = NewImportSymbol(s.Name, from, s.TypeOnly)
		}
		return m
	}
	r := sym(protobuf.Symbols)
	c := sym(protobuf.CodegenSymbols)
	return &RuntimeImports{
		IsMessage:      r["isMessage"],
		Message:        r["Message"],
		Create:         r["create"],
		FromJSON:       r["fromJson"],
		FromJSONString: r["fromJsonString"],
		FromBinary:     r["fromBinary"],
		ToBinary:       r["toBinary"],
		ToJSON:         r["toJson"],
		ToJSONString:   r["toJsonString"],
		ProtoInt64:     r["protoInt64"],
		JSONValue:      r["JsonValue"],
		JSONObject:     r["JsonObject"],
		UnknownEnum:    r["UnknownEnum"],
		Codegen: CodegenImports{
			Boot:         c["boot"],
			FileDesc:     c["fileDesc"],
			EnumDesc:     c["enumDesc"],
			ExtDesc:      c["extDesc"],
			MessageDesc:  c["messageDesc"],
			ServiceDesc:  c["serviceDesc"],
			TsEnum:       c["tsEnum"],
			ObjEnum:      c["objEnum"],
			GenFile:      c["GenFile"],
			GenEnum:      c["GenEnum"],
			GenExtension: c["GenExtension"],
			GenMessage:   c["GenMessage"],
			GenService:   c["GenService"],
		},
	}
}
