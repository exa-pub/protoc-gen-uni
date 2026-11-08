// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* Port of protoc-gen-c/c_helpers.{h,cc} (protobuf-c v1.5.2). The <ctype.h> functions upstream
 * calls on (signed) chars are those of the "C" locale: only ASCII letters and digits are
 * letters and digits, and only 0x20-0x7e is printable. */

static bool c_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static bool c_islower(char c) { return c >= 'a' && c <= 'z'; }
static bool c_isalnum(char c) { return c_isupper(c) || c_islower(c) || (c >= '0' && c <= '9'); }
static char c_toupper(char c) { return c_islower(c) ? (char)(c - 'a' + 'A') : c; }
static char c_tolower(char c) { return c_isupper(c) ? (char)(c - 'A' + 'a') : c; }

/* SimpleItoa (std::stringstream << n) */
static char *simple_itoa(int64_t n) { return xfmt("%lld", (long long)n); }
static char *simple_utoa(uint64_t n)
{
	char t[24];
	int i = 23;
	t[i] = 0;
	do
		t[--i] = (char)('0' + n % 10);
	while (n /= 10);
	return xstrdup(t + i);
}

/* snprintf("%.*g", digits, d) of glibc */
static char *format_g(double d, int digits)
{
	if (isnan(d))
		return xstrdup(signbit(d) ? "-nan" : "nan");
	if (isinf(d))
		return xstrdup(d < 0 ? "-inf" : "inf");
	return lp_format_g(d, digits);
}

/* SimpleFtoa: "%.*g" with FLT_DIG (6) digits */
static char *simple_ftoa(float f) { return format_g((double)f, 6); }
/* SimpleDtoa: "%.*g" with DBL_DIG (15) digits */
static char *simple_dtoa(double d) { return format_g(d, 15); }

/* CamelToUpper, CamelToLower */
static char *camel_to(const char *name, bool upper)
{
	bool was_upper = true; /* suppress initial _ */
	Buf rv = {0};
	for (const char *p = name; *p; p++) {
		bool is_upper = c_isupper(*p);
		if (is_upper) {
			if (!was_upper)
				buf_addc(&rv, '_');
			buf_addc(&rv, upper ? *p : c_tolower(*p));
		} else {
			buf_addc(&rv, upper ? c_toupper(*p) : *p);
		}
		was_upper = is_upper;
	}
	return (char *)buf_str(&rv);
}

static char *camel_to_upper(const char *name) { return camel_to(name, true); }
static char *camel_to_lower(const char *name) { return camel_to(name, false); }

static char *to_lower(const char *name)
{
	char *rv = xstrdup(name);
	for (char *p = rv; *p; p++)
		*p = c_tolower(*p);
	return rv;
}

/* ToCamel: lowercased, underscored name to camel case */
static char *to_camel(const char *name)
{
	Buf rv = {0};
	bool next_is_upper = true;
	for (const char *p = name; *p; p++) {
		if (*p == '_') {
			next_is_upper = true;
		} else if (next_is_upper) {
			buf_addc(&rv, c_toupper(*p));
			next_is_upper = false;
		} else {
			buf_addc(&rv, *p);
		}
	}
	return (char *)buf_str(&rv);
}

/* OverrideFullName */
static const char *override_full_name(const char *full_name, const LpFile *file)
{
	const PbcFileOptions *opt = pbc_file_options(file);
	if (!opt->has_c_package)
		return full_name;
	const char *package = file->package;
	return xcat(opt->c_package, *package ? "" : ".", full_name + strlen(package));
}

/* FullNameToLower, FullNameToUpper, FullNameToC: the pieces between dots (empty ones skipped,
 * like SplitStringUsing) converted and joined with "__". */
static char *full_name_to(const char *full_name, const LpFile *file, char *(*convert)(const char *))
{
	const char *s = override_full_name(full_name, file);
	Buf rv = {0};
	while (*s) {
		const char *dot = strchr(s, '.');
		size_t n = dot ? (size_t)(dot - s) : strlen(s);
		if (n) {
			if (rv.n)
				buf_adds(&rv, "__");
			buf_adds(&rv, convert(xstrndup(s, n)));
		}
		s += n;
		if (*s)
			s++;
	}
	return (char *)buf_str(&rv);
}

