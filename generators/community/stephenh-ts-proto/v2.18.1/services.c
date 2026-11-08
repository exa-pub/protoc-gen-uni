/* Port of ts-proto v2.18.1 src/generate-services.ts (the default service interface, the client
 * implementation and the Rpc type), src/generate-grpc-js.ts and src/encode.ts, and the service
 * part of generateFile (visitServices).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* ---- types.ts: request and response types ---- */

static TpCode *observable_type(bool as_type)
{
	if (opt_true(&O.opts, "useAsyncIterable"))
		return tplain("AsyncIterable");
	TpCode *c = tp_code(NULL);
	c->node = tp_tref(c, tp_imp(as_type ? "t:Observable@rxjs" : "Observable@rxjs"), (NodeList){0});
	return c;
}

/* code`${observableType(ctx)}<${type}>` */
static TpCode *observable_of(TpCode *type, bool as_type)
{
	TpCode *c = tp_code(NULL);
	Node *o = tp_sub(c, observable_type(as_type));
	Node *t = tp_sub(c, type);
	o->b = dts_targs(L(t, NULL));
	c->node = o;
	return c;
}

static TpCode *request_type(const TpdMethod *md, bool partial)
{
	TpCode *t = message_to_type_name(md->input_type, true, false);
	if (partial) {
		TpCode *p = tp_code(NULL);
		Node *dp = tp_co(p, C.u->deep_partial);
		p->node = dts_tref_node(dp, L(tp_sub(p, t), NULL));
		t = p;
	}
	if (md->client_streaming)
		return observable_of(t, false);
	return t;
}

static TpCode *response_type(const TpdMethod *md) { return message_to_type_name(md->output_type, true, false); }

static TpCode *response_promise_or_observable(const TpdMethod *md)
{
	if (opt_true(&O.opts, "returnObservable") || md->server_streaming)
		return observable_of(response_type(md), false);
	TpCode *c = tp_code(NULL);
	c->node = tref("Promise", tp_sub(c, response_type(md)), NULL);
	return c;
}

/* ---- generateService ---- */

static TpImport *metadata_import(void)
{
	JsVal *mt = opt(&O.opts, "metadataType");
	return js_truthy(mt) ? tp_imp(js_value_to_string(mt)) : tp_imp("t:Metadata@@grpc/grpc-js");
}

/* ---- context and data loaders ---- */

static bool ctx_opt(void) { return opt_true(&O.opts, "context"); }
static bool dataloaders(void) { return ctx_opt() && opt_true(&O.opts, "useContextDataloaders"); }

/* `<${contextTypeVar(options)}>` */
static Node *context_type_params(void)
{
	return dts_tparams(dts_tparam("Context", opt_true(&O.opts, "useContextDataloaders") ? tref("DataLoaders", NULL) : NULL, NULL), NULL);
}

typedef struct {
	TpdMethod *md;
	const char *unique_identifier, *single_method_name, *input_field_name, *output_field_name;
	TpCode *input_type, *output_type;
	bool map_type;
} BatchMethod;

static bool has_single_repeated_field(const TpdMessage *m) { return m->nfield == 1 && m->field[0]->label == LABEL_REP; }

/* detectBatchMethod */
static bool detect_batch_method(TpdFile *f, TpdService *s, TpdMethod *md, BatchMethod *out)
{
	if (strncmp(md->name, "Batch", 5) != 0)
		return false;
	TypeInfo *in = map_get(&C.type_map, md->input_type), *outt = map_get(&C.type_map, md->output_type);
	if (!in || !outt)
		return false;
	/* inputType[2] of an enum is an EnumDescriptorProto: `.field` is undefined */
	if (!in->msg || !outt->msg)
		fail("TypeError: Cannot read properties of undefined (reading 'length')");
	if (!has_single_repeated_field(in->msg) || !has_single_repeated_field(outt->msg))
		return false;
	TpdMessage *im = in->msg, *om = outt->msg;
	out->md = md;
	out->single_method_name = format_method_name(js_replace_first(md->name, "Batch", "Get"));
	out->input_field_name = im->field[0]->name;
	out->input_type = basic_type_name(im->field[0], false);
	out->output_field_name = om->field[0]->name;
	out->output_type = basic_type_name(om->field[0], false);
	MapType mt;
	out->map_type = detect_map_type(om, om->field[0], &mt);
	if (out->map_type)
		out->output_type = mt.value_type;
	out->unique_identifier = xcat(maybe_prefix_package(f, s->name), ".", md->name);
	return true;
}

/* singular(): drop the last character */
static const char *singular(const char *name)
{
	size_t n = strlen(name);
	return xstrndup(name, n ? n - 1 : 0);
}

static TpCode *generate_service(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	NodeComment *cm = jsdoc_comment(si->self, s->options && s->options->deprecated, NULL);
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *i = dts_interface(tp_defn(head, tp_def(s->name)), ctx_opt() ? context_type_params() : NULL);
	bool grpc_web = opt_is(&O.opts, "outputClientImpl", "grpc-web");
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		TpCode *pj = tp_add(mcode, tp_code(NULL)); /* joinCode(params) */
		NodeList params = {0};
		if (ctx_opt()) {
			tp_add(pj, tp_code(NULL));
			dts_add(&params, sparam("ctx", tref("Context", NULL), false));
		}
		TpCode *rc = tp_add(pj, tp_code(NULL));
		dts_add(&params, sparam("request", tp_sub(rc, request_type(md, grpc_web)), false));
		if (grpc_web) {
			dts_add(&params, sparam("metadata", tref("grpc.Metadata", NULL), true));
		} else if (js_truthy(opt(&O.opts, "metadataType")) || opt_true(&O.opts, "addGrpcMetadata")) {
			TpCode *mdc = tp_add(pj, tp_code(NULL));
			dts_add(&params, sparam("metadata", tp_tref(mdc, metadata_import(), (NodeList){0}), true));
		}
		if (opt_true(&O.opts, "useAbortSignal"))
			dts_add(&params, sparam("abortSignal", tref("AbortSignal", NULL), true));
		if (opt_true(&O.opts, "addNestjsRestParameter"))
			dts_add(&params, dts_rest_pat(dts_binding("rest", NULL, false), KW("any")));
		Node *ret = tp_sub(mcode, response_promise_or_observable(md));
		Node *sig = dts_method_sig(dts_id(format_method_name(md->name)), NULL, params, ret, false);
		add_comment(sig, mc, 0);
		dts_interface_add(i, sig);
		BatchMethod bm;
		if (dataloaders() && detect_batch_method(f, s, md, &bm)) {
			/* `${singleMethodName}(ctx: Context, ${singular(inputFieldName)}: ${inputType},): Promise<${outputType}>;` */
			TpCode *bc = tp_add(c, tp_code(NULL));
			NodeList bp = L(sparam("ctx", tref("Context", NULL), false), sparam(singular(bm.input_field_name), tp_sub(bc, bm.input_type), false), NULL);
			dts_interface_add(i, dts_method_sig(dts_id(bm.single_method_name), NULL, bp, tref("Promise", tp_sub(bc, bm.output_type), NULL), false));
		}
	}
	if (!i->c->list.n)
		i->c->hints |= H_CLOSE_NL;
	Node *decl = dts_export(i);
	add_comment(decl, cm, 0);
	c->node = decl;
	return c;
}

/* ---- generateServiceClientImpl ---- */

static Node *this_(const char *p) { return dts_member(dts_this(), p); }

/* generateBeforeRequest and the afterResponse call: `if (this.rpc.hook) { this.rpc.hook(this.service, "M", v); }` */
static Node *rpc_hook_call(const char *hook, const char *method, const char *var)
{
	return dts_if(MEM(this_("rpc"), hook), block(ES(mcall(this_("rpc"), hook, this_("service"), dq(method), ID(var), NULL)), NULL), NULL);
}

