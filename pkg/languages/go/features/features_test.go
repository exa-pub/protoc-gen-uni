package features

import (
	"testing"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/gofeaturespb"
)

func TestForEdition(t *testing.T) {
	const keep = int(gofeaturespb.GoFeatures_STRIP_ENUM_PREFIX_KEEP)
	tests := []struct {
		edition descriptorpb.Edition
		want    Go
	}{
		{descriptorpb.Edition_EDITION_PROTO2, Go{LegacyUnmarshalJSONEnum: true, StripEnumPrefix: keep}},
		{descriptorpb.Edition_EDITION_PROTO3, Go{StripEnumPrefix: keep}},
		{descriptorpb.Edition_EDITION_2023, Go{StripEnumPrefix: keep}},
		{descriptorpb.Edition_EDITION_2024, Go{APILevel: int(gofeaturespb.GoFeatures_API_OPAQUE), StripEnumPrefix: keep}},
	}
	for _, tt := range tests {
		got, err := ForEdition(tt.edition)
		if err != nil {
			t.Fatalf("ForEdition(%v): %v", tt.edition, err)
		}
		if got != tt.want {
			t.Errorf("ForEdition(%v) = %+v, want %+v", tt.edition, got, tt.want)
		}
	}
	if _, err := ForEdition(descriptorpb.Edition_EDITION_99999_TEST_ONLY); err == nil {
		t.Error("ForEdition(EDITION_99999_TEST_ONLY): want error")
	}
}

func goFeatures(gf *gofeaturespb.GoFeatures) *descriptorpb.FeatureSet {
	fs := &descriptorpb.FeatureSet{}
	proto.SetExtension(fs, gofeaturespb.E_Go, gf)
	return fs
}

func TestResolve(t *testing.T) {
	fdp := &descriptorpb.FileDescriptorProto{
		Name:    proto.String("test.proto"),
		Package: proto.String("test"),
		Syntax:  proto.String("editions"),
		Edition: descriptorpb.Edition_EDITION_2023.Enum(),
		Options: &descriptorpb.FileOptions{Features: goFeatures(&gofeaturespb.GoFeatures{
			LegacyUnmarshalJsonEnum: proto.Bool(true),
		})},
		MessageType: []*descriptorpb.DescriptorProto{{
			Name: proto.String("M"),
			Options: &descriptorpb.MessageOptions{Features: goFeatures(&gofeaturespb.GoFeatures{
				ApiLevel: gofeaturespb.GoFeatures_API_HYBRID.Enum(),
			})},
			NestedType: []*descriptorpb.DescriptorProto{{Name: proto.String("N")}},
			EnumType: []*descriptorpb.EnumDescriptorProto{{
				Name:  proto.String("E"),
				Value: []*descriptorpb.EnumValueDescriptorProto{{Name: proto.String("E_ZERO"), Number: proto.Int32(0)}},
				Options: &descriptorpb.EnumOptions{Features: goFeatures(&gofeaturespb.GoFeatures{
					LegacyUnmarshalJsonEnum: proto.Bool(false),
				})},
			}},
		}},
		EnumType: []*descriptorpb.EnumDescriptorProto{{
			Name:  proto.String("E2"),
			Value: []*descriptorpb.EnumValueDescriptorProto{{Name: proto.String("E2_ZERO"), Number: proto.Int32(0)}},
		}},
	}
	fd, err := protodesc.NewFile(fdp, nil)
	if err != nil {
		t.Fatal(err)
	}
	if got := Edition(fd); got != descriptorpb.Edition_EDITION_2023 {
		t.Errorf("Edition = %v", got)
	}
	m := fd.Messages().Get(0)
	const keep = int(gofeaturespb.GoFeatures_STRIP_ENUM_PREFIX_KEEP)
	hybrid := int(gofeaturespb.GoFeatures_API_HYBRID)
	tests := []struct {
		name string
		got  func() (Go, error)
		want Go
	}{
		{"file", func() (Go, error) { return Resolve(fd) }, Go{LegacyUnmarshalJSONEnum: true, StripEnumPrefix: keep}},
		{"M", func() (Go, error) { return Resolve(m) }, Go{LegacyUnmarshalJSONEnum: true, APILevel: hybrid, StripEnumPrefix: keep}},
		{"M.N", func() (Go, error) { return Resolve(m.Messages().Get(0)) }, Go{LegacyUnmarshalJSONEnum: true, APILevel: hybrid, StripEnumPrefix: keep}},
		{"M.E", func() (Go, error) { return Resolve(m.Enums().Get(0)) }, Go{APILevel: hybrid, StripEnumPrefix: keep}},
		{"E2", func() (Go, error) { return Resolve(fd.Enums().Get(0)) }, Go{LegacyUnmarshalJSONEnum: true, StripEnumPrefix: keep}},
	}
	for _, tt := range tests {
		got, err := tt.got()
		if err != nil {
			t.Fatalf("%s: %v", tt.name, err)
		}
		if got != tt.want {
			t.Errorf("%s: got %+v, want %+v", tt.name, got, tt.want)
		}
	}
}
