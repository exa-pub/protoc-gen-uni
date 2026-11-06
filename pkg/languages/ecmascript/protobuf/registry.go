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

package protobuf

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strings"

	"google.golang.org/protobuf/encoding/protowire"
	"google.golang.org/protobuf/types/descriptorpb"
)

// Ported from packages/protobuf/src/registry.ts (createFileRegistry from a FileDescriptorSet).

// FileRegistry holds file descriptors and the types they define.
type FileRegistry struct {
	files     []*DescFile
	fileNames map[string]*DescFile
	types     map[string]Desc
}

// Files returns all files in the order they were added.
func (r *FileRegistry) Files() []*DescFile { return r.files }

// GetFile looks up a file by its name, e.g. "foo/bar.proto".
func (r *FileRegistry) GetFile(name string) *DescFile { return r.fileNames[name] }

// Get looks up a message, enum, extension or service by its fully qualified name.
func (r *FileRegistry) Get(typeName string) Desc { return r.types[typeName] }

// GetMessage looks up a message by its fully qualified name.
func (r *FileRegistry) GetMessage(typeName string) *DescMessage {
	m, _ := r.types[typeName].(*DescMessage)
	return m
}

// GetEnum looks up an enum by its fully qualified name.
func (r *FileRegistry) GetEnum(typeName string) *DescEnum {
	e, _ := r.types[typeName].(*DescEnum)
	return e
}

type registryError struct{ msg string }

func assert(cond bool, msg string) {
	if !cond {
		panic(registryError{msg})
	}
}

// CreateFileRegistry creates a registry from the files of a google.protobuf.FileDescriptorSet.
// Dependencies must precede the files importing them.
func CreateFileRegistry(files []*descriptorpb.FileDescriptorProto) (reg *FileRegistry, err error) {
	reg = &FileRegistry{fileNames: map[string]*DescFile{}, types: map[string]Desc{}}
	defer func() {
		if r := recover(); r != nil {
			re, ok := r.(registryError)
			if !ok {
				panic(r)
			}
			reg, err = nil, fmt.Errorf("%s", re.msg)
		}
	}()
	for _, file := range files {
		if err := AssertValidNames(file); err != nil {
			return nil, err
		}
		addFile(file, reg)
	}
	return reg, nil
}

var identifierRe = regexp.MustCompile(`^[A-Za-z0-9_]+$`)

// AssertValidNames returns an error if a name in the file is not a Protobuf identifier.
func AssertValidNames(file *descriptorpb.FileDescriptorProto) error {
	var err error
	check := func(kind, name string) {
		if err == nil && !identifierRe.MatchString(name) {
			q, _ := json.Marshal(name)
			err = fmt.Errorf("%s: invalid %s name %s", file.GetName(), kind, q)
		}
	}
	checkEnum := func(e *descriptorpb.EnumDescriptorProto) {
		check("enum", e.GetName())
		for _, v := range e.GetValue() {
			check("enum value", v.GetName())
		}
	}
	var checkMessage func(m *descriptorpb.DescriptorProto)
	checkMessage = func(m *descriptorpb.DescriptorProto) {
		check("message", m.GetName())
		for _, f := range m.GetField() {
			check("field", f.GetName())
		}
		for _, o := range m.GetOneofDecl() {
			check("oneof", o.GetName())
		}
		for _, x := range m.GetExtension() {
			check("extension", x.GetName())
		}
		for _, n := range m.GetNestedType() {
			checkMessage(n)
		}
		for _, e := range m.GetEnumType() {
			checkEnum(e)
		}
	}
	for _, m := range file.GetMessageType() {
		checkMessage(m)
	}
	for _, e := range file.GetEnumType() {
		checkEnum(e)
	}
	for _, x := range file.GetExtension() {
		check("extension", x.GetName())
	}
	for _, s := range file.GetService() {
		check("service", s.GetName())
		for _, m := range s.GetMethod() {
			check("method", m.GetName())
		}
	}
	return err
}

