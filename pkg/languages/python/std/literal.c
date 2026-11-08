/* Whether ast.parse of CPython 3.13 accepts a source that is expected to be string literals: exact
 * for string literal expression statements (Parser/lexer/lexer.c string scanning, indentation and
 * brackets, Parser/string_parser.c escape decoding, Parser/action_helpers.c concatenation of str
 * and bytes literals), plus the syntax errors that the tokens alone prove. Any other Python code is
 * PY_PARSE_UNKNOWN. See python.h. */
#include "python.h"

#include <string.h>

static bool is_hex(char c) { return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }

static int hex_val(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

/* Prefix flags of a string literal. */
enum { P_RAW = 1, P_BYTES = 2, P_FORMAT = 4, P_UNICODE = 8 };

/* Parses a string prefix (the letters before the quote) of length n; returns its flags or -1 if
 * it is not one (Lib/tokenize.py _all_string_prefixes: u, r, b, br, f, fr in any case and order;
 * u combines with nothing). */
static int parse_prefix(const char *p, size_t n)
{
	int flags = 0;
	if (n > 2)
		return -1;
	for (size_t i = 0; i < n; i++) {
		int f;
		switch (p[i] | 0x20) {
		case 'r': f = P_RAW; break;
		case 'b': f = P_BYTES; break;
		case 'f': f = P_FORMAT; break;
		case 'u': f = P_UNICODE; break;
		default: return -1;
		}
		if (flags & f)
			return -1;
		flags |= f;
	}
	if ((flags & P_UNICODE) && n > 1)
		return -1;
	if ((flags & P_BYTES) && (flags & P_FORMAT))
		return -1;
	return flags;
}


/* Validates the escapes of a non-raw literal body (decode_unicode_with_escapes and
 * _PyBytes_DecodeEscape2): only errors matter, invalid escapes are warnings. A well-formed
 * \N{name} is PY_PARSE_UNKNOWN (no Unicode name database). */
static PyParse check_escapes(const char *s, size_t n, bool bytes)
{
	PyParse r = PY_PARSE_OK;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (bytes && c >= 0x80)
			return PY_PARSE_ERROR; /* bytes can only contain ASCII literal characters */
		if (c != '\\')
			continue;
		if (++i >= n)
			return PY_PARSE_ERROR; /* the tokenizer never ends a literal body with a lone backslash */
		size_t digits = 0;
		switch (s[i]) {
		case 'x': digits = 2; break;
		case 'u': digits = bytes ? 0 : 4; break;
		case 'U': digits = bytes ? 0 : 8; break;
		case 'N':
			if (bytes)
				break;
			{
				if (i + 1 >= n || s[i + 1] != '{')
					return PY_PARSE_ERROR; /* malformed \N character escape */
				size_t j = i + 2, start = j;
				while (j < n && s[j] != '}')
					j++;
				if (j >= n || j == start)
					return PY_PARSE_ERROR;
				/* Unicode names and aliases consist of letters, digits, spaces and hyphens
				 * (matched case-insensitively) */
				for (size_t k = start; k < j; k++) {
					char ch = s[k];
					if (!((ch >= '0' && ch <= '9') || ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'z') || ch == ' ' ||
					      ch == '-'))
						return PY_PARSE_ERROR; /* unknown Unicode character name */
				}
				r = PY_PARSE_UNKNOWN;
				i = j;
			}
			break;
		}
		if (!digits)
			continue;
		uint32_t v = 0;
		for (size_t k = 0; k < digits; k++) {
			if (i + 1 + k >= n || !is_hex(s[i + 1 + k]))
				return PY_PARSE_ERROR; /* truncated \xXX / \uXXXX / \UXXXXXXXX escape */
			v = v << 4 | (uint32_t)hex_val(s[i + 1 + k]);
		}
		if (digits == 8 && v > 0x10ffff)
			return PY_PARSE_ERROR; /* illegal Unicode character */
		i += digits;
	}
	return r;
}

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\f'; }
static bool is_ident(unsigned char c)
{
	return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c == '_' || c >= 0x80;
}

static bool in_list(const char *const *list, const char *p, size_t n)
{
	for (; *list; list++)
		if (strlen(*list) == n && !memcmp(*list, p, n))
			return true;
	return false;
}

/* The names that may follow an expression in the grammar of Python 3.13 (keywords of
 * expressions, comprehensions, with/except/case "as", raise "from"); after a string any other name
 * is a syntax error. */
