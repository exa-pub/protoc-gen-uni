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

import (
	"encoding/base64"
	"strconv"
	"strings"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
)

// Ported from packages/protobuf/src/codegenv2/embed.ts.

// EmbeddedFile has the information necessary to embed a file descriptor in generated code.
type EmbeddedFile struct {
	file *descriptorpb.FileDescriptorProto
	// Bootable is true for google/protobuf/descriptor.proto, which can also be embedded as a plain
	// object (see Boot).
	Bootable bool
}

// EmbedFileDesc creates the necessary information to embed a file descriptor in generated code.
func EmbedFileDesc(file *descriptorpb.FileDescriptorProto) *EmbeddedFile {
	return &EmbeddedFile{
		file:     file,
		Bootable: file.GetName() == "google/protobuf/descriptor.proto",
	}
}

// Proto returns a copy of the file descriptor without dependencies, source code info and default
// JSON names.
func (e *EmbeddedFile) Proto() *descriptorpb.FileDescriptorProto {
	stripped := proto.CloneOf(e.file)
	ExtensionsToUnknown(stripped.ProtoReflect())
	stripped.Dependency = nil
	stripped.SourceCodeInfo = nil
	for _, m := range stripped.MessageType {
		stripJSONNames(m)
	}
	return stripped
}

// Base64 returns the serialized descriptor, base64 encoded without padding.
func (e *EmbeddedFile) Base64() string {
	b, err := proto.MarshalOptions{Deterministic: true}.Marshal(e.Proto())
	if err != nil {
		panic(err)
	}
	return base64.RawStdEncoding.EncodeToString(b)
}

func stripJSONNames(d *descriptorpb.DescriptorProto) {
	for _, f := range d.Field {
		if f.GetJsonName() == ProtoCamelCase(f.GetName()) {
			f.JsonName = nil
		}
	}
	for _, n := range d.NestedType {
		stripJSONNames(n)
	}
}

// PathInFileDesc computes the path to a message, enumeration, extension, or service in a file
// descriptor.
func PathInFileDesc(desc Desc) []int {
	if s, ok := desc.(*DescService); ok {
		return []int{indexOf(s.File.Services, s)}
	}
	var parent *DescMessage
	switch d := desc.(type) {
	case *DescEnum:
		if d.Parent == nil {
			return []int{indexOf(d.File.Enums, d)}
		}
		parent = d.Parent
	case *DescMessage:
		if d.Parent == nil {
			return []int{indexOf(d.File.Messages, d)}
		}
		parent = d.Parent
	case *DescField:
		if d.Parent == nil {
			return []int{indexOf(d.File.Extensions, d)}
		}
		parent = d.Parent
	}
	var nested []int
	cur := parent
	for p := cur.Parent; p != nil; p = cur.Parent {
		nested = append([]int{indexOf(p.NestedMessages, cur)}, nested...)
		cur = p
	}
	path := append([]int{indexOf(cur.File.Messages, cur)}, nested...)
	switch d := desc.(type) {
	case *DescField:
		return append(path, indexOf(parent.NestedExtensions, d))
	case *DescMessage:
		return append(path, indexOf(parent.NestedMessages, d))
	case *DescEnum:
		return append(path, indexOf(parent.NestedEnums, d))
	}
	return path
}

func indexOf[T comparable](s []T, v T) int {
	for i, x := range s {
		if x == v {
			return i
		}
	}
	return -1
}

// Boot returns google/protobuf/descriptor.proto as a plain object in JSON, which can be hydrated
// with boot() from @bufbuild/protobuf/codegenv2. See createFileDescriptorProtoBoot.
func (e *EmbeddedFile) Boot() string {
	p := e.Proto()
	var b jsonObj
	b.str("name", p.GetName())
	b.str("package", p.GetPackage())
	var msgs []string
	for _, m := range p.MessageType {
		msgs = append(msgs, descriptorBoot(m))
	}
	b.raw("messageType", "["+strings.Join(msgs, ",")+"]")
	var enums []string
	for _, en := range p.EnumType {
		enums = append(enums, enumBoot(en))
	}
	b.raw("enumType", "["+strings.Join(enums, ",")+"]")
	return b.String()
}

