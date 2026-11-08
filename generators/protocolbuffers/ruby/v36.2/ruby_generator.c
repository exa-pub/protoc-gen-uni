// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/ruby/ruby_generator.cc (protobuf v36.2). */

/* kBannedRubyPackageChars, IsValidRubyPackage */
static bool is_valid_ruby_package(Str pkg, Buf *error)
{
	static const char banned[] = ";`{}()'\"`$#=<>|&[]/\\@%\n\r!?*~^,";
	for (size_t i = 0; i < pkg.n; i++) {
		char c = pkg.p[i];
		if (c == 0 || memchr(banned, c, sizeof banned - 1)) {
			buf_adds(error, "Invalid character in ruby_package option: character '");
			buf_addc(error, c);
			buf_adds(error, "' is not allowed because it could enable code injection.");
			return false;
		}
	}
	return true;
}

/* GetRequireName */
static char *get_require_name(const char *proto_file)
{
	const char *dot = strrchr(proto_file, '.');
	size_t n = dot ? (size_t)(dot - proto_file) : strlen(proto_file);
	return xcat(xstrndup(proto_file, n), "_pb");
}

/* GetOutputFilename */
static char *get_output_filename(const char *proto_file) { return xcat(get_require_name(proto_file), ".rb"); }

static bool is_lower(char ch) { return ch >= 'a' && ch <= 'z'; }
static bool is_upper(char ch) { return ch >= 'A' && ch <= 'Z'; }
static bool is_alpha(char ch) { return is_lower(ch) || is_upper(ch); }
static char upper_char(char ch) { return is_lower(ch) ? (char)(ch - 'a' + 'A') : ch; }

/* PackageToModule */
static char *package_to_module(const char *name)
{
	bool next_upper = true;
	Buf b = {0};
	for (const char *p = name; *p; p++) {
		if (*p == '_') {
			next_upper = true;
		} else {
			buf_addc(&b, next_upper ? upper_char(*p) : *p);
			next_upper = false;
		}
	}
	return (char *)buf_str(&b);
}

/* RubifyConstant */
static char *rubify_constant(const char *name)
{
	char *ret = xstrdup(name);
	if (ret[0]) {
		if (is_lower(ret[0]))
			ret[0] = upper_char(ret[0]);
		else if (!is_alpha(ret[0]))
			return xcat("PB_", ret);
	}
	return ret;
}

static void generate_enum_assignment(const char *prefix, const LpEnum *en, LpPrinter *printer)
{
	lp_print(printer, "$prefix$$name$ = ", "prefix", prefix, "name", rubify_constant(en->name), NULL);
	lp_print(printer, "::Google::Protobuf::DescriptorPool.generated_pool.lookup(\"$full_name$\").enummodule\n",
		 "full_name", en->full_name, NULL);
}

static void generate_message_assignment(const char *prefix, const LpMessage *message, LpPrinter *printer)
{
	/* Don't generate MapEntry messages -- we use the Ruby extension's native support for map
	 * fields instead. */
	if (pb_int(message->options, MSGOPT_MAP_ENTRY, 0))
		return;
	lp_print(printer, "$prefix$$name$ = ", "prefix", prefix, "name", rubify_constant(message->name), NULL);
	lp_print(printer, "::Google::Protobuf::DescriptorPool.generated_pool.lookup(\"$full_name$\").msgclass\n",
		 "full_name", message->full_name, NULL);
	char *nested_prefix = xcat(prefix, rubify_constant(message->name), "::");
	for (int i = 0; i < message->n_nested; i++)
		generate_message_assignment(nested_prefix, &message->nested[i], printer);
	for (int i = 0; i < message->n_enums; i++)
		generate_enum_assignment(nested_prefix, &message->enums[i], printer);
}

