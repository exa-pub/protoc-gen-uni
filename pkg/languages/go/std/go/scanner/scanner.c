/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Package go/scanner: ported from go1.27.1 src/go/scanner/scanner.go.
 *
 * Literals returned by Scan alias the source (Go copies them; the source is immutable here)
 * except comments and raw strings containing '\r', which are new strings. The stringEnd field
 * is read directly by the parser (Go: the scannerhooks.StringEnd backdoor). */
#include "../../internal.h"

enum {
	scanner_bom = 0xFEFF, /* byte order mark, only permitted as very first character */
	scanner_eof = -1, /* end of file */
};

/* ---- helpers for fmt verbs and path/filepath (Unix semantics) used by the scanner ---- */

/* fmt's %#U: "U+0041 'A'" (the quoted character only if strconv.IsPrint). */
static void scanner_fmtSharpU(Buf *b, rune ch)
{
	uint32_t u = (uint32_t)ch; /* not ported: negative runes (Go formats uint64(int64(r))); never passed */
	buf_adds(b, "U+");
	char hex[16];
	int n = 0;
	do {
		hex[n++] = "0123456789ABCDEF"[u & 0xF];
		u >>= 4;
	} while (u);
	for (int i = n; i < 4; i++)
		buf_addc(b, '0');
	while (n > 0)
		buf_addc(b, hex[--n]);
	if ((uint32_t)ch <= utf8_MaxRune && strconv_IsPrint(ch)) {
		buf_adds(b, " '");
		utf8_AppendRune(b, ch);
		buf_addc(b, '\'');
	}
}

/* fmt's %q of a rune: strconv.QuoteRune (values > MaxRune become RuneError). */
static void scanner_fmtQ(Buf *b, rune ch)
{
	if ((uint32_t)ch > utf8_MaxRune)
		ch = utf8_RuneError;
	buf_addg(b, strconv_QuoteRune(ch));
}

/* filepath.Split: splits immediately following the final '/'. */
static GoStr scanner_filepathDir(GoStr path)
{
	int i = path.n - 1;
	while (i >= 0 && path.p[i] != '/')
		i--;
	return gostr_sub(path, 0, i + 1);
}

/* filepath.Clean (Unix): the shortest path name equivalent to path by purely lexical
 * processing (path.Clean's algorithm). */
static GoStr scanner_filepathClean(GoStr path)
{
	if (path.n == 0)
		return GOSTR(".");
	bool rooted = path.p[0] == '/';
	int n = path.n;
	/* Invariants: reading from path; r is index of next byte to process. Writing to out; dotdot
	 * is index in out where .. must stop, either because it is the leading slash or it is a
	 * leading ../../.. prefix. */
	char *out = xalloc((size_t)n + 1);
	int w = 0;
	int r = 0, dotdot = 0;
	if (rooted) {
		out[w++] = '/';
		r = 1;
		dotdot = 1;
	}
	while (r < n) {
		if (path.p[r] == '/') {
			/* empty path element */
			r++;
		} else if (path.p[r] == '.' && (r + 1 == n || path.p[r + 1] == '/')) {
			/* . element */
			r++;
		} else if (path.p[r] == '.' && path.p[r + 1] == '.' && (r + 2 == n || path.p[r + 2] == '/')) {
			/* .. element: remove to last / */
			r += 2;
			if (w > dotdot) {
				/* can backtrack */
				w--;
				while (w > dotdot && out[w] != '/')
					w--;
			} else if (!rooted) {
				/* cannot backtrack, but not rooted, so append .. element. */
				if (w > 0)
					out[w++] = '/';
				out[w++] = '.';
				out[w++] = '.';
				dotdot = w;
			}
		} else {
			/* real path element. add slash if needed */
			if ((rooted && w != 1) || (!rooted && w != 0))
				out[w++] = '/';
			/* copy element */
			for (; r < n && path.p[r] != '/'; r++)
				out[w++] = path.p[r];
		}
	}
	/* Turn empty string into "." */
	if (w == 0)
		return GOSTR(".");
	return (GoStr){out, w};
}

/* filepath.Join (Unix) of two elements. */
static GoStr scanner_filepathJoin(GoStr dir, GoStr name)
{
	if (dir.n == 0) {
		if (name.n == 0)
			return (GoStr){"", 0};
		return scanner_filepathClean(name);
	}
	if (name.n == 0)
		return scanner_filepathClean(dir);
	return scanner_filepathClean(gostr_cat(gostr_cat(dir, GOSTR("/")), name));
}

/* ---- scanner.go ---- */

static void scanner_Scanner_error(scanner_Scanner *s, int offs, GoStr msg);

