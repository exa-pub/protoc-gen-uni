// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/doc_comment.{h,cc}. */

char *jv_escape_javadoc(Str input)
{
	Buf result = {0};
	char prev = '*';
	for (size_t i = 0; i < input.n; i++) {
		char c = input.p[i];
		switch (c) {
		case '*':
			if (prev == '/')
				buf_adds(&result, "&#42;");
			else
				buf_addc(&result, c);
			break;
		case '/':
			if (prev == '*')
				buf_adds(&result, "&#47;");
			else
				buf_addc(&result, c);
			break;
		case '@': buf_adds(&result, "&#64;"); break;
		case '<': buf_adds(&result, "&lt;"); break;
		case '>': buf_adds(&result, "&gt;"); break;
		case '&': buf_adds(&result, "&amp;"); break;
		case '\\': buf_adds(&result, "&#92;"); break;
		default: buf_addc(&result, c); break;
		}
		prev = c;
	}
	return (char *)buf_str(&result);
}

static char *escape_kdoc(Str input)
{
	Buf result = {0};
	char prev = 'a';
	for (size_t i = 0; i < input.n; i++) {
		char c = input.p[i];
		switch (c) {
		case '*':
			if (prev == '/')
				buf_adds(&result, "&#42;");
			else
				buf_addc(&result, c);
			break;
		case '/':
			if (prev == '*')
				buf_adds(&result, "&#47;");
			else
				buf_addc(&result, c);
			break;
		default: buf_addc(&result, c); break;
		}
		prev = c;
	}
	return (char *)buf_str(&result);
}

/* std::isspace in the C locale */
static bool dc_isspace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static void write_doc_comment_body_for_location(LpPrinter *p, const LpSourceLocation *location,
						const JvOptions *options, bool kdoc)
{
	if (options->strip_nonfunctional_codegen)
		return;
	Str comments = location->leading_comments.n ? location->leading_comments : location->trailing_comments;
	if (!comments.n)
		return;
	/* EscapeKdoc / EscapeJavadoc, keeping NUL bytes (the lines are split on the Str) */
	Buf esc = {0};
	{
		Str in = comments;
		char prev = kdoc ? 'a' : '*';
		for (size_t i = 0; i < in.n; i++) {
			char c = in.p[i];
			const char *rep = NULL;
			if (c == '*' && prev == '/')
				rep = "&#42;";
			else if (c == '/' && prev == '*')
				rep = "&#47;";
			else if (!kdoc) {
				switch (c) {
				case '@': rep = "&#64;"; break;
				case '<': rep = "&lt;"; break;
				case '>': rep = "&gt;"; break;
				case '&': rep = "&amp;"; break;
				case '\\': rep = "&#92;"; break;
				default: break;
				}
			}
			if (rep)
				buf_adds(&esc, rep);
			else
				buf_addc(&esc, c);
			prev = c;
		}
	}
	Str all = buf_view(&esc);
	Str *lines;
	int n = str_split_s(all, '\n', &lines);
	while (n > 0 && lines[n - 1].n == 0)
		n--;
	lp_print(p, kdoc ? " * ```\n" : " * <pre>\n", NULL);
	for (int i = 0; i < n; i++) {
		Str line = lines[i];
		size_t k = 0;
		while (k < line.n && dc_isspace((unsigned char)line.p[k]))
			k++;
		line.p += k;
		line.n -= k;
		if (line.n) {
			/* the value is printed up to a NUL byte like any std::string through Print */
			Buf l = {0};
			buf_add(&l, line.p, line.n);
			lp_printv(p, " * $line$\n", 1, (const char *[]){"line", buf_str(&l)});
		} else {
			lp_print(p, " *\n", NULL);
		}
	}
	lp_print(p, kdoc ? " * ```\n" : " * </pre>\n", NULL);
	lp_print(p, " *\n", NULL);
}

static void write_doc_comment_body(LpPrinter *p, const ProtoDesc *descriptor, const JvOptions *options, bool kdoc)
{
	LpSourceLocation location;
	if (jv_get_source_location(descriptor, &location))
		write_doc_comment_body_for_location(p, &location, options, kdoc);
}

/* FirstLineOf(DebugString()) */
static char *first_line_of(char *line)
{
	size_t n = strlen(line);
	if (n && line[n - 1] == '{')
		return xcat(line, " ... }");
	return line;
}

static void write_debug_string(LpPrinter *p, const LpField *field, const JvOptions *options, bool kdoc)
{
	char *field_comment = first_line_of(lp_field_debug_string_first_line(field));
	if (options->strip_nonfunctional_codegen)
		field_comment = (char *)field->name;
	if (kdoc)
		lp_print(p, " * `$def$`\n", "def", escape_kdoc(str_c(field_comment)), NULL);
	else
		lp_print(p, " * <code>$def$</code>\n", "def", jv_escape_javadoc(str_c(field_comment)), NULL);
}

