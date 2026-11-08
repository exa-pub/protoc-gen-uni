/* MIT License
 *
 * Copyright (c) 2022 SpiralScout
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

/* Ported from protoc_plugins/protoc-gen-php-grpc/php/keywords.go (roadrunner-server/grpc v6.0.0),
 * with the Go 1.27.1 semantics of strings.ToLower, strings.Split/Trim and the unicode case
 * mappings it relies on. */

/* utf8.AppendRune (invalid runes become U+FFFD) */
static void add_rune(Buf *b, uint32_t r)
{
	char p[4];
	int n;
	if (r < 0x80) {
		buf_addc(b, (char)r);
		return;
	}
	if (r > 0x10FFFF || (r >= 0xD800 && r <= 0xDFFF))
		r = 0xFFFD;
	if (r < 0x800) {
		p[0] = (char)(0xC0 | r >> 6);
		n = 2;
	} else if (r < 0x10000) {
		p[0] = (char)(0xE0 | r >> 12);
		p[1] = (char)(0x80 | ((r >> 6) & 0x3F));
		n = 3;
	} else {
		p[0] = (char)(0xF0 | r >> 18);
		p[1] = (char)(0x80 | ((r >> 12) & 0x3F));
		p[2] = (char)(0x80 | ((r >> 6) & 0x3F));
		n = 4;
	}
	p[n - 1] = (char)(0x80 | (r & 0x3F));
	buf_add(b, p, (size_t)n);
}

/* @see https://github.com/protocolbuffers/protobuf/blob/master/php/ext/google/protobuf/protobuf.c#L168
 * immutable */
static const char *const reservedKeywords[] = {
	"abstract", "and", "array", "as", "break",
	"callable", "case", "catch", "class", "clone",
	"const", "continue", "declare", "default", "die",
	"do", "echo", "else", "elseif", "empty",
	"enddeclare", "endfor", "endforeach", "endif", "endswitch",
	"endwhile", "eval", "exit", "extends", "final",
	"for", "foreach", "function", "global", "goto",
	"if", "implements", "include", "include_once", "instanceof",
	"insteadof", "interface", "isset", "list", "namespace",
	"new", "or", "print", "private", "protected",
	"public", "require", "require_once", "return", "static",
	"switch", "throw", "trait", "try", "unset",
	"use", "var", "while", "xor", "int",
	"float", "bool", "string", "true", "false",
	"null", "void", "iterable",
};

/* strings.ToLower: unicode.ToLower of every rune; an invalid UTF-8 byte becomes U+FFFD. */
static Str to_lower(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		i += (size_t)utf8_decode(s.p + i, s.n - i, &r);
		add_rune(&b, (uint32_t)go_std_to_lower((int32_t)r));
	}
	return buf_view(&b);
}

/* Check if given name/keyword is reserved by php. */
static bool isReserved(Str name)
{
	Str lower = to_lower(name);
	for (size_t i = 0; i < sizeof reservedKeywords / sizeof *reservedKeywords; i++)
		if (str_eqc(lower, reservedKeywords[i]))
			return true;
	return false;
}

/* Camelize "dino_party" -> "DinoParty": splitAtCaseChangeWithTitlecase joined. A word starts at
 * the beginning, after a spacer ("_ :-", dropped) and at an upper case letter; its first rune is
 * mapped to upper case, the others to lower case. */
static void camelize(Buf *b, Str word)
{
	bool start = true; /* len(word) == 0 */
	for (size_t i = 0; i < word.n;) {
		uint32_t c;
		i += (size_t)utf8_decode(word.p + i, word.n - i, &c);
		bool spacer = c == '_' || c == ' ' || c == ':' || c == '-';
		if (!start && (go_std_is_upper((int32_t)c) || spacer))
			start = true; /* words = append(words, string(word)); word = word[:0] */
		if (!spacer) {
			add_rune(b, (uint32_t)(start ? go_std_to_upper((int32_t)c) : go_std_to_lower((int32_t)c)));
			start = false;
		}
	}
}

/* create php identifier for class or message */
static void identifier(Buf *b, Str name, const char *suffix)
{
	camelize(b, name);
	if (*suffix)
		camelize(b, str_c(suffix));
}

static Str identifier_s(Str name, const char *suffix)
{
	Buf b = {0};
	identifier(&b, name, suffix);
	return buf_view(&b);
}

/* strings.Trim(s, sep) with a one-byte cutset */
static Str trim_byte(Str s, char sep)
{
	while (s.n && s.p[0] == sep)
		s.p++, s.n--;
	while (s.n && s.p[s.n - 1] == sep)
		s.n--;
	return s;
}

/* generate php namespace or path; pkg.p == NULL: a nil *string */
static Str namespace(Str pkg, char sep)
{
	if (!pkg.p)
		return STR_LIT("");
	Buf result = {0};
	for (size_t i = 0;; i++) { /* strings.SplitSeq(*pkg, ".") */
		const char *dot = memchr(pkg.p + i, '.', pkg.n - i);
		size_t end = dot ? (size_t)(dot - pkg.p) : pkg.n;
		identifier(&result, (Str){pkg.p + i, end - i}, "");
		buf_addc(&result, sep);
		if (!dot)
			break;
		i = end;
	}
	return trim_byte(buf_view(&result), sep);
}

static Str resolveReserved(Str ident, Str pkg)
{
	if (isReserved(to_lower(ident))) {
		Buf b = {0};
		buf_adds(&b, str_eqc(pkg, ".google.protobuf") ? "GPB" : "PB");
		buf_add_str(&b, ident);
		return buf_view(&b);
	}
	return ident;
}
