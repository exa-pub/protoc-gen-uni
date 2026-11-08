/* Port of protoc_plugin 25.1.0 lib/string_escape.dart.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* quoted: a single-quoted Dart literal of input (per rune). */
static char *quoted(Str input)
{
	Buf b = {0};
	buf_addc(&b, '\'');
	for (size_t i = 0; i < input.n; i++) {
		unsigned char c = (unsigned char)input.p[i];
		switch (c) {
		case '\\': buf_adds(&b, "\\\\"); break;
		case '\t': buf_adds(&b, "\\t"); break;
		case '\v': buf_adds(&b, "\\v"); break;
		case '\b': buf_adds(&b, "\\b"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '\'': buf_adds(&b, "\\'"); break;
		case '$': buf_adds(&b, "\\$"); break;
		default:
			if (c < 32)
				buf_fmt(&b, "\\x%02x", c);
			else
				buf_addc(&b, (char)c);
		}
	}
	buf_addc(&b, '\'');
	return (char *)buf_str(&b);
}
