/* Copyright 2026 The Connect Authors. Licensed under the Apache License, Version 2.0.
 *
 * Port of connectrpc-codegen 0.9.0 src/codegen.rs: the `generate` entry point of the protoc plugin
 * (option parsing, generate_services) and the ConnectRPC service code. Upstream builds `quote!`
 * token streams and prints them with prettyplease; the port builds the syn nodes those tokens
 * parse to (pkg/languages/rust/formatters/prettyplease). Errors (anyhow) fail() with their text.
 * The unified path (generate_files, buffa message types in the same files) is library API that
 * the plugin does not reach and is not ported. */

/* ---- quote! glue: literals and shorthands for the nodes the quoted tokens parse to ---- */

static PpNode *lit_str(const char *s) { return pp_e_lit(pp_lit_str_repr(pm2_string(str_c(s)), str_c(s))); }
static PpNode *doc_attr(const char *text) { return pp_attr_name_value(pp_path("doc"), lit_str(text), false); }
static void doc(PpNode *node, const char *text) { pp_push_attr(node, doc_attr(text)); }

static PpNode *attr_src(const char *path, const char *tokens)
{
	PpNode *a = pp_attr_list(pp_path(path), PP_DELIM_PAREN, false);
	pp_tt_push_src(a, tokens);
	return a;
}

/* doc_attrs: one `#[doc = " line"]` per line of text (blank lines stay empty). */
static void doc_attrs(PpNode *node, const char *text)
{
	Str *lines;
	int n = rust_lines(str_c(text), &lines);
	for (int i = 0; i < n; i++)
		doc(node, lines[i].n ? xcat(" ", xstrndup(lines[i].p, lines[i].n)) : "");
}

/* rust_path_to_tokens makes an Ident of every `::` segment of the unescaped path: Ident::new_raw
 * for a keyword that can be raw, else Ident::new, which panics on a segment that is not an
 * identifier (an extern_path target such as `crate::`, `my-crate` or `crate::r#foo`). Non-ASCII
 * identifier chars would need Unicode XID tables. */
static void check_path_segment(const char *path, const char *seg, size_t n)
{
	bool ok = n && !(seg[0] >= '0' && seg[0] <= '9');
	for (size_t i = 0; ok && i < n; i++) {
		unsigned char c = (unsigned char)seg[i];
		if (c >= 0x80)
			fail("unsupported by protoc-gen-uni: non-ASCII identifier character in Rust path `%s` "
			     "(upstream checks it against Unicode XID)",
			     path);
		ok = c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
	}
	if (!ok)
		fail("Unsupported: Rust path `%s`: proc-macro2 panics on `%s`, which is not a valid Ident", path,
		     xstrndup(seg, n));
}

static PpNode *path_node(const char *path)
{
	const char *p = has_prefix(path, "::") ? path + 2 : path;
	for (;;) {
		const char *e = strstr(p, "::");
		check_path_segment(path, p, e ? (size_t)(e - p) : strlen(p));
		if (!e)
			break;
		p = e + 2;
	}
	return pp_path(bc_rust_path(path));
}

static PpNode *ty(const char *path) { return pp_ty(path); }
static PpNode *ty1(const char *path, PpNode *arg) { return pp_ty_gen(path, arg, NULL); }
static PpNode *ty2(const char *path, PpNode *a, PpNode *b) { return pp_ty_gen(path, a, b, NULL); }
static PpNode *ty_life(PpNode *path, const char *lifetime)
{
	return pp_ty_path(pp_path_args(path, pp_angle(pp_lifetime(lifetime), NULL)));
}
static PpNode *e_call(const char *f, PpNode *a, PpNode *b) { return pp_e_call(pp_e(f), a, b, NULL); }
static PpNode *e_ref(PpNode *e) { return pp_e_ref(false, e); }
static PpNode *self_f(const char *field) { return pp_e_field(pp_e("self"), field); }
static PpNode *arc_clone(PpNode *e) { return e_call("::std::sync::Arc::clone", e_ref(e), NULL); }
static PpNode *s_let(const char *name, PpNode *init) { return pp_s_local(pp_p_ident(name, false, false), init); }
static PpNode *s_expr(PpNode *e) { return pp_s_expr(e, false); }
static PpNode *arg(const char *name, PpNode *t) { return pp_p_type(pp_p_ident(name, false, false), t); }
static PpNode *bound(PpNode *path) { return pp_trait_bound(path, false); }
static PpNode *bound_s(const char *path) { return bound(pp_path(path)); }

/* `path::<args>` as an expression path (turbofish on the last segment) */
static PpNode *e_turbofish(const char *path, PpNode *args) { return pp_e_path(pp_path_args(pp_path(path), args)); }

/* `<path><T>::rest` as an expression: ::connectrpc::ServiceRequest::<T>::from_parts */
static PpNode *e_generic_assoc(const char *path, PpNode *t, const char *rest)
{
	PpNode *p = pp_path_args(pp_path(path), pp_angle(t, NULL));
	return pp_e_path(pp_path_push(p, rest, NULL));
}

/* impl ::std::future::Future<Output = out> + Send */
static PpNode *ty_future(PpNode *out)
{
	PpNode *output = pp_node(PP_ASSOC_TYPE);
	output->text = "Output";
	output->b = out;
	PpNode *future = pp_path_args(pp_path("::std::future::Future"), pp_angle(output, NULL));
	return pp_ty_impl(pp_bounds(bound(future), bound_s("Send"), NULL));
}

/* impl ::connectrpc::Encodable<out> + Send + use<captures> */
static PpNode *ty_impl_encodable(PpNode *out, PpNode *captures)
{
	PpNode *enc = pp_path_args(pp_path("::connectrpc::Encodable"), pp_angle(out, NULL));
	return pp_ty_impl(pp_bounds(bound(enc), bound_s("Send"), captures, NULL));
}

/* fn name(&self, inputs...) -> ret; receiver set by the caller */
static PpNode *sig_self(const char *name, PpNode *ret)
{
	PpNode *sig = pp_sig(name, ret);
	pp_push(sig, pp_receiver(true, false, NULL));
	return sig;
}

static PpNode *generics_param(const char *name, const char *bound_path)
{
	PpNode *g = pp_generics();
	pp_push(g, pp_type_param(name, bound_path ? pp_bounds(bound_s(bound_path), NULL) : NULL, NULL));
	return g;
}

/* ---- Options ---- */

typedef struct {
	BcConfig buffa;
	bool gate_client_feature;
	const char *client_feature_name;
	bool all_messages; /* EncodableImpls::AllMessages */
} Options;

/* Options::client_feature_name */
COLD static const char *options_client_feature_name(const Options *o)
{
	Str t = rust_trim(str_c(o->client_feature_name));
	char *name = xstrndup(t.p, t.n);
	if (!*name) {
		if (o->gate_client_feature)
			fail("client feature name must not be empty");
		return "client";
	}
	if (!bc_is_valid_feature_name(name)) {
		Buf b = {0};
		rust_str_debug(&b, str_c(name));
		fail("client feature name %s is not a valid Cargo feature name (must start with an alphanumeric or `_` "
		     "and contain only alphanumerics, `_`, `-`, `+`, `.`)",
		     buf_str(&b));
	}
	return name;
}

/* ---- TypeResolver (require_extern: the generate_services path) ---- */

static void check_extern_coverage(const char *fqn, const char *path_prefix)
{
	if (!has_prefix(path_prefix, "::") && !has_prefix(path_prefix, "crate::"))
		fail("type %s is not covered by any extern_path mapping. Add extern_path=.=<your_buffa_module> (e.g. "
		     "extern_path=.=crate::proto) to the plugin opts.",
		     fqn);
}

static NORETURN void unresolved_type_error(const char *fqn)
{
	fail("type %s not found in descriptor set (missing proto import?)", fqn);
}

static const char *resolve_path(const char *fqn, const char *package)
{
	const char *path = bc_rust_type_relative(fqn, package, 0);
	if (!path)
		unresolved_type_error(fqn);
	check_extern_coverage(fqn, path);
	return path;
}

static PpNode *rust_type_path(const char *fqn, const char *package) { return path_node(resolve_path(fqn, package)); }
static PpNode *rust_type(const char *fqn, const char *package) { return pp_ty_path(rust_type_path(fqn, package)); }

/* rust_view_type: `<to-package>::__buffa::view::<within>View`, as a path (callers add `<'_>`). */
static PpNode *rust_view_path(const char *fqn, const char *package)
{
	const char *to_package, *within;
	if (!bc_rust_type_relative_split(fqn, package, 0, &to_package, &within))
		unresolved_type_error(fqn);
	check_extern_coverage(fqn, to_package);
	const char *prefix = *to_package ? xcat(to_package, "::" BC_SENTINEL_MOD "::view") : BC_SENTINEL_MOD "::view";
	return path_node(xcat(prefix, "::", within, "View"));
}

static PpNode *rust_view_type(const char *fqn, const char *package, const char *lifetime)
{
	return ty_life(rust_view_path(fqn, package), lifetime);
}

/* bare_type_name: the last segment of a proto FQN */
static const char *bare_type_name(const char *fqn)
{
	const char *s = fqn[0] == '.' ? fqn + 1 : fqn;
	const char *dot = strrchr(s, '.');
	return dot ? dot + 1 : s;
}

/* ---- descriptors ---- */

static const char *method_input(const PbMsg *m) { return pb_str(m, MDP_INPUT_TYPE); }
static const char *method_output(const PbMsg *m) { return pb_str(m, MDP_OUTPUT_TYPE); }
static bool client_streaming(const PbMsg *m) { return pb_int(m, MDP_CLIENT_STREAMING, 0) != 0; }
static bool server_streaming(const PbMsg *m) { return pb_int(m, MDP_SERVER_STREAMING, 0) != 0; }
enum { MOPT_IDEMPOTENCY_LEVEL = 34, IDEMPOTENCY_NO_SIDE_EFFECTS = 1, IDEMPOTENCY_IDEMPOTENT = 2 };
static int idempotency_level(const PbMsg *m)
{
	const PbMsg *o = pb_msg(m, MDP_OPTIONS);
	return pb_has(o, MOPT_IDEMPOTENCY_LEVEL) ? (int)pb_int(o, MOPT_IDEMPOTENCY_LEVEL, 0) : -1;
}
static bool is_no_side_effects(const PbMsg *m) { return idempotency_level(m) == IDEMPOTENCY_NO_SIDE_EFFECTS; }

