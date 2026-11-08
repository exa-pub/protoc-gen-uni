// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://protobuf.dev/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/* Port of generator/js_generator.{h,cc} of protobuf-javascript v4.0.3
 * (https://github.com/protocolbuffers/protobuf-javascript, tag v4.0.3). The C++ sets of strings
 * (std::set) are StrSet, iterated in byte order; the maps keyed by descriptor pointers are only
 * looked up. The functions keep the upstream names in snake case. */

/* GeneratorOptions::ImportStyle */
enum { IMPORT_CLOSURE, IMPORT_COMMONJS, IMPORT_COMMONJS_STRICT, IMPORT_BROWSER, IMPORT_ES6 };
/* GeneratorOptions::OutputMode */
enum { ONE_OUTPUT_FILE_PER_INPUT_FILE, ONE_OUTPUT_FILE_PER_SCC, EVERYTHING_IN_ONE_FILE };

/* GeneratorOptions */
typedef struct {
	const char *output_dir;       /* Output path. */
	const char *namespace_prefix; /* Namespace prefix. */
	bool binary;                  /* Enable binary-format support? */
	int import_style;             /* What style of imports should be used. */
	/* The remaining options are only relevant when we are using kImportClosure. */
	bool add_require_for_enums;
	bool testonly;
	const char *library;
	const char *extension;
	bool one_output_file_per_input_file;
	bool annotate_code;
} JsOptions;

/* The mode of operation for bytes fields. */
enum { BYTES_DEFAULT, BYTES_B64, BYTES_U8 };

/* ---- std::set<std::string> ---- */

typedef struct {
	Map m;
	Vec v;
} StrSet;

static bool strset_insert(StrSet *s, const char *str)
{
	if (map_has(&s->m, str))
		return false;
	map_put(&s->m, str, (void *)str);
	vec_push(&s->v, (void *)str);
	return true;
}

static bool strset_has(const StrSet *s, const char *str) { return map_has(&s->m, str); }

static int strset_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* The elements in std::set order (byte order). */
static const char **strset_sorted(const StrSet *s)
{
	const char **v = xalloc(sizeof(char *) * (size_t)(s->v.n + 1));
	if (s->v.n) {
		memcpy(v, s->v.v, sizeof(char *) * (size_t)s->v.n);
		qsort(v, (size_t)s->v.n, sizeof(char *), strset_cmp);
	}
	return v;
}

/* A map key for a pointer (std::map<const void*, ...>, lookups only). */
static const char *ptr_key(const void *p) { return xfmt("%llx", (unsigned long long)(uintptr_t)p); }

/* Results of JSFieldIndex, JSOneofIndex and HasOneofFields per message or oneof: upstream
 * recomputes them in time linear in the message for every field, which makes large messages
 * quadratic. Set by js_generate_all for the request (arena memory). */
typedef struct {
	Map group_field; /* ptr_key(message) -> the group field of its parent + 1 ((void *)1: none) */
	Map oneof_index; /* ptr_key(oneof) -> index + 2 */
	Map has_oneof;   /* ptr_key(message) -> 1 or 2 */
} JsCache;
static JsCache *js_cache;

/* ---- names ---- */

/* Sorted list of JavaScript keywords. These cannot be used as names. If they
 * appear, we prefix them with "pb_". */
static const char *const kKeyword[] = {
	"abstract",   "boolean",      "break",      "byte",    "case",
	"catch",      "char",         "class",      "const",   "continue",
	"debugger",   "default",      "delete",     "do",      "double",
	"else",       "enum",         "export",     "extends", "false",
	"final",      "finally",      "float",      "for",     "function",
	"goto",       "if",           "implements", "import",  "in",
	"instanceof", "int",          "interface",  "long",    "native",
	"new",        "null",         "package",    "private", "protected",
	"public",     "return",       "short",      "static",  "super",
	"switch",     "synchronized", "this",       "throw",   "throws",
	"transient",  "try",          "typeof",     "var",     "void",
	"volatile",   "while",        "with",
};

static bool is_reserved(const char *ident)
{
	for (size_t i = 0; i < sizeof kKeyword / sizeof *kKeyword; i++)
		if (str_eq(ident, kKeyword[i]))
			return true;
	return false;
}

static char *get_snake_filename(const char *filename) { return str_replace_all(filename, "/", "_"); }

/* GeneratorOptions::GetFileNameExtension */
static const char *file_name_extension(const JsOptions *o)
{
	return o->import_style == IMPORT_CLOSURE ? o->extension : "_pb.js";
}

/* Given a filename like foo/bar/baz.proto, returns the corresponding JavaScript
 * file foo/bar/baz.js. */
static char *get_js_filename(const JsOptions *o, const char *filename)
{
	return xcat(lp_strip_proto(filename), file_name_extension(o));
}

/* Given a filename like foo/bar/baz.proto, returns the root directory
 * path ../../ */
static char *get_root_path(const char *from_filename, const char *to_filename)
{
	if (has_prefix(to_filename, "google/protobuf"))
		/* Well-known types (.proto files in the google/protobuf directory) are
		 * assumed to come from the 'google-protobuf' npm package. */
		return "google-protobuf/";
	size_t slashes = 0;
	for (const char *c = from_filename; *c; c++)
		slashes += *c == '/';
	if (slashes == 0)
		return "./";
	Buf b = {0};
	for (size_t i = 0; i < slashes; i++)
		buf_adds(&b, "../");
	return (char *)buf_str(&b);
}

/* Returns the alias we assign to the module of the given .proto filename
 * when importing. */
static char *module_alias(const char *filename)
{
	char *basename = lp_strip_proto(filename);
	basename = str_replace_all(basename, "-", "$");
	basename = str_replace_all(basename, "/", "_");
	basename = str_replace_all(basename, ".", "_");
	return xcat(basename, "_pb");
}

/* Returns the fully normalized JavaScript namespace for the given
 * file descriptor's package. */
static const char *get_namespace(const JsOptions *o, const LpFile *file)
{
	if (*o->namespace_prefix)
		return o->namespace_prefix;
	if (*file->package)
		return xcat("proto.", file->package);
	return "proto";
}

/* Returns the name of the message with a leading dot and taking into account
 * nesting, for example ".OuterMessage.InnerMessage", or returns empty if
 * descriptor is null. */
static const char *get_nested_message_name(const LpMessage *descriptor)
{
	if (!descriptor)
		return "";
	const char *result = descriptor->full_name;
	if (has_prefix(result, descriptor->file->package))
		result += strlen(descriptor->file->package);
	if (*result && *result != '.')
		return xcat(".", result);
	return result;
}

/* Returns the path prefix for a message or enumeration that
 * lives under the given file and containing type. */
static char *get_prefix(const JsOptions *o, const LpFile *file, const LpMessage *containing_type)
{
	char *prefix = xcat(get_namespace(o, file), get_nested_message_name(containing_type));
	if (*prefix)
		prefix = xcat(prefix, ".");
	return prefix;
}

static char *get_message_path_prefix(const JsOptions *o, const LpMessage *d)
{
	return get_prefix(o, d->file, d->containing_type);
}

static char *get_message_path(const JsOptions *o, const LpMessage *d)
{
	return xcat(get_message_path_prefix(o, d), d->name);
}

static char *get_enum_path_prefix(const JsOptions *o, const LpEnum *e) { return get_prefix(o, e->file, e->containing_type); }

static char *get_enum_path(const JsOptions *o, const LpEnum *e) { return xcat(get_enum_path_prefix(o, e), e->name); }

static char *maybe_cross_file_ref(const JsOptions *o, const LpFile *from_file, const LpMessage *to_message)
{
	if ((o->import_style == IMPORT_COMMONJS || o->import_style == IMPORT_COMMONJS_STRICT) &&
	    from_file != to_message->file)
		/* Cross-file ref in CommonJS needs to use the module alias instead of
		 * the global name. */
		return xcat(module_alias(to_message->file->name), get_nested_message_name(to_message->containing_type), ".",
			    to_message->name);
	/* Within a single file we use a full name. */
	return get_message_path(o, to_message);
}

static char *submessage_type_ref(const JsOptions *o, const LpField *field)
{
	return maybe_cross_file_ref(o, field->file, field->message_type);
}

/* Locale-independent version of ToLower that deals only with ASCII A-Z. */
static char to_lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* ParseLowerUnderscore and ParseUpperCamel */
static Vec parse_lower_underscore(const char *input)
{
	Vec words = {0};
	Buf running = {0};
	for (const char *c = input; *c; c++) {
		if (*c == '_') {
			if (running.n) {
				vec_push(&words, xstrndup(running.p, running.n));
				running.n = 0;
			}
		} else {
			buf_addc(&running, to_lower_ascii(*c));
		}
	}
	if (running.n)
		vec_push(&words, xstrndup(running.p, running.n));
	return words;
}

static Vec parse_upper_camel(const char *input)
{
	Vec words = {0};
	Buf running = {0};
	for (const char *c = input; *c; c++) {
		if (*c >= 'A' && *c <= 'Z' && running.n) {
			vec_push(&words, xstrndup(running.p, running.n));
			running.n = 0;
		}
		buf_addc(&running, to_lower_ascii(*c));
	}
	if (running.n)
		vec_push(&words, xstrndup(running.p, running.n));
	return words;
}

static char *to_lower_camel(Vec words)
{
	Buf result = {0};
	for (int i = 0; i < words.n; i++) {
		char *word = words.v[i];
		if (i == 0 && word[0] >= 'A' && word[0] <= 'Z')
			word[0] = (char)(word[0] - 'A' + 'a');
		else if (i > 0 && word[0] >= 'a' && word[0] <= 'z')
			word[0] = (char)(word[0] - 'a' + 'A');
		buf_adds(&result, word);
	}
	return xstrdup(buf_str(&result));
}

static char *to_upper_camel(Vec words)
{
	Buf result = {0};
	for (int i = 0; i < words.n; i++) {
		char *word = words.v[i];
		if (word[0] >= 'a' && word[0] <= 'z')
			word[0] = (char)(word[0] - 'a' + 'A');
		buf_adds(&result, word);
	}
	return xstrdup(buf_str(&result));
}

/* Uppercases the entire string, turning ValueName into VALUENAME. */
static char *to_enum_case(const char *input)
{
	char *r = xstrdup(input);
	for (char *c = r; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c = (char)(*c - 'a' + 'A');
	return r;
}

static char *to_lower(const char *input)
{
	char *r = xstrdup(input);
	for (char *c = r; *c; c++)
		if (*c >= 'A' && *c <= 'Z')
			*c = (char)(*c - 'A' + 'a');
	return r;
}

/* When we're generating one output file per SCC, this is the filename
 * that top-level extensions should go in. */
static char *get_extension_file_name(const JsOptions *o, const LpFile *file, bool with_filename)
{
	char *snake_name = lp_strip_proto(get_snake_filename(file->name));
	return xcat(o->output_dir, "/", to_lower(get_namespace(o, file)),
		    with_filename ? xcat("_", snake_name, "_extensions") : "", file_name_extension(o));
}

/* The state of GenerateAll shared by its helpers: the static long_name_dict of
 * GetMessagesFileName. */
typedef struct {
	Map long_name_dict; /* representative -> name */
	int long_name_n;
} JsState;

/* When we're generating one output file per SCC, this is the filename
 * that all messages in the SCC should go in. */
static char *get_messages_file_name(JsState *st, const JsOptions *o, const LpScc *scc, bool with_package)
{
	const LpMessage *rep = lp_scc_representative(scc);
	const char *package_base = with_package ? to_lower(xcat(get_namespace(o, rep->file), "_")) : "";
	Vec all_message_names = {0};
	for (int i = 0; i < scc->n; i++)
		if (!scc->descriptors[i]->containing_type)
			vec_push(&all_message_names, to_lower(scc->descriptors[i]->name));
	if (all_message_names.n)
		qsort(all_message_names.v, (size_t)all_message_names.n, sizeof(char *), strset_cmp);
	Buf filename_base = {0};
	for (int i = 0; i < all_message_names.n; i++) {
		if (filename_base.n)
			buf_addc(&filename_base, '_');
		buf_adds(&filename_base, all_message_names.v[i]);
	}
	const char *base = buf_str(&filename_base);
	if (filename_base.n + strlen(package_base) > 200) {
		const char *key = ptr_key(rep);
		if (!map_has(&st->long_name_dict, key)) {
			char *snake_name = lp_strip_proto(get_snake_filename(rep->file->name));
			/* `dict[rep] = StrCat(..., dict.size())`: C++17 evaluates the right operand
			 * first, so the size before the insertion */
			map_put(&st->long_name_dict, key,
				xfmt("%s_long_sccs_%llu", snake_name, (unsigned long long)st->long_name_n));
			st->long_name_n++;
		}
		base = map_get(&st->long_name_dict, key);
	}
	return xcat(o->output_dir, "/", package_base, base, file_name_extension(o));
}

/* When we're generating one output file per type name, this is the filename
 * that a top-level enum should go in. */
static char *get_enum_file_name(const JsOptions *o, const LpEnum *desc, bool with_package)
{
	return xcat(o->output_dir, "/", with_package ? to_lower(xcat(get_namespace(o, desc->file), "_")) : "",
		    to_lower(desc->name), file_name_extension(o));
}

/* IgnoreField: no-op in open source; IgnoreMessage: map entries. */
static bool ignore_message(const LpMessage *d) { return proto_message_is_map_entry(d); }

static bool is_synthetic_oneof(const LpOneof *oneof)
{
	return oneof->n_fields == 1 && !proto_field_real_oneof(proto_oneof_field(oneof, 0));
}

/* Does JSPB ignore this entire oneof? True only if all fields are ignored. */
static bool ignore_oneof(const LpOneof *oneof)
{
	if (is_synthetic_oneof(oneof))
		return true;
	return oneof->n_fields == 0;
}

static char *js_ident(const LpField *field, bool is_upper_camel, bool is_map, bool drop_list)
{
	char *result;
	if (field->type == PT_GROUP)
		result = is_upper_camel ? to_upper_camel(parse_upper_camel(field->message_type->name))
					: to_lower_camel(parse_upper_camel(field->message_type->name));
	else
		result = is_upper_camel ? to_upper_camel(parse_lower_underscore(field->name))
					: to_lower_camel(parse_lower_underscore(field->name));
	if (is_map || proto_field_is_map(field))
		result = xcat(result, "Map"); /* JSPB-style or proto3-style map. */
	else if (!drop_list && proto_field_is_repeated(field))
		result = xcat(result, "List"); /* Repeated field. */
	return result;
}

static char *js_object_field_name(const LpField *field)
{
	char *name = js_ident(field, false, false, false);
	if (is_reserved(name))
		name = xcat("pb_", name);
	return name;
}

static const char *js_byte_getter_suffix(int bytes_mode)
{
	switch (bytes_mode) {
	case BYTES_B64: return "B64";
	case BYTES_U8: return "U8";
	default: return "";
	}
}

/* Returns the field name as a capitalized portion of a getter/setter method
 * name, e.g. MyField for .getMyField(). */
static char *js_getter_name_ex(const LpField *field, int bytes_mode, bool drop_list)
{
	char *name = js_ident(field, true, false, drop_list);
	if (field->type == PT_BYTES) {
		const char *suffix = js_byte_getter_suffix(bytes_mode);
		if (*suffix)
			name = xcat(name, "_as", suffix);
	}
	if (str_eq(name, "Extension") || str_eq(name, "JsPbMessageId"))
		name = xcat(name, "$"); /* Avoid conflicts with base-class names. */
	return name;
}

static char *js_getter_name(const LpField *field) { return js_getter_name_ex(field, BYTES_DEFAULT, false); }

static char *js_oneof_name(const LpOneof *oneof) { return to_upper_camel(parse_lower_underscore(oneof->name)); }

static char *itoa_js(int64_t v) { return xfmt("%lld", (long long)v); }

/* Returns the index corresponding to this field in the JSPB array (underlying
 * data storage array). */
static char *js_field_index(const LpField *field)
{
	/* For fields of a group, the index is relative to the field number of the group
	 * submessage field (the first one of the parent with this message type). */
	const LpMessage *containing_type = field->containing_type;
	const LpMessage *parent_type = containing_type ? containing_type->containing_type : NULL;
	if (!parent_type)
		return itoa_js(field->number);
	const char *key = ptr_key(containing_type);
	const LpField *group = map_get(&js_cache->group_field, key);
	if (!group) {
		group = (const LpField *)1;
		for (int i = 0; i < parent_type->n_fields; i++)
			if (parent_type->fields[i].type == PT_GROUP && parent_type->fields[i].message_type == containing_type) {
				group = &parent_type->fields[i];
				break;
			}
		map_put(&js_cache->group_field, key, (void *)group);
	}
	if (group != (const LpField *)1)
		return itoa_js((int64_t)field->number - group->number);
	return itoa_js(field->number);
}

static char *js_oneof_index(const LpOneof *oneof)
{
	intptr_t cached = (intptr_t)map_get(&js_cache->oneof_index, ptr_key(oneof));
	if (cached)
		return itoa_js(cached - 2);
	/* the index of every oneof of the message, in one pass */
	int index = -1;
	const LpMessage *m = oneof->containing_type;
	for (int i = 0; i < m->n_oneofs; i++) {
		const LpOneof *o = &m->oneofs[i];
		if (is_synthetic_oneof(o))
			continue;
		/* If at least one field in this oneof is not JSPB-ignored, count the oneof. */
		if (o->n_fields > 0)
			index++;
		map_put(&js_cache->oneof_index, ptr_key(o), (void *)(intptr_t)(index + 2));
	}
	cached = (intptr_t)map_get(&js_cache->oneof_index, ptr_key(oneof));
	return itoa_js(cached ? cached - 2 : index);
}

/* Decodes a codepoint in \x0000 -- \xFFFF. */
static uint16_t decode_utf8_codepoint(const uint8_t *bytes, size_t *length)
{
	if (*length == 0)
		return 0;
	size_t expected = 0;
	if ((*bytes & 0x80) == 0)
		expected = 1;
	else if ((*bytes & 0xe0) == 0xc0)
		expected = 2;
	else if ((*bytes & 0xf0) == 0xe0)
		expected = 3;
	else {
		*length = 0; /* Too long -- don't accept. */
		return 0;
	}
	if (*length < expected) {
		*length = 0; /* Not enough bytes -- don't accept. */
		return 0;
	}
	*length = expected;
	switch (expected) {
	case 1: return bytes[0];
	case 2: return (uint16_t)(((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F));
	case 3: return (uint16_t)(((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F));
	default: return 0;
	}
}

/* Escapes the contents of a string to be included within double-quotes ("") in
 * JavaScript. Returns false if out was truncated because in contained invalid UTF-8 or
 * codepoints outside the BMP. */
static bool escape_js_string(Str in, Buf *out)
{
	static const char hex[] = "0123456789abcdef";
	size_t decoded = 0;
	for (size_t i = 0; i < in.n; i += decoded) {
		size_t have_bytes = in.n - i;
		uint8_t bytes[3] = {
			(uint8_t)in.p[i],
			(uint8_t)(i + 1 < in.n ? in.p[i + 1] : 0),
			(uint8_t)(i + 2 < in.n ? in.p[i + 2] : 0),
		};
		uint16_t codepoint = decode_utf8_codepoint(bytes, &have_bytes);
		if (have_bytes == 0)
			return false;
		decoded = have_bytes;
		switch (codepoint) {
		case '\'': buf_adds(out, "\\x27"); break;
		case '"': buf_adds(out, "\\x22"); break;
		case '<': buf_adds(out, "\\x3c"); break;
		case '=': buf_adds(out, "\\x3d"); break;
		case '>': buf_adds(out, "\\x3e"); break;
		case '&': buf_adds(out, "\\x26"); break;
		case '\b': buf_adds(out, "\\b"); break;
		case '\t': buf_adds(out, "\\t"); break;
		case '\n': buf_adds(out, "\\n"); break;
		case '\f': buf_adds(out, "\\f"); break;
		case '\r': buf_adds(out, "\\r"); break;
		case '\\': buf_adds(out, "\\\\"); break;
		default:
			if (codepoint >= 0x20 && codepoint <= 0x7e) {
				buf_addc(out, (char)codepoint);
			} else if (codepoint >= 0x100) {
				buf_adds(out, "\\u");
				for (int s = 12; s >= 0; s -= 4)
					buf_addc(out, hex[(codepoint >> s) & 0xf]);
			} else {
				buf_adds(out, "\\x");
				buf_addc(out, hex[codepoint >> 4]);
				buf_addc(out, hex[codepoint & 0xf]);
			}
			break;
		}
	}
	return true;
}

/* EscapeBase64: in[i] is a (signed) char of the x86-64 build, sign-extended into value. */
static char *escape_base64(Str in)
{
	static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	Buf result = {0};
	for (size_t i = 0; i < in.n; i += 3) {
		uint32_t value = (uint32_t)((int32_t)(signed char)in.p[i] * 65536);
		if (i + 1 < in.n)
			value |= (uint32_t)((int32_t)(signed char)in.p[i + 1] * 256);
		if (i + 2 < in.n)
			value |= (uint32_t)(int32_t)(signed char)in.p[i + 2];
		buf_addc(&result, kAlphabet[(value >> 18) & 0x3f]);
		buf_addc(&result, kAlphabet[(value >> 12) & 0x3f]);
		buf_addc(&result, i + 1 < in.n ? kAlphabet[(value >> 6) & 0x3f] : '=');
		buf_addc(&result, i + 2 < in.n ? kAlphabet[value & 0x3f] : '=');
	}
	return xstrdup(buf_str(&result));
}

/* Post-process the result of SimpleFtoa/SimpleDtoa to *exactly* match the
 * original codegen's formatting (which is just .toString() on java.lang.Double
 * or java.lang.Float). */
static char *post_process_float(const char *result)
{
	if (str_eq(result, "inf"))
		return "Infinity";
	if (str_eq(result, "-inf"))
		return "-Infinity";
	if (str_eq(result, "nan"))
		return "NaN";
	const char *e = strchr(result, 'e');
	if (e) {
		char *mantissa = xstrndup(result, (size_t)(e - result));
		const char *exponent = e + 1;
		if (!strchr(mantissa, '.'))
			mantissa = xcat(mantissa, ".0");
		bool exp_neg = false;
		if (*exponent == '+') {
			exponent++;
		} else if (*exponent == '-') {
			exp_neg = true;
			exponent++;
		}
		while (strlen(exponent) > 1 && exponent[0] == '0')
			exponent++;
		return xcat(mantissa, "E", exp_neg ? "-" : "", exponent);
	}
	if (!strchr(result, '.'))
		return xcat(result, ".0");
	return (char *)result;
}

static bool in_real_oneof(const LpField *field) { return proto_field_real_oneof(field) != NULL; }

/* Return true if this is an integral field that should be represented as string in JS. */
static bool is_integral_field_with_string_js_type(const LpField *field)
{
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT64:
	case LP_CPPTYPE_UINT64:
		return pb_int(field->options, FOPT_JSTYPE, 0) == JSTYPE_JS_STRING;
	default:
		return false;
	}
}

static char *maybe_number_string(const LpField *field, char *orig)
{
	return is_integral_field_with_string_js_type(field) ? xcat("\"", orig, "\"") : orig;
}

static char *js_field_default(const LpField *field)
{
	if (proto_field_is_repeated(field))
		return "[]";
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: return maybe_number_string(field, itoa_js((int32_t)field->default_value.i));
	case LP_CPPTYPE_UINT32:
		/* Java stores unsigned integers as signed: reinterpret as base-2 signed. */
		return maybe_number_string(field, itoa_js((int32_t)(uint32_t)field->default_value.u));
	case LP_CPPTYPE_INT64: return maybe_number_string(field, itoa_js(field->default_value.i));
	case LP_CPPTYPE_UINT64: return maybe_number_string(field, itoa_js((int64_t)field->default_value.u));
	case LP_CPPTYPE_ENUM:
		if (!field->default_value_enum)
			fail("enum field %s has no default value", field->full_name);
		return itoa_js(field->default_value_enum->number);
	case LP_CPPTYPE_BOOL: return field->default_value.b ? "true" : "false";
	case LP_CPPTYPE_FLOAT: return post_process_float(lp_simple_ftoa(field->default_value.f));
	case LP_CPPTYPE_DOUBLE: return post_process_float(lp_simple_dtoa(field->default_value.d));
	case LP_CPPTYPE_STRING:
		if (field->type == PT_STRING) {
			Buf out = {0};
			/* an invalid default is truncated (upstream logs a warning) */
			escape_js_string(field->default_value_string, &out);
			return xcat("\"", buf_str(&out), "\"");
		}
		return xcat("\"", escape_base64(field->default_value_string), "\"");
	default:
		return "null";
	}
}

static char *proto_type_name(const JsOptions *o, const LpField *field)
{
	switch (field->type) {
	case PT_BOOL: return "bool";
	case PT_INT32: return "int32";
	case PT_UINT32: return "uint32";
	case PT_SINT32: return "sint32";
	case PT_FIXED32: return "fixed32";
	case PT_SFIXED32: return "sfixed32";
	case PT_INT64: return "int64";
	case PT_UINT64: return "uint64";
	case PT_SINT64: return "sint64";
	case PT_FIXED64: return "fixed64";
	case PT_SFIXED64: return "sfixed64";
	case PT_FLOAT: return "float";
	case PT_DOUBLE: return "double";
	case PT_STRING: return "string";
	case PT_BYTES: return "bytes";
	case PT_GROUP: return get_message_path(o, field->message_type);
	case PT_ENUM: return get_enum_path(o, field->enum_type);
	case PT_MESSAGE: return get_message_path(o, field->message_type);
	default: return "";
	}
}

static const char *js_integer_type_name(const LpField *field)
{
	return is_integral_field_with_string_js_type(field) ? "string" : "number";
}

static const char *js_string_type_name(const LpField *field, int bytes_mode)
{
	if (field->type == PT_BYTES) {
		switch (bytes_mode) {
		case BYTES_DEFAULT: return "(string|Uint8Array)";
		case BYTES_B64: return "string";
		case BYTES_U8: return "Uint8Array";
		}
	}
	return "string";
}

static const char *js_type_name(const JsOptions *o, const LpField *field, int bytes_mode)
{
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_BOOL: return "boolean";
	case LP_CPPTYPE_INT32:
	case LP_CPPTYPE_INT64:
	case LP_CPPTYPE_UINT32:
	case LP_CPPTYPE_UINT64: return js_integer_type_name(field);
	case LP_CPPTYPE_FLOAT:
	case LP_CPPTYPE_DOUBLE: return "number";
	case LP_CPPTYPE_STRING: return js_string_type_name(field, bytes_mode);
	case LP_CPPTYPE_ENUM: return get_enum_path(o, field->enum_type);
	case LP_CPPTYPE_MESSAGE: return get_message_path(o, field->message_type);
	default: return "";
	}
}

/* Returns true for fields that return "null" from accessors when they are
 * unset (UseBrokenPresenceSemantics is false in open source). */
static bool returns_null_when_unset(const LpField *field)
{
	return lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE && !proto_field_is_required(field) &&
	       !proto_field_is_repeated(field);
}

static bool declared_return_type_is_nullable(const LpField *field)
{
	if (proto_field_is_required(field) || field->type == PT_ENUM)
		return false;
	if (!proto_field_has_presence(field) && !proto_field_is_repeated(field) &&
	    lp_field_cpp_type(field) != LP_CPPTYPE_MESSAGE)
		return false;
	return returns_null_when_unset(field);
}

/* Returns types which are known to by non-nullable by default. */
static bool is_primitive(const char *type)
{
	return str_eq(type, "undefined") || str_eq(type, "string") || str_eq(type, "number") || str_eq(type, "boolean");
}

static char *js_field_type_annotation(const JsOptions *o, const LpField *field, bool is_setter_argument,
				      bool force_present, bool singular_if_not_packed, int bytes_mode, bool force_singular)
{
	char *jstype = (char *)js_type_name(o, field, bytes_mode);
	if (!force_singular && proto_field_is_repeated(field) &&
	    (proto_field_is_packed(field) || !singular_if_not_packed)) {
		if (field->type == PT_BYTES && bytes_mode == BYTES_DEFAULT) {
			jstype = "(Array<!Uint8Array>|Array<string>)";
		} else {
			if (!is_primitive(jstype))
				jstype = xcat("!", jstype);
			jstype = xcat("Array<", jstype, ">");
		}
	}
	bool is_null_or_undefined = false;
	if (is_setter_argument) {
		if (returns_null_when_unset(field)) { /* SetterAcceptsNull */
			jstype = xcat("?", jstype);
			is_null_or_undefined = true;
		}
		if (returns_null_when_unset(field)) { /* SetterAcceptsUndefined */
			jstype = xcat(jstype, "|undefined");
			is_null_or_undefined = true;
		}
	} else if (force_present) {
		/* Don't add null or undefined. */
	} else if (declared_return_type_is_nullable(field)) {
		jstype = xcat("?", jstype);
		is_null_or_undefined = true;
	}
	if (!is_null_or_undefined && !is_primitive(jstype))
		jstype = xcat("!", jstype);
	return jstype;
}

static char *js_type_annotation(const JsOptions *o, const LpField *field, bool is_setter_argument, bool force_present,
				bool singular_if_not_packed)
{
	return js_field_type_annotation(o, field, is_setter_argument, force_present, singular_if_not_packed, BYTES_DEFAULT,
					false);
}

/* FieldDescriptor::kTypeToName */
static const char *const type_names[] = {
	"ERROR", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool", "string",
	"group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64",
};

static char *js_binary_method_type(const LpField *field, bool is_writer)
{
	char *name = xstrdup(field->type >= 1 && field->type <= 18 ? type_names[field->type] : "ERROR");
	if (name[0] >= 'a' && name[0] <= 'z')
		name[0] = (char)(name[0] - 'a' + 'A');
	if (!is_writer && field->type == PT_STRING && proto_field_requires_utf8_validation(field))
		name = xcat(name, "RequireUtf8");
	return is_integral_field_with_string_js_type(field) ? xcat(name, "String") : name;
}

static char *js_binary_read_write_method_name(const LpField *field, bool is_writer)
{
	char *name = js_binary_method_type(field, is_writer);
	if (is_writer && proto_field_is_packed(field))
		name = xcat("Packed", name);
	else if (!is_writer && proto_field_is_packable(field))
		name = xcat("Packable", name, "Into");
	else if (is_writer && proto_field_is_repeated(field))
		name = xcat("Repeated", name);
	return name;
}

static char *js_binary_reader_method_name(const LpField *field)
{
	return xcat("jspb.BinaryReader.prototype.read", js_binary_read_write_method_name(field, false));
}

static char *js_binary_writer_method_name(const LpField *field)
{
	if (field->containing_type && pb_int(field->containing_type->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0))
		return "jspb.BinaryWriter.prototype.writeMessageSet";
	return xcat("jspb.BinaryWriter.prototype.write", js_binary_read_write_method_name(field, true));
}

static const char *js_type_tag(const LpField *desc)
{
	switch (desc->type) {
	case PT_DOUBLE:
	case PT_FLOAT: return "Float";
	case PT_INT32:
	case PT_UINT32:
	case PT_INT64:
	case PT_UINT64:
	case PT_FIXED32:
	case PT_FIXED64:
	case PT_SINT32:
	case PT_SINT64:
	case PT_SFIXED32:
	case PT_SFIXED64: return is_integral_field_with_string_js_type(desc) ? "StringInt" : "Int";
	case PT_BOOL: return "Boolean";
	case PT_STRING: return "String";
	case PT_BYTES: return "Bytes";
	case PT_ENUM: return "Enum";
	default: return "";
	}
}

static bool has_repeated_fields(const LpMessage *desc)
{
	for (int i = 0; i < desc->n_fields; i++)
		if (proto_field_is_repeated(&desc->fields[i]) && !proto_field_is_map(&desc->fields[i]))
			return true;
	return false;
}

static const char kRepeatedFieldArrayName[] = ".repeatedFields_";

static char *repeated_fields_array_name(const JsOptions *o, const LpMessage *desc)
{
	return has_repeated_fields(desc) ? xcat(get_message_path(o, desc), kRepeatedFieldArrayName) : "null";
}

static bool has_oneof_fields(const LpMessage *desc)
{
	const char *key = ptr_key(desc);
	intptr_t cached = (intptr_t)map_get(&js_cache->has_oneof, key);
	if (!cached) {
		cached = 1;
		for (int i = 0; i < desc->n_fields; i++)
			if (in_real_oneof(&desc->fields[i])) {
				cached = 2;
				break;
			}
		map_put(&js_cache->has_oneof, key, (void *)cached);
	}
	return cached == 2;
}

static const char kOneofGroupArrayName[] = ".oneofGroups_";

static char *oneof_fields_array_name(const JsOptions *o, const LpMessage *desc)
{
	return has_oneof_fields(desc) ? xcat(get_message_path(o, desc), kOneofGroupArrayName) : "null";
}

static char *repeated_field_number_list(const LpMessage *desc)
{
	Buf b = {0};
	buf_addc(&b, '[');
	bool first = true;
	for (int i = 0; i < desc->n_fields; i++)
		if (proto_field_is_repeated(&desc->fields[i]) && !proto_field_is_map(&desc->fields[i])) {
			if (!first)
				buf_addc(&b, ',');
			first = false;
			buf_adds(&b, js_field_index(&desc->fields[i]));
		}
	buf_addc(&b, ']');
	return xstrdup(buf_str(&b));
}

static char *oneof_group_list(const LpMessage *desc)
{
	/* List of arrays (one per oneof), each of which is a list of field indices */
	Buf b = {0};
	buf_addc(&b, '[');
	bool first = true;
	for (int i = 0; i < desc->n_oneofs; i++) {
		const LpOneof *oneof = &desc->oneofs[i];
		if (ignore_oneof(oneof))
			continue;
		if (!first)
			buf_addc(&b, ',');
		first = false;
		buf_addc(&b, '[');
		for (int j = 0; j < oneof->n_fields; j++) {
			if (j)
				buf_addc(&b, ',');
			buf_adds(&b, js_field_index(proto_oneof_field(oneof, j)));
		}
		buf_addc(&b, ']');
	}
	buf_addc(&b, ']');
	return xstrdup(buf_str(&b));
}

static char *js_oneof_array(const JsOptions *o, const LpField *field)
{
	return xcat(oneof_fields_array_name(o, field->containing_type), "[", js_oneof_index(field->containing_oneof), "]");
}

static char *relative_type_name(const LpField *field)
{
	/* For a field with an enum or message type, compute a name relative to the
	 * path name of the message type containing this field. */
	const char *package = field->file->package;
	char *containing_type = xcat(field->containing_type->full_name, ".");
	const char *type =
		lp_field_cpp_type(field) == LP_CPPTYPE_ENUM ? field->enum_type->full_name : field->message_type->full_name;
	size_t prefix = 0, package_size = strlen(package);
	for (size_t i = 0; type[i] && containing_type[i]; i++) {
		if (type[i] != containing_type[i])
			break;
		if (type[i] == '.' && i >= package_size)
			prefix = i + 1;
	}
	return xstrdup(type + prefix);
}

static char *js_extensions_object_name(const JsOptions *o, const LpFile *from_file, const LpMessage *desc)
{
	if (str_eq(desc->full_name, "google.protobuf.bridge.MessageSet"))
		return "jspb.Message.messageSetExtensions";
	return xcat(maybe_cross_file_ref(o, from_file, desc), ".extensions");
}

static const LpField *map_field_key(const LpField *field)
{
	const LpField *f = proto_message_field_by_number(field->message_type, 1);
	if (!f)
		fail("map entry %s has no key", field->message_type->full_name);
	return f;
}

static const LpField *map_field_value(const LpField *field)
{
	const LpField *f = proto_message_field_by_number(field->message_type, 2);
	if (!f)
		fail("map entry %s has no value", field->message_type->full_name);
	return f;
}

static char *field_definition(const JsOptions *o, const LpField *field)
{
	if (proto_field_is_map(field)) {
		const LpField *key_field = map_field_key(field);
		const LpField *value_field = map_field_value(field);
		char *key_type = proto_type_name(o, key_field);
		char *value_type;
		if (value_field->type == PT_ENUM || value_field->type == PT_MESSAGE)
			value_type = relative_type_name(value_field);
		else
			value_type = proto_type_name(o, value_field);
		return xfmt("map<%s, %s> %s = %d;", key_type, value_type, field->name, field->number);
	}
	const char *qualifier =
		proto_field_is_repeated(field) ? "repeated" : (proto_field_is_required(field) ? "required" : "optional");
	const char *type, *name;
	if (field->type == PT_ENUM || field->type == PT_MESSAGE) {
		type = relative_type_name(field);
		name = field->name;
	} else if (field->type == PT_GROUP) {
		type = "group";
		name = field->message_type->name;
	} else {
		type = proto_type_name(o, field);
		name = field->name;
	}
	return xfmt("%s %s %s = %d;", qualifier, type, name, field->number);
}

static const char *field_comments(const LpField *field, int bytes_mode)
{
	if (field->type == PT_BYTES && bytes_mode == BYTES_U8)
		return " * Note that Uint8Array is not supported on all browsers.\n"
		       " * @see http://caniuse.com/Uint8Array\n";
	return "";
}

static bool should_generate_extension(const LpField *field) { return field->is_extension; }

static bool has_extensions_message(const LpMessage *desc)
{
	for (int i = 0; i < desc->n_extensions; i++)
		if (should_generate_extension(&desc->extensions[i]))
			return true;
	for (int i = 0; i < desc->n_nested; i++)
		if (has_extensions_message(&desc->nested[i]))
			return true;
	return false;
}

static bool has_extensions_file(const LpFile *file)
{
	for (int i = 0; i < file->n_extensions; i++)
		if (should_generate_extension(&file->extensions[i]))
			return true;
	for (int i = 0; i < file->n_messages; i++)
		if (has_extensions_message(&file->messages[i]))
			return true;
	return false;
}

static bool has_map(const LpMessage *desc)
{
	for (int i = 0; i < desc->n_fields; i++)
		if (proto_field_is_map(&desc->fields[i]))
			return true;
	for (int i = 0; i < desc->n_nested; i++)
		if (has_map(&desc->nested[i]))
			return true;
	return false;
}

static bool file_has_map(const LpFile *desc)
{
	for (int i = 0; i < desc->n_messages; i++)
		if (has_map(&desc->messages[i]))
			return true;
	return false;
}

static bool is_extendable(const LpMessage *desc) { return desc->n_extension_ranges > 0; }

/* Returns the max index in the underlying data storage array beyond which the
 * extension object is used. */
static char *get_pivot(const LpMessage *desc)
{
	enum { kDefaultPivot = 500 };
	int max_field_number = 0;
	for (int i = 0; i < desc->n_fields; i++)
		if (desc->fields[i].number > max_field_number)
			max_field_number = desc->fields[i].number;
	int pivot = -1;
	if (is_extendable(desc) || max_field_number >= kDefaultPivot)
		pivot = max_field_number + 1 < kDefaultPivot ? max_field_number + 1 : kDefaultPivot;
	return itoa_js(pivot);
}

/* Whether this field represents presence. */
static bool has_field_presence(const LpField *field) { return proto_field_has_presence(field); }

/* FileDeduplicator: the semantics that same file can be generated multiple times, but only the
 * last one keeps the short name. */
typedef struct {
	Map descs_by_shortname;        /* short name -> desc */
	Map allowed_descs_actual_name; /* ptr_key(desc) -> name */
	Map allowed_descs_full_name;   /* ptr_key(desc) -> name */
} FileDeduplicator;

static void dedup_add_file(FileDeduplicator *d, const char *short_name, const char *full_name, const void *desc)
{
	const void *short_name_desc = map_get(&d->descs_by_shortname, short_name);
	if (short_name_desc) {
		/* Change old pointer's actual name to full name. */
		const char *k = ptr_key(short_name_desc);
		map_put(&d->allowed_descs_actual_name, k, map_get(&d->allowed_descs_full_name, k));
	}
	map_put(&d->descs_by_shortname, short_name, (void *)desc);
	const char *k = ptr_key(desc);
	map_put(&d->allowed_descs_actual_name, k, (void *)short_name);
	map_put(&d->allowed_descs_full_name, k, (void *)full_name);
}

/* DepthFirstSearch of the files in the order of the old code generator (iterative). */
static void depth_first_search(const LpFile *root, Vec *list, Map *seen)
{
	typedef struct {
		const LpFile *file;
		int next;
	} Frame;
	if (map_has(seen, ptr_key(root)))
		return;
	map_put(seen, ptr_key(root), (void *)root);
	Frame *stack = xalloc(sizeof *stack);
	int n = 1, cap = 1;
	stack[0] = (Frame){root, 0};
	while (n) {
		Frame *f = &stack[n - 1];
		if (f->next < f->file->n_dependencies) {
			const LpFile *dep = f->file->dependencies[f->next++];
			if (map_has(seen, ptr_key(dep)))
				continue;
			map_put(seen, ptr_key(dep), (void *)dep);
			if (n == cap) {
				stack = xrealloc(stack, sizeof *stack * (size_t)cap, sizeof *stack * (size_t)cap * 2);
				cap *= 2;
			}
			stack[n++] = (Frame){dep, 0};
			continue;
		}
		vec_push(list, (void *)f->file);
		n--;
	}
}

/* GenerateJspbFileOrder: an ordering of the input FileDescriptors that matches
 * the logic of the old code generator. */
static Vec generate_jspb_file_order(LpFile *const *input, int n)
{
	Vec ordered = {0}, result = {0};
	Map seen = {0}, input_set = {0};
	for (int i = 0; i < n; i++) {
		depth_first_search(input[i], &ordered, &seen);
		map_put(&input_set, ptr_key(input[i]), input[i]);
	}
	for (int i = 0; i < ordered.n; i++)
		if (map_has(&input_set, ptr_key(ordered.v[i])))
			vec_push(&result, ordered.v[i]);
	return result;
}

/* DepsGenerator */
static void deps_generator(const LpMessage *desc, Vec *deps, void *arg)
{
	(void)arg;
	for (int i = 0; i < desc->n_fields; i++)
		if (desc->fields[i].message_type)
			vec_push(deps, desc->fields[i].message_type);
	for (int i = 0; i < desc->n_extensions; i++) {
		if (desc->extensions[i].message_type)
			vec_push(deps, desc->extensions[i].message_type);
		if (desc->extensions[i].containing_type)
			vec_push(deps, desc->extensions[i].containing_type);
	}
	for (int i = 0; i < desc->n_nested; i++)
		vec_push(deps, &desc->nested[i]);
	if (desc->containing_type)
		vec_push(deps, desc->containing_type);
}

/* GenerateJspbAllowedMap: the last descriptor for each filename (ptr_key -> filename). */
static void generate_jspb_allowed_map(JsState *st, const JsOptions *o, LpFile *const *files, int n, Map *allowed,
				      LpSccAnalyzer *analyzer)
{
	Vec files_ordered = generate_jspb_file_order(files, n);
	FileDeduplicator dedup = {0};
	Map added = {0};
	for (int i = 0; i < files_ordered.n; i++) {
		const LpFile *file = files_ordered.v[i];
		for (int j = 0; j < file->n_messages; j++) {
			const LpScc *scc = lp_scc_get(analyzer, &file->messages[j]);
			if (map_has(&added, ptr_key(scc)))
				continue;
			map_put(&added, ptr_key(scc), (void *)scc);
			dedup_add_file(&dedup, get_messages_file_name(st, o, scc, false), get_messages_file_name(st, o, scc, true),
				       scc);
		}
		for (int j = 0; j < file->n_enums; j++)
			dedup_add_file(&dedup, get_enum_file_name(o, &file->enums[j], false),
				       get_enum_file_name(o, &file->enums[j], true), &file->enums[j]);
		/* Pull out all free-floating extensions and generate files for those too. */
		bool has_extension = false;
		for (int j = 0; j < file->n_extensions; j++)
			if (should_generate_extension(&file->extensions[j]))
				has_extension = true;
		if (has_extension)
			dedup_add_file(&dedup, get_extension_file_name(o, file, false), get_extension_file_name(o, file, true),
				       file);
	}
	*allowed = dedup.allowed_descs_actual_name;
}

/* Embeds base64 encoded GeneratedCodeInfo proto in a comment at the end of file. */
static void embed_code_annotations(const Buf *annotations, LpPrinter *printer)
{
	char *meta_64 = lp_base64_escape((Str){annotations->p ? annotations->p : "", annotations->n});
	lp_print(printer, "\n// Below is base64 encoded GeneratedCodeInfo proto", NULL);
	lp_print(printer, "\n// $encoded_proto$\n", "encoded_proto", meta_64, NULL);
}

static bool is_well_known_type_file(const LpFile *file) { return has_prefix(file->name, "google/protobuf/"); }

/* ---- Generator ---- */

#define P(...) lp_print(printer, __VA_ARGS__, NULL)

static void generate_header(const LpFile *file, LpPrinter *printer)
{
	if (file)
		P("// source: $filename$\n", "filename", file->name);
	P("/**\n"
	  " * @fileoverview\n"
	  " * @enhanceable\n"
	  " * @suppress {missingRequire} reports error on implicit type usages.\n"
	  " * @suppress {messageConventions} JS Compiler reports an "
	  "error if a variable or\n"
	  " *     field starts with 'MSG_' and isn't a translatable "
	  "message.\n"
	  " * @public\n"
	  " */\n"
	  "// GENERATED CODE -- DO NOT EDIT!\n"
	  "/* eslint-disable */\n"
	  "// @ts-nocheck\n"
	  "\n");
}

static void find_provides_for_enum(const JsOptions *o, const LpEnum *enumdesc, StrSet *provided)
{
	strset_insert(provided, get_enum_path(o, enumdesc));
}

static void find_provides_for_message(const JsOptions *o, const LpMessage *desc, StrSet *provided)
{
	if (ignore_message(desc))
		return;
	strset_insert(provided, get_message_path(o, desc));
	for (int i = 0; i < desc->n_enums; i++)
		find_provides_for_enum(o, &desc->enums[i], provided);
	/* FindProvidesForOneOfEnums */
	if (has_oneof_fields(desc))
		for (int i = 0; i < desc->n_oneofs; i++) {
			if (ignore_oneof(&desc->oneofs[i]))
				continue;
			strset_insert(provided,
				      xcat(get_message_path(o, desc), ".", js_oneof_name(&desc->oneofs[i]), "Case"));
		}
	for (int i = 0; i < desc->n_nested; i++)
		find_provides_for_message(o, &desc->nested[i], provided);
}

static void find_provides_for_file(const JsOptions *o, const LpFile *file, StrSet *provided)
{
	for (int i = 0; i < file->n_messages; i++)
		find_provides_for_message(o, &file->messages[i], provided);
	for (int i = 0; i < file->n_enums; i++)
		find_provides_for_enum(o, &file->enums[i], provided);
}

static void find_provides(const JsOptions *o, LpPrinter *printer, LpFile *const *files, int n, StrSet *provided)
{
	for (int i = 0; i < n; i++)
		find_provides_for_file(o, files[i], provided);
	P("\n");
}

/* For extension fields at file scope. */
static void find_provides_for_fields(const JsOptions *o, const LpField *const *fields, int n, StrSet *provided)
{
	for (int i = 0; i < n; i++)
		strset_insert(provided, xcat(get_namespace(o, fields[i]->file), ".", js_object_field_name(fields[i])));
}

static void generate_provides(const JsOptions *o, LpPrinter *printer, const StrSet *provided)
{
	const char **names = strset_sorted(provided);
	for (int i = 0; i < provided->v.n; i++) {
		if (o->import_style == IMPORT_CLOSURE) {
			P("goog.provide('$name$');\n", "name", names[i]);
		} else if (o->import_style == IMPORT_COMMONJS_STRICT) {
			/* Do not use global scope in strict mode: remove "proto." from the namespace object */
			if (strncmp(names[i], "proto.", 6) != 0)
				fail("Check failed: 0 == namespaceObject.compare(0, 6, \"proto.\")");
			P("goog.exportSymbol('$name$', null, proto);\n", "name", names[i] + 6);
		} else {
			P("goog.exportSymbol('$name$', null, global);\n", "name", names[i]);
		}
	}
}

static void find_requires_for_field(const JsOptions *o, const LpField *field, StrSet *required, StrSet *forwards)
{
	if (lp_field_cpp_type(field) == LP_CPPTYPE_ENUM &&
	    /* N.B.: file-level extensions with enum type do *not* create
	     * dependencies, as per original codegen. */
	    !(field->is_extension && !field->extension_scope)) {
		if (o->add_require_for_enums)
			strset_insert(required, get_enum_path(o, field->enum_type));
		else
			strset_insert(forwards, get_enum_path(o, field->enum_type));
	} else if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE) {
		if (!ignore_message(field->message_type))
			strset_insert(required, get_message_path(o, field->message_type));
	}
}

