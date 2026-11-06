package protobuf

// Unknown fields as protobuf-es keeps them (UnknownField in
// packages/protobuf/src/wire/binary-encoding.ts of protobuf-es v2.17.0), and the conversion of
// extensions parsed by the Go runtime back to unknown fields.

import (
	"slices"

	"google.golang.org/protobuf/encoding/protowire"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
)

// UnknownField is a field unknown to the message schema, as protobuf-es stores them in $unknown.
type UnknownField struct {
	Number protowire.Number
	Type   protowire.Type
	// Data is the raw field value following the tag (including the length prefix of
	// length-delimited fields).
	Data []byte
}

// UnknownFields returns the unknown fields of a message, in wire order.
func UnknownFields(m proto.Message) []UnknownField {
	return parseUnknown(m.ProtoReflect().GetUnknown())
}

func parseUnknown(b []byte) []UnknownField {
	var out []UnknownField
	for len(b) > 0 {
		num, typ, n := protowire.ConsumeTag(b)
		if n < 0 {
			return out
		}
		m := protowire.ConsumeFieldValue(num, typ, b[n:])
		if m < 0 {
			return out
		}
		out = append(out, UnknownField{Number: num, Type: typ, Data: b[n : n+m]})
		b = b[n+m:]
	}
	return out
}

// ExtensionsToUnknown moves all extension fields of m and its sub-messages to the unknown fields.
//
// protobuf-es parses descriptors without an extension registry, so all extensions - custom
// options, features of other languages - are unknown fields that are serialized after the known
// fields, in their original order. The Go runtime parses extensions that are linked into the
// binary (for example pb.go features) and serializes them before the known fields. Converting
// them back to unknown fields makes the serialization independent of the linked extensions.
// Since the original position is lost, extensions are merged into the unknown fields ordered by
// field number, which is the order compilers serialize options in.
func ExtensionsToUnknown(m protoreflect.Message) {
	var exts []protoreflect.FieldDescriptor
	m.Range(func(fd protoreflect.FieldDescriptor, v protoreflect.Value) bool {
		if fd.IsExtension() {
			exts = append(exts, fd)
			return true
		}
		switch {
		case fd.IsMap():
			if fd.MapValue().Message() != nil {
				v.Map().Range(func(_ protoreflect.MapKey, mv protoreflect.Value) bool {
					ExtensionsToUnknown(mv.Message())
					return true
				})
			}
		case fd.IsList():
			if fd.Message() != nil {
				l := v.List()
				for i := range l.Len() {
					ExtensionsToUnknown(l.Get(i).Message())
				}
			}
		case fd.Message() != nil:
			ExtensionsToUnknown(v.Message())
		}
		return true
	})
	if len(exts) == 0 {
		return
	}
	slices.SortFunc(exts, func(a, b protoreflect.FieldDescriptor) int { return int(a.Number() - b.Number()) })
	type chunk struct {
		num  protowire.Number
		data []byte
	}
	var extChunks []chunk
	for _, fd := range exts {
		tmp := m.New()
		tmp.Set(fd, m.Get(fd))
		b, err := proto.MarshalOptions{Deterministic: true}.Marshal(tmp.Interface())
		if err != nil {
			continue
		}
		extChunks = append(extChunks, chunk{fd.Number(), b})
		m.Clear(fd)
	}
	var merged []byte
	unknown := m.GetUnknown()
	j := 0
	for len(unknown) > 0 {
		num, typ, n := protowire.ConsumeTag(unknown)
		if n < 0 {
			break
		}
		for j < len(extChunks) && extChunks[j].num < num {
			merged = append(merged, extChunks[j].data...)
			j++
		}
		size := protowire.ConsumeFieldValue(num, typ, unknown[n:])
		if size < 0 {
			break
		}
		merged = append(merged, unknown[:n+size]...)
		unknown = unknown[n+size:]
	}
	merged = append(merged, unknown...)
	for ; j < len(extChunks); j++ {
		merged = append(merged, extChunks[j].data...)
	}
	m.SetUnknown(merged)
}
