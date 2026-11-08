/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of generator/pattern/match.go (vtprotobuf v0.6.0; derived from
 * github.com/bmatcuk/doublestar): the shell patterns of the pool= and pool-exclude= options,
 * matched against "<import path>.<Go name>". Strings are byte slices (s, n) as in Go; Go would
 * panic on an out-of-range index, which the code below never produces either. */

/* Limits. Go copies the pattern for every alternative of a `{...}` and drops the copies with the
 * GC. Here a copy made for an alternative that is not the last one is freed (arena_release) when
 * its match returns, and the last alternative, which Go matches with a tail call, is spliced in
 * place into a copy owned by the frame (`own`) and matched by the same frame. A match keeps one
 * copy per nesting level of non-last alternatives alive; that nesting (also the recursion depth)
 * is limited to PAT_MAX_DEPTH levels. The work of all matches of a request (bytes copied and
 * matching steps) is limited to PAT_BUDGET: upstream copies the pattern for every `{` (quadratic
 * time in the pattern length, for every message) and backtracks through every combination of
 * alternatives. Beyond a limit generation fails with an "unsupported by protoc-gen-uni" error;
 * below them every result is upstream's. (The pool= values come from the comma-separated
 * parameter, so they have no commas and every `{...}` has a single alternative.) */
#define PAT_UNSUPPORTED "unsupported by protoc-gen-uni: "
#define PAT_MAX_DEPTH 100
#define PAT_BUDGET ((uint64_t)1 << 30)
static uint64_t pat_work;		     /* work of the matches of this request */
static uint64_t pat_budget = PAT_BUDGET; /* a variable for the unit test */
static bool pat_unsupported; /* the last fail() is one of the errors below */

static COLD NORETURN void pat_too_deep(void)
{
	pat_unsupported = true;
	fail(PAT_UNSUPPORTED "pool or pool-exclude pattern with alternatives nested over %d levels", PAT_MAX_DEPTH);
}

static COLD NORETURN void pat_too_costly(void)
{
	pat_unsupported = true;
	fail(PAT_UNSUPPORTED "pool or pool-exclude patterns that take over %d million steps to match", (int)(PAT_BUDGET / 1000000));
}

/* Charges n units of work (a step or a byte copied) to the budget. */
static inline void pat_charge(size_t n)
{
	pat_work += n;
	if (pat_work > pat_budget)
		pat_too_costly();
}

/* utf8.DecodeRuneInString: (RuneError, 0) for an empty string. */
static int pat_decode(const char *s, size_t n, uint32_t *r)
{
	if (!n) {
		*r = 0xfffd;
		return 0;
	}
	return utf8_decode(s, n, r);
}

#define PAT_ERR_BAD_PATTERN (-1)

static bool pat_do_validate(const char *s, size_t l, uint32_t separator);

/* Finds the next comma, but ignores any commas that appear inside nested `{}`. */
static long pat_index_next_alt(const char *s, size_t l, bool allow_escaping)
{
	int alts = 1;
	for (size_t i = 0; i < l; i++) {
		if (allow_escaping && s[i] == '\\')
			i++; /* skip next byte */
		else if (s[i] == '{')
			alts++;
		else if (s[i] == '}')
			alts--;
		else if (s[i] == ',' && alts == 1)
			return (long)i;
	}
	return -1;
}

/* Finds the index of the first unescaped byte c, or -1. */
static long pat_index_unescaped_byte(const char *s, size_t l, char c, bool allow_escaping)
{
	for (size_t i = 0; i < l; i++) {
		if (allow_escaping && s[i] == '\\')
			i++; /* skip next byte */
		else if (s[i] == c)
			return (long)i;
	}
	return -1;
}

/* Assuming the byte before the beginning of s is an opening `{`, finds the index of the matching
 * `}`. */
static long pat_index_matched_closing_alt(const char *s, size_t l, bool allow_escaping)
{
	int alts = 1;
	for (size_t i = 0; i < l; i++) {
		if (allow_escaping && s[i] == '\\') {
			i++; /* skip next byte */
		} else if (s[i] == '{') {
			alts++;
		} else if (s[i] == '}') {
			if (--alts == 0)
				return (long)i;
		}
	}
	return -1;
}

/* a[:i] + a[j:k] + a[m:] (i <= j <= k <= m <= n), a new copy */
static char *pat_splice(const char *a, size_t n, size_t i, size_t j, size_t k, size_t m, size_t *out_n)
{
	size_t len = i + (k - j) + (n - m);
	pat_charge(len + 1);
	char *s = xalloc(len + 1);
	memcpy(s, a, i);
	memcpy(s + i, a + j, k - j);
	memcpy(s + i + (k - j), a + m, n - m);
	*out_n = len;
	return s;
}

