/* Python language support: the parts of CPython 3.13 semantics (Unicode 15.1.0) that generators
 * written in Python rely on for their output: str whitespace and stripping, sorted() order of str,
 * repr() of str, bytes, int, float and bool, a check whether a source string that is expected
 * to be string literals parses (a subset of ast.parse), and the command line helpers shlex,
 * optparse and printf-style str formatting.
 *
 * Strings are UTF-8 Str values (Python str is a sequence of code points; UTF-8 byte order is code
 * point order). Invalid UTF-8 bytes are treated as single non-space, non-printable characters
 * (protoc and buf never pass invalid UTF-8 in descriptors). */
#ifndef PY_PYTHON_H
#define PY_PYTHON_H

#include "util.h"

/* ---- unicode.c ---- */

/* str.isspace() of one code point (Py_UNICODE_ISSPACE: bidirectional class WS, B or S, or
 * category Zs). */
bool py_isspace(uint32_t c);
/* str.isprintable() of one code point: false for categories Cc, Cf, Cs, Co, Cn, Zl, Zp and Zs
 * except U+0020. */
bool py_isprintable(uint32_t c);
/* str.strip(), str.lstrip(), str.rstrip() without arguments (no copy). */
Str py_strip(Str s);
Str py_lstrip(Str s);
Str py_rstrip(Str s);

/* ---- codecs.c ---- */

/* str() of UnicodeDecodeError(encoding, object, start, end, reason), prefixed with
 * "UnicodeDecodeError: ". */
char *py_unicode_decode_error(const char *encoding, Str object, size_t start, size_t end, const char *reason);
/* The same for UnicodeEncodeError of a str of n code points. */
char *py_unicode_encode_error(const char *encoding, const uint32_t *object, size_t n, size_t start, size_t end,
	const char *reason);
/* The error of str(s, "utf-8") (strict), NULL if s is valid UTF-8. */
char *py_utf8_decode_error(Str s);

/* ---- sort.c ---- */

/* Comparison of two str values (code point order, a prefix sorts first): <0, 0, >0. */
int py_str_cmp(Str a, Str b);
/* sorted() of a list of NUL-terminated strings, in place. */
void py_sort_cstrs(const char **v, int n);
/* sorted() of a list of Str, in place. */
void py_sort_strs(Str *v, int n);

/* ---- repr.c ---- */

/* repr(str): quotes with ' unless the text contains ' and no ". */
char *py_repr_str(Str s);
/* repr(bytes): b'...' with the same quote choice. */
char *py_repr_bytes(Str s);
/* repr(int) */
char *py_repr_int(int64_t v);
char *py_repr_uint(uint64_t v);
/* repr(bool) */
const char *py_repr_bool(bool v);
/* repr(float): the shortest digits that round-trip, as float_repr_style "short" prints them
 * ("1e+16", "1e-05", "0.1", "1.0", "inf", "nan", "-0.0"). */
char *py_repr_float(double v);

/* ---- literal.c ---- */

/* Whether ast.parse(src) succeeds, for a source that is expected to be string literals (e.g.
 * '"""' + text + '"""'). Exact for sources made of string literal statements (u, r, b prefixes,
 * f-strings without replacement fields, implicit concatenation, escapes, line continuations,
 * comments, blank lines, semicolons). Other Python code (names, operators, numbers, brackets,
 * f-string replacement fields) and \N{name} escapes (no Unicode name database) give
 * PY_PARSE_UNKNOWN, unless the tokens prove a syntax error: an invalid character, an unterminated
 * literal, unbalanced brackets, an invalid escape, a name next to a string where the grammar has none,
 * an empty statement, mixed bytes and str literals, an unexpected indent. */
typedef enum { PY_PARSE_OK, PY_PARSE_ERROR, PY_PARSE_UNKNOWN } PyParse;
PyParse py_parse_string_literals(Str src);

/* ---- lines.c ---- */

/* str.splitlines(): the lines without their boundaries (*out points into s). */
int py_splitlines(Str s, Str **out);

/* ---- textwrap.c ---- */

/* textwrap.wrap(text, width) of CPython 3.11 with the default options (text is valid UTF-8,
 * width >= 1): the number of lines, *out the lines. */
int py_textwrap_wrap(Str text, int width, Str **out);

/* ---- stdlib_names.c ---- */

/* name in sys.stdlib_module_names (CPython 3.13). */
bool py_is_stdlib_module(Str name);

/* ---- hashlib.c ---- */

/* hashlib.sha256(data).hexdigest() */
COLD char *py_sha256_hex(Str data);

/* ---- number.c ---- */

typedef enum { PY_NUM_OK, PY_NUM_ERROR, PY_NUM_UNSUPPORTED } PyNum;
/* int(s): PY_NUM_OK with *repr = repr(int(s)), PY_NUM_ERROR for a ValueError, PY_NUM_UNSUPPORTED
 * for non-ASCII digits. */
PyNum py_parse_int(Str s, char **repr);
/* float(s), like py_parse_int. */
PyNum py_parse_float(Str s, double *out);

/* ---- shlex.c ---- */

/* list(shlex.shlex(s, posix=posix)) with whitespace_split = True and commenters = "", and the
 * given whitespace characters (" \t\r\n" is the default; shlex.split(s) is posix with it).
 * Returns the number of tokens, or 0 with *err set to the ValueError text ("No closing
 * quotation", "No escaped character"). */
int py_shlex(Str s, bool posix, const char *whitespace, Str **out, const char **err);

/* ---- format.c ---- */

typedef enum { PY_FORMAT_OK, PY_FORMAT_TYPE_ERROR, PY_FORMAT_VALUE_ERROR } PyFormat;
/* fmt % arg for a str arg: appends the result to out, or returns the exception with its text in
 * *msg. */
PyFormat py_percent_format(Str fmt, Str arg, Buf *out, char **msg);

/* ---- optparse.c ---- */

/* An option of an optparse.OptionParser: its names separated by spaces ("-e --extension") and
 * whether it takes one value (actions store and append; store_true, store_false and help take
 * none). */
typedef struct {
	const char *names;
	bool takes_value;
} PyOption;
/* One processed option: the index in the table and its value (p == NULL if none). */
typedef struct {
	int option;
	Str value;
} PyOptEvent;
/* OptionParser.parse_args(args): the processed options in order and the positional arguments.
 * On a usage error (what OptionParser.error prints after "<prog>: error: "), *err is set and the
 * options processed before it are returned. */
int py_optparse(const PyOption *opts, int nopts, const Str *args, int nargs, PyOptEvent **events,
	Str **positional, int *npositional, char **err);

#endif
