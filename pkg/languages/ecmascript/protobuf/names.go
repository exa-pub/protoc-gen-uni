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

import "strings"

// Ported from packages/protobuf/src/reflect/names.ts.

// QualifiedName returns a fully-qualified name for a descriptor. For a file descriptor, it returns
// the original file path.
func QualifiedName(desc Desc) string {
	switch d := desc.(type) {
	case *DescField:
		if d.IsExtension {
			return d.TypeName
		}
		return d.Parent.TypeName + "." + d.Name
	case *DescOneof:
		return d.Parent.TypeName + "." + d.Name
	case *DescMethod:
		return d.Parent.TypeName + "." + d.Name
	case *DescEnumValue:
		p := d.Parent.File.Proto.GetPackage()
		if d.Parent.Parent != nil {
			p = d.Parent.Parent.TypeName
		}
		if p != "" {
			return p + "." + d.Name
		}
		return d.Name
	case *DescService:
		return d.TypeName
	case *DescMessage:
		return d.TypeName
	case *DescEnum:
		return d.TypeName
	case *DescFile:
		return d.Proto.GetName()
	}
	return ""
}

// ProtoCamelCase converts snake_case to protoCamelCase according to the convention used by protoc
// to convert a field name to a JSON name.
func ProtoCamelCase(snakeCase string) string {
	capNext := false
	var b strings.Builder
	for _, c := range snakeCase {
		switch {
		case c == '_':
			capNext = true
		case c >= '0' && c <= '9':
			b.WriteRune(c)
			capNext = false
		default:
			if capNext {
				capNext = false
				b.WriteString(strings.ToUpper(string(c)))
			} else {
				b.WriteRune(c)
			}
		}
	}
	return b.String()
}

// ProtoSnakeCase converts protoCamelCase to snake_case.
func ProtoSnakeCase(lowerCamelCase string) string {
	var b strings.Builder
	for _, c := range lowerCamelCase {
		if c >= 'A' && c <= 'Z' {
			b.WriteByte('_')
			b.WriteRune(c + ('a' - 'A'))
		} else {
			b.WriteRune(c)
		}
	}
	return b.String()
}

// reservedObjectProperties are names that cannot be used for object properties because they are
// reserved by built-in JavaScript properties.
var reservedObjectProperties = map[string]bool{
	"constructor": true,
	"toString":    true,
	"toJSON":      true,
	"valueOf":     true,
}

// SafeObjectProperty escapes names that are reserved for ECMAScript built-in object properties.
func SafeObjectProperty(name string) string {
	if reservedObjectProperties[name] {
		return name + "$"
	}
	return name
}
