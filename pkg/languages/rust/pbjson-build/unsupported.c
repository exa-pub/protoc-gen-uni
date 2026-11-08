/* Not a port: the inputs pbjson-build accepts that the port does not implement. They fail with
 * "unsupported by protoc-gen-uni: ..." instead of giving a different output. protoc and buf
 * never produce them. */

/* heck and the keyword escaping are ported for ASCII identifiers only (heck.h): upstream maps
 * the case of non-ASCII letters with Unicode rules. */
static const char *ascii_identifier(const char *s)
{
	for (const char *p = s; *p; p++)
		if ((unsigned char)*p >= 0x80)
			fail("unsupported by protoc-gen-uni: non-ASCII identifier %s (upstream converts its case "
			     "with Unicode rules)",
			     quote_json(str_c(s)));
	return s;
}
