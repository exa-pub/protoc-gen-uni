/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/defaults.rs: proto `default_value` strings as Rust expressions,
 * the bare open-enum default and unescape_c_escape_string. Rust's `str::parse` of the integer and
 * float types is reproduced (an optional sign, digits; floats: decimal or exponent forms, inf,
 * infinity, nan, case-insensitive). */

static bool rust_parse_i64(const char *s, int64_t lo, int64_t hi, int64_t *out)
{
	bool neg = false;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';
	if (!*s)
		return false;
	uint64_t v = 0, lim = neg ? (uint64_t)(-(lo + 1)) + 1 : (uint64_t)hi;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return false;
		unsigned d = (unsigned)(*s - '0');
		if (v > (lim - d) / 10)
			return false;
		v = v * 10 + d;
	}
	*out = neg ? (int64_t)(0 - v) : (int64_t)v;
	return true;
}

static bool rust_parse_u64(const char *s, uint64_t hi, uint64_t *out)
{
	if (*s == '+')
		s++;
	if (!*s)
		return false;
	uint64_t v = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return false;
		unsigned d = (unsigned)(*s - '0');
		if (v > (hi - d) / 10)
			return false;
		v = v * 10 + d;
	}
	*out = v;
	return true;
}

/* f32/f64::from_str grammar: [+-] (inf | infinity | nan | digits [. digits] [e [+-] digits] | . digits ...) */
static bool rust_float_syntax(const char *s)
{
	if (*s == '+' || *s == '-')
		s++;
	char low[16];
	size_t n = strlen(s);
	if (n < sizeof low) {
		for (size_t i = 0; i <= n; i++)
			low[i] = lower(s[i]);
		if (str_eq(low, "inf") || str_eq(low, "infinity") || str_eq(low, "nan"))
			return true;
	}
	bool digits = false;
	while (*s >= '0' && *s <= '9')
		s++, digits = true;
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9')
			s++, digits = true;
	}
	if (!digits)
		return false;
	if (*s == 'e' || *s == 'E') {
		s++;
		if (*s == '+' || *s == '-')
			s++;
		if (!(*s >= '0' && *s <= '9'))
			return false;
		while (*s >= '0' && *s <= '9')
			s++;
	}
	return !*s;
}

static PpNode *parse_float_default(const char *s, bool f32)
{
	if (str_eq(s, "inf") || str_eq(s, "infinity"))
		return pp_e(f32 ? "f32::INFINITY" : "f64::INFINITY");
	if (str_eq(s, "-inf") || str_eq(s, "-infinity"))
		return pp_e(f32 ? "f32::NEG_INFINITY" : "f64::NEG_INFINITY");
	if (str_eq(s, "nan"))
		return pp_e(f32 ? "f32::NAN" : "f64::NAN");
	if (!rust_float_syntax(s))
		fail("%s", CODEGEN_ERROR("invalid %s default: '%s'", f32 ? "f32" : "f64", s));
	double d = strtod(s, NULL);
	if (f32)
		return pp_e_lit(pp_lit(PP_LIT_FLOAT, pm2_f32_suffixed(strtof(s, NULL))));
	return pp_e_lit(pp_lit(PP_LIT_FLOAT, pm2_f64_suffixed(d)));
}

static Str unescape_c_escape_string(const char *s)
{
	Buf b = {0};
	size_t len = strlen(s), p = 0;
	while (p < len) {
		if (s[p] != '\\') {
			buf_addc(&b, s[p++]);
			continue;
		}
		if (++p == len)
			fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): ends with '\\'", s));
		char c = s[p];
		const char *simple = strchr("abfnrtv\\?'\"", c);
		if (simple && c) {
			static const char vals[] = "\a\b\f\n\r\t\v\\?'\"";
			buf_addc(&b, vals[simple - "abfnrtv\\?'\""]);
			p++;
		} else if (c >= '0' && c <= '7') {
			unsigned oct = 0;
			for (int i = 0; i < 3 && p < len && s[p] >= '0' && s[p] <= '7'; i++)
				oct = oct * 8 + (unsigned)(s[p++] - '0');
			if (oct > 255)
				fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): octal escape \\%d%d%d out of range "
							 "(max \\377)", s, oct >> 6, (oct >> 3) & 7, oct & 7));
			buf_addc(&b, (char)oct);
		} else if (c == 'x' || c == 'X') {
			if (++p == len)
				fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): incomplete hex value", s));
			unsigned hex = 0;
			bool any = false;
			for (; p < len; p++) {
				char h = s[p];
				int d = h >= '0' && h <= '9' ? h - '0' : (h | 0x20) >= 'a' && (h | 0x20) <= 'f' ? (h | 0x20) - 'a' + 10 : -1;
				if (d < 0)
					break;
				any = true;
				hex = hex > 0xFFFFFFF ? 0xFFFFFFFF : hex * 16 + (unsigned)d;
			}
			if (!any)
				fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): invalid hex value", s));
			if (hex > 255)
				fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): hex escape \\x%x out of range "
							 "(max \\xff)", s, hex));
			buf_addc(&b, (char)hex);
		} else
			fail("%s", CODEGEN_ERROR("invalid c-escaped default binary value (%s): invalid escape", s));
	}
	return (Str){b.p ? b.p : "", b.n};
}

