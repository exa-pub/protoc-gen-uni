/* Port of ts-proto v2.18.1 src/main.ts generateFile and the file assembly of src/plugin.ts
 * (code.toString with getTsPoetOpts).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* ---- generate-async-iterable.ts ---- */

/* generateEncodeTransform (encode) and generateDecodeTransform */
static TpCode *generate_transform(const char *full_name, bool encode)
{
	TpCode *c = tp_code(NULL);
	Node *g = tp_co(c, C.u->global_this);
	const char *in = encode ? full_name : "Uint8Array", *out = encode ? "Uint8Array" : full_name;
	Node *u1 = dts_tunion(tref(in, NULL), dts_tarray(tref(in, NULL)), NULL);
	Node *u2 = dts_tunion(tref(in, NULL), dts_tarray(tref(in, NULL)), NULL);
	Node *src = dts_tunion(tref("AsyncIterable", u1, NULL), tref("Iterable", u2, NULL), NULL);
	Node *item[2];
	for (int k = 0; k < 2; k++) {
		Node *arg = k ? dts_as(ID("pkt"), KW("any")) : ID("p");
		Node *e = encode ? mcall(mcall(ID(full_name), "encode", arg, NULL), "finish", NULL) : mcall(ID(full_name), "decode", arg, NULL);
		item[k] = HINT(ES(dts_yield(dts_arr(e, NULL), true)), H_NO_SEMI);
	}
	Node *pv = dts_var_decl(VAR_CONST);
	dts_add(&pv->list, dts_declarator(dts_binding("p", NULL, false), NULL));
	Node *inner = dts_for_of(pv, dts_paren(dts_as(ID("pkt"), KW("any"))), block(item[0], NULL), false);
	Node *ifs = dts_if(call(MEM(MEM(g, "Array"), "isArray"), ID("pkt"), NULL), block(inner, NULL), block(item[1], NULL));
	Node *kv_ = dts_var_decl(VAR_CONST);
	dts_add(&kv_->list, dts_declarator(dts_binding("pkt", NULL, false), NULL));
	Node *loop = dts_for_of(kv_, ID("source"), block(ifs, NULL), true);
	const char *name = encode ? "encodeTransform" : "decodeTransform";
	Node *m = dts_method_prop(dts_id(name), dts_function(L(param("source", src, false, NULL), NULL), NULL, tref("AsyncIterable", tref(out, NULL), NULL),
								     block(loop, NULL), F_ASYNC | F_GENERATOR));
	dts_comment(m, str_c(encode ? "// encodeTransform encodes a source of message objects." : "// decodeTransform decodes a source of encoded messages."),
		    false, 0);
	dts_comment(m, str_c(xcat("// Transform<", in, ", ", out, ">")), false, NL_LINE);
	m->nl_after_cm = NL_LINE;
	c->node = m;
	return c;
}

/* ---- generateFile ---- */

typedef struct {
	FileOut *out;
	TpdFile *file;
} VisitArg;

static void visit_interface(void *arg, const char *full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name)
{
	VisitArg *a = arg;
	out_chunk(a->out, generate_interface_declaration(full_name, m, si, maybe_prefix_package(a->file, proto_full_name)));
}

static void visit_enum(void *arg, const char *full_name, TpdEnum *e, SourceInfo *si, const char *proto_full_name)
{
	(void)proto_full_name;
	VisitArg *a = arg;
	out_chunk(a->out, generate_enum(full_name, e, si));
}

static bool js_opt_is(const char *key, const char *s) { return opt_is(&O.opts, key, s); }

/* joinCode(interfaces, { on: " & " }): one flat intersection */
static Node *and_type(Node *type, Node *t)
{
	if (!type)
		return t;
	if (type->kind == N_TS_INTERSECTION_TYPE) {
		dts_add(&type->list, t);
		return type;
	}
	return dts_tintersection(type, t, NULL);
}

