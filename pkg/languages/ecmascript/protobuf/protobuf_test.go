package protobuf

import (
	"bytes"
	"testing"

	"google.golang.org/protobuf/encoding/protowire"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/gofeaturespb"
)

func TestProtoCamelCase(t *testing.T) {
	for in, want := range map[string]string{
		"foo_bar":              "fooBar",
		"_leading_underscore":  "LeadingUnderscore",
		"trailing_underscore_": "trailingUnderscore",
		"double__underscore":   "doubleUnderscore",
		"field_1_number":       "field1Number",
		"field_1a":             "field1a",
		"camelCaseAlready":     "camelCaseAlready",
		"ALLCAPS":              "ALLCAPS",
	} {
		if got := ProtoCamelCase(in); got != want {
			t.Errorf("ProtoCamelCase(%q) = %q, want %q", in, got, want)
		}
	}
	if got := SafeObjectProperty("constructor"); got != "constructor$" {
		t.Errorf("SafeObjectProperty = %q", got)
	}
}

func TestFindEnumSharedPrefix(t *testing.T) {
	values := func(names ...string) []*descriptorpb.EnumValueDescriptorProto {
		var vs []*descriptorpb.EnumValueDescriptorProto
		for _, n := range names {
			vs = append(vs, &descriptorpb.EnumValueDescriptorProto{Name: proto.String(n)})
		}
		return vs
	}
	for _, tt := range []struct {
		enum   string
		values []string
		want   string
	}{
		{"MyEnum", []string{"MY_ENUM_A", "MY_ENUM_B"}, "my_enum_"},
		{"MyEnum", []string{"my_enum_a"}, "my_enum_"},
		{"MyEnum", []string{"MY_ENUM_A", "OTHER"}, ""},
		{"MyEnum", []string{"MY_ENUM_"}, ""},
		{"MyEnum", []string{"MY_ENUM_1"}, ""},
		{"HTTPCode", []string{"H_T_T_P_CODE_OK"}, "h_t_t_p_code_"},
	} {
		if got := findEnumSharedPrefix(tt.enum, values(tt.values...)); got != tt.want {
			t.Errorf("findEnumSharedPrefix(%q, %v) = %q, want %q", tt.enum, tt.values, got, tt.want)
		}
	}
}

func TestFeatureResolution(t *testing.T) {
	features := func(fp descriptorpb.FeatureSet_FieldPresence) *descriptorpb.FeatureSet {
		return &descriptorpb.FeatureSet{FieldPresence: fp.Enum()}
	}
	field := func(name string, n int32, typ descriptorpb.FieldDescriptorProto_Type, opts *descriptorpb.FieldOptions) *descriptorpb.FieldDescriptorProto {
		return &descriptorpb.FieldDescriptorProto{
			Name: proto.String(name), Number: proto.Int32(n), Type: typ.Enum(),
			Label: descriptorpb.FieldDescriptorProto_LABEL_OPTIONAL.Enum(), JsonName: proto.String(ProtoCamelCase(name)), Options: opts,
		}
	}
	file := &descriptorpb.FileDescriptorProto{
		Name:    proto.String("a.proto"),
		Syntax:  proto.String("editions"),
		Edition: descriptorpb.Edition_EDITION_2023.Enum(),
		Options: &descriptorpb.FileOptions{Features: features(descriptorpb.FeatureSet_IMPLICIT)},
		MessageType: []*descriptorpb.DescriptorProto{{
			Name: proto.String("M"),
			Field: []*descriptorpb.FieldDescriptorProto{
				field("a", 1, descriptorpb.FieldDescriptorProto_TYPE_INT32, nil),
				field("b", 2, descriptorpb.FieldDescriptorProto_TYPE_INT32, &descriptorpb.FieldOptions{Features: features(descriptorpb.FeatureSet_LEGACY_REQUIRED)}),
				field("c", 3, descriptorpb.FieldDescriptorProto_TYPE_MESSAGE, nil),
			},
			NestedType: []*descriptorpb.DescriptorProto{{
				Name:    proto.String("N"),
				Options: &descriptorpb.MessageOptions{Features: features(descriptorpb.FeatureSet_EXPLICIT)},
				Field:   []*descriptorpb.FieldDescriptorProto{field("d", 1, descriptorpb.FieldDescriptorProto_TYPE_INT32, nil)},
			}},
		}},
	}
	file.MessageType[0].Field[2].TypeName = proto.String(".M.N")
	reg, err := CreateFileRegistry([]*descriptorpb.FileDescriptorProto{file})
	if err != nil {
		t.Fatal(err)
	}
	m := reg.GetMessage("M")
	got := []int32{m.Fields[0].Presence, m.Fields[1].Presence, m.Fields[2].Presence, m.NestedMessages[0].Fields[0].Presence}
	want := []int32{PresenceImplicit, PresenceLegacyRequired, PresenceExplicit, PresenceExplicit}
	for i := range got {
		if got[i] != want[i] {
			t.Errorf("presence of field %d = %d, want %d", i, got[i], want[i])
		}
	}
	if path := PathInFileDesc(m.NestedMessages[0]); len(path) != 2 || path[0] != 0 || path[1] != 0 {
		t.Errorf("PathInFileDesc = %v", path)
	}

	file.Edition = descriptorpb.Edition(1003).Enum()
	if _, err := CreateFileRegistry([]*descriptorpb.FileDescriptorProto{file}); err == nil || err.Error() != "a.proto: unsupported edition" {
		t.Errorf("unsupported edition: %v", err)
	}
	file.Edition = descriptorpb.Edition_EDITION_2023.Enum()
	file.MessageType[0].Name = proto.String("bad-name")
	if _, err := CreateFileRegistry([]*descriptorpb.FileDescriptorProto{file}); err == nil || err.Error() != `a.proto: invalid message name "bad-name"` {
		t.Errorf("invalid name: %v", err)
	}
}

