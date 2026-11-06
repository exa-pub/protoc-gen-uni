package es

import (
	"testing"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/pluginpb"
)

func TestResponse(t *testing.T) {
	file := &descriptorpb.FileDescriptorProto{Name: proto.String("a.proto"), Syntax: proto.String("proto3")}
	run := func(param string) *pluginpb.CodeGeneratorResponse {
		return Generator.Generate(&pluginpb.CodeGeneratorRequest{
			FileToGenerate: []string{"a.proto"},
			ProtoFile:      []*descriptorpb.FileDescriptorProto{file},
			Parameter:      proto.String(param),
		})
	}
	resp := run("target=ts")
	if resp.Error != nil {
		t.Fatal(resp.GetError())
	}
	if resp.GetSupportedFeatures() != 3 || resp.GetMinimumEdition() != 998 || resp.GetMaximumEdition() != 1002 {
		t.Errorf("features = %d, editions = %d..%d", resp.GetSupportedFeatures(), resp.GetMinimumEdition(), resp.GetMaximumEdition())
	}
	if len(resp.File) != 1 || resp.File[0].GetName() != "a_pb.ts" {
		t.Errorf("files = %v", resp.File)
	}

	for param, want := range map[string]string{
		"target=ts,json_types=yes":                 `protoc-gen-es: invalid option "json_types=yes": please provide true or false`,
		"target=ts,valid_types=legacy_required+x":  `protoc-gen-es: invalid option "valid_types=legacy_required+x"`,
		"target=ts,erasable_syntax=1,foo=bar":      `protoc-gen-es: invalid option "erasable_syntax=1,foo=bar"`,
		"target=ts,erasable_syntax=yes,json_types": `protoc-gen-es: invalid option "erasable_syntax=yes,json_types": please provide true or false`,
		"target=swift": `protoc-gen-es: invalid option "target=swift"`,
	} {
		if got := run(param).GetError(); got != want {
			t.Errorf("parameter %q: error = %q, want %q", param, got, want)
		}
	}

	file.Syntax = proto.String("editions")
	file.Edition = descriptorpb.Edition_EDITION_99999_TEST_ONLY.Enum()
	if got, want := run("").GetError(), "protoc-gen-es: a.proto: unsupported edition 99999_TEST_ONLY - the latest supported edition is 2026"; got != want {
		t.Errorf("error = %q, want %q", got, want)
	}
}