static char *snake(const char *s) { return heck_snake(s); }
static char *upper_camel(const char *s) { return heck_upper_camel(s); }
/* str::to_uppercase (identifiers are ASCII here) */
static char *to_uppercase(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 32);
	return r;
}
static char *method_snake(const char *name) { return bc_make_field_ident(snake(name)); }

/* ---- BatchState ---- */

typedef struct {
	Map encodable_seen;    /* proto FQN */
	Map alias_seen;        /* package \x01 FQN */
	Map colliding_aliases; /* package \x01 alias */
	bool gate_client_feature;
	const char *client_feature_name;
	bool all_message_encodable_impls;
	/* Lookup caches of the port, which live as long as the request: */
	Map files;           /* see find_file, built on first use */
	bool files_built;
	Map comment_indexes; /* see file_comment_index */
} Batch;

/* proto_file.iter().find(|f| f.name == name): the first file with the name (indexed once) */
static const PbMsg *find_file(Batch *b, const PbMsg *req, const char *name)
{
	if (!b->files_built) {
		b->files_built = true;
		int n = pb_count(req, CGR_PROTO_FILE);
		for (int i = n - 1; i >= 0; i--) {
			const PbMsg *f = pb_msg_at(req, CGR_PROTO_FILE, i);
			if (pb_has(f, FDP_NAME))
				map_put(&b->files, pb_str(f, FDP_NAME), (void *)f);
		}
	}
	return map_get(&b->files, name);
}

static const char *batch_client_feature_name(const Batch *b)
{
	return *b->client_feature_name ? b->client_feature_name : "client";
}

/* owned_view_alias_ident: `.example.v1.Record` -> OwnedRecordView */
static char *owned_view_alias(const char *fqn) { return xcat("Owned", upper_camel(bare_type_name(fqn)), "View"); }

static bool alias_collides(const Batch *b, const char *package, const char *fqn)
{
	return map_has(&b->colliding_aliases, xcat(package, "\x01", owned_view_alias(fqn)));
}

/* collect_alias_collisions */
COLD static void collect_alias_collisions(Batch *b, const PbMsg *req)
{
	Map first_seen = {0};
	int nf = pb_count(req, CGR_FILE_TO_GENERATE);
	for (int i = 0; i < nf; i++) {
		const PbMsg *file = find_file(b, req, pb_str_at(req, CGR_FILE_TO_GENERATE, i));
		if (!file)
			continue;
		const char *package = pb_str(file, FDP_PACKAGE);
		for (int s = 0; s < pb_count(file, FDP_SERVICE); s++) {
			const PbMsg *svc = pb_msg_at(file, FDP_SERVICE, s);
			for (int k = 0; k < pb_count(svc, SDP_METHOD); k++) {
				const PbMsg *m = pb_msg_at(svc, SDP_METHOD, k);
				for (int io = 0; io < 2; io++) {
					uint32_t num = io ? MDP_OUTPUT_TYPE : MDP_INPUT_TYPE;
					if (!pb_has(m, num))
						continue;
					const char *fqn = pb_str(m, num);
					char *key = xcat(package, "\x01", owned_view_alias(fqn));
					const char *prev = map_get(&first_seen, key);
					if (!prev)
						map_put(&first_seen, key, (void *)fqn);
					else if (!str_eq(prev, fqn))
						map_put(&b->colliding_aliases, key, (void *)1);
				}
			}
		}
	}
}

/* generate_owned_view_aliases */
static void generate_owned_view_aliases(Vec *out, const PbMsg *file, Batch *b)
{
	const char *package = pb_str(file, FDP_PACKAGE);
	for (int s = 0; s < pb_count(file, FDP_SERVICE); s++) {
		const PbMsg *svc = pb_msg_at(file, FDP_SERVICE, s);
		for (int k = 0; k < pb_count(svc, SDP_METHOD); k++) {
			const PbMsg *m = pb_msg_at(svc, SDP_METHOD, k);
			for (int io = 0; io < 2; io++) {
				uint32_t num = io ? MDP_OUTPUT_TYPE : MDP_INPUT_TYPE;
				if (!pb_has(m, num))
					continue;
				const char *fqn = pb_str(m, num);
				if (alias_collides(b, package, fqn))
					continue;
				char *key = xcat(package, "\x01", fqn);
				if (map_has(&b->alias_seen, key))
					continue;
				map_put(&b->alias_seen, key, (void *)1);
				PpNode *view = rust_view_type(fqn, package, "'static");
				PpNode *item = pp_item_type(PP_VIS_PUB, owned_view_alias(fqn), ty1("::buffa::view::OwnedView", view));
				doc(item, xcat("Shorthand for `OwnedView<", upper_camel(bare_type_name(fqn)), "View<'static>>`."));
				vec_push(out, item);
			}
		}
	}
}

/* fn encode(&self, codec: ::connectrpc::CodecFormat) -> Result<ok, ::connectrpc::ConnectError> */
static PpNode *encode_fn(const char *name, const char *ok, PpNode *body)
{
	PpNode *ret = ty2("::std::result::Result", ty(ok), ty("::connectrpc::ConnectError"));
	PpNode *sig = sig_self(name, ret);
	pp_push(sig, arg("codec", ty("::connectrpc::CodecFormat")));
	return pp_item_fn(PP_VIS_INHERITED, sig, pp_block(s_expr(body), NULL));
}

/* encodable_impl_pair */
static void encodable_impl_pair(Vec *out, const char *fqn, const char *package, Batch *b)
{
	if (map_has(&b->encodable_seen, fqn))
		return;
	map_put(&b->encodable_seen, fqn, (void *)1);
	const char *path = resolve_path(fqn, package);
	if (has_prefix(path, "::"))
		return;
	PpNode *owned = rust_type(fqn, package);
	PpNode *encodable = pp_path_args(pp_path("::connectrpc::Encodable"), pp_angle(owned, NULL));

	PpNode *view_impl = pp_item_impl(encodable, rust_view_type(fqn, package, "'_"));
	pp_push(view_impl, encode_fn("encode", "::buffa::bytes::Bytes",
				     e_call("::connectrpc::__codegen::encode_view_body", pp_e("self"), pp_e("codec"))));
	vec_push(out, view_impl);

	PpNode *owned_impl = pp_item_impl(pp_path_args(pp_path("::connectrpc::Encodable"), pp_angle(owned, NULL)),
					  ty1("::buffa::view::OwnedView", rust_view_type(fqn, package, "'static")));
	PpNode *reborrow = pp_e_method(pp_e("self"), "reborrow", NULL);
	pp_push(owned_impl, encode_fn("encode", "::buffa::bytes::Bytes",
				      e_call("::connectrpc::__codegen::encode_view_body", reborrow, pp_e("codec"))));
	PpNode *segments = encode_fn(
		"encode_segments", "::connectrpc::EncodedBody",
		pp_e_call(pp_e("::connectrpc::__codegen::encode_view_body_segments"), pp_e_method(pp_e("self"), "reborrow", NULL),
			  pp_e_method(pp_e("self"), "bytes", NULL), pp_e("codec"), NULL));
	doc(segments, " An `OwnedView` still holds the buffer it was decoded from, so");
	doc(segments, " its large fields can be handed to the response body by");
	doc(segments, " reference count instead of copied. The bare view impl above");
	doc(segments, " cannot do this: it has borrows but no buffer to name.");
	pp_push(owned_impl, segments);
	vec_push(out, owned_impl);
}

/* generate_encodable_view_impls */
static void generate_encodable_view_impls(Vec *out, const PbMsg *file, Batch *b)
{
	const char *package = pb_str(file, FDP_PACKAGE);
	for (int s = 0; s < pb_count(file, FDP_SERVICE); s++) {
		const PbMsg *svc = pb_msg_at(file, FDP_SERVICE, s);
		for (int k = 0; k < pb_count(svc, SDP_METHOD); k++)
			encodable_impl_pair(out, method_output(pb_msg_at(svc, SDP_METHOD, k)), package, b);
	}
}

/* generate_all_message_encodable_impls (recurse) */
static void all_message_impls(Vec *out, const PbMsg *msg, const char *fqn_prefix, const char *package, Batch *b)
{
	enum { MESSAGE_OPTIONS_MAP_ENTRY = 7 };
	if (pb_int(pb_msg(msg, DP_OPTIONS), MESSAGE_OPTIONS_MAP_ENTRY, 0))
		return;
	char *fqn = xcat(fqn_prefix, ".", pb_str(msg, DP_NAME));
	encodable_impl_pair(out, fqn, package, b);
	for (int i = 0; i < pb_count(msg, DP_NESTED_TYPE); i++)
		all_message_impls(out, pb_msg_at(msg, DP_NESTED_TYPE, i), fqn, package, b);
}

static void generate_all_message_encodable_impls(Vec *out, const PbMsg *file, Batch *b)
{
	const char *package = pb_str(file, FDP_PACKAGE);
	const char *prefix = *package ? xcat(".", package) : "";
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++)
		all_message_impls(out, pb_msg_at(file, FDP_MESSAGE_TYPE, i), prefix, package, b);
}

/* ---- comments ---- */

/* str::trim_end (Unicode white space) */
static Str trim_end(Str s)
{
	while (s.n) {
		size_t i = s.n - 1;
		while (i > 0 && ((unsigned char)s.p[i] & 0xC0) == 0x80)
			i--;
		uint32_t c;
		utf8_decode(s.p + i, s.n - i, &c);
		if (!rust_is_whitespace(c))
			break;
		s.n = i;
	}
	return s;
}

