/* Port of src/extern_paths.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost,
 * Apache-2.0): protobuf paths provided by other crates (extern_path, and prost-types for the
 * well-known types). */

typedef struct {
	Map paths;   /* proto path -> Rust path */
	Map lengths; /* length_key of the length of a proto path -> 1 */
} ExternPaths;

/* The decimal digits of n in key[24]. */
static const char *length_key(char *key, size_t n)
{
	char *p = key + 23;
	*p = 0;
	do
		*--p = (char)('0' + n % 10);
	while (n /= 10);
	return p;
}

static const char *validate_proto_path(const char *path)
{
	if (*path != '.')
		return xcat("Protobuf paths must be fully qualified (begin with a leading '.'): ", path);
	Vec parts = str_split(path, '.');
	for (int i = 1; i < parts.n; i++)
		if (!*(const char *)parts.v[i])
			return xcat("invalid fully-qualified Protobuf path: ", path);
	return NULL;
}

static const char *extern_paths_insert(ExternPaths *e, const char *proto_path, const char *rust_path)
{
	const char *err = validate_proto_path(proto_path);
	if (err)
		return err;
	if (map_has(&e->paths, proto_path))
		return xcat("duplicate extern Protobuf path: ", proto_path);
	map_put(&e->paths, proto_path, (void *)rust_path);
	char key[24];
	map_put(&e->lengths, xstrdup(length_key(key, strlen(proto_path))), (void *)1);
	return NULL;
}

/* ExternPaths::new: paths is a list of (proto path, Rust path) pairs. Returns NULL or the error. */
static const char *extern_paths_new(ExternPaths *e, const Vec *paths, bool prost_types)
{
	for (int i = 0; i + 1 < paths->n; i += 2) {
		const char *err = extern_paths_insert(e, paths->v[i], paths->v[i + 1]);
		if (err)
			return err;
	}
	if (!prost_types)
		return NULL;
	static const char *const wkt[][2] = {
		{".google.protobuf", "::prost_types"},
		{".google.protobuf.BoolValue", "bool"},
		{".google.protobuf.BytesValue", "::prost::alloc::vec::Vec<u8>"},
		{".google.protobuf.DoubleValue", "f64"},
		{".google.protobuf.Empty", "()"},
		{".google.protobuf.FloatValue", "f32"},
		{".google.protobuf.Int32Value", "i32"},
		{".google.protobuf.Int64Value", "i64"},
		{".google.protobuf.StringValue", "::prost::alloc::string::String"},
		{".google.protobuf.UInt32Value", "u32"},
		{".google.protobuf.UInt64Value", "u64"},
	};
	for (size_t i = 0; i < sizeof wkt / sizeof *wkt; i++) {
		const char *err = extern_paths_insert(e, wkt[i][0], wkt[i][1]);
		if (err)
			return err;
	}
	return NULL;
}

/* assert_eq!(".", &pb_ident[..1]): protoc always gives fully qualified identifiers. */
static void assert_fully_qualified(const char *pb_ident)
{
	if (!*pb_ident)
		fail("byte index 1 is out of bounds of ``");
	if (*pb_ident != '.')
		fail("assertion `left == right` failed\n  left: \".\"\n right: \"%c...\"", *pb_ident);
}

static char *extern_paths_resolve_ident(const ExternPaths *e, const char *pb_ident)
{
	assert_fully_qualified(pb_ident);
	const char *rust_path = map_get(&e->paths, pb_ident);
	if (rust_path)
		return xstrdup(rust_path);
	/* The prefixes before each dot, longest first; only those of the length of some path can
	 * match, and they are looked up in one copy cut short in place. */
	char *prefix = NULL;
	for (size_t idx = strlen(pb_ident); idx-- > 0;) {
		if (pb_ident[idx] != '.')
			continue;
		char key[24];
		if (!map_has(&e->lengths, length_key(key, idx)))
			continue;
		if (!prefix)
			prefix = xstrdup(pb_ident);
		prefix[idx] = 0;
		rust_path = map_get(&e->paths, prefix);
		if (!rust_path)
			continue;
		Vec segments = str_split(pb_ident + idx + 1, '.');
		const char *ident_type = to_upper_camel(segments.v[segments.n - 1]);
		Buf b = {0};
		Vec rust = {0};
		for (const char *p = rust_path;;) {
			const char *sep = strstr(p, "::");
			vec_push(&rust, xstrndup(p, sep ? (size_t)(sep - p) : strlen(p)));
			if (!sep)
				break;
			p = sep + 2;
		}
		for (int i = 0; i < segments.n - 1; i++)
			vec_push(&rust, segments.v[i]);
		for (int i = 0; i < rust.n; i++) {
			const char *seg = rust.v[i];
			/* A leading `crate` is the keyword, not escaped into a raw identifier. */
			buf_adds(&b, i == 0 && str_eq(seg, "crate") ? seg : to_snake(seg));
			buf_adds(&b, "::");
		}
		buf_adds(&b, ident_type);
		return xstrdup(buf_str(&b));
	}
	return NULL;
}
