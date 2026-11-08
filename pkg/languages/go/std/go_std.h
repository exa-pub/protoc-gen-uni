/* Go 1.27.1 standard library port: the formatting that compiler/protogen applies to generated
 * Go code (go/parser + go/printer), and the parts of strconv and unicode that generators need.
 * The output depends on the toolchain that built the upstream plugin: this port implements the
 * newest Go release in use (go1.27.1), and an older release (protocolbuffers/go:v1.36.12 is built
 * with go1.26.5) is selected with go_std_use(). Everything else in this directory is static
 * (unity build go_std.c). */
#ifndef GO_STD_H
#define GO_STD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* A Go release older than the newest one, defined by the library of its versioned parts:
 * go_std_go1_26_5 (directory go1.26.5/). */
typedef struct go_std_release go_std_release;
extern const go_std_release go_std_go1_26_5;
/* Selects the release whose behaviour the port reproduces; NULL: the newest (the default). The
 * setting is process-wide: a generator built with an older Go selects its release for its
 * request and resets it with go_std_use(NULL) before it returns. */
void go_std_use(const go_std_release *r);

/* An import added by go_std_format_content: `name "path"` (NUL-terminated strings). */
typedef struct {
	const char *name;
	const char *path;
} go_std_import;

/* Formats generated Go source like protogen's (*GeneratedFile).Content: parses src (with
 * comments), inserts one import block with the given imports (in the given order; protogen sorts
 * them by path) after the package clause or a comment ending on its line, and prints the file with
 * printer.Config{Mode: TabIndent | UseSpaces, Tabwidth: 8}. The output is appended to *out.
 * Returns NULL on success or the parse error (text of go/scanner.ErrorList.Error()). */
const char *go_std_format_content(const char *src, size_t n, const go_std_import *imports, size_t nimports,
				    Buf *out);

/* go/format.Source of a complete Go source file: parses src (with comments), sorts the imports
 * (ast.SortImports) and prints it like gofmt (printer.Config{Mode: UseSpaces | TabIndent |
 * normalizeNumbers, Tabwidth: 8}), appended to *out. Returns NULL on success or the parse error
 * (text of go/scanner.ErrorList.Error()). A source without a package clause (a declaration or
 * statement list, which Source also accepts) gives the parse error. */
const char *go_std_format_source(const char *src, size_t n, Buf *out);

/* An identifier of a top-level declaration: symbol as protogen annotates it ("Name" or
 * "Type.Name") and its byte offsets in the source. */
typedef struct {
	const char *symbol;
	int begin, end;
} go_std_ident;

/* Parses src (formatted output) and lists the identifiers that protogen's generatedCodeInfo visits,
 * in its order: type names, their struct fields and interface methods, const/var names, functions
 * and methods (receiver type "T" or "*T"). Returns NULL on success or the parse error. */
const char *go_std_decl_idents(const char *src, size_t n, go_std_ident **out, size_t *nout);

/* strconv.Quote of s[0:n]; the result is NUL-terminated, its length is stored in *out_n if set. */
char *go_std_quote(const char *s, size_t n, size_t *out_n);

/* strconv.QuoteRune(r) (fmt's %q of a rune), NUL-terminated. */
char *go_std_quote_rune(int32_t r);

/* strconv.FormatFloat(f, fmt, prec, bitSize) for fmt 'e', 'E', 'f', 'g' and 'G' (bitSize 32 or
 * 64); a NUL-terminated arena string. */
char *go_std_format_float(double f, char fmt, int prec, int bit_size);

/* strconv.ParseUint, ParseInt and ParseBool of s[0:n]: 0, or the error (strconv.ErrSyntax,
 * ErrRange, or an invalid base or bit size); a range error stores the clamped value, as Go
 * returns it. */
enum { GO_STD_ERR_SYNTAX = 1, GO_STD_ERR_RANGE = 2, GO_STD_ERR_BASE = 3 };
int go_std_parse_uint(const char *s, size_t n, int base, int bit_size, uint64_t *out);
int go_std_parse_int(const char *s, size_t n, int base, int bit_size, int64_t *out);
int go_std_parse_bool(const char *s, size_t n, bool *out);
/* strconv.ParseFloat of s[0:n] (bit_size 32 or 64): 0, GO_STD_ERR_SYNTAX or GO_STD_ERR_RANGE
 * (*out is then ±Inf, as Go returns it). */