// TestExtensionsToUnknown checks that extensions known to the Go runtime are serialized like
// protobuf-es serializes unknown fields: after the known fields, ordered by field number.
func TestExtensionsToUnknown(t *testing.T) {
	fs := &descriptorpb.FeatureSet{FieldPresence: descriptorpb.FeatureSet_IMPLICIT.Enum()}
	proto.SetExtension(fs, gofeaturespb.E_Go, &gofeaturespb.GoFeatures{LegacyUnmarshalJsonEnum: proto.Bool(true)})
	var unknown []byte
	unknown = protowire.AppendTag(unknown, 1001, protowire.VarintType)
	unknown = protowire.AppendVarint(unknown, 1)
	unknown = protowire.AppendTag(unknown, 1003, protowire.VarintType)
	unknown = protowire.AppendVarint(unknown, 3)
	fs.ProtoReflect().SetUnknown(unknown)
	file := &descriptorpb.FileDescriptorProto{
		Name:    proto.String("a.proto"),
		Options: &descriptorpb.FileOptions{Features: fs},
	}
	e := EmbedFileDesc(file).Proto()

	var want []byte
	want = protowire.AppendTag(want, 1, protowire.VarintType)
	want = protowire.AppendVarint(want, 2)
	want = protowire.AppendTag(want, 1001, protowire.VarintType)
	want = protowire.AppendVarint(want, 1)
	want = protowire.AppendTag(want, 1002, protowire.BytesType)
	want = protowire.AppendBytes(want, []byte{0x08, 0x01})
	want = protowire.AppendTag(want, 1003, protowire.VarintType)
	want = protowire.AppendVarint(want, 3)
	got, err := proto.MarshalOptions{Deterministic: true}.Marshal(e.GetOptions().GetFeatures())
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Errorf("features = %x, want %x", got, want)
	}
	if !proto.HasExtension(file.GetOptions().GetFeatures(), gofeaturespb.E_Go) {
		t.Error("the original descriptor was modified")
	}
}

func TestBase64AndBoot(t *testing.T) {
	file := &descriptorpb.FileDescriptorProto{
		Name:       proto.String("a.proto"),
		Dependency: []string{"b.proto"},
		MessageType: []*descriptorpb.DescriptorProto{{
			Name: proto.String("M"),
			Field: []*descriptorpb.FieldDescriptorProto{{
				Name: proto.String("foo_bar"), Number: proto.Int32(1), JsonName: proto.String("fooBar"),
				Type: descriptorpb.FieldDescriptorProto_TYPE_STRING.Enum(), Label: descriptorpb.FieldDescriptorProto_LABEL_OPTIONAL.Enum(),
			}},
		}},
		SourceCodeInfo: &descriptorpb.SourceCodeInfo{},
	}
	// name "a.proto", message M with field foo_bar = 1 (json_name stripped), no dependency.
	if got, want := EmbedFileDesc(file).Base64(), "CgdhLnByb3RvIhQKAU0SDwoHZm9vX2JhchgBIAEoCQ"; got != want {
		t.Errorf("Base64 = %q, want %q", got, want)
	}
	if got, want := JSONString("a\"\\\n\x01 <"), `"a\"\\\n\u0001`+" "+`<"`; got != want {
		t.Errorf("JSONString = %q, want %q", got, want)
	}
}
