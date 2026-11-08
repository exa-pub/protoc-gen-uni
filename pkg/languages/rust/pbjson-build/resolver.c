/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/resolver.rs of pbjson-build 0.8.0: Rust paths of types relative to a package. */

typedef struct {
	const Vec *extern_types; /* pairs: proto path, rust path */
	const TypePath *package;
	bool retain_enum_prefix;
	Map *shared; /* "<package id>|<package id>" -> 1 + shared parts of two packages; NULL: no cache */
} Resolver;

/* The number of leading TypeNames two packages share. Cached per pair, so that types of
 * packages with a long common prefix do not compare it again for every reference. */
static int shared_parts(const Resolver *r, const Package *a, const Package *b)
{
	if (a == b)
		return a->n;
	char *key = NULL;
	if (r->shared) {
		key = xfmt("%d|%d", a->id, b->id);
		void *v = map_get(r->shared, key);
		if (v)
			return (int)((intptr_t)v - 1);
	}
	int shared = 0;
	while (shared < a->n && shared < b->n && str_eq(a->parts[shared], b->parts[shared]))
		shared++;
	if (r->shared)
		map_put(r->shared, key, (void *)(intptr_t)(shared + 1));
	return shared;
}

/* Resolver::rust_type */
static char *rust_type(const Resolver *r, const TypePath *path)
{
	Buf ret = {0};
	int skip;
	/* resolve_extern: the last of the longest matches */
	int match_len = 0, match_idx = -1;
	for (int i = 0; i + 1 < r->extern_types->n; i += 2) {
		int m = prefix_match(path, r->extern_types->v[i]);
		if (m >= 0 && m >= match_len) {
			match_idx = i;
			match_len = m;
		}
	}
	if (match_idx >= 0) {
		buf_adds(&ret, r->extern_types->v[match_idx + 1]);
		if (match_len != path->n)
			buf_adds(&ret, "::");
		skip = match_len;
	} else {
		/* the package of r is a prefix of the path only if the packages share it */
		int shared = shared_parts(r, r->package->pkg, path->pkg);
		while (shared == path->np && shared < r->package->np && shared < path->n &&
		       str_eq(r->package->pkg->parts[shared], tp_part(path, shared)))
			shared++;
		for (int i = shared; i < r->package->np; i++)
			buf_adds(&ret, "super::");
		skip = shared;
	}
	for (int i = skip; i < path->n; i++) {
		if (i + 1 < path->n) {
			buf_adds(&ret, snake_ident(tp_part(path, i)));
			buf_adds(&ret, "::");
		} else {
			buf_adds(&ret, upper_camel_type(tp_part(path, i)));
		}
	}
	return xstrdup(buf_str(&ret));
}

/* Resolver::rust_variant */
static char *rust_variant(const Resolver *r, const TypePath *enumeration, const char *variant)
{
	char *v = heck_upper_camel(ascii_identifier(variant));
	if (r->retain_enum_prefix)
		return v;
	const char *prefix = upper_camel_type(tp_part(enumeration, enumeration->n - 1));
	const char *stripped = has_prefix(v, prefix) ? v + strlen(prefix) : v;
	/* "Foo" should not be stripped from "Foobar". char::is_uppercase; ASCII only (see heck.h) */
	return *stripped >= 'A' && *stripped <= 'Z' ? xstrdup(stripped) : v;
}
