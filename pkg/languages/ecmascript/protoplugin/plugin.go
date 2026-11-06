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

package protoplugin

import (
	"errors"
	"fmt"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/pluginpb"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoplugin/src/create-es-plugin.ts and plugin.ts.

// PluginInit configures an ECMAScript plugin.
type PluginInit[T any] struct {
	// Name of this code generator plugin.
	Name string
	// Version of this code generator plugin.
	Version string
	// ParseOptions parses the plugin's own options. It must return an error for unrecognized
	// options.
	ParseOptions func(rawOptions []RawOption) (T, error)
	// MinimumEdition is the earliest edition supported by this plugin. Defaults to the minimum
	// edition supported by @bufbuild/protobuf.
	MinimumEdition descriptorpb.Edition
	// MaximumEdition is the latest edition supported by this plugin. Defaults to the maximum
	// edition supported by @bufbuild/protobuf.
	MaximumEdition descriptorpb.Edition
	// GenerateTs generates TypeScript files.
	GenerateTs func(schema *Schema[T], target Target)
	// GenerateJs generates JavaScript files.
	GenerateJs func(schema *Schema[T], target Target)
	// GenerateDts generates TypeScript declaration files.
	GenerateDts func(schema *Schema[T], target Target)
}

// Plugin is an ECMAScript code generator plugin.
type Plugin[T any] struct {
	init PluginInit[T]
}

// CreateEcmaScriptPlugin creates a new code generator plugin for ECMAScript.
func CreateEcmaScriptPlugin[T any](init PluginInit[T]) *Plugin[T] {
	return &Plugin[T]{init: init}
}

// Name returns the name of the plugin.
func (p *Plugin[T]) Name() string { return p.init.Name }

// Version returns the version of the plugin.
func (p *Plugin[T]) Version() string { return p.init.Version }

// Run runs the plugin for the given request. Errors that upstream throws are returned; generators
// may panic with an error, which is recovered and returned as well.
func (p *Plugin[T]) Run(req *pluginpb.CodeGeneratorRequest) (resp *pluginpb.CodeGeneratorResponse, err error) {
	defer func() {
		if r := recover(); r != nil {
			e, ok := r.(error)
			if !ok {
				e = fmt.Errorf("%v", r)
			}
			resp, err = nil, e
		}
	}()
	init := p.init
	minimumEdition := init.MinimumEdition
	if minimumEdition == 0 {
		minimumEdition = protobuf.MinimumEdition
	}
	maximumEdition := init.MaximumEdition
	if maximumEdition == 0 {
		maximumEdition = protobuf.MaximumEdition
	}
	parameter, err := ParseParameter(req.GetParameter(), init.ParseOptions)
	if err != nil {
		return nil, err
	}
	var version *string
	if !parameter.Parsed.ElidePluginVersion {
		version = &init.Version
	}
	schema, err := createSchema(req, parameter, init.Name, version, minimumEdition, maximumEdition)
	if err != nil {
		return nil, err
	}
	targetTs := containsTarget(schema.Targets, TargetTs)
	targetJs := containsTarget(schema.Targets, TargetJs)
	targetDts := containsTarget(schema.Targets, TargetDts)

	var tsFiles []FileInfo
	if targetTs || (targetJs && init.GenerateJs == nil) || (targetDts && init.GenerateDts == nil) {
		schema.prepareGenerate(TargetTs)
		init.GenerateTs(schema, TargetTs)
		tsFiles = schema.fileInfo()
	}
	if targetJs {
		if init.GenerateJs == nil {
			return nil, errors.New("transpiling TypeScript to JavaScript is not supported")
		}
		schema.prepareGenerate(TargetJs)
		init.GenerateJs(schema, TargetJs)
	}
	if targetDts {
		if init.GenerateDts == nil {
			return nil, errors.New("transpiling TypeScript to declarations is not supported")
		}
		schema.prepareGenerate(TargetDts)
		init.GenerateDts(schema, TargetDts)
	}
	files := schema.fileInfo()
	if !targetTs && len(tsFiles) > 0 {
		kept := files[:0]
		for _, f := range files {
			isTs := false
			for _, t := range tsFiles {
				if t.Name == f.Name {
					isTs = true
				}
			}
			if !isTs {
				kept = append(kept, f)
			}
		}
		files = kept
	}
	return toResponse(files, minimumEdition, maximumEdition), nil
}

func toResponse(files []FileInfo, minimumEdition, maximumEdition descriptorpb.Edition) *pluginpb.CodeGeneratorResponse {
	resp := &pluginpb.CodeGeneratorResponse{
		SupportedFeatures: proto.Uint64(uint64(pluginpb.CodeGeneratorResponse_FEATURE_PROTO3_OPTIONAL | pluginpb.CodeGeneratorResponse_FEATURE_SUPPORTS_EDITIONS)),
		MinimumEdition:    proto.Int32(int32(minimumEdition)),
		MaximumEdition:    proto.Int32(int32(maximumEdition)),
	}
	for _, f := range files {
		content := f.Content
		if f.Preamble != nil {
			content = *f.Preamble + "\n" + content
		}
		resp.File = append(resp.File, &pluginpb.CodeGeneratorResponse_File{
			Name:    proto.String(f.Name),
			Content: proto.String(content),
		})
	}
	return resp
}