/* Read the next Unicode char into s.ch. s.ch < 0 means end-of-file. */
static void scanner_Scanner_next(scanner_Scanner *s)
{
	if (s->rdOffset < s->src.n) {
		s->offset = s->rdOffset;
		if (s->ch == '\n') {
			s->lineOffset = s->offset;
			token_File_AddLine(s->file, s->offset);
		}
		rune r = (uint8_t)s->src.p[s->rdOffset];
		int w = 1;
		if (r == 0) {
			scanner_Scanner_error(s, s->offset, GOSTR("illegal character NUL"));
		} else if (r >= utf8_RuneSelf) {
			/* not ASCII */
			r = utf8_DecodeRune(s->src.p + s->rdOffset, s->src.n - s->rdOffset, &w);
			if (r == utf8_RuneError && w == 1) {
				const uint8_t *in = (const uint8_t *)s->src.p + s->rdOffset;
				int nin = s->src.n - s->rdOffset;
				if (s->offset == 0 && nin >= 2 &&
				    ((in[0] == 0xFF && in[1] == 0xFE) || (in[0] == 0xFE && in[1] == 0xFF))) {
					/* U+FEFF BOM at start of file, encoded as big- or little-endian UCS-2
					 * (i.e. 2-byte UTF-16). Give specific error (go.dev/issue/71950). */
					scanner_Scanner_error(s, s->offset,
							      GOSTR("illegal UTF-8 encoding (got UTF-16)"));
					s->rdOffset += nin; /* consume all input to avoid error cascade */
				} else {
					scanner_Scanner_error(s, s->offset, GOSTR("illegal UTF-8 encoding"));
				}
			} else if (r == scanner_bom && s->offset > 0) {
				scanner_Scanner_error(s, s->offset, GOSTR("illegal byte order mark"));
			}
		}
		s->rdOffset += w;
		s->ch = r;
	} else {
		s->offset = s->src.n;
		if (s->ch == '\n') {
			s->lineOffset = s->offset;
			token_File_AddLine(s->file, s->offset);
		}
		s->ch = scanner_eof;
	}
}

/* peek returns the byte following the most recently read character without advancing the
 * scanner. If the scanner is at EOF, peek returns 0. */
static uint8_t scanner_Scanner_peek(scanner_Scanner *s)
{
	if (s->rdOffset < s->src.n)
		return (uint8_t)s->src.p[s->rdOffset];
	return 0;
}

/* Init prepares the scanner s to tokenize the text src by setting the scanner at the beginning
 * of src. The scanner uses the file set file for position information and it adds line
 * information for each line. Note that Init may call err if there is an error in the first
 * character of the file. */
static void scanner_Scanner_Init(scanner_Scanner *s, token_File *file, GoStr src, scanner_ErrorHandler err,
				 void *errctx, int mode)
{
	/* Explicitly initialize all fields since a scanner may be reused. */
	if (token_File_Size(file) != src.n)
		fail("file size (%d) does not match src len (%d)", token_File_Size(file), src.n);
	s->file = file;
	s->dir = scanner_filepathDir(file->name);
	s->src = src;
	s->err = err;
	s->errctx = errctx;
	s->mode = mode;

	s->ch = ' ';
	s->offset = 0;
	s->rdOffset = 0;
	s->lineOffset = 0;
	s->insertSemi = false;
	s->ErrorCount = 0;

	scanner_Scanner_next(s);
	if (s->ch == scanner_bom)
		scanner_Scanner_next(s); /* ignore BOM at file beginning */
}

static void scanner_Scanner_error(scanner_Scanner *s, int offs, GoStr msg)
{
	if (s->err)
		s->err(s->errctx, token_File_Position(s->file, token_File_Pos(s->file, offs)), msg);
	s->ErrorCount++;
}

static void scanner_Scanner_updateLineInfo(scanner_Scanner *s, int next, int offs, GoStr text);
static GoStr scanner_stripCR(GoStr b, bool comment);

/* scanComment returns the text of the comment and (if nonzero) the offset of the first newline
 * within it, which implies a / *...* / comment. */
static GoStr scanner_Scanner_scanComment(scanner_Scanner *s, int *nlOffsetOut)
{
	/* initial '/' already consumed; s.ch == '/' || s.ch == '*' */
	int offs = s->offset - 1; /* position of initial '/' */
	int next = -1; /* position immediately following the comment; < 0 means invalid comment */
	int numCR = 0;
	int nlOffset = 0; /* offset of first newline within / *...* / comment */

	if (s->ch == '/') {
		/* //-style comment (the final '\n' is not considered part of the comment) */
		scanner_Scanner_next(s);
		while (s->ch != '\n' && s->ch >= 0) {
			if (s->ch == '\r')
				numCR++;
			scanner_Scanner_next(s);
		}
		/* if we are at '\n', the position following the comment is afterwards */
		next = s->offset;
		if (s->ch == '\n')
			next++;
		goto exit;
	}

	/* / *-style comment */
	scanner_Scanner_next(s);
	while (s->ch >= 0) {
		rune ch = s->ch;
		if (ch == '\r')
			numCR++;
		else if (ch == '\n' && nlOffset == 0)
			nlOffset = s->offset;
		scanner_Scanner_next(s);
		if (ch == '*' && s->ch == '/') {
			scanner_Scanner_next(s);
			next = s->offset;
			goto exit;
		}
	}

	scanner_Scanner_error(s, offs, GOSTR("comment not terminated"));

exit:;
	GoStr lit = gostr_sub(s->src, offs, s->offset);

	/* On Windows, a (//-comment) line may end in "\r\n". Remove the final '\r' before
	 * analyzing the text for line directives (matching the compiler). Remove any other '\r'
	 * afterwards (matching the pre-existing behavior of the scanner). */
	if (numCR > 0 && lit.n >= 2 && lit.p[1] == '/' && lit.p[lit.n - 1] == '\r') {
		lit.n--;
		numCR--;
	}

	/* interpret line directives (//line directives must start at the beginning of the current
	 * line) */
	if (next >= 0 /* implies valid comment */ && (lit.p[1] == '*' || offs == s->lineOffset) &&
	    strings_HasPrefix(gostr_from(lit, 2), GOSTR("line ")))
		scanner_Scanner_updateLineInfo(s, next, offs, lit);

	if (numCR > 0)
		lit = scanner_stripCR(lit, lit.p[1] == '*');

	*nlOffsetOut = nlOffset;
	return lit;
}