static void visit_methods(void *arg, const char *full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name)
{
	(void)si;
	VisitArg *a = arg;
	const char *full_type_name = maybe_prefix_package(a->file, proto_full_name);
	bool wrap = is_wrapper_type(full_type_name);
	JsVal *oe = opt(&O.opts, "outputEncodeMethods");
	bool nest = opt_true(&O.opts, "nestJs");
	if ((js_truthy(oe) && !js_opt_is("outputEncodeMethods", "encode-no-creation") && !js_opt_is("outputEncodeMethods", "encode-only")) ||
	    O.output_partial || (wrap && !(nest && is_any_value_type_name(full_type_name))))
		out_chunk(a->out, generate_base_instance_factory(full_name, m, full_type_name));

	TpCode *chunk = tp_code(NULL);
	Node *def = tp_defn(chunk, tp_def(full_name));
	TpCode *ifaces = tp_add(chunk, tp_code(NULL));
	TpCode *members = tp_add(chunk, tp_code(NULL));
	NodeList props = {0};
	bool has_type = has_type_member();
	bool exts = opt_true(&O.opts, "outputExtensions");
	bool ext_fns = exts && O.unknown_fields && m->nextension_range;
	int nmembers = 0;
	/* `= {\n${joinCode(staticMembers, { on: ",\n\n" })}\n}`: members on their own lines, a blank line
	 * between the codes */
#define MEMBER(code_)                                                                                       \
	do {                                                                                                \
		TpCode *mc_ = (code_);                                                                      \
		tp_add(members, mc_);                                                                       \
		Node *p_ = mc_->node ? tp_clone(mc_->node) : NULL;                                          \
		if (p_)                                                                                     \
			dts_add(&props, dts_nl(p_, nmembers++ ? NL_BLANK : NL_LINE));                       \
		for (int i_ = 0; i_ < mc_->list.n; i_++)                                                    \
			dts_add(&props, dts_nl(tp_clone(mc_->list.v[i_]), i_ == 0 && nmembers++ ? NL_BLANK : NL_LINE)); \
	} while (0)
	if (has_type)
		MEMBER(tp_code(kv("$type", as_const(sq(full_type_name)))));
	for (int k = 0; exts && k < m->nextension; k++) {
		/* `${name}: <${ctx.utils.Extension}<${type}>> ${extensionInfo}` */
		ExtensionCode e = generate_extension(m, m->extension[k]);
		TpCode *ec = tp_code(NULL);
		Node *t = tp_co_type(ec, C.u->extension, L(tp_sub(ec, e.type), NULL));
		Node *ta = dts_new(N_TS_TYPE_ASSERTION);
		ta->a = tp_sub(ec, e.info);
		ta->b = t;
		ec->node = kv(e.name, ta);
		MEMBER(ec);
	}
	if (js_truthy(oe)) {
		if (opt_true(&O.opts, "outputEncodeIncludeTypes") || opt_true(&O.opts, "outputDecodeIncludeTypes"))
			fail("unsupported by protoc-gen-uni: the ts-proto options outputEncodeIncludeTypes and outputDecodeIncludeTypes");
		if (O.output_encode_true || js_opt_is("outputEncodeMethods", "encode-only") || js_opt_is("outputEncodeMethods", "encode-no-creation")) {
			MEMBER(generate_encode(full_name, m));
			if (ext_fns)
				MEMBER(generate_set_extension(full_name));
		}
		if (O.output_encode_true || js_opt_is("outputEncodeMethods", "decode-only")) {
			MEMBER(generate_decode(full_name, m));
			if (ext_fns)
				MEMBER(generate_get_extension(full_name));
		}
	}
	if (opt_true(&O.opts, "useAsyncIterable")) {
		MEMBER(generate_transform(full_name, true));
		MEMBER(generate_transform(full_name, false));
	}
	JsVal *oj = opt(&O.opts, "outputJsonMethods");
	if (js_truthy(oj)) {
		if (O.output_json_true || js_opt_is("outputJsonMethods", "from-only"))
			MEMBER(generate_from_json(full_name, full_type_name, m));
		if (O.output_json_true || js_opt_is("outputJsonMethods", "to-only"))
			MEMBER(generate_to_json(full_name, full_type_name, m));
	}
	if (O.output_partial)
		MEMBER(generate_from_partial(full_name, m));
	StructFieldNames fn = struct_field_names();
	{
		TpCode *w = tp_code(NULL);
		if (nest) {
			generate_wrap_deep(w, full_type_name, &fn, &w->list);
			generate_unwrap_deep(w, full_type_name, &fn, &w->list);
		} else {
			generate_wrap_shallow(w, full_type_name, &fn, &w->list);
			generate_unwrap_shallow(w, full_type_name, &fn, &w->list);
		}
		for (int i = 0; i < w->list.n; i++) {
			TpCode *one = w->n > i ? w->v[i] : tp_code(NULL);
			one->node = w->list.v[i];
			MEMBER(one);
		}
	}
#undef MEMBER
	if (props.n > 0) {
		Node *type = NULL;
		if (C.u->message_fns) {
			TpCode *ic = tp_add(ifaces, tp_code(NULL));
			type = tp_co_type(ic, C.u->message_fns, has_type ? L(tref(full_name, NULL), dts_tlit(sq(full_type_name)), NULL) : L(tref(full_name, NULL), NULL));
		}
		if (js_truthy(oe) && ext_fns) {
			TpCode *ic = tp_add(ifaces, tp_code(NULL));
			Node *t = tp_co_type(ic, C.u->extension_fns, L(tref(full_name, NULL), NULL));
			type = and_type(type, t);
		}
		TpCond *wfn = is_struct_type_name(full_type_name)	? C.u->struct_wrapper_fns
			      : is_any_value_type_name(full_type_name)	? C.u->any_value_wrapper_fns
			      : is_list_value_type_name(full_type_name) ? C.u->list_value_wrapper_fns
			      : is_field_mask_type_name(full_type_name) ? C.u->field_mask_wrapper_fns
									: NULL;
		if (wfn) {
			TpCode *ic = tp_add(ifaces, tp_code(NULL));
			Node *t = tp_co_type(ic, wfn, (NodeList){0});
			type = and_type(type, t);
		}
		for (int k = 0; exts && k < m->nextension; k++) {
			TpCode *ic = tp_add(ifaces, tp_code(NULL));
			Node *t = tp_co_type(ic, C.u->extension_holder,
					     L(dts_tlit(dq(maybe_snake_to_camel(m->extension[k]->name))), tp_sub(ic, to_type_name(m, m->extension[k], false)), NULL));
			type = and_type(type, t);
		}
		if (!type)
			fail("unsupported by protoc-gen-uni: messages without MessageFns (upstream emits invalid TypeScript)");
		Node *o = dts_obj();
		o->list = props;
		chunk->node = dts_export(dts_var(VAR_CONST, dts_bindingn(def->text, type, false), o));
		out_chunk(a->out, chunk);
	}
	if (O.output_type_registry) {
		TpCode *rc = tp_code(NULL);
		Node *reg = tp_ref(rc, imp_file("messageTypeRegistry@./typeRegistry"));
		rc->node = ES(mcall(reg, "set", MEM(ID(full_name), "$type"), ID(full_name), NULL));
		out_chunk(a->out, rc);
	}
}