static TpCode *generate_regular_rpc_method(TpdMethod *md, bool first)
{
	TpCode *c = tp_code(NULL);
	TpImport *br = tp_imp("BinaryReader@@bufbuild/protobuf/wire");
	TpCode *raw_in = message_to_type_name(md->input_type, true, false);
	TpCode *in = request_type(md, false);
	TpCode *raw_out = response_type(md);
	bool md_param = js_truthy(opt(&O.opts, "metadataType")) || opt_true(&O.opts, "addGrpcMetadata");
	TpImport *mt = metadata_import();
	TpCode *pj = tp_add(c, tp_code(NULL));
	NodeList params = {0};
	if (ctx_opt()) {
		tp_add(pj, tp_code(NULL));
		dts_add(&params, param("ctx", tref("Context", NULL), false, NULL));
	}
	TpCode *pr = tp_add(pj, tp_code(NULL));
	dts_add(&params, param("request", tp_sub(pr, in), false, NULL));
	if (md_param) {
		TpCode *pm = tp_add(pj, tp_code(NULL));
		dts_add(&params, param("metadata", tp_tref(pm, mt, (NodeList){0}), true, NULL));
	}
	if (opt_true(&O.opts, "useAbortSignal"))
		dts_add(&params, param("abortSignal", tref("AbortSignal", NULL), true, NULL));
	Node *ret = tp_sub(c, response_promise_or_observable(md));
	bool before = opt_true(&O.opts, "rpcBeforeRequest"), after = opt_true(&O.opts, "rpcAfterResponse");
	bool errh = opt_true(&O.opts, "rpcErrorHandler");
	/* encode */
	TpCode *enc = tp_code(NULL);
	enc->node = mcall(mcall(tp_expr(tp_sub(enc, raw_in)), "encode", ID("request"), NULL), "finish", NULL);
	Node *before_stmt = NULL;
	if (before && !md->client_streaming) {
		before_stmt = rpc_hook_call("beforeRequest", md->name, "request");
	} else if (md->client_streaming && before) {
		/* {const encodedRequest = ${encode}; ${generateBeforeRequest(...)}; return encodedRequest} */
		TpCode *e2 = tp_code(NULL);
		Node *b = block(const_("encodedRequest", NULL, tp_sub(e2, enc)), rpc_hook_call("beforeRequest", md->name, "encodedRequest"),
				NL(dts_new(N_EMPTY_STMT), SAME), NL(HINT(RET(ID("encodedRequest")), H_NO_SEMI), SAME), NULL);
		e2->node = b;
		enc = e2;
	}
	if (md->client_streaming) {
		TpCode *e2 = tp_code(NULL);
		if (opt_true(&O.opts, "useAsyncIterable")) {
			e2->node = mcall(tp_expr(tp_sub(e2, raw_in)), "encodeTransform", ID("request"), NULL);
		} else {
			Node *map = tp_ref(e2, tp_imp("map@rxjs/operators"));
			Node *a = HINT(arrow1("request", tp_sub(e2, enc)), H_NO_PARENS);
			e2->node = mcall(ID("request"), "pipe", call(map, a, NULL), NULL);
		}
		enc = e2;
	}
	Node *data = const_("data", NULL, tp_sub(c, enc));
	/* decode and the return statement */
	TpCode *dec = tp_code(NULL);
	Node *dt = tp_expr(tp_sub(dec, raw_out));
	Node *brn = tp_ref(dec, br);
	Node *dexpr = mcall(dt, "decode", newx(brn, ID("data"), NULL), NULL);
	if (after) {
		dec->list = L(const_("response", NULL, dexpr), rpc_hook_call("afterResponse", md->name, "response"), RET(ID("response")), NULL);
	} else {
		dec->node = dexpr;
	}
	TpCode *rs = tp_code(NULL);
	bool observable = opt_true(&O.opts, "returnObservable") || md->server_streaming;
	if (observable && opt_true(&O.opts, "useAsyncIterable")) {
		rs->node = mcall(tp_expr(tp_sub(rs, response_type(md))), "decodeTransform", ID("result"), NULL);
	} else if (observable) {
		Node *map = tp_ref(rs, tp_imp("map@rxjs/operators"));
		TpCode *arrowc = tp_add(rs, tp_code(NULL));
		tp_add(arrowc, dec);
		Node *body;
		if (errh) {
			if (!after)
				fail("unsupported by protoc-gen-uni: the ts-proto option rpcErrorHandler on a streaming method without rpcAfterResponse "
				     "(upstream emits invalid TypeScript, unformatted)");
			body = block(dts_try(blockl(dec->list), dts_binding("error", NULL, false), block(HINT(dts_throw(ID("error")), H_NO_SEMI), NULL), NULL),
				     NULL);
		} else {
			body = after ? blockl(dec->list) : dec->node;
		}
		arrowc->node = arrow(L(dts_binding("data", NULL, false), NULL), NULL, body);
		rs->node = mcall(ID("result"), "pipe", call(map, tp_clone(arrowc->node), NULL), NULL);
	} else if (errh) {
		TpCode *a1 = tp_add(rs, tp_code(NULL));
		tp_add(a1, dec);
		NodeList d2 = after ? dec->list : L(HINT(RET(dec->node), H_NO_SEMI), NULL);
		Node *rej = RET(mcall(ID("Promise"), "reject", ID("error"), NULL));
		Node *t = dts_try(blockl(d2), dts_binding("error", NULL, false), block(rej, NULL), NULL);
		Node *f1 = arrow(L(dts_binding("data", NULL, false), NULL), NULL, block(t, NULL));
		tp_add(rs, tp_code(NULL));
		Node *h = dts_if(MEM(this_("rpc"), "handleError"),
				 block(RET(mcall(ID("Promise"), "reject", mcall(this_("rpc"), "handleError", this_("service"), dq(md->name), ID("error"), NULL), NULL)),
				       NULL),
				 NULL);
		Node *f2 = arrow(L(dts_binding("error", NULL, false), NULL), NULL, block(h, RET(mcall(ID("Promise"), "reject", ID("error"), NULL)), NULL));
		rs->node = mcall(mcall(ID("promise"), "then", f1, NULL), "catch", f2, NULL);
	} else {
		TpCode *arrowc = tp_add(rs, tp_code(NULL));
		tp_add(arrowc, dec);
		arrowc->node = arrow(L(dts_binding("data", NULL, false), NULL), NULL, after ? blockl(dec->list) : dec->node);
		rs->node = mcall(ID("promise"), "then", tp_clone(arrowc->node), NULL);
	}
	const char *rpc_method = md->client_streaming && md->server_streaming ? "bidirectionalStreamingRequest"
				 : md->server_streaming			      ? "serverStreamingRequest"
				 : md->client_streaming			      ? "clientStreamingRequest"
									      : "request";
	NodeList args = L(this_("service"), dq(md->name), ID("data"), NULL);
	if (ctx_opt()) {
		NodeList a2 = L(ID("ctx"), NULL);
		addl(&a2, args);
		args = a2;
	}
	if (opt_true(&O.opts, "addGrpcMetadata"))
		dts_add(&args, ID("metadata"));
	if (opt_true(&O.opts, "useAbortSignal"))
		dts_add(&args, BIN(LOGICAL_OR, ID("abortSignal"), UNDEF()));
	Node *rpc = dts_callv(dts_member(this_("rpc"), rpc_method), NULL, args);
	rpc->hints |= H_TRAILING_COMMA;
	Node *body = before_stmt ? block(data, before_stmt, const_(observable ? "result" : "promise", NULL, rpc), RET(tp_sub(c, rs)), NULL)
				 : block(data, const_(observable ? "result" : "promise", NULL, rpc), RET(tp_sub(c, rs)), NULL);
	Node *m = dts_class_method(dts_id(format_method_name(md->name)), dts_function(params, NULL, ret, body, 0), 0, ACC_NONE, METHOD_METHOD);
	c->node = first ? m : BLANK(m);
	return c;
}