static int scanner_trailingDigits(GoStr text, int64_t *n, bool *ok);

/* updateLineInfo parses the incoming comment text at offset offs as a line directive. If
 * successful, it updates the line info table for the position next per the line directive. */
static void scanner_Scanner_updateLineInfo(scanner_Scanner *s, int next, int offs, GoStr text)
{
	/* extract comment text */
	if (text.p[1] == '*')
		text = gostr_sub(text, 0, text.n - 2); /* lop off trailing "* /" */
	text = gostr_from(text, 7); /* lop off leading "//line " or "/ *line " */
	offs += 7;

	int64_t n;
	bool ok;
	int i = scanner_trailingDigits(text, &n, &ok);
	if (i == 0)
		return; /* ignore (not a line directive) */
	/* i > 0 */

	if (!ok) {
		/* text has a suffix :xxx but xxx is not a number */
		scanner_Scanner_error(s, offs + i, gostr_cat(GOSTR("invalid line number: "), gostr_from(text, i)));
		return;
	}

	/* Put a cap on the maximum size of line and column numbers. 30 bits allows for some
	 * additional space before wrapping an int32. Keep this consistent with
	 * cmd/compile/internal/syntax.PosMax. */
	const int64_t maxLineCol = (int64_t)1 << 30;
	int64_t line, col = 0;
	int64_t n2;
	bool ok2;
	int i2 = scanner_trailingDigits(gostr_sub(text, 0, i - 1), &n2, &ok2);
	if (ok2) {
		/* //line filename:line:col */
		int t = i;
		i = i2;
		i2 = t;
		line = n2;
		col = n;
		if (col == 0 || col > maxLineCol) {
			scanner_Scanner_error(s, offs + i2,
					      gostr_cat(GOSTR("invalid column number: "), gostr_from(text, i2)));
			return;
		}
		text = gostr_sub(text, 0, i2 - 1); /* lop off ":col" */
	} else {
		/* //line filename:line */
		line = n;
	}

	if (line == 0 || line > maxLineCol) {
		scanner_Scanner_error(s, offs + i, gostr_cat(GOSTR("invalid line number: "), gostr_from(text, i)));
		return;
	}

	/* If we have a column (//line filename:line:col form), an empty filename means to use the
	 * previous filename. */
	GoStr filename = gostr_dup(gostr_sub(text, 0, i - 1)); /* lop off ":line", and trim white space */
	if (filename.n == 0 && ok2) {
		filename = token_File_Position(s->file, token_File_Pos(s->file, offs)).Filename;
	} else if (filename.n != 0) {
		/* Put a relative filename in the current directory. This is for compatibility with
		 * earlier releases. See issue 26671. */
		filename = scanner_filepathClean(filename);
		if (!(filename.n > 0 && filename.p[0] == '/')) /* !filepath.IsAbs */
			filename = scanner_filepathJoin(s->dir, filename);
	}

	/* not ported: Go's line/col are 64-bit ints; values that wrapped negative in Go's
	 * int(uint64) conversion (>= 2^63) are truncated to int here. */
	token_File_AddLineColumnInfo(s->file, next, filename, (int)line, (int)col);
}

/* trailingDigits returns the index after the last ':' in text (0 if there is none), the value
 * of the digits after it (as Go's int(n) of strconv.ParseUint(text[i+1:], 10, 0)) and whether
 * they parse. */
static int scanner_trailingDigits(GoStr text, int64_t *n, bool *ok)
{
	*n = 0;
	*ok = false;
	int i = strings_LastIndexByte(text, ':'); /* look from right (Windows filenames may contain ':') */
	if (i < 0)
		return 0; /* no ":" */
	/* i >= 0 */
	GoStr d = gostr_from(text, i + 1);
	uint64_t v = 0;
	bool valid = d.n > 0;
	for (int k = 0; k < d.n && valid; k++) {
		uint8_t c = (uint8_t)d.p[k];
		if (c < '0' || c > '9') {
			valid = false;
			v = 0;
		} else if (v > (UINT64_MAX - (c - '0')) / 10) {
			valid = false; /* ErrRange: n = maxVal */
			v = UINT64_MAX;
		} else {
			v = v * 10 + (c - '0');
		}
	}
	*n = (int64_t)v;
	*ok = valid;
	return i + 1;
}