static char *full_name_to_lower(const char *full_name, const LpFile *file)
{
	return full_name_to(full_name, file, camel_to_lower);
}

static char *full_name_to_upper(const char *full_name, const LpFile *file)
{
	return full_name_to(full_name, file, camel_to_upper);
}

static char *full_name_to_c(const char *full_name, const LpFile *file) { return full_name_to(full_name, file, to_camel); }

/* PrintComment: the lines of the comment (split at any '\r' or '\n', empty ones skipped) in a
 * C block comment that they cannot close. */
static void print_comment(LpPrinter3 *printer, Str comment)
{
	if (!comment.n)
		return;
	lp3_print(printer, "/*\n", NULL);
	size_t i = 0;
	while (i < comment.n) {
		while (i < comment.n && (comment.p[i] == '\r' || comment.p[i] == '\n'))
			i++;
		size_t start = i;
		while (i < comment.n && comment.p[i] != '\r' && comment.p[i] != '\n')
			i++;
		if (i == start)
			break;
		Buf line = {0};
		/* Make sure we don't inadvertently close the comment block */
		if (comment.p[start] == '/')
			buf_addc(&line, ' ');
		buf_add(&line, comment.p + start, i - start);
		/* Or cause other compiler issues. */
		for (size_t k = 0; k + 1 < line.n; k++)
			if (line.p[k] == '/' && line.p[k + 1] == '*')
				line.p[k] = ' ';
		for (size_t k = 0; k + 1 < line.n; k++)
			if (line.p[k] == '*' && line.p[k + 1] == '/')
				line.p[k + 1] = ' ';
		LpVar3 v = {"line", buf_view(&line)};
		LpVars3 vars = {&v, 1, 1};
		lp3_print_vars(printer, &vars, " *$line$\n");
	}
	lp3_print(printer, " */\n", NULL);
}

/* ConvertToSpaces: a string of spaces as long as input */
static char *convert_to_spaces(const char *input)
{
	size_t n = strlen(input);
	char *rv = xalloc(n + 1);
	memset(rv, ' ', n);
	return rv;
}

