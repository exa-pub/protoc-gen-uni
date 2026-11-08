/* Package net/url, ported from go1.25.0 src/net/url/url.go (Parse, URL.String and their
 * helpers; errors as text). Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see
 * LICENSE in Go). The same in go1.27.1 for these functions. */

enum { url_encodePath = 1, url_encodePathSegment, url_encodeHost, url_encodeZone, url_encodeUserPassword, url_encodeQueryComponent, url_encodeFragment };

static bool url_ishex(char c) { return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F'); }

static uint8_t url_unhex(char c)
{
	if ('0' <= c && c <= '9')
		return (uint8_t)(c - '0');
	if ('a' <= c && c <= 'f')
		return (uint8_t)(c - 'a' + 10);
	return (uint8_t)(c - 'A' + 10);
}

/* Return true if the specified character should be escaped when appearing in a URL string,
 * according to RFC 3986. */
static bool url_shouldEscape(uint8_t c, int mode)
{
	/* §2.3 Unreserved characters (alphanum) */
	if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))
		return false;
	if (mode == url_encodeHost || mode == url_encodeZone) {
		switch (c) {
		case '!': case '$': case '&': case '\'': case '(': case ')': case '*': case '+': case ',': case ';': case '=':
		case ':': case '[': case ']': case '<': case '>': case '"':
			return false;
		}
	}
	switch (c) {
	case '-': case '_': case '.': case '~': /* §2.3 Unreserved characters (mark) */
		return false;
	case '$': case '&': case '+': case ',': case '/': case ':': case ';': case '=': case '?': case '@': /* §2.2 Reserved characters (reserved) */
		switch (mode) {
		case url_encodePath: /* §3.3 */
			return c == '?';
		case url_encodePathSegment: /* §3.3 */
			return c == '/' || c == ';' || c == ',' || c == '?';
		case url_encodeUserPassword: /* §3.2.1 */
			return c == '@' || c == '/' || c == '?' || c == ':';
		case url_encodeQueryComponent: /* §3.4 */
			return true;
		case url_encodeFragment: /* §4.1 */
			return false;
		}
	}
	if (mode == url_encodeFragment) {
		switch (c) {
		case '!': case '(': case ')': case '*':
			return false;
		}
	}
	/* Everything else must be escaped. */
	return true;
}

static GoStr gostr_cat3(GoStr a, GoStr b, GoStr c) { return gostr_cat(gostr_cat(a, b), c); }

static const char *url_escapeError(GoStr s) { return gostr_cstr(gostr_cat(GOSTR("invalid URL escape "), strconv_Quote(s))); }

/* unescape unescapes a string; the mode specifies which section of the URL string is being
 * unescaped. Returns NULL or the error. */
static const char *url_unescape(GoStr s, int mode, GoStr *out)
{
	/* Count %, check that they're well-formed. */
	int n = 0;
	bool hasPlus = false;
	for (int i = 0; i < s.n;) {
		switch (s.p[i]) {
		case '%':
			n++;
			if (i + 2 >= s.n || !url_ishex(s.p[i + 1]) || !url_ishex(s.p[i + 2])) {
				GoStr t = gostr_sub(s, i, s.n);
				if (t.n > 3)
					t.n = 3;
				return url_escapeError(t);
			}
			/* Per https://tools.ietf.org/html/rfc3986#page-21 in the host component
			 * %-encoding can only be used for non-ASCII bytes. But
			 * https://tools.ietf.org/html/rfc6874#section-2 introduces %25 being allowed
			 * to escape a percent sign in IPv6 scoped-address literals. Yay. */
			if (mode == url_encodeHost && url_unhex(s.p[i + 1]) < 8 && !(s.p[i + 1] == '2' && s.p[i + 2] == '5'))
				return url_escapeError(gostr_sub(s, i, i + 3));
			if (mode == url_encodeZone) {
				/* RFC 6874 says basically "anything goes" for zone identifiers and that
				 * even non-ASCII can be redundantly escaped, but it seems prudent to
				 * restrict %-escaped bytes here to those that are valid host name bytes
				 * in their unescaped form. That is, you can use escaping in the zone
				 * identifier but not to introduce bytes you couldn't just write directly.
				 * But Windows puts spaces here! Yay. */
				uint8_t v = (uint8_t)(url_unhex(s.p[i + 1]) << 4 | url_unhex(s.p[i + 2]));
				if (!(s.p[i + 1] == '2' && s.p[i + 2] == '5') && v != ' ' && url_shouldEscape(v, url_encodeHost))
					return url_escapeError(gostr_sub(s, i, i + 3));
			}
			i += 3;
			break;
		case '+':
			hasPlus = mode == url_encodeQueryComponent;
			i++;
			break;
		default:
			if ((mode == url_encodeHost || mode == url_encodeZone) && (uint8_t)s.p[i] < 0x80 && url_shouldEscape((uint8_t)s.p[i], mode))
				return gostr_cstr(gostr_cat3(GOSTR("invalid character "), strconv_Quote(gostr_sub(s, i, i + 1)), GOSTR(" in host name")));
			i++;
		}
	}

	if (n == 0 && !hasPlus) {
		*out = s;
		return NULL;
	}

	Buf t = {0};
	for (int i = 0; i < s.n; i++) {
		switch (s.p[i]) {
		case '%':
			buf_addc(&t, (char)(url_unhex(s.p[i + 1]) << 4 | url_unhex(s.p[i + 2])));
			i += 2;
			break;
		case '+':
			buf_addc(&t, mode == url_encodeQueryComponent ? ' ' : '+');
			break;
		default:
			buf_addc(&t, s.p[i]);
		}
	}
	*out = gostr(t.p ? t.p : "", (int)t.n);
	return NULL;
}