/* the prefix of getTsPoetOpts as comments */
static int prefix_comments(const char *file_name, NodeComment *cm)
{
	int n = 0;
	cm[n++] = line_comment("// Code generated by protoc-gen-ts_proto. DO NOT EDIT.", 0);
	if (opt_true(&O.opts, "annotateFilesWithVersion")) {
		cm[n++] = line_comment("// versions:", NL_LINE);
		cm[n++] = line_comment(xcat("//   protoc-gen-ts_proto  ", C.ts_proto_version), NL_LINE);
		cm[n++] = line_comment(xcat("//   protoc               ", C.proto_version), NL_LINE);
	}
	if (file_name)
		cm[n++] = line_comment(xcat("// source: ", file_name), NL_LINE);
	cm[n++] = (NodeComment){STR_LIT("/* eslint-disable */"), true, NL_BLANK};
	return n;
}

/* the prefix of getTsPoetOpts as text */
static const char *raw_prefix(const char *file_name)
{
	Buf b = {0};
	buf_adds(&b, "// Code generated by protoc-gen-ts_proto. DO NOT EDIT.");
	if (opt_true(&O.opts, "annotateFilesWithVersion"))
		buf_fmt(&b, "\n// versions:\n//   protoc-gen-ts_proto  %s\n//   protoc               %s", C.ts_proto_version, C.proto_version);
	buf_fmt(&b, "\n%s\n\n    /* eslint-disable */", file_name ? xcat("// source: ", file_name) : "");
	return xstrdup(buf_str(&b));
}

