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

package es

import (
	"errors"
	"regexp"
	"slices"
	"strings"

	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protobuf"
	"github.com/exa-pub/protoc-gen-uni/pkg/languages/ecmascript/protoplugin"
)

// Ported from packages/protoc-gen-es/src/protoc-gen-es-plugin.ts.

const version = "2.17.0"

var protocGenEs = protoplugin.CreateEcmaScriptPlugin(protoplugin.PluginInit[options]{
	Name:         "protoc-gen-es",
	Version:      "v" + version,
	ParseOptions: parseOptions,
	GenerateTs:   generateTs,
	GenerateJs:   generateJs,
	GenerateDts:  generateDts,
})

type validTypesOptions struct {
	legacyRequired        bool
	protovalidateRequired bool
}

type options struct {
	jsonTypes      bool
	validTypes     validTypesOptions
	erasableSyntax bool
}

type schema = protoplugin.Schema[options]

func parseBoolOption(value string) (bool, error) {
	switch value {
	case "true", "1":
		return true, nil
	case "false", "0":
		return false, nil
	}
	return false, errors.New("please provide true or false")
}

func parseOptions(rawOptions []protoplugin.RawOption) (options, error) {
	var opts options
	for _, o := range rawOptions {
		switch o.Key {
		case "json_types":
			v, err := parseBoolOption(o.Value)
			if err != nil {
				return opts, err
			}
			opts.jsonTypes = v
		case "valid_types":
			for _, part := range strings.Split(o.Value, "+") {
				switch part {
				case "protovalidate_required":
					opts.validTypes.protovalidateRequired = true
				case "legacy_required":
					opts.validTypes.legacyRequired = true
				default:
					return opts, errors.New("")
				}
			}
		case "erasable_syntax":
			v, err := parseBoolOption(o.Value)
			if err != nil {
				return opts, err
			}
			opts.erasableSyntax = v
		default:
			return opts, errors.New("")
		}
	}
	return opts, nil
}

// This annotation informs bundlers that the succeeding function call is free of
// side effects. This means the symbol can be removed from the module during
// tree-shaking if it is unused.
// See https://github.com/bufbuild/protobuf-es/pull/470
const pure = "/*@__PURE__*/"

func descPath(fileDesc printable, desc protobuf.Desc) []printable {
	args := []printable{fileDesc}
	for _, i := range protobuf.PathInFileDesc(desc) {
		args = append(args, i)
	}
	return args
}

func hasValidTypes(opts options) bool {
	return opts.validTypes.legacyRequired || opts.validTypes.protovalidateRequired
}

func generateTs(schema *schema, _ protoplugin.Target) {
	opts := schema.PluginOptions
	for _, file := range schema.Files {
		f := schema.GenerateFile(file.Name + "_pb.ts")
		f.Preamble(file)
		genFile := f.Runtime.Codegen.GenFile
		fileDesc := f.ImportSchema(file, false)
		generateDescDoc(f, file)
		f.Print(f.Export("const", fileDesc.Name), ": ", genFile, " = ", pure)
		f.Print("  ", getFileDescCall(f, file, schema), ";")
		f.Print()
		for _, desc := range schema.TypesInFile(file) {
			switch desc := desc.(type) {
			case *protobuf.DescMessage:
				generateMessageShape(f, desc, protoplugin.TargetTs)
				if opts.jsonTypes {
					generateMessageJSONShape(f, desc, protoplugin.TargetTs)
				}
				if hasValidTypes(opts) {
					generateMessageValidShape(f, desc, opts.validTypes, protoplugin.TargetTs)
				}
				generateDescDoc(f, desc)
				name := f.ImportSchema(desc, false).Name
				f.Print(f.Export("const", name), ": ", messageGenType(desc, f, opts), " = ", pure)
				call := functionCall(f.Runtime.Codegen.MessageDesc, descPath(fileDesc, desc))
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescEnum:
				if opts.erasableSyntax {
					generateObjEnum(f, desc, protoplugin.TargetTs)
				} else {
					generateEnumShape(f, desc)
				}
				if opts.jsonTypes {
					generateEnumJSONShape(f, desc, protoplugin.TargetTs)
				}
				generateDescDoc(f, desc)
				name := f.ImportSchema(desc, false).Name
				shape := f.ImportShape(desc)
				genEnum := f.Runtime.Codegen.GenEnum
				if opts.jsonTypes {
					jsonType := f.ImportJSON(desc)
					f.Print(f.Export("const", name), ": ", genEnum, "<", shape, ", ", jsonType, ">", " = ", pure)
				} else {
					f.Print(f.Export("const", name), ": ", genEnum, "<", shape, ">", " = ", pure)
				}
				call := functionCall(f.Runtime.Codegen.EnumDesc, descPath(fileDesc, desc))
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescField: // extension
				name := f.ImportSchema(desc, false).Name
				e := f.ImportShape(desc.Extendee)
				v, _ := fieldTypeScriptType(desc, f.Runtime, false)
				call := functionCall(f.Runtime.Codegen.ExtDesc, descPath(fileDesc, desc))
				f.Print(f.JSDocDesc(desc, ""))
				f.Print(f.Export("const", name), ": ", f.Runtime.Codegen.GenExtension, "<", e, ", ", v, ">", " = ", pure)
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescService:
				name := f.ImportSchema(desc, false).Name
				call := functionCall(f.Runtime.Codegen.ServiceDesc, descPath(fileDesc, desc))
				f.Print(f.JSDocDesc(desc, ""))
				f.Print(f.Export("const", name), ": ", f.Runtime.Codegen.GenService, "<", getServiceShapeExpr(f, desc), "> = ", pure)
				f.Print("  ", call, ";")
				f.Print()
			}
		}
	}
}