static const char *const AFTER_EXPR[] = {"and", "as", "async", "else", "for", "from", "if", "in", "is", "not", "or", NULL};
/* The names that a string can never follow: atoms and keywords of statements without an
 * expression, and soft keywords that are names here. Other keywords and the soft keywords match
 * and case may be followed by an expression; any other name is an identifier, which a string
 * cannot follow. */
static const char *const NOT_BEFORE_EXPR[] = {"False", "None", "True", "async", "break", "class", "continue", "def",
					     "finally", "global", "import", "lambda", "nonlocal", "pass", "try", NULL};
static const char *const KEYWORDS[] = { /* Lib/keyword.py kwlist and softkwlist match, case */
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
	"elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
	"not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "match", "case", NULL,
};

/* The previous token of the logical line, for the errors that adjacent tokens prove: T_NAME is a
 * name that a string cannot follow, T_NAME_KW one that it may. */
enum { T_START, T_SEMI, T_STR, T_NAME, T_NAME_KW, T_OTHER };

/* Scans the string literal with prefix flags whose opening quote is at q: sets *next past it and
 * *bytes. PY_PARSE_UNKNOWN for an f-string with a replacement field (its end depends on the
 * expression) or a \N{name} escape. */
static PyParse scan_literal(const char *q, const char *end, int flags, const char **next)
{
	char quote = *q;
	size_t qsize = q + 2 < end && q[1] == quote && q[2] == quote ? 3 : 1;
	const char *body = q + qsize, *s = body;
	size_t endq = 0;
	while (endq != qsize) {
		if (s >= end)
			return PY_PARSE_ERROR; /* unterminated string literal */
		char ch = *s++;
		if (qsize == 1 && ch == '\n')
			return PY_PARSE_ERROR;
		if (ch == quote) {
			endq++;
		} else {
			endq = 0;
			if (ch == '\\') {
				if (s >= end)
					return PY_PARSE_ERROR;
				s++;
			}
		}
	}
	*next = s;
	size_t n = (size_t)(s - qsize - body);
	bool bytes = flags & P_BYTES;
	if (flags & P_FORMAT) {
		/* without replacement fields an f-string is tokenized like a string */
		for (size_t i = 0; i < n; i++) {
			if (body[i] == '{' && i + 1 < n && body[i + 1] == '{') {
				i++;
			} else if (body[i] == '{') {
				return PY_PARSE_UNKNOWN;
			} else if (body[i] == '}') {
				if (i + 1 >= n || body[i + 1] != '}')
					return PY_PARSE_ERROR; /* f-string: single '}' is not allowed */
				i++;
			} else if (body[i] == '\\' && !(flags & P_RAW) && i + 1 < n) {
				/* tok_get_fstring_mode: a backslash does not escape a brace (\{ and \} are a
				 * literal backslash, with a SyntaxWarning, before the brace) */
				if (body[i + 1] == '{' || body[i + 1] == '}')
					continue;
				if (body[i + 1] == 'N' && i + 2 < n && body[i + 2] == '{')
					return PY_PARSE_UNKNOWN; /* \N{name} */
				i++;
			}
		}
	}
	if (!(flags & P_RAW))
		return check_escapes(body, n, bytes);
	if (bytes)
		for (size_t i = 0; i < n; i++)
			if ((unsigned char)body[i] >= 0x80)
				return PY_PARSE_ERROR; /* bytes can only contain ASCII literal characters */
	return PY_PARSE_OK;
}

/* Tokenizer MAXLEVEL */
#define MAX_LEVEL 200

