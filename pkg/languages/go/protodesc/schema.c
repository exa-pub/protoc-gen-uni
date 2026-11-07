/* Copyright 2008 Google Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* The messages of google/protobuf/descriptor.proto, compiler/plugin.proto and go_features.proto
 * as compiled into google.golang.org/protobuf v1.36.12 (types/descriptorpb, types/pluginpb,
 * types/gofeaturespb): field numbers, types, labels, packedness, retention=SOURCE and whether
 * the message has extension ranges. Dumped from the Go descriptors (protoreflect) of that
 * version. FeatureSetDefaults is not needed (features.c has the defaults as a table). */
#include "internal.h"

enum {
	M_FileDescriptorProto, M_DescriptorProto, M_ExtensionRange, M_ReservedRange, M_ExtensionRangeOptions,
	M_Declaration, M_FieldDescriptorProto, M_OneofDescriptorProto, M_EnumDescriptorProto, M_EnumReservedRange,
	M_EnumValueDescriptorProto, M_ServiceDescriptorProto, M_MethodDescriptorProto, M_FileOptions,
	M_MessageOptions, M_FieldOptions, M_EditionDefault, M_FeatureSupport, M_OneofOptions, M_EnumOptions,
	M_EnumValueOptions, M_ServiceOptions, M_MethodOptions, M_UninterpretedOption, M_NamePart, M_FeatureSet,
	M_SourceCodeInfo, M_Location, M_GeneratedCodeInfo, M_Annotation, M_Version, M_CodeGeneratorRequest,
	M_GoFeatures, M_COUNT
};

#define F(num, t) {num, PT_##t, PL_OPT, 0, -1}
#define R(num, t) {num, PT_##t, PL_REP, 0, -1}
#define P(num, t) {num, PT_##t, PL_PACKED, 0, -1}
#define FS(num, t) {num, PT_##t, PL_OPT, GOF_SOURCE, -1}
#define FM(num, m) {num, PT_MESSAGE, PL_OPT, 0, M_##m}
#define RM(num, m) {num, PT_MESSAGE, PL_REP, 0, M_##m}
#define RMS(num, m) {num, PT_MESSAGE, PL_REP, GOF_SOURCE, M_##m}

