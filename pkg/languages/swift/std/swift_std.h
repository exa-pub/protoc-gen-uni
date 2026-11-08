/* Swift standard library behavior that shows in generated output (Swift 6.x): String and
 * Character operations that work on Characters (extended grapheme clusters) rather than bytes.
 *
 * Strings are valid UTF-8 (SwiftProtobuf rejects requests with invalid UTF-8). Grapheme cluster
 * breaking needs the Unicode tables of the Swift runtime, which are not ported: the functions
 * here are exact for ASCII and for a conservative set of characters that never join their
 * neighbors in a cluster other than by combining marks (Latin, Greek, Cyrillic, CJK, kana,
 * Hangul syllables, common symbols and emoji, a few blocks of combining marks). On anything else
 * they fail with "unsupported by protoc-gen-uni: ..." naming `what`, never giving a different
 * result. */
#ifndef SWIFT_STD_H
#define SWIFT_STD_H

#include "util.h"

/* The end (byte offset) of the Character that starts at byte i of s (i < s.n). */
size_t swift_character_end(Str s, size_t i, const char *what);

/* s.dropFirst(n) of a String: the byte offset after the first n Characters (s.n if s has fewer). */
size_t swift_drop_first(Str s, int n, const char *what);

/* String.split(separator: c, maxSplits: max_splits) with omittingEmptySubsequences: true, for an
 * ASCII separator Character c (max_splits < 0: no limit). Returns the number of parts; the parts
 * point into s. */
int swift_split(Str s, char c, int max_splits, Str **out, const char *what);

/* String.split(omittingEmptySubsequences: false, whereSeparator: \.isNewline): the separators are
 * the Characters "\r\n", "\n", "\u{B}", "\u{C}", "\r", "\u{85}", "\u{2028}" and "\u{2029}" (newline
 * scalars never join other scalars in a cluster). Returns the number of lines (at least 1); they
 * point into s. */
int swift_lines(Str s, Str **out);

#endif
