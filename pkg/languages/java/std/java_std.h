/* The Java standard library, as far as it shows in generated code: Double.toString and
 * Float.toString (JDK 19+), built on the shortest digits of pkg/num; String decoding and
 * comparison, stable sorting and exceptions of the JVM that runs the plugins (string.c). */
#ifndef JAVA_STD_H
#define JAVA_STD_H

#include "num.h"

/* Java Double.toString(v) and Float.toString(v) (JDK 19+, Raffaello Giulietti's algorithm):
 * "NaN", "Infinity", "-0.0", "100.0", "0.001", "1.0E7", "4.9E-324". Arena strings. */
char *java_double_to_string(double v);
char *java_float_to_string(float v);

/* The bytes of a protobuf string field (NUL-terminated, see pb.h) as protobuf-java decodes them
 * into a String (new String(bytes, UTF_8) of JDK 25: malformed sequences become U+FFFD), encoded
 * to UTF-8 again; the result is NUL-terminated too. */
Str java_string(Str s);
/* java_string as a C string. Java strings may contain U+0000, which ports do not support in names,
 * options and comments: it fails instead of producing a different output. */
const char *java_cstr(Str s);
/* Fails unless s has no characters of the Basic Multilingual Plane beyond ASCII:
 * Character.isLowerCase/isUpperCase/isDigit/toUpperCase/toLowerCase of the JDK's Unicode tables
 * are not ported. Supplementary characters are two surrogates in Java, neither letters nor digits;
 * callers treat their four bytes the same way. */
void java_require_ascii(const char *s);
/* String.compareTo (UTF-16 code units) of two valid UTF-8 strings. */
int java_strcmp(const char *a, const char *b);
/* List.sortBy, Collections.sort: a stable merge sort, O(n log n). */
void java_sort(void **v, int n, int (*cmp)(const void *, const void *));
/* Throws a Java exception: fail() with Throwable.toString ("<class>: <message>", or the class
 * alone when msg is NULL), the first line of a stack trace. */
NORETURN COLD void java_exception(const char *cls, const char *msg);

#endif