static rune scanner_lower(rune ch) { return ('a' - 'A') | ch; } /* returns lower-case ch iff ch is ASCII letter */
static bool scanner_isDecimal(rune ch) { return '0' <= ch && ch <= '9'; }
static bool scanner_isHex(rune ch)
{
	return ('0' <= ch && ch <= '9') || ('a' <= scanner_lower(ch) && scanner_lower(ch) <= 'f');
}

static bool scanner_isLetter(rune ch)
{
	return ('a' <= scanner_lower(ch) && scanner_lower(ch) <= 'z') || ch == '_' ||
	       (ch >= utf8_RuneSelf && unicode_IsLetter(ch));
}

static bool scanner_isDigit(rune ch) { return scanner_isDecimal(ch) || (ch >= utf8_RuneSelf && unicode_IsDigit(ch)); }

/* scanIdentifier reads the string of valid identifier characters at s.offset. It must only be
 * called when s.ch is known to be a valid letter. */
static GoStr scanner_Scanner_scanIdentifier(scanner_Scanner *s)
{
	int offs = s->offset;

	/* Optimize for the common case of an ASCII identifier. In case we encounter a non-ASCII
	 * character, fall back on the slower path of calling into s.next(). */
	for (int rdOffset = 0; s->rdOffset + rdOffset < s->src.n; rdOffset++) {
		uint8_t b = (uint8_t)s->src.p[s->rdOffset + rdOffset];
		if (('a' <= b && b <= 'z') || ('A' <= b && b <= 'Z') || b == '_' || ('0' <= b && b <= '9'))
			continue; /* Avoid assigning a rune for the common case of an ascii character. */
		s->rdOffset += rdOffset;
		if (0 < b && b < utf8_RuneSelf) {
			/* Optimization: we've encountered an ASCII character that's not a letter or
			 * number. Avoid the call into s.next() and corresponding set up. (Only possible
			 * because we know that the preceding character is not '\n'.) */
			s->ch = b;
			s->offset = s->rdOffset;
			s->rdOffset++;
			goto exit;
		}
		/* We know that the preceding character is valid for an identifier because
		 * scanIdentifier is only called when s.ch is a letter, so calling s.next() at
		 * s.rdOffset resets the scanner state. */
		scanner_Scanner_next(s);
		while (scanner_isLetter(s->ch) || scanner_isDigit(s->ch))
			scanner_Scanner_next(s);
		goto exit;
	}
	s->offset = s->src.n;
	s->rdOffset = s->src.n;
	s->ch = scanner_eof;

exit:
	return gostr_sub(s->src, offs, s->offset);
}

static int scanner_digitVal(rune ch)
{
	if ('0' <= ch && ch <= '9')
		return (int)(ch - '0');
	if ('a' <= scanner_lower(ch) && scanner_lower(ch) <= 'f')
		return (int)(scanner_lower(ch) - 'a' + 10);
	return 16; /* larger than any legal digit val */
}

/* digits accepts the sequence { digit | '_' }. If base <= 10, digits accepts any decimal digit
 * but records the offset (relative to the source start) of a digit >= base in *invalid, if
 * *invalid < 0. digits returns a bitset describing whether the sequence contained digits (bit 0
 * is set), or separators '_' (bit 1 is set). */
static int scanner_Scanner_digits(scanner_Scanner *s, int base, int *invalid)
{
	int digsep = 0;
	if (base <= 10) {
		rune max = '0' + base;
		while (scanner_isDecimal(s->ch) || s->ch == '_') {
			int ds = 1;
			if (s->ch == '_')
				ds = 2;
			else if (s->ch >= max && *invalid < 0)
				*invalid = s->offset; /* record invalid rune offset */
			digsep |= ds;
			scanner_Scanner_next(s);
		}
	} else {
		while (scanner_isHex(s->ch) || s->ch == '_') {
			int ds = 1;
			if (s->ch == '_')
				ds = 2;
			digsep |= ds;
			scanner_Scanner_next(s);
		}
	}
	return digsep;
}

static GoStr scanner_litname(rune prefix);
static int scanner_invalidSep(GoStr x);

