// Command protoc-gen-dump is a helper for scripts/gen-testdata.sh: it writes the
// CodeGeneratorRequest it receives to <out>/request.binpb (with the parameter cleared, so the
// request can be shared by all option groups) and the parameter to <out>/parameter.txt.
// Golden tests replay exactly the request that buf sent to the upstream plugin.
package main

import (
	"fmt"
	"io"
	"os"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/pluginpb"
)

func main() {
	if err := run(os.Stdin, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "protoc-gen-dump:", err)
		os.Exit(1)
	}
}

func run(r io.Reader, w io.Writer) error {
	in, err := io.ReadAll(r)
	if err != nil {
		return err
	}
	req := &pluginpb.CodeGeneratorRequest{}
	if err := proto.Unmarshal(in, req); err != nil {
		return fmt.Errorf("parse request: %w", err)
	}
	parameter := req.GetParameter()
	req.Parameter = nil
	shared, err := proto.MarshalOptions{Deterministic: true}.Marshal(req)
	if err != nil {
		return err
	}
	resp := &pluginpb.CodeGeneratorResponse{
		SupportedFeatures: proto.Uint64(uint64(pluginpb.CodeGeneratorResponse_FEATURE_PROTO3_OPTIONAL |
			pluginpb.CodeGeneratorResponse_FEATURE_SUPPORTS_EDITIONS)),
		MinimumEdition: proto.Int32(int32(descriptorpb.Edition_EDITION_PROTO2)),
		MaximumEdition: proto.Int32(int32(descriptorpb.Edition_EDITION_MAX)),
		File: []*pluginpb.CodeGeneratorResponse_File{
			{Name: proto.String("request.binpb"), Content: proto.String(string(shared))},
			{Name: proto.String("parameter.txt"), Content: proto.String(parameter)},
		},
	}
	out, err := proto.Marshal(resp)
	if err != nil {
		return err
	}
	_, err = w.Write(out)
	return err
}