void jv_write_message_doc_comment(LpPrinter *p, const LpMessage *message, const JvOptions *options, bool kdoc)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)message, options, kdoc);
	if (kdoc)
		lp_print(p,
			 " * Protobuf type `$fullname$`\n"
			 " */\n",
			 "fullname", escape_kdoc(str_c(message->full_name)), NULL);
	else
		lp_print(p,
			 " * Protobuf type {@code $fullname$}\n"
			 " */\n",
			 "fullname", jv_escape_javadoc(str_c(message->full_name)), NULL);
}

void jv_write_field_doc_comment(LpPrinter *p, const LpField *field, const JvOptions *options, bool kdoc)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)field, options, kdoc);
	write_debug_string(p, field, options, kdoc);
	lp_print(p, " */\n", NULL);
}

static void write_deprecated_javadoc(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
				     const JvOptions *options)
{
	(void)type;
	if (!jv_field_deprecated(field))
		return;
	char *start_line = "0";
	LpSourceLocation location;
	if (jv_get_source_location((const ProtoDesc *)field, &location))
		start_line = jv_itoa(location.start_line);
	lp_print(p, " * @deprecated $name$ is deprecated.\n", "name", field->full_name, NULL);
	if (!options->strip_nonfunctional_codegen)
		lp_print(p, " *     See $file$;l=$line$\n", "file", field->file->name, "line", start_line, NULL);
}

static bool is_open_enum_field(const LpField *field)
{
	return field->enum_type != NULL && !proto_enum_is_closed(field->enum_type);
}

void jv_write_field_accessor_doc_comment(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
					 const JvOptions *options, bool builder, bool kdoc, bool is_private)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)field, options, kdoc);
	write_debug_string(p, field, options, kdoc);
	if (!kdoc && !is_private)
		write_deprecated_javadoc(p, field, type, options);
	const char *name = lp_field_camelcase_name(field);
	switch (type) {
	case JV_HAZZER: lp_print(p, " * @return Whether the $name$ field is set.\n", "name", name, NULL); break;
	case JV_GETTER: lp_print(p, " * @return The $name$.\n", "name", name, NULL); break;
	case JV_SETTER:
		lp_print(p, " * @param value The $name$ to set.\n", "name", name, NULL);
		if (is_open_enum_field(field))
			lp_print(p, " * @throws IllegalArgumentException if UNRECOGNIZED is provided.\n", NULL);
		break;
	case JV_CLEARER: break;
	case JV_LIST_COUNT: lp_print(p, " * @return The count of $name$.\n", "name", name, NULL); break;
	case JV_LIST_GETTER: lp_print(p, " * @return A list containing the $name$.\n", "name", name, NULL); break;
	case JV_LIST_INDEXED_GETTER:
		lp_print(p, " * @param index The index of the element to return.\n", NULL);
		lp_print(p, " * @return The $name$ at the given index.\n", "name", name, NULL);
		break;
	case JV_LIST_INDEXED_SETTER:
		lp_print(p, " * @param index The index to set the value at.\n", NULL);
		lp_print(p, " * @param value The $name$ to set.\n", "name", name, NULL);
		if (is_open_enum_field(field))
			lp_print(p, " * @throws IllegalArgumentException if UNRECOGNIZED is provided.\n", NULL);
		break;
	case JV_LIST_ADDER: lp_print(p, " * @param value The $name$ to add.\n", "name", name, NULL); break;
	case JV_LIST_MULTI_ADDER:
		lp_print(p, " * @param values The $name$ to add.\n", "name", name, NULL);
		if (is_open_enum_field(field))
			lp_print(p, " * @throws IllegalArgumentException if UNRECOGNIZED is provided.\n", NULL);
		break;
	}
	if (builder)
		lp_print(p, " * @return This builder for chaining.\n", NULL);
	lp_print(p, " */\n", NULL);
}

