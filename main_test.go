package main

import (
	"bytes"
	"strings"
	"testing"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"
)

func TestVersion(t *testing.T) {
	var out bytes.Buffer
	if err := run([]string{"--version"}, nil, &out); err != nil {
		t.Fatal(err)
	}
	if got := out.String(); got != version+"\n" {
		t.Errorf("--version = %q, want %q", got, version+"\n")
	}
}

func TestUnknownGenerator(t *testing.T) {
	in, err := proto.Marshal(&pluginpb.CodeGeneratorRequest{Parameter: proto.String("uni-name=no/such,uni-version=v1.0.0")})
	if err != nil {
		t.Fatal(err)
	}
	var out bytes.Buffer
	if err := run(nil, bytes.NewReader(in), &out); err != nil {
		t.Fatal(err)
	}
	resp := &pluginpb.CodeGeneratorResponse{}
	if err := proto.Unmarshal(out.Bytes(), resp); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(resp.GetError(), `unknown generator "no/such"`) {
		t.Errorf("error = %q", resp.GetError())
	}
}

func TestBadInput(t *testing.T) {
	if err := run(nil, strings.NewReader("\xff\xff"), &bytes.Buffer{}); err == nil {
		t.Error("want error for malformed request")
	}
	if err := run([]string{"foo"}, nil, &bytes.Buffer{}); err == nil {
		t.Error("want error for unexpected arguments")
	}
}
