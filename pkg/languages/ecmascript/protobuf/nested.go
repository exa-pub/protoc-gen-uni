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

// Ported from packages/protobuf/src/reflect/nested-types.ts.

// NestedTypes returns all types of a file or message - enumerations, extensions, services,
// messages - including enumerations, extensions and messages nested in messages.
func NestedTypes(desc Desc) []Desc {
	var out []Desc
	appendNestedTypes(&out, desc)
	return out
}

func appendNestedTypes(out *[]Desc, desc Desc) {
	switch d := desc.(type) {
	case *DescFile:
		for _, m := range d.Messages {
			*out = append(*out, m)
			appendNestedTypes(out, m)
		}
		for _, e := range d.Enums {
			*out = append(*out, e)
		}
		for _, s := range d.Services {
			*out = append(*out, s)
		}
		for _, x := range d.Extensions {
			*out = append(*out, x)
		}
	case *DescMessage:
		for _, m := range d.NestedMessages {
			*out = append(*out, m)
			appendNestedTypes(out, m)
		}
		for _, e := range d.NestedEnums {
			*out = append(*out, e)
		}
		for _, x := range d.NestedExtensions {
			*out = append(*out, x)
		}
	}
}

// ParentTypes returns the ancestors of a given Protobuf element, up to the file.
func ParentTypes(desc Desc) []Desc {
	var parents []Desc
	for {
		p := parentOf(desc)
		if p == nil {
			return parents
		}
		parents = append(parents, p)
		desc = p
	}
}

func parentOf(desc Desc) Desc {
	switch d := desc.(type) {
	case *DescEnumValue:
		return d.Parent
	case *DescField:
		if d.IsExtension {
			if d.Parent != nil {
				return d.Parent
			}
			return d.File
		}
		return d.Parent
	case *DescOneof:
		return d.Parent
	case *DescMethod:
		return d.Parent
	case *DescService:
		return d.File
	case *DescEnum:
		if d.Parent != nil {
			return d.Parent
		}
		return d.File
	case *DescMessage:
		if d.Parent != nil {
			return d.Parent
		}
		return d.File
	}
	return nil
}

// IsDeprecated reports whether the descriptor itself is marked deprecated.
func IsDeprecated(desc Desc) bool {
	switch d := desc.(type) {
	case *DescFile:
		return d.Deprecated
	case *DescMessage:
		return d.Deprecated
	case *DescEnum:
		return d.Deprecated
	case *DescEnumValue:
		return d.Deprecated
	case *DescField:
		return d.Deprecated
	case *DescService:
		return d.Deprecated
	case *DescMethod:
		return d.Deprecated
	}
	return false // oneofs are never deprecated
}
