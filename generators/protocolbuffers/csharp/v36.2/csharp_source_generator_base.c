// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_source_generator_base.{h,cc} (protobuf
 * v36.2): SourceGeneratorBase, whose options() is the options pointer of each generator. */

static const char *cs_class_access_level(const CsOptions *options)
{
	return options->internal_access ? "internal" : "public";
}

/* WriteGeneratedCodeAttributes: the attributes of generated function members (methods and
 * properties), not of types. */
static void cs_write_generated_code_attributes(LpPrinter *printer)
{
	lp_print(printer, "[global::System.Diagnostics.DebuggerNonUserCodeAttribute]\n", NULL);
	lp_print(printer, "[global::System.CodeDom.Compiler.GeneratedCode(\"protoc\", null)]\n", NULL);
}