/* GeneratePackageModules */
static int generate_package_modules(const LpFile *file, LpPrinter *printer)
{
	int levels = 0;
	bool need_change_to_module = true;
	const char *package_name;
	if (pb_has(file->options, FILEOPT_RUBY_PACKAGE)) {
		package_name = pb_str(file->options, FILEOPT_RUBY_PACKAGE);
		if (strstr(package_name, "::"))
			need_change_to_module = false;
		/* else if it contains '.': upstream logs a warning */
	} else {
		package_name = file->package;
	}
	const char *delimiter = need_change_to_module ? "." : "::";
	size_t delimiter_size = need_change_to_module ? 1 : 2;
	while (*package_name) {
		const char *dot = strstr(package_name, delimiter);
		char *component;
		if (!dot) {
			component = xstrdup(package_name);
			package_name = "";
		} else {
			component = xstrndup(package_name, (size_t)(dot - package_name));
			package_name = dot + delimiter_size;
		}
		if (need_change_to_module)
			component = package_to_module(component);
		lp_print(printer, "module $name$\n", "name", component, NULL);
		lp_indent(printer);
		levels++;
	}
	return levels;
}

/* EndPackageModules */
static void end_package_modules(int levels, LpPrinter *printer)
{
	while (levels > 0) {
		levels--;
		lp_outdent(printer);
		lp_print(printer, "end\n", NULL);
	}
}

/* SerializedDescriptor */
static Str serialized_descriptor(const LpFile *file)
{
	Buf b = {0};
	lp_msg_serialize(lp_strip_source_retention_options(file), &b);
	return (Str){b.p ? b.p : "", b.n};
}

/* RubyEscape: CHexEscape, and a backslash before '#' followed by '{', '$' or '@' */
static char *ruby_escape(Str s)
{
	const char *c_escaped = lp_chexescape(s);
	size_t n = strlen(c_escaped);
	Buf b = {0};
	for (size_t i = 0; i < n; i++) {
		if (c_escaped[i] == '#' && i + 1 < n &&
		    (c_escaped[i + 1] == '{' || c_escaped[i + 1] == '$' || c_escaped[i + 1] == '@'))
			buf_addc(&b, '\\');
		buf_addc(&b, c_escaped[i]);
	}
	return (char *)buf_str(&b);
}

/* GenerateBinaryDescriptor (the "imports" variable of upstream, DumpImportList, is unused) */
static void generate_binary_descriptor(const LpFile *file, LpPrinter *printer)
{
	lp_print(printer,
		 "\n"
		 "descriptor_data = \"$descriptor_data$\"\n"
		 "\n"
		 "pool = ::Google::Protobuf::DescriptorPool.generated_pool\n"
		 "pool.add_serialized_file(descriptor_data)\n"
		 "\n",
		 "descriptor_data", ruby_escape(serialized_descriptor(file)), NULL);
}

/* GenerateFile */
static bool generate_file(const LpFile *file, LpPrinter *printer, Buf *error)
{
	if (pb_has(file->options, FILEOPT_RUBY_PACKAGE) &&
	    !is_valid_ruby_package(pb_bytes(file->options, FILEOPT_RUBY_PACKAGE), error))
		return false;
	lp_print(printer,
		 "# frozen_string_literal: true\n"
		 "# Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		 "# source: $filename$\n"
		 "\n",
		 "filename", file->name, NULL);
	lp_print(printer, "require 'google/protobuf'\n\n", NULL);
	if (file->n_dependencies != 0) {
		for (int i = 0; i < file->n_dependencies; i++)
			lp_print(printer, "require '$name$'\n", "name", get_require_name(file->dependencies[i]->name), NULL);
		lp_print(printer, "\n", NULL);
	}
	generate_binary_descriptor(file, printer);
	int levels = generate_package_modules(file, printer);
	for (int i = 0; i < file->n_messages; i++)
		generate_message_assignment("", &file->messages[i], printer);
	for (int i = 0; i < file->n_enums; i++)
		generate_enum_assignment("", &file->enums[i], printer);
	end_package_modules(levels, printer);
	return true;
}

/* Generator::Generate */
static bool ruby_generate(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	(void)g;
	(void)parameter;
	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(ctx, get_output_filename(file->name)), '$');
	return generate_file(file, &printer, error);
}
