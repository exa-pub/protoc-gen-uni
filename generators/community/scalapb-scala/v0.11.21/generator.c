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

/* community/scalapb-scala v0.11.21: protoc-gen-scala v0.11.21 (buf.build/community/scalapb-scala),
 * the Scala code generator of ScalaPB (https://github.com/scalapb/ScalaPB): the CodeGenApp
 * scalapb.ScalaPbCodeGenerator (compiler-plugin/src/main/scala/scalapb/ScalaPbCodeGenerator.scala)
 * run by protoc-bridge v0.9.10. The generator is the ScalaPB library, pkg/languages/scala/scalapb,
 * on protobuf-java 3.25.8 (pkg/languages/java/protobuf-java). */
#include "scalapb.h"
#include "uni.h"

/* ScalaPbCodeGenerator: registerExtensions = Scalapb.registerAllExtensions,
 * process = ProtobufGenerator.handleCodeGeneratorRequest */
static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return protocgen_run(req, raw, &jd_CodeGeneratorRequest, scalapb_code_generator_process, NULL, out);
}

const UniGenerator gen_community_scalapb_scala_v0_11_21 = {
	.name = "community/scalapb-scala",
	.version = "v0.11.21",
	.generate = generate,
};