func (r *FileRegistry) add(d Desc, typeName string) {
	r.types[typeName] = d
}

type mapEntries struct {
	order  []*DescMessage
	byName map[string]*DescMessage
}

func (m *mapEntries) add(desc *DescMessage) {
	assert(desc.Proto.GetOptions().GetMapEntry(), "")
	if _, ok := m.byName[desc.TypeName]; !ok {
		m.order = append(m.order, desc)
	}
	m.byName[desc.TypeName] = desc
}

func addFile(proto *descriptorpb.FileDescriptorProto, reg *FileRegistry) {
	file := &DescFile{
		Proto:      proto,
		Deprecated: proto.GetOptions().GetDeprecated(),
		Edition:    getFileEdition(proto),
		Name:       strings.TrimSuffix(proto.GetName(), ".proto"),
	}
	for _, dep := range proto.GetDependency() {
		d := reg.fileNames[dep]
		if d == nil {
			panic(registryError{fmt.Sprintf("Cannot find %s, imported by %s", dep, proto.GetName())})
		}
		file.Dependencies = append(file.Dependencies, d)
	}
	entries := &mapEntries{byName: map[string]*DescMessage{}}
	for _, e := range proto.GetEnumType() {
		addEnum(e, file, nil, reg)
	}
	for _, m := range proto.GetMessageType() {
		addMessage(m, file, nil, reg, entries)
	}
	for _, s := range proto.GetService() {
		addService(s, file, reg)
	}
	addFileExtensions(file, reg)
	for _, entry := range entries.order {
		addFields(entry, reg, entries)
	}
	for _, m := range file.Messages {
		addFields(m, reg, entries)
		addMessageExtensions(m, reg)
	}
	if _, ok := reg.fileNames[proto.GetName()]; ok {
		for i, f := range reg.files {
			if f.Proto.GetName() == proto.GetName() {
				reg.files[i] = file
			}
		}
	} else {
		reg.files = append(reg.files, file)
	}
	reg.fileNames[proto.GetName()] = file
}

func addFileExtensions(file *DescFile, reg *FileRegistry) {
	for _, p := range file.Proto.GetExtension() {
		ext := newExtension(p, file, nil, reg)
		file.Extensions = append(file.Extensions, ext)
		reg.add(ext, ext.TypeName)
	}
}

func addMessageExtensions(msg *DescMessage, reg *FileRegistry) {
	for _, p := range msg.Proto.GetExtension() {
		ext := newExtension(p, msg.File, msg, reg)
		msg.NestedExtensions = append(msg.NestedExtensions, ext)
		reg.add(ext, ext.TypeName)
	}
	for _, n := range msg.NestedMessages {
		addMessageExtensions(n, reg)
	}
}

func addFields(message *DescMessage, reg *FileRegistry, entries *mapEntries) {
	allOneofs := make([]*DescOneof, 0, len(message.Proto.GetOneofDecl()))
	for _, p := range message.Proto.GetOneofDecl() {
		allOneofs = append(allOneofs, &DescOneof{
			Proto:     p,
			Parent:    message,
			Name:      p.GetName(),
			LocalName: SafeObjectProperty(ProtoCamelCase(p.GetName())),
		})
	}
	seen := map[*DescOneof]bool{}
	for _, p := range message.Proto.GetField() {
		oneof := findOneof(p, allOneofs)
		field := newField(p, message, reg, oneof, entries)
		message.Fields = append(message.Fields, field)
		if oneof == nil {
			message.Members = append(message.Members, field)
		} else {
			oneof.Fields = append(oneof.Fields, field)
			if !seen[oneof] {
				seen[oneof] = true
				message.Members = append(message.Members, oneof)
			}
		}
	}
	for _, o := range allOneofs {
		if seen[o] {
			message.Oneofs = append(message.Oneofs, o)
		}
	}
	for _, child := range message.NestedMessages {
		addFields(child, reg, entries)
	}
}

