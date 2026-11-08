/* Ports of prost-build 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0): the Config fields
 * protoc-gen-prost sets (src/config.rs) and Context (src/context.rs), the global information of
 * the code generation. */

/* Config: only what protoc-gen-prost can configure. Every map_type matcher is BTreeMap and every
 * bytes_type matcher is Bytes; the other path maps carry attributes, or nothing. */
typedef struct {
	PathMap map_type, bytes_type, type_attributes, message_attributes, enum_attributes, field_attributes,
		boxed, disable_comments, skip_debug;
	bool prost_types, strip_enum_prefix, enable_type_names;
	Vec extern_paths; /* proto path, Rust path, proto path, ... */
} Config;

typedef struct {
	const Config *config;
	MessageGraph graph;
	ExternPaths extern_paths;
	Map copy_memo, eq_memo; /* message name -> 1 (false) or 2 (true) */
} Context;

/* FieldDescriptorProto accessors with the prost-types defaults. */
static int field_type(const PbMsg *f) { return (int)pb_int(f, FD_TYPE, PT_DOUBLE); }
static int field_label(const PbMsg *f) { return (int)pb_int(f, FD_LABEL, LABEL_OPTIONAL); }
static const char *field_name(const PbMsg *f) { return pb_str(f, FD_NAME); }
static const char *field_type_name(const PbMsg *f) { return pb_str(f, FD_TYPE_NAME); }

static char *resolve_extern_ident(Context *c, const char *pb_ident)
{
	return extern_paths_resolve_ident(&c->extern_paths, pb_ident);
}

static bool is_btree_map(Context *c, const char *fq_message_name, const char *field)
{
	return path_map_get_first_field(&c->config->map_type, fq_message_name, field) != NULL;
}

static bool is_bytes_bytes(Context *c, const char *fq_message_name, const char *field)
{
	return path_map_get_first_field(&c->config->bytes_type, fq_message_name, field) != NULL;
}

static bool should_box_impl(Context *c, const char *fq_message_name, const char *oneof, const PbMsg *field)
{
	/* Repeated fields are stored in Vec, therefore already heap allocated. */
	if (field_label(field) == LABEL_REPEATED)
		return false;
	int t = field_type(field);
	if ((t == PT_MESSAGE || t == PT_GROUP) && message_graph_is_nested(&c->graph, field_type_name(field), fq_message_name))
		return true;
	/* get_first_field of "<fq_message_name>[.<oneof>]" */
	FullPath p = full_path(fq_message_name, oneof, field_name(field));
	return path_map_get_first(&c->config->boxed, &p) != NULL;
}

static bool should_box_message_field(Context *c, const char *fq_message_name, const PbMsg *field)
{
	return should_box_impl(c, fq_message_name, NULL, field);
}

static bool should_box_oneof_field(Context *c, const char *fq_message_name, const char *oneof, const PbMsg *field)
{
	return should_box_impl(c, fq_message_name, oneof, field);
}

/* can_field_derive_copy / can_field_derive_eq without the recursion into the field's message:
 * 0 or 1, or 2 when that depends on can_message_derive_*(*sub). */
static int field_derive(Context *c, const char *fq_message_name, const PbMsg *field, bool eq, const char **sub)
{
	assert_fully_qualified(fq_message_name);
	int t = field_type(field);
	if (eq) {
		if (t == PT_MESSAGE) {
			if (field_label(field) == LABEL_REPEATED ||
			    message_graph_is_nested(&c->graph, field_type_name(field), fq_message_name))
				return 0;
			*sub = field_type_name(field);
			return 2;
		}
		return t != PT_FLOAT && t != PT_DOUBLE && t != PT_GROUP;
	}
	if (field_label(field) == LABEL_REPEATED)
		return 0;
	if (t == PT_MESSAGE) {
		/* nested and boxed messages cannot derive Copy */
		if (message_graph_is_nested(&c->graph, field_type_name(field), fq_message_name))
			return 0;
		if (path_map_get_first_field(&c->config->boxed, fq_message_name, field_name(field)))
			return 0;
		*sub = field_type_name(field);
		return 2;
	}
	return t != PT_STRING && t != PT_BYTES && t != PT_GROUP;
}