static void find_requires_for_extension(const JsOptions *o, const LpField *field, StrSet *required, StrSet *forwards)
{
	if (!str_eq(field->containing_type->full_name, "google.protobuf.bridge.MessageSet"))
		strset_insert(required, get_message_path(o, field->containing_type));
	find_requires_for_field(o, field, required, forwards);
}

static void find_requires_for_message(const JsOptions *o, const LpMessage *desc, StrSet *required, StrSet *forwards,
				      bool *have_message)
{
	*have_message = true;
	for (int i = 0; i < desc->n_fields; i++)
		find_requires_for_field(o, &desc->fields[i], required, forwards);
	for (int i = 0; i < desc->n_extensions; i++)
		find_requires_for_extension(o, &desc->extensions[i], required, forwards);
	for (int i = 0; i < desc->n_nested; i++)
		find_requires_for_message(o, &desc->nested[i], required, forwards, have_message);
}

static void generate_requires_impl(LpPrinter *printer, StrSet *required, StrSet *forwards, const StrSet *provided,
				   bool require_jspb, bool require_extension, bool require_map)
{
	if (require_jspb) {
		strset_insert(required, "jspb.Message");
		strset_insert(required, "jspb.internal.public_for_gencode");
		strset_insert(required, "jspb.BinaryReader");
		strset_insert(required, "jspb.BinaryWriter");
		strset_insert(required, "jspb.binary.bytesource");
	}
	if (require_extension) {
		strset_insert(required, "jspb.ExtensionFieldBinaryInfo");
		strset_insert(required, "jspb.ExtensionFieldInfo");
	}
	if (require_map)
		strset_insert(required, "jspb.Map");
	const char **r = strset_sorted(required);
	for (int i = 0; i < required->v.n; i++)
		if (!strset_has(provided, r[i]))
			P("goog.require('$name$');\n", "name", r[i]);
	P("\n");
	const char **f = strset_sorted(forwards);
	for (int i = 0; i < forwards->v.n; i++)
		if (!strset_has(provided, f[i]))
			P("goog.forwardDeclare('$name$');\n", "name", f[i]);
}