/* The comment of one location (find_comment): NULL if it has none, "" if it sanitizes to nothing. */
static const char *location_comment(const PbMsg *loc)
{
	Str comment;
	if (pb_has(loc, LOC_LEADING))
		comment = pb_bytes(loc, LOC_LEADING);
	else if (pb_has(loc, LOC_TRAILING))
		comment = pb_bytes(loc, LOC_TRAILING);
	else
		return NULL;
	Str *lines;
	int nl = rust_lines(comment, &lines);
	Buf b = {0};
	for (int k = 0; k < nl; k++) {
		Str line = lines[k];
		if (line.n && line.p[0] == ' ')
			line.p++, line.n--;
		if (k)
			buf_addc(&b, '\n');
		buf_add_str(&b, trim_end(line));
	}
	Str normalized = buf_view(&b);
	while (normalized.n && normalized.p[0] == '\n')
		normalized.p++, normalized.n--;
	while (normalized.n && normalized.p[normalized.n - 1] == '\n')
		normalized.n--;
	return sanitize_comment(normalized);
}

/* Per-file indexes (built once per file): the locations of service and method paths ([6, s] and
 * [6, s, 2, m]) in order, the first index of each service name and the first (service, method)
 * of each pair of names. Names are keyed with a presence mark (Option<String> equality). */
typedef struct {
	Map locations; /* "s" or "s.m" -> Vec of locations */
	Map services;  /* name -> index + 1 */
	Map methods;   /* service name \x01 method name -> PathIdx */
} FileIndex;
typedef struct {
	int si, mi;
} PathIdx;

static const char *opt_name(const PbMsg *m, uint32_t num) { return pb_has(m, num) ? xcat("+", pb_str(m, num)) : "-"; }

/* Batch.comment_indexes: the file pointer as key ("%llx") */
static FileIndex *file_comment_index(Batch *b, const PbMsg *file)
{
	char *key = xfmt("%llx", (unsigned long long)(uintptr_t)file);
	FileIndex *x = map_get(&b->comment_indexes, key);
	if (x)
		return x;
	x = xalloc(sizeof *x);
	map_put(&b->comment_indexes, key, x);
	const PbMsg *info = pb_msg(file, FDP_SOURCE_CODE_INFO);
	int n = pb_count(info, SCI_LOCATION);
	for (int i = 0; i < n; i++) {
		const PbMsg *loc = pb_msg_at(info, SCI_LOCATION, i);
		int np = pb_count(loc, LOC_PATH);
		if ((np != 2 && np != 4) || pb_int_at(loc, LOC_PATH, 0) != 6 || (np == 4 && pb_int_at(loc, LOC_PATH, 2) != 2))
			continue;
		char *k = np == 2 ? xfmt("%lld", (long long)pb_int_at(loc, LOC_PATH, 1))
				  : xfmt("%lld.%lld", (long long)pb_int_at(loc, LOC_PATH, 1), (long long)pb_int_at(loc, LOC_PATH, 3));
		Vec *v = map_get(&x->locations, k);
		if (!v) {
			v = xalloc(sizeof *v);
			map_put(&x->locations, k, v);
		}
		vec_push(v, (void *)loc);
	}
	for (int si = 0; si < pb_count(file, FDP_SERVICE); si++) {
		const PbMsg *s = pb_msg_at(file, FDP_SERVICE, si);
		const char *sname = opt_name(s, SDP_NAME);
		if (!map_has(&x->services, sname))
			map_put(&x->services, sname, (void *)(intptr_t)(si + 1));
		for (int mi = 0; mi < pb_count(s, SDP_METHOD); mi++) {
			char *mk = xcat(sname, "\x01", opt_name(pb_msg_at(s, SDP_METHOD, mi), MDP_NAME));
			if (!map_has(&x->methods, mk)) {
				PathIdx *p = xalloc(sizeof *p);
				p->si = si, p->mi = mi;
				map_put(&x->methods, mk, p);
			}
		}
	}
	return x;
}

/* find_comment: the first location with the path whose comment is not empty after sanitizing;
 * NULL as soon as a matching location has no comment at all. */
static const char *find_comment(const FileIndex *x, const char *path_key)
{
	const Vec *v = map_get(&x->locations, path_key);
	for (int i = 0; v && i < v->n; i++) {
		const char *cleaned = location_comment(v->v[i]);
		if (!cleaned)
			return NULL;
		if (*cleaned)
			return cleaned;
	}
	return NULL;
}

/* get_service_comment */
static const char *get_service_comment(Batch *b, const PbMsg *file, const PbMsg *service)
{
	FileIndex *x = file_comment_index(b, file);
	intptr_t si = (intptr_t)map_get(&x->services, opt_name(service, SDP_NAME));
	return si ? find_comment(x, xfmt("%d", (int)(si - 1))) : NULL;
}

/* get_method_comment: the first service with the name that has a method with the name */
static const char *get_method_comment(Batch *b, const PbMsg *file, const PbMsg *service, const PbMsg *method)
{
	FileIndex *x = file_comment_index(b, file);
	const PathIdx *p = map_get(&x->methods, xcat(opt_name(service, SDP_NAME), "\x01", opt_name(method, MDP_NAME)));
	return p ? find_comment(x, xfmt("%d.%d", p->si, p->mi)) : NULL;
}

/* ---- services ---- */

/* check_method_collisions */
COLD static void check_method_collisions(const char *service_name, const PbMsg *service)
{
	Map seen = {0};
	for (int k = 0; k < pb_count(service, SDP_METHOD); k++) {
		const char *proto_name = pb_str(pb_msg_at(service, SDP_METHOD, k), MDP_NAME);
		char *s = snake(proto_name);
		const char *idents[2] = {s, xcat(s, "_with_options")};
		for (int i = 0; i < 2; i++) {
			const char *prev = map_get(&seen, idents[i]);
			if (prev) {
				Buf a = {0}, c = {0};
				rust_str_debug(&a, str_c(prev));
				rust_str_debug(&c, str_c(proto_name));
				fail("service %s: RPC methods %s and %s both generate Rust identifier `%s`; rename one in the proto",
				     service_name, buf_str(&a), buf_str(&c), idents[i]);
			}
		}
		for (int i = 0; i < 2; i++)
			map_put(&seen, idents[i], (void *)proto_name);
	}
}

/* method_spec_const_ident: {SERVICE}_{METHOD}_SPEC */
static char *spec_const_ident(const PbMsg *service, const char *method_name)
{
	return xcat(to_uppercase(snake(pb_str(service, SDP_NAME))), "_", to_uppercase(snake(method_name)), "_SPEC");
}

/* generate_spec_consts */
static void generate_spec_consts(Vec *out, const char *full_service_name, const PbMsg *service)
{
	for (int k = 0; k < pb_count(service, SDP_METHOD); k++) {
		const PbMsg *m = pb_msg_at(service, SDP_METHOD, k);
		const char *method_name = pb_str(m, MDP_NAME);
		bool cs = client_streaming(m), ss = server_streaming(m);
		const char *stream_type = cs && ss ? "::connectrpc::StreamType::BidiStream"
					  : cs	   ? "::connectrpc::StreamType::ClientStream"
					  : ss	   ? "::connectrpc::StreamType::ServerStream"
						   : "::connectrpc::StreamType::Unary";
		int level = idempotency_level(m);
		const char *idem = level == IDEMPOTENCY_NO_SIDE_EFFECTS ? "::connectrpc::IdempotencyLevel::NoSideEffects"
				   : level == IDEMPOTENCY_IDEMPOTENT	 ? "::connectrpc::IdempotencyLevel::Idempotent"
									 : "::connectrpc::IdempotencyLevel::Unknown";
		PpNode *server = e_call("::connectrpc::Spec::server", lit_str(xcat("/", full_service_name, "/", method_name)),
					pp_e(stream_type));
		PpNode *item = pp_item_const(PP_VIS_PUB, spec_const_ident(service, method_name), ty("::connectrpc::Spec"),
					     pp_e_method(server, "with_idempotency_level", pp_e(idem), NULL));
		doc_attrs(item, xcat("Static [`Spec`](::connectrpc::Spec) for the `", method_name,
				     "` RPC, as seen by the server; the generated client passes it with "
				     "[`origin`](::connectrpc::Spec::origin) `Client` (compare across sides with "
				     "[`Spec::same_method`](::connectrpc::Spec::same_method))."));
		vec_push(out, item);
	}
}

/* ::connectrpc::ServiceRequest<'_, input> */
static PpNode *service_request_ty(PpNode *input)
{
	return pp_ty_path(pp_path_args(pp_path("::connectrpc::ServiceRequest"), pp_angle(pp_lifetime("'_"), input, NULL)));
}

static void doc_lines(PpNode *item, const char *const *lines)
{
	for (; *lines; lines++)
		doc(item, *lines);
}

static const char *const BORROW_DOC[] = {"", " `'a` lets the response body borrow from `&self` (e.g. server-resident state).", NULL};

/* stream_items_doc */
static void stream_items_doc(PpNode *item, const PbMsg *m)
{
	static const char *const lines[] = {
		"",
		" Each `requests` item is a [`StreamMessage`](::connectrpc::StreamMessage):",
		" it owns its buffer, is `Send + 'static`, and exposes zero-copy",
		" accessor methods (`item.name()`), `.view()`, and",
		" `.to_owned_message()`.",
		NULL,
	};
	doc_lines(item, lines);
	if (pb_has(m, MDP_INPUT_TYPE) == pb_has(m, MDP_OUTPUT_TYPE) && str_eq(method_input(m), method_output(m))) {
		doc(item, " Items can be yielded back unchanged");
		doc(item, " (`StreamMessage<M>` implements `Encodable<M>`).");
	}
}