/* a[:i] + a[j:k] + a[m:] in place if the frame owns a (*own == a), else into a new copy that it
 * then owns. */
static const char *pat_splice_tail(const char *a, size_t n, char **own, size_t i, size_t j, size_t k, size_t m, size_t *out_n)
{
	if (*own != a)
		return *own = pat_splice(a, n, i, j, k, m, out_n);
	char *s = *own;
	size_t len = i + (k - j) + (n - m);
	pat_charge(len + 1);
	memmove(s + i, s + j, k - j);
	memmove(s + i + (k - j), s + m, n - m);
	*out_n = len;
	return s;
}

/* isZeroLengthPattern: 1 (true), 0 (false) or PAT_ERR_BAD_PATTERN. own: pattern if the frame
 * may change it, else NULL; depth: the nesting of non-last alternatives. */
static int pat_is_zero_length(const char *pattern, size_t n, uint32_t separator, char *own, int depth)
{
again:;
	/* "/" "**", "**" "/" and "/" "**" "/" are special cases: a pattern such as "path/to/a/" "**"
	 * or "path/to/a/" "**" "/" should match "path/to/a" because "a" might be a directory. */
	char sep = (char)separator;
	if (n == 0 || (n == 1 && pattern[0] == '*') || (n == 2 && !memcmp(pattern, "**", 2)) ||
	    (n == 3 && pattern[0] == sep && !memcmp(pattern + 1, "**", 2)) || (n == 3 && !memcmp(pattern, "**", 2) && pattern[2] == sep) ||
	    (n == 4 && pattern[0] == sep && !memcmp(pattern + 1, "**", 2) && pattern[3] == sep))
		return 1;

	if (pattern[0] == '{') {
		long closing = pat_index_matched_closing_alt(pattern + 1, n - 1, separator != '\\');
		if (closing == -1)
			return PAT_ERR_BAD_PATTERN; /* no closing '}' */
		size_t closing_idx = (size_t)closing + 1;
		size_t pat_idx = 1;
		for (;;) {
			long comma = pat_index_next_alt(pattern + pat_idx, closing_idx - pat_idx, separator != '\\');
			if (comma == -1)
				break;
			size_t comma_idx = (size_t)comma + pat_idx;
			if (depth >= PAT_MAX_DEPTH)
				pat_too_deep();
			ArenaMark mark = arena_mark();
			size_t ln;
			char *alt = pat_splice(pattern, n, 0, pat_idx, comma_idx, closing_idx + 1, &ln);
			int ret = pat_is_zero_length(alt, ln, separator, alt, depth + 1);
			arena_release(mark);
			if (ret != 0)
				return ret;
			pat_idx = comma_idx + 1;
		}
		/* return isZeroLengthPattern(alt, separator) */
		pattern = pat_splice_tail(pattern, n, &own, 0, pat_idx, closing_idx, closing_idx + 1, &n);
		goto again;
	}

	/* no luck - validate the rest of the pattern */
	if (!pat_do_validate(pattern, n, separator))
		return PAT_ERR_BAD_PATTERN;
	return 0;
}

