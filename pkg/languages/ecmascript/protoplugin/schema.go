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
	"slices"
	"strings"

	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/pluginpb"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoplugin/src/schema.ts.

// Schema describes the files and types that the plugin is requested to generate.
type Schema[T any] struct {
	// Files are the files we are asked to generate.
	Files []*protobuf.DescFile
	// AllFiles are all files contained in the code generator request.
	AllFiles []*protobuf.DescFile
	// Targets is the plugin option `target`.
	Targets []Target
	// Options are the standard plugin options.
	Options EcmaScriptPluginOptions
	// PluginOptions are the options parsed by the plugin.
	PluginOptions T
	// Proto is the original google.protobuf.compiler.CodeGeneratorRequest.
	Proto *pluginpb.CodeGeneratorRequest

	target         Target
	generatedFiles []*GeneratedFile
	runtime        *RuntimeImports
	resolve        resolvers
	rewriteImport  func(string) string
	createPreamble func(*protobuf.DescFile) string
}

// TypesInFile lists all types in a file, including messages, enumerations, and extensions
// nested in messages.
func (s *Schema[T]) TypesInFile(file *protobuf.DescFile) []protobuf.Desc {
	return protobuf.NestedTypes(file)
}

// GenerateFile generates a new file with the given name.
func (s *Schema[T]) GenerateFile(name string) *GeneratedFile {
	if s.target == "" {
		panic(errors.New("prepareGenerate() must be called before generateFile()"))
	}
	style := JSImportStyleModule // ts and dts always use import/export, only js may use commonjs
	if s.target == TargetJs {
		style = s.Options.JSImportStyle
	}
	f := &GeneratedFile{
		name:           name,
		importPath:     DeriveImportPath(name),
		jsImportStyle:  style,
		rewriteImport:  s.rewriteImport,
		resolve:        s.resolve,
		createPreamble: s.createPreamble,
		Runtime:        s.runtime,
	}
	s.generatedFiles = append(s.generatedFiles, f)
	return f
}

func (s *Schema[T]) fileInfo() []FileInfo {
	var out []FileInfo
	for _, f := range s.generatedFiles {
		fi := f.FileInfo()
		if s.Options.KeepEmptyFiles || fi.Content != "" {
			out = append(out, fi)
		}
	}
	return out
}

func (s *Schema[T]) prepareGenerate(target Target) {
	s.target = target
}

func createSchema[T any](request *pluginpb.CodeGeneratorRequest, parameter *ParsedParameter[T], pluginName string, pluginVersion *string, minimumEdition, maximumEdition descriptorpb.Edition) (*Schema[T], error) {
	allFiles, filesToGenerate, err := getFilesToGenerate(request, minimumEdition, maximumEdition)
	if err != nil {
		return nil, err
	}
	opts := parameter.Parsed
	mapImports := compileMapImports(opts.MapImports)
	rewriteImports := compileRewriteImports(opts.RewriteImports)
	n := &namer{}
	filePath := func(desc protobuf.Desc) string {
		return generateFilePath(protobuf.DescFileOf(desc), opts.BootstrapWkt, filesToGenerate, mapImports)
	}
	s := &Schema[T]{
		Files:         filesToGenerate,
		AllFiles:      allFiles,
		Targets:       opts.Targets,
		Options:       opts,
		PluginOptions: parameter.Extra,
		Proto:         request,
		runtime:       createRuntimeImports(opts.BootstrapWkt),
		resolve: resolvers{
			descImport: func(desc protobuf.Desc, typeOnly bool) *ImportSymbol {
				return NewImportSymbol(n.generatedDescName(desc), filePath(desc), typeOnly)
			},
			shapeImport: func(desc protobuf.Desc) *ImportSymbol {
				return NewImportSymbol(n.generatedShapeName(desc), filePath(desc), true)
			},
			jsonImport: func(desc protobuf.Desc) *ImportSymbol {
				return NewImportSymbol(n.generatedJSONTypeName(desc), filePath(desc), true)
			},
			validImport: func(desc *protobuf.DescMessage) *ImportSymbol {
				return NewImportSymbol(n.generatedValidTypeName(desc), filePath(desc), true)
			},
		},
		rewriteImport: func(importPath string) string {
			return RewriteImportPath(importPath, rewriteImports, opts.ImportExtension)
		},
		createPreamble: func(file *protobuf.DescFile) string {
			return makeFilePreamble(file, pluginName, pluginVersion, parameter.Sanitized, opts.TsNocheck)
		},
	}
	return s, nil
}