static Node *class_prop(const char *name, Node *type)
{
	return dts_class_prop(dts_id(name), type, NULL, F_READONLY, ACC_PRIVATE);
}

static Node *rpc_context_type(void) { return tref("Rpc", tref("Context", NULL), NULL); }
static Node *rpc_plain_type(void) { return tref("Rpc", NULL); }

/* `{ cacheKeyFn: ${hash}, ...ctx.rpcDataLoaderOptions }` and the DataLoader constructor */
static Node *dataloader_new(TpCode *c, TpCode *in, TpCode *out, Node *lambda, bool trailing)
{
	bool interop = opt_true(&O.opts, "esModuleInterop");
	TpImport *hash = tp_imp(interop ? "hash=object-hash" : "hash*object-hash");
	TpImport *dl = tp_imp(interop ? "DataLoader=dataloader" : "DataLoader*dataloader");
	Node *dlr = tp_ref(c, dl);
	NodeList targs = L(tp_sub(c, in), tp_sub(c, out), KW("string"), NULL);
	Node *opts = obj(kv("cacheKeyFn", tp_ref(c, hash)), dts_spread(MEM(ID("ctx"), "rpcDataLoaderOptions")), NULL);
	Node *n = newx(dlr, lambda, opts, NULL);
	n->b = dts_targs(targs);
	if (trailing)
		n->hints |= H_TRAILING_COMMA;
	return n;
}

/* `const dl = ctx.getDataLoader("${id}", () => { return new DataLoader...; }); return dl.load(${key});` */
static Node *dataloader_body(const char *id, Node *new_dl, const char *key)
{
	Node *get = mcall(ID("ctx"), "getDataLoader", dq(id), arrow((NodeList){0}, NULL, block(RET(new_dl), NULL)), NULL);
	return block(const_("dl", NULL, get), RET(mcall(ID("dl"), "load", ID(key), NULL)), NULL);
}

/* generateBatchingRpcMethod */
static TpCode *generate_batching_rpc_method(const BatchMethod *bm)
{
	TpCode *c = tp_code(NULL);
	const char *f = bm->input_field_name;
	TpCode *lambda = tp_code(NULL); /* joinCode(lambda) */
	Node *req = dts_as(ID("request"), KW("any"));
	Node *then_call = dts_callv(dts_member(dts_this(), format_method_name(bm->md->name)), NULL, L(ID("ctx"), req, NULL));
	Node *ret;
	if (bm->map_type) {
		TpCode *lc = tp_add(lambda, tp_code(NULL));
		Node *idx = BIN(NULLISH, dts_index(MEM(ID("res"), bm->output_field_name), ID("key")), call(tp_co(lc, C.u->fail), NULL));
		Node *m = mcall(ID(f), "map", HINT(arrow1("key", idx), H_NO_PARENS), NULL);
		ret = RET(mcall(then_call, "then", HINT(arrow1("res", block(HINT(RET(m), H_NO_SEMI), NULL)), H_NO_PARENS), NULL));
	} else {
		tp_add(lambda, tp_code(NULL));
		ret = HINT(RET(mcall(then_call, "then", HINT(arrow1("res", MEM(ID("res"), bm->output_field_name)), H_NO_PARENS), NULL)), H_NO_SEMI);
	}
	tp_add(lambda, tp_code(NULL));
	Node *lam = arrow(L(dts_binding(f, NULL, false), NULL), NULL, block(const_("request", NULL, obj(shorthand(f), NULL)), ret, NULL));
	Node *in_t = tp_sub(c, bm->input_type);
	Node *out_t = tref("Promise", tp_sub(c, bm->output_type), NULL);
	tp_add(c, lambda);
	Node *nd = dataloader_new(c, bm->input_type, bm->output_type, lam, false);
	Node *body = dataloader_body(bm->unique_identifier, nd, singular(f));
	NodeList params = L(param("ctx", tref("Context", NULL), false, NULL), param(singular(f), in_t, false, NULL), NULL);
	c->node = dts_class_method(dts_id(bm->single_method_name), dts_function(params, NULL, out_t, body, 0), 0, ACC_NONE, METHOD_METHOD);
	return c;
}

/* generateCachingRpcMethod */
static TpCode *generate_caching_rpc_method(TpdFile *f, TpdService *s, TpdMethod *md)
{
	TpCode *c = tp_code(NULL);
	TpCode *in = request_type(md, false), *out = response_type(md);
	const char *svc = maybe_prefix_package(f, s->name);
	TpImport *br = tp_imp("BinaryReader@@bufbuild/protobuf/wire");
	TpCode *lc = tp_code(NULL);
	Node *data = HINT(const_("data", NULL, mcall(mcall(tp_expr(tp_sub(lc, in)), "encode", ID("request"), NULL), "finish", NULL)), H_NO_SEMI);
	Node *resp = const_("response", NULL, dts_await(mcall(this_("rpc"), "request", ID("ctx"), dq(svc), dq(md->name), ID("data"), NULL)));
	Node *dec = RET(mcall(tp_expr(tp_sub(lc, out)), "decode", newx(tp_ref(lc, br), ID("response"), NULL), NULL));
	Node *async_arrow = HINT(dts_arrow(L(dts_binding("request", NULL, false), NULL), NULL, block(data, resp, dec, NULL), F_ASYNC), H_NO_PARENS);
	Node *lam = arrow(L(dts_binding("requests", NULL, false), NULL), NULL,
			  block(const_("responses", NULL, mcall(ID("requests"), "map", async_arrow, NULL)), RET(mcall(ID("Promise"), "all", ID("responses"), NULL)), NULL));
	lc->node = lam;
	Node *in_t = tp_sub(c, in);
	Node *out_t = tref("Promise", tp_sub(c, out), NULL);
	Node *lam2 = tp_sub(c, lc);
	Node *nd = dataloader_new(c, in, out, lam2, true);
	Node *body = dataloader_body(xcat(svc, ".", md->name), nd, "request");
	NodeList params = L(param("ctx", tref("Context", NULL), false, NULL), param("request", in_t, false, NULL), NULL);
	c->node = dts_class_method(dts_id(format_method_name(md->name)), dts_function(params, NULL, out_t, body, 0), 0, ACC_NONE, METHOD_METHOD);
	return c;
}

/* generateDataLoaderOptionsType and generateDataLoadersType */
static TpCode *generate_data_loader_options_type(void)
{
	Node *i = dts_interface(ID("DataLoaderOptions"), NULL);
	dts_interface_add(i, prop_sig("cache", KW("boolean"), true, false));
	return tp_code(dts_export(i));
}

static TpCode *generate_data_loaders_type(void)
{
	Node *i = dts_interface(ID("DataLoaders"), NULL);
	dts_interface_add(i, prop_sig("rpcDataLoaderOptions", tref("DataLoaderOptions", NULL), true, false));
	NodeList ps = L(sparam("identifier", KW("string"), false), sparam("constructorFn", dts_tfn((NodeList){0}, NULL, tref("T", NULL)), false), NULL);
	dts_interface_add(i, dts_method_sig(dts_id("getDataLoader"), tparams1("T"), ps, tref("T", NULL), false));
	return tp_code(dts_export(i));
}