static const GoSpecField f_FileDescriptorProto[] = {
	F(1, STRING), F(2, STRING), R(3, STRING), RM(4, DescriptorProto), RM(5, EnumDescriptorProto),
	RM(6, ServiceDescriptorProto), RM(7, FieldDescriptorProto), FM(8, FileOptions), FM(9, SourceCodeInfo),
	R(10, INT32), R(11, INT32), F(12, STRING), F(14, ENUM), R(15, STRING),
};
static const GoSpecField f_DescriptorProto[] = {
	F(1, STRING), RM(2, FieldDescriptorProto), RM(3, DescriptorProto), RM(4, EnumDescriptorProto),
	RM(5, ExtensionRange), RM(6, FieldDescriptorProto), FM(7, MessageOptions), RM(8, OneofDescriptorProto),
	RM(9, ReservedRange), R(10, STRING), F(11, ENUM),
};
static const GoSpecField f_ExtensionRange[] = {F(1, INT32), F(2, INT32), FM(3, ExtensionRangeOptions)};
static const GoSpecField f_Range[] = {F(1, INT32), F(2, INT32)};
static const GoSpecField f_ExtensionRangeOptions[] = {
	RMS(2, Declaration), FS(3, ENUM), FM(50, FeatureSet), RM(999, UninterpretedOption),
};
static const GoSpecField f_Declaration[] = {F(1, INT32), F(2, STRING), F(3, STRING), F(5, BOOL), F(6, BOOL)};
static const GoSpecField f_FieldDescriptorProto[] = {
	F(1, STRING), F(2, STRING), F(3, INT32), F(4, ENUM), F(5, ENUM), F(6, STRING), F(7, STRING),
	FM(8, FieldOptions), F(9, INT32), F(10, STRING), F(17, BOOL),
};
static const GoSpecField f_OneofDescriptorProto[] = {F(1, STRING), FM(2, OneofOptions)};
static const GoSpecField f_EnumDescriptorProto[] = {
	F(1, STRING), RM(2, EnumValueDescriptorProto), FM(3, EnumOptions), RM(4, EnumReservedRange), R(5, STRING),
	F(6, ENUM),
};
static const GoSpecField f_EnumValueDescriptorProto[] = {F(1, STRING), F(2, INT32), FM(3, EnumValueOptions)};
static const GoSpecField f_ServiceDescriptorProto[] = {F(1, STRING), RM(2, MethodDescriptorProto), FM(3, ServiceOptions)};
static const GoSpecField f_MethodDescriptorProto[] = {
	F(1, STRING), F(2, STRING), F(3, STRING), FM(4, MethodOptions), F(5, BOOL), F(6, BOOL),
};
static const GoSpecField f_FileOptions[] = {
	F(1, STRING), F(8, STRING), F(9, ENUM), F(10, BOOL), F(11, STRING), F(16, BOOL), F(17, BOOL),
	F(18, BOOL), F(20, BOOL), F(23, BOOL), F(27, BOOL), F(31, BOOL), F(36, STRING), F(37, STRING),
	F(39, STRING), F(40, STRING), F(41, STRING), F(44, STRING), F(45, STRING), FM(50, FeatureSet),
	RM(999, UninterpretedOption),
};
static const GoSpecField f_MessageOptions[] = {
	F(1, BOOL), F(2, BOOL), F(3, BOOL), F(7, BOOL), F(11, BOOL), FM(12, FeatureSet), RM(999, UninterpretedOption),
};
static const GoSpecField f_FieldOptions[] = {
	F(1, ENUM), F(2, BOOL), F(3, BOOL), F(5, BOOL), F(6, ENUM), F(10, BOOL), F(15, BOOL), F(16, BOOL),
	F(17, ENUM), R(19, ENUM), RM(20, EditionDefault), FM(21, FeatureSet), FM(22, FeatureSupport),
	RM(999, UninterpretedOption),
};
static const GoSpecField f_EditionDefault[] = {F(2, STRING), F(3, ENUM)};
static const GoSpecField f_FeatureSupport[] = {F(1, ENUM), F(2, ENUM), F(3, STRING), F(4, ENUM), F(5, STRING)};
static const GoSpecField f_OneofOptions[] = {FM(1, FeatureSet), RM(999, UninterpretedOption)};
static const GoSpecField f_EnumOptions[] = {F(2, BOOL), F(3, BOOL), F(6, BOOL), FM(7, FeatureSet), RM(999, UninterpretedOption)};
static const GoSpecField f_EnumValueOptions[] = {
	F(1, BOOL), FM(2, FeatureSet), F(3, BOOL), FM(4, FeatureSupport), RM(999, UninterpretedOption),
};
static const GoSpecField f_ServiceOptions[] = {F(33, BOOL), FM(34, FeatureSet), RM(999, UninterpretedOption)};
static const GoSpecField f_MethodOptions[] = {F(33, BOOL), F(34, ENUM), FM(35, FeatureSet), RM(999, UninterpretedOption)};
static const GoSpecField f_UninterpretedOption[] = {
	RM(2, NamePart), F(3, STRING), F(4, UINT64), F(5, INT64), F(6, DOUBLE), F(7, BYTES), F(8, STRING),
};
static const GoSpecField f_NamePart[] = {F(1, STRING), F(2, BOOL)};
static const GoSpecField f_FeatureSet[] = {
	F(1, ENUM), F(2, ENUM), F(3, ENUM), F(4, ENUM), F(5, ENUM), F(6, ENUM), FS(7, ENUM), FS(8, ENUM),
};
static const GoSpecField f_SourceCodeInfo[] = {RM(1, Location)};
static const GoSpecField f_Location[] = {P(1, INT32), P(2, INT32), F(3, STRING), F(4, STRING), R(6, STRING)};
static const GoSpecField f_GeneratedCodeInfo[] = {RM(1, Annotation)};
static const GoSpecField f_Annotation[] = {P(1, INT32), F(2, STRING), F(3, INT32), F(4, INT32), F(5, ENUM)};
static const GoSpecField f_Version[] = {F(1, INT32), F(2, INT32), F(3, INT32), F(4, STRING)};
static const GoSpecField f_CodeGeneratorRequest[] = {
	R(1, STRING), F(2, STRING), FM(3, Version), RM(15, FileDescriptorProto), RM(17, FileDescriptorProto),
};
static const GoSpecField f_GoFeatures[] = {F(1, BOOL), F(2, ENUM), F(3, ENUM)};

