/* Port of src/resolver.rs of protoc-gen-tonic 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0). */

typedef struct Node Node;
struct Node {
	const char *extern_path; /* NULL: None */
	Map children;            /* Node * */
};

typedef struct {
	Node extern_root;
	bool compile_well_known_types;
} Resolver;

/* The panic of assert_eq!: both sides formatted with <str as Debug>. */
NORETURN COLD static void assert_leading_dot_fail(char c)
{
	Buf b = {0};
	rust_str_debug(&b, (Str){&c, 1});
	fail("assertion `left == right` failed\n  left: \".\"\n right: %s", buf_str(&b));
}

/* assert_eq!(".", &fq_proto_path[..1]) */
static void assert_leading_dot(const char *fq_proto_path)
{
	if (!*fq_proto_path || (unsigned char)*fq_proto_path >= 0x80)
		rust_str_slice_end_fail(str_c(fq_proto_path), 1);
	if (*fq_proto_path != '.')
		assert_leading_dot_fail(*fq_proto_path);
}

/* path.split_once('.').unwrap_or((path, "")): *next is the part before the dot. */
static const char *split_once(const char *path, char **next)
{
	const char *dot = strchr(path, '.');
	*next = dot ? xstrndup(path, (size_t)(dot - path)) : (char *)path;
	return dot ? dot + 1 : "";
}

static void node_insert(Node *self, const char *fq_proto_path, const char *extern_path)
{
	assert_leading_dot(fq_proto_path);
	const char *path = fq_proto_path + 1;
	Node *current = self;
	while (*path) {
		char *next;
		path = split_once(path, &next);
		Node *child = map_get(&current->children, next);
		if (!child) {
			child = xalloc(sizeof *child);
			map_put(&current->children, next, child);
		}
		current = child;
	}
	current->extern_path = extern_path;
}

static const char *node_resolve_extern(const Node *self, const char *fq_proto_path)
{
	assert_leading_dot(fq_proto_path);
	const char *path = fq_proto_path + 1;
	const Node *current = self;
	while (*path) {
		char *next;
		const char *remaining = split_once(path, &next);
		Node *node = map_get(&current->children, next);
		if (node) {
			current = node;
			path = remaining;
			continue;
		}
		const char *prefix = current->extern_path;
		if (!prefix)
			return NULL;
		const char *dot = strrchr(path, '.');
		const char *package = dot ? xstrndup(path, (size_t)(dot - path)) : "";
		const char *type_name = dot ? dot + 1 : path;
		ProstModule *module = prost_module_from_package(package);
		Buf b = {0};
		buf_adds(&b, prefix);
		for (int i = 0; i < module->n; i++) {
			buf_adds(&b, "::");
			buf_adds(&b, module->parts[i]);
		}
		buf_adds(&b, "::");
		buf_adds(&b, to_upper_camel(type_name));
		return xstrdup(buf_str(&b));
	}
	return current->extern_path;
}

/* extern_path: pairs (proto path, Rust path). */
static void resolver_init(Resolver *r, const Vec *extern_path, bool compile_well_known_types)
{
	if (!compile_well_known_types) {
		static const char *const wkt[] = {
			".google.protobuf", "::prost_types",
			".google.protobuf.BoolValue", "bool",
			".google.protobuf.BytesValue", "::prost::alloc::vec::Vec<u8>",
			".google.protobuf.DoubleValue", "f64",
			".google.protobuf.Empty", "()",
			".google.protobuf.FloatValue", "f32",
			".google.protobuf.Int32Value", "i32",
			".google.protobuf.Int64Value", "i64",
			".google.protobuf.StringValue", "::prost::alloc::string::String",
			".google.protobuf.UInt32Value", "u32",
			".google.protobuf.UInt64Value", "u64",
		};
		for (size_t i = 0; i < sizeof wkt / sizeof *wkt; i += 2)
			node_insert(&r->extern_root, wkt[i], wkt[i + 1]);
	}
	for (int i = 0; i + 1 < extern_path->n; i += 2)
		node_insert(&r->extern_root, extern_path->v[i], extern_path->v[i + 1]);
	r->compile_well_known_types = compile_well_known_types;
}

static void difference(const ProstModule *left, const ProstModule *right, int *down, int *prefix)
{
	*prefix = 0;
	for (int i = 0;; i++) {
		if (i < left->n && i < right->n && str_eq(left->parts[i], right->parts[i])) {
			++*prefix;
			continue;
		}
		*down = i < left->n ? left->n - i : 0;
		return;
	}
}

static char *resolve_ident(const Resolver *r, const ProstModule *from, const char *to_fq)
{
	const char *proto_ident = node_resolve_extern(&r->extern_root, to_fq);
	if (proto_ident)
		return (char *)proto_ident;
	const char *dot = strrchr(to_fq, '.');
	ProstModule *to = prost_module_from_package(xstrndup(to_fq, (size_t)(dot - to_fq)));
	int down, prefix;
	difference(from, to, &down, &prefix);
	Vec parts = {0};
	for (int i = 0; i < down; i++)
		vec_push(&parts, "super");
	for (int i = prefix; i < to->n; i++)
		vec_push(&parts, to->parts[i]);
	vec_push(&parts, to_upper_camel(dot + 1));
	return str_join(&parts, "::");
}