static void generate_requires_for_scc(const JsOptions *o, LpPrinter *printer, const LpScc *scc, const StrSet *provided)
{
	StrSet required = {0}, forwards = {0};
	bool have_message = false, has_extension = false, has_map_ = false;
	for (int i = 0; i < scc->n; i++) {
		const LpMessage *desc = scc->descriptors[i];
		if (!desc->containing_type) {
			find_requires_for_message(o, desc, &required, &forwards, &have_message);
			has_extension = has_extension || has_extensions_message(desc);
			has_map_ = has_map_ || has_map(desc);
		}
	}
	generate_requires_impl(printer, &required, &forwards, provided, have_message, has_extension, has_map_);
}

static void generate_requires_for_library(const JsOptions *o, LpPrinter *printer, LpFile *const *files, int n,
					  const StrSet *provided)
{
	/* For Closure imports we need to import every message type individually. */
	StrSet required = {0}, forwards = {0};
	bool have_extensions = false, have_map = false, have_message = false;
	for (int i = 0; i < n; i++) {
		const LpFile *file = files[i];
		for (int j = 0; j < file->n_messages; j++)
			if (!ignore_message(&file->messages[j]))
				find_requires_for_message(o, &file->messages[j], &required, &forwards, &have_message);
		if (!have_extensions && has_extensions_file(file))
			have_extensions = true;
		if (!have_map && file_has_map(file))
			have_map = true;
		for (int j = 0; j < file->n_extensions; j++) {
			const LpField *extension = &file->extensions[j];
			if (!str_eq(extension->containing_type->full_name, "google.protobuf.bridge.MessageSet"))
				strset_insert(&required, get_message_path(o, extension->containing_type));
			find_requires_for_field(o, extension, &required, &forwards);
			have_extensions = true;
		}
	}
	generate_requires_impl(printer, &required, &forwards, provided, have_message, have_extensions, have_map);
}