static TpOpts poet_opts(const char *path, const char *file_name)
{
	static const char *const long_[] = {"long"};
	NodeComment *cm = xalloc(sizeof(NodeComment) * 8);
	TpOpts o = {.path = path, .prefix = cm, .nprefix = prefix_comments(file_name, cm), .force_require = long_,
		    .nforce_require = opt_true(&O.opts, "esModuleInterop") ? 0 : 1};
	return o;
}

/* generateFile + code.toString: appends the response file */
static void generate_file(TpdFile *f, Buf *resp)
{
	create_file_context(f);
	tp_file_begin();
	const char *module_name = js_replace_first(f->name, ".proto", xcat(O.file_suffix, ".ts"));
	FileOut out = {0};
	if (O.export_common_symbols) {
		out_chunk(&out, tp_code(dts_export(const_("protobufPackage", NULL, sq(f->package)))));
		out_chunk(&out, tp_code(dts_export(const_("protobufFileName", NULL, sq(f->name)))));
	}
	SourceInfo si = source_info_from(f);
	out_comment(&out, jsdoc_comment(si_lookup(&si, FILE_SYNTAX, -1), f->options && f->options->deprecated, NULL));
	VisitArg va = {&out, f};
	visit(f, &si, visit_interface, visit_enum, &va);
	bool nest = opt_true(&O.opts, "nestJs");
	if (nest) {
		if (O.export_common_symbols) {
			Buf p = {0};
			for (const char *c = f->package; *c; c++)
				buf_addc(&p, *c == '.' ? '_' : *c);
			const char *name = xcat(camel_to_snake(buf_str(&p)), "_PACKAGE_NAME");
			out_chunk(&out, tp_code(dts_export(const_(name, NULL, sq(f->package)))));
		}
		if (O.use_date == DATE_DATE && file_has_field(f, is_timestamp))
			out_chunk(&out, protobuf_wrapper("Timestamp", timestamp_wrapper_value));
	}
	if (opt_true(&O.opts, "outputEncodeMethods") || opt_true(&O.opts, "outputJsonMethods") || O.output_type_annotations ||
	    O.output_type_registry || nest)
		visit(f, &si, visit_methods, NULL, &va);
	if (opt_true(&O.opts, "outputExtensions")) {
		for (int k = 0; k < f->nextension; k++) {
			/* export const ${name}: ${ctx.utils.Extension}<${type}> = ${extensionInfo}; */
			ExtensionCode e = generate_extension(NULL, f->extension[k]);
			TpCode *ec = tp_code(NULL);
			Node *t = tp_co_type(ec, C.u->extension, L(tp_sub(ec, e.type), NULL));
			ec->node = dts_export(const_(e.name, t, tp_sub(ec, e.info)));
			out_chunk(&out, ec);
		}
	}
	if (nest && file_has_field(f, is_struct_type))
		out_chunk(&out, protobuf_wrapper("Struct", struct_wrapper_value));
	generate_services(&out, f, &si);
	if (opt_true(&O.opts, "outputSchema"))
		fail("unsupported by protoc-gen-uni: the ts-proto option outputSchema");
	if (opt_true(&O.opts, "esModuleInterop") && out.nchunks == 0)
		out_chunk(&out, tp_code(dts_export_named(NULL, false)));
	/* the used helpers, in the order of the utils */
	TpCode *root = tp_join((TpCode *const *)out.chunks.v, out.chunks.n);
	tp_find_used(root);
	for (int i = 0; i < C.u->nhelpers; i++) {
		Helper *h = &C.u->helpers[i];
		if (!h->build || !tp_used(h->co))
			continue;
		/* the declaration prints as in the first file that printed it (Code.toCodeString) */
		int mark = tp_uses_mark();
		TpCode *c = tp_code(NULL);
		h->build(c);
		tp_cached_text(h->co->decl, mark);
		TpCode *chunk = tp_code(NULL);
		chunk->list = c->list;
		out_chunk(&out, chunk);
	}
	NodeComment trailing[8];
	int ntrailing = out.npend;
	for (int i = 0; i < out.npend; i++) {
		trailing[i] = out.pend[i];
		trailing[i].nl = NL_BLANK;
	}
	TpOpts po = poet_opts(module_name, f->name);
	po.raw_prefix = raw_prefix(f->name);
	po.raw_body = raw_file_body;
	po.raw_arg = f;
	Buf content = {0};
	tp_to_string(root, out.body, trailing, ntrailing, &po, &content);
	Buf file = {0};
	pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(module_name));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&content));
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&file));
}

