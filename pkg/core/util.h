/* Core utilities shared by all generators: arena allocation, growable buffers, pointer vectors,
 * a string map, string helpers, quoting, base64 and error handling.
 *
 * Memory model: the plugin handles one request per process, so everything is allocated from one
 * arena and never freed individually (arena_reset() frees everything at once, for the fuzzer).
 * Out-of-memory aborts with a message. All buffer writes go through the bounds-checked helpers
 * below.
 *
 * Strings are NUL-terminated `char *` where the content cannot contain NUL (names, generated
 * code) and Str (pointer + length) where it can (data from the request: comments, default
 * values, JSON names). */
#ifndef UTIL_H
#define UTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NORETURN __attribute__((noreturn))
#define COLD __attribute__((cold))

/* ---- arena.c ---- */

void *xalloc(size_t n); /* zeroed, never NULL */
void *xrealloc(void *p, size_t old, size_t n);
char *xstrndup(const char *s, size_t n);
char *xstrdup(const char *s);
/* Frees everything allocated so far (all pointers become invalid). */
void arena_reset(void);
/* Scoped allocation, for generators that produce independent outputs one after the other: the
 * memory allocated after arena_mark() is freed by arena_release() of that mark (pointers into it
 * become invalid), except what xalloc_keep() allocated, which lives until arena_reset(). */
typedef struct {
	void *chunks;
	char *p, *end;
} ArenaMark;
ArenaMark arena_mark(void);
void arena_release(ArenaMark m);
void *xalloc_keep(size_t n); /* zeroed, never NULL; survives arena_release() */

/* ---- buf.c ---- */

char *xfmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Concatenates NUL-terminated strings up to a NULL sentinel. */
char *xcat_(const char *first, ...);
#define xcat(...) xcat_(__VA_ARGS__, (const char *)0)

/* Growable byte buffer; always NUL-terminated (p may be NULL while empty). */
typedef struct {
	char *p;
	size_t n, cap;
} Buf;
void buf_grow(Buf *b, size_t extra);
void buf_add(Buf *b, const void *p, size_t n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, char c);
/* Minimal printf: %s, %c, %%, and %d / %lld / %x / %llx with an optional (zero-padded) width.
 * No stdio: musl's vfprintf alone is about 10 KB. */
void buf_fmt(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_vfmt(Buf *b, const char *fmt, va_list ap);
const char *buf_str(Buf *b); /* "" if empty */

/* ---- container.c ---- */

/* Vector of pointers. */
typedef struct {
	void **v;
	int n, cap;
} Vec;
void vec_push(Vec *v, void *p);
#define VEC_AT(vec, T, i) ((T *)(vec).v[i])
int vec_index(const Vec *v, const void *p);

/* String -> pointer hash map (open addressing, keys are not copied). */
typedef struct {
	const char **keys;
	void **vals;
	size_t cap, n;
} Map;
void *map_get(const Map *m, const char *key);
bool map_has(const Map *m, const char *key);
void map_put(Map *m, const char *key, void *val);

/* ---- str.c ---- */

bool str_eq(const char *a, const char *b);
bool has_prefix(const char *s, const char *prefix);
bool has_suffix(const char *s, const char *suffix);
char *str_trim_suffix(const char *s, const char *suffix);
/* Replaces every occurrence of `from` (non-empty) with `to`. */
char *str_replace_all(const char *s, const char *from, const char *to);
/* Splits s at every occurrence of sep (like strings.Split; "" yields one empty element). */
Vec str_split(const char *s, char sep);
char *str_join(const Vec *parts, const char *sep);

/* Byte string with explicit length; may contain NUL. */
typedef struct {
	const char *p;
	size_t n;
} Str;
#define STR_LIT(s) ((Str){(s), sizeof(s) - 1})
Str str_c(const char *s); /* view of a NUL-terminated string */
bool str_equal(Str a, Str b);
bool str_eqc(Str a, const char *b);
bool str_has_prefix(Str s, Str prefix);
bool str_has_suffix(Str s, Str suffix);
Str str_trim_suffix_s(Str s, Str suffix); /* strings.TrimSuffix, no copy */
/* Splits at every sep (strings.Split); the elements point into s. */
int str_split_s(Str s, char sep, Str **out);
void buf_add_str(Buf *b, Str s);
Str buf_view(Buf *b); /* the content as Str (b keeps owning it) */

/* Decodes one UTF-8 sequence like Go's utf8.DecodeRune: invalid or truncated input yields
 * U+FFFD with width 1, n must be > 0. Returns the width. */
int utf8_decode(const char *p, size_t n, uint32_t *rune);
/* Go's unicode.IsSpace. */
bool go_is_space(uint32_t r);
/* Go's strings.TrimSpace (unicode.IsSpace). */
COLD Str go_trim_space(Str s);

/* ---- quote.c ---- */

/* Go's strconv.Quote (the %q verb for strings), NUL-terminated result. */
COLD char *quote_go(Str s);
/* JSON.stringify of a string whose code units are the bytes of s (valid UTF-8 passes through):
 * escapes '"', '\\', \b \f \n \r \t and other control characters as \u00xx. */
COLD char *quote_json(Str s);

/* ---- base64.c ---- */

char *base64_raw_std(const uint8_t *p, size_t n); /* no padding */

/* ---- fail.c ---- */

/* Error handling: generator code reports errors with fail(); the top-level runner catches them
 * with a setjmp in try_run(). */
NORETURN COLD void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Like fail, with a message that may contain NUL (msg.p must be NUL-terminated after n). */
NORETURN COLD void fail_str(Str msg);
/* Runs fn(arg); returns NULL on success or the error message. */
const char *try_run(void (*fn)(void *), void *arg);
/* The message of the last failure, with its exact length. */
Str fail_message(void);

#endif