static int effective_type(const PbMsg *field, Feat features);
static bool is_required_field(const PbMsg *field, Feat features);

/* parse_default_value (string_repr is always the default String) */
static PpNode *parse_default_value(const PbMsg *field, const char *pkg, Feat f, int nesting)
{
	const char *d = pb_has(field, FD_DEFAULT_VALUE) ? pb_str(field, FD_DEFAULT_VALUE) : "";
	if (!*d)
		return NULL;
	if (f.presence != PROTO_PRESENCE_EXPLICIT && f.presence != PROTO_PRESENCE_LEGACY_REQUIRED)
		return NULL;
	const char *name = pb_has(field, FD_NAME) ? dname(field) : "<unknown>";
	int64_t i;
	uint64_t u;
	switch (fd_type(field)) {
	case PT_BOOL:
		if (str_eq(d, "true") || str_eq(d, "false"))
			return pp_e_lit_bool(d[0] == 't');
		fail("%s", CODEGEN_ERROR("field '%s': invalid bool default: '%s'", name, d));
	case PT_INT32: case PT_SINT32: case PT_SFIXED32:
		if (!rust_parse_i64(d, INT32_MIN, INT32_MAX, &i))
			fail("%s", CODEGEN_ERROR("field '%s': invalid i32 default: '%s'", name, d));
		return lit_i32(i);
	case PT_UINT32: case PT_FIXED32:
		if (!rust_parse_u64(d, UINT32_MAX, &u))
			fail("%s", CODEGEN_ERROR("field '%s': invalid u32 default: '%s'", name, d));
		return lit_u32(u);
	case PT_INT64: case PT_SINT64: case PT_SFIXED64:
		if (!rust_parse_i64(d, INT64_MIN, INT64_MAX, &i))
			fail("%s", CODEGEN_ERROR("field '%s': invalid i64 default: '%s'", name, d));
		return lit_suffixed(i, "i64");
	case PT_UINT64: case PT_FIXED64:
		if (!rust_parse_u64(d, UINT64_MAX, &u))
			fail("%s", CODEGEN_ERROR("field '%s': invalid u64 default: '%s'", name, d));
		return lit_usuffixed(u, "u64");
	case PT_FLOAT:
		return parse_float_default(d, true);
	case PT_DOUBLE:
		return parse_float_default(d, false);
	case PT_STRING: {
		Str v = pb_bytes(field, FD_DEFAULT_VALUE);
		PpNode *from = e_call1("::buffa::alloc::string::String::from", lit_strn(v));
		if (effective_type(field, f) == PT_BYTES)
			return e_meth0(from, "into_bytes");
		return from;
	}
	case PT_BYTES: {
		Str v = unescape_c_escape_string(d);
		PpNode *m = pp_macro(pp_path("::buffa::alloc::vec"), PP_DELIM_BRACKET);
		for (size_t k = 0; k < v.n; k++) {
			if (k)
				pp_push(m, pp_tt_punct(',', false));
			pp_push(m, pp_tt_lit(pm2_uint((unsigned char)v.p[k], "u8")));
		}
		return pp_e_macro(m);
	}
	case PT_ENUM: {
		const char *tn = fd_type_name(field);
		if (!tn)
			fail("missing required descriptor field: field.type_name");
		const char *path = ctx_rust_type_relative(tn, pkg, nesting);
		if (!path)
			fail("%s", CODEGEN_ERROR("enum type '%s' not found in descriptor set", tn));
		PpNode *e = pp_e(xcat(rust_path(path), "::", make_field_ident(d)));
		return f.enum_type == PROTO_ENUM_TYPE_OPEN ? e_call1("::buffa::EnumValue::Known", e) : e;
	}
	default:
		return NULL;
	}
}

/* open_enum_bare_default_value */
static PpNode *open_enum_bare_default_value(const PbMsg *field, const char *pkg, Feat f, int nesting)
{
	if (fd_type(field) != PT_ENUM || f.enum_type != PROTO_ENUM_TYPE_OPEN || !is_required_field(field, f))
		return NULL;
	PpNode *e = parse_default_value(field, pkg, f, nesting);
	if (e)
		return e;
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	int *first = map_get(&cx.enum_first, tn);
	if (!first || !*first)
		return NULL;
	const char *path = ctx_rust_type_relative(tn, pkg, nesting);
	if (!path)
		fail("%s", CODEGEN_ERROR("enum type '%s' not found in descriptor set", tn));
	return e_call1("::buffa::EnumValue::Known",
		       pp_e_call(e_qpath(pp_ty(rust_path(path)), "::core::default::Default", "default"), NULL));
}
