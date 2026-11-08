// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/shared_code_generator.{h,cc}: GenerateDescriptors
 * (SharedCodeGenerator::Generate is only reached by the "shared" option without "immutable",
 * whose descriptor file upstream never writes in the open source release: see generator.cc). */

static void jv_shared_code_generator_generate_descriptors(const LpFile *file, const JvOptions *options,
							  LpPrinter *printer)
{
	PbMsg *file_proto = lp_strip_source_retention_options(file);
	Buf file_data = {0};
	if (!options->strip_nonfunctional_codegen)
		lp_msg_serialize(file_proto, &file_data);

	JV_PRINT(printer, "java.lang.String[] descriptorData = {\n");
	lp_indent(printer);
	enum { kBytesPerLine = 40, kLinesPerPart = 400, kBytesPerPart = kBytesPerLine * kLinesPerPart };
	for (size_t i = 0; i < file_data.n; i += kBytesPerLine) {
		if (i > 0) {
			if (i % kBytesPerPart == 0)
				JV_PRINT(printer, ",\n");
			else
				JV_PRINT(printer, " +\n");
		}
		size_t n = file_data.n - i < kBytesPerLine ? file_data.n - i : kBytesPerLine;
		lp_print(printer, "\"$data$\"", "data", lp_cescape((Str){file_data.p + i, n}), NULL);
	}
	lp_outdent(printer);
	JV_PRINT(printer, "\n};\n");

	JV_PRINT(printer, "descriptor = com.google.protobuf.Descriptors.FileDescriptor\n"
			  "  .internalBuildGeneratedFileFrom(descriptorData,\n");
	JV_PRINT(printer, "    new com.google.protobuf.Descriptors.FileDescriptor[] {\n");
	for (int i = 0; i < file->n_dependencies; i++) {
		const LpFile *dep = file->dependencies[i];
		char *package = jv_file_java_package(dep);
		char *classname = jv_get_descriptor_class_name(dep);
		char *full_name = *package ? xcat(package, ".", classname) : classname;
		lp_print(printer, "      $dependency$.getDescriptor(),\n", "dependency", full_name, NULL);
	}
	JV_PRINT(printer, "    });\n");
}