typedef struct {
	const char *name;
	const PbMsg *msg;
	int field;
} DeriveFrame;

/* can_message_derive_copy / can_message_derive_eq. Upstream recurses through the message fields
 * (is_nested cuts every cycle) and recomputes the result of every message each time, which is
 * exponential for some message graphs; this walks the same fields with an explicit stack and
 * keeps each result (memo: 1 false, 2 true, 3 being computed). */
static bool can_message_derive(Context *c, const char *fq_message_name, bool eq)
{
	assert_fully_qualified(fq_message_name);
	Map *memo = eq ? &c->eq_memo : &c->copy_memo;
	intptr_t cached = (intptr_t)map_get(memo, fq_message_name);
	if (cached)
		return cached == 2;
	DeriveFrame *stack = NULL;
	int sp = 0, cap = 0;
	const char *push = fq_message_name;
	for (;;) {
		if (push) {
			const PbMsg *msg = message_graph_get_message(&c->graph, push);
			if (!msg)
				fail("called `Option::unwrap()` on a `None` value");
			if (sp == cap) {
				int ncap = cap ? cap * 2 : 16;
				stack = xrealloc(stack, sizeof *stack * (size_t)cap, sizeof *stack * (size_t)ncap);
				cap = ncap;
			}
			stack[sp++] = (DeriveFrame){push, msg, 0};
			map_put(memo, push, (void *)(intptr_t)3);
			push = NULL;
		}
		DeriveFrame *f = &stack[sp - 1];
		int r = 1;
		if (f->field < pb_count(f->msg, DP_FIELD)) {
			const char *sub = NULL;
			r = field_derive(c, f->name, pb_msg_at(f->msg, DP_FIELD, f->field), eq, &sub);
			if (r == 2) {
				cached = (intptr_t)map_get(memo, sub);
				if (cached == 3) /* unreachable: is_nested cuts cycles */
					fail("thread 'main' has overflowed its stack");
				if (!cached) {
					push = sub;
					continue;
				}
				r = cached == 2;
			}
			if (r) {
				f->field++;
				continue;
			}
		}
		/* f is done: r is its result */
		map_put(memo, f->name, (void *)(intptr_t)(r ? 2 : 1));
		if (--sp == 0)
			return r;
	}
}

static bool can_message_derive_copy(Context *c, const char *fq_message_name)
{
	return can_message_derive(c, fq_message_name, false);
}

static bool can_message_derive_eq(Context *c, const char *fq_message_name)
{
	return can_message_derive(c, fq_message_name, true);
}

static bool can_field_derive(Context *c, const char *fq_message_name, const PbMsg *field, bool eq)
{
	const char *sub = NULL;
	int r = field_derive(c, fq_message_name, field, eq, &sub);
	return r == 2 ? can_message_derive(c, sub, eq) : r;
}

static bool can_field_derive_copy(Context *c, const char *fq_message_name, const PbMsg *field)
{
	return can_field_derive(c, fq_message_name, field, false);
}

static bool can_field_derive_eq(Context *c, const char *fq_message_name, const PbMsg *field)
{
	return can_field_derive(c, fq_message_name, field, true);
}

static bool should_disable_comments(Context *c, const char *fq_message_name, const char *field)
{
	if (field)
		return path_map_get_first_field(&c->config->disable_comments, fq_message_name, field) != NULL;
	return path_map_any(&c->config->disable_comments, fq_message_name);
}

static bool should_skip_debug(Context *c, const char *fq_message_name)
{
	return path_map_any(&c->config->skip_debug, fq_message_name);
}