/* For extension fields at file scope. */
static void generate_requires_for_extensions(const JsOptions *o, LpPrinter *printer, const LpField *const *fields,
					     int n, const StrSet *provided)
{
	StrSet required = {0}, forwards = {0};
	for (int i = 0; i < n; i++)
		find_requires_for_extension(o, fields[i], &required, &forwards);
	generate_requires_impl(printer, &required, &forwards, provided, false, n > 0, false);
}

static void generate_test_only(const JsOptions *o, LpPrinter *printer)
{
	if (o->testonly)
		P("goog.setTestOnly();\n\n");
	P("\n");
}

static void generate_class_extension_field_info(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	if (!is_extendable(desc))
		return;
	P("\n"
	  "/**\n"
	  " * The extensions registered with this message class. This is a "
	  "map of\n"
	  " * extension field number to fieldInfo object.\n"
	  " *\n"
	  " * For example:\n"
	  " *     { 123: {fieldIndex: 123, fieldName: {my_field_name: 0}, "
	  "ctor: proto.example.MyMessage} }\n"
	  " *\n"
	  " * fieldName contains the JsCompiler renamed field name property "
	  "so that it\n"
	  " * works in OPTIMIZED mode.\n"
	  " *\n"
	  " * @type {!Object<number, jspb.ExtensionFieldInfo>}\n"
	  " */\n"
	  "$class$.extensions = {};\n"
	  "\n",
	  "class", get_message_path(o, desc));
	P("\n"
	  "/**\n"
	  " * The extensions registered with this message class. This is a "
	  "map of\n"
	  " * extension field number to fieldInfo object.\n"
	  " *\n"
	  " * For example:\n"
	  " *     { 123: {fieldIndex: 123, fieldName: {my_field_name: 0}, "
	  "ctor: proto.example.MyMessage} }\n"
	  " *\n"
	  " * fieldName contains the JsCompiler renamed field name property "
	  "so that it\n"
	  " * works in OPTIMIZED mode.\n"
	  " *\n"
	  " * @type {!Object<number, jspb.ExtensionFieldBinaryInfo>}\n"
	  " */\n"
	  "$class$.extensionsBinary = {};\n"
	  "\n",
	  "class", get_message_path(o, desc));
}