/* generate_trait_method */
static PpNode *generate_trait_method(Batch *b, const PbMsg *file, const PbMsg *service, const PbMsg *m, const char *package)
{
	const char *method_name = pb_str(m, MDP_NAME);
	char *name = method_snake(method_name);
	PpNode *output = rust_type(method_output(m), package);
	const char *method_doc = get_method_comment(b, file, service, m);
	if (!method_doc)
		method_doc = "";
	const char *comment = *method_doc ? method_doc : xcat("Handle the ", method_name, " RPC.");
	bool cs = client_streaming(m), ss = server_streaming(m);
	PpNode *ctx = arg("ctx", ty("::connectrpc::RequestContext"));
	PpNode *sig, *ret;
	PpNode *item;
	if (ss && !cs) {
		PpNode *request = arg("request", service_request_ty(rust_type(method_input(m), package)));
		ret = ty_future(ty1("::connectrpc::ServiceResult",
				    ty1("::connectrpc::ServiceStream", ty_impl_encodable(output, pp_precise_capture("Self", NULL)))));
		sig = sig_self(name, ret);
		pp_push(sig, ctx);
		pp_push(sig, request);
		item = pp_item_fn(PP_VIS_INHERITED, sig, NULL);
		doc_attrs(item, comment);
		static const char *const request_doc[] = {
			"",
			" `request` is borrowed from the request body and is valid for the",
			" duration of the call (until the response stream is returned);",
			" message fields are read directly on it (zero-copy). Data the",
			" returned stream needs must be copied out or converted via",
			" `.to_owned_message()`.",
			NULL,
		};
		doc_lines(item, request_doc);
		return item;
	}
	if (cs) {
		PpNode *stream_owned = rust_type(method_input(m), package);
		PpNode *requests = arg("requests", ty1("::connectrpc::InboundStream", stream_owned));
		if (!ss) {
			ret = ty_future(ty1("::connectrpc::ServiceResult", ty_impl_encodable(output, pp_precise_capture("'a", "Self", NULL))));
			sig = pp_sig(name, ret);
			pp_set_generics(sig, pp_generics());
			pp_push(pp_generics_of(sig), pp_lifetime_param("'a"));
			pp_push(sig, pp_receiver(true, false, "'a"));
		} else {
			ret = ty_future(ty1("::connectrpc::ServiceResult",
					    ty1("::connectrpc::ServiceStream", ty_impl_encodable(output, pp_precise_capture("Self", NULL)))));
			sig = sig_self(name, ret);
		}
		pp_push(sig, ctx);
		pp_push(sig, requests);
		item = pp_item_fn(PP_VIS_INHERITED, sig, NULL);
		doc_attrs(item, comment);
		if (!ss)
			doc_lines(item, BORROW_DOC);
		stream_items_doc(item, m);
		return item;
	}
	/* unary */
	PpNode *request = arg("request", service_request_ty(rust_type(method_input(m), package)));
	ret = ty_future(ty1("::connectrpc::ServiceResult", ty_impl_encodable(output, pp_precise_capture("'a", "Self", NULL))));
	sig = pp_sig(name, ret);
	pp_set_generics(sig, pp_generics());
	pp_push(pp_generics_of(sig), pp_lifetime_param("'a"));
	pp_push(sig, pp_receiver(true, false, "'a"));
	pp_push(sig, ctx);
	pp_push(sig, request);
	item = pp_item_fn(PP_VIS_INHERITED, sig, NULL);
	doc_attrs(item, comment);
	doc_lines(item, BORROW_DOC);
	static const char *const request_doc[] = {
		"",
		" `request` is borrowed from the request body and is valid for the",
		" duration of the call; message fields are read directly on it",
		" (zero-copy). The response cannot borrow from `request` — use",
		" `.to_owned_message()` (or copy the specific fields) for anything",
		" returned, stored, or moved into `tokio::spawn`.",
		NULL,
	};
	doc_lines(item, request_doc);
	return item;
}

/* ::buffa::view::OwnedView<#input_view<'static>> */
static PpNode *owned_view_ty(const char *fqn, const char *package)
{
	return ty1("::buffa::view::OwnedView", rust_view_type(fqn, package, "'static"));
}

/* svc.#method(ctx, req) */
static PpNode *svc_call(const char *method, const char *req)
{
	return pp_e_method(pp_e("svc"), method, pp_e("ctx"), pp_e(req), NULL);
}

/* x.encode::<output>(format) */
static PpNode *encode_call(PpNode *x, PpNode *output)
{
	return pp_e_method_tf(x, "encode", pp_angle(output, NULL), pp_e("format"), NULL);
}

/* let svc = ::std::sync::Arc::clone(&svc); async move { stmts } as the statements of a block */
static PpNode *svc_async_block(PpNode *async_body)
{
	return pp_e_block(pp_block(s_let("svc", arc_clone(pp_e("svc"))), s_expr(pp_e_async(async_body, true)), NULL));
}

static PpNode *closure(PpNode *body, bool move, PpNode *first, ...)
{
	PpNode *c = pp_e_closure(body, move);
	va_list ap;
	va_start(ap, first);
	for (PpNode *p = first; p; p = va_arg(ap, PpNode *))
		pp_push(c, p);
	va_end(ap);
	return c;
}

static PpNode *pat(const char *name) { return pp_p_ident(name, false, false); }

/* `let sreq = ::connectrpc::ServiceRequest::<input>::from_parts(req.reborrow(), req.bytes());` */
static PpNode *sreq_from_owned_view(const char *input_fqn, const char *package)
{
	return s_let("sreq", pp_e_call(e_generic_assoc("::connectrpc::ServiceRequest", rust_type(input_fqn, package), "from_parts"),
				       pp_e_method(pp_e("req"), "reborrow", NULL), pp_e_method(pp_e("req"), "bytes", NULL), NULL));
}

/* One route registration of the Ext trait (the closure of generate_service). */
static PpNode *route_registration(PpNode *router, const PbMsg *service, const PbMsg *m, const char *package,
				  const char *service_name_const)
{
	const char *method_name = pb_str(m, MDP_NAME);
	char *name = method_snake(method_name);
	char *spec_const = spec_const_ident(service, method_name);
	bool cs = client_streaming(m), ss = server_streaming(m);
	const char *input_fqn = method_input(m);
	PpNode *output = rust_type(method_output(m), package);
	PpNode *call;
	if (ss && !cs) {
		PpNode *async_body = pp_block(sreq_from_owned_view(input_fqn, package), s_expr(pp_e_await(svc_call(name, "sreq"))), NULL);
		PpNode *f = closure(svc_async_block(async_body), true, pat("ctx"), arg("req", owned_view_ty(input_fqn, package)), NULL);
		PpNode *handler = pp_e_block(pp_block(s_let("svc", arc_clone(pp_e("self"))), s_expr(f), NULL));
		call = pp_e_method_tf(router, "route_view_server_stream", pp_angle(pp_ty_infer(), pp_ty_infer(), output, NULL),
				      pp_e(service_name_const), lit_str(method_name),
				      e_call("::connectrpc::view_streaming_handler_fn", handler, NULL), NULL);
	} else if (cs) {
		PpNode *into_items = s_let("req", pp_e_call(e_turbofish("::connectrpc::dispatcher::codegen::into_stream_messages",
									pp_angle(rust_type(input_fqn, package), NULL)),
							    pp_e("req"), NULL));
		PpNode *result = pp_e_await(svc_call(name, "req"));
		PpNode *f;
		if (!ss) {
			PpNode *async_body = pp_block(into_items, s_expr(encode_call(pp_e_try(result), output)), NULL);
			f = closure(svc_async_block(async_body), true, pat("ctx"), pat("req"), pat("format"), NULL);
		} else {
			PpNode *async_body = pp_block(into_items, s_expr(result), NULL);
			f = closure(svc_async_block(async_body), true, pat("ctx"), pat("req"), NULL);
		}
		PpNode *handler = pp_e_block(pp_block(s_let("svc", arc_clone(pp_e("self"))), s_expr(f), NULL));
		if (!ss)
			call = pp_e_method(router, "route_view_client_stream", pp_e(service_name_const), lit_str(method_name),
					   e_call("::connectrpc::view_client_streaming_handler_fn", handler, NULL), NULL);
		else
			call = pp_e_method_tf(router, "route_view_bidi_stream", pp_angle(pp_ty_infer(), pp_ty_infer(), output, NULL),
					      pp_e(service_name_const), lit_str(method_name),
					      e_call("::connectrpc::view_bidi_streaming_handler_fn", handler, NULL), NULL);
	} else {
		const char *route_method = is_no_side_effects(m) ? "route_view_idempotent" : "route_view";
		PpNode *async_body = pp_block(sreq_from_owned_view(input_fqn, package),
					      s_expr(encode_call(pp_e_try(pp_e_await(svc_call(name, "sreq"))), output)), NULL);
		PpNode *f = closure(svc_async_block(async_body), true, pat("ctx"), arg("req", owned_view_ty(input_fqn, package)),
				    pat("format"), NULL);
		PpNode *handler = pp_e_block(
			pp_block(s_let("svc", arc_clone(pp_e("self"))), s_expr(e_call("::connectrpc::view_handler_fn", f, NULL)), NULL));
		call = pp_e_method(router, route_method, pp_e(service_name_const), lit_str(method_name), handler, NULL);
	}
	return pp_e_method(call, "with_spec", pp_e(spec_const), NULL);
}

