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

// Ported from packages/protoplugin/src/safe-identifier.ts.

// reservedIdentifiers are names that cannot be used for identifiers, such as class names, but
// can be used for object properties.
var reservedIdentifiers = map[string]bool{
	// ECMAScript 2015 keywords
	"break": true, "case": true, "catch": true, "class": true, "const": true, "continue": true,
	"debugger": true, "default": true, "delete": true, "do": true, "else": true, "export": true,
	"extends": true, "false": true, "finally": true, "for": true, "function": true, "if": true,
	"import": true, "in": true, "instanceof": true, "new": true, "null": true, "return": true,
	"super": true, "switch": true, "this": true, "throw": true, "true": true, "try": true,
	"typeof": true, "var": true, "void": true, "while": true, "with": true, "yield": true,

	// ECMAScript 2015 future reserved keywords
	"enum": true, "implements": true, "interface": true, "let": true, "package": true,
	"private": true, "protected": true, "public": true, "static": true,

	// Class name cannot be 'Object' when targeting ES5 with module CommonJS
	"Object": true,

	// TypeScript keywords that cannot be used for types (as opposed to variables)
	"bigint": true, "number": true, "boolean": true, "string": true, "object": true,

	// Identifiers reserved for the runtime, so we can generate legible code
	"globalThis": true, "Uint8Array": true, "Partial": true,
}

// SafeIdentifier escapes reserved words in ECMAScript and TypeScript identifiers, by appending a
// dollar sign.
func SafeIdentifier(name string) string {
	if reservedIdentifiers[name] {
		return name + "$"
	}
	return name
}
