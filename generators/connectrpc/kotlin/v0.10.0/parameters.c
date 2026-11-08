// Copyright 2022-2026 The Connect Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* A port of protoc-gen-connect-kotlin/src/main/kotlin/com/connectrpc/protocgen/connect/internal/
 * Parameters.kt and ProtoHelpers.kt of connect-kotlin v0.10.0 (the option parsing) and of
 * SourceInfo.kt (comments by SourceCodeInfo path). */

/* Configuration */
typedef struct {
	bool generate_callback_methods, generate_coroutine_methods, generate_blocking_unary_methods;
} Configuration;

/* underscoresToCamelCaseImpl(input, capFirstLetter = false): characters other than ASCII letters
 * and digits are dropped (and capitalize the next letter). */
static const char *underscores_to_camel_case(Str in)
{
	Buf b = {0};
	bool cap = false;
	for (size_t i = 0; i < in.n; i++) {
		char c = in.p[i];
		if (c >= 'a' && c <= 'z') {
			buf_addc(&b, cap ? (char)(c - 'a' + 'A') : c);
			cap = false;
		} else if (c >= 'A' && c <= 'Z') {
			buf_addc(&b, i == 0 && !cap ? (char)(c - 'A' + 'a') : c);
			cap = false;
		} else if (c >= '0' && c <= '9') {
			buf_addc(&b, c);
			cap = true;
		} else {
			cap = true;
		}
	}
	return xstrdup(buf_str(&b));
}

/* String.toBoolean(): equals("true", ignoreCase = true) */
static bool kotlin_to_boolean(Str s)
{
	static const char t[] = "true";
	if (s.n != 4)
		return false;
	for (int i = 0; i < 4; i++)
		if ((s.p[i] | 0x20) != t[i])
			return false;
	return true;
}

/* parse(parseGeneratorParameter(input)): the last value of a key wins; keys are camel-cased. */
static Configuration parse_configuration(Str input)
{
	Configuration c = {false, true, false};
	Str *parts;
	int n = input.n ? str_split_s(input, ',', &parts) : 0;
	for (int i = 0; i < n; i++) {
		Str part = parts[i];
		if (!part.n)
			continue;
		const char *eq = memchr(part.p, '=', part.n);
		Str key = eq ? (Str){part.p, (size_t)(eq - part.p)} : part;
		Str value = eq ? (Str){eq + 1, part.n - (size_t)(eq - part.p) - 1} : STR_LIT("");
		const char *k = underscores_to_camel_case(key);
		if (str_eq(k, "generateCallbackMethods"))
			c.generate_callback_methods = kotlin_to_boolean(value);
		else if (str_eq(k, "generateCoroutineMethods"))
			c.generate_coroutine_methods = kotlin_to_boolean(value);
		else if (str_eq(k, "generateBlockingUnaryMethods"))
			c.generate_blocking_unary_methods = kotlin_to_boolean(value);
	}
	return c;
}

/* ---- SourceInfo.kt ---- */

/* SourceCodeHelper: the first location of the paths [6, service] and [6, service, 2, method]. */
typedef struct {
	Map first; /* "s" or "s.m" -> Location */
} SourceCodeHelper;

static SourceCodeHelper *source_code_helper(const PbMsg *file)
{
	SourceCodeHelper *h = xalloc(sizeof *h);
	PbMsg *sci = pb_msg(file, FDP_SOURCE_CODE_INFO);
	for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
		PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		int n = pb_count(loc, LOC_PATH);
		const char *key = NULL;
		if (n == 2 && pb_int_at(loc, LOC_PATH, 0) == FDP_SERVICE)
			key = xfmt("%lld", (long long)(int32_t)pb_int_at(loc, LOC_PATH, 1));
		else if (n == 4 && pb_int_at(loc, LOC_PATH, 0) == FDP_SERVICE && pb_int_at(loc, LOC_PATH, 2) == SDP_METHOD)
			key = xfmt("%lld.%lld", (long long)(int32_t)pb_int_at(loc, LOC_PATH, 1),
				   (long long)(int32_t)pb_int_at(loc, LOC_PATH, 3));
		if (key && !map_has(&h->first, key))
			map_put(&h->first, key, loc);
	}
	return h;
}

/* Char.isWhitespace() of Kotlin/JVM: Character.isWhitespace || Character.isSpaceChar */
static bool kotlin_is_whitespace(uint32_t c)
{
	return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0xa0 || c == 0x1680 ||
	       (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

static bool kotlin_is_blank(const char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		uint32_t c;
		i += (size_t)utf8_decode(s + i, n - i, &c);
		if (!kotlin_is_whitespace(c))
			return false;
	}
	return true;
}

/* getComment(path): the leading comments of the first location, or its trailing comments when
 * they are blank. */
static const char *source_comment(const SourceCodeHelper *h, const char *key)
{
	PbMsg *loc = map_get(&h->first, key);
	if (!loc)
		return "";
	const char *c = java_cstr(java_string(pb_bytes(loc, LOC_LEADING)));
	if (kotlin_is_blank(c))
		c = java_cstr(java_string(pb_bytes(loc, LOC_TRAILING)));
	return c;
}