static GoStr url_escape(GoStr s, int mode)
{
	static const char upperhex[] = "0123456789ABCDEF";
	int spaceCount = 0, hexCount = 0;
	for (int i = 0; i < s.n; i++) {
		uint8_t c = (uint8_t)s.p[i];
		if (url_shouldEscape(c, mode)) {
			if (c == ' ' && mode == url_encodeQueryComponent)
				spaceCount++;
			else
				hexCount++;
		}
	}
	if (spaceCount == 0 && hexCount == 0)
		return s;
	Buf t = {0};
	for (int i = 0; i < s.n; i++) {
		uint8_t c = (uint8_t)s.p[i];
		if (c == ' ' && mode == url_encodeQueryComponent) {
			buf_addc(&t, '+');
		} else if (url_shouldEscape(c, mode)) {
			buf_addc(&t, '%');
			buf_addc(&t, upperhex[c >> 4]);
			buf_addc(&t, upperhex[c & 15]);
		} else {
			buf_addc(&t, (char)c);
		}
	}
	return gostr(t.p, (int)t.n);
}

static bool url_gostr_eq(GoStr a, GoStr b) { return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, (size_t)a.n) == 0); }

static int url_index_byte(GoStr s, char c)
{
	for (int i = 0; i < s.n; i++)
		if (s.p[i] == c)
			return i;
	return -1;
}

static int url_last_index_byte(GoStr s, char c)
{
	for (int i = s.n - 1; i >= 0; i--)
		if (s.p[i] == c)
			return i;
	return -1;
}

static bool url_validOptionalPort(GoStr port)
{
	if (port.n == 0)
		return true;
	if (port.p[0] != ':')
		return false;
	/* for _, b := range port[1:]: runes; a non-ASCII rune is not a digit either */
	for (int i = 1; i < port.n; i++)
		if (port.p[i] < '0' || port.p[i] > '9')
			return false;
	return true;
}