static token_Token scanner_Scanner_scanNumber(scanner_Scanner *s, GoStr *litOut)
{
	int offs = s->offset;
	token_Token tok = token_ILLEGAL;

	int base = 10; /* number base */
	rune prefix = 0; /* one of 0 (decimal), '0' (0-octal), 'x', 'o', or 'b' */
	int digsep = 0; /* bit 0: digit present, bit 1: '_' present */
	int invalid = -1; /* index of invalid digit in literal, or < 0 */

	/* integer part */
	if (s->ch != '.') {
		tok = token_INT;
		if (s->ch == '0') {
			scanner_Scanner_next(s);
			switch (scanner_lower(s->ch)) {
			case 'x':
				scanner_Scanner_next(s);
				base = 16, prefix = 'x';
				break;
			case 'o':
				scanner_Scanner_next(s);
				base = 8, prefix = 'o';
				break;
			case 'b':
				scanner_Scanner_next(s);
				base = 2, prefix = 'b';
				break;
			default:
				base = 8, prefix = '0';
				digsep = 1; /* leading 0 */
			}
		}
		digsep |= scanner_Scanner_digits(s, base, &invalid);
	}

	/* fractional part */
	if (s->ch == '.') {
		tok = token_FLOAT;
		if (prefix == 'o' || prefix == 'b')
			scanner_Scanner_error(s, s->offset,
					      gostr_cat(GOSTR("invalid radix point in "), scanner_litname(prefix)));
		scanner_Scanner_next(s);
		digsep |= scanner_Scanner_digits(s, base, &invalid);
	}

	if ((digsep & 1) == 0)
		scanner_Scanner_error(s, s->offset, gostr_cat(scanner_litname(prefix), GOSTR(" has no digits")));

	/* exponent */
	rune e = scanner_lower(s->ch);
	if (e == 'e' || e == 'p') {
		if (e == 'e' && prefix != 0 && prefix != '0') {
			Buf b = {0};
			scanner_fmtQ(&b, s->ch);
			buf_adds(&b, " exponent requires decimal mantissa");
			scanner_Scanner_error(s, s->offset, gostr_buf(&b));
		} else if (e == 'p' && prefix != 'x') {
			Buf b = {0};
			scanner_fmtQ(&b, s->ch);
			buf_adds(&b, " exponent requires hexadecimal mantissa");
			scanner_Scanner_error(s, s->offset, gostr_buf(&b));
		}
		scanner_Scanner_next(s);
		tok = token_FLOAT;
		if (s->ch == '+' || s->ch == '-')
			scanner_Scanner_next(s);
		int ds = scanner_Scanner_digits(s, 10, NULL); /* base 10: invalid is never touched */
		digsep |= ds;
		if ((ds & 1) == 0)
			scanner_Scanner_error(s, s->offset, GOSTR("exponent has no digits"));
	} else if (prefix == 'x' && tok == token_FLOAT) {
		scanner_Scanner_error(s, s->offset, GOSTR("hexadecimal mantissa requires a 'p' exponent"));
	}

	/* suffix 'i' */
	if (s->ch == 'i') {
		tok = token_IMAG;
		scanner_Scanner_next(s);
	}

	GoStr lit = gostr_sub(s->src, offs, s->offset);
	if (tok == token_INT && invalid >= 0) {
		Buf b = {0};
		buf_adds(&b, "invalid digit ");
		scanner_fmtQ(&b, (uint8_t)lit.p[invalid - offs]);
		buf_adds(&b, " in ");
		buf_addg(&b, scanner_litname(prefix));
		scanner_Scanner_error(s, invalid, gostr_buf(&b));
	}
	if (digsep & 2) {
		int i = scanner_invalidSep(lit);
		if (i >= 0)
			scanner_Scanner_error(s, offs + i, GOSTR("'_' must separate successive digits"));
	}

	*litOut = lit;
	return tok;
}

static GoStr scanner_litname(rune prefix)
{
	switch (prefix) {
	case 'x':
		return GOSTR("hexadecimal literal");
	case 'o':
	case '0':
		return GOSTR("octal literal");
	case 'b':
		return GOSTR("binary literal");
	}
	return GOSTR("decimal literal");
}

/* invalidSep returns the index of the first invalid separator in x, or -1. */
static int scanner_invalidSep(GoStr x)
{
	rune x1 = ' '; /* prefix char, we only care if it's 'x' */
	rune d = '.'; /* digit, one of '_', '0' (a digit), or '.' (anything else) */
	int i = 0;

	/* a prefix counts as a digit */
	if (x.n >= 2 && x.p[0] == '0') {
		x1 = scanner_lower((uint8_t)x.p[1]);
		if (x1 == 'x' || x1 == 'o' || x1 == 'b') {
			d = '0';
			i = 2;
		}
	}

	/* mantissa and exponent */
	for (; i < x.n; i++) {
		rune p = d; /* previous digit */
		d = (uint8_t)x.p[i];
		if (d == '_') {
			if (p != '0')
				return i;
		} else if (scanner_isDecimal(d) || (x1 == 'x' && scanner_isHex(d))) {
			d = '0';
		} else {
			if (p == '_')
				return i - 1;
			d = '.';
		}
	}
	if (d == '_')
		return x.n - 1;

	return -1;
}

/* scanEscape parses an escape sequence where rune is the accepted escaped quote. In case of a
 * syntax error, it stops at the offending character (without consuming it) and returns false.
 * Otherwise it returns true. */
