/* The Dart standard library (dart:core, dart:convert) as far as it shows in the output of
 * protocolbuffers/dart: number printing, String semantics (UTF-16 lengths and order, whitespace,
 * line splitting), base64 and JSON string encoding. Strings are UTF-8 here; Dart strings are
 * UTF-16, so lengths and orders that Dart computes on code units are computed on code units. */
#ifndef DART_STD_H
#define DART_STD_H

#include "util.h"

/* double.toString(): the shortest round-trip digits in JavaScript's layout ("0.1", "1e-7",
 * "1.5e+300", "NaN", "Infinity"), with ".0" for integral values below 1e21 ("2.0", "-0.0"). */
char *dart_double_to_string(double v);
/* base64Encode (dart:convert): the standard alphabet with padding. */
char *dart_base64_encode(const uint8_t *p, size_t n);
/* String.length: UTF-16 code units of the UTF-8 text. */
int dart_utf16_len(const char *s, size_t n);
/* String.compareTo: UTF-16 code unit order. */
int dart_compare(const char *a, const char *b);
/* String.trim / trimRight (Dart's whitespace: Unicode White_Space and U+FEFF). */
Str dart_trim(Str s);
Str dart_trim_right(Str s);
/* LineSplitter.split: lines ended by \n, \r\n or \r; a final line terminator does not start a new
 * line. Returns the number of lines; *out points into s. */
int dart_split_lines(Str s, Str **out);
/* jsonEncode of a string (with the quotes): escapes ", \ and control characters
 * (\b \t \n \f \r, others as \u00xx); everything else is written as is. */
char *dart_json_string(Str s);
/* Utf8Decoder(allowMalformed: true).convert: the UTF-8 of the decoded string, without a leading
 * byte order mark, ill-formed sequences replaced with U+FFFD. */
Str dart_utf8_decode_allow_malformed(Str s);
/* toLowerCase of a string of one UTF-16 code unit c (simple case mapping of the Dart VM). */
uint32_t dart_to_lower_code_unit(uint32_t c);

#endif