static void generate_class_constructor(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	P("/**\n"
	  " * Generated by JsPbCodeGenerator.\n"
	  " * @param {Array=} opt_data Optional initial data array, typically "
	  "from a\n"
	  " * server response, or constructed directly in Javascript. The array "
	  "is used\n"
	  " * in place and becomes part of the constructed object. It is not "
	  "cloned.\n"
	  " * If no data is provided, the constructed object will be empty, but "
	  "still\n"
	  " * valid.\n"
	  " * @extends {jspb.Message}\n"
	  " * @constructor\n"
	  " */\n"
	  "$classprefix$$classname$ = function(opt_data) {\n",
	  "classprefix", get_message_path_prefix(o, desc), "classname", desc->name);
	lp_annotate(printer, "classname", desc);
	P("  jspb.Message.initialize(this, opt_data, $messageId$, $pivot$, "
	  "$rptfields$, $oneoffields$);\n",
	  "messageId", "0", "pivot", get_pivot(desc), "rptfields", repeated_fields_array_name(o, desc), "oneoffields",
	  oneof_fields_array_name(o, desc));
	P("};\n"
	  "goog.inherits($classname$, jspb.Message);\n"
	  "if (goog.DEBUG && !COMPILED) {\n"
	  "  /**\n"
	  "   * @public\n"
	  "   * @override\n"
	  "   */\n"
	  "  $classname$.displayName = '$classname$';\n"
	  "}\n",
	  "classname", get_message_path(o, desc));
}

static void generate_class_constructor_and_declare_extension_field_info(const JsOptions *o, LpPrinter *printer,
									 const LpMessage *desc)
{
	generate_class_constructor(o, printer, desc);
	if (is_extendable(desc) && !str_eq(desc->full_name, "google.protobuf.bridge.MessageSet"))
		generate_class_extension_field_info(o, printer, desc);
	for (int i = 0; i < desc->n_nested; i++)
		if (!ignore_message(&desc->nested[i]))
			generate_class_constructor_and_declare_extension_field_info(o, printer, &desc->nested[i]);
}

static void generate_oneof_case_definition(const JsOptions *o, LpPrinter *printer, const LpOneof *oneof)
{
	P("/**\n"
	  " * @enum {number}\n"
	  " */\n"
	  "$classname$.$oneof$Case = {\n"
	  "  $upcase$_NOT_SET: 0",
	  "classname", get_message_path(o, oneof->containing_type), "oneof", js_oneof_name(oneof), "upcase",
	  to_enum_case(oneof->name));
	for (int i = 0; i < oneof->n_fields; i++) {
		const LpField *f = proto_oneof_field(oneof, i);
		P(",\n"
		  "  $upcase$: $number$",
		  "upcase", to_enum_case(f->name), "number", js_field_index(f));
		lp_annotate(printer, "upcase", f);
	}
	P("\n"
	  "};\n"
	  "\n"
	  "/**\n"
	  " * @return {$class$.$oneof$Case}\n"
	  " */\n"
	  "$class$.prototype.get$oneof$Case = function() {\n"
	  "  return /** @type {$class$.$oneof$Case} */(jspb.Message."
	  "computeOneofCase(this, $class$.oneofGroups_[$oneofindex$]));\n"
	  "};\n"
	  "\n",
	  "class", get_message_path(o, oneof->containing_type), "oneof", js_oneof_name(oneof), "oneofindex",
	  js_oneof_index(oneof));
}

static void generate_class_field_info(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	if (has_repeated_fields(desc))
		P("/**\n"
		  " * List of repeated fields within this message type.\n"
		  " * @private {!Array<number>}\n"
		  " * @const\n"
		  " */\n"
		  "$classname$$rptfieldarray$ = $rptfields$;\n"
		  "\n",
		  "classname", get_message_path(o, desc), "rptfieldarray", kRepeatedFieldArrayName, "rptfields",
		  repeated_field_number_list(desc));
	if (has_oneof_fields(desc)) {
		P("/**\n"
		  " * Oneof group definitions for this message. Each group defines the "
		  "field\n"
		  " * numbers belonging to that group. When of these fields' value is "
		  "set, all\n"
		  " * other fields in the group are cleared. During deserialization, if "
		  "multiple\n"
		  " * fields are encountered for a group, only the last value seen will "
		  "be kept.\n"
		  " * @private {!Array<!Array<number>>}\n"
		  " * @const\n"
		  " */\n"
		  "$classname$$oneofgrouparray$ = $oneofgroups$;\n"
		  "\n",
		  "classname", get_message_path(o, desc), "oneofgrouparray", kOneofGroupArrayName, "oneofgroups",
		  oneof_group_list(desc));
		for (int i = 0; i < desc->n_oneofs; i++) {
			if (ignore_oneof(&desc->oneofs[i]))
				continue;
			generate_oneof_case_definition(o, printer, &desc->oneofs[i]);
		}
	}
}

static void generate_field_value_expression(LpPrinter *printer, const char *obj_reference, const LpField *field,
					    bool use_default)
{
	int ct = lp_field_cpp_type(field);
	bool is_float_or_double = ct == LP_CPPTYPE_FLOAT || ct == LP_CPPTYPE_DOUBLE;
	bool is_boolean = ct == LP_CPPTYPE_BOOL;
	const char *with_default = use_default ? "WithDefault" : "";
	const char *default_arg = use_default ? xcat(", ", js_field_default(field)) : "";
	const char *cardinality = proto_field_is_repeated(field) ? "Repeated" : "";
	const char *type = "";
	if (is_float_or_double)
		type = "FloatingPoint";
	if (is_boolean)
		type = "Boolean";
	if (is_float_or_double && !proto_field_is_repeated(field) && !use_default)
		P("jspb.Message.getOptionalFloatingPointField($obj$, "
		  "$index$$default$)",
		  "obj", obj_reference, "index", js_field_index(field), "default", default_arg);
	else
		P("jspb.Message.get$cardinality$$type$Field$with_default$($obj$, "
		  "$index$$default$)",
		  "cardinality", cardinality, "type", type, "with_default", with_default, "obj", obj_reference, "index",
		  js_field_index(field), "default", default_arg);
}

static void generate_class_field_to_object(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	P("$fieldname$: ", "fieldname", js_object_field_name(field));
	if (proto_field_is_map(field)) {
		const LpField *value_field = map_field_value(field);
		/* If the map values are of a message type, we must provide their static
		 * toObject() method; otherwise we pass undefined for that argument. */
		const char *value_to_object = lp_field_cpp_type(value_field) == LP_CPPTYPE_MESSAGE
						      ? xcat(get_message_path(o, value_field->message_type), ".toObject")
						      : "undefined";
		P("(f = msg.get$name$()) ? f.toObject(includeInstance, $valuetoobject$) "
		  ": []",
		  "name", js_getter_name(field), "valuetoobject", value_to_object);
	} else if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE) {
		if (proto_field_is_repeated(field))
			P("jspb.Message.toObjectList(msg.get$getter$(),\n"
			  "    $type$.toObject, includeInstance)",
			  "getter", js_getter_name(field), "type", submessage_type_ref(o, field));
		else
			P("(f = msg.get$getter$()) && "
			  "$type$.toObject(includeInstance, f)",
			  "getter", js_getter_name(field), "type", submessage_type_ref(o, field));
	} else if (field->type == PT_BYTES) {
		/* For bytes fields we want to always return the B64 data. */
		P("msg.get$getter$()", "getter", js_getter_name_ex(field, BYTES_B64, false));
	} else {
		bool use_default = !proto_field_is_repeated(field) &&
				   (field->has_default_value || !proto_field_has_presence(field));
		if (!use_default)
			P("(f = ");
		generate_field_value_expression(printer, "msg", field, use_default);
		if (!use_default)
			P(") == null ? undefined : f");
	}
}

static void generate_class_to_object(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	P("\n"
	  "\n"
	  "if (jspb.Message.GENERATE_TO_OBJECT) {\n"
	  "/**\n"
	  " * Creates an object representation of this proto.\n"
	  " * Field names that are reserved in JavaScript and will be renamed to "
	  "pb_name.\n"
	  " * Optional fields that are not set will be set to undefined.\n"
	  " * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.\n"
	  " * For the list of reserved names please see:\n"
	  " *     net/proto2/compiler/js/internal/generator.cc#kKeyword.\n"
	  " * @param {boolean=} opt_includeInstance Deprecated. whether to include "
	  "the\n"
	  " *     JSPB instance for transitional soy proto support:\n"
	  " *     http://goto/soy-param-migration\n"
	  " * @return {!Object}\n"
	  " */\n"
	  "$classname$.prototype.toObject = function(opt_includeInstance) {\n"
	  "  return $classname$.toObject(opt_includeInstance, this);\n"
	  "};\n"
	  "\n"
	  "\n"
	  "/**\n"
	  " * Static version of the {@see toObject} method.\n"
	  " * @param {boolean|undefined} includeInstance Deprecated. Whether to "
	  "include\n"
	  " *     the JSPB instance for transitional soy proto support:\n"
	  " *     http://goto/soy-param-migration\n"
	  " * @param {!$classname$} msg The msg instance to transform.\n"
	  " * @return {!Object}\n"
	  " * @suppress {unusedLocalVariables} f is only used for nested messages\n"
	  " */\n"
	  "$classname$.toObject = function(includeInstance, msg) {\n"
	  "  var f, obj = {",
	  "classname", get_message_path(o, desc));
	bool first = true;
	for (int i = 0; i < desc->n_fields; i++) {
		if (!first) {
			P(",\n    ");
		} else {
			P("\n    ");
			first = false;
		}
		generate_class_field_to_object(o, printer, &desc->fields[i]);
	}
	if (!first)
		P("\n  };\n\n");
	else
		P("\n\n  };\n\n");
	if (is_extendable(desc))
		P("  jspb.Message.toObjectExtension(/** @type {!jspb.Message} */ (msg), "
		  "obj,\n"
		  "      $extObject$, $class$.prototype.getExtension,\n"
		  "      includeInstance);\n",
		  "extObject", js_extensions_object_name(o, desc->file, desc), "class", get_message_path(o, desc));
	P("  if (includeInstance) {\n"
	  "    obj.$$jspbMessageInstance = msg;\n"
	  "  }\n"
	  "  return obj;\n"
	  "};\n"
	  "}\n"
	  "\n"
	  "\n",
	  "classname", get_message_path(o, desc));
}

static void generate_class_deserialize_binary_field(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	P("    case $num$:\n", "num", itoa_js(field->number));
	if (proto_field_is_map(field)) {
		const LpField *key_field = map_field_key(field);
		const LpField *value_field = map_field_value(field);
		P("      var value = msg.get$name$();\n"
		  "      reader.readMessage(value, function(message, reader) {\n",
		  "name", js_getter_name(field));
		P("        jspb.Map.deserializeBinary(message, reader, "
		  "$keyReaderFn$, $valueReaderFn$",
		  "keyReaderFn", js_binary_reader_method_name(key_field), "valueReaderFn",
		  js_binary_reader_method_name(value_field));
		if (value_field->type == PT_MESSAGE)
			P(", $messageType$.deserializeBinaryFromReader", "messageType",
			  get_message_path(o, value_field->message_type));
		else
			P(", null");
		P(", $defaultKey$", "defaultKey", js_field_default(key_field));
		if (value_field->type == PT_MESSAGE)
			P(", new $messageType$()", "messageType", get_message_path(o, value_field->message_type));
		else
			P(", $defaultValue$", "defaultValue", js_field_default(value_field));
		P(");\n");
		P("         });\n");
	} else {
		if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE) {
			P("      var value = new $fieldclass$;\n"
			  "      reader.read$msgOrGroup$($grpfield$value,"
			  "$fieldclass$.deserializeBinaryFromReader);\n",
			  "fieldclass", submessage_type_ref(o, field), "msgOrGroup",
			  field->type == PT_GROUP ? "Group" : "Message", "grpfield",
			  field->type == PT_GROUP ? xcat(itoa_js(field->number), ", ") : "");
		} else if (proto_field_is_packable(field)) {
			P("      reader.read$reader$(msg.get$name$());\n", "reader",
			  js_binary_read_write_method_name(field, false), "name", js_getter_name_ex(field, BYTES_DEFAULT, false));
			P("      break;\n");
			return;
		} else {
			P("      var value = /** @type {$fieldtype$} */ "
			  "(reader.read$reader$());\n",
			  "fieldtype", js_field_type_annotation(o, field, false, true, true, BYTES_U8, false), "reader",
			  js_binary_read_write_method_name(field, false));
		}
		if (proto_field_is_repeated(field))
			P("      msg.add$name$(value);\n", "name", js_getter_name_ex(field, BYTES_DEFAULT, true));
		else
			/* Singular fields, and packed repeated fields, receive a |value| either
			 * as the field's value or as the array of all the field's values; set
			 * this as the field's value directly. */
			P("      msg.set$name$(value);\n", "name", js_getter_name(field));
	}
	P("      break;\n");
}