func generateJs(schema *schema, _ protoplugin.Target) {
	opts := schema.PluginOptions
	for _, file := range schema.Files {
		f := schema.GenerateFile(file.Name + "_pb.js")
		f.Preamble(file)
		fileDesc := f.ImportSchema(file, false)
		generateDescDoc(f, file)
		f.Print(f.Export("const", fileDesc.Name), " = ", pure)
		f.Print("  ", getFileDescCall(f, file, schema), ";")
		f.Print()
		for _, desc := range schema.TypesInFile(file) {
			switch desc := desc.(type) {
			case *protobuf.DescMessage:
				name := f.ImportSchema(desc, false).Name
				generateDescDoc(f, desc)
				call := functionCall(f.Runtime.Codegen.MessageDesc, descPath(fileDesc, desc))
				f.Print(f.Export("const", name), " = ", pure)
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescEnum:
				// generate descriptor
				generateDescDoc(f, desc)
				name := f.ImportSchema(desc, false).Name
				f.Print(f.Export("const", name), " = ", pure)
				call := functionCall(f.Runtime.Codegen.EnumDesc, descPath(fileDesc, desc))
				f.Print("  ", call, ";")
				f.Print()
				// generate enum
				f.Print(f.JSDocDesc(desc, ""))
				f.Print(f.Export("const", f.ImportShape(desc).Name), " = ", pure)
				enumFn := f.Runtime.Codegen.TsEnum
				if opts.erasableSyntax {
					enumFn = f.Runtime.Codegen.ObjEnum
				}
				call = functionCall(enumFn, []printable{f.ImportSchema(desc, false)})
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescField: // extension
				f.Print(f.JSDocDesc(desc, ""))
				name := f.ImportSchema(desc, false).Name
				f.Print(f.Export("const", name), " = ", pure)
				call := functionCall(f.Runtime.Codegen.ExtDesc, descPath(fileDesc, desc))
				f.Print("  ", call, ";")
				f.Print()
			case *protobuf.DescService:
				f.Print(f.JSDocDesc(desc, ""))
				name := f.ImportSchema(desc, false).Name
				f.Print(f.Export("const", name), " = ", pure)
				call := functionCall(f.Runtime.Codegen.ServiceDesc, descPath(fileDesc, desc))
				f.Print("  ", call, ";")
				f.Print()
			}
		}
	}
}