/* parseHost parses host as an authority without user information. That is, as host[:port]. */
static const char *url_parseHost(GoStr host, GoStr *out)
{
	if (host.n > 0 && host.p[0] == '[') {
		/* Parse an IP-Literal in RFC 3986 and RFC 6874. E.g., "[fe80::1]",
		 * "[fe80::1%25en0]", "[fe80::1]:80". */
		int i = url_last_index_byte(host, ']');
		if (i < 0)
			return "missing ']' in host";
		GoStr colonPort = gostr_sub(host, i + 1, host.n);
		if (!url_validOptionalPort(colonPort))
			return gostr_cstr(gostr_cat3(GOSTR("invalid port "), strconv_Quote(colonPort), GOSTR(" after host")));

		/* RFC 6874 defines that %25 (%-encoded percent) introduces the zone identifier,
		 * and the zone identifier can use basically any %-encoding it likes. That's
		 * different from the host, which can only %-encode non-ASCII bytes. We do impose
		 * some restrictions on the zone, to avoid stupidity like newlines. */
		int zone = -1;
		for (int k = 0; k + 3 <= i; k++)
			if (host.p[k] == '%' && host.p[k + 1] == '2' && host.p[k + 2] == '5') {
				zone = k;
				break;
			}
		if (zone >= 0) {
			GoStr host1, host2, host3;
			const char *err;
			if ((err = url_unescape(gostr_sub(host, 0, zone), url_encodeHost, &host1)))
				return err;
			if ((err = url_unescape(gostr_sub(host, zone, i), url_encodeZone, &host2)))
				return err;
			if ((err = url_unescape(gostr_sub(host, i, host.n), url_encodeHost, &host3)))
				return err;
			*out = gostr_cat3(host1, host2, host3);
			return NULL;
		}
	} else {
		int i = url_last_index_byte(host, ':');
		if (i != -1) {
			GoStr colonPort = gostr_sub(host, i, host.n);
			if (!url_validOptionalPort(colonPort))
				return gostr_cstr(gostr_cat3(GOSTR("invalid port "), strconv_Quote(colonPort), GOSTR(" after host")));
		}
	}
	return url_unescape(host, url_encodeHost, out);
}

static bool url_validUserinfo(GoStr s)
{
	for (int i = 0; i < s.n; i++) {
		char r = s.p[i];
		if (('A' <= r && r <= 'Z') || ('a' <= r && r <= 'z') || ('0' <= r && r <= '9'))
			continue;
		switch (r) {
		case '-': case '.': case '_': case ':': case '~': case '!': case '$': case '&': case '\'': case '(': case ')':
		case '*': case '+': case ',': case ';': case '=': case '%': case '@':
			continue;
		default:
			return false;
		}
	}
	return true;
}

static const char *url_parseAuthority(GoStr authority, go_std_url *u)
{
	int i = url_last_index_byte(authority, '@');
	const char *err;
	GoStr host;
	if (i < 0)
		err = url_parseHost(authority, &host);
	else
		err = url_parseHost(gostr_sub(authority, i + 1, authority.n), &host);
	if (err)
		return err;
	u->Host = (Str){host.p, (size_t)host.n};
	if (i < 0)
		return NULL;
	GoStr userinfo = gostr_sub(authority, 0, i);
	if (!url_validUserinfo(userinfo))
		return "net/url: invalid userinfo";
	u->HasUser = true;
	int colon = url_index_byte(userinfo, ':');
	if (colon < 0) {
		GoStr un;
		if ((err = url_unescape(userinfo, url_encodeUserPassword, &un)))
			return err;
		u->Username = (Str){un.p, (size_t)un.n};
	} else {
		GoStr un, pw;
		if ((err = url_unescape(gostr_sub(userinfo, 0, colon), url_encodeUserPassword, &un)))
			return err;
		if ((err = url_unescape(gostr_sub(userinfo, colon + 1, userinfo.n), url_encodeUserPassword, &pw)))
			return err;
		u->Username = (Str){un.p, (size_t)un.n};
		u->Password = (Str){pw.p, (size_t)pw.n};
		u->PasswordSet = true;
	}
	return NULL;
}

/* setPath sets the Path and RawPath fields of the URL based on the provided escaped path p. */
static const char *url_setPath(go_std_url *u, GoStr p)
{
	GoStr path;
	const char *err = url_unescape(p, url_encodePath, &path);
	if (err)
		return err;
	u->Path = (Str){path.p, (size_t)path.n};
	GoStr escp = url_escape(path, url_encodePath);
	u->RawPath = url_gostr_eq(p, escp) ? (Str){"", 0} : (Str){p.p, (size_t)p.n};
	return NULL;
}

