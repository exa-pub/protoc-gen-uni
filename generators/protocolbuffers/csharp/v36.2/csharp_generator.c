// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_generator.{h,cc} (protobuf v36.2):
 * Generator::Generate. */

/* kBannedNamespaceChars, IsValidCSharpNamespace: characters that could enable code injection in
 * generated C# code. */
static bool cs_is_valid_csharp_namespace(Str ns, Buf *error)
{
	static const char banned[] = ";{}()'\"`$#=<>|&[]/\\@\n\r!?*~^%,";
	for (size_t i = 0; i < ns.n; i++) {
		char c = ns.p[i];
		if (c == 0 || memchr(banned, c, sizeof banned - 1)) {
			buf_adds(error, "Invalid character in csharp_namespace option: character '");
			buf_addc(error, c);
			buf_adds(error, "' is not allowed because it could enable code injection.");
			return false;
		}
	}
	return true;
}

/* Generator::Generate */
static bool cs_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *ctx,
			Buf *error)
{
	(void)gen;
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	CsOptions cli_options = {.file_extension = ".cs", .base_namespace = ""};
	for (int i = 0; i < n; i++) {
		if (str_eq(keys[i], "file_extension")) {
			cli_options.file_extension = values[i];
		} else if (str_eq(keys[i], "base_namespace")) {
			cli_options.base_namespace = values[i];
			cli_options.base_namespace_specified = true;
		} else if (str_eq(keys[i], "internal_access")) {
			cli_options.internal_access = true;
		} else if (str_eq(keys[i], "serializable")) {
			cli_options.serializable = true;
		} else if (str_eq(keys[i], "experimental_strip_nonfunctional_codegen")) {
			cli_options.strip_nonfunctional_codegen = true;
		} else {
			buf_adds(error, xcat("Unknown generator option: ", keys[i]));
			return false;
		}
	}

	if (pb_has(file->options, FILEOPT_CSHARP_NAMESPACE) &&
	    !cs_is_valid_csharp_namespace(pb_bytes(file->options, FILEOPT_CSHARP_NAMESPACE), error))
		return false;

	char *filename_error = "";
	char *filename = lp_csharp_output_file(file, cli_options.file_extension, cli_options.base_namespace_specified,
					       cli_options.base_namespace, &filename_error);
	if (!*filename) {
		buf_adds(error, filename_error);
		return false;
	}
	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(ctx, filename), '$');
	cli_options.locations = lp_source_locations(file);
	cs_reflection_class_generate(file, &cli_options, &printer);
	return true;
}