/* ---- utils.ts generateIndexFiles (outputIndex) ---- */

typedef struct PkgTree PkgTree;
struct PkgTree {
	const char *index;
	Map leaves;
	Vec order;  /* PkgTree*: Object.values order (insertion; package parts are not integers) */
	Vec chunks; /* Node*: the statements */
};

/* an own or inherited property of a plain object (`part in branch.leaves`) */
static bool object_prototype_name(const char *s)
{
	static const char *const names[] = {"constructor", "__defineGetter__", "__defineSetter__", "hasOwnProperty", "__lookupGetter__",
					    "__lookupSetter__", "isPrototypeOf", "propertyIsEnumerable", "toString", "valueOf", "__proto__",
					    "toLocaleString"};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (str_eq(s, names[i]))
			return true;
	return false;
}

static PkgTree *pkg_tree(const char *index)
{
	PkgTree *t = xalloc(sizeof *t);
	t->index = index;
	return t;
}

COLD static void generate_index_files(TpdFile **files, int nfiles, Buf *resp)
{
	PkgTree *root = pkg_tree("index.ts");
	for (int k = 0; k < nfiles; k++) {
		TpdFile *f = files[k];
		const char *module_name = js_replace_first(f->name, ".proto", O.file_suffix);
		Vec parts = {0};
		if (*f->package)
			parts = str_split(f->package, '.');
		PkgTree *branch = root;
		Buf pre = {0};
		for (int i = 0; i < parts.n; i++) {
			const char *part = parts.v[i];
			if (i)
				buf_addc(&pre, '.');
			buf_adds(&pre, part);
			if (object_prototype_name(part)) {
				/* branch becomes a function (or Object.prototype): no leaves, no chunks */
				if (i + 1 < parts.n)
					fail("TypeError: Cannot use 'in' operator to search for '%s' in undefined", (const char *)parts.v[i + 1]);
				fail("TypeError: Cannot read properties of undefined (reading 'push')");
			}
			PkgTree *leaf = map_get(&branch->leaves, part);
			if (!leaf) {
				const char *index = xcat("index.", buf_str(&pre), ".ts");
				Node *e = dts_export_named(dq(xcat("./", xstrndup(index, strlen(index) - 3), O.import_suffix)), false);
				dts_export_namespace(e, ID(part));
				vec_push(&branch->chunks, e);
				leaf = pkg_tree(index);
				map_put(&branch->leaves, part, leaf);
				vec_push(&branch->order, leaf);
			}
			branch = leaf;
		}
		vec_push(&branch->chunks, dts_export_all(dq(xcat("./", module_name, O.import_suffix)), false));
	}
	Vec stack = {0};
	vec_push(&stack, root);
	while (stack.n) {
		PkgTree *t = stack.v[--stack.n];
		tp_file_begin();
		/* joinCode(chunks): no separator, the statements follow each other */
		NodeList body = {0};
		Vec codes = {0};
		for (int i = 0; i < t->chunks.n; i++) {
			dts_add(&body, t->chunks.v[i]);
			vec_push(&codes, tp_code(NULL));
		}
		TpCode *code = tp_join((TpCode *const *)codes.v, codes.n);
		TpOpts po = poet_opts(t->index, NULL);
		Buf content = {0};
		tp_to_string(code, body, NULL, 0, &po, &content);
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(t->index));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&content));
		pb_put_bytes(resp, CGRESP_FILE, buf_view(&file));
		for (int i = 0; i < t->order.n; i++)
			vec_push(&stack, t->order.v[i]);
	}
}

/* ---- generate-type-registry.ts (outputTypeRegistry) ---- */

static Node *sig_m(const char *name, NodeList params, Node *ret) { return dts_method_sig(dts_id(name), NULL, params, ret, false); }

/* the declaration of a used helper, with the statements in out */
static void registry_helper(TpCond *co, NodeList *out, bool blank)
{
	for (int i = 0; i < C.u->nhelpers; i++) {
		Helper *h = &C.u->helpers[i];
		if (h->co != co || !h->build || !tp_used(co))
			continue;
		TpCode *c = tp_code(NULL);
		h->build(c);
		for (int k = 0; k < c->list.n; k++)
			dts_add(out, blank && k == 0 ? BLANK(c->list.v[k]) : c->list.v[k]);
	}
}