static bool scanner_Scanner_scanEscape(scanner_Scanner *s, rune quote)
{
	int offs = s->offset;

	int n;
	uint32_t base, max;
	switch (s->ch) {
	case 'a':
	case 'b':
	case 'f':
	case 'n':
	case 'r':
	case 't':
	case 'v':
	case '\\':
		scanner_Scanner_next(s);
		return true;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		n = 3, base = 8, max = 255;
		break;
	case 'x':
		scanner_Scanner_next(s);
		n = 2, base = 16, max = 255;
		break;
	case 'u':
		scanner_Scanner_next(s);
		n = 4, base = 16, max = unicode_MaxRune;
		break;
	case 'U':
		scanner_Scanner_next(s);
		n = 8, base = 16, max = unicode_MaxRune;
		break;
	default:
		if (s->ch == quote) {
			scanner_Scanner_next(s);
			return true;
		}
		GoStr msg = GOSTR("unknown escape sequence");
		if (s->ch < 0)
			msg = GOSTR("escape sequence not terminated");
		scanner_Scanner_error(s, offs, msg);
		return false;
	}

	uint32_t x = 0;
	while (n > 0) {
		uint32_t d = (uint32_t)scanner_digitVal(s->ch);
		if (d >= base) {
			Buf b = {0};
			if (s->ch < 0) {
				buf_adds(&b, "escape sequence not terminated");
			} else {
				buf_adds(&b, "illegal character ");
				scanner_fmtSharpU(&b, s->ch);
				buf_adds(&b, " in escape sequence");
			}
			scanner_Scanner_error(s, s->offset, gostr_buf(&b));
			return false;
		}
		x = x * base + d;
		scanner_Scanner_next(s);
		n--;
	}

	if (x > max || (0xD800 <= x && x < 0xE000)) {
		scanner_Scanner_error(s, offs, GOSTR("escape sequence is invalid Unicode code point"));
		return false;
	}

	return true;
}

static GoStr scanner_Scanner_scanRune(scanner_Scanner *s)
{
	/* '\'' opening already consumed */
	int offs = s->offset - 1;

	bool valid = true;
	int n = 0;
	for (;;) {
		rune ch = s->ch;
		if (ch == '\n' || ch < 0) {
			/* only report error if we don't have one already */
			if (valid) {
				scanner_Scanner_error(s, offs, GOSTR("rune literal not terminated"));
				valid = false;
			}
			break;
		}
		scanner_Scanner_next(s);
		if (ch == '\'')
			break;
		n++;
		if (ch == '\\') {
			if (!scanner_Scanner_scanEscape(s, '\''))
				valid = false;
			/* continue to read to closing quote */
		}
	}

	if (valid && n != 1)
		scanner_Scanner_error(s, offs, GOSTR("illegal rune literal"));

	return gostr_sub(s->src, offs, s->offset);
}

static GoStr scanner_Scanner_scanString(scanner_Scanner *s)
{
	/* '"' opening already consumed */
	int offs = s->offset - 1;

	for (;;) {
		rune ch = s->ch;
		if (ch == '\n' || ch < 0) {
			scanner_Scanner_error(s, offs, GOSTR("string literal not terminated"));
			break;
		}
		scanner_Scanner_next(s);
		if (ch == '"')
			break;
		if (ch == '\\')
			scanner_Scanner_scanEscape(s, '"');
	}

	return gostr_sub(s->src, offs, s->offset);
}

static GoStr scanner_stripCR(GoStr b, bool comment)
{
	char *c = xalloc((size_t)b.n + 1);
	int i = 0;
	for (int j = 0; j < b.n; j++) {
		char ch = b.p[j];
		/* In a / *-style comment, don't strip \r from *\r/ (incl. sequences of \r from
		 * *\r\r...\r/) since the resulting * / would terminate the comment too early unless
		 * the \r is immediately following the opening / * in which case it's ok because / * /
		 * is not closed yet (issue #11151). */
		if (ch != '\r' || (comment && i > 2 && c[i - 1] == '*' && j + 1 < b.n && b.p[j + 1] == '/'))
			c[i++] = ch;
	}
	return (GoStr){c, i};
}

static GoStr scanner_Scanner_scanRawString(scanner_Scanner *s, int *rawLen)
{
	/* '`' opening already consumed */
	int offs = s->offset - 1;

	bool hasCR = false;
	for (;;) {
		rune ch = s->ch;
		if (ch < 0) {
			scanner_Scanner_error(s, offs, GOSTR("raw string literal not terminated"));
			break;
		}
		scanner_Scanner_next(s);
		if (ch == '`')
			break;
		if (ch == '\r')
			hasCR = true;
	}

	GoStr lit = gostr_sub(s->src, offs, s->offset);
	*rawLen = lit.n;
	if (hasCR)
		lit = scanner_stripCR(lit, false);

	return lit;
}

static void scanner_Scanner_skipWhitespace(scanner_Scanner *s)
{
	while (s->ch == ' ' || s->ch == '\t' || (s->ch == '\n' && !s->insertSemi) || s->ch == '\r')
		scanner_Scanner_next(s);
}

