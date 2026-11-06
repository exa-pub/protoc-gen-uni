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

// Ported from packages/protoplugin/src/printable.ts.

// Printable is anything that can be passed to GeneratedFile.Print:
//
//   - string: printed verbatim
//   - int, int32, int64, uint32, uint64, float64: a number literal
//   - bool: a boolean literal
//   - []byte: an expression that re-creates the Uint8Array
//   - *ImportSymbol: adds an import statement and prints the name of the symbol
//   - ExportStatement, JSDocBlock, LiteralString, LiteralProtoInt64
//   - DescImport, ShapeImport, JSONTypeImport, ValidTypeImport: import generated symbols
//   - []Printable: printed in sequence
type Printable = any

// ExportStatement is an export statement, see GeneratedFile.Export.
type ExportStatement struct {
	Name        string
	Declaration string
}

// LiteralProtoInt64 prints a 64-bit integer literal.
type LiteralProtoInt64 struct {
	Type         protobuf.ScalarType
	LongAsString bool
	// Value is a decimal integer.
	Value string
}

// LiteralString prints a string literal.
type LiteralString struct {
	Value string
}

// DescImport imports the descriptor generated for a message, enum, extension, service or file.
type DescImport struct {
	Desc     protobuf.Desc
	TypeOnly bool
}

// ShapeImport imports the type generated for a message or enum.
type ShapeImport struct {
	Desc protobuf.Desc
}

// JSONTypeImport imports the JSON type generated for a message or enum.
type JSONTypeImport struct {
	Desc protobuf.Desc
}

// ValidTypeImport imports the Valid type generated for a message.
type ValidTypeImport struct {
	Desc *protobuf.DescMessage
}

// JSDocBlock prints a JSDoc comment block.
type JSDocBlock struct {
	Text        string
	Indentation string
}
