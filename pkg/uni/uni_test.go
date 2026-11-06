package uni

import (
	"strings"
	"testing"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"
)

func TestSplitParameter(t *testing.T) {
	tests := []struct {
		in                  string
		name, version, rest string
		wantErr             bool
	}{
		{in: ""},
		{in: "uni-name=a/b,uni-version=v1.0.0", name: "a/b", version: "v1.0.0"},
		{in: "target=ts,uni-name=a/b,import_extension=js,uni-version=v1.0.0", name: "a/b", version: "v1.0.0", rest: "target=ts,import_extension=js"},
		{in: " uni-name = a/b ,x, y=1", name: "a/b", rest: "x, y=1"},
		{in: "Mfoo.proto=example.com/foo,paths=source_relative", rest: "Mfoo.proto=example.com/foo,paths=source_relative"},
		{in: "uni-name=a,uni-name=b", wantErr: true},
		{in: "uni-version=a,uni-version=b", wantErr: true},
	}
	for _, tt := range tests {
		name, version, rest, err := SplitParameter(tt.in)
		if (err != nil) != tt.wantErr {
			t.Errorf("SplitParameter(%q) error = %v, wantErr %v", tt.in, err, tt.wantErr)
			continue
		}
		if name != tt.name || version != tt.version || rest != tt.rest {
			t.Errorf("SplitParameter(%q) = %q, %q, %q; want %q, %q, %q", tt.in, name, version, rest, tt.name, tt.version, tt.rest)
		}
	}
}

func TestRun(t *testing.T) {
	var got *string
	gens := []Generator{
		{Name: "a/b", Version: "v1.0.0", Generate: func(req *pluginpb.CodeGeneratorRequest) *pluginpb.CodeGeneratorResponse {
			got = req.Parameter
			return &pluginpb.CodeGeneratorResponse{}
		}},
		{Name: "a/b", Version: "v1.1.0"},
		{Name: "c/d", Version: "v1.0.0"},
	}
	run := func(param string) *pluginpb.CodeGeneratorResponse {
		got = nil
		return Run(&pluginpb.CodeGeneratorRequest{Parameter: proto.String(param)}, gens)
	}

	if resp := run("x=1,uni-name=a/b,uni-version=v1.0.0,y"); resp.Error != nil || got == nil || *got != "x=1,y" {
		t.Errorf("parameter passed = %v, error = %v; want %q", got, resp.Error, "x=1,y")
	}
	if resp := run("uni-name=a/b,uni-version=v1.0.0"); resp.Error != nil || got != nil {
		t.Errorf("parameter passed = %v, error = %v; want nil", got, resp.Error)
	}

	errs := map[string]string{
		"":                                "are required",
		"uni-name=a/b":                    "are required",
		"uni-name=x/y,uni-version=v1.0.0": `unknown generator "x/y", available: a/b, c/d`,
		"uni-name=a/b,uni-version=v9.9.9": `unknown version "v9.9.9" of "a/b", available: v1.0.0, v1.1.0`,
		"uni-name=a/b,uni-name=a/b,uni-version=v1.0.0": "duplicate",
	}
	for param, want := range errs {
		if resp := run(param); !strings.Contains(resp.GetError(), want) {
			t.Errorf("Run(%q) error = %q, want it to contain %q", param, resp.GetError(), want)
		}
	}
}