PyParse py_parse_string_literals(Str src)
{
	if (memchr(src.p, 0, src.n))
		return PY_PARSE_ERROR; /* source code string cannot contain null bytes */
	/* universal newlines, like the string tokenizer (translate_newlines) */
	Buf b = {0};
	for (size_t i = 0; i < src.n; i++) {
		if (src.p[i] == '\r') {
			buf_addc(&b, '\n');
			if (i + 1 < src.n && src.p[i + 1] == '\n')
				i++;
		} else {
			buf_addc(&b, src.p[i]);
		}
	}
	const char *p = b.p ? b.p : "", *end = p + b.n;
	bool bol = true;      /* at the start of a logical line */
	bool other = false;   /* a token other than string literals and ';' was seen */
	bool unknown = false; /* a literal whose validity is not known (\N{name}) was seen */
	int prev = T_START, prev_kind = 0; /* prev_kind of a T_STR: 1 str, 2 bytes */
	char brackets[MAX_LEVEL];
	int level = 0;
	while (p < end) {
		if (bol && !level) {
			int col = 0;
			const char *q = p;
			for (; q < end && is_ws(*q); q++)
				col = *q == '\f' ? 0 : col + 1;
			if (q == end)
				break;
			if (*q == '\n' || *q == '#') {
				while (q < end && *q != '\n')
					q++;
				p = q < end ? q + 1 : q;
				continue;
			}
			/* after simple statements of string literals an indented line is an error; after
			 * other code it may open a block */
			if (col && !other)
				return PY_PARSE_ERROR; /* unexpected indent */
			p = q;
			bol = false;
			prev = T_START;
		}
		unsigned char c = (unsigned char)*p;
		if (is_ws((char)c)) {
			p++;
		} else if (c == '#') {
			while (p < end && *p != '\n')
				p++;
		} else if (c == '\n') {
			p++;
			bol = !level; /* newlines in brackets join lines */
		} else if (c == '\\') {
			if (p + 1 >= end || p[1] != '\n')
				return PY_PARSE_ERROR; /* unexpected character after line continuation character, EOF */
			p += 2;
		} else if (c == ';' && !level) {
			if (prev == T_START || prev == T_SEMI)
				return PY_PARSE_ERROR; /* an empty statement */
			prev = T_SEMI;
			p++;
		} else if (c >= '0' && c <= '9') {
			/* a number (not checked) */
			while (p < end && (is_ident((unsigned char)*p) || *p == '.'))
				p++;
			other = true;
			prev = T_OTHER;
		} else if (c == '"' || c == '\'' || is_ident(c)) {
			/* a string literal with an optional prefix, or a name */
			const char *q = p;
			bool ascii = true;
			while (q < end && is_ident((unsigned char)*q))
				ascii &= (unsigned char)*q++ < 0x80;
			int flags = q < end && (*q == '"' || *q == '\'') ? parse_prefix(p, (size_t)(q - p)) : -1;
			if (flags < 0) {
				other = true;
				if (!ascii) {
					prev = T_OTHER; /* a Unicode identifier or an invalid character */
				} else {
					size_t n = (size_t)(q - p);
					if (prev == T_STR && !in_list(AFTER_EXPR, p, n))
						return PY_PARSE_ERROR; /* invalid syntax */
					prev = in_list(KEYWORDS, p, n) && !in_list(NOT_BEFORE_EXPR, p, n) ? T_NAME_KW : T_NAME;
				}
				p = q;
				continue;
			}
			if (prev == T_NAME)
				return PY_PARSE_ERROR; /* a name followed by a string */
			PyParse r = scan_literal(q, end, flags, &p);
			if (r == PY_PARSE_ERROR)
				return r;
			if (r == PY_PARSE_UNKNOWN) {
				if (flags & P_FORMAT)
					return r; /* the rest cannot be tokenized */
				unknown = true;
			}
			int k = flags & P_BYTES ? 2 : 1;
			if (prev == T_STR && prev_kind != k)
				return PY_PARSE_ERROR; /* cannot mix bytes and nonbytes literals */
			prev = T_STR;
			prev_kind = k;
		} else {
			other = true;
			prev = T_OTHER;
			switch (c) {
			case '(': case '[': case '{':
				if (level >= MAX_LEVEL)
					return PY_PARSE_ERROR; /* too many nested parentheses */
				brackets[level++] = (char)c;
				break;
			case ')': case ']': case '}':
				if (!level)
					return PY_PARSE_ERROR; /* unmatched ')' */
				if (brackets[--level] != (c == ')' ? '(' : c == ']' ? '[' : '{'))
					return PY_PARSE_ERROR; /* closing parenthesis does not match opening parenthesis */
				break;
			case '!':
				if (p + 1 >= end || p[1] != '=')
					return PY_PARSE_ERROR; /* '!' outside an f-string */
				p++;
				break;
			case '+': case '-': case '*': case '/': case '%': case '@': case '&': case '|': case '^':
			case '~': case '<': case '>': case '=': case '.': case ',': case ':': case ';':
				break;
			default:
				return PY_PARSE_ERROR; /* '$', '?', '`', non-printable characters */
			}
			p++;
		}
	}
	if (level)
		return PY_PARSE_ERROR; /* '(' was never closed */
	return other || unknown ? PY_PARSE_UNKNOWN : PY_PARSE_OK;
}
