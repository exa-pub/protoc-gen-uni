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
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
)

// Ported from packages/protoplugin/src/names.ts.

// generateFilePath returns the import path of the file generated for the given file descriptor.
func generateFilePath(file *protobuf.DescFile, bootstrapWkt bool, filesToGenerate []*protobuf.DescFile, mapImports []compiledPattern) string {
	// Well-known types are published with the runtime package. We usually want
	// the generated code to import them from the runtime package, with the
	// following exceptions:
	// 1. We are bootstrapping the runtime package via the plugin option
	//    "bootstrap_wkt". In that case, we cannot refer to the runtime package
	//    itself.
	// 2. We were explicitly asked to generate the well-known type.
	if wktFrom, ok := protobuf.WktPublicImportPaths[file.Proto.GetName()]; ok && !bootstrapWkt && !containsFileName(filesToGenerate, file.Name) {
		return wktFrom
	}
	if target, ok := mapImportTarget(file.Proto.GetName(), mapImports); ok {
		return target + "/" + file.Name + "_pb.js"
	}
	return "./" + file.Name + "_pb.js"
}

func containsFileName(files []*protobuf.DescFile, name string) bool {
	for _, f := range files {
		if f.Name == name {
			return true
		}
	}
	return false
}

// fileNames are the names of all elements generated for a file, with name clashes resolved.
type fileNames struct {
	shape     map[protobuf.Desc]string
	desc      map[protobuf.Desc]string
	jsonType  map[protobuf.Desc]string
	validType map[protobuf.Desc]string
}

type namer struct {
	cache map[*protobuf.DescFile]*fileNames
}

func (n *namer) names(file *protobuf.DescFile) *fileNames {
	if n.cache == nil {
		n.cache = map[*protobuf.DescFile]*fileNames{}
	}
	if fn, ok := n.cache[file]; ok {
		return fn
	}
	fn := allNames(file)
	n.cache[file] = fn
	return fn
}

func (n *namer) generatedDescName(desc protobuf.Desc) string {
	name, ok := n.names(protobuf.DescFileOf(desc)).desc[desc]
	if !ok {
		panic(fmt.Errorf("unable to determine unique descriptor name for %s", desc))
	}
	return name
}

func (n *namer) generatedShapeName(desc protobuf.Desc) string {
	name, ok := n.names(protobuf.DescFileOf(desc)).shape[desc]
	if !ok {
		panic(fmt.Errorf("unable to determine unique shape name for %s", desc))
	}
	return name
}

func (n *namer) generatedJSONTypeName(desc protobuf.Desc) string {
	name, ok := n.names(protobuf.DescFileOf(desc)).jsonType[desc]
	if !ok {
		panic(fmt.Errorf("unable to determine unique json type name for %s", desc))
	}
	return name
}

func (n *namer) generatedValidTypeName(desc protobuf.Desc) string {
	name, ok := n.names(protobuf.DescFileOf(desc)).validType[desc]
	if !ok {
		panic(fmt.Errorf("unable to determine unique valid type name for %s", desc))
	}
	return name
}

func salt(i int) string {
	switch i {
	case 0:
		return ""
	case 1:
		return "$"
	default:
		return "$" + strconv.Itoa(i-1)
	}
}

var nonIdentRE = regexp.MustCompile(`[^a-zA-Z0-9_]+`)

func idealDescName(desc protobuf.Desc, i int) string {
	switch d := desc.(type) {
	case *protobuf.DescFile:
		return SafeIdentifier("file_" + nonIdentRE.ReplaceAllString(d.Name, "_") + salt(i))
	case *protobuf.DescEnum, *protobuf.DescMessage:
		return SafeIdentifier(identifier(desc) + "Schema" + salt(i))
	default:
		return SafeIdentifier(identifier(desc) + salt(i))
	}
}

// identifier returns an identifier for the given descriptor based on its type name: the type
// name without the package, with dots replaced by underscores. For example: ParentMsg_MyEnum.
func identifier(desc protobuf.Desc) string {
	var typeName string
	switch d := desc.(type) {
	case *protobuf.DescMessage:
		typeName = d.TypeName
	case *protobuf.DescEnum:
		typeName = d.TypeName
	case *protobuf.DescField:
		typeName = d.TypeName
	case *protobuf.DescService:
		typeName = d.TypeName
	}
	pkg := protobuf.DescFileOf(desc).Proto.GetPackage()
	offset := 0
	if pkg != "" {
		offset = len(pkg) + 1
	}
	return strings.ReplaceAll(typeName[offset:], ".", "_")
}

func allNames(file *protobuf.DescFile) *fileNames {
	taken := map[string]bool{}
	fn := &fileNames{
		shape:     map[protobuf.Desc]string{},
		desc:      map[protobuf.Desc]string{},
		jsonType:  map[protobuf.Desc]string{},
		validType: map[protobuf.Desc]string{},
	}
	nested := protobuf.NestedTypes(file)
	register := func(dst map[protobuf.Desc]string, desc protobuf.Desc, ideal func(int) string) {
		var name string
		for i := 0; ; i++ {
			name = ideal(i)
			if !taken[name] {
				break
			}
		}
		taken[name] = true
		dst[desc] = name
	}
	isShape := func(d protobuf.Desc) bool {
		k := d.Kind()
		return k == "enum" || k == "message"
	}
	// In the first pass, register shape names
	for _, desc := range nested {
		if isShape(desc) {
			register(fn.shape, desc, func(i int) string { return SafeIdentifier(identifier(desc) + salt(i)) })
		}
	}
	// In the second pass, register desc names
	for _, desc := range append([]protobuf.Desc{file}, nested...) {
		register(fn.desc, desc, func(i int) string { return idealDescName(desc, i) })
	}
	// In the third pass, register json type names
	for _, desc := range nested {
		if isShape(desc) {
			register(fn.jsonType, desc, func(i int) string { return SafeIdentifier(identifier(desc) + "Json" + salt(i)) })
		}
	}
	for _, desc := range nested {
		if isShape(desc) {
			register(fn.validType, desc, func(i int) string { return SafeIdentifier(identifier(desc) + "Valid" + salt(i)) })
		}
	}
	return fn
}