static TpCode *generate_service_client_impl(TpdFile *f, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	TpCode *arr = tp_add(c, tp_code(NULL)); /* code`${chunks}`: the array */
	const char *service_const = xcat(s->name, "ServiceName");
	Node *k1 = dts_export(const_(service_const, NULL, dq(maybe_prefix_package(f, s->name))));
	TpCode *h = tp_add(arr, tp_code(NULL));
	bool cx = ctx_opt();
	Node *cls = dts_class(NULL, cx ? context_type_params() : NULL);
	Node *impl = dts_new(N_TS_EXPR_WITH_TYPE_ARGS);
	if (cx) {
		/* def(`${name}<Context>`) */
		tp_defn(h, tp_def(xcat(s->name, "<Context>")));
		impl->a = ID(s->name);
		impl->b = dts_targs(L(tref("Context", NULL), NULL));
	} else {
		impl->a = tp_defn(h, tp_def(s->name));
	}
	dts_add(&cls->list3, impl);
	Node *(*rpc_t)(void) = cx ? rpc_context_type : rpc_plain_type;
	dts_class_add(cls, class_prop("rpc", rpc_t()));
	dts_class_add(cls, class_prop("service", KW("string")));
	Node *opts_t = dts_ttype_lit();
	dts_ttype_lit_add(opts_t, HINT(dts_prop_sig(dts_id("service"), KW("string"), true, false), H_NO_SEMI));
	NodeList ctor_body = L(ES(SET(this_("service"), BIN(LOGICAL_OR, dts_opt_member(ID("opts"), "service", true), ID(service_const)))),
			       ES(SET(this_("rpc"), ID("rpc"))), NULL);
	for (int k = 0; k < s->nmethod; k++) {
		const char *n = format_method_name(s->method[k]->name);
		dts_add(&ctor_body, ES(SET(this_(n), mcall(this_(n), "bind", dts_this(), NULL))));
	}
	dts_class_add(cls, dts_constructor(L(param("rpc", rpc_t(), false, NULL), param("opts", opts_t, true, NULL), NULL), blockl(ctor_body), ACC_NONE));
	bool first = true;
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		BatchMethod bm;
		if (dataloaders() && detect_batch_method(f, s, md, &bm)) {
			TpCode *mc = tp_add(arr, generate_batching_rpc_method(&bm));
			dts_class_add(cls, first ? tp_clone(mc->node) : BLANK(tp_clone(mc->node)));
			first = false;
		}
		TpCode *mc;
		if (dataloaders() && strncmp(md->name, "Get", 3) == 0 && md->name[3] >= 'A' && md->name[3] <= 'Z' && !md->server_streaming &&
		    !md->client_streaming)
			mc = tp_add(arr, generate_caching_rpc_method(f, s, md));
		else
			mc = tp_add(arr, generate_regular_rpc_method(md, true));
		dts_class_add(cls, first ? tp_clone(mc->node) : BLANK(tp_clone(mc->node)));
		first = false;
	}
	c->list = L(k1, dts_export(dts_class_decl(ID(xcat(s->name, "ClientImpl")), cls)), NULL);
	return c;
}

/* generateRpcType */
static TpCode *generate_rpc_type(bool has_streaming)
{
	TpCode *c = tp_code(NULL);
	bool md_param = js_truthy(opt(&O.opts, "metadataType")) || opt_true(&O.opts, "addGrpcMetadata");
	TpImport *mt = metadata_import();
	Node *i = dts_interface(ID("Rpc"), ctx_opt() ? tparams1("Context") : NULL);
	const char *names[4] = {"request", "clientStreamingRequest", "serverStreamingRequest", "bidirectionalStreamingRequest"};
	for (int k = 0; k < (has_streaming ? 4 : 1); k++) {
		TpCode *mc = tp_add(c, tp_code(NULL));
		Node *data, *ret;
		if (k == 0) {
			data = tref("Uint8Array", NULL);
			ret = tref("Promise", tref("Uint8Array", NULL), NULL);
		} else {
			TpCode *d = tp_add(mc, tp_code(NULL));
			TpCode *r = tp_add(mc, tp_code(NULL));
			data = k == 2 ? tref("Uint8Array", NULL) : tp_sub(d, observable_of(tplain("Uint8Array"), true));
			ret = k == 1 ? tref("Promise", tref("Uint8Array", NULL), NULL) : tp_sub(r, observable_of(tplain("Uint8Array"), true));
		}
		NodeList params = L(sparam("service", KW("string"), false), sparam("method", KW("string"), false),
				    sparam("data", data, false), NULL);
		if (ctx_opt()) {
			NodeList p2 = L(sparam("ctx", tref("Context", NULL), false), NULL);
			addl(&p2, params);
			params = p2;
		}
		if (md_param) {
			TpCode *pm = tp_add(mc, tp_code(NULL));
			dts_add(&params, sparam("metadata", tp_tref(pm, mt, (NodeList){0}), true));
		}
		if (opt_true(&O.opts, "useAbortSignal"))
			dts_add(&params, sparam("abortSignal", tref("AbortSignal", NULL), true));
		dts_interface_add(i, dts_method_sig(dts_id(names[k]), NULL, params, ret, false));
	}
	static const char *const hooks[] = {"rpcBeforeRequest", "beforeRequest", "request", "rpcAfterResponse", "afterResponse", "response"};
	for (int k = 0; k < 6; k += 3) {
		if (!opt_true(&O.opts, hooks[k]))
			continue;
		tp_add(c, tp_code(NULL));
		/* <T extends { [k in keyof T]: unknown }> */
		Node *mapped = dts_tmapped("k", dts_tkeyof(tref("T", NULL)), NULL, KW("unknown"), MAPPED_NONE, MAPPED_NONE);
		Node *tp = dts_tparams(dts_tparam("T", mapped, NULL), NULL);
		NodeList ps = L(sparam("service", KW("string"), false), sparam("method", KW("string"), false), sparam(hooks[k + 2], tref("T", NULL), false), NULL);
		dts_interface_add(i, dts_method_sig(dts_id(hooks[k + 1]), tp, ps, KW("void"), true));
	}
	if (opt_true(&O.opts, "rpcErrorHandler")) {
		tp_add(c, tp_code(NULL));
		NodeList ps = L(sparam("service", KW("string"), false), sparam("method", KW("string"), false),
				sparam("error", tref("globalThis.Error", NULL), false), NULL);
		dts_interface_add(i, dts_method_sig(dts_id("handleError"), NULL, ps, tref("globalThis.Error", NULL), true));
	}
	c->node = i;
	return c;
}

/* ---- encode.ts ---- */

static void wkt_imp_code(const char *module, const char *name, TpImport **out) { *out = imp_proto(module, wrap_type_name(name, false)); }