func generateDts(schema *schema, _ protoplugin.Target) {
	opts := schema.PluginOptions
	for _, file := range schema.Files {
		f := schema.GenerateFile(file.Name + "_pb.d.ts")
		f.Preamble(file)
		genFile := f.Runtime.Codegen.GenFile
		fileDesc := f.ImportSchema(file, false)
		generateDescDoc(f, file)
		f.Print(f.Export("declare const", fileDesc.Name), ": ", genFile, ";")
		f.Print()
		for _, desc := range schema.TypesInFile(file) {
			switch desc := desc.(type) {
			case *protobuf.DescMessage:
				generateMessageShape(f, desc, protoplugin.TargetDts)
				if opts.jsonTypes {
					generateMessageJSONShape(f, desc, protoplugin.TargetDts)
				}
				if hasValidTypes(opts) {
					generateMessageValidShape(f, desc, opts.validTypes, protoplugin.TargetDts)
				}
				name := f.ImportSchema(desc, false).Name
				generateDescDoc(f, desc)
				f.Print(f.Export("declare const", name), ": ", messageGenType(desc, f, opts), ";")
				f.Print()
			case *protobuf.DescEnum:
				if opts.erasableSyntax {
					generateObjEnum(f, desc, protoplugin.TargetDts)
				} else {
					generateEnumShape(f, desc)
				}
				if opts.jsonTypes {
					generateEnumJSONShape(f, desc, protoplugin.TargetDts)
				}
				generateDescDoc(f, desc)
				name := f.ImportSchema(desc, false).Name
				shape := f.ImportShape(desc)
				genEnum := f.Runtime.Codegen.GenEnum
				if opts.jsonTypes {
					jsonType := f.ImportJSON(desc)
					f.Print(f.Export("declare const", name), ": ", genEnum, "<", shape, ", ", jsonType, ">;")
				} else {
					f.Print(f.Export("declare const", name), ": ", genEnum, "<", shape, ">;")
				}
				f.Print()
			case *protobuf.DescField: // extension
				name := f.ImportSchema(desc, false).Name
				e := f.ImportShape(desc.Extendee)
				v, _ := fieldTypeScriptType(desc, f.Runtime, false)
				f.Print(f.JSDocDesc(desc, ""))
				f.Print(f.Export("declare const", name), ": ", f.Runtime.Codegen.GenExtension, "<", e, ", ", v, ">;")
				f.Print()
			case *protobuf.DescService:
				name := f.ImportSchema(desc, false).Name
				f.Print(f.JSDocDesc(desc, ""))
				f.Print(f.Export("declare const", name), ": ", f.Runtime.Codegen.GenService, "<", getServiceShapeExpr(f, desc), ">;")
				f.Print()
			}
		}
	}
}

func generateDescDoc(f *protoplugin.GeneratedFile, desc protobuf.Desc) {
	var lines []string
	switch desc.(type) {
	case *protobuf.DescFile, *protobuf.DescEnum:
		lines = []string{"Describes the " + desc.String() + "."}
	case *protobuf.DescMessage:
		lines = []string{
			"Describes the " + desc.String() + ".",
			"Use `create(" + f.ImportSchema(desc, false).Name + ")` to create a new message.",
		}
	}
	deprecated := protobuf.IsDeprecated(desc)
	for _, p := range protobuf.ParentTypes(desc) {
		deprecated = deprecated || protobuf.IsDeprecated(p)
	}
	if deprecated {
		lines = append(lines, "@deprecated")
	}
	f.Print(protoplugin.JSDocBlock{Text: strings.Join(lines, "\n")})
}

func getFileDescCall(f *protoplugin.GeneratedFile, file *protobuf.DescFile, schema *schema) printable {
	// Schema provides files with source retention options. Since we do not want to
	// embed source retention options in generated code, we use FileDescriptorProto
	// messages from CodeGeneratorRequest.proto_file instead.
	sourceFile := file.Proto
	runtimeFile := sourceFile
	for _, p := range schema.Proto.GetProtoFile() {
		if p.GetName() == sourceFile.GetName() {
			runtimeFile = p
			break
		}
	}
	info := protobuf.EmbedFileDesc(runtimeFile)
	if info.Bootable && !strings.HasPrefix(f.Runtime.Create.From, "@bufbuild/protobuf") {
		// google/protobuf/descriptor.proto is embedded as a plain object when
		// bootstrapping to avoid recursion
		return functionCall(f.Runtime.Codegen.Boot, []printable{info.Boot()})
	}
	if len(file.Dependencies) > 0 {
		deps := make([]printable, 0, len(file.Dependencies))
		for _, d := range file.Dependencies {
			deps = append(deps, protoplugin.DescImport{Desc: d})
		}
		return functionCall(f.Runtime.Codegen.FileDesc, []printable{f.String(info.Base64()), f.Array(deps)})
	}
	return functionCall(f.Runtime.Codegen.FileDesc, []printable{f.String(info.Base64())})
}

