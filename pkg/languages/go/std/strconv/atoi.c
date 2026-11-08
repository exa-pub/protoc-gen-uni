/* Package strconv, ported from go1.27.1 src/internal/strconv/atoi.go and atob.go (ParseUint,
 * ParseInt, ParseBool; the error kinds instead of *NumError). Copyright 2009 The Go Authors. All
 * rights reserved. BSD-3-Clause (see LICENSE in Go). */

static char strconv_lower(char c) { return (char)(c | ('x' - 'X')); }

/* underscoreOK reports whether the underscores in s are allowed. Checking them in this one
 * function lets all the parsers skip over them simply. Underscore must appear only between digits
 * or between a base prefix and a digit. */
static bool strconv_underscoreOK(const char *s, size_t n)
{
	/* saw tracks the last character (class) we saw: ^ for beginning of number, 0 for a digit or
	 * base prefix, _ for an underscore, ! for none of the above. */
	char saw = '^';
	size_t i = 0;

	/* Optional sign. */
	if (n >= 1 && (s[0] == '-' || s[0] == '+'))
		s++, n--;

	/* Optional base prefix. */
	bool hex = false;
	if (n >= 2 && s[0] == '0' && (strconv_lower(s[1]) == 'b' || strconv_lower(s[1]) == 'o' || strconv_lower(s[1]) == 'x')) {
		i = 2;
		saw = '0'; /* base prefix counts as a digit for "underscore as digit separator" */
		hex = strconv_lower(s[1]) == 'x';
	}

	/* Number proper. */
	for (; i < n; i++) {
		/* Digits are always okay. */
		if (('0' <= s[i] && s[i] <= '9') || (hex && 'a' <= strconv_lower(s[i]) && strconv_lower(s[i]) <= 'f')) {
			saw = '0';
			continue;
		}
		/* Underscore must follow digit. */
		if (s[i] == '_') {
			if (saw != '0')
				return false;
			saw = '_';
			continue;
		}
		/* Underscore must also be followed by digit. */
		if (saw == '_')
			return false;
		/* Saw non-digit, non-underscore. */
		saw = '!';
	}
	return saw != '_';
}

/* ParseUint is like ParseInt but for unsigned numbers. A sign prefix is not permitted. */
static int strconv_ParseUint(const char *s, size_t n, int base, int bitSize, uint64_t *out)
{
	*out = 0;
	if (n == 0)
		return GO_STD_ERR_SYNTAX;

	bool base0 = base == 0;
	const char *s0 = s;
	size_t n0 = n;
	if (2 <= base && base <= 36) {
		/* valid base; nothing to do */
	} else if (base == 0) {
		/* Look for octal, hex prefix. */
		base = 10;
		if (s[0] == '0') {
			if (n >= 3 && strconv_lower(s[1]) == 'b') {
				base = 2;
				s += 2, n -= 2;
			} else if (n >= 3 && strconv_lower(s[1]) == 'o') {
				base = 8;
				s += 2, n -= 2;
			} else if (n >= 3 && strconv_lower(s[1]) == 'x') {
				base = 16;
				s += 2, n -= 2;
			} else {
				base = 8;
				s += 1, n -= 1;
			}
		}
	} else {
		return GO_STD_ERR_BASE;
	}

	if (bitSize == 0)
		bitSize = 64;
	else if (bitSize < 0 || bitSize > 64)
		return GO_STD_ERR_BASE;

	/* Cutoff is the smallest number such that cutoff*base > maxUint64. */
	uint64_t cutoff = UINT64_MAX / (uint64_t)base + 1;
	uint64_t maxVal = bitSize == 64 ? UINT64_MAX : ((uint64_t)1 << bitSize) - 1;

	bool underscores = false;
	uint64_t v = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		uint8_t d;
		if (c == '_' && base0) {
			underscores = true;
			continue;
		} else if ('0' <= c && c <= '9') {
			d = (uint8_t)(c - '0');
		} else if ('a' <= strconv_lower(c) && strconv_lower(c) <= 'z') {
			d = (uint8_t)(strconv_lower(c) - 'a' + 10);
		} else {
			return GO_STD_ERR_SYNTAX;
		}

		if (d >= (uint8_t)base)
			return GO_STD_ERR_SYNTAX;

		if (v >= cutoff) {
			/* v*base overflows */
			*out = maxVal;
			return GO_STD_ERR_RANGE;
		}
		v *= (uint64_t)base;

		uint64_t n1 = v + d;
		if (n1 < v || n1 > maxVal) {
			/* v+d overflows */
			*out = maxVal;
			return GO_STD_ERR_RANGE;
		}
		v = n1;
	}

	if (underscores && !strconv_underscoreOK(s0, n0))
		return GO_STD_ERR_SYNTAX;

	*out = v;
	return 0;
}

/* ParseInt interprets a string s in the given base (0, 2 to 36) and bit size (0 to 64) and
 * returns the corresponding value i. */
static int strconv_ParseInt(const char *s, size_t n, int base, int bitSize, int64_t *out)
{
	*out = 0;
	if (n == 0)
		return GO_STD_ERR_SYNTAX;

	/* Pick off leading sign. */
	bool neg = false;
	if (s[0] == '+') {
		s++, n--;
	} else if (s[0] == '-') {
		neg = true;
		s++, n--;
	}

	/* Convert unsigned and check range. */
	uint64_t un;
	int err = strconv_ParseUint(s, n, base, bitSize, &un);
	if (err && err != GO_STD_ERR_RANGE)
		return err;

	if (bitSize == 0)
		bitSize = 64;

	uint64_t cutoff = (uint64_t)1 << (bitSize - 1);
	if (!neg && un >= cutoff) {
		*out = (int64_t)(cutoff - 1);
		return GO_STD_ERR_RANGE;
	}
	if (neg && un > cutoff) {
		*out = -(int64_t)(cutoff - 1) - 1;
		return GO_STD_ERR_RANGE;
	}
	*out = neg ? (int64_t)(0 - un) : (int64_t)un;
	return err;
}

/* ParseBool returns the boolean value represented by the string. It accepts 1, t, T, TRUE, true,
 * True, 0, f, F, FALSE, false, False. Any other value returns an error. */
static int strconv_ParseBool(const char *s, size_t n, bool *out)
{
	static const char *const t[] = {"1", "t", "T", "true", "TRUE", "True"};
	static const char *const f[] = {"0", "f", "F", "false", "FALSE", "False"};
	for (size_t i = 0; i < 6; i++) {
		if (strlen(t[i]) == n && memcmp(t[i], s, n) == 0) {
			*out = true;
			return 0;
		}
		if (strlen(f[i]) == n && memcmp(f[i], s, n) == 0) {
			*out = false;
			return 0;
		}
	}
	*out = false;
	return GO_STD_ERR_SYNTAX;
}