static TpCode *generate_encoder(const char *type_name)
{
	const char *name = wrapper_type_name(type_name);
	TpCode *c = tp_code(NULL);
	if (!name) {
		Node *t = tp_expr(tp_sub(c, message_to_type_name(type_name, true, false)));
		c->node = mcall(mcall(t, "encode", ID("value"), NULL), "finish", NULL);
		return c;
	}
	TpImport *tv;
	if (str_eq(name, "Timestamp")) {
		wkt_imp_code("google/protobuf/timestamp", name, &tv);
		Node *t = tp_ref(c, tv);
		TpCode *v = tp_code(ID("value"));
		if (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL) {
			TpCode *v2 = tp_code(NULL);
			Node *fn = tp_co(v2, C.u->to_timestamp);
			v2->node = call(fn, tp_sub(v2, v), NULL);
			v = v2;
		}
		c->node = mcall(mcall(t, "encode", tp_sub(c, v), NULL), "finish", NULL);
		return c;
	}
	if (str_eq(name, "Struct") || str_eq(name, "Value")) {
		wkt_imp_code("google/protobuf/struct", name, &tv);
		Node *t1 = tp_ref(c, tv);
		Node *t2 = tp_ref(c, tv);
		c->node = mcall(mcall(t1, "encode", mcall(t2, "wrap", ID("value"), NULL), NULL), "finish", NULL);
		return c;
	}
	if (str_eq(name, "ListValue")) {
		wkt_imp_code("google/protobuf/struct", name, &tv);
		Node *t = tp_ref(c, tv);
		c->node = mcall(mcall(t, "encode", obj(kv("values", BIN(NULLISH, ID("value"), dts_arr(NULL))), NULL), NULL), "finish", NULL);
		return c;
	}
	wkt_imp_code("google/protobuf/wrappers", name, &tv);
	Node *t = tp_ref(c, tv);
	Node *v;
	if (str_eq(name, "StringValue"))
		v = BIN(NULLISH, ID("value"), DQ(""));
	else if (str_eq(name, "Int32Value") || str_eq(name, "UInt32Value") || str_eq(name, "DoubleValue") || str_eq(name, "FloatValue"))
		v = BIN(NULLISH, ID("value"), NUM("0"));
	else if (str_eq(name, "Int64Value") || str_eq(name, "UInt64Value"))
		v = O.force_long == LONG_LONG ? dts_cond(ID("value"), mcall(ID("value"), "toNumber", NULL), NUM("0")) : BIN(NULLISH, ID("value"), NUM("0"));
	else if (str_eq(name, "BoolValue"))
		v = BIN(NULLISH, ID("value"), dts_bool(false));
	else if (str_eq(name, "BytesValue"))
		v = BIN(NULLISH, ID("value"), newx(ID("Uint8Array"), NUM("0"), NULL));
	else
		fail("unknown wrapper type: %s", name);
	c->node = mcall(mcall(t, "encode", obj(kv("value", v), NULL), NULL), "finish", NULL);
	return c;
}

static TpCode *generate_decoder(const char *type_name)
{
	const char *name = wrapper_type_name(type_name);
	TpCode *c = tp_code(NULL);
	if (!name) {
		Node *t = tp_expr(tp_sub(c, message_to_type_name(type_name, true, false)));
		c->node = mcall(t, "decode", ID("value"), NULL);
		return c;
	}
	TpImport *tv;
	if (str_eq(name, "Timestamp")) {
		wkt_imp_code("google/protobuf/timestamp", name, &tv);
		TpCode *d = tp_code(NULL);
		d->node = mcall(tp_ref(d, tv), "decode", ID("value"), NULL);
		if (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL) {
			Node *fn = tp_co(c, C.u->from_timestamp);
			c->node = call(fn, tp_sub(c, d), NULL);
			return c;
		}
		return d;
	}
	if (str_eq(name, "Struct") || str_eq(name, "ListValue") || str_eq(name, "Value")) {
		wkt_imp_code("google/protobuf/struct", name, &tv);
		Node *t1 = tp_ref(c, tv);
		Node *t2 = tp_ref(c, tv);
		c->node = mcall(t1, "unwrap", mcall(t2, "decode", ID("value"), NULL), NULL);
		return c;
	}
	wkt_imp_code("google/protobuf/wrappers", name, &tv);
	c->node = MEM(mcall(tp_ref(c, tv), "decode", ID("value"), NULL), "value");
	return c;
}

/* ---- generate-grpc-js.ts ---- */

static struct {
	TpImport *CallOptions, *ChannelCredentials, *ClientOptions, *Client, *ClientDuplexStream, *ClientReadableStream,
		*ClientUnaryCall, *ClientWritableStream, *handleBidiStreamingCall, *handleClientStreamingCall,
		*handleServerStreamingCall, *handleUnaryCall, *UntypedServiceImplementation, *makeGenericClientConstructor,
		*Metadata, *ServiceError;
	TpImport *NiceCallOptions, *NiceCallContext; /* generate-nice-grpc.ts */
} G;

/* the module-level imports of generate-grpc-js.ts (made once per request) */
COLD static void grpc_js_imports(void)
{
	G.CallOptions = tp_imp("t:CallOptions@@grpc/grpc-js");
	G.ChannelCredentials = tp_imp("t:ChannelCredentials@@grpc/grpc-js");
	G.ClientOptions = tp_imp("t:ClientOptions@@grpc/grpc-js");
	G.Client = tp_imp("Client@@grpc/grpc-js");
	G.ClientDuplexStream = tp_imp("t:ClientDuplexStream@@grpc/grpc-js");
	G.ClientReadableStream = tp_imp("t:ClientReadableStream@@grpc/grpc-js");
	G.ClientUnaryCall = tp_imp("t:ClientUnaryCall@@grpc/grpc-js");
	G.ClientWritableStream = tp_imp("t:ClientWritableStream@@grpc/grpc-js");
	G.handleBidiStreamingCall = tp_imp("t:handleBidiStreamingCall@@grpc/grpc-js");
	G.handleClientStreamingCall = tp_imp("t:handleClientStreamingCall@@grpc/grpc-js");
	G.handleServerStreamingCall = tp_imp("t:handleServerStreamingCall@@grpc/grpc-js");
	G.handleUnaryCall = tp_imp("t:handleUnaryCall@@grpc/grpc-js");
	G.UntypedServiceImplementation = tp_imp("t:UntypedServiceImplementation@@grpc/grpc-js");
	G.makeGenericClientConstructor = tp_imp("makeGenericClientConstructor@@grpc/grpc-js");
	G.Metadata = tp_imp("t:Metadata@@grpc/grpc-js");
	G.ServiceError = tp_imp("t:ServiceError@@grpc/grpc-js");
	G.NiceCallOptions = tp_imp("t:CallOptions@nice-grpc-common");
	G.NiceCallContext = tp_imp("t:CallContext@nice-grpc-common");
}

/* (value: T): R => body */
static Node *typed_arrow(Node *ptype, Node *ret, Node *body)
{
	return arrow(L(dts_binding("value", ptype, false), NULL), ret, body);
}