void jv_write_field_enum_value_accessor_doc_comment(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
						    const JvOptions *options, bool builder, bool is_private)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)field, options, false);
	write_debug_string(p, field, options, false);
	if (!is_private)
		write_deprecated_javadoc(p, field, type, options);
	const char *name = lp_field_camelcase_name(field);
	switch (type) {
	case JV_HAZZER: break;
	case JV_GETTER:
		lp_print(p, " * @return The enum numeric value on the wire for $name$.\n", "name", name, NULL);
		break;
	case JV_SETTER:
		lp_print(p, " * @param value The enum numeric value on the wire for $name$ to set.\n", "name", name, NULL);
		break;
	case JV_CLEARER: break;
	case JV_LIST_COUNT: break;
	case JV_LIST_GETTER:
		lp_print(p, " * @return A list containing the enum numeric values on the wire for $name$.\n", "name",
			 name, NULL);
		break;
	case JV_LIST_INDEXED_GETTER:
		lp_print(p, " * @param index The index of the value to return.\n", NULL);
		lp_print(p, " * @return The enum numeric value on the wire of $name$ at the given index.\n", "name", name,
			 NULL);
		break;
	case JV_LIST_INDEXED_SETTER:
		lp_print(p, " * @param index The index to set the value at.\n", NULL);
		lp_print(p, " * @param value The enum numeric value on the wire for $name$ to set.\n", "name", name, NULL);
		break;
	case JV_LIST_ADDER:
		lp_print(p, " * @param value The enum numeric value on the wire for $name$ to add.\n", "name", name, NULL);
		break;
	case JV_LIST_MULTI_ADDER:
		lp_print(p, " * @param values The enum numeric values on the wire for $name$ to add.\n", "name", name,
			 NULL);
		break;
	}
	if (builder)
		lp_print(p, " * @return This builder for chaining.\n", NULL);
	lp_print(p, " */\n", NULL);
}

void jv_write_field_string_bytes_accessor_doc_comment(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
						      const JvOptions *options, bool builder, bool kdoc,
						      bool is_private)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)field, options, kdoc);
	write_debug_string(p, field, options, kdoc);
	if (!kdoc && !is_private)
		write_deprecated_javadoc(p, field, type, options);
	const char *name = lp_field_camelcase_name(field);
	switch (type) {
	case JV_HAZZER: break;
	case JV_GETTER: lp_print(p, " * @return The bytes for $name$.\n", "name", name, NULL); break;
	case JV_SETTER: lp_print(p, " * @param value The bytes for $name$ to set.\n", "name", name, NULL); break;
	case JV_CLEARER: break;
	case JV_LIST_COUNT: break;
	case JV_LIST_GETTER:
		lp_print(p, " * @return A list containing the bytes for $name$.\n", "name", name, NULL);
		break;
	case JV_LIST_INDEXED_GETTER:
		lp_print(p, " * @param index The index of the value to return.\n", NULL);
		lp_print(p, " * @return The bytes of the $name$ at the given index.\n", "name", name, NULL);
		break;
	case JV_LIST_INDEXED_SETTER:
		lp_print(p, " * @param index The index to set the value at.\n", NULL);
		lp_print(p, " * @param value The bytes of the $name$ to set.\n", "name", name, NULL);
		break;
	case JV_LIST_ADDER: lp_print(p, " * @param value The bytes of the $name$ to add.\n", "name", name, NULL); break;
	case JV_LIST_MULTI_ADDER:
		lp_print(p, " * @param values The bytes of the $name$ to add.\n", "name", name, NULL);
		break;
	}
	if (builder)
		lp_print(p, " * @return This builder for chaining.\n", NULL);
	lp_print(p, " */\n", NULL);
}

void jv_write_enum_doc_comment(LpPrinter *p, const LpEnum *enum_, const JvOptions *options, bool kdoc)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)enum_, options, kdoc);
	if (kdoc)
		lp_print(p,
			 " * Protobuf enum `$fullname$`\n"
			 " */\n",
			 "fullname", escape_kdoc(str_c(enum_->full_name)), NULL);
	else
		lp_print(p,
			 " * Protobuf enum {@code $fullname$}\n"
			 " */\n",
			 "fullname", jv_escape_javadoc(str_c(enum_->full_name)), NULL);
}

void jv_write_enum_value_doc_comment(LpPrinter *p, const LpEnumValue *value, const JvOptions *options)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)value, options, false);
	if (!options->strip_nonfunctional_codegen)
		lp_print(p,
			 " * <code>$def$</code>\n"
			 " */\n",
			 "def", jv_escape_javadoc(str_c(first_line_of(lp_enum_value_debug_string_first_line(value)))),
			 NULL);
}

void jv_write_service_doc_comment(LpPrinter *p, const LpService *service, const JvOptions *options)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)service, options, false);
	lp_print(p,
		 " * Protobuf service {@code $fullname$}\n"
		 " */\n",
		 "fullname", jv_escape_javadoc(str_c(service->full_name)), NULL);
}

void jv_write_method_doc_comment(LpPrinter *p, const LpMethod *method, const JvOptions *options)
{
	lp_print(p, "/**\n", NULL);
	write_doc_comment_body(p, (const ProtoDesc *)method, options, false);
	lp_print(p,
		 " * <code>$def$</code>\n"
		 " */\n",
		 "def", jv_escape_javadoc(str_c(first_line_of(lp_method_debug_string_first_line(method)))), NULL);
}