COLD static void generate_type_registry(Buf *resp)
{
	tp_file_begin(); /* before any use: the last file's arena is gone */
	make_utils();	 /* plugin.ts makes new utils for the registry */
	TpCode *parts[4];
	/* generateMessageType */
	TpCode *c1 = tp_code(NULL);
	Node *um = tref("UnknownMessage", NULL);
	Node *i = dts_interface(ID("MessageType"), dts_tparams(dts_tparam("Message", um, tref("UnknownMessage", NULL)), NULL));
	tp_add(c1, tp_code(NULL));
	tp_add(c1, tp_code(NULL));
	Node *msg = tref("Message", NULL);
	dts_interface_add(i, prop_sig("$type", O.add_type_to_messages ? dts_tindexed(msg, dts_tlit(SQ("$type"))) : KW("string"), false, false));
	if (js_truthy(opt(&O.opts, "outputEncodeMethods"))) {
		TpImport *br = tp_imp("t:BinaryReader@@bufbuild/protobuf/wire");
		TpImport *bw = tp_imp("t:BinaryWriter@@bufbuild/protobuf/wire");
		TpCode *e = tp_add(c1, tp_code(NULL));
		Node *w1 = tp_tref(e, bw, (NodeList){0});
		Node *w2 = tp_tref(e, bw, (NodeList){0});
		dts_interface_add(i, sig_m("encode", L(sparam("message", tref("Message", NULL), false), sparam("writer", w1, true), NULL), w2));
		TpCode *d = tp_add(c1, tp_code(NULL));
		Node *r1 = tp_tref(d, br, (NodeList){0});
		dts_interface_add(i, sig_m("decode", L(sparam("input", dts_tunion(r1, tref("Uint8Array", NULL), NULL), false), sparam("length", KW("number"), true), NULL),
					   tref("Message", NULL)));
	}
	if (js_truthy(opt(&O.opts, "outputJsonMethods"))) {
		tp_add(c1, tp_code(NULL));
		tp_add(c1, tp_code(NULL));
		dts_interface_add(i, sig_m("fromJSON", L(sparam("object", KW("any"), false), NULL), tref("Message", NULL)));
		dts_interface_add(i, sig_m("toJSON", L(sparam("message", tref("Message", NULL), false), NULL), KW("unknown")));
	}
	if (O.output_partial) {
		TpCode *p = tp_add(c1, tp_code(NULL));
		Node *dp = tp_co(p, C.u->deep_partial);
		dts_interface_add(i, sig_m("fromPartial", L(sparam("object", dts_tref_node(dp, L(tref("Message", NULL), NULL)), false), NULL), tref("Message", NULL)));
	}
	tp_add(c1, tp_code(NULL));
	c1->node = dts_export(i);
	parts[0] = c1;
	/* UnknownMessage */
	Node *ut;
	if (O.add_type_to_messages) {
		ut = dts_ttype_lit();
		dts_ttype_lit_add(ut, HINT(prop_sig("$type", KW("string"), opt_is(&O.opts, "outputTypeAnnotations", "optional"), false), H_NO_SEMI));
	} else {
		ut = KW("unknown");
	}
	parts[1] = tp_code(dts_export(dts_type_alias(ID("UnknownMessage"), NULL, ut)));
	Node *map = newx(ID("Map"), NULL);
	map->b = dts_targs(L(KW("string"), tref("MessageType", NULL), NULL));
	parts[2] = tp_code(dts_export(const_("messageTypeRegistry", NULL, map)));
	parts[3] = tp_code(NULL); /* the ifUsed of Builtin and DeepPartial */
	TpCode *root = tp_join(parts, 4);
	NodeList body = {0};
	for (int k = 0; k < 3; k++)
		dts_add(&body, k ? BLANK(parts[k]->node) : parts[k]->node);
	tp_find_used(root);
	NodeList helpers = {0};
	registry_helper(C.u->builtin, &helpers, true);
	registry_helper(C.u->deep_partial, &helpers, helpers.n == 0);
	addl(&body, helpers);
	TpOpts po = poet_opts("typeRegistry.ts", NULL);
	Buf content = {0};
	tp_to_string(root, body, NULL, 0, &po, &content);
	Buf file = {0};
	pb_put_bytes(&file, CGRESP_FILE_NAME, STR_LIT("typeRegistry.ts"));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, buf_view(&content));
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&file));
}