/* doMatchWithSeparator: 1, 0 or PAT_ERR_BAD_PATTERN. */
static int pat_do_match(const char *pattern, size_t pat_len, const char *name, size_t name_len, uint32_t separator, bool validate,
			long doublestar_pattern_backtrack, long doublestar_name_backtrack, long star_pattern_backtrack,
			long star_name_backtrack, size_t pat_idx, size_t name_idx, char *own, int depth)
{
	bool start_of_segment;
CALL:
	start_of_segment = true;
MATCH:
	while (name_idx < name_len) {
		pat_charge(1);
		if (pat_idx < pat_len) {
			switch (pattern[pat_idx]) {
			case '*':
				if (++pat_idx < pat_len && pattern[pat_idx] == '*') {
					/* doublestar - must begin with a path separator, otherwise we'll
					 * treat it like a single star like bash */
					pat_idx++;
					if (start_of_segment) {
						if (pat_idx >= pat_len)
							return 1; /* pattern ends in "/" "**": return true */
						/* doublestar must also end with a path separator, otherwise
						 * we're just going to treat the doublestar as a single star
						 * like bash */
						uint32_t pat_rune;
						int pat_rune_len = pat_decode(pattern + pat_idx, pat_len - pat_idx, &pat_rune);
						if (pat_rune == separator) {
							pat_idx += (size_t)pat_rune_len;
							doublestar_pattern_backtrack = (long)pat_idx;
							doublestar_name_backtrack = (long)name_idx;
							star_pattern_backtrack = -1;
							star_name_backtrack = -1;
							continue;
						}
					}
				}
				start_of_segment = false;
				star_pattern_backtrack = (long)pat_idx;
				star_name_backtrack = (long)name_idx;
				continue;

			case '?': {
				start_of_segment = false;
				uint32_t name_rune;
				int name_rune_len = pat_decode(name + name_idx, name_len - name_idx, &name_rune);
				if (name_rune == separator)
					break; /* `?` cannot match the separator */
				pat_idx++;
				name_idx += (size_t)name_rune_len;
				continue;
			}

			case '[': {
				start_of_segment = false;
				if (++pat_idx >= pat_len)
					return PAT_ERR_BAD_PATTERN; /* class didn't end */
				uint32_t name_rune;
				int name_rune_len = pat_decode(name + name_idx, name_len - name_idx, &name_rune);
				bool matched = false;
				bool negate = pattern[pat_idx] == '!' || pattern[pat_idx] == '^';
				if (negate)
					pat_idx++;
				if (pat_idx >= pat_len || pattern[pat_idx] == ']')
					return PAT_ERR_BAD_PATTERN; /* class didn't end or empty character class */

				const uint32_t max_rune = 0x10FFFF;
				uint32_t last = max_rune;
				while (pat_idx < pat_len && pattern[pat_idx] != ']') {
					uint32_t pat_rune;
					int pat_rune_len = pat_decode(pattern + pat_idx, pat_len - pat_idx, &pat_rune);
					pat_idx += (size_t)pat_rune_len;

					/* match a range */
					if (last < max_rune && pat_rune == '-' && pat_idx < pat_len && pattern[pat_idx] != ']') {
						if (pattern[pat_idx] == '\\')
							pat_idx++; /* next character is escaped */
						pat_rune_len = pat_decode(pattern + pat_idx, pat_len - pat_idx, &pat_rune);
						pat_idx += (size_t)pat_rune_len;
						if (last <= name_rune && name_rune <= pat_rune) {
							matched = true;
							break;
						}
						/* didn't match range - reset `last` */
						last = max_rune;
						continue;
					}

					/* not a range - check if the next rune is escaped */
					if (pat_rune == '\\') {
						pat_rune_len = pat_decode(pattern + pat_idx, pat_len - pat_idx, &pat_rune);
						pat_idx += (size_t)pat_rune_len;
					}

					/* check if the rune matches */
					if (pat_rune == name_rune) {
						matched = true;
						break;
					}

					/* no matches yet */
					last = pat_rune;
				}

				if (matched == negate) {
					/* failed to match - if we reached the end of the pattern, that means
					 * we never found a closing `]` */
					if (pat_idx >= pat_len)
						return PAT_ERR_BAD_PATTERN;
					break;
				}

				long closing = pat_index_unescaped_byte(pattern + pat_idx, pat_len - pat_idx, ']', true);
				if (closing == -1)
					return PAT_ERR_BAD_PATTERN; /* no closing `]` */
				pat_idx += (size_t)closing + 1;
				name_idx += (size_t)name_rune_len;
				continue;
			}

			case '{': {
				start_of_segment = false;
				size_t before_idx = pat_idx;
				pat_idx++;
				long closing = pat_index_matched_closing_alt(pattern + pat_idx, pat_len - pat_idx, separator != '\\');
				if (closing == -1)
					return PAT_ERR_BAD_PATTERN; /* no closing `}` */
				size_t closing_idx = (size_t)closing + pat_idx;
				for (;;) {
					long comma = pat_index_next_alt(pattern + pat_idx, closing_idx - pat_idx, separator != '\\');
					if (comma == -1)
						break;
					size_t comma_idx = (size_t)comma + pat_idx;
					if (depth >= PAT_MAX_DEPTH)
						pat_too_deep();
					ArenaMark mark = arena_mark();
					size_t ln;
					char *alt = pat_splice(pattern, pat_len, before_idx, pat_idx, comma_idx, closing_idx + 1, &ln);
					int result = pat_do_match(alt, ln, name, name_len, separator, validate, doublestar_pattern_backtrack,
								  doublestar_name_backtrack, star_pattern_backtrack, star_name_backtrack,
								  before_idx, name_idx, alt, depth + 1);
					arena_release(mark);
					if (result != 0)
						return result;
					pat_idx = comma_idx + 1;
				}
				/* return doMatchWithSeparator(alt, ..., beforeIdx, nameIdx) */
				pattern = pat_splice_tail(pattern, pat_len, &own, before_idx, pat_idx, closing_idx, closing_idx + 1, &pat_len);
				pat_idx = before_idx;
				goto CALL;
			}

			case '\\':
				if (separator != '\\') {
					/* next rune is "escaped" in the pattern - literal match */
					if (++pat_idx >= pat_len)
						return PAT_ERR_BAD_PATTERN; /* pattern ended */
				}
				/* fallthrough */
			default: {
				uint32_t pat_rune, name_rune;
				int pat_rune_len = pat_decode(pattern + pat_idx, pat_len - pat_idx, &pat_rune);
				int name_rune_len = pat_decode(name + name_idx, name_len - name_idx, &name_rune);
				if (pat_rune != name_rune) {
					if (separator != '\\' && pat_idx > 0 && pattern[pat_idx - 1] == '\\') {
						/* if this rune was meant to be escaped, we need to move pat_idx
						 * back to the backslash before backtracking or validating
						 * below */
						pat_idx--;
					}
					break;
				}
				pat_idx += (size_t)pat_rune_len;
				name_idx += (size_t)name_rune_len;
				start_of_segment = pat_rune == separator;
				continue;
			}
			}
		}

		if (star_pattern_backtrack >= 0) {
			/* `*` backtrack, but only if the `name` rune isn't the separator */
			uint32_t name_rune;
			int name_rune_len = pat_decode(name + star_name_backtrack, name_len - (size_t)star_name_backtrack, &name_rune);
			if (name_rune != separator) {
				star_name_backtrack += name_rune_len;
				pat_idx = (size_t)star_pattern_backtrack;
				name_idx = (size_t)star_name_backtrack;
				start_of_segment = false;
				continue;
			}
		}

		if (doublestar_pattern_backtrack >= 0) {
			/* `**` backtrack, advance `name` past next separator */
			name_idx = (size_t)doublestar_name_backtrack;
			while (name_idx < name_len) {
				uint32_t name_rune;
				int name_rune_len = pat_decode(name + name_idx, name_len - name_idx, &name_rune);
				name_idx += (size_t)name_rune_len;
				if (name_rune == separator) {
					doublestar_name_backtrack = (long)name_idx;
					pat_idx = (size_t)doublestar_pattern_backtrack;
					start_of_segment = true;
					goto MATCH;
				}
			}
		}

		if (validate && pat_idx < pat_len && !pat_do_validate(pattern + pat_idx, pat_len - pat_idx, separator))
			return PAT_ERR_BAD_PATTERN;
		return 0;
	}

	if (name_idx < name_len)
		return 0; /* we reached the end of `pattern` before the end of `name` */

	/* we've reached the end of `name`; we've successfully matched if we've also reached the end
	 * of `pattern`, or if the rest of `pattern` can match a zero-length string */
	return pat_is_zero_length(pattern + pat_idx, pat_len - pat_idx, separator, own ? own + pat_idx : NULL, depth);
}