func descriptorBoot(m *descriptorpb.DescriptorProto) string {
	var b jsonObj
	b.str("name", m.GetName())
	if len(m.Field) > 0 {
		var fs []string
		for _, f := range m.Field {
			fs = append(fs, fieldBoot(f))
		}
		b.raw("field", "["+strings.Join(fs, ",")+"]")
	}
	if len(m.NestedType) > 0 {
		var ns []string
		for _, n := range m.NestedType {
			ns = append(ns, descriptorBoot(n))
		}
		b.raw("nestedType", "["+strings.Join(ns, ",")+"]")
	}
	if len(m.EnumType) > 0 {
		var es []string
		for _, en := range m.EnumType {
			es = append(es, enumBoot(en))
		}
		b.raw("enumType", "["+strings.Join(es, ",")+"]")
	}
	if len(m.ExtensionRange) > 0 {
		var rs []string
		for _, r := range m.ExtensionRange {
			var o jsonObj
			o.num("start", int64(r.GetStart()))
			o.num("end", int64(r.GetEnd()))
			rs = append(rs, o.String())
		}
		b.raw("extensionRange", "["+strings.Join(rs, ",")+"]")
	}
	return b.String()
}

func fieldBoot(f *descriptorpb.FieldDescriptorProto) string {
	var b jsonObj
	b.str("name", f.GetName())
	b.num("number", int64(f.GetNumber()))
	b.num("type", int64(f.GetType()))
	if f.Label != nil {
		b.num("label", int64(f.GetLabel()))
	}
	if f.TypeName != nil {
		b.str("typeName", f.GetTypeName())
	}
	if f.Extendee != nil {
		b.str("extendee", f.GetExtendee())
	}
	if f.DefaultValue != nil {
		b.str("defaultValue", f.GetDefaultValue())
	}
	if o := f.Options; o != nil {
		var ob jsonObj
		if o.Packed != nil {
			ob.raw("packed", strconv.FormatBool(o.GetPacked()))
		}
		if o.Deprecated != nil {
			ob.raw("deprecated", strconv.FormatBool(o.GetDeprecated()))
		}
		if o.Retention != nil {
			ob.num("retention", int64(o.GetRetention()))
		}
		if len(o.Targets) > 0 {
			var ts []string
			for _, t := range o.Targets {
				ts = append(ts, strconv.Itoa(int(t)))
			}
			ob.raw("targets", "["+strings.Join(ts, ",")+"]")
		}
		if len(o.EditionDefaults) > 0 {
			var ds []string
			for _, d := range o.EditionDefaults {
				var db jsonObj
				db.str("value", d.GetValue())
				db.num("edition", int64(d.GetEdition()))
				ds = append(ds, db.String())
			}
			ob.raw("editionDefaults", "["+strings.Join(ds, ",")+"]")
		}
		b.raw("options", ob.String())
	}
	return b.String()
}

func enumBoot(e *descriptorpb.EnumDescriptorProto) string {
	var b jsonObj
	b.str("name", e.GetName())
	var vs []string
	for _, v := range e.Value {
		var vb jsonObj
		vb.str("name", v.GetName())
		vb.num("number", int64(v.GetNumber()))
		vs = append(vs, vb.String())
	}
	b.raw("value", "["+strings.Join(vs, ",")+"]")
	return b.String()
}

// jsonObj builds a JSON object like JSON.stringify, keeping the insertion order of keys.
type jsonObj struct {
	parts []string
}

func (o *jsonObj) raw(key, value string) {
	o.parts = append(o.parts, JSONString(key)+":"+value)
}

func (o *jsonObj) str(key, value string) { o.raw(key, JSONString(value)) }

func (o *jsonObj) num(key string, value int64) { o.raw(key, strconv.FormatInt(value, 10)) }

func (o *jsonObj) String() string { return "{" + strings.Join(o.parts, ",") + "}" }

// JSONString quotes a string like JSON.stringify.
func JSONString(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for _, r := range s {
		switch r {
		case '"':
			b.WriteString(`\"`)
		case '\\':
			b.WriteString(`\\`)
		case '\b':
			b.WriteString(`\b`)
		case '\f':
			b.WriteString(`\f`)
		case '\n':
			b.WriteString(`\n`)
		case '\r':
			b.WriteString(`\r`)
		case '\t':
			b.WriteString(`\t`)
		default:
			if r < 0x20 {
				b.WriteString(`\u00`)
				b.WriteString(strconv.FormatInt(int64(r)>>4, 16))
				b.WriteString(strconv.FormatInt(int64(r)&0xf, 16))
			} else {
				b.WriteRune(r)
			}
		}
	}
	b.WriteByte('"')
	return b.String()
}