func addEnum(proto *descriptorpb.EnumDescriptorProto, file *DescFile, parent *DescMessage, reg *FileRegistry) {
	sharedPrefix := findEnumSharedPrefix(proto.GetName(), proto.GetValue())
	desc := &DescEnum{
		Proto:        proto,
		Deprecated:   proto.GetOptions().GetDeprecated(),
		File:         file,
		Parent:       parent,
		Name:         proto.GetName(),
		TypeName:     makeTypeName(proto.GetName(), parent, file),
		SharedPrefix: sharedPrefix,
	}
	var parentRef featureRef = file
	if parent != nil {
		parentRef = parent
	}
	desc.Open = resolveFeature(featEnumType, proto.GetOptions().GetFeatures(), parentRef) == 1 // OPEN
	reg.add(desc, desc.TypeName)
	for _, p := range proto.GetValue() {
		name := p.GetName()
		local := name
		if sharedPrefix != "" {
			local = name[len(sharedPrefix):]
		}
		jsonName, ok := findEnumValueJSONName(p)
		if !ok {
			jsonName = name
		}
		desc.Values = append(desc.Values, &DescEnumValue{
			Proto:      p,
			Deprecated: p.GetOptions().GetDeprecated(),
			Parent:     desc,
			Name:       name,
			LocalName:  SafeObjectProperty(local),
			Number:     p.GetNumber(),
			JSONName:   jsonName,
		})
	}
	if parent != nil {
		parent.NestedEnums = append(parent.NestedEnums, desc)
	} else {
		file.Enums = append(file.Enums, desc)
	}
}

func addMessage(proto *descriptorpb.DescriptorProto, file *DescFile, parent *DescMessage, reg *FileRegistry, entries *mapEntries) {
	desc := &DescMessage{
		Proto:      proto,
		Deprecated: proto.GetOptions().GetDeprecated(),
		File:       file,
		Parent:     parent,
		Name:       proto.GetName(),
		TypeName:   makeTypeName(proto.GetName(), parent, file),
	}
	if proto.GetOptions().GetMapEntry() {
		entries.add(desc)
	} else {
		if parent != nil {
			parent.NestedMessages = append(parent.NestedMessages, desc)
		} else {
			file.Messages = append(file.Messages, desc)
		}
		reg.add(desc, desc.TypeName)
	}
	for _, e := range proto.GetEnumType() {
		addEnum(e, file, desc, reg)
	}
	for _, m := range proto.GetNestedType() {
		addMessage(m, file, desc, reg, entries)
	}
}

func addService(proto *descriptorpb.ServiceDescriptorProto, file *DescFile, reg *FileRegistry) {
	desc := &DescService{
		Proto:      proto,
		Deprecated: proto.GetOptions().GetDeprecated(),
		File:       file,
		Name:       proto.GetName(),
		TypeName:   makeTypeName(proto.GetName(), nil, file),
	}
	file.Services = append(file.Services, desc)
	reg.add(desc, desc.TypeName)
	for _, p := range proto.GetMethod() {
		desc.Methods = append(desc.Methods, newMethod(p, desc, reg))
	}
}

func newMethod(proto *descriptorpb.MethodDescriptorProto, parent *DescService, reg *FileRegistry) *DescMethod {
	var kind string
	switch {
	case proto.GetClientStreaming() && proto.GetServerStreaming():
		kind = "bidi_streaming"
	case proto.GetClientStreaming():
		kind = "client_streaming"
	case proto.GetServerStreaming():
		kind = "server_streaming"
	default:
		kind = "unary"
	}
	input := reg.GetMessage(trimLeadingDot(proto.GetInputType()))
	output := reg.GetMessage(trimLeadingDot(proto.GetOutputType()))
	assert(input != nil, "invalid MethodDescriptorProto: input_type "+proto.GetInputType()+" not found")
	assert(output != nil, "invalid MethodDescriptorProto: output_type "+proto.GetInputType()+" not found")
	name := proto.GetName()
	local := name
	if name != "" {
		local = SafeObjectProperty(strings.ToLower(name[:1]) + name[1:])
	}
	return &DescMethod{
		Proto:       proto,
		Deprecated:  proto.GetOptions().GetDeprecated(),
		Parent:      parent,
		Name:        name,
		LocalName:   SafeObjectProperty(local),
		MethodKind:  kind,
		Input:       input,
		Output:      output,
		Idempotency: proto.GetOptions().GetIdempotencyLevel(),
	}
}