static TpCode *generate_service_definition(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	NodeComment *cm = jsdoc_comment(si->self, s->options && s->options->deprecated, NULL);
	TpDef *name = tp_def(xcat(s->name, "Service"));
	TpCode *c1 = tp_add(c, tp_code(NULL));
	Node *n1 = tp_defn(c1, name);
	Node *n2 = tp_defn(c1, name);
	Node *t = dts_export(dts_type_alias(n1, NULL, dts_tquery(n2)));
	add_comment(t, cm, 0);
	TpCode *c2 = tp_add(c, tp_code(NULL));
	Node *o = dts_obj();
	Node *n3 = tp_defn(c2, name);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		TpCode *in = message_to_type_name(md->input_type, false, false);
		TpCode *out = message_to_type_name(md->output_type, false, false);
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpCode *ie = generate_encoder(md->input_type), *oe = generate_encoder(md->output_type);
		TpCode *id = generate_decoder(md->input_type), *od = generate_decoder(md->output_type);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		Node *path = sq(xcat("/", maybe_prefix_package(f, s->name), "/", md->name));
		Node *v = dts_obj();
		dts_obj_add(v, kv("path", as_const(path)));
		dts_obj_add(v, kv("requestStream", as_const(dts_bool(md->client_streaming))));
		dts_obj_add(v, kv("responseStream", as_const(dts_bool(md->server_streaming))));
		Node *it1 = tp_sub(mcode, in);
		Node *ies = tp_sub(mcode, ie);
		dts_obj_add(v, kv("requestSerialize", typed_arrow(it1, tref("Buffer", NULL), call(MEM(ID("Buffer"), "from"), ies, NULL))));
		Node *it2 = tp_sub(mcode, in);
		dts_obj_add(v, kv("requestDeserialize", typed_arrow(tref("Buffer", NULL), it2, tp_sub(mcode, id))));
		Node *ot1 = tp_sub(mcode, out);
		Node *oes = tp_sub(mcode, oe);
		dts_obj_add(v, kv("responseSerialize", typed_arrow(ot1, tref("Buffer", NULL), call(MEM(ID("Buffer"), "from"), oes, NULL))));
		Node *ot2 = tp_sub(mcode, out);
		dts_obj_add(v, kv("responseDeserialize", typed_arrow(tref("Buffer", NULL), ot2, tp_sub(mcode, od))));
		v->hints |= H_TRAILING_COMMA;
		for (int i = 0; i < v->list.n; i++)
			v->list.v[i]->nl = NL_LINE;
		Node *p = NL(kv(format_method_name(md->name), v), LINE);
		if (mc) {
			/* the comment chunk is on its own line */
			add_comment(p, mc, 0);
			p->nl = NL_LINE;
			p->nl_after_cm = NL_LINE;
		}
		dts_obj_add(o, p);
	}
	if (o->list.n)
		o->hints |= H_TRAILING_COMMA;
	else
		o->hints |= H_CLOSE_NL;
	tp_add(c, tp_code(NULL));
	Node *d = dts_export(dts_var(VAR_CONST, dts_bindingn(n3->text, NULL, false), as_const(o)));
	c->list = L(t, d, NULL);
	return c;
}

static TpCode *generate_server_stub(SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	TpCode *h = tp_add(c, tp_code(NULL));
	Node *i = dts_interface(tp_defn(h, tp_def(xcat(s->name, "Server"))), NULL);
	Node *ext = dts_new(N_TS_EXPR_WITH_TYPE_ARGS);
	ext->a = tp_ref(h, G.UntypedServiceImplementation);
	dts_add(&i->list, ext);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		TpCode *in = message_to_type_name(md->input_type, false, false);
		TpCode *out = message_to_type_name(md->output_type, false, false);
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpImport *ct = md->client_streaming ? (md->server_streaming ? G.handleBidiStreamingCall : G.handleClientStreamingCall)
						    : (md->server_streaming ? G.handleServerStreamingCall : G.handleUnaryCall);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		Node *ctn = tp_entity(mcode, ct);
		Node *a1 = tp_sub(mcode, in);
		Node *a2 = tp_sub(mcode, out);
		Node *sig = dts_prop_sig(dts_id(format_method_name(md->name)), dts_tref_node(ctn, L(a1, a2, NULL)), false, false);
		add_comment(sig, mc, 0);
		dts_interface_add(i, sig);
	}
	if (!i->c->list.n)
		i->c->hints |= H_CLOSE_NL;
	c->node = dts_export(i);
	return c;
}

static Node *partial_of(TpCode *c, TpImport *t) { return tref("Partial", tp_tref(c, t, (NodeList){0}), NULL); }

/* (error: ServiceError | null, response: T) => void */
static TpCode *response_callback(TpCode *out)
{
	TpCode *c = tp_code(NULL);
	Node *se = tp_tref(c, G.ServiceError, (NodeList){0});
	Node *ot = tp_sub(c, out);
	c->node = dts_tfn(L(dts_binding("error", dts_tunion(se, KW("null"), NULL), false), dts_binding("response", ot, false), NULL), NULL,
			  KW("void"));
	return c;
}

static Node *client_sig(const char *name, NodeList params, Node *ret)
{
	return dts_method_sig(dts_id(name), NULL, params, ret, false);
}

static TpCode *generate_client_stub(SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	TpCode *h = tp_add(c, tp_code(NULL));
	Node *i = dts_interface(tp_defn(h, tp_def(xcat(s->name, "Client"))), NULL);
	Node *ext = dts_new(N_TS_EXPR_WITH_TYPE_ARGS);
	ext->a = tp_ref(h, G.Client);
	dts_add(&i->list, ext);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		TpCode *in = message_to_type_name(md->input_type, false, false);
		TpCode *out = message_to_type_name(md->output_type, false, false);
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpCode *cb = response_callback(out);
		const char *n = format_method_name(md->name);
		TpCode *m = tp_add(c, tp_code(NULL));
		NodeList sigs = {0};
#define IN() tp_sub(m, in)
#define OUT() tp_sub(m, out)
#define CB() tp_sub(m, cb)
#define T(x) tp_tref(m, G.x, (NodeList){0})
#define P(name, type, opt) sparam(name, type, opt)
		if (md->client_streaming && md->server_streaming) {
			Node *r1 = tp_tref(m, G.ClientDuplexStream, L(IN(), OUT(), NULL));
			dts_add(&sigs, client_sig(n, (NodeList){0}, r1));
			Node *p = P("options", partial_of(m, G.CallOptions), false);
			dts_add(&sigs, client_sig(n, L(p, NULL), tp_tref(m, G.ClientDuplexStream, L(IN(), OUT(), NULL))));
			Node *p1 = P("metadata", T(Metadata), false);
			Node *p2 = P("options", partial_of(m, G.CallOptions), true);
			dts_add(&sigs, client_sig(n, L(p1, p2, NULL), tp_tref(m, G.ClientDuplexStream, L(IN(), OUT(), NULL))));
		} else if (md->client_streaming) {
			Node *p = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(p, NULL), tp_tref(m, G.ClientWritableStream, L(IN(), NULL))));
			Node *q1 = P("metadata", T(Metadata), false);
			Node *q2 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(q1, q2, NULL), tp_tref(m, G.ClientWritableStream, L(IN(), NULL))));
			Node *r1 = P("options", partial_of(m, G.CallOptions), false);
			Node *r2 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(r1, r2, NULL), tp_tref(m, G.ClientWritableStream, L(IN(), NULL))));
			Node *s1 = P("metadata", T(Metadata), false);
			Node *s2 = P("options", partial_of(m, G.CallOptions), false);
			Node *s3 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(s1, s2, s3, NULL), tp_tref(m, G.ClientWritableStream, L(IN(), NULL))));
		} else if (md->server_streaming) {
			Node *p1 = P("request", IN(), false);
			Node *p2 = P("options", partial_of(m, G.CallOptions), true);
			dts_add(&sigs, client_sig(n, L(p1, p2, NULL), tp_tref(m, G.ClientReadableStream, L(OUT(), NULL))));
			Node *q1 = P("request", IN(), false);
			Node *q2 = P("metadata", T(Metadata), true);
			Node *q3 = P("options", partial_of(m, G.CallOptions), true);
			dts_add(&sigs, client_sig(n, L(q1, q2, q3, NULL), tp_tref(m, G.ClientReadableStream, L(OUT(), NULL))));
		} else {
			Node *p1 = P("request", IN(), false);
			Node *p2 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(p1, p2, NULL), T(ClientUnaryCall)));
			Node *q1 = P("request", IN(), false);
			Node *q2 = P("metadata", T(Metadata), false);
			Node *q3 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(q1, q2, q3, NULL), T(ClientUnaryCall)));
			Node *r1 = P("request", IN(), false);
			Node *r2 = P("metadata", T(Metadata), false);
			Node *r3 = P("options", partial_of(m, G.CallOptions), false);
			Node *r4 = P("callback", CB(), false);
			dts_add(&sigs, client_sig(n, L(r1, r2, r3, r4, NULL), T(ClientUnaryCall)));
		}