func syntaxEdition(file *descriptorpb.FileDescriptorProto) descriptorpb.Edition {
	switch file.GetSyntax() {
	case "", "proto2":
		return protobuf.EditionProto2
	case "proto3":
		return protobuf.EditionProto3
	case "editions":
		return file.GetEdition()
	default:
		return descriptorpb.Edition_EDITION_UNKNOWN
	}
}

func getFilesToGenerate(request *pluginpb.CodeGeneratorRequest, minimumEdition, maximumEdition descriptorpb.Edition) (allFiles, filesToGenerate []*protobuf.DescFile, err error) {
	if minimumEdition > maximumEdition {
		return nil, nil, fmt.Errorf("configured minimumEdition %s > maximumEdition %s - please contact plugin author",
			editionToString(minimumEdition), editionToString(maximumEdition))
	}
	var missing []string
	for _, name := range request.GetFileToGenerate() {
		if !slices.ContainsFunc(request.GetProtoFile(), func(f *descriptorpb.FileDescriptorProto) bool { return f.GetName() == name }) {
			missing = append(missing, name)
		}
	}
	if len(missing) > 0 {
		return nil, nil, fmt.Errorf("files_to_generate missing in the request: %s", strings.Join(missing, ", "))
	}
	for _, file := range request.GetProtoFile() {
		if !slices.Contains(request.GetFileToGenerate(), file.GetName()) {
			continue
		}
		edition := syntaxEdition(file)
		// EDITION_UNSTABLE is a sandbox for in-development features; accept
		// it regardless of the plugin's min/max bounds.
		if edition == protobuf.EditionUnstable {
			continue
		}
		if edition < minimumEdition {
			return nil, nil, fmt.Errorf("%s: unsupported edition %s - the earliest supported edition is %s",
				file.GetName(), editionToString(edition), editionToString(minimumEdition))
		}
		if edition > maximumEdition {
			return nil, nil, fmt.Errorf("%s: unsupported edition %s - the latest supported edition is %s",
				file.GetName(), editionToString(edition), editionToString(maximumEdition))
		}
	}
	// Our goal is to provide options with source retention to plugin authors.
	// CodeGeneratorRequest.proto_file elides options with source retention for
	// files to generate. For these files, we take the file from source_file_descriptors,
	// which does include options with source retention.
	allProtoWithSourceOptions := make([]*descriptorpb.FileDescriptorProto, 0, len(request.GetProtoFile()))
	for _, protoFile := range request.GetProtoFile() {
		file := protoFile
		for _, s := range request.GetSourceFileDescriptors() {
			if s.GetName() == protoFile.GetName() {
				file = s
				break
			}
		}
		allProtoWithSourceOptions = append(allProtoWithSourceOptions, file)
	}
	registry, err := protobuf.CreateFileRegistry(allProtoWithSourceOptions)
	if err != nil {
		return nil, nil, err
	}
	// The registry only validates the copies it is given, but plugins may print
	// names from either copy. For example, protoc-gen-es embeds proto_file.
	for _, f := range request.GetProtoFile() {
		if err := protobuf.AssertValidNames(f); err != nil {
			return nil, nil, err
		}
	}
	for _, file := range registry.Files() {
		allFiles = append(allFiles, file)
		if slices.Contains(request.GetFileToGenerate(), file.Proto.GetName()) {
			filesToGenerate = append(filesToGenerate, file)
		}
	}
	return allFiles, filesToGenerate, nil
}
