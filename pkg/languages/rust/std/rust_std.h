/* The parts of the Rust standard library (Rust 1.91.1, the toolchain of the buf.build remote
 * plugins) whose exact behavior shows in generated code: Unicode white space, str::trim,
 * str::lines and the escape_default family. Results are arena strings.
 *
 * Strings are UTF-8 in Rust; invalid sequences decode as U+FFFD of one byte here (utf8_decode). */
#ifndef RUST_STD_H
#define RUST_STD_H

#include "util.h"

/* char::is_whitespace (the Unicode White_Space property; also `\s` of the regex crate with
 * unicode-bool). */
bool rust_is_whitespace(uint32_t c);
/* str::trim: removes leading and trailing white space. */
Str rust_trim(Str s);
/* str::lines: splits at "\n" and strips one "\r" before each "\n"; a final empty line is not a
 * line. Returns the number of lines; the elements point into s. */
int rust_lines(Str s, Str **out);
/* char::escape_default: \t \r \n \' \" \\, printable ASCII as is, others as \u{hex}. */
void rust_char_escape_default(Buf *b, uint32_t c);
/* str::escape_default: char::escape_default of every char. */
void rust_str_escape_default(Buf *b, Str s);
/* <str as Debug>::fmt: the string in double quotes, with \" \\ \t \r \n \0 and \u{hex} for
 * other ASCII control chars. Non-ASCII chars are copied (upstream escapes the non-printable ones
 * and grapheme extenders). */
void rust_str_debug(Buf *b, Str s);
/* char::escape_debug (EscapeDebugExtArgs::ESCAPE_ALL): \0 \t \r \n \\ \" \', printable chars as
 * is, others as \u{hex}. ASCII is exact; for non-ASCII the Unicode tables of core::unicode
 * (printable, grapheme_extend) are approximated by ranges (rust_std.c). */
void rust_char_escape_debug(Buf *b, uint32_t c);
/* <f64 as Display> (bits 64) and <f32 as Display> (bits 32) without precision: the shortest
 * round-trip digits, never an exponent ("1", "0.1", "100000000000000000000", "-0", "inf", "NaN"). */
void rust_float_display(Buf *b, double v, int bits);
/* The panic of &s[..index] when index is out of bounds or not a char boundary
 * (slice_error_fail_rt of library/core/src/str/mod.rs): "byte index 1 is not a char boundary; it is
 * inside 'é' (bytes 0..2) of `é`", with s cut to 256 bytes and "[...]" after it. */
NORETURN COLD void rust_str_slice_end_fail(Str s, size_t index);
/* core::ascii::escape_default: \t \r \n \' \" \\, printable ASCII as is, others as \xHH. */
void rust_ascii_escape_default(Buf *b, uint8_t c);

#endif