static const char *url_parse(GoStr rawURL, go_std_url *u)
{
	for (int i = 0; i < rawURL.n; i++)
		if ((uint8_t)rawURL.p[i] < ' ' || rawURL.p[i] == 0x7f)
			return "net/url: invalid control character in URL";
	if (rawURL.n == 1 && rawURL.p[0] == '*') {
		u->Path = (Str){"*", 1};
		return NULL;
	}
	/* Split off possible leading "http:", "mailto:", etc. Cannot contain escaped
	 * characters. */
	GoStr rest = rawURL;
	for (int i = 0; i < rawURL.n; i++) {
		char c = rawURL.p[i];
		if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {
			continue;
		} else if (('0' <= c && c <= '9') || c == '+' || c == '-' || c == '.') {
			if (i == 0)
				break;
		} else if (c == ':') {
			if (i == 0)
				return "missing protocol scheme";
			Buf b = {0};
			for (int k = 0; k < i; k++)
				buf_addc(&b, (char)(rawURL.p[k] >= 'A' && rawURL.p[k] <= 'Z' ? rawURL.p[k] + 32 : rawURL.p[k]));
			u->Scheme = buf_view(&b);
			rest = gostr_sub(rawURL, i + 1, rawURL.n);
			break;
		} else {
			break;
		}
	}

	int nq = 0;
	for (int i = 0; i < rest.n; i++)
		nq += rest.p[i] == '?';
	if (rest.n > 0 && rest.p[rest.n - 1] == '?' && nq == 1) {
		u->ForceQuery = true;
		rest.n--;
	} else {
		int q = url_index_byte(rest, '?');
		if (q >= 0) {
			u->RawQuery = (Str){rest.p + q + 1, (size_t)(rest.n - q - 1)};
			rest.n = q;
		}
	}

	bool slash = rest.n > 0 && rest.p[0] == '/';
	if (!slash) {
		if (u->Scheme.n) {
			/* We consider rootless paths per RFC 3986 as opaque. */
			u->Opaque = (Str){rest.p, (size_t)rest.n};
			return NULL;
		}
		/* Avoid confusion with malformed schemes, like cache_object:foo/bar. See
		 * golang.org/issue/16822. RFC 3986, §3.3: In addition, a URI reference (Section
		 * 4.1) may be a relative-path reference, in which case the first path segment
		 * cannot contain a colon (":") character. */
		int s = url_index_byte(rest, '/');
		GoStr segment = s >= 0 ? gostr_sub(rest, 0, s) : rest;
		if (url_index_byte(segment, ':') >= 0)
			return "first path segment in URL cannot contain colon";
	}

	bool triple = rest.n >= 3 && rest.p[0] == '/' && rest.p[1] == '/' && rest.p[2] == '/';
	bool dbl = rest.n >= 2 && rest.p[0] == '/' && rest.p[1] == '/';
	if ((u->Scheme.n || !triple) && dbl) {
		GoStr authority = gostr_sub(rest, 2, rest.n);
		rest = GOSTR("");
		int i = url_index_byte(authority, '/');
		if (i >= 0) {
			rest = gostr_sub(authority, i, authority.n);
			authority = gostr_sub(authority, 0, i);
		}
		const char *err = url_parseAuthority(authority, u);
		if (err)
			return err;
	} else if (u->Scheme.n && slash) {
		/* OmitHost is set to true when rawURL has an empty host (authority). See
		 * golang.org/issue/46059. */
		u->OmitHost = true;
	}

	/* Set Path and, optionally, RawPath. RawPath is a hint of the encoding of Path. We
	 * don't want to set it if the default escaping of Path is equivalent, to help make
	 * sure that people don't rely on it in general. */
	return url_setPath(u, rest);
}

static bool url_validEncoded(GoStr s, int mode)
{
	for (int i = 0; i < s.n; i++) {
		/* RFC 3986, Appendix A. pchar = unreserved / pct-encoded / sub-delims / ":" / "@".
		 * shouldEscape is not quite compliant with the RFC, so we check the sub-delims
		 * ourselves and let shouldEscape handle the others. */
		switch (s.p[i]) {
		case '!': case '$': case '&': case '\'': case '(': case ')': case '*': case '+': case ',': case ';': case '=':
		case ':': case '@':
		case '[': case ']':
		case '%':
			break;
		default:
			if (url_shouldEscape((uint8_t)s.p[i], mode))
				return false;
		}
	}
	return true;
}

static GoStr url_gs(Str s) { return gostr(s.p ? s.p : "", (int)s.n); }

/* EscapedPath returns the escaped form of u.Path. */
static GoStr url_EscapedPath(const go_std_url *u)
{
	GoStr raw = url_gs(u->RawPath), path = url_gs(u->Path);
	if (raw.n && url_validEncoded(raw, url_encodePath)) {
		GoStr p;
		if (!url_unescape(raw, url_encodePath, &p) && url_gostr_eq(p, path))
			return raw;
	}
	if (path.n == 1 && path.p[0] == '*')
		return GOSTR("*"); /* don't escape (Issue 11202) */
	return url_escape(path, url_encodePath);
}

