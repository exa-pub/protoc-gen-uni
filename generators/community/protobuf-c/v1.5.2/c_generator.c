// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_generator.{h,cc} (protobuf-c v1.5.2): CGenerator::Generate and
 * ParseOptions. */

/* CGenerator::Generate */
static bool c_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	(void)gen;
	cache = (Cache){0};
	if (pbc_file_options(file)->no_generate)
		return true;

	/* ParseOptions: split at ',' (empty parts skipped), then at the first '=' */
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);

	/* parse generator options: dllexport_decl is written in front of every symbol that should
	 * be exported if this .proto is compiled into a Windows DLL. */
	const char *dllexport_decl = "";
	for (int i = 0; i < n; i++) {
		if (strcmp(keys[i], "dllexport_decl") == 0) {
			dllexport_decl = values[i];
		} else {
			buf_adds(error, "Unknown generator option: ");
			buf_adds(error, keys[i]);
			return false;
		}
	}

	char *basename = xcat(strip_proto(file->name), ".pb-c");

	FileGenerator file_generator;
	file_generator_init(&file_generator, file, dllexport_decl);

	/* Generate header. */
	LpPrinter3 printer;
	lp3_printer_init(&printer, lp_context_open(ctx, xcat(basename, ".h")), '$');
	file_generate_header(&file_generator, &printer);

	/* Generate cc file. */
	lp3_printer_init(&printer, lp_context_open(ctx, xcat(basename, ".c")), '$');
	file_generate_source(&file_generator, &printer);
	return true;
}