int go_std_parse_float(const char *s, size_t n, int bit_size, double *out);
/* time.Parse(layout, value) succeeds, for the layouts time.RFC3339 and "2006-01-02" only. */
bool go_std_time_parse(const char *layout, Str value);

/* sort.Slice of n pointers with the given less (pattern-defeating quicksort: not stable). */
void go_std_sort_slice(void **v, size_t n, bool (*less)(const void *a, const void *b, void *ctx), void *ctx);

/* net/url.URL (the fields Parse sets; HasUser: User != nil). */
typedef struct {
	Str Scheme, Opaque;
	bool HasUser, PasswordSet;
	Str Username, Password;
	Str Host, Path, RawPath;
	bool OmitHost, ForceQuery;
	Str RawQuery, Fragment, RawFragment;
} go_std_url;
/* url.Parse: NULL or the error text (*url.Error). */
const char *go_std_url_parse(Str raw, go_std_url *u);
/* (*url.URL).String */
Str go_std_url_string(const go_std_url *u);

/* unicode.ToUpper, unicode.ToLower (the case mappings of the newest release) */
int32_t go_std_to_upper(int32_t r);
int32_t go_std_to_lower(int32_t r);
/* unicode.IsLetter, unicode.IsDigit, unicode.IsUpper */
bool go_std_is_letter(int32_t r);
bool go_std_is_digit(int32_t r);
bool go_std_is_upper(int32_t r);
/* token.Lookup(s).IsKeyword() */
bool go_std_is_keyword(const char *s, size_t n);
/* token.IsIdentifier(s) */
bool go_std_is_identifier(const char *s, size_t n);

/* A top-level type, const or var name of a Go file: tok is "type", "const" or "var", selector
 * whether its type (type specs) or value (value specs; false without a value) is a selector
 * expression (pkg.Name). */
typedef struct {
	const char *tok;
	const char *name;
	bool selector;
} go_std_forward;

/* Parses src (with comments) and lists the type and value specs of its declarations in order,
 * as protoc-gen-go's genImport walks them for public imports. Returns NULL or the parse error. */
const char *go_std_forward_decls(const char *src, size_t n, go_std_forward **out, size_t *nout);

/* encoding/json of Go 1.27 (the v1 API on the jsonv2 engine, v1 default options): pieces to marshal
 * values like json.Marshal and json.Encoder do (encoding/json/json.c). */
/* jsonwire.AppendQuote: escapes '"', '\\', control characters, <, >, & and U+2028, U+2029;
 * invalid UTF-8 becomes U+FFFD. */
void go_std_json_quote(Buf *b, Str s);
/* jsonwire.AppendFloat of a finite value (bits 32 or 64). */
void go_std_json_float(Buf *b, double f, int bits);
/* Encoder.WriteValue of the result of a MarshalJSON method: appends it without whitespace, its
 * strings kept as they are except for <, >, &, U+2028 and U+2029, which are escaped. Returns
 * false (b unchanged) if raw is not exactly one valid JSON value. */
bool go_std_json_write_raw(Buf *b, Str raw);
/* json.Indent(dst, src, prefix, indent) of valid compact JSON. */
void go_std_json_indent(Buf *dst, Str src, const char *prefix, const char *indent);
/* json.Unmarshal into an `any`: the value tree. Objects (map[string]any) keep the last member of
 * a name and are sorted by name; numbers are float64 (raw: the text). */
enum { GO_JSON_NULL, GO_JSON_BOOL, GO_JSON_NUMBER, GO_JSON_STRING, GO_JSON_ARRAY, GO_JSON_OBJECT };
typedef struct go_std_json_value go_std_json_value;
typedef struct {
	Str key; /* objects */
	go_std_json_value *value;
	int index;
} go_std_json_member;
struct go_std_json_value {
	int kind;
	bool b;
	double f;
	Str s, raw;
	go_std_json_member *items;
	int n;
};
/* NULL and *err on failure: "invalid JSON" (the syntax error of Go is not reproduced), the
 * UnmarshalTypeError of a number out of range, or an unsupported nesting depth. */
go_std_json_value *go_std_json_unmarshal_any(Str raw, const char **err);

#endif