/* EscapedFragment returns the escaped form of u.Fragment. */
static GoStr url_EscapedFragment(const go_std_url *u)
{
	GoStr raw = url_gs(u->RawFragment), frag = url_gs(u->Fragment);
	if (raw.n && url_validEncoded(raw, url_encodeFragment)) {
		GoStr f;
		if (!url_unescape(raw, url_encodeFragment, &f) && url_gostr_eq(f, frag))
			return raw;
	}
	return url_escape(frag, url_encodeFragment);
}

const char *go_std_url_parse(Str raw, go_std_url *u)
{
	memset(u, 0, sizeof *u);
	GoStr s = url_gs(raw), frag = GOSTR("");
	int h = url_index_byte(s, '#');
	if (h >= 0) {
		frag = gostr_sub(s, h + 1, s.n);
		s = gostr_sub(s, 0, h);
	}
	const char *err = url_parse(s, u);
	/* the *url.Error text: parse "<url>": <err> */
	if (err)
		return gostr_cstr(gostr_cat3(GOSTR("parse "), strconv_Quote(s), gostr_cat(GOSTR(": "), gostr_c(err))));
	if (frag.n == 0)
		return NULL;
	GoStr f;
	if ((err = url_unescape(frag, url_encodeFragment, &f))) {
		memset(u, 0, sizeof *u);
		return gostr_cstr(gostr_cat3(GOSTR("parse "), strconv_Quote(url_gs(raw)), gostr_cat(GOSTR(": "), gostr_c(err))));
	}
	u->Fragment = (Str){f.p, (size_t)f.n};
	GoStr escf = url_escape(f, url_encodeFragment);
	u->RawFragment = url_gostr_eq(frag, escf) ? (Str){"", 0} : (Str){frag.p, (size_t)frag.n};
	return NULL;
}

Str go_std_url_string(const go_std_url *u)
{
	Buf buf = {0};
	if (u->Scheme.n) {
		buf_add_str(&buf, u->Scheme);
		buf_addc(&buf, ':');
	}
	if (u->Opaque.n) {
		buf_add_str(&buf, u->Opaque);
	} else {
		if (u->Scheme.n || u->Host.n || u->HasUser) {
			if (u->OmitHost && !u->Host.n && !u->HasUser) {
				/* Omit empty host */
			} else {
				if (u->Host.n || u->Path.n || u->HasUser)
					buf_adds(&buf, "//");
				if (u->HasUser) {
					GoStr un = url_escape(url_gs(u->Username), url_encodeUserPassword);
					buf_add(&buf, un.p, (size_t)un.n);
					if (u->PasswordSet) {
						buf_addc(&buf, ':');
						GoStr pw = url_escape(url_gs(u->Password), url_encodeUserPassword);
						buf_add(&buf, pw.p, (size_t)pw.n);
					}
					buf_addc(&buf, '@');
				}
				if (u->Host.n) {
					GoStr h = url_escape(url_gs(u->Host), url_encodeHost);
					buf_add(&buf, h.p, (size_t)h.n);
				}
			}
		}
		GoStr path = url_EscapedPath(u);
		if (path.n && path.p[0] != '/' && u->Host.n)
			buf_addc(&buf, '/');
		if (buf.n == 0) {
			/* RFC 3986 §4.2. A path segment that contains a colon character (e.g.,
			 * "this:that") cannot be used as the first segment of a relative-path
			 * reference, as it would be mistaken for a scheme name. Such a path segment
			 * must be preceded by a dot-segment (e.g., "./this:that") to make a
			 * relative-path reference. */
			int s = url_index_byte(path, '/');
			GoStr segment = s >= 0 ? gostr_sub(path, 0, s) : path;
			if (url_index_byte(segment, ':') >= 0)
				buf_adds(&buf, "./");
		}
		buf_add(&buf, path.p, (size_t)path.n);
	}
	if (u->ForceQuery || u->RawQuery.n) {
		buf_addc(&buf, '?');
		buf_add_str(&buf, u->RawQuery);
	}
	if (u->Fragment.n) {
		buf_addc(&buf, '#');
		GoStr f = url_EscapedFragment(u);
		buf_add(&buf, f.p, (size_t)f.n);
	}
	return (Str){buf.p ? buf.p : "", buf.n};
}