/* Helper functions for scanning multi-byte tokens such as >> += >>= . Different routines
 * recognize different length tok_i based on matches of ch_i. If a token ends in '=', the result
 * is tok1 or tok3 respectively. Otherwise, the result is tok0 if there was no other matching
 * character, or tok2 if the matching character was ch2. */

static token_Token scanner_Scanner_switch2(scanner_Scanner *s, token_Token tok0, token_Token tok1)
{
	if (s->ch == '=') {
		scanner_Scanner_next(s);
		return tok1;
	}
	return tok0;
}

static token_Token scanner_Scanner_switch3(scanner_Scanner *s, token_Token tok0, token_Token tok1, rune ch2,
					   token_Token tok2)
{
	if (s->ch == '=') {
		scanner_Scanner_next(s);
		return tok1;
	}
	if (s->ch == ch2) {
		scanner_Scanner_next(s);
		return tok2;
	}
	return tok0;
}

static token_Token scanner_Scanner_switch4(scanner_Scanner *s, token_Token tok0, token_Token tok1, rune ch2,
					   token_Token tok2, token_Token tok3)
{
	if (s->ch == '=') {
		scanner_Scanner_next(s);
		return tok1;
	}
	if (s->ch == ch2) {
		scanner_Scanner_next(s);
		if (s->ch == '=') {
			scanner_Scanner_next(s);
			return tok3;
		}
		return tok2;
	}
	return tok0;
}

/* Scan scans the next token and returns the token position, the token, and its literal string
 * if applicable. The source end is indicated by token.EOF.
 *
 * If the returned token is a literal (IDENT, INT, FLOAT, IMAG, CHAR, STRING) or COMMENT, the
 * literal string has the corresponding value. If the returned token is a keyword, the literal
 * string is the keyword. If the returned token is SEMICOLON, the corresponding literal string
 * is ";" if the semicolon was present in the source, and "\n" if the semicolon was inserted
 * because of a newline or at EOF. If the returned token is ILLEGAL, the literal string is the
 * offending character. In all other cases, Scan returns an empty literal string.
 *
 * For more tolerant parsing, Scan will return a valid token if possible even if a syntax error
 * was encountered: a client must check the scanner's ErrorCount or the number of calls of the
 * error handler. Scan adds line information to the file added to the file set with Init. */