static void generate_class_deserialize_binary(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	P("/**\n"
	  " * Deserializes binary data (in protobuf wire format).\n"
	  " * @param {jspb.binary.bytesource.ByteSource} bytes The bytes to deserialize.\n"
	  " * @return {!$class$}\n"
	  " */\n"
	  "$class$.deserializeBinary = function(bytes) {\n"
	  "  var reader = new jspb.BinaryReader(bytes);\n"
	  "  var msg = new $class$;\n"
	  "  return $class$.deserializeBinaryFromReader(msg, reader);\n"
	  "};\n"
	  "\n"
	  "\n"
	  "/**\n"
	  " * Deserializes binary data (in protobuf wire format) from the\n"
	  " * given reader into the given message object.\n"
	  " * @param {!$class$} msg The message object to deserialize into.\n"
	  " * @param {!jspb.BinaryReader} reader The BinaryReader to use.\n"
	  " * @return {!$class$}\n"
	  " */\n"
	  "$class$.deserializeBinaryFromReader = function(msg, reader) {\n"
	  "  while (reader.nextField()) {\n",
	  "class", get_message_path(o, desc));
	P("    if (reader.isEndGroup()) {\n"
	  "      break;\n"
	  "    }\n"
	  "    var field = reader.getFieldNumber();\n"
	  "    switch (field) {\n");
	for (int i = 0; i < desc->n_fields; i++)
		generate_class_deserialize_binary_field(o, printer, &desc->fields[i]);
	P("    default:\n");
	if (is_extendable(desc))
		P("      jspb.Message.readBinaryExtension(msg, reader,\n"
		  "        $extobj$Binary,\n"
		  "        $class$.prototype.getExtension,\n"
		  "        $class$.prototype.setExtension);\n"
		  "      break;\n"
		  "    }\n",
		  "extobj", js_extensions_object_name(o, desc->file, desc), "class", get_message_path(o, desc));
	else
		P("      reader.skipField();\n"
		  "      break;\n"
		  "    }\n");
	P("  }\n"
	  "  return msg;\n"
	  "};\n"
	  "\n"
	  "\n");
}

/* Generates the code to access a single field value for the binary serializer. */
static void generate_class_serialize_binary_field_access(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	if (lp_field_cpp_type(field) != LP_CPPTYPE_MESSAGE)
		P("    /** @type {$type$} */ "
		  "(message.internal_getField($index$))",
		  "index", js_field_index(field), "type", js_type_annotation(o, field, false, false, false));
	else
		P("    message.get$name$($nolazy$)", "name", js_getter_name_ex(field, BYTES_U8, false),
		  /* No lazy creation for maps containers -- fastpath the empty case. */
		  "nolazy", proto_field_is_map(field) ? "true" : "");
}

static void generate_class_serialize_binary_field(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	if (has_field_presence(field) && lp_field_cpp_type(field) != LP_CPPTYPE_MESSAGE)
		P("  f = /** @type {$type$} */ "
		  "(jspb.Message.getField(message, $index$));\n",
		  "index", js_field_index(field), "type", js_type_annotation(o, field, false, false, false));
	else
		P("  f = message.get$name$($nolazy$);\n", "name", js_getter_name_ex(field, BYTES_U8, false), "nolazy",
		  proto_field_is_map(field) ? "true" : "");

	/* Print an `if (condition)` statement that evaluates to true if the field
	 * goes on the wire. */
	if (proto_field_is_map(field)) {
		P("  if (f && f.getLength() > 0) {\n");
	} else if (proto_field_is_repeated(field)) {
		P("  if (f.length > 0) {\n");
	} else if (has_field_presence(field)) {
		P("  if (f != null) {\n");
	} else {
		switch (lp_field_cpp_type(field)) {
		case LP_CPPTYPE_INT32:
		case LP_CPPTYPE_INT64:
		case LP_CPPTYPE_UINT32:
		case LP_CPPTYPE_UINT64:
			if (is_integral_field_with_string_js_type(field))
				P("  if (parseInt(f, 10) !== 0) {\n");
			else
				P("  if (f !== 0) {\n");
			break;
		case LP_CPPTYPE_ENUM:
		case LP_CPPTYPE_FLOAT:
		case LP_CPPTYPE_DOUBLE: P("  if (f !== 0.0) {\n"); break;
		case LP_CPPTYPE_BOOL: P("  if (f) {\n"); break;
		case LP_CPPTYPE_STRING: P("  if (f.length > 0) {\n"); break;
		default: break;
		}
	}

	/* Write the field on the wire. */
	if (proto_field_is_map(field)) {
		const LpField *key_field = map_field_key(field);
		const LpField *value_field = map_field_value(field);
		P("jspb.internal.public_for_gencode.serializeMapToBinary(\n");
		generate_class_serialize_binary_field_access(o, printer, field);
		P(",\n    $index$,\n"
		  "    writer,\n"
		  "    $keyWriterFn$,\n"
		  "    $valueWriterFn$",
		  "index", itoa_js(field->number), "keyWriterFn", js_binary_writer_method_name(key_field), "valueWriterFn",
		  js_binary_writer_method_name(value_field));
		if (value_field->type == PT_MESSAGE)
			P(",\n    $messageType$.serializeBinaryToWriter", "messageType",
			  get_message_path(o, value_field->message_type));
		P(");\n");
	} else {
		P("    writer.write$method$(\n"
		  "      $index$,\n"
		  "      f",
		  "method", js_binary_read_write_method_name(field, true), "index", itoa_js(field->number));
		if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE && !proto_field_is_map(field))
			P(",\n"
			  "      $submsg$.serializeBinaryToWriter\n",
			  "submsg", submessage_type_ref(o, field));
		else
			P("\n");
		P("    );\n");
	}
	/* Close the `if`. */
	P("  }\n");
}

static void generate_class_serialize_binary(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	P("/**\n"
	  " * Serializes the message to binary data (in protobuf wire format).\n"
	  " * @return {!Uint8Array}\n"
	  " */\n"
	  "$class$.prototype.serializeBinary = function() {\n"
	  "  var writer = new jspb.BinaryWriter();\n"
	  "  $class$.serializeBinaryToWriter(this, writer);\n"
	  "  return writer.getResultBuffer();\n"
	  "};\n"
	  "\n"
	  "\n"
	  "/**\n"
	  " * Serializes the given message to binary data (in protobuf wire\n"
	  " * format), writing to the given BinaryWriter.\n"
	  " * @param {!$class$} message\n"
	  " * @param {!jspb.BinaryWriter} writer\n"
	  " * @suppress {unusedLocalVariables} f is only used for nested messages\n"
	  " */\n"
	  "$class$.serializeBinaryToWriter = function(message, "
	  "writer) {\n"
	  "  var f = undefined;\n",
	  "class", get_message_path(o, desc));
	for (int i = 0; i < desc->n_fields; i++)
		generate_class_serialize_binary_field(o, printer, &desc->fields[i]);
	if (is_extendable(desc))
		P("  jspb.Message.serializeBinaryExtensions(message, writer,\n"
		  "    $extobj$Binary, $class$.prototype.getExtension);\n",
		  "extobj", js_extensions_object_name(o, desc->file, desc), "class", get_message_path(o, desc));
	P("};\n"
	  "\n"
	  "\n");
}

static void generate_enum(const JsOptions *o, LpPrinter *printer, const LpEnum *enumdesc)
{
	P("/**\n"
	  " * @enum {number}\n"
	  " */\n"
	  "$enumprefix$$name$ = {\n",
	  "enumprefix", get_enum_path_prefix(o, enumdesc), "name", enumdesc->name);
	lp_annotate(printer, "name", enumdesc);
	StrSet used_name = {0};
	int *valid_index = xalloc(sizeof(int) * (size_t)(enumdesc->n_values + 1));
	int n_valid = 0;
	bool allow_alias = pb_int(enumdesc->options, ENUMOPT_ALLOW_ALIAS, 0) != 0;
	for (int i = 0; i < enumdesc->n_values; i++) {
		if (allow_alias && !strset_insert(&used_name, to_enum_case(enumdesc->values[i].name)))
			continue;
		valid_index[n_valid++] = i;
	}
	for (int k = 0; k < n_valid; k++) {
		const LpEnumValue *value = &enumdesc->values[valid_index[k]];
		P("  $name$: $value$$comma$\n", "name", to_enum_case(value->name), "value", itoa_js(value->number), "comma",
		  valid_index[k] == valid_index[n_valid - 1] ? "" : ",");
		lp_annotate(printer, "name", value);
	}
	P("};\n"
	  "\n");
}

static void generate_extension(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	const char *extension_scope =
		field->extension_scope ? get_message_path(o, field->extension_scope) : get_namespace(o, field->file);
	const char *extension_object_name = js_object_field_name(field);
	bool is_message = lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE;
	P("\n"
	  "/**\n"
	  " * A tuple of {field number, class constructor} for the extension\n"
	  " * field named `$nameInComment$`.\n"
	  " * @type {!jspb.ExtensionFieldInfo<$extensionType$>}\n"
	  " */\n"
	  "$class$.$name$ = new jspb.ExtensionFieldInfo(\n",
	  "nameInComment", extension_object_name, "name", extension_object_name, "class", extension_scope,
	  "extensionType", js_type_annotation(o, field, false, true, false));
	lp_annotate(printer, "name", field);
	P("    $index$,\n"
	  "    {$name$: 0},\n"
	  "    $ctor$,\n"
	  "     /** @type {?function((boolean|undefined),!jspb.Message=): "
	  "!Object} */ (\n"
	  "         $toObject$),\n"
	  "    $repeated$);\n",
	  "index", itoa_js(field->number), "name", extension_object_name, "ctor",
	  is_message ? submessage_type_ref(o, field) : "null", "toObject",
	  is_message ? xcat(submessage_type_ref(o, field), ".toObject") : "null", "repeated",
	  proto_field_is_repeated(field) ? "1" : "0");
	P("\n"
	  "$extendName$Binary[$index$] = new jspb.ExtensionFieldBinaryInfo(\n"
	  "    $class$.$name$,\n"
	  "    $binaryReaderFn$,\n"
	  "    $binaryWriterFn$,\n"
	  "    $binaryMessageSerializeFn$,\n"
	  "    $binaryMessageDeserializeFn$,\n",
	  "extendName", js_extensions_object_name(o, field->file, field->containing_type), "index",
	  itoa_js(field->number), "class", extension_scope, "name", extension_object_name, "binaryReaderFn",
	  js_binary_reader_method_name(field), "binaryWriterFn", js_binary_writer_method_name(field),
	  "binaryMessageSerializeFn", is_message ? xcat(submessage_type_ref(o, field), ".serializeBinaryToWriter") : "undefined",
	  "binaryMessageDeserializeFn",
	  is_message ? xcat(submessage_type_ref(o, field), ".deserializeBinaryFromReader") : "undefined");
	P("    $isPacked$);\n", "isPacked", proto_field_is_packable(field) ? "true" : "false");
	P("// This registers the extension field with the extended class, so that\n"
	  "// toObject() will function correctly.\n"
	  "$extendName$[$index$] = $class$.$name$;\n"
	  "\n",
	  "extendName", js_extensions_object_name(o, field->file, field->containing_type), "index",
	  itoa_js(field->number), "class", extension_scope, "name", extension_object_name);
}

static void generate_bytes_wrapper(const JsOptions *o, LpPrinter *printer, const LpField *field, int bytes_mode)
{
	char *type = js_field_type_annotation(o, field, false, false, false, bytes_mode, false);
	P("/**\n"
	  " * $fielddef$\n"
	  "$comment$"
	  " * This is a type-conversion wrapper around `get$defname$()`\n"
	  " * @return {$type$}\n"
	  " */\n"
	  "$class$.prototype.get$name$ = function() {\n"
	  "  return /** @type {$type$} */ (jspb.Message.bytes$list$As$suffix$(\n"
	  "      this.get$defname$()));\n"
	  "};\n"
	  "\n"
	  "\n",
	  "fielddef", field_definition(o, field), "comment", field_comments(field, bytes_mode), "type", type, "class",
	  get_message_path(o, field->containing_type), "name", js_getter_name_ex(field, bytes_mode, false), "list",
	  proto_field_is_repeated(field) ? "List" : "", "suffix", js_byte_getter_suffix(bytes_mode), "defname",
	  js_getter_name_ex(field, BYTES_DEFAULT, false));
}

static void generate_repeated_primitive_helper_methods(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	P("/**\n"
	  " * @param {$optionaltype$} value\n"
	  " * @param {number=} opt_index\n"
	  " * @return {!$class$} returns this\n"
	  " */\n"
	  "$class$.prototype.$addername$ = function(value, opt_index) {\n"
	  "  return jspb.Message.addToRepeatedField(this, "
	  "$index$",
	  "class", get_message_path(o, field->containing_type), "addername",
	  xcat("add", js_getter_name_ex(field, BYTES_DEFAULT, true)), "optionaltype",
	  js_field_type_annotation(o, field, false, true, false, BYTES_DEFAULT, true), "index", js_field_index(field));
	lp_annotate(printer, "addername", field);
	P("$oneofgroup$, $type$value$rptvalueinit$$typeclose$, "
	  "opt_index);\n"
	  "};\n"
	  "\n"
	  "\n",
	  "type", "", "typeclose", "", "oneofgroup", in_real_oneof(field) ? xcat(", ", js_oneof_array(o, field)) : "",
	  "rptvalueinit", "");
}