/* Match: 1 if name matches the shell pattern, 0 if not, PAT_ERR_BAD_PATTERN if the pattern is
 * malformed. */
static int pat_match(Str pattern, Str name)
{
	return pat_do_match(pattern.p, pattern.n, name.p, name.n, '/', true, -1, -1, -1, -1, 0, 0, NULL, 0);
}

static bool pat_do_validate(const char *s, size_t l, uint32_t separator)
{
	int alt_depth = 0;
	for (size_t i = 0; i < l; i++) {
		switch (s[i]) {
		case '\\':
			if (separator != '\\') {
				/* skip the next byte - return false if there is no next byte */
				if (++i >= l)
					return false;
			}
			continue;
		case '[':
			if (++i >= l)
				return false; /* class didn't end */
			if (s[i] == '^' || s[i] == '!')
				i++;
			if (i >= l || s[i] == ']')
				return false; /* class didn't end or empty character class */
			for (; i < l; i++) {
				if (separator != '\\' && s[i] == '\\')
					i++;
				else if (s[i] == ']')
					goto next; /* looks good */
			}
			return false; /* class didn't end */
		case '{':
			alt_depth++;
			continue;
		case '}':
			if (alt_depth == 0)
				return false; /* alt end without a corresponding start */
			alt_depth--;
			continue;
		}
	next:;
	}
	/* valid as long as all alts are closed */
	return alt_depth == 0;
}

/* ValidatePattern assumes your pattern uses '/' as the path separator. */
static bool pat_validate(Str s) { return pat_do_validate(s.p, s.n, '/'); }
