// Copyright 2018 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package golang is a port of protoc-gen-go v1.36.12 (buf.build/protocolbuffers/go).
//
// Generate is ported from google.golang.org/protobuf v1.36.12
// cmd/protoc-gen-go/main.go; the code generator itself is in ./gengo.
package golang

import (
	"errors"
	"flag"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"

	"github.com/exa-pub/protoc-gen-uni/generators/protocolbuffers/go/v1.36.12/gengo"
	"github.com/exa-pub/protoc-gen-uni/pkg/languages/go/protogen"
	"github.com/exa-pub/protoc-gen-uni/pkg/uni"
)

const grpcDocURL = "https://grpc.io/docs/languages/go/quickstart/#regenerate-grpc-code"

// Generator is protocolbuffers/go v1.36.12.
var Generator = uni.Generator{
	Name:     "protocolbuffers/go",
	Version:  gengo.Version,
	Generate: generate,
}

func generate(req *pluginpb.CodeGeneratorRequest) *pluginpb.CodeGeneratorResponse {
	var (
		flags                                 flag.FlagSet
		plugins                               = flags.String("plugins", "", "deprecated option")
		experimentalStripNonFunctionalCodegen = flags.Bool("experimental_strip_nonfunctional_codegen", false, "experimental_strip_nonfunctional_codegen true means that the plugin will not emit certain parts of the generated code in order to make it possible to compare a proto2/proto3 file with its equivalent (according to proto spec) editions file. Primarily, this is the encoded descriptor.")
	)
	gen, err := protogen.Options{
		ParamFunc:                    flags.Set,
		InternalStripForEditionsDiff: experimentalStripNonFunctionalCodegen,
	}.New(req)
	if err != nil {
		// Upstream prints "protoc-gen-go: <err>" to stderr and exits with 1.
		return &pluginpb.CodeGeneratorResponse{Error: proto.String("protoc-gen-go: " + err.Error())}
	}
	if err := run(gen, *plugins); err != nil {
		gen.Error(err)
	}
	return gen.Response()
}

func run(gen *protogen.Plugin, plugins string) error {
	if plugins != "" {
		//nolint:staticcheck // upstream error text
		return errors.New("protoc-gen-go: plugins are not supported; use 'protoc --go-grpc_out=...' to generate gRPC\n\n" +
			"See " + grpcDocURL + " for more information.")
	}
	for _, f := range gen.Files {
		if f.Generate {
			gengo.GenerateFile(gen, f)
		}
	}
	gen.SupportedFeatures = gengo.SupportedFeatures
	gen.SupportedEditionsMinimum = gengo.SupportedEditionsMinimum
	gen.SupportedEditionsMaximum = gengo.SupportedEditionsMaximum
	return nil
}