static void generate_repeated_message_helper_methods(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	P("/**\n"
	  " * @param {!$optionaltype$=} opt_value\n"
	  " * @param {number=} opt_index\n"
	  " * @return {!$optionaltype$}\n"
	  " */\n"
	  "$class$.prototype.$addername$ = function(opt_value, opt_index) {\n"
	  "  return jspb.Message.addTo$repeatedtag$WrapperField(",
	  "optionaltype", js_type_name(o, field, BYTES_DEFAULT), "class", get_message_path(o, field->containing_type),
	  "addername", xcat("add", js_getter_name_ex(field, BYTES_DEFAULT, true)), "repeatedtag",
	  proto_field_is_repeated(field) ? "Repeated" : "");
	lp_annotate(printer, "addername", field);
	P("this, $index$$oneofgroup$, opt_value, $ctor$, opt_index);\n"
	  "};\n"
	  "\n"
	  "\n",
	  "index", js_field_index(field), "oneofgroup", in_real_oneof(field) ? xcat(", ", js_oneof_array(o, field)) : "",
	  "ctor", get_message_path(o, field->message_type));
}

static void generate_class_field(const JsOptions *o, LpPrinter *printer, const LpField *field)
{
	const char *klass = get_message_path(o, field->containing_type);
	if (proto_field_is_map(field)) {
		const LpField *key_field = map_field_key(field);
		const LpField *value_field = map_field_value(field);
		/* Map field: special handling to instantiate the map object on demand. */
		char *key_type = js_type_annotation(o, key_field, false, true, false);
		char *value_type = js_type_annotation(o, value_field, false, true, false);
		P("/**\n"
		  " * $fielddef$\n"
		  " * @param {boolean=} opt_noLazyCreate Do not create the map if\n"
		  " * empty, instead returning `undefined`\n"
		  " * @return {!jspb.Map<$keytype$,$valuetype$>}\n"
		  " */\n",
		  "fielddef", field_definition(o, field), "keytype", key_type, "valuetype", value_type);
		P("$class$.prototype.$gettername$ = function(opt_noLazyCreate) {\n"
		  "  return /** @type {!jspb.Map<$keytype$,$valuetype$>} */ (\n",
		  "class", klass, "gettername", xcat("get", js_getter_name(field)), "keytype", key_type, "valuetype",
		  value_type);
		lp_annotate(printer, "gettername", field);
		P("      jspb.Message.getMapField(this, $index$, opt_noLazyCreate", "index", js_field_index(field));
		if (value_field->type == PT_MESSAGE)
			P(",\n"
			  "      $messageType$",
			  "messageType", get_message_path(o, value_field->message_type));
		else
			P(",\n"
			  "      null");
		P("));\n");
		P("};\n"
		  "\n"
		  "\n");
	} else if (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE) {
		/* Message field: special handling in order to wrap the underlying data
		 * array with a message object. */
		P("/**\n"
		  " * $fielddef$\n"
		  "$comment$"
		  " * @return {$type$}\n"
		  " */\n",
		  "fielddef", field_definition(o, field), "comment", field_comments(field, BYTES_DEFAULT), "type",
		  js_type_annotation(o, field, false, false, false));
		P("$class$.prototype.$gettername$ = function() {\n"
		  "  return /** @type{$type$} */ (\n"
		  "    jspb.Message.get$rpt$WrapperField(this, $wrapperclass$, "
		  "$index$$required$));\n"
		  "};\n"
		  "\n"
		  "\n",
		  "class", klass, "gettername", xcat("get", js_getter_name(field)), "type",
		  js_type_annotation(o, field, false, false, false), "rpt", proto_field_is_repeated(field) ? "Repeated" : "",
		  "index", js_field_index(field), "wrapperclass", submessage_type_ref(o, field), "required",
		  proto_field_is_required(field) ? ", 1" : "");
		lp_annotate(printer, "gettername", field);
		P("/**\n"
		  " * @param {$optionaltype$} value\n"
		  " * @return {!$class$} returns this\n"
		  "*/\n"
		  "$class$.prototype.$settername$ = function(value) {\n"
		  "  return jspb.Message.set$oneoftag$$repeatedtag$WrapperField(",
		  "optionaltype", js_type_annotation(o, field, true, false, false), "class", klass, "settername",
		  xcat("set", js_getter_name(field)), "oneoftag", in_real_oneof(field) ? "Oneof" : "", "repeatedtag",
		  proto_field_is_repeated(field) ? "Repeated" : "");
		lp_annotate(printer, "settername", field);
		P("this, $index$$oneofgroup$, value);\n"
		  "};\n"
		  "\n"
		  "\n",
		  "index", js_field_index(field), "oneofgroup",
		  in_real_oneof(field) ? xcat(", ", js_oneof_array(o, field)) : "");
		if (proto_field_is_repeated(field))
			generate_repeated_message_helper_methods(o, printer, field);
	} else {
		/* Simple (primitive) field, either singular or repeated. */
		int bytes_mode = field->type == PT_BYTES && !o->binary ? BYTES_B64 : BYTES_DEFAULT;
		char *typed_annotation = js_field_type_annotation(o, field, false, false, false, bytes_mode, false);
		P("/**\n"
		  " * $fielddef$\n"
		  "$comment$"
		  " * @return {$type$}\n"
		  " */\n",
		  "fielddef", field_definition(o, field), "comment", field_comments(field, bytes_mode), "type",
		  typed_annotation);
		P("$class$.prototype.$gettername$ = function() {\n", "class", klass, "gettername",
		  xcat("get", js_getter_name(field)));
		lp_annotate(printer, "gettername", field);
		P("  return /** @type {$type$} */ (", "type", typed_annotation);
		bool use_default = !returns_null_when_unset(field);
		/* Repeated fields get initialized to their default in the constructor
		 * (why?), so we emit a plain getField() call for them. */
		if (proto_field_is_repeated(field))
			use_default = false;
		generate_field_value_expression(printer, "this", field, use_default);
		P(");\n"
		  "};\n"
		  "\n"
		  "\n");
		if (field->type == PT_BYTES) {
			generate_bytes_wrapper(o, printer, field, BYTES_B64);
			generate_bytes_wrapper(o, printer, field, BYTES_U8);
		}
		P("/**\n"
		  " * @param {$optionaltype$} value\n"
		  " * @return {!$class$} returns this\n"
		  " */\n",
		  "class", klass, "optionaltype", js_type_annotation(o, field, true, false, false));
		if (!proto_field_is_repeated(field) && !proto_field_is_map(field) && !proto_field_has_presence(field)) {
			/* Proto3 non-repeated and non-map fields without presence use the
			 * setProto3*Field function. */
			P("$class$.prototype.$settername$ = function(value) {\n"
			  "  return jspb.Message.setProto3$typetag$Field(this, $index$, "
			  "value);"
			  "\n"
			  "};\n"
			  "\n"
			  "\n",
			  "class", klass, "settername", xcat("set", js_getter_name(field)), "typetag", js_type_tag(field), "index",
			  js_field_index(field));
			lp_annotate(printer, "settername", field);
		} else {
			/* Otherwise, use the regular setField function. */
			P("$class$.prototype.$settername$ = function(value) {\n"
			  "  return jspb.Message.set$oneoftag$Field(this, $index$",
			  "class", klass, "settername", xcat("set", js_getter_name(field)), "oneoftag",
			  in_real_oneof(field) ? "Oneof" : "", "index", js_field_index(field));
			lp_annotate(printer, "settername", field);
			P("$oneofgroup$, $type$value$rptvalueinit$$typeclose$);\n"
			  "};\n"
			  "\n"
			  "\n",
			  "type", "", "typeclose", "", "oneofgroup",
			  in_real_oneof(field) ? xcat(", ", js_oneof_array(o, field)) : "", "rptvalueinit",
			  proto_field_is_repeated(field) ? " || []" : "");
		}
		if (proto_field_is_repeated(field))
			generate_repeated_primitive_helper_methods(o, printer, field);
	}

	/* Generate clearFoo() method for map fields, repeated fields, and other
	 * fields with presence. */
	if (proto_field_is_map(field)) {
		P("/**\n"
		  " * Clears values from the map. The map will be non-null.\n"
		  " * @return {!$class$} returns this\n"
		  " */\n"
		  "$class$.prototype.$clearername$ = function() {\n"
		  "  this.$gettername$().clear();\n"
		  "  return this;\n"
		  "};\n"
		  "\n"
		  "\n",
		  "class", klass, "clearername", xcat("clear", js_getter_name(field)), "gettername",
		  xcat("get", js_getter_name(field)));
		lp_annotate(printer, "clearername", field);
	} else if (proto_field_is_repeated(field) ||
		   (lp_field_cpp_type(field) == LP_CPPTYPE_MESSAGE && !proto_field_is_required(field))) {
		/* Fields where we can delegate to the regular setter. */
		P("/**\n"
		  " * $jsdoc$\n"
		  " * @return {!$class$} returns this\n"
		  " */\n"
		  "$class$.prototype.$clearername$ = function() {\n"
		  "  return this.$settername$($clearedvalue$);\n"
		  "};\n"
		  "\n"
		  "\n",
		  "jsdoc",
		  proto_field_is_repeated(field) ? "Clears the list making it empty but non-null."
						 : "Clears the message field making it undefined.",
		  "class", klass, "clearername", xcat("clear", js_getter_name(field)), "settername",
		  xcat("set", js_getter_name(field)), "clearedvalue", proto_field_is_repeated(field) ? "[]" : "undefined");
		lp_annotate(printer, "clearername", field);
	} else if (has_field_presence(field)) {
		/* Fields where we can't delegate to the regular setter because it doesn't
		 * accept "undefined" as an argument. */
		P("/**\n"
		  " * Clears the field making it undefined.\n"
		  " * @return {!$class$} returns this\n"
		  " */\n"
		  "$class$.prototype.$clearername$ = function() {\n"
		  "  return jspb.Message.set$maybeoneof$Field(this, "
		  "$index$$maybeoneofgroup$, ",
		  "class", klass, "clearername", xcat("clear", js_getter_name(field)), "maybeoneof",
		  in_real_oneof(field) ? "Oneof" : "", "maybeoneofgroup",
		  in_real_oneof(field) ? xcat(", ", js_oneof_array(o, field)) : "", "index", js_field_index(field));
		lp_annotate(printer, "clearername", field);
		P("$clearedvalue$);\n"
		  "};\n"
		  "\n"
		  "\n",
		  "clearedvalue", proto_field_is_repeated(field) ? "[]" : "undefined");
	}

	if (has_field_presence(field)) {
		P("/**\n"
		  " * Returns whether this field is set.\n"
		  " * @return {boolean}\n"
		  " */\n"
		  "$class$.prototype.$hasername$ = function() {\n"
		  "  return jspb.Message.getField(this, $index$) != null;\n"
		  "};\n"
		  "\n"
		  "\n",
		  "class", klass, "hasername", xcat("has", js_getter_name(field)), "index", js_field_index(field));
		lp_annotate(printer, "hasername", field);
	}
}

static void generate_class(const JsOptions *o, LpPrinter *printer, const LpMessage *desc)
{
	if (ignore_message(desc))
		return;
	P("\n");
	generate_class_field_info(o, printer, desc);
	generate_class_to_object(o, printer, desc);
	/* These must come *before* the extension-field info generation in
	 * GenerateClassRegistration so that references to the binary
	 * serialization/deserialization functions may be placed in the extension
	 * objects. */
	generate_class_deserialize_binary(o, printer, desc);
	generate_class_serialize_binary(o, printer, desc);
	/* Recurse on nested types. */
	for (int i = 0; i < desc->n_enums; i++)
		generate_enum(o, printer, &desc->enums[i]);
	for (int i = 0; i < desc->n_nested; i++)
		generate_class(o, printer, &desc->nested[i]);
	/* GenerateClassRegistration: register any extensions defined inside this message type. */
	for (int i = 0; i < desc->n_extensions; i++)
		if (should_generate_extension(&desc->extensions[i]))
			generate_extension(o, printer, &desc->extensions[i]);
	/* GenerateClassFields */
	for (int i = 0; i < desc->n_fields; i++)
		generate_class_field(o, printer, &desc->fields[i]);
	if (o->import_style != IMPORT_CLOSURE)
		for (int i = 0; i < desc->n_extensions; i++)
			generate_extension(o, printer, &desc->extensions[i]);
}

/* Generate definitions for all message classes and enums. */
static void generate_classes_and_enums(const JsOptions *o, LpPrinter *printer, const LpFile *file)
{
	for (int i = 0; i < file->n_messages; i++)
		generate_class_constructor_and_declare_extension_field_info(o, printer, &file->messages[i]);
	for (int i = 0; i < file->n_messages; i++)
		generate_class(o, printer, &file->messages[i]);
	for (int i = 0; i < file->n_enums; i++)
		generate_enum(o, printer, &file->enums[i]);
}

/* GenerateFilesInDepOrder with GenerateFileAndDeps (iterative). */
static void generate_files_in_dep_order(const JsOptions *o, LpPrinter *printer, LpFile *const *files, int n)
{
	Map all_files = {0}, generated = {0};
	for (int i = 0; i < n; i++)
		map_put(&all_files, ptr_key(files[i]), files[i]);
	typedef struct {
		const LpFile *file;
		int next;
	} Frame;
	Frame *stack = NULL;
	int depth = 0, cap = 0;
	for (int i = 0; i < n; i++) {
		if (map_has(&generated, ptr_key(files[i])))
			continue;
		map_put(&generated, ptr_key(files[i]), files[i]);
		if (depth == cap) {
			int c = cap ? 2 * cap : 16;
			stack = xrealloc(stack, sizeof *stack * (size_t)cap, sizeof *stack * (size_t)c);
			cap = c;
		}
		stack[depth++] = (Frame){files[i], 0};
		while (depth) {
			Frame *f = &stack[depth - 1];
			if (f->next < f->file->n_dependencies) {
				const LpFile *dep = f->file->dependencies[f->next++];
				if (map_has(&generated, ptr_key(dep)))
					continue;
				map_put(&generated, ptr_key(dep), (void *)dep);
				if (depth == cap) {
					int c = 2 * cap;
					stack = xrealloc(stack, sizeof *stack * (size_t)cap, sizeof *stack * (size_t)c);
					cap = c;
				}
				stack[depth++] = (Frame){dep, 0};
				continue;
			}
			/* Only generate if the file is part of the original set requested. */
			if (map_has(&all_files, ptr_key(f->file)))
				generate_classes_and_enums(o, printer, f->file);
			depth--;
		}
	}
}