func getServiceShapeExpr(f *protoplugin.GeneratedFile, service *protobuf.DescService) printable {
	p := []printable{"{\n"}
	for _, method := range service.Methods {
		p = append(p, f.JSDocDesc(method, "  "), "\n")
		p = append(p, "  ", method.LocalName, ": {\n")
		p = append(p, "    methodKind: ", f.String(method.MethodKind), ";\n")
		p = append(p, "    input: typeof ", f.ImportSchema(method.Input, true), ";\n")
		p = append(p, "    output: typeof ", f.ImportSchema(method.Output, true), ";\n")
		p = append(p, "  },\n")
	}
	return append(p, "}")
}

func generateEnumShape(f *protoplugin.GeneratedFile, enum *protobuf.DescEnum) {
	f.Print(f.JSDocDesc(enum, ""))
	f.Print(f.Export("enum", f.ImportShape(enum).Name), " {")
	for i, value := range enum.Values {
		if i > 0 {
			f.Print()
		}
		f.Print(f.JSDocDesc(value, "  "))
		f.Print("  ", value.LocalName, " = ", value.Number, ",")
	}
	f.Print("}")
	f.Print()
}

func generateObjEnum(f *protoplugin.GeneratedFile, enum *protobuf.DescEnum, target protoplugin.Target) {
	name := f.ImportShape(enum).Name
	f.Print(f.JSDocDesc(enum, ""))
	if target == protoplugin.TargetTs {
		f.Print(f.Export("const", name), " = {")
	} else {
		f.Print(f.Export("declare const", name), ": {")
	}
	for i, value := range enum.Values {
		if i > 0 {
			f.Print()
		}
		f.Print(f.JSDocDesc(value, "  "))
		readonly := "readonly "
		if target == protoplugin.TargetTs {
			readonly = ""
		}
		f.Print("  ", readonly, value.LocalName, ": ", value.Number, ",")
	}
	if target == protoplugin.TargetTs {
		f.Print("}", " as const", ";")
	} else {
		f.Print("}", "", ";")
	}
	f.Print()
	generateObjEnumType(f, enum, target)
}

func generateObjEnumType(f *protoplugin.GeneratedFile, enum *protobuf.DescEnum, target protoplugin.Target) {
	name := f.ImportShape(enum).Name
	f.Print(f.JSDocDesc(enum, ""))
	declaration := "declare type"
	if target == protoplugin.TargetTs {
		declaration = "type"
	}
	var unknown printable = []printable{}
	if enum.Open {
		unknown = []printable{" | ", f.Runtime.UnknownEnum}
	}
	f.Print(f.Export(declaration, name), " = (typeof ", name, ")[keyof typeof ", name, "]", unknown, ";")
	f.Print()
}

func typeDeclaration(target protoplugin.Target) string {
	if target == protoplugin.TargetTs {
		return "type"
	}
	return "declare type"
}

func generateEnumJSONShape(f *protoplugin.GeneratedFile, enum *protobuf.DescEnum, target protoplugin.Target) {
	f.Print(f.JSDocDesc(enum, ""))
	var values []printable
	if enum.TypeName == "google.protobuf.NullValue" {
		values = append(values, "null")
	} else {
		// The JSON type matches the output of toJson(), which emits the custom JSON
		// name if one is set. We omit the Protobuf name from the type, even though
		// fromJson() accepts it.
		var names []string
		for _, v := range enum.Values {
			if !slices.Contains(names, v.JSONName) {
				names = append(names, v.JSONName)
			}
		}
		for _, name := range names {
			if len(values) > 0 {
				values = append(values, " | ")
			}
			values = append(values, f.String(name))
		}
	}
	f.Print(f.Export(typeDeclaration(target), f.ImportJSON(enum).Name), " = ", values, ";")
	f.Print()
}

func generateMessageShape(f *protoplugin.GeneratedFile, message *protobuf.DescMessage, target protoplugin.Target) {
	f.Print(f.JSDocDesc(message, ""))
	f.Print(f.Export(typeDeclaration(target), f.ImportShape(message).Name), " = ", f.Runtime.Message, "<", f.String(message.TypeName), "> & {")
	for i, member := range message.Members {
		generateMessageShapeMember(f, member, nil)
		if i < len(message.Members)-1 {
			f.Print()
		}
	}
	f.Print("};")
	f.Print()
}