func newExtension(proto *descriptorpb.FieldDescriptorProto, file *DescFile, parent *DescMessage, reg *FileRegistry) *DescField {
	var parentRef featureRef = file
	if parent != nil {
		parentRef = parent
	}
	typeName := makeTypeName(proto.GetName(), parent, file)
	field := &DescField{
		Proto:       proto,
		Deprecated:  proto.GetOptions().GetDeprecated(),
		Name:        proto.GetName(),
		Number:      proto.GetNumber(),
		Presence:    getFieldPresence(proto, nil, true, parentRef),
		IsExtension: true,
		File:        file,
		Parent:      parent,
		TypeName:    typeName,
		JSONName:    "[" + typeName + "]",
	}
	field.Extendee = reg.GetMessage(trimLeadingDot(proto.GetExtendee()))
	assert(field.Extendee != nil, "invalid FieldDescriptorProto: extendee "+proto.GetExtendee()+" not found")
	setFieldType(field, proto, parentRef, reg, nil)
	return field
}

func newField(proto *descriptorpb.FieldDescriptorProto, parent *DescMessage, reg *FileRegistry, oneof *DescOneof, entries *mapEntries) *DescField {
	local := ProtoCamelCase(proto.GetName())
	if oneof == nil {
		local = SafeObjectProperty(local)
	}
	field := &DescField{
		Proto:      proto,
		Deprecated: proto.GetOptions().GetDeprecated(),
		Name:       proto.GetName(),
		Number:     proto.GetNumber(),
		Presence:   getFieldPresence(proto, oneof, false, parent),
		Parent:     parent,
		Oneof:      oneof,
		LocalName:  local,
		JSONName:   proto.GetJsonName(),
	}
	setFieldType(field, proto, parent, reg, entries)
	return field
}

const (
	typeGroup   = descriptorpb.FieldDescriptorProto_TYPE_GROUP
	typeMessage = descriptorpb.FieldDescriptorProto_TYPE_MESSAGE
	typeEnum    = descriptorpb.FieldDescriptorProto_TYPE_ENUM
	typeString  = descriptorpb.FieldDescriptorProto_TYPE_STRING
	typeBytes   = descriptorpb.FieldDescriptorProto_TYPE_BYTES
)