#undef IN
#undef OUT
#undef CB
#undef T
#undef P
		add_comment(sigs.v[0], mc, 0);
		addl(&i->c->list, sigs);
	}
	if (!i->c->list.n)
		i->c->hints |= H_CLOSE_NL;
	c->node = dts_export(i);
	return c;
}

static TpCode *generate_client_constructor(TpdFile *f, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	Node *id = tp_defn(c, tp_def(xcat(s->name, "Client")));
	Node *mk = tp_ref(c, G.makeGenericClientConstructor);
	Node *cc = tp_tref(c, G.ChannelCredentials, (NodeList){0});
	Node *co = partial_of(c, G.ClientOptions);
	Node *lit = dts_ttype_lit();
	Node *ctor = dts_new(N_TS_CONSTRUCT_SIGNATURE_DECL);
	ctor->list = L(dts_binding("address", KW("string"), false), dts_binding("credentials", cc, false), dts_binding("options", co, true), NULL);
	ctor->c = dts_type_ann(tref(xcat(s->name, "Client"), NULL));
	dts_ttype_lit_add(lit, ctor);
	dts_ttype_lit_add(lit, dts_prop_sig(dts_id("service"), dts_tquery(ID(xcat(s->name, "Service"))), false, false));
	dts_ttype_lit_add(lit, dts_prop_sig(dts_id("serviceName"), KW("string"), false, false));
	Node *e = dts_as(dts_as(call(mk, ID(xcat(s->name, "Service")), sq(maybe_prefix_package(f, s->name)), NULL), KW("unknown")), lit);
	c->node = dts_export(HINT(dts_var(VAR_CONST, dts_bindingn(id->text, NULL, false), e), H_NO_SEMI));
	return c;
}

static TpCode *generate_grpc_js_service(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *parts[4];
	int n = 0;
	parts[n++] = generate_service_definition(f, si, s);
	parts[n++] = generate_server_stub(si, s);
	if (opt_true(&O.opts, "outputClientImpl")) {
		parts[n++] = generate_client_stub(si, s);
		parts[n++] = generate_client_constructor(f, s);
	}
	TpCode *c = tp_join(parts, n);
	for (int i = 0; i < n; i++) {
		NodeList l = {0};
		if (parts[i]->node)
			dts_add(&l, tp_clone(parts[i]->node));
		for (int k = 0; k < parts[i]->list.n; k++)
			dts_add(&l, tp_clone(parts[i]->list.v[k]));
		if (i > 0 && l.n)
			BLANK(l.v[0]);
		addl(&c->list, l);
	}
	return c;
}

/* ---- generate-generic-service-definition.ts ---- */

/* generateMethodOptions: joinCode([`{`, entries..., `}`], { on: "\n" }) */
static Node *generic_method_options(const TpdMethodOptions *o)
{
	Node *obj = dts_obj();
	bool comma = false;
	if (o) {
		const char *level = o->idempotency_level == 2 ? "'IDEMPOTENT'" : o->idempotency_level == 1 ? "'NO_SIDE_EFFECTS'" : NULL;
		if (level) {
			dts_obj_add(obj, NL(kv("idempotencyLevel", str_raw(level)), LINE));
			comma = true;
		}
		if (o->unknown) {
			bool node = O.env == ENV_NODE;
			Node *u = dts_obj();
			for (int i = 0; i < o->nunknown; i++) {
				const TpdUnknownField *f = &o->unknown[i];
				Node *arr = dts_arr(NULL);
				for (int k = 0; k < f->nvalue; k++) {
					/* `${env == "node" ? "Buffer.from" : "new Uint8Array"}([${value.join(", ")}]) as ...` */
					Node *bytes = dts_arr(NULL);
					for (size_t b = 0; b < f->value[k].n; b++) {
						Node *e = dts_new(N_EXPR_OR_SPREAD);
						e->a = dts_num(xfmt("%d", (unsigned char)f->value[k].p[b]));
						dts_add(&bytes->list, e);
					}
					Node *v = node ? call(MEM(ID("Buffer"), "from"), bytes, NULL) : newx(ID("Uint8Array"), bytes, NULL);
					Node *e = dts_new(N_EXPR_OR_SPREAD);
					e->a = dts_as(v, tref(node ? "Buffer" : "Uint8Array", NULL));
					if (k == 0)
						e->nl = NL_LINE;
					dts_add(&arr->list, e);
				}
				arr->hints |= H_CLOSE_NL;
				dts_obj_add(u, NL(dts_kv(dts_num(xfmt("%lld", (long long)f->tag)), arr), LINE));
			}
			u->hints |= H_TRAILING_COMMA | H_CLOSE_NL;
			dts_obj_add(obj, NL(kv("_unknownFields", u), LINE));
			comma = false;
		}
	}
	obj->hints |= H_CLOSE_NL;
	if (comma)
		obj->hints |= H_TRAILING_COMMA;
	return obj;
}

/* generateMethodDefinition */
static TpCode *generic_method_definition(const TpdMethod *md)
{
	TpCode *c = tp_code(NULL);
	TpCode *in = message_to_type_name(md->input_type, true, false);
	TpCode *out = message_to_type_name(md->output_type, true, false);
	Node *o = dts_obj();
	dts_obj_add(o, NL(kv("name", sq(md->name)), LINE));
	Node *types[2];
	for (int i = 0; i < 2; i++) {
		TpCode *t = i ? out : in;
		Node *e = tp_expr(tp_sub(c, t));
		Node *q = tp_sub(c, t);
		if (q->kind != N_TS_TYPE_REF || q->b)
			fail("unsupported by protoc-gen-uni: the type of %s in a generic service definition", md->name);
		types[i] = dts_as(e, dts_tquery(q->a));
	}
	dts_obj_add(o, NL(kv("requestType", types[0]), LINE));
	dts_obj_add(o, NL(kv("requestStream", dts_bool(md->client_streaming)), LINE));
	dts_obj_add(o, NL(kv("responseType", types[1]), LINE));
	dts_obj_add(o, NL(kv("responseStream", dts_bool(md->server_streaming)), LINE));
	tp_add(c, tp_code(NULL)); /* generateMethodOptions */
	dts_obj_add(o, NL(kv("options", generic_method_options(md->options)), LINE));
	o->hints |= H_CLOSE_NL;
	c->node = o;
	return c;
}

static TpCode *generate_generic_service_definition(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	NodeComment *cm = jsdoc_comment(si->self, s->options && s->options->deprecated, NULL);
	TpDef *name = tp_def(xcat(s->name, "Definition"));
	TpCode *c1 = tp_add(c, tp_code(NULL));
	Node *n1 = tp_defn(c1, name);
	Node *n2 = tp_defn(c1, name);
	Node *t = dts_export(dts_type_alias(n1, NULL, dts_tquery(n2)));
	add_comment(t, cm, 0);
	TpCode *c2 = tp_add(c, tp_code(NULL));
	Node *n3 = tp_defn(c2, name);
	tp_add(c, tp_code(NULL)); /* name, fullName, methods */
	Node *o = dts_obj();
	dts_obj_add(o, NL(kv("name", sq(s->name)), LINE));
	dts_obj_add(o, NL(kv("fullName", sq(maybe_prefix_package(f, s->name))), LINE));
	Node *methods = dts_obj();
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		Node *def = tp_sub(mcode, generic_method_definition(md));
		Node *p = NL(kv(uncapitalize(md->name), def), LINE);
		if (mc) {
			add_comment(p, mc, 0);
			p->nl_after_cm = NL_LINE;
		}
		dts_obj_add(methods, p);
	}
	methods->hints |= H_CLOSE_NL;
	if (methods->list.n)
		methods->hints |= H_TRAILING_COMMA;
	dts_obj_add(o, NL(kv("methods", methods), LINE));
	o->hints |= H_TRAILING_COMMA | H_CLOSE_NL;
	tp_add(c, tp_code(NULL));
	Node *d = dts_export(dts_var(VAR_CONST, dts_bindingn(n3->text, NULL, false), as_const(o)));
	c->list = L(t, d, NULL);
	return c;
}