/* kKeywordList */
static const char *const keyword_list[] = {
	"and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
	"catch", "char", "class", "compl", "const", "const_cast", "continue",
	"default", "delete", "do", "double", "dynamic_cast", "else", "enum",
	"explicit", "extern", "false", "float", "for", "friend", "goto", "if",
	"inline", "int", "long", "mutable", "namespace", "new", "not", "not_eq",
	"operator", "or", "or_eq", "private", "protected", "public", "register",
	"reinterpret_cast", "return", "short", "signed", "sizeof", "static",
	"static_cast", "struct", "switch", "template", "this", "throw", "true", "try",
	"typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
	"void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

/* FieldName: the name lower-cased, with "_" appended to C++ keywords */
static char *field_name(const LpField *field)
{
	char *result = to_lower(field->name);
	for (size_t i = 0; i < sizeof keyword_list / sizeof *keyword_list; i++)
		if (strcmp(result, keyword_list[i]) == 0)
			return xcat(result, "_");
	return result;
}

/* FieldDeprecated */
static const char *field_deprecated(const LpField *field)
{
	return pb_int(field->options, FOPT_DEPRECATED, 0) ? " PROTOBUF_C__DEPRECATED" : "";
}

/* StripProto */
static char *strip_proto(const char *filename) { return lp_strip_proto(filename); }

/* FilenameIdentifier: a file name as a valid identifier (other bytes as _ and their hex code) */
static char *filename_identifier(const char *filename)
{
	Buf result = {0};
	for (const char *p = filename; *p; p++) {
		if (c_isalnum(*p))
			buf_addc(&result, *p);
		else
			buf_fmt(&result, "_%x", (unsigned)(uint8_t)*p);
	}
	return (char *)buf_str(&result);
}

/* GetLabelName */
static const char *get_label_name(int label)
{
	switch (label) {
	case LABEL_OPTIONAL: return "optional";
	case LABEL_REQUIRED: return "required";
	case LABEL_REPEATED: return "repeated";
	}
	return "bad-label";
}

/* WriteIntRanges: IntRanges entries for sorted values; returns the number of ranges. */
static unsigned write_int_ranges(LpPrinter3 *printer, int n_values, const int *values, const char *name)
{
	LpVars3 vars = {0};
	lp3_vars_set(&vars, "name", name);
	if (n_values > 0) {
		int n_ranges = 1;
		for (int i = 1; i < n_values; i++)
			if (values[i - 1] + 1 != values[i])
				n_ranges++;
		lp3_vars_set(&vars, "n_ranges", simple_itoa(n_ranges));
		lp3_print_vars(printer, &vars,
			       "static const ProtobufCIntRange $name$[$n_ranges$ + 1] =\n"
			       "{\n");
		int last_range_start = 0;
		for (int i = 1; i < n_values; i++) {
			if (values[i - 1] + 1 != values[i]) {
				int count = i - last_range_start;
				int expected = values[i - 1] + 1;
				lp3_vars_set(&vars, "start_value", simple_itoa(expected - count));
				lp3_vars_set(&vars, "orig_offset", simple_itoa(last_range_start));
				lp3_print_vars(printer, &vars, "  { $start_value$, $orig_offset$ },\n");
				last_range_start = i;
			}
		}
		/* write last real entry */
		{
			int i = n_values;
			int count = i - last_range_start;
			int expected = values[i - 1] + 1;
			lp3_vars_set(&vars, "start_value", simple_itoa(expected - count));
			lp3_vars_set(&vars, "orig_offset", simple_itoa(last_range_start));
			lp3_print_vars(printer, &vars, "  { $start_value$, $orig_offset$ },\n");
		}
		/* write sentinel entry */
		lp3_vars_set(&vars, "n_entries", simple_itoa(n_values));
		lp3_print_vars(printer, &vars, "  { 0, $n_entries$ }\n");
		lp3_print_vars(printer, &vars, "};\n");
		return (unsigned)n_ranges;
	}
	lp3_print_vars(printer, &vars, "#define $name$ NULL\n");
	return 0;
}

/* CEscape (CEscapeInternal without hex): octal escapes for the bytes that are not printable */
static char *c_escape(Str src)
{
	Buf dest = {0};
	for (size_t i = 0; i < src.n; i++) {
		char c = src.p[i];
		switch (c) {
		case '\n': buf_adds(&dest, "\\n"); break;
		case '\r': buf_adds(&dest, "\\r"); break;
		case '\t': buf_adds(&dest, "\\t"); break;
		case '\"': buf_adds(&dest, "\\\""); break;
		case '\'': buf_adds(&dest, "\\'"); break;
		case '\\': buf_adds(&dest, "\\\\"); break;
		default:
			if (c < 0x20 || c > 0x7e) {
				uint8_t u = (uint8_t)c;
				buf_addc(&dest, '\\');
				buf_addc(&dest, (char)('0' + (u >> 6)));
				buf_addc(&dest, (char)('0' + ((u >> 3) & 7)));
				buf_addc(&dest, (char)('0' + (u & 7)));
			} else
				buf_addc(&dest, c);
		}
	}
	return (char *)buf_str(&dest);
}

/* FieldSyntax: 3 for a field of a proto3 file (FileDescriptor::CopyTo of v3.21.12 sets the
 * syntax only for proto3), else 2. */
static int field_syntax(const LpField *field) { return field->file->edition == LP_EDITION_PROTO3 ? 3 : 2; }

/* file->options().has_optimize_for() && optimize_for() == CODE_SIZE */
static bool optimize_code_size(const LpFile *file)
{
	return pb_has(file->options, FILEOPT_OPTIMIZE_FOR) &&
	       pb_int(file->options, FILEOPT_OPTIMIZE_FOR, 0) == FILEOPT_CODE_SIZE;
}

/* GetSourceLocation; the locations of the last file asked for are kept (until c_generate resets
 * the cache). */
static void source_comments(const void *descriptor, Str *leading, Str *trailing)
{
	const LpFile *f = proto_desc_file(descriptor);
	if (f != cache.locations_file) {
		cache.locations_file = f;
		cache.locations = lp_source_locations(f);
	}
	LpSourceLocation loc = {0};
	if (!lp_get_source_location(cache.locations, descriptor, &loc)) {
		*leading = *trailing = (Str){"", 0};
		return;
	}
	*leading = loc.leading_comments;
	*trailing = loc.trailing_comments;
}