#define MSG(name, fields, ext) {name, fields, (int)(sizeof fields / sizeof *fields), ext}
const GoSpecMsg go_spec_msgs[M_COUNT] = {
	MSG("google.protobuf.FileDescriptorProto", f_FileDescriptorProto, false),
	MSG("google.protobuf.DescriptorProto", f_DescriptorProto, false),
	MSG("google.protobuf.DescriptorProto.ExtensionRange", f_ExtensionRange, false),
	MSG("google.protobuf.DescriptorProto.ReservedRange", f_Range, false),
	MSG("google.protobuf.ExtensionRangeOptions", f_ExtensionRangeOptions, true),
	MSG("google.protobuf.ExtensionRangeOptions.Declaration", f_Declaration, false),
	MSG("google.protobuf.FieldDescriptorProto", f_FieldDescriptorProto, false),
	MSG("google.protobuf.OneofDescriptorProto", f_OneofDescriptorProto, false),
	MSG("google.protobuf.EnumDescriptorProto", f_EnumDescriptorProto, false),
	MSG("google.protobuf.EnumDescriptorProto.EnumReservedRange", f_Range, false),
	MSG("google.protobuf.EnumValueDescriptorProto", f_EnumValueDescriptorProto, false),
	MSG("google.protobuf.ServiceDescriptorProto", f_ServiceDescriptorProto, false),
	MSG("google.protobuf.MethodDescriptorProto", f_MethodDescriptorProto, false),
	MSG("google.protobuf.FileOptions", f_FileOptions, true),
	MSG("google.protobuf.MessageOptions", f_MessageOptions, true),
	MSG("google.protobuf.FieldOptions", f_FieldOptions, true),
	MSG("google.protobuf.FieldOptions.EditionDefault", f_EditionDefault, false),
	MSG("google.protobuf.FieldOptions.FeatureSupport", f_FeatureSupport, false),
	MSG("google.protobuf.OneofOptions", f_OneofOptions, true),
	MSG("google.protobuf.EnumOptions", f_EnumOptions, true),
	MSG("google.protobuf.EnumValueOptions", f_EnumValueOptions, true),
	MSG("google.protobuf.ServiceOptions", f_ServiceOptions, true),
	MSG("google.protobuf.MethodOptions", f_MethodOptions, true),
	MSG("google.protobuf.UninterpretedOption", f_UninterpretedOption, false),
	MSG("google.protobuf.UninterpretedOption.NamePart", f_NamePart, false),
	MSG("google.protobuf.FeatureSet", f_FeatureSet, true),
	MSG("google.protobuf.SourceCodeInfo", f_SourceCodeInfo, true),
	MSG("google.protobuf.SourceCodeInfo.Location", f_Location, false),
	MSG("google.protobuf.GeneratedCodeInfo", f_GeneratedCodeInfo, false),
	MSG("google.protobuf.GeneratedCodeInfo.Annotation", f_Annotation, false),
	MSG("google.protobuf.compiler.Version", f_Version, false),
	MSG("google.protobuf.compiler.CodeGeneratorRequest", f_CodeGeneratorRequest, false),
	MSG("pb.GoFeatures", f_GoFeatures, false),
};
const int go_spec_n_msgs = M_COUNT;

/* protoregistry.GlobalTypes of protoc-gen-go: extend google.protobuf.FeatureSet {
 * optional pb.GoFeatures go = 1002; } (gofeaturespb). */
const GoSpecExt go_spec_base_ext = {"pb.go", M_FeatureSet, 1002, M_GoFeatures};