func generateMessageValidShape(f *protoplugin.GeneratedFile, message *protobuf.DescMessage, validTypes validTypesOptions, target protoplugin.Target) {
	declaration := typeDeclaration(target)
	if !messageNeedsCustomValidType(message, validTypes) {
		f.Print(f.Export(declaration, f.ImportValid(message).Name), " = ", f.ImportShape(message), ";")
		f.Print()
		return
	}
	f.Print(f.JSDocDesc(message, ""))
	f.Print(f.Export(declaration, f.ImportValid(message).Name), " = ", f.Runtime.Message, "<", f.String(message.TypeName), "> & {")
	for i, member := range message.Members {
		generateMessageShapeMember(f, member, &validTypes)
		if i < len(message.Members)-1 {
			f.Print()
		}
	}
	f.Print("};")
	f.Print()
}

func generateMessageShapeMember(f *protoplugin.GeneratedFile, member protobuf.Desc, validTypes *validTypesOptions) {
	switch member := member.(type) {
	case *protobuf.DescOneof:
		f.Print(f.JSDocDesc(member, "  "))
		f.Print("  ", member.LocalName, ": {")
		for i, field := range member.Fields {
			if i > 0 {
				f.Print("  } | {")
			}
			f.Print(f.JSDocDesc(field, "    "))
			typing, _ := fieldTypeScriptType(field, f.Runtime, validTypes != nil && !isProtovalidateDisabled(field))
			f.Print("    value: ", typing, ";")
			f.Print(`    case: "`, field.LocalName, `";`)
		}
		f.Print("  } | { case: undefined; value?: undefined };")
	case *protobuf.DescField:
		f.Print(f.JSDocDesc(member, "  "))
		typing, optional := fieldTypeScriptType(member, f.Runtime, validTypes != nil && !isProtovalidateDisabled(member))
		if optional && validTypes != nil {
			isRequired := (validTypes.legacyRequired && isLegacyRequired(member)) ||
				(validTypes.protovalidateRequired && isProtovalidateRequired(member))
			if isRequired {
				optional = false
			}
		}
		if optional {
			f.Print("  ", member.LocalName, "?: ", typing, " | undefined;")
		} else {
			f.Print("  ", member.LocalName, ": ", typing, ";")
		}
	}
}

var (
	startWithNumber    = regexp.MustCompile(`^[0-9]`)
	containsSpecialChr = regexp.MustCompile(`[^a-zA-Z0-9_$]`)
)

func generateMessageJSONShape(f *protoplugin.GeneratedFile, message *protobuf.DescMessage, target protoplugin.Target) {
	exp := f.Export(typeDeclaration(target), f.ImportJSON(message).Name)
	f.Print(f.JSDocDesc(message, ""))
	switch message.TypeName {
	case "google.protobuf.Any":
		f.Print(exp, " = {")
		f.Print(`  "@type"?: string;`)
		f.Print("};")
	case "google.protobuf.Timestamp", "google.protobuf.Duration", "google.protobuf.FieldMask":
		f.Print(exp, " = string;")
	case "google.protobuf.Struct":
		f.Print(exp, " = ", f.Runtime.JSONObject, ";")
	case "google.protobuf.Value":
		f.Print(exp, " = ", f.Runtime.JSONValue, ";")
	case "google.protobuf.ListValue":
		f.Print(exp, " = ", f.Runtime.JSONValue, "[];")
	case "google.protobuf.Empty":
		f.Print(exp, " = Record<string, never>;")
	default:
		if protobuf.IsWrapperDesc(message) {
			f.Print(exp, " = ", fieldJSONType(message.Fields[0]), ";")
			break
		}
		f.Print(exp, " = {")
		for i, field := range message.Fields {
			f.Print(f.JSDocDesc(field, "  "))
			var jsonName printable = field.JSONName
			if field.JSONName == "" || startWithNumber.MatchString(field.JSONName) || containsSpecialChr.MatchString(field.JSONName) {
				jsonName = f.String(field.JSONName)
			}
			f.Print("  ", jsonName, "?: ", fieldJSONType(field), ";")
			if i < len(message.Fields)-1 {
				f.Print()
			}
		}
		f.Print("};")
	}
	f.Print()
}