/* generate_client_method: the method and its _with_options variant */
static void generate_client_method(PpNode *impl, const PbMsg *service, const char *full_service_name, const PbMsg *m,
				   const char *package)
{
	const char *method_name = pb_str(m, MDP_NAME);
	char *spec_const = spec_const_ident(service, method_name);
	PpNode *client_spec = pp_e_method(pp_e(spec_const), "with_origin", pp_e("::connectrpc::SpecOrigin::Client"), NULL);
	char *name = method_snake(method_name);
	char *with_opts = xcat(snake(method_name), "_with_options");
	const char *input_fqn = method_input(m), *output_fqn = method_output(m);
	PpNode *input_type = rust_type(input_fqn, package);
	PpNode *output_view = rust_view_path(output_fqn, package);
	bool cs = client_streaming(m), ss = server_streaming(m);
	char *doc_short = xcat(" Call the ", method_name, " RPC. Sends a request to /", full_service_name, "/", method_name, ".");
	char *doc_opts = xcat(" Call the ", method_name,
			      " RPC with explicit per-call options. Options override "
			      "[`ClientConfig`](::connectrpc::client::ClientConfig) defaults.");
	static const char *const client_stream_doc[] = {
		"",
		" `requests` is any `Stream<Item = ...> + Send + 'static` of",
		" request messages (the `ClientRequestStream` bound); messages",
		" are sent as the stream yields them. It backs the request",
		" body, so yield owned messages or feed the call from a",
		" channel-backed stream. For a collection that is already in",
		" hand, wrap it with `::connectrpc::stream_iter(...)`.",
		"",
		" Dropping the returned future cancels the call: the request",
		" body is dropped along with it, so messages the stream had",
		" not yet yielded are never delivered. A caller that needs the",
		" request delivered must drive the call to completion rather",
		" than, say, wrapping it in a `timeout`.",
		NULL,
	};
	PpNode *view_static = ty_life(output_view, "'static");
	PpNode *inner;
	if (cs && !ss)
		inner = ty1("::connectrpc::client::UnaryResponse", ty1("::buffa::view::OwnedView", view_static));
	else if (cs && ss)
		inner = pp_ty_gen("::connectrpc::client::BidiStream", ty("T::ResponseBody"), input_type, view_static, NULL);
	else if (ss)
		inner = ty2("::connectrpc::client::ServerStream", ty("T::ResponseBody"), view_static);
	else
		inner = ty1("::connectrpc::client::UnaryResponse", ty1("::buffa::view::OwnedView", view_static));
	PpNode *ret = ty2("Result", inner, ty("::connectrpc::ConnectError"));
	const char *call_fn = cs && !ss ? "::connectrpc::client::call_client_stream"
			      : cs	? "::connectrpc::client::call_bidi_stream"
			      : ss	? "::connectrpc::client::call_server_stream"
					: "::connectrpc::client::call_unary";
	const char *value = cs && !ss ? "requests" : cs ? NULL : "request";
	PpNode *value_ty =
		cs && !ss
			? pp_ty_impl(pp_bounds(bound(pp_path_args(pp_path("::connectrpc::client::ClientRequestStream"), pp_angle(input_type, NULL))),
					       NULL))
			: input_type;

	/* short method: delegates to _with_options */
	PpNode *sig = sig_self(name, ret);
	sig->flags |= PP_F_ASYNC;
	PpNode *delegate = pp_e_method(pp_e("self"), with_opts, NULL);
	if (value) {
		pp_push(sig, arg(value, value_ty));
		pp_push(delegate, pp_e(value));
	}
	pp_push(delegate, pp_e_call(pp_e("::connectrpc::client::CallOptions::default"), NULL));
	PpNode *short_fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(pp_e_await(delegate)), NULL));
	doc(short_fn, doc_short);
	if (cs && !ss)
		doc_lines(short_fn, client_stream_doc);
	pp_push(impl, short_fn);

	/* _with_options */
	sig = sig_self(with_opts, ret);
	sig->flags |= PP_F_ASYNC;
	PpNode *call = pp_e_call(pp_e(call_fn), e_ref(self_f("transport")), e_ref(self_f("config")), client_spec, NULL);
	if (value) {
		pp_push(sig, arg(value, value_ty));
		pp_push(call, pp_e(value));
	}
	pp_push(sig, arg("options", ty("::connectrpc::client::CallOptions")));
	pp_push(call, pp_e("options"));
	PpNode *opts_fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(pp_e_await(call)), NULL));
	doc(opts_fn, doc_opts);
	if (cs && !ss)
		doc_lines(opts_fn, client_stream_doc);
	pp_push(impl, opts_fn);
}

/* `let Some(method) = path.strip_prefix(prefix) else { return unimplemented(path); };` */
static PpNode *let_method_else(const char *path_prefix, const char *unimplemented)
{
	PpNode *s = pp_s_local(pp_p_tuple_struct(pp_path("Some"), pat("method"), NULL),
			       pp_e_method(pp_e("path"), "strip_prefix", lit_str(path_prefix), NULL));
	s->c = pp_e_block(pp_block(pp_s_expr(pp_e_return(e_call(unimplemented, pp_e("path"), NULL)), true), NULL));
	return s;
}

/* one call_* method of the Dispatcher impl */
static PpNode *dispatcher_call(const char *fn, const char *payload, const char *payload_ty, const char *result,
			       const char *unimplemented, const char *path_prefix, const Vec *arms)
{
	PpNode *sig = sig_self(fn, ty(result));
	pp_push(sig, arg("path", pp_ty_ref(NULL, false, ty("str"))));
	pp_push(sig, arg("ctx", ty("::connectrpc::RequestContext")));
	pp_push(sig, arg(payload, ty(payload_ty)));
	pp_push(sig, arg("format", ty("::connectrpc::CodecFormat")));
	PpNode *match = pp_e_match(pp_e("method"));
	for (int i = 0; i < arms->n; i++)
		pp_push(match, arms->v[i]);
	pp_push(match, pp_arm(pp_p_wild(), NULL, e_call(unimplemented, pp_e("path"), NULL)));
	PpNode *unused = pp_s_local(pp_p_wild(), pp_e_tuple(e_ref(pp_e("ctx")), e_ref(pp_e(payload)), e_ref(pp_e("format")), NULL));
	return pp_item_fn(PP_VIS_INHERITED, sig, pp_block(let_method_else(path_prefix, unimplemented), unused, s_expr(match), NULL));
}

/* `#method_name => { let svc = ...; Box::pin(async move { stmts }) }` */
static PpNode *dispatch_arm(const char *method_name, PpNode *async_body)
{
	PpNode *body = pp_block(s_let("svc", arc_clone(self_f("inner"))),
				s_expr(e_call("Box::pin", pp_e_async(async_body, true), NULL)), NULL);
	return pp_arm(pp_p_lit(lit_str(method_name)), NULL, pp_e_block(body));
}

/* Ok(resp.map_body(|s| encode_response_stream::<output, _, _>(s, format))) */
static PpNode *ok_stream(PpNode *output)
{
	PpNode *enc = pp_e_call(e_turbofish("::connectrpc::dispatcher::codegen::encode_response_stream",
					    pp_angle(output, pp_ty_infer(), pp_ty_infer(), NULL)),
				pp_e("s"), pp_e("format"), NULL);
	return e_call("Ok", pp_e_method(pp_e("resp"), "map_body", closure(enc, false, pat("s"), NULL), NULL), NULL);
}

/* the borrowed-view request statements of the unary and server-streaming arms */
static void push_view_request(PpNode *block, const char *input_fqn, const char *package, PpNode *body_src)
{
	PpNode *input_owned = rust_type(input_fqn, package);
	PpNode *body = pp_e_try(pp_e_call(e_turbofish("::connectrpc::dispatcher::codegen::request_proto_bytes", pp_angle(input_owned, NULL)),
					  body_src, pp_e("format"), NULL));
	pp_push(block, s_let("body", body));
	PpNode *decode = pp_e_try(pp_e_call(pp_e("::connectrpc::dispatcher::codegen::decode_borrowed_request_view"),
					    e_ref(pp_e("body")), pp_e_method(pp_e("ctx"), "decode_options", NULL), NULL));
	pp_push(block, pp_s_local(pp_p_type(pat("req"), rust_view_type(input_fqn, package, "'_")), decode));
	pp_push(block, s_let("req", pp_e_call(e_generic_assoc("::connectrpc::ServiceRequest", rust_type(input_fqn, package), "from_parts"),
					      e_ref(pp_e("req")), e_ref(pp_e("body")), NULL)));
}