/* ---- generate-nice-grpc.ts ---- */

/* code`${ctx.utils.DeepPartial}<${type}>` */
static TpCode *nice_deep_partial(TpCode *type)
{
	TpCode *c = tp_code(NULL);
	Node *dp = tp_co(c, C.u->deep_partial);
	c->node = dts_tref_node(dp, L(tp_sub(c, type), NULL));
	return c;
}

/* generateServerStub (server) and generateClientStub */
static TpCode *nice_grpc_stub(SourceInfo *si, TpdService *s, bool server)
{
	TpCode *c = tp_code(NULL);
	TpCode *h = tp_add(c, tp_code(NULL));
	const char *suffix = !server ? "Client" : has_suffix(s->name, "Service") ? "Implementation" : "ServiceImplementation";
	const char *ext = server ? "CallContextExt" : "CallOptionsExt";
	Node *i = dts_interface(tp_defn(h, tp_def(xcat(s->name, suffix))), dts_tparams(dts_tparam(ext, NULL, dts_ttype_lit()), NULL));
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		TpCode *in = message_to_type_name(md->input_type, true, false);
		TpCode *out = message_to_type_name(md->output_type, true, false);
		if (O.output_partial) {
			if (server)
				out = nice_deep_partial(out);
			else
				in = nice_deep_partial(in);
		}
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		Node *req = tp_sub(mcode, in);
		if (md->client_streaming)
			req = tref("AsyncIterable", req, NULL);
		Node *ctx_type = dts_tintersection(server ? tp_tref(mcode, G.NiceCallContext, (NodeList){0}) : tp_tref(mcode, G.NiceCallOptions, (NodeList){0}),
						   tref(ext, NULL), NULL);
		Node *ret;
		if (server && md->server_streaming) {
			Node *r = tp_co(mcode, C.u->server_streaming_method_result);
			ret = dts_tref_node(r, L(tp_sub(mcode, out), NULL));
		} else {
			ret = tref(md->server_streaming ? "AsyncIterable" : "Promise", tp_sub(mcode, out), NULL);
		}
		NodeList params = L(sparam("request", req, false), sparam(server ? "context" : "options", ctx_type, !server), NULL);
		Node *sig = dts_method_sig(dts_id(uncapitalize(md->name)), NULL, params, ret, false);
		add_comment(sig, mc, 0);
		dts_interface_add(i, sig);
	}
	tp_add(c, tp_code(NULL));
	if (!i->c->list.n)
		i->c->hints |= H_CLOSE_NL;
	c->node = dts_export(i);
	return c;
}

static TpCode *generate_nice_grpc_service(SourceInfo *si, TpdService *s)
{
	TpCode *parts[2] = {nice_grpc_stub(si, s, true), nice_grpc_stub(si, s, false)};
	TpCode *c = tp_join(parts, 2);
	dts_add(&c->list, tp_clone(parts[0]->node));
	dts_add(&c->list, BLANK(tp_clone(parts[1]->node)));
	return c;
}

/* ---- generateFile: visitServices ---- */

/* grpc_web.c */
static TpCode *generate_grpc_client_impl(TpdService *s);
static TpCode *generate_grpc_service_desc(TpdFile *f, TpdService *s);
static TpCode *generate_grpc_method_desc(TpdService *s, TpdMethod *md);
static TpCode *add_grpc_web_misc(bool has_streaming);

static int str_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* [...new Set(options.outputServices)].sort(): a repeated key makes nested arrays ([[a, b], c]),
 * which are distinct objects for the Set, sort by their String() and match no service; only
 * strings are kept (as such, other values become "") */
static Vec output_services_sorted(TpdFile *f)
{
	JsVal *os = opt(&O.opts, "outputServices");
	Vec uniq = {0};
	if (os->t == JS_ARRAY) {
		Map seen = {0};
		Vec keys = {0};
		for (int i = 0; i < os->items.n; i++) {
			JsVal *x = os->items.v[i];
			const char *v = js_value_to_string(x);
			if (x->t != JS_ARRAY) {
				const char *k = xfmt("%d:%s", (int)x->t, v);
				if (map_has(&seen, k))
					continue;
				map_put(&seen, k, (void *)1);
			}
			vec_push(&keys, (void *)xcat(v, "\x01", x->t == JS_STRING ? v : ""));
		}
		if (keys.n > 1)
			qsort(keys.v, (size_t)keys.n, sizeof(void *), str_cmp);
		for (int i = 0; i < keys.n; i++)
			vec_push(&uniq, (void *)(strchr(keys.v[i], 1) + 1));
	} else if (f->nservice) {
		fail("TypeError: options.outputServices is not iterable");
	}
	return uniq;
}

static void generate_services(void *outp, TpdFile *f, void *sip)
{
	FileOut *out = outp;
	SourceInfo *si = sip;
	Vec uniq = output_services_sorted(f);
	JsVal *oci = opt(&O.opts, "outputClientImpl");
	bool has_streaming = false, has_server_streaming = false;
	for (int k = 0; k < f->nservice; k++) {
		TpdService *s = f->service[k];
		SourceInfo ssi = si_open(si, FILE_SERVICE, k);
		if (opt_true(&O.opts, "nestJs"))
			generate_nestjs_service(out, f, &ssi, s);
		for (int u = 0; u < uniq.n; u++) {
			const char *svc = uniq.v[u];
			if (str_eq(svc, "grpc-js")) {
				out_chunk(out, generate_grpc_js_service(f, &ssi, s));
			} else if (str_eq(svc, "nice-grpc")) {
				out_chunk(out, generate_nice_grpc_service(&ssi, s));
			} else if (str_eq(svc, "generic-definitions")) {
				out_chunk(out, generate_generic_service_definition(f, &ssi, s));
			} else if (str_eq(svc, "default")) {
				out_chunk(out, generate_service(f, &ssi, s));
				if (oci->t == JS_BOOL && oci->b)
					out_chunk(out, generate_service_client_impl(f, s));
				else if (oci->t == JS_STRING && str_eqc(oci->s, "grpc-web")) {
					out_chunk(out, generate_grpc_client_impl(s));
					out_chunk(out, generate_grpc_service_desc(f, s));
					for (int m = 0; m < s->nmethod; m++) {
						if (!s->method[m]->client_streaming)
							out_chunk(out, generate_grpc_method_desc(s, s->method[m]));
						has_server_streaming |= s->method[m]->server_streaming;
					}
				}
			}
		}
		for (int m = 0; m < s->nmethod; m++)
			has_streaming |= s->method[m]->server_streaming || s->method[m]->client_streaming;
	}
	bool has_default = false;
	for (int u = 0; u < uniq.n; u++)
		has_default |= str_eq(uniq.v[u], "default");
	if (opt_includes(&O.opts, "outputServices", "default") && js_truthy(oci) && f->nservice > 0) {
		if (oci->t == JS_BOOL && oci->b)
			out_chunk(out, generate_rpc_type(has_streaming));
		else if (oci->t == JS_STRING && str_eqc(oci->s, "grpc-web"))
			out_chunk(out, add_grpc_web_misc(has_server_streaming));
	}
	(void)has_default;
	if (dataloaders()) {
		out_chunk(out, generate_data_loader_options_type());
		out_chunk(out, generate_data_loaders_type());
	}
}
