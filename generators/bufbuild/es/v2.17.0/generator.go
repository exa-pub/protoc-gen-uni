// Package es is a port of protoc-gen-es v2.17.0 (buf.build/bufbuild/es), the TypeScript and
// JavaScript code generator of Protobuf-ES (https://github.com/bufbuild/protobuf-es).
//
// The plugin is ported from packages/protoc-gen-es/src; the plugin framework and the runtime
// parts it needs live in pkg/languages/ecmascript.
package es

import (
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/pluginpb"

	"github.com/exa-pub/protoc-gen-uni/pkg/uni"
)

// Generator is bufbuild/es v2.17.0.
var Generator = uni.Generator{
	Name:    "bufbuild/es",
	Version: "v" + version,
	Generate: func(req *pluginpb.CodeGeneratorRequest) *pluginpb.CodeGeneratorResponse {
		resp, err := protocGenEs.Run(req)
		if err != nil {
			// Upstream writes the error to stderr, prefixed with the plugin name.
			return &pluginpb.CodeGeneratorResponse{Error: proto.String(protocGenEs.Name() + ": " + err.Error())}
		}
		return resp
	},
}