/* generate_service_server */
COLD static void generate_service_server(Vec *out, const char *full_service_name, const char *trait_name, const char *server_name,
				    const PbMsg *service, const char *package)
{
	char *path_prefix = xcat(full_service_name, "/");
	int nm = pb_count(service, SDP_METHOD);
	Vec lookup_arms = {0};
	for (int k = 0; k < nm; k++) {
		const PbMsg *m = pb_msg_at(service, SDP_METHOD, k);
		const char *method_name = pb_str(m, MDP_NAME);
		bool cs = client_streaming(m), ss = server_streaming(m);
		PpNode *desc;
		if (cs && ss)
			desc = pp_e_call(pp_e("::connectrpc::dispatcher::codegen::MethodDescriptor::bidi_streaming"), NULL);
		else if (cs)
			desc = pp_e_call(pp_e("::connectrpc::dispatcher::codegen::MethodDescriptor::client_streaming"), NULL);
		else if (ss)
			desc = pp_e_call(pp_e("::connectrpc::dispatcher::codegen::MethodDescriptor::server_streaming"), NULL);
		else
			desc = e_call("::connectrpc::dispatcher::codegen::MethodDescriptor::unary", pp_e_lit_bool(is_no_side_effects(m)), NULL);
		PpNode *some = e_call("Some", pp_e_method(desc, "with_spec", pp_e(spec_const_ident(service, method_name)), NULL), NULL);
		vec_push(&lookup_arms, pp_arm(pp_p_lit(lit_str(method_name)), NULL, some));
	}

	Vec unary = {0}, server_stream = {0}, client_stream = {0}, bidi = {0};
	for (int k = 0; k < nm; k++) {
		const PbMsg *m = pb_msg_at(service, SDP_METHOD, k);
		const char *method_name = pb_str(m, MDP_NAME);
		char *name = method_snake(method_name);
		const char *input_fqn = method_input(m);
		rust_view_path(input_fqn, package); /* input_view resolves first */
		PpNode *output = rust_type(method_output(m), package);
		bool cs = client_streaming(m), ss = server_streaming(m);
		PpNode *stream_decode = pp_e_call(
			e_turbofish("::connectrpc::dispatcher::codegen::decode_message_request_stream",
				    pp_angle(rust_type(input_fqn, package), NULL)),
			pp_e("requests"), pp_e("format"), pp_e_method(pp_e_method(pp_e("ctx"), "decode_options", NULL), "clone", NULL), NULL);
		PpNode *call = pp_e_await(svc_call(name, cs ? "req_stream" : "req"));
		if (cs && ss) {
			PpNode *body = pp_block(s_let("req_stream", stream_decode), s_let("resp", pp_e_try(call)), s_expr(ok_stream(output)), NULL);
			vec_push(&bidi, dispatch_arm(method_name, body));
		} else if (cs) {
			PpNode *body = pp_block(s_let("req_stream", stream_decode), s_expr(encode_call(pp_e_try(call), output)), NULL);
			vec_push(&client_stream, dispatch_arm(method_name, body));
		} else if (ss) {
			PpNode *body = pp_block(NULL);
			push_view_request(body, input_fqn, package, pp_e("request"));
			pp_push(body, s_let("resp", pp_e_try(call)));
			pp_push(body, s_expr(ok_stream(output)));
			vec_push(&server_stream, dispatch_arm(method_name, body));
		} else {
			PpNode *body = pp_block(NULL);
			push_view_request(body, input_fqn, package, pp_e_try(pp_e_method(pp_e("request"), "encoded", NULL)));
			pp_push(body, s_expr(encode_call(pp_e_try(call), output)));
			vec_push(&unary, dispatch_arm(method_name, body));
		}
	}

	/* pub struct FooServer<T> { inner: ::std::sync::Arc<T>, } */
	PpNode *st = pp_item_struct(PP_VIS_PUB, server_name, PP_F_NAMED);
	pp_set_generics(st, generics_param("T", NULL));
	pp_push(st, pp_field(PP_VIS_INHERITED, "inner", ty1("::std::sync::Arc", ty("T"))));
	doc_attrs(st, xcat("Monomorphic dispatcher for `", trait_name,
			   "`.\n\n"
			   "Unlike `.register(Router)` which type-erases each method into an `Arc<dyn ErasedHandler>` stored in "
			   "a `HashMap`, this struct dispatches via a compile-time `match` on method name: no vtable, no hash "
			   "lookup.\n\n"
			   "# Example\n\n"
			   "```rust,ignore\n"
			   "use connectrpc::ConnectRpcService;\n\n"
			   "let server = ",
			   server_name,
			   "::new(MyImpl);\n"
			   "let service = ConnectRpcService::new(server);\n"
			   "// hand `service` to axum/hyper as a fallback_service\n"
			   "```"));
	vec_push(out, st);

	PpNode *server_t = ty1(server_name, ty("T"));
	PpNode *impl = pp_item_impl(NULL, server_t);
	pp_set_generics(impl, generics_param("T", trait_name));
	PpNode *sig = pp_sig("new", ty("Self"));
	pp_push(sig, arg("service", ty("T")));
	PpNode *s = pp_e_struct(pp_path("Self"), NULL);
	pp_push(s, pp_field_value("inner", e_call("::std::sync::Arc::new", pp_e("service"), NULL)));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(s), NULL));
	doc(fn, " Wrap a service implementation in a monomorphic dispatcher.");
	pp_push(impl, fn);
	sig = pp_sig("from_arc", ty("Self"));
	pp_push(sig, arg("inner", ty1("::std::sync::Arc", ty("T"))));
	s = pp_e_struct(pp_path("Self"), NULL);
	pp_push(s, pp_field_value("inner", NULL));
	fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(s), NULL));
	doc(fn, " Wrap an already-`Arc`'d service implementation.");
	pp_push(impl, fn);
	vec_push(out, impl);

	impl = pp_item_impl(pp_path("Clone"), ty1(server_name, ty("T")));
	pp_set_generics(impl, generics_param("T", NULL));
	s = pp_e_struct(pp_path("Self"), NULL);
	pp_push(s, pp_field_value("inner", arc_clone(self_f("inner"))));
	pp_push(impl, pp_item_fn(PP_VIS_INHERITED, sig_self("clone", ty("Self")), pp_block(s_expr(s), NULL)));
	vec_push(out, impl);

	impl = pp_item_impl(pp_path("::connectrpc::Dispatcher"), ty1(server_name, ty("T")));
	pp_set_generics(impl, generics_param("T", trait_name));
	sig = sig_self("lookup", ty1("Option", ty("::connectrpc::dispatcher::codegen::MethodDescriptor")));
	pp_push(sig, arg("path", pp_ty_ref(NULL, false, ty("str"))));
	PpNode *match = pp_e_match(pp_e("method"));
	for (int i = 0; i < lookup_arms.n; i++)
		pp_push(match, lookup_arms.v[i]);
	pp_push(match, pp_arm(pp_p_wild(), NULL, pp_e("None")));
	fn = pp_item_fn(PP_VIS_INHERITED, sig,
			pp_block(s_let("method", pp_e_try(pp_e_method(pp_e("path"), "strip_prefix", lit_str(path_prefix), NULL))),
				 s_expr(match), NULL));
	pp_push_attr(fn, pp_attr_path(pp_path("inline"), false));
	pp_push(impl, fn);
	pp_push(impl, dispatcher_call("call_unary", "request", "::connectrpc::Payload", "::connectrpc::dispatcher::codegen::UnaryResult",
				      "::connectrpc::dispatcher::codegen::unimplemented_unary", path_prefix, &unary));
	pp_push(impl, dispatcher_call("call_server_streaming", "request", "::buffa::bytes::Bytes",
				      "::connectrpc::dispatcher::codegen::StreamingResult",
				      "::connectrpc::dispatcher::codegen::unimplemented_streaming", path_prefix, &server_stream));
	pp_push(impl, dispatcher_call("call_client_streaming", "requests", "::connectrpc::dispatcher::codegen::RequestStream",
				      "::connectrpc::dispatcher::codegen::UnaryResult",
				      "::connectrpc::dispatcher::codegen::unimplemented_unary", path_prefix, &client_stream));
	pp_push(impl, dispatcher_call("call_bidi_streaming", "requests", "::connectrpc::dispatcher::codegen::RequestStream",
				      "::connectrpc::dispatcher::codegen::StreamingResult",
				      "::connectrpc::dispatcher::codegen::unimplemented_streaming", path_prefix, &bidi));
	vec_push(out, impl);
}

static const char SERVICE_DOC_TAIL[] =
	"\n\n"
	"# Implementing handlers\n"
	"\n"
	"Implement methods with plain `async fn`; the returned future satisfies\n"
	"the `Send` bound automatically.\n"
	"\n"
	"**Unary and server-streaming requests** arrive as\n"
	"[`ServiceRequest<'_, Req>`](::connectrpc::ServiceRequest): a zero-copy\n"
	"view of the request plus its body, valid for the duration of the call.\n"
	"Fields are read directly (`request.name` is a `&str` into the decoded\n"
	"buffer) and the borrow may be held across `.await` points. Anything\n"
	"that must outlive the call — `tokio::spawn`, channels, server state,\n"
	"or data captured by a returned response stream — takes owned data:\n"
	"call `request.to_owned_message()` (or copy the specific fields)\n"
	"first.\n"
	"\n"
	"**Client-streaming and bidi requests** arrive as\n"
	"[`InboundStream<Req>`](::connectrpc::InboundStream) — a\n"
	"`ServiceStream` of [`StreamMessage`](::connectrpc::StreamMessage)s.\n"
	"Each item owns its decoded buffer and is `Send + 'static`, so items\n"
	"can be buffered or moved into spawned tasks; read fields zero-copy\n"
	"through the generated accessor methods (`item.name()`) or `.view()`,\n"
	"convert with `.to_owned_message()`, or yield an item back unchanged —\n"
	"`StreamMessage<M>` implements `Encodable<M>`.\n"
	"\n"
	"Request types resolved through `extern_path` (e.g. well-known types\n"
	"from another crate) use the same wrappers; the crate that owns the\n"
	"type must be generated with buffa ≥ 0.9.0 and views enabled so the\n"
	"backing `HasMessageView` impl exists.\n"
	"\n"
	"The `impl Encodable<Out>` return bound accepts the owned `Out`, the\n"
	"generated `OutView<'_>` / `OwnedOutView`,\n"
	"[`MaybeBorrowed`](::connectrpc::MaybeBorrowed), or\n"
	"[`PreEncoded`](::connectrpc::PreEncoded) for handlers that encode a\n"
	"non-`'static` view internally and pass the bytes across the handler\n"
	"boundary. View bodies are not emitted for output types mapped via\n"
	"`extern_path` (the impl would be an orphan); return owned for\n"
	"WKT/extern outputs.\n"
	"\n"
	"Server-streaming and bidi-streaming methods return\n"
	"`ServiceStream<impl Encodable<Out> + Send + use<Self>>`. The\n"
	"`use<Self>` precise-capturing clause excludes `&self`'s lifetime and\n"
	"the request's lifetime (unary methods use `use<'a, Self>` and may\n"
	"borrow from `&self`), so stream items must be `'static` and cannot\n"
	"borrow from the request. To stream view-encoded data, encode each\n"
	"item inside the stream body and yield\n"
	"[`PreEncoded`](::connectrpc::PreEncoded) — see its `# Streaming\n"
	"example` doc.";