static token_Token scanner_Scanner_Scan(scanner_Scanner *s, token_Pos *posOut, GoStr *litOut)
{
	token_Pos pos;
	token_Token tok;
	GoStr lit;
scanAgain:
	lit = (GoStr){0};
	if (token_Pos_IsValid(s->nlPos)) {
		/* Return artificial ';' token after / *...* / comment containing newline, at
		 * position of first newline. */
		*posOut = s->nlPos;
		*litOut = GOSTR("\n");
		s->nlPos = token_NoPos;
		return token_SEMICOLON;
	}

	scanner_Scanner_skipWhitespace(s);

	/* current token start */
	pos = token_File_Pos(s->file, s->offset);

	/* determine token value */
	bool insertSemi = false;
	rune ch = s->ch;
	if (scanner_isLetter(ch)) {
		lit = scanner_Scanner_scanIdentifier(s);
		if (lit.n > 1) {
			/* keywords are longer than one letter - avoid lookup otherwise */
			tok = token_Lookup(lit);
			switch (tok) {
			case token_IDENT:
			case token_BREAK:
			case token_CONTINUE:
			case token_FALLTHROUGH:
			case token_RETURN:
				insertSemi = true;
				break;
			default:
				break;
			}
		} else {
			insertSemi = true;
			tok = token_IDENT;
		}
	} else if (scanner_isDecimal(ch) || (ch == '.' && scanner_isDecimal(scanner_Scanner_peek(s)))) {
		insertSemi = true;
		tok = scanner_Scanner_scanNumber(s, &lit);
	} else {
		scanner_Scanner_next(s); /* always make progress */
		switch (ch) {
		case scanner_eof:
			if (s->insertSemi) {
				s->insertSemi = false; /* EOF consumed */
				*posOut = pos;
				*litOut = GOSTR("\n");
				return token_SEMICOLON;
			}
			tok = token_EOF;
			break;
		case '\n':
			/* we only reach here if s.insertSemi was set in the first place and exited
			 * early from s.skipWhitespace() */
			s->insertSemi = false; /* newline consumed */
			*posOut = pos;
			*litOut = GOSTR("\n");
			return token_SEMICOLON;
		case '"':
			insertSemi = true;
			tok = token_STRING;
			lit = scanner_Scanner_scanString(s);
			s->stringEnd = pos + lit.n;
			break;
		case '\'':
			insertSemi = true;
			tok = token_CHAR;
			lit = scanner_Scanner_scanRune(s);
			break;
		case '`': {
			insertSemi = true;
			tok = token_STRING;
			int rawLen;
			lit = scanner_Scanner_scanRawString(s, &rawLen);
			s->stringEnd = pos + rawLen;
			break;
		}
		case ':':
			tok = scanner_Scanner_switch2(s, token_COLON, token_DEFINE);
			break;
		case '.':
			/* fractions starting with a '.' are handled by outer switch */
			tok = token_PERIOD;
			if (s->ch == '.' && scanner_Scanner_peek(s) == '.') {
				scanner_Scanner_next(s);
				scanner_Scanner_next(s); /* consume last '.' */
				tok = token_ELLIPSIS;
			}
			break;
		case ',':
			tok = token_COMMA;
			break;
		case ';':
			tok = token_SEMICOLON;
			lit = GOSTR(";");
			break;
		case '(':
			tok = token_LPAREN;
			break;
		case ')':
			insertSemi = true;
			tok = token_RPAREN;
			break;
		case '[':
			tok = token_LBRACK;
			break;
		case ']':
			insertSemi = true;
			tok = token_RBRACK;
			break;
		case '{':
			tok = token_LBRACE;
			break;
		case '}':
			insertSemi = true;
			tok = token_RBRACE;
			break;
		case '+':
			tok = scanner_Scanner_switch3(s, token_ADD, token_ADD_ASSIGN, '+', token_INC);
			if (tok == token_INC)
				insertSemi = true;
			break;
		case '-':
			tok = scanner_Scanner_switch3(s, token_SUB, token_SUB_ASSIGN, '-', token_DEC);
			if (tok == token_DEC)
				insertSemi = true;
			break;
		case '*':
			tok = scanner_Scanner_switch2(s, token_MUL, token_MUL_ASSIGN);
			break;
		case '/':
			if (s->ch == '/' || s->ch == '*') {
				/* comment */
				int nlOffset;
				GoStr comment = scanner_Scanner_scanComment(s, &nlOffset);
				if (s->insertSemi && nlOffset != 0) {
					/* For / *...* / containing \n, return COMMENT then artificial
					 * SEMICOLON. */
					s->nlPos = token_File_Pos(s->file, nlOffset);
					s->insertSemi = false;
				} else {
					insertSemi = s->insertSemi; /* preserve insertSemi info */
				}
				if ((s->mode & scanner_ScanComments) == 0) {
					/* skip comment */
					goto scanAgain;
				}
				tok = token_COMMENT;
				lit = comment;
			} else {
				/* division */
				tok = scanner_Scanner_switch2(s, token_QUO, token_QUO_ASSIGN);
			}
			break;
		case '%':
			tok = scanner_Scanner_switch2(s, token_REM, token_REM_ASSIGN);
			break;
		case '^':
			tok = scanner_Scanner_switch2(s, token_XOR, token_XOR_ASSIGN);
			break;
		case '<':
			if (s->ch == '-') {
				scanner_Scanner_next(s);
				tok = token_ARROW;
			} else {
				tok = scanner_Scanner_switch4(s, token_LSS, token_LEQ, '<', token_SHL,
								      token_SHL_ASSIGN);
			}
			break;
		case '>':
			tok = scanner_Scanner_switch4(s, token_GTR, token_GEQ, '>', token_SHR, token_SHR_ASSIGN);
			break;
		case '=':
			tok = scanner_Scanner_switch2(s, token_ASSIGN, token_EQL);
			break;
		case '!':
			tok = scanner_Scanner_switch2(s, token_NOT, token_NEQ);
			break;
		case '&':
			if (s->ch == '^') {
				scanner_Scanner_next(s);
				tok = scanner_Scanner_switch2(s, token_AND_NOT, token_AND_NOT_ASSIGN);
			} else {
				tok = scanner_Scanner_switch3(s, token_AND, token_AND_ASSIGN, '&', token_LAND);
			}
			break;
		case '|':
			tok = scanner_Scanner_switch3(s, token_OR, token_OR_ASSIGN, '|', token_LOR);
			break;
		case '~':
			tok = token_TILDE;
			break;
		default:
			/* next reports unexpected BOMs - don't repeat */
			if (ch != scanner_bom) {
				Buf b = {0};
				/* Report an informative error for U+201[CD] quotation marks, which are
				 * easily introduced via copy and paste. */
				if (ch == 0x201C || ch == 0x201D) {
					buf_adds(&b, "curly quotation mark ");
					scanner_fmtQ(&b, ch);
					buf_adds(&b, " (use neutral '\"')");
				} else {
					buf_adds(&b, "illegal character ");
					scanner_fmtSharpU(&b, ch);
				}
				scanner_Scanner_error(s, token_File_Offset(s->file, pos), gostr_buf(&b));
			}
			insertSemi = s->insertSemi; /* preserve insertSemi info */
			tok = token_ILLEGAL;
			{
				/* string(ch) */
				char *p = xalloc(utf8_UTFMax + 1);
				lit = (GoStr){p, utf8_EncodeRune(p, ch)};
			}
		}
	}
	if ((s->mode & scanner_dontInsertSemis) == 0)
		s->insertSemi = insertSemi;

	*posOut = pos;
	*litOut = lit;
	return tok;
}
