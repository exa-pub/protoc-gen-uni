/* String.prototype.localeCompare in Node.js with the default (root) ICU collation, for the
 * characters of ECMAScript identifiers generated from Protobuf names: ASCII letters, digits, '_'
 * and '$'. Upstream sorts imported names with it (generated-file.ts). Same as collate.go of the
 * Go port.
 *
 * The root collation compares in levels: first the base characters ignoring case (punctuation '_'
 * sorts before the currency symbol '$', which sorts before digits, which sort before letters),
 * then the case (lowercase before uppercase). Other bytes are compared by value after letters,
 * which is not exact but does not occur in generated identifiers. */
#include "internal.h"

#include <string.h>

static int primary_weight(unsigned char r)
{
	if (r == '_')
		return 1;
	if (r == '$')
		return 2;
	if (r >= '0' && r <= '9')
		return 10 + (r - '0');
	if (r >= 'a' && r <= 'z')
		return 100 + (r - 'a');
	if (r >= 'A' && r <= 'Z')
		return 100 + (r - 'A');
	return 1000 + r;
}

int locale_compare(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b);
	for (int level = 0; level < 2; level++) {
		for (size_t i = 0; i < la && i < lb; i++) {
			unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
			int wa = level == 0 ? primary_weight(ca) : (ca >= 'A' && ca <= 'Z');
			int wb = level == 0 ? primary_weight(cb) : (cb >= 'A' && cb <= 'Z');
			if (wa != wb)
				return wa < wb ? -1 : 1;
		}
		if (la != lb)
			return la < lb ? -1 : 1;
	}
	int c = strcmp(a, b);
	return c < 0 ? -1 : c > 0;
}
