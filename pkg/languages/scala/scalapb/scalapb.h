/* Copyright (c) ScalaPB authors (Nadav Samet and contributors).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* ScalaPB 0.11.21, the compiler-plugin library (compiler-plugin/src/main/scala/scalapb, with
 * scalapb.options from protobuf/scalapb/scalapb.proto): ProtobufGenerator, DescriptorImplicits,
 * FunctionalPrinter and the rest of scalapb.compiler, on the descriptors of protobuf-java 3.25.8
 * (pkg/languages/java/protobuf-java) and the requests of protoc-bridge (protocgen.h). The plugins
 * built on it: protoc-gen-scala (ScalaPbCodeGenerator, community/scalapb-scala) and the ScalaPB
 * plugins of other projects (zio-grpc: community/scalapb-zio-grpc).
 *
 * Functions are named scalapb_<class>_<member> after the Scala definitions they port. */
#ifndef SCALAPB_H
#define SCALAPB_H

#include "protocgen.h"

/* CodeGeneratorRequest as protobuf-java parses it with Scalapb.registerAllExtensions: the
 * registry for protocgen_run. */
extern const PbMsgDef jd_CodeGeneratorRequest;

/* ScalaPbCodeGenerator.process: ProtobufGenerator.handleCodeGeneratorRequest. For
 * protocgen_run (arg is unused). */
void scalapb_code_generator_process(const CodeGenRequest *request, CodeGenResponse *response, void *arg);

/* ProtobufGenerator.parseParameters (GeneratorParams.fromString); Left(error) fails with the
 * error, as CodeGenResponse.fail(error) does. The parameters are those of the implicits made next. */
void scalapb_parse_parameters(Str params);

/* DescriptorImplicits.fromCodeGenRequest(params, request): the implicits below are over the files
 * of request, with the parameters of the last scalapb_parse_parameters. GeneratorExceptions that
 * they throw (FileOptionsCache) are exceptions: "scalapb.compiler.GeneratorException: <message>". */
void scalapb_descriptor_implicits(const CodeGenRequest *request);

/* DescriptorImplicits.ScalaName */
typedef struct ScalaName ScalaName;
/* ScalaName./(name) */
ScalaName *scalapb_scala_name_child(const ScalaName *n, const char *name);
/* ScalaName.name: the last part as given */
const char *scalapb_scala_name_name(const ScalaName *n);
/* ScalaName.fullName: the parts as symbols (asSymbol), joined by "." */
const char *scalapb_scala_name_full_name(const ScalaName *n);

/* NameUtils.snakeCaseToCamelCase(name, upperInitial) */
const char *scalapb_snake_case_to_camel_case(const char *name, bool upper_initial);
/* DescriptorImplicits.baseName(fileName) */
const char *scalapb_base_name(const char *file_name);

/* ExtendedFileDescriptor.scalaPackage */
ScalaName *scalapb_file_scala_package(JFile *f);
/* ExtendedServiceDescriptor.name, .grpcDescriptor */
const char *scalapb_service_name(JService *s);
ScalaName *scalapb_service_grpc_descriptor(JService *s);
/* ExtendedMethodDescriptor.name, .grpcDescriptor, .streamType */
const char *scalapb_method_name(JMethod *m);
ScalaName *scalapb_method_grpc_descriptor(JMethod *m);
enum { SCALAPB_UNARY, SCALAPB_CLIENT_STREAMING, SCALAPB_SERVER_STREAMING, SCALAPB_BIDIRECTIONAL }; /* StreamType */
int scalapb_method_stream_type(const JMethod *m);
/* ExtendedMethodDescriptor.inputType.scalaType and outputType.scalaType (MethodTypeWrapper) of
 * the input or output message of a method */
const char *scalapb_method_type_scala_type(JMsg *type);

/* FunctionalPrinter: add(s) splits s at newlines and indents every line by 2 spaces per level;
 * result() joins the lines with "\n". */
typedef struct FunctionalPrinter {
	Vec lines; /* const char* */
	int indent;
} FunctionalPrinter;
void scalapb_printer_add(FunctionalPrinter *p, const char *s);
void scalapb_printer_indent(FunctionalPrinter *p);
void scalapb_printer_outdent(FunctionalPrinter *p);
const char *scalapb_printer_result(FunctionalPrinter *p);

#endif