static void generate_file_contents(const JsOptions *o, LpPrinter *printer, const LpFile *file)
{
	generate_header(file, printer);

	/* Generate "require" statements. */
	if (o->import_style == IMPORT_COMMONJS || o->import_style == IMPORT_COMMONJS_STRICT) {
		P("var jspb = require('google-protobuf');\n");
		P("var goog = jspb;\n");
		/* Do not use global scope in strict mode */
		if (o->import_style == IMPORT_COMMONJS_STRICT)
			P("var proto = {};\n\n");
		else
			P("var global = globalThis;\n\n");
		for (int i = 0; i < file->n_dependencies; i++) {
			const char *name = file->dependencies[i]->name;
			P("var $alias$ = require('$file$');\n"
			  "goog.object.extend(proto, $alias$);\n",
			  "alias", module_alias(name), "file", xcat(get_root_path(file->name, name), get_js_filename(o, name)));
		}
	}

	StrSet provided = {0};
	for (int i = 0; i < file->n_extensions; i++)
		strset_insert(&provided, xcat(get_namespace(o, file), ".", js_object_field_name(&file->extensions[i])));
	find_provides_for_file(o, file, &provided);
	generate_provides(o, printer, &provided);
	if (o->import_style == IMPORT_CLOSURE) {
		LpFile *files[1] = {(LpFile *)file};
		generate_requires_for_library(o, printer, files, 1, &provided);
	}

	generate_classes_and_enums(o, printer, file);

	/* Generate code for top-level extensions (std::set<const FieldDescriptor*>: the extensions of a
	 * file are one array, so pointer order is declaration order). */
	for (int i = 0; i < file->n_extensions; i++)
		generate_extension(o, printer, &file->extensions[i]);

	/* if provided is empty, do not export anything */
	if (o->import_style == IMPORT_COMMONJS && provided.v.n)
		P("goog.object.extend(exports, $package$);\n", "package", get_namespace(o, file));
	else if (o->import_style == IMPORT_COMMONJS_STRICT)
		P("goog.object.extend(exports, proto);\n", "package", get_namespace(o, file));

	/* Emit well-known type methods. */
	char *js_name = xcat(lp_strip_proto(file->name), ".js");
	for (const FileToc *toc = well_known_types_js; toc->name; toc++)
		if (str_eq(xcat("google/protobuf/", toc->name), js_name))
			lp_print(printer, toc->data, NULL);
}

/* A new output file with its printer (and annotation collector). */
static void open_printer(const JsOptions *o, LpContext *ctx, const char *filename, LpPrinter *printer, Buf *annotations)
{
	lp_printer_init(printer, lp_context_open(ctx, filename), '$');
	*annotations = (Buf){0};
	if (o->annotate_code)
		printer->annotations = annotations;
}

static void close_printer(const JsOptions *o, LpPrinter *printer, const Buf *annotations)
{
	if (o->annotate_code)
		embed_code_annotations(annotations, printer);
}

/* Generator::GenerateFile(file, options, context, use_short_name) */
static void generate_file(const LpFile *file, const JsOptions *o, LpContext *ctx, bool use_short_name)
{
	const char *name = file->name;
	if (use_short_name) {
		const char *slash = strrchr(name, '/');
		if (!slash)
			fail("basic_string::substr: __pos (which is 18446744073709551615) > this->size()");
		name = slash;
	}
	LpPrinter printer;
	Buf annotations;
	open_printer(o, ctx, xcat(o->output_dir, "/", get_js_filename(o, name)), &printer, &annotations);
	generate_file_contents(o, &printer, file);
	close_printer(o, &printer, &annotations);
}

/* GeneratorOptions::ParseFromOptions */
static bool parse_from_options(JsOptions *o, char **keys, char **values, int n, Buf *error)
{
	for (int i = 0; i < n; i++) {
		const char *k = keys[i], *v = values[i];
		if (str_eq(k, "add_require_for_enums")) {
			if (*v) {
				buf_adds(error, "Unexpected option value for add_require_for_enums");
				return false;
			}
			o->add_require_for_enums = true;
		} else if (str_eq(k, "binary")) {
			if (*v) {
				buf_adds(error, "Unexpected option value for binary");
				return false;
			}
			o->binary = true;
		} else if (str_eq(k, "testonly")) {
			if (*v) {
				buf_adds(error, "Unexpected option value for testonly");
				return false;
			}
			o->testonly = true;
		} else if (str_eq(k, "error_on_name_conflict")) {
			/* ignored (upstream logs a warning) */
		} else if (str_eq(k, "output_dir")) {
			o->output_dir = v;
		} else if (str_eq(k, "namespace_prefix")) {
			o->namespace_prefix = v;
		} else if (str_eq(k, "library")) {
			o->library = v;
		} else if (str_eq(k, "import_style")) {
			if (str_eq(v, "closure"))
				o->import_style = IMPORT_CLOSURE;
			else if (str_eq(v, "commonjs"))
				o->import_style = IMPORT_COMMONJS;
			else if (str_eq(v, "commonjs_strict"))
				o->import_style = IMPORT_COMMONJS_STRICT;
			else if (str_eq(v, "browser"))
				o->import_style = IMPORT_BROWSER;
			else if (str_eq(v, "es6"))
				o->import_style = IMPORT_ES6;
			else {
				/* the error is set but parsing goes on */
				error->n = 0;
				buf_adds(error, xcat("Unknown import style ", v, ", expected one of: closure, commonjs, browser, es6."));
			}
		} else if (str_eq(k, "extension")) {
			o->extension = v;
		} else if (str_eq(k, "one_output_file_per_input_file")) {
			if (*v) {
				buf_adds(error, "Unexpected option value for one_output_file_per_input_file");
				return false;
			}
			o->one_output_file_per_input_file = true;
		} else if (str_eq(k, "annotate_code")) {
			if (*v) {
				buf_adds(error, "Unexpected option value for annotate_code");
				return false;
			}
			o->annotate_code = true;
		} else {
			/* Assume any other option is an output directory, as long as it is a bare
			 * `key` rather than a `key=value` option. */
			if (*v) {
				buf_adds(error, xcat("Unknown option: ", k));
				return false;
			}
			o->output_dir = k;
		}
	}
	if (o->import_style != IMPORT_CLOSURE && (o->add_require_for_enums || o->testonly || *o->library ||
						  !str_eq(o->extension, ".js") || o->one_output_file_per_input_file)) {
		buf_adds(error, "The add_require_for_enums, testonly, library, extension, and "
				"one_output_file_per_input_file options should only be "
				"used for import_style=closure");
		return false;
	}
	return true;
}

/* GeneratorOptions::output_mode */
static int output_mode(const JsOptions *o)
{
	if (o->import_style != IMPORT_CLOSURE || o->one_output_file_per_input_file)
		return ONE_OUTPUT_FILE_PER_INPUT_FILE;
	if (*o->library)
		return EVERYTHING_IN_ONE_FILE;
	return ONE_OUTPUT_FILE_PER_SCC;
}

static void generate_everything_in_one_file(const JsOptions *o, LpFile *const *files, int n, LpContext *ctx)
{
	LpPrinter pr, *printer = &pr;
	Buf annotations;
	open_printer(o, ctx, xcat(o->output_dir, "/", o->library, file_name_extension(o)), printer, &annotations);
	/* Pull out all extensions -- we need these to generate all provides/requires. */
	Vec extensions = {0};
	for (int i = 0; i < n; i++)
		for (int j = 0; j < files[i]->n_extensions; j++)
			vec_push(&extensions, &files[i]->extensions[j]);
	generate_header(n == 1 ? files[0] : NULL, printer);
	StrSet provided = {0};
	find_provides(o, printer, files, n, &provided);
	find_provides_for_fields(o, (const LpField *const *)extensions.v, extensions.n, &provided);
	generate_provides(o, printer, &provided);
	generate_test_only(o, printer);
	generate_requires_for_library(o, printer, files, n, &provided);
	generate_files_in_dep_order(o, printer, files, n);
	for (int i = 0; i < extensions.n; i++)
		if (should_generate_extension(extensions.v[i]))
			generate_extension(o, printer, extensions.v[i]);
	close_printer(o, printer, &annotations);
}

static void generate_one_file_per_scc(const JsOptions *o, LpFile *const *files, int n, LpContext *ctx)
{
	JsState st = {0};
	Map have_printed = {0};
	LpSccAnalyzer *analyzer = lp_scc_analyzer_new(deps_generator, NULL);
	Map allowed_map;
	generate_jspb_allowed_map(&st, o, files, n, &allowed_map, analyzer);

	bool generated = false;
	for (int i = 0; i < n; i++) {
		const LpFile *file = files[i];
		/* Force well known type to generate in a whole file. */
		if (is_well_known_type_file(file)) {
			generate_file(file, o, ctx, true);
			generated = true;
			continue;
		}
		for (int j = 0; j < file->n_messages; j++) {
			const LpMessage *desc = &file->messages[j];
			const LpScc *scc = lp_scc_get(analyzer, desc);
			if (map_has(&have_printed, ptr_key(desc)) || !map_has(&allowed_map, ptr_key(scc)))
				continue;
			generated = true;
			LpPrinter pr, *printer = &pr;
			Buf annotations;
			open_printer(o, ctx, map_get(&allowed_map, ptr_key(scc)), printer, &annotations);
			generate_header(file, printer);
			StrSet provided = {0};
			for (int k = 0; k < scc->n; k++)
				if (!scc->descriptors[k]->containing_type)
					find_provides_for_message(o, scc->descriptors[k], &provided);
			generate_provides(o, printer, &provided);
			generate_test_only(o, printer);
			generate_requires_for_scc(o, printer, scc, &provided);
			for (int k = 0; k < scc->n; k++)
				if (!scc->descriptors[k]->containing_type)
					generate_class_constructor_and_declare_extension_field_info(o, printer, scc->descriptors[k]);
			for (int k = 0; k < scc->n; k++)
				if (!scc->descriptors[k]->containing_type)
					generate_class(o, printer, scc->descriptors[k]);
			for (int k = 0; k < scc->n; k++)
				map_put(&have_printed, ptr_key(scc->descriptors[k]), (void *)scc->descriptors[k]);
			close_printer(o, printer, &annotations);
		}
		for (int j = 0; j < file->n_enums; j++) {
			const LpEnum *enumdesc = &file->enums[j];
			if (!map_has(&allowed_map, ptr_key(enumdesc)))
				continue;
			generated = true;
			LpPrinter pr, *printer = &pr;
			Buf annotations;
			open_printer(o, ctx, map_get(&allowed_map, ptr_key(enumdesc)), printer, &annotations);
			generate_header(file, printer);
			StrSet provided = {0};
			find_provides_for_enum(o, enumdesc, &provided);
			generate_provides(o, printer, &provided);
			generate_test_only(o, printer);
			generate_enum(o, printer, enumdesc);
			close_printer(o, printer, &annotations);
		}
		/* File-level extensions (message-level extensions are generated under
		 * the enclosing message). */
		if (map_has(&allowed_map, ptr_key(file))) {
			generated = true;
			LpPrinter pr, *printer = &pr;
			Buf annotations;
			open_printer(o, ctx, map_get(&allowed_map, ptr_key(file)), printer, &annotations);
			generate_header(file, printer);
			StrSet provided = {0};
			Vec fields = {0};
			for (int j = 0; j < file->n_extensions; j++)
				if (should_generate_extension(&file->extensions[j]))
					vec_push(&fields, &file->extensions[j]);
			find_provides_for_fields(o, (const LpField *const *)fields.v, fields.n, &provided);
			generate_provides(o, printer, &provided);
			generate_test_only(o, printer);
			generate_requires_for_extensions(o, printer, (const LpField *const *)fields.v, fields.n, &provided);
			for (int j = 0; j < file->n_extensions; j++)
				if (should_generate_extension(&file->extensions[j]))
					generate_extension(o, printer, &file->extensions[j]);
			close_printer(o, printer, &annotations);
		}
	}
	if (!generated)
		lp_context_open(ctx, xcat(o->output_dir, "/", "empty_no_content_void_file", file_name_extension(o)));
}

/* Generator::GenerateAll */
static COLD bool js_generate_all(const LpGenerator *g, LpFile *const *files, int n, const char *parameter, LpContext *ctx,
				 Buf *error)
{
	(void)g;
	js_cache = xalloc(sizeof *js_cache);
	char **keys, **values;
	int n_options = lp_parse_generator_parameter(parameter, &keys, &values);
	JsOptions o = {
		.output_dir = ".",
		.namespace_prefix = "",
		.import_style = IMPORT_CLOSURE,
		.library = "",
		.extension = ".js",
	};
	if (!parse_from_options(&o, keys, values, n_options, error))
		return false;
	switch (output_mode(&o)) {
	case EVERYTHING_IN_ONE_FILE: generate_everything_in_one_file(&o, files, n, ctx); break;
	case ONE_OUTPUT_FILE_PER_SCC: generate_one_file_per_scc(&o, files, n, ctx); break;
	default:
		/* Generate one output file per input (.proto) file. */
		for (int i = 0; i < n; i++)
			generate_file(files[i], &o, ctx, false);
		break;
	}
	return true;
}

/* Generator::Generate */
static COLD bool js_generate(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	(void)g, (void)file, (void)parameter, (void)ctx;
	buf_adds(error, "Unimplemented Generate() method. Call GenerateAll() instead.");
	return false;
}