COLD static char *client_doc(const char *client, const char *example)
{
	return xcat("Client for this service.\n"
		    "\n"
		    "Generic over `T: ClientTransport`. For **gRPC** (HTTP/2), use\n"
		    "`Http2Connection` — it has honest `poll_ready` and composes with\n"
		    "`tower::balance` for multi-connection load balancing. For **Connect\n"
		    "over HTTP/1.1** (or unknown protocol), use `HttpClient`.\n"
		    "\n"
		    "# Example (gRPC / HTTP/2)\n"
		    "\n"
		    "```rust,ignore\n"
		    "use connectrpc::client::{Http2Connection, ClientConfig};\n"
		    "use connectrpc::Protocol;\n"
		    "\n"
		    "let uri: http::Uri = \"http://localhost:8080\".parse()?;\n"
		    "let conn = Http2Connection::connect_plaintext(uri.clone()).await?.shared(1024);\n"
		    "let config = ClientConfig::new(uri).with_protocol(Protocol::Grpc);\n"
		    "\n"
		    "let client = ",
		    client, "::new(conn, config);\nlet response = client.", example,
		    "(request).await?;\n"
		    "```\n"
		    "\n"
		    "# Example (Connect / HTTP/1.1 or ALPN)\n"
		    "\n"
		    "```rust,ignore\n"
		    "use connectrpc::client::{HttpClient, ClientConfig};\n"
		    "\n"
		    "let http = HttpClient::plaintext();  // cleartext http:// only\n"
		    "let config = ClientConfig::new(\"http://localhost:8080\".parse()?);\n"
		    "\n"
		    "let client = ",
		    client, "::new(http, config);\nlet response = client.", example,
		    "(request).await?;\n"
		    "```\n"
		    "\n"
		    "# Working with the response\n"
		    "\n"
		    "Unary calls return [`UnaryResponse<OwnedView<FooView>>`](::connectrpc::client::UnaryResponse).\n"
		    "[`view()`](::connectrpc::client::UnaryResponse::view) borrows the response\n"
		    "message, so field access is zero-copy:\n"
		    "\n"
		    "```rust,ignore\n"
		    "let resp = client.",
		    example,
		    "(request).await?;\n"
		    "let name: &str = resp.view().name;  // borrow into the response buffer\n"
		    "```\n"
		    "\n"
		    "If you need the owned struct (e.g. to store or pass by value), use\n"
		    "[`into_owned()`](::connectrpc::client::UnaryResponse::into_owned):\n"
		    "\n"
		    "```rust,ignore\n"
		    "let owned = client.",
		    example,
		    "(request).await?.into_owned();\n"
		    "```\n"
		    "\n"
		    "[`into_view()`](::connectrpc::client::UnaryResponse::into_view) keeps the\n"
		    "zero-copy decoded body (an `OwnedView`) without copying; field access on it\n"
		    "goes through `.reborrow()`. Streaming responses yield one\n"
		    "[`StreamMessage`](::connectrpc::StreamMessage) per received message from\n"
		    "`.message().await` — read fields zero-copy through the generated accessor\n"
		    "methods (`msg.name()`) or `.view()`, or convert with `.to_owned_message()`.");
}

/* fn register(self: ::std::sync::Arc<Self>, router: ::connectrpc::Router) -> ::connectrpc::Router */
static PpNode *register_sig(const char *name)
{
	PpNode *sig = pp_sig(name, ty("::connectrpc::Router"));
	PpNode *recv = pp_receiver(false, false, NULL);
	recv->a = ty1("::std::sync::Arc", ty("Self"));
	pp_push(sig, recv);
	pp_push(sig, arg("router", ty("::connectrpc::Router")));
	return sig;
}

/* generate_service */
COLD static void generate_service(Vec *out, const PbMsg *file, const PbMsg *service, Batch *b)
{
	const char *package = pb_str(file, FDP_PACKAGE);
	const char *service_name = pb_str(service, SDP_NAME);
	check_method_collisions(service_name, service);
	const char *full_service_name = *package ? xcat(package, ".", service_name) : service_name;
	char *service_upper = upper_camel(service_name);
	const char *trait_name = str_eq(service_upper, "Self") ? "Self_" : service_upper;
	char *ext_trait_name = xcat(service_upper, "Ext");
	char *register_marker_name = xcat(service_upper, "RegisterMarker");
	char *client_name = xcat(service_upper, "Client");
	char *server_name = xcat(service_upper, "Server");
	char *service_name_const = xcat(to_uppercase(snake(service_name)), "_SERVICE_NAME");

	const char *service_doc = get_service_comment(b, file, service);
	const char *base_doc = service_doc && *service_doc ? service_doc : xcat("Server trait for ", service_name, ".");
	char *full_doc = xcat(base_doc, SERVICE_DOC_TAIL);

	int nm = pb_count(service, SDP_METHOD);
	Vec trait_methods = {0};
	for (int k = 0; k < nm; k++)
		vec_push(&trait_methods, generate_trait_method(b, file, service, pb_msg_at(service, SDP_METHOD, k), package));

	PpNode *router = pp_e("router");
	for (int k = 0; k < nm; k++)
		router = route_registration(router, service, pb_msg_at(service, SDP_METHOD, k), package, service_name_const);

	PpNode *client_impl = pp_item_impl(NULL, ty1(client_name, ty("T")));
	PpNode *client_tmp = pp_item_impl(NULL, ty("T")); /* collects the client methods */
	for (int k = 0; k < nm; k++)
		generate_client_method(client_tmp, service, full_service_name, pb_msg_at(service, SDP_METHOD, k), package);

	Vec server_items = {0};
	generate_service_server(&server_items, full_service_name, trait_name, server_name, service, package);

	const char *example_method = nm && pb_has(pb_msg_at(service, SDP_METHOD, 0), MDP_NAME)
					     ? method_snake(pb_str(pb_msg_at(service, SDP_METHOD, 0), MDP_NAME))
					     : "method";

	PpNode *client_cfg_attr = NULL;
	if (b->gate_client_feature) {
		client_cfg_attr = pp_attr_list(pp_path("cfg"), PP_DELIM_PAREN, false);
		pp_push(client_cfg_attr, pp_tt_ident("feature"));
		pp_push(client_cfg_attr, pp_tt_punct('=', false));
		pp_push(client_cfg_attr, pp_tt_lit(pm2_string(str_c(batch_client_feature_name(b)))));
	}

	/* pub const FOO_SERVICE_NAME: &str = "pkg.Foo"; */
	PpNode *name_const = pp_item_const(PP_VIS_PUB, service_name_const, pp_ty_ref(NULL, false, ty("str")), lit_str(full_service_name));
	doc(name_const, " Full service name for this service.");
	vec_push(out, name_const);
	generate_spec_consts(out, full_service_name, service);

	PpNode *trait = pp_item_trait(PP_VIS_PUB, trait_name, pp_bounds(bound_s("Send"), bound_s("Sync"), pp_lifetime("'static"), NULL));
	doc_attrs(trait, full_doc);
	pp_push_attr(trait, attr_src("allow", "clippy::type_complexity"));
	for (int i = 0; i < trait_methods.n; i++)
		pp_push(trait, trait_methods.v[i]);
	vec_push(out, trait);

	PpNode *ext = pp_item_trait(PP_VIS_PUB, ext_trait_name, pp_bounds(bound_s(trait_name), NULL));
	static const char *const ext_doc[] = {
		" Extension trait for registering a service implementation with a Router.",
		"",
		" This trait is automatically implemented for all types that implement the service trait.",
		" Prefer [`Router::add_service`](::connectrpc::Router::add_service) for",
		" top-down registration; `register` remains available for compatibility",
		" and cases where the service-first call shape is more convenient.",
		"",
		" # Example",
		"",
		" ```rust,ignore",
		" use std::sync::Arc;",
		"",
		" let service = Arc::new(MyServiceImpl);",
		" let router = service.register(Router::new());",
		" ```",
		NULL,
	};
	doc_lines(ext, ext_doc);
	PpNode *reg = pp_item_fn(PP_VIS_INHERITED, register_sig("register"), NULL);
	doc(reg, " Register this service implementation with a Router.");
	doc(reg, "");
	doc(reg, " Takes ownership of the `Arc<Self>` and returns a new Router with");
	doc(reg, " this service's methods registered.");
	pp_push(ext, reg);
	vec_push(out, ext);

	PpNode *ext_impl = pp_item_impl(pp_path(ext_trait_name), ty("S"));
	pp_set_generics(ext_impl, generics_param("S", trait_name));
	pp_push(ext_impl, pp_item_fn(PP_VIS_INHERITED, register_sig("register"), pp_block(s_expr(router), NULL)));
	vec_push(out, ext_impl);

	PpNode *marker = pp_item_struct(PP_VIS_PUB, register_marker_name, 0);
	doc(marker, " Type-inference marker used by [`Router::add_service`](::connectrpc::Router::add_service).");
	pp_push_attr(marker, attr_src("doc", "hidden"));
	vec_push(out, marker);

	PpNode *sr = pp_item_impl(pp_path_args(pp_path("::connectrpc::ServiceRegister"), pp_angle(ty(register_marker_name), NULL)),
				  ty1("::std::sync::Arc", ty("S")));
	pp_set_generics(sr, generics_param("S", trait_name));
	PpNode *sig = pp_sig("register_service", ty("::connectrpc::Router"));
	pp_push(sig, pp_receiver(false, false, NULL));
	pp_push(sig, arg("router", ty("::connectrpc::Router")));
	PpNode *qcall = pp_e_call(pp_e_qpath(ty("S"), 1, pp_path(xcat(ext_trait_name, "::register"))), pp_e("self"), pp_e("router"), NULL);
	pp_push(sr, pp_item_fn(PP_VIS_INHERITED, sig, pp_block(s_expr(qcall), NULL)));
	vec_push(out, sr);

	for (int i = 0; i < server_items.n; i++)
		vec_push(out, server_items.v[i]);

	PpNode *client = pp_item_struct(PP_VIS_PUB, client_name, PP_F_NAMED);
	pp_set_generics(client, generics_param("T", NULL));
	pp_push(client, pp_field(PP_VIS_INHERITED, "transport", ty("T")));
	pp_push(client, pp_field(PP_VIS_INHERITED, "config", ty("::connectrpc::client::ClientConfig")));
	doc_attrs(client, client_doc(client_name, example_method));
	if (client_cfg_attr)
		pp_push_attr(client, client_cfg_attr);
	pp_push_attr(client, attr_src("derive", "Clone"));
	vec_push(out, client);

	PpNode *g = generics_param("T", NULL);
	PpNode *where = pp_where();
	pp_push(where, pp_pred_type(ty("T"), pp_bounds(bound_s("::connectrpc::client::ClientTransport"), NULL)));
	PpNode *qself = pp_ty_qpath(ty("T::ResponseBody"), 3, pp_path("::connectrpc::http_body::Body::Error"));
	pp_push(where, pp_pred_type(qself, pp_bounds(bound_s("::std::fmt::Display"), NULL)));
	g->a = where;
	pp_set_generics(client_impl, g);
	if (client_cfg_attr)
		pp_push_attr(client_impl, client_cfg_attr);
	sig = pp_sig("new", ty("Self"));
	pp_push(sig, arg("transport", ty("T")));
	pp_push(sig, arg("config", ty("::connectrpc::client::ClientConfig")));
	PpNode *s = pp_e_struct(pp_path("Self"), NULL);
	pp_push(s, pp_field_value("transport", NULL));
	pp_push(s, pp_field_value("config", NULL));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(s), NULL));
	doc(fn, " Create a new client with the given transport and configuration.");
	pp_push(client_impl, fn);
	fn = pp_item_fn(PP_VIS_PUB, sig_self("config", pp_ty_ref(NULL, false, ty("::connectrpc::client::ClientConfig"))),
			pp_block(s_expr(e_ref(self_f("config"))), NULL));
	doc(fn, " Get the client configuration.");
	pp_push(client_impl, fn);
	sig = pp_sig("config_mut", pp_ty_ref(NULL, true, ty("::connectrpc::client::ClientConfig")));
	pp_push(sig, pp_receiver(true, true, NULL));
	fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(pp_e_ref(true, self_f("config"))), NULL));
	doc(fn, " Get a mutable reference to the client configuration.");
	pp_push(client_impl, fn);
	for (int i = 0; i < client_tmp->list.n; i++)
		pp_push(client_impl, client_tmp->list.v[i]);
	vec_push(out, client_impl);
}

