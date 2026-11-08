/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/escape.rs of pbjson-build 0.8.0: escapes identifiers that collide with Rust
 * keywords. The list differs from prost-build's ident.rs (no `gen`, `extern` is suffixed). */

static const char *const pbjson_raw_keywords[] = {
	/* 2015 strict keywords. */
	"as", "break", "const", "continue", "else", "enum", "false", "fn", "for", "if", "impl", "in",
	"let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "static", "struct",
	"trait", "true", "type", "unsafe", "use", "where", "while",
	/* 2018 strict keywords. */
	"dyn",
	/* 2015 reserved keywords. */
	"abstract", "become", "box", "do", "final", "macro", "override", "priv", "typeof", "unsized",
	"virtual", "yield",
	/* 2018 reserved keywords. */
	"async", "await", "try",
};

/* escape_ident: a raw identifier for keywords; self, super, extern and crate (not supported as
 * raw identifiers) get an underscore suffix. */
static char *escape_ident(const char *ident)
{
	for (size_t i = 0; i < sizeof pbjson_raw_keywords / sizeof *pbjson_raw_keywords; i++)
		if (str_eq(ident, pbjson_raw_keywords[i]))
			return xcat("r#", ident);
	if (str_eq(ident, "self") || str_eq(ident, "super") || str_eq(ident, "extern") || str_eq(ident, "crate"))
		return xcat(ident, "_");
	return (char *)ident;
}

/* escape_type: Self is not supported as a raw identifier and gets an underscore suffix. */
static char *escape_type(const char *ident) { return str_eq(ident, "Self") ? xcat(ident, "_") : (char *)ident; }

static char *snake_ident(const char *s) { return escape_ident(heck_snake(ascii_identifier(s))); }
static char *upper_camel_type(const char *s) { return escape_type(heck_upper_camel(ascii_identifier(s))); }
