/* Not a port: the inputs upstream accepts that the port does not implement, and the requests
 * that upstream's decoder (prost) rejects but pkg/pb accepts. They fail with
 * "unsupported by protoc-gen-uni: ..." instead of giving a different output (README.md). */

#define UNSUPPORTED "unsupported by protoc-gen-uni: "

/* Strict UTF-8 (str::from_utf8): no overlong forms, surrogates or values above U+10FFFF. */
COLD static bool valid_utf8(Str s)
{
	const uint8_t *p = (const uint8_t *)s.p, *end = p + s.n;
	while (p < end) {
		uint8_t c = *p++;
		if (c < 0x80)
			continue;
		int n = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
		if (n < 0 || end - p < n)
			return false;
		uint8_t lo = 0x80, hi = 0xBF;
		if (c == 0xE0)
			lo = 0xA0;
		else if (c == 0xED)
			hi = 0x9F;
		else if (c == 0xF0)
			lo = 0x90;
		else if (c == 0xF4)
			hi = 0x8F;
		if (p[0] < lo || p[0] > hi)
			return false;
		for (int i = 1; i < n; i++)
			if ((p[i] & 0xC0) != 0x80)
				return false;
		p += n;
	}
	return true;
}

COLD static char *debug_str(Str s)
{
	Buf b = {0};
	rust_str_debug(&b, s);
	return xstrdup(buf_str(&b));
}

/* prost decodes every string field of the request as a Rust String (an error unless it is
 * UTF-8), reads enum fields as open i32 values, and fails on a known field with another wire
 * type; pkg/pb drops unknown values of closed enums and keeps mismatching fields as unknown
 * fields. Strings are read as C strings here, so a NUL byte would cut them short. */
COLD static void check_message(const PbMsg *m)
{
	const PbMsgDef *def = m->def;
	for (int i = 0; i < def->n; i++) {
		const PbField *f = &def->f[i];
		const PbSlot *slot = &m->slots[i];
		for (int j = 0; j < slot->n; j++) {
			if (f->type == PT_MESSAGE) {
				check_message(slot->v[j].m);
			} else if (f->type == PT_STRING) {
				Str s = slot->v[j].s;
				if (!valid_utf8(s))
					fail(UNSUPPORTED "invalid UTF-8 in a string of the request (upstream fails to decode it)");
				if (memchr(s.p, 0, s.n))
					fail(UNSUPPORTED "NUL byte in a string of the request: %s", debug_str(s));
			}
		}
	}
	PbReader r = pb_unknown_fields(m);
	PbRawField raw;
	while (pb_next_field(&r, &raw)) {
		for (int i = 0; i < def->n; i++) {
			const PbField *f = &def->f[i];
			if (f->num != raw.num || (f->type == PT_ENUM && raw.wt == WT_VARINT))
				continue;
			fail(UNSUPPORTED "field %d of a message in the request has wire type %d (upstream fails to "
				     "decode it)",
			     (int)raw.num, raw.wt);
		}
	}
}

/* heck and the Rust keyword rules are ported for ASCII identifiers only. */
static const char *ascii_identifier(const char *s)
{
	for (const char *p = s; *p; p++)
		if ((unsigned char)*p >= 0x80)
			fail(UNSUPPORTED "non-ASCII identifier %s", debug_str(str_c(s)));
	return s;
}

static char *to_snake(const char *s) { return prost_to_snake(ascii_identifier(s)); }
static char *to_upper_camel(const char *s) { return prost_to_upper_camel(ascii_identifier(s)); }

/* Module::from_protobuf_package_name converts the packages with to_snake. */
COLD static void check_packages(const PbMsg *req)
{
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++)
		ascii_identifier(pb_str(pb_msg_at(req, CGR_PROTO_FILE, i), FDP_PACKAGE));
}

/* rust_str_debug escapes like <str as Debug> for ASCII only (Unicode printable and grapheme
 * extend tables are not ported). */
static void debug_ascii(Buf *b, const char *s, const char *what)
{
	for (const char *p = s; *p; p++)
		if ((unsigned char)*p >= 0x80)
			fail(UNSUPPORTED "non-ASCII %s %s", what, s);
	rust_str_debug(b, str_c(s));
}