/* generate_connect_services */
static void generate_connect_services(Vec *out, const PbMsg *file, Batch *b)
{
	generate_owned_view_aliases(out, file, b);
	generate_encodable_view_impls(out, file, b);
	if (b->all_message_encodable_impls)
		generate_all_message_encodable_impls(out, file, b);
	for (int s = 0; s < pb_count(file, FDP_SERVICE); s++)
		generate_service(out, file, pb_msg_at(file, FDP_SERVICE, s), b);
}

/* GeneratedFile */
typedef struct {
	const char *name, *package, *content;
} GenFile;

/* emit_service_files */
COLD static void emit_service_files(Vec *out, const PbMsg *req, const Options *o, const char *client_feature_name)
{
	Batch b = {0};
	collect_alias_collisions(&b, req);
	b.gate_client_feature = o->gate_client_feature;
	b.client_feature_name = client_feature_name;
	b.all_message_encodable_impls = o->all_messages;
	int nf = pb_count(req, CGR_FILE_TO_GENERATE);
	for (int i = 0; i < nf; i++) {
		const char *file_name = pb_str_at(req, CGR_FILE_TO_GENERATE, i);
		const PbMsg *file = find_file(&b, req, file_name);
		if (!file || !(pb_count(file, FDP_SERVICE) || (b.all_message_encodable_impls && pb_count(file, FDP_MESSAGE_TYPE))))
			continue;
		Vec items = {0};
		generate_connect_services(&items, file, &b);
		if (!items.n)
			continue;
		PpNode *f = pp_file();
		for (int k = 0; k < items.n; k++)
			pp_push(f, items.v[k]);
		GenFile *g = xalloc(sizeof *g);
		g->name = xcat(bc_proto_path_to_stem(file_name), ".__connect.rs");
		g->package = pb_str(file, FDP_PACKAGE);
		g->content = pp_unparse(f);
		vec_push(out, g);
	}
}

static int str_ptr_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* BTreeMap<package, Vec<GenFile>>: the distinct packages sorted, the files of each in order */
COLD static Vec group_by_package(const Vec *files, Map *by_package)
{
	Vec pkgs = {0};
	for (int i = 0; i < files->n; i++) {
		const GenFile *f = files->v[i];
		Vec *v = map_get(by_package, f->package);
		if (!v) {
			v = xalloc(sizeof *v);
			map_put(by_package, f->package, v);
			vec_push(&pkgs, (void *)f->package);
		}
		vec_push(v, (void *)f);
	}
	if (pkgs.n > 1)
		qsort(pkgs.v, (size_t)pkgs.n, sizeof *pkgs.v, str_ptr_cmp);
	return pkgs;
}

#define HEADER "// @generated by connectrpc-codegen. DO NOT EDIT.\n"

/* generate_services */
COLD static void generate_services(Vec *files, const PbMsg *req, const Options *o)
{
	BcConfig config = o->buffa;
	config.generate_views = true;
	bc_context_for_generate(&config, req);
	const char *client_feature_name = options_client_feature_name(o);
	Vec generated = {0};
	emit_service_files(&generated, req, o, client_feature_name);
	Map by_package = {0};
	Vec pkgs = group_by_package(&generated, &by_package);
	if (config.file_per_package) {
		for (int p = 0; p < pkgs.n; p++) {
			const Vec *v = map_get(&by_package, pkgs.v[p]);
			Buf content = {0};
			buf_adds(&content, HEADER);
			for (int i = 0; i < v->n; i++) {
				buf_addc(&content, '\n');
				buf_adds(&content, ((const GenFile *)v->v[i])->content);
			}
			GenFile *g = xalloc(sizeof *g);
			g->name = bc_package_to_filename(pkgs.v[p]);
			g->package = pkgs.v[p];
			g->content = buf_str(&content);
			vec_push(files, g);
		}
		return;
	}
	for (int i = 0; i < generated.n; i++)
		vec_push(files, generated.v[i]);
	for (int p = 0; p < pkgs.n; p++) {
		const Vec *v = map_get(&by_package, pkgs.v[p]);
		Buf content = {0};
		buf_adds(&content, HEADER);
		for (int i = 0; i < v->n; i++) {
			buf_adds(&content, "include!(");
			rust_str_debug(&content, str_c(((const GenFile *)v->v[i])->name));
			buf_adds(&content, ");\n");
		}
		GenFile *g = xalloc(sizeof *g);
		g->name = bc_package_to_mod_filename(pkgs.v[p]);
		g->package = pkgs.v[p];
		g->content = buf_str(&content);
		vec_push(files, g);
	}
}

/* generate: parses the comma-separated parameter into Options. */
COLD static void parse_options(Str param, Options *o)
{
	bc_config_default(&o->buffa);
	o->buffa.generate_json = true;
	o->client_feature_name = "client";
	Str *parts;
	int n = str_split_s(param, ',', &parts);
	for (int i = 0; i < n; i++) {
		Str opt = rust_trim(parts[i]);
		if (!opt.n)
			continue;
		const char *eq = memchr(opt.p, '=', opt.n);
		Str key = eq ? (Str){opt.p, (size_t)(eq - opt.p)} : opt;
		Str value = eq ? (Str){eq + 1, (size_t)(opt.p + opt.n - eq - 1)} : (Str){"", 0};
		char *opt_s = xstrndup(opt.p, opt.n);
		if (eq && str_eqc(key, "buffa_module")) {
			Str rust = rust_trim(value);
			if (!rust.n)
				fail("buffa_module requires a non-empty path, e.g. buffa_module=crate::proto");
			BcPair *p = xalloc(sizeof *p);
			p->key = ".";
			p->value = xstrndup(rust.p, rust.n);
			vec_push(&o->buffa.extern_paths, p);
		} else if (eq && str_eqc(key, "extern_path")) {
			const char *eq2 = memchr(value.p, '=', value.n);
			Buf dbg = {0};
			rust_str_debug(&dbg, value);
			if (!eq2)
				fail("invalid extern_path format %s, expected extern_path=.proto.pkg=::rust::path", buf_str(&dbg));
			Str proto = rust_trim((Str){value.p, (size_t)(eq2 - value.p)});
			Str rust = rust_trim((Str){eq2 + 1, (size_t)(value.p + value.n - eq2 - 1)});
			if (!proto.n || !rust.n)
				fail("invalid extern_path format %s, expected extern_path=.proto.pkg=::rust::path (both sides "
				     "non-empty)",
				     buf_str(&dbg));
			BcPair *p = xalloc(sizeof *p);
			p->key = proto.p[0] == '.' ? xstrndup(proto.p, proto.n) : xcat(".", xstrndup(proto.p, proto.n));
			p->value = xstrndup(rust.p, rust.n);
			vec_push(&o->buffa.extern_paths, p);
		} else if (eq && str_eqc(key, "gate_client_feature")) {
			Str feature = rust_trim(value);
			if (!feature.n)
				fail("gate_client_feature requires a non-empty feature name");
			o->gate_client_feature = true;
			o->client_feature_name = xstrndup(feature.p, feature.n);
		} else if (eq && str_eqc(key, "encodable_impls")) {
			Str v = rust_trim(value);
			if (str_eqc(v, "all_messages")) {
				o->all_messages = true;
			} else if (str_eqc(v, "outputs")) {
				o->all_messages = false;
			} else {
				Buf dbg = {0};
				rust_str_debug(&dbg, v);
				fail("invalid encodable_impls value %s, expected `all_messages` or `outputs`", buf_str(&dbg));
			}
		} else if (eq && str_eqc(rust_trim(key), BC_ELEMENT_MEMORY_LIMIT_OPT)) {
			/* consumed by decode_request */
		} else if (str_eq(opt_s, "file_per_package")) {
			o->buffa.file_per_package = true;
		} else if (str_eq(opt_s, "strict_utf8_mapping")) {
			o->buffa.strict_utf8_mapping = true;
		} else if (str_eq(opt_s, "no_json")) {
			o->buffa.generate_json = false;
		} else if (str_eq(opt_s, "no_register_fn")) {
			o->buffa.emit_register_fn = false;
		} else if (str_eq(opt_s, "gate_client_feature")) {
			o->gate_client_feature = true;
		} else {
			Buf dbg = {0};
			rust_str_debug(&dbg, opt);
			fail("unknown plugin option: %s. Supported: buffa_module=<rust_path>, extern_path=<proto>=<rust>, "
			     "encodable_impls=<all_messages|outputs>, " BC_ELEMENT_MEMORY_LIMIT_OPT "=<bytes|unlimited>, "
			     "file_per_package, strict_utf8_mapping, no_json, no_register_fn, gate_client_feature, "
			     "gate_client_feature=<name>",
			     buf_str(&dbg));
		}
	}
}