func setFieldType(field *DescField, proto *descriptorpb.FieldDescriptorProto, parent featureRef, reg *FileRegistry, entries *mapEntries) {
	typ := proto.GetType()
	jsString := proto.GetOptions().GetJstype() == descriptorpb.FieldOptions_JS_STRING
	if proto.GetLabel() == descriptorpb.FieldDescriptorProto_LABEL_REPEATED {
		var entry *DescMessage
		if typ == typeMessage && entries != nil {
			entry = entries.byName[trimLeadingDot(proto.GetTypeName())]
		}
		if entry != nil {
			field.FieldKind = FieldKindMap
			key, value := findMapEntryFields(entry)
			field.MapKey = key.Scalar
			field.MapKind = value.FieldKind
			field.Message = value.Message
			field.Enum = value.Enum
			field.Scalar = value.Scalar
			return
		}
		field.FieldKind = FieldKindList
		switch typ {
		case typeMessage, typeGroup:
			field.ListKind = FieldKindMessage
			field.Message = reg.GetMessage(trimLeadingDot(proto.GetTypeName()))
			assert(field.Message != nil, "")
			field.DelimitedEncoding = isDelimitedEncoding(proto, parent)
		case typeEnum:
			field.ListKind = FieldKindEnum
			field.Enum = reg.GetEnum(trimLeadingDot(proto.GetTypeName()))
			assert(field.Enum != nil, "")
		default:
			field.ListKind = FieldKindScalar
			field.Scalar = ScalarType(typ)
			field.LongAsString = jsString
		}
		field.Packed = isPackedField(proto, parent)
		return
	}
	switch typ {
	case typeMessage, typeGroup:
		field.FieldKind = FieldKindMessage
		field.Message = reg.GetMessage(trimLeadingDot(proto.GetTypeName()))
		assert(field.Message != nil, "invalid FieldDescriptorProto: type_name "+proto.GetTypeName()+" not found")
		field.DelimitedEncoding = isDelimitedEncoding(proto, parent)
	case typeEnum:
		field.Enum = reg.GetEnum(trimLeadingDot(proto.GetTypeName()))
		assert(field.Enum != nil, "invalid FieldDescriptorProto: type_name "+proto.GetTypeName()+" not found")
		field.FieldKind = FieldKindEnum
	default:
		field.FieldKind = FieldKindScalar
		field.Scalar = ScalarType(typ)
		field.LongAsString = jsString
	}
}

func getFileEdition(proto *descriptorpb.FileDescriptorProto) descriptorpb.Edition {
	switch proto.GetSyntax() {
	case "", "proto2":
		return EditionProto2
	case "proto3":
		return EditionProto3
	case "editions":
		if proto.GetEdition() == EditionUnstable {
			return MaximumEdition
		}
		if _, ok := featureDefaults[proto.GetEdition()]; ok {
			return proto.GetEdition()
		}
		panic(registryError{proto.GetName() + ": unsupported edition"})
	default:
		panic(registryError{fmt.Sprintf("%s: unsupported syntax \"%s\"", proto.GetName(), proto.GetSyntax())})
	}
}

// findEnumSharedPrefix finds a prefix shared by enum values, for example "my_enum_" for
// enum MyEnum {MY_ENUM_A=0; MY_ENUM_B=1;}.
func findEnumSharedPrefix(enumName string, values []*descriptorpb.EnumValueDescriptorProto) string {
	prefix := camelToSnakeCase(enumName) + "_"
	for _, v := range values {
		if !strings.HasPrefix(jsToLower(v.GetName()), prefix) {
			return ""
		}
		short := v.GetName()[len(prefix):]
		if short == "" {
			return ""
		}
		if short[0] >= '0' && short[0] <= '9' {
			return ""
		}
	}
	return prefix
}

// findEnumValueJSONName finds the custom JSON name of an enum value, set with the option
// (pb.enumvalue.json).string from google/protobuf/json_enumvalue_options.proto.
func findEnumValueJSONName(proto *descriptorpb.EnumValueDescriptorProto) (string, bool) {
	const extensionNumber = 998
	const fieldNumber = 1
	var jsonName string
	var found bool
	if proto.GetOptions() == nil {
		return "", false
	}
	for _, uf := range UnknownFields(proto.GetOptions()) {
		if uf.Number != extensionNumber || uf.Type != protowire.BytesType {
			continue
		}
		b, n := protowire.ConsumeBytes(uf.Data)
		if n < 0 {
			continue
		}
		for len(b) > 0 {
			num, typ, n := protowire.ConsumeTag(b)
			if n < 0 {
				break
			}
			b = b[n:]
			if num == fieldNumber && typ == protowire.BytesType {
				v, n := protowire.ConsumeBytes(b)
				if n < 0 {
					break
				}
				jsonName, found = string(v), true
				b = b[n:]
				continue
			}
			n = protowire.ConsumeFieldValue(num, typ, b)
			if n < 0 {
				break
			}
			b = b[n:]
		}
	}
	return jsonName, found
}

