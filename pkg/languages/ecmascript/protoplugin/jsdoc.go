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
	"strings"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoplugin/src/jsdoc.ts.

// CreateJSDocTextFromDesc creates the text of a JSDoc comment for a descriptor: the comments from
// the protobuf source, and annotations such as `@generated from message MyMessage`.
func CreateJSDocTextFromDesc(desc protobuf.Desc) string {
	comments := GetComments(desc)
	text := ""
	if comments.Leading != nil {
		text += *comments.Leading
		text = strings.TrimSuffix(text, "\n")
	}
	if comments.Trailing != nil {
		if text != "" {
			text += "\n\n"
		}
		text += *comments.Trailing
		text = strings.TrimSuffix(text, "\n")
	}
	if text != "" {
		text += "\n\n"
	}
	lines := strings.Split(text, "\n")
	for i, line := range lines {
		lines[i] = strings.TrimPrefix(line, " ")
	}
	text = strings.Join(lines, "\n")
	switch desc.Kind() {
	case "enum_value":
		text += "@generated from enum value: " + GetDeclarationString(desc) + ";"
	case "field":
		text += "@generated from field: " + GetDeclarationString(desc) + ";"
	case "extension":
		text += "@generated from extension: " + GetDeclarationString(desc) + ";"
	case "message", "enum":
		text += "@generated from " + desc.String()
		if featureOptions := GetFeatureOptionStrings(desc); len(featureOptions) > 0 {
			word := "option"
			if len(featureOptions) > 1 {
				word = "options"
			}
			text += "\n@generated with " + word + " " + strings.Join(featureOptions, ", ")
		}
	default:
		text += "@generated from " + desc.String()
	}
	var deprecated bool
	switch desc.Kind() {
	case "field", "enum_value", "rpc":
		deprecated = protobuf.IsDeprecated(desc)
	default:
		deprecated = protobuf.IsDeprecated(desc)
		for _, p := range protobuf.ParentTypes(desc) {
			deprecated = deprecated || protobuf.IsDeprecated(p)
		}
	}
	if deprecated {
		text += "\n@deprecated"
	}
	return text
}

// FormatJSDocBlock formats text as a JSDoc comment block.
func FormatJSDocBlock(text, indentation string) string {
	if strings.TrimSpace(text) == "" {
		return ""
	}
	lines := strings.Split(text, "\n")
	var b strings.Builder
	b.WriteString(indentation + "/**\n")
	for _, l := range lines {
		l = strings.ReplaceAll(l, "*/", "*\\/")
		if l != "" {
			l = " " + l
		}
		b.WriteString(indentation + " *" + l + "\n")
	}
	b.WriteString(indentation + " */")
	return b.String()
}
