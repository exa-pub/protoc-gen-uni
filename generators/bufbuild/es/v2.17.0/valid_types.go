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
	"google.golang.org/protobuf/encoding/protowire"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoc-gen-es/src/valid-types.ts and the reduced protovalidate schema in
// packages/protoc-gen-es/proto/minimal-validate.proto. The extension buf.validate.field is read
// from the unknown fields of google.protobuf.FieldOptions, as protobuf-es does.

const (
	extFieldNumber = 1159 // extend google.protobuf.FieldOptions { optional FieldRules field = 1159; }

	ignoreAlways = 3 // buf.validate.Ignore.IGNORE_ALWAYS
)

// fieldRules is buf.validate.FieldRules of minimal-validate.proto.
type fieldRules struct {
	required bool
	ignore   int32
	// typeCase is the case of the oneof type: "", "repeated" or "map".
	typeCase string
	// items of RepeatedRules, or values of MapRules.
	child *fieldRules
}

// merge parses the serialized FieldRules b into r.
func (r *fieldRules) merge(b []byte) {
	for len(b) > 0 {
		num, typ, n := protowire.ConsumeTag(b)
		if n < 0 {
			return
		}
		b = b[n:]
		switch {
		case num == 25 && typ == protowire.VarintType:
			v, n := protowire.ConsumeVarint(b)
			if n < 0 {
				return
			}
			r.required = v != 0
			b = b[n:]
			continue
		case num == 27 && typ == protowire.VarintType:
			v, n := protowire.ConsumeVarint(b)
			if n < 0 {
				return
			}
			switch int32(v) {
			case 0, 1, 3: // closed enum: other values are unknown fields
				r.ignore = int32(v)
			}
			b = b[n:]
			continue
		case (num == 18 || num == 19) && typ == protowire.BytesType:
			v, n := protowire.ConsumeBytes(b)
			if n < 0 {
				return
			}
			c := "repeated"
			childNumber := protowire.Number(4) // RepeatedRules.items
			if num == 19 {
				c = "map"
				childNumber = 5 // MapRules.values
			}
			if r.typeCase != c {
				r.typeCase = c
				r.child = nil
			}
			r.mergeChild(v, childNumber)
			b = b[n:]
			continue
		}
		n = protowire.ConsumeFieldValue(num, typ, b)
		if n < 0 {
			return
		}
		b = b[n:]
	}
}

// mergeChild parses a RepeatedRules or MapRules message, whose only field of interest is the
// FieldRules with the given number.
func (r *fieldRules) mergeChild(b []byte, childNumber protowire.Number) {
	for len(b) > 0 {
		num, typ, n := protowire.ConsumeTag(b)
		if n < 0 {
			return
		}
		b = b[n:]
		if num == childNumber && typ == protowire.BytesType {
			v, n := protowire.ConsumeBytes(b)
			if n < 0 {
				return
			}
			if r.child == nil {
				r.child = &fieldRules{}
			}
			r.child.merge(v)
			b = b[n:]
			continue
		}
		n = protowire.ConsumeFieldValue(num, typ, b)
		if n < 0 {
			return
		}
		b = b[n:]
	}
}

// getFieldRules returns the option buf.validate.field of the field, like getOption().
func getFieldRules(field *protobuf.DescField) *fieldRules {
	r := &fieldRules{}
	if field.Proto.GetOptions() == nil {
		return r
	}
	for _, uf := range protobuf.UnknownFields(field.Proto.GetOptions()) {
		if uf.Number != extFieldNumber || uf.Type != protowire.BytesType {
			continue
		}
		v, n := protowire.ConsumeBytes(uf.Data)
		if n < 0 {
			continue
		}
		r.merge(v)
	}
	return r
}

// hasFieldRules reports whether the field has the option buf.validate.field, like hasOption().
func hasFieldRules(field *protobuf.DescField) bool {
	if field.Proto.GetOptions() == nil {
		return false
	}
	for _, uf := range protobuf.UnknownFields(field.Proto.GetOptions()) {
		if uf.Number == extFieldNumber {
			return true
		}
	}
	return false
}

// messageNeedsCustomValidType returns true if the given message needs a ValidType. A message
// needs a ValidType if one or more of the following conditions are true:
//   - A proto2 field has the `required` label
//   - A edition field has the feature `field_presence = LEGACY_REQUIRED`
//   - A field has the protovalidate `required` rule and not `ignore = IGNORE_ALWAYS`
//   - A message field (repeated, singular, or map value) needs a ValidType
func messageNeedsCustomValidType(message *protobuf.DescMessage, opts validTypesOptions) bool {
	var usesProtovalidateRequired func(m *protobuf.DescMessage, seen map[string]bool) bool
	usesProtovalidateRequired = func(m *protobuf.DescMessage, seen map[string]bool) bool {
		seen[m.TypeName] = true
		for _, field := range m.Fields {
			if isProtovalidateDisabled(field) {
				continue
			}
			if isProtovalidateRequired(field) {
				return true
			}
			if field.Message != nil && !seen[field.Message.TypeName] {
				if usesProtovalidateRequired(field.Message, seen) {
					return true
				}
			}
		}
		return false
	}
	var usesLegacyRequired func(m *protobuf.DescMessage, seen map[string]bool) bool
	usesLegacyRequired = func(m *protobuf.DescMessage, seen map[string]bool) bool {
		seen[m.TypeName] = true
		for _, field := range m.Fields {
			if isLegacyRequired(field) {
				return true
			}
			if field.Message != nil && !seen[field.Message.TypeName] {
				if usesLegacyRequired(field.Message, seen) {
					return true
				}
			}
		}
		return false
	}
	return (opts.protovalidateRequired && usesProtovalidateRequired(message, map[string]bool{})) ||
		(opts.legacyRequired && usesLegacyRequired(message, map[string]bool{}))
}

// isProtovalidateDisabled returns true if the field's protovalidate rules are (conditionally)
// disabled. It only applies to message fields (singular, repeated, map).
func isProtovalidateDisabled(field *protobuf.DescField) bool {
	if field.Message == nil {
		return false
	}
	rules := getFieldRules(field)
	if rules.ignore == ignoreAlways {
		return true
	}
	var child *fieldRules
	switch {
	case field.FieldKind == protobuf.FieldKindList && rules.typeCase == "repeated":
		child = rules.child
	case field.FieldKind == protobuf.FieldKindMap && rules.typeCase == "map":
		child = rules.child
	}
	if child != nil {
		return child.ignore == ignoreAlways
	}
	return false
}

// isProtovalidateRequired returns true if the field is required by protovalidate.
func isProtovalidateRequired(field *protobuf.DescField) bool {
	if !hasFieldRules(field) {
		return false
	}
	rules := getFieldRules(field)
	if rules.ignore == ignoreAlways {
		return false
	}
	return rules.required
}

// isLegacyRequired returns true if the field has the proto2 `required` label, or the Edition
// feature field_presence = LEGACY_REQUIRED. It only applies to singular message fields.
func isLegacyRequired(field *protobuf.DescField) bool {
	return field.FieldKind == protobuf.FieldKindMessage && field.Presence == protobuf.PresenceLegacyRequired
}