// camelToSnakeCase converts lowerCamelCase or UpperCamelCase into lower_snake_case.
func camelToSnakeCase(camel string) string {
	if camel == "" {
		return ""
	}
	var b strings.Builder
	b.WriteString(camel[:1])
	for _, c := range camel[1:] {
		if c >= 'A' && c <= 'Z' {
			b.WriteByte('_')
		}
		b.WriteRune(c)
	}
	return jsToLower(b.String())
}

// jsToLower is String.prototype.toLowerCase for the ASCII range, which is all that protobuf
// identifiers use.
func jsToLower(s string) string {
	return strings.ToLower(s)
}

func makeTypeName(name string, parent *DescMessage, file *DescFile) string {
	switch {
	case parent != nil:
		return parent.TypeName + "." + name
	case file.Proto.GetPackage() != "":
		return file.Proto.GetPackage() + "." + name
	default:
		return name
	}
}

func trimLeadingDot(typeName string) string {
	return strings.TrimPrefix(typeName, ".")
}

func findOneof(proto *descriptorpb.FieldDescriptorProto, all []*DescOneof) *DescOneof {
	if proto.OneofIndex == nil {
		return nil
	}
	if proto.GetProto3Optional() {
		return nil
	}
	i := int(proto.GetOneofIndex())
	assert(i >= 0 && i < len(all), fmt.Sprintf("invalid FieldDescriptorProto: oneof #%d for field #%d not found", proto.GetOneofIndex(), proto.GetNumber()))
	return all[i]
}

func getFieldPresence(proto *descriptorpb.FieldDescriptorProto, oneof *DescOneof, isExtension bool, parent featureRef) int32 {
	switch {
	case proto.GetLabel() == descriptorpb.FieldDescriptorProto_LABEL_REQUIRED:
		return PresenceLegacyRequired
	case proto.GetLabel() == descriptorpb.FieldDescriptorProto_LABEL_REPEATED:
		return PresenceImplicit
	case oneof != nil || proto.GetProto3Optional():
		return PresenceExplicit
	case isExtension:
		return PresenceExplicit
	}
	resolved := resolveFeature(featFieldPresence, proto.GetOptions().GetFeatures(), parent)
	if resolved == PresenceImplicit && (proto.GetType() == typeMessage || proto.GetType() == typeGroup) {
		return PresenceExplicit
	}
	return resolved
}

func isPackedField(proto *descriptorpb.FieldDescriptorProto, parent featureRef) bool {
	if proto.GetLabel() != descriptorpb.FieldDescriptorProto_LABEL_REPEATED {
		return false
	}
	switch proto.GetType() {
	case typeString, typeBytes, typeGroup, typeMessage:
		return false
	}
	if o := proto.GetOptions(); o != nil && o.Packed != nil {
		return o.GetPacked()
	}
	return resolveFeature(featRepeatedFieldEncoding, proto.GetOptions().GetFeatures(), parent) == 1 // PACKED
}

func findMapEntryFields(entry *DescMessage) (key, value *DescField) {
	for _, f := range entry.Fields {
		switch f.Number {
		case 1:
			if key == nil {
				key = f
			}
		case 2:
			if value == nil {
				value = f
			}
		}
	}
	assert(key != nil && key.FieldKind == FieldKindScalar &&
		key.Scalar != ScalarBytes && key.Scalar != ScalarFloat && key.Scalar != ScalarDouble &&
		value != nil && value.FieldKind != FieldKindList && value.FieldKind != FieldKindMap, "")
	return key, value
}

func isDelimitedEncoding(proto *descriptorpb.FieldDescriptorProto, parent featureRef) bool {
	if proto.GetType() == typeGroup {
		return true
	}
	return resolveFeature(featMessageEncoding, proto.GetOptions().GetFeatures(), parent) == 2 // DELIMITED
}
