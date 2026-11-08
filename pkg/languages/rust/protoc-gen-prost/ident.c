/* Port of src/ident.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0). */
#include "heck.h"
#include "prost.h"

#include <string.h>

static const char *const raw_keywords[] = {
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
	/* 2024 reserved keywords. */
	"gen",
};

/* Not supported as raw identifiers, therefore suffixed with an underscore. */
static const char *const suffixed[] = {"_", "super", "self", "Self", "extern", "crate"};

char *prost_sanitize_identifier(const char *s)
{
	for (size_t i = 0; i < sizeof raw_keywords / sizeof *raw_keywords; i++)
		if (str_eq(s, raw_keywords[i]))
			return xcat("r#", s);
	for (size_t i = 0; i < sizeof suffixed / sizeof *suffixed; i++)
		if (str_eq(s, suffixed[i]))
			return xcat(s, "_");
	/* char::is_numeric; ASCII only (see heck.h) */
	if (*s >= '0' && *s <= '9')
		return xcat("_", s);
	return xstrdup(s);
}

char *prost_to_snake(const char *s) { return prost_sanitize_identifier(heck_snake(s)); }

char *prost_to_upper_camel(const char *s) { return prost_sanitize_identifier(heck_upper_camel(s)); }

char *prost_strip_enum_prefix(const char *prefix, const char *name)
{
	const char *stripped = has_prefix(name, prefix) ? name + strlen(prefix) : name;
	/* If the next char after the stripped prefix is not uppercase, it was not a true prefix: "Foo"
	 * is not stripped from "Foobar". */
	if (!(*stripped >= 'A' && *stripped <= 'Z'))
		stripped = name;
	return prost_sanitize_identifier(stripped);
}
