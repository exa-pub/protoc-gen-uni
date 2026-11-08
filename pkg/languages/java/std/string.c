/* java.lang.String of JDK 25 (src/java.base/share/classes/java/lang/String.java, the JVM of the
 * buf.build plugin images): decoding of UTF-8 bytes (new String(bytes, UTF_8), which protobuf-java's
 * ByteString.toStringUtf8 uses for string fields) and compareTo; with Collections.sort
 * (List.sortBy) and Throwable.toString. */
#include "java_std.h"

#include <string.h>

static bool jutf8_cont(unsigned b) { return (b & 0xc0) == 0x80; }

/* String.decodeUTF8_UTF16 with replacement: protobuf-java decodes string fields with
 * ByteString.toStringUtf8 (new String(bytes, UTF_8)), and the decoded string is encoded to UTF-8
 * again on output. Appends the result to out (when not NULL); returns whether it differs from the
 * input (a malformed sequence was replaced, possibly dropping the rest of the input). */
static bool jutf8_decode(const unsigned char *src, size_t sl, Buf *out)
{
	static const char REPL[] = "\xef\xbf\xbd";
	bool changed = false;
	size_t sp = 0;
#define JREPL() (changed = true, out ? buf_add(out, REPL, 3) : (void)0)
#define JKEEP(from, len) (out ? buf_add(out, (const char *)src + (from), len) : (void)0)
	while (sp < sl) {
		size_t start = sp;
		unsigned b1 = src[sp++];
		if (b1 < 0x80) {
			JKEEP(start, 1);
		} else if (b1 >= 0xc2 && b1 <= 0xdf) {
			if (sp < sl) {
				unsigned b2 = src[sp++];
				if (!jutf8_cont(b2)) {
					JREPL();
					sp--;
				} else {
					JKEEP(start, 2);
				}
				continue;
			}
			JREPL();
			break;
		} else if (b1 >= 0xe0 && b1 <= 0xef) {
			if (sp + 1 < sl) {
				unsigned b2 = src[sp++], b3 = src[sp++];
				if ((b1 == 0xe0 && (b2 & 0xe0) == 0x80) || !jutf8_cont(b2) || !jutf8_cont(b3)) {
					JREPL();
					sp -= 3;
					/* malformed3 */
					sp += (b1 == 0xe0 && (b2 & 0xe0) == 0x80) || !jutf8_cont(b2) ? 1 : 2;
				} else if (b1 == 0xed && b2 >= 0xa0) {
					JREPL(); /* a surrogate */
				} else {
					JKEEP(start, 3);
				}
				continue;
			}
			if (sp < sl && ((b1 == 0xe0 && (src[sp] & 0xe0) == 0x80) || !jutf8_cont(src[sp]))) {
				JREPL();
				continue;
			}
			JREPL();
			break;
		} else if (b1 >= 0xf0 && b1 <= 0xf7) {
			if (sp + 2 < sl) {
				unsigned b2 = src[sp++], b3 = src[sp++], b4 = src[sp++];
				uint32_t uc = (b1 & 0x07) << 18 | (b2 & 0x3f) << 12 | (b3 & 0x3f) << 6 | (b4 & 0x3f);
				if (!jutf8_cont(b2) || !jutf8_cont(b3) || !jutf8_cont(b4) || uc < 0x10000 || uc > 0x10ffff) {
					JREPL();
					sp -= 4;
					/* malformed4 */
					if (b1 > 0xf4 || (b1 == 0xf0 && (b2 < 0x90 || b2 > 0xbf)) || (b1 == 0xf4 && (b2 & 0xf0) != 0x80) ||
					    !jutf8_cont(b2))
						sp += 1;
					else if (!jutf8_cont(b3))
						sp += 2;
					else
						sp += 3;
				} else {
					JKEEP(start, 4);
				}
				continue;
			}
			if (b1 > 0xf4 || (sp < sl && ((b1 == 0xf0 && (src[sp] < 0x90 || src[sp] > 0xbf)) ||
						      (b1 == 0xf4 && (src[sp] & 0xf0) != 0x80) || !jutf8_cont(src[sp])))) {
				JREPL();
				continue;
			}
			sp++;
			JREPL();
			if (sp < sl && !jutf8_cont(src[sp]))
				continue;
			break;
		} else {
			JREPL();
		}
	}
#undef JREPL
#undef JKEEP
	return changed;
}

/* The bytes of a protobuf string field (a NUL-terminated copy, see pb.h) as protobuf-java decodes
 * them into a String, encoded to UTF-8 again; the result is NUL-terminated too. */
Str java_string(Str s)
{
	if (!s.p)
		return (Str){"", 0};
	if (!jutf8_decode((const unsigned char *)s.p, s.n, NULL))
		return s;
	Buf b = {0};
	jutf8_decode((const unsigned char *)s.p, s.n, &b);
	const char *p = buf_str(&b);
	return (Str){p, b.n};
}

/* java_string as a C string. Java strings may contain U+0000, which the port does not support in
 * names, options and comments: it fails instead of producing a different output. */
const char *java_cstr(Str s)
{
	s = java_string(s);
	const char *nul = memchr(s.p, 0, s.n);
	if (nul)
		fail("protoc-gen-uni does not support NUL characters in strings: \"%s\\0...\"", xstrndup(s.p, (size_t)(nul - s.p)));
	return s.p;
}

/* Character.isLowerCase/isUpperCase/isDigit/toUpperCase/toLowerCase of the JDK's Unicode tables are
 * not ported, so names converted by NameUtils and friends must not contain characters of the BMP
 * beyond ASCII. Identifiers are ASCII (descriptor validation); file names, packages and ScalaPB
 * options need not be. Supplementary characters are two surrogates in Java, which are neither
 * letters nor digits, and the port treats their four bytes the same way. */
void java_require_ascii(const char *s)
{
	for (const char *p = s; *p; p++)
		if ((unsigned char)*p >= 0xc0 && (unsigned char)*p < 0xf0)
			fail("protoc-gen-uni does not support non-ASCII characters in names that ScalaPB converts with "
			     "Java's Unicode character rules: \"%s\"", s);
}

/* String.compareTo (UTF-16 code units) of two valid UTF-8 strings: byte order is code point order,
 * except that the surrogate pairs of supplementary characters (lead bytes F0-F4) sort before
 * U+E000-U+FFFF (lead bytes EE, EF). */
int java_strcmp(const char *a, const char *b)
{
	size_t i = 0;
	while (a[i] && a[i] == b[i])
		i++;
	unsigned x = (unsigned char)a[i], y = (unsigned char)b[i];
	if (x >= 0xee && y >= 0xee) {
		x = x <= 0xef ? x + 0x10 : x;
		y = y <= 0xef ? y + 0x10 : y;
	}
	return x < y ? -1 : x > y;
}

void java_sort(void **v, int n, int (*cmp)(const void *, const void *))
{
	if (n < 2)
		return;
	void **tmp = xalloc(sizeof(void *) * (size_t)n);
	for (int w = 1; w < n; w *= 2) {
		for (int lo = 0; lo < n; lo += 2 * w) {
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				tmp[k++] = cmp(v[j], v[i]) < 0 ? v[j++] : v[i++];
			while (i < mid)
				tmp[k++] = v[i++];
			while (j < hi)
				tmp[k++] = v[j++];
		}
		memcpy(v, tmp, sizeof(void *) * (size_t)n);
	}
}

void java_exception(const char *cls, const char *msg)
{
	fail("%s%s%s", cls, msg ? ": " : "", msg ? msg : "");
}
