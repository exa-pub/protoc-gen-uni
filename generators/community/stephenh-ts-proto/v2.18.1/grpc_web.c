/* Port of ts-proto v2.18.1 src/generate-grpc-web.ts: the client implementation, service and
 * method descriptors, Rpc type and GrpcWebImpl for @improbable-eng/grpc-web
 * (outputClientImpl=grpc-web).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* the module-level imports of generate-grpc-web.ts (made once per request with the grpc-js ones) */
static struct {
	TpImport *grpc, *share, *take, *BrowserHeaders;
} GW;

COLD static void grpc_web_imports(void)
{
	GW.grpc = tp_imp("grpc@@improbable-eng/grpc-web");
	GW.share = tp_imp("share@rxjs/operators");
	GW.take = tp_imp("take@rxjs/operators");
	GW.BrowserHeaders = tp_imp("BrowserHeaders@browser-headers");
}

static Node *qname(Node *left, const char *right)
{
	Node *q = dts_new(N_TS_QUALIFIED_NAME);
	q->a = left;
	q->b = dts_id(right);
	return q;
}

/* `metadata?: grpc.Metadata` and the like (plain text in the templates, not the import) */
static Node *grpc_t(const char *name) { return tref(xcat("grpc.", name), NULL); }

static bool use_abort_signal(void) { return opt_true(&O.opts, "useAbortSignal"); }

/* generateRpcMethod */
static TpCode *grpc_web_rpc_method(TpdService *s, TpdMethod *md)
{
	TpCode *c = tp_code(NULL);
	TpCode *request_message = request_type(md, false);
	TpCode *input_type = request_type(md, true);
	TpCode *returns = response_promise_or_observable(md);
	const char *name = format_method_name(md->name);
	NodeList params = L(param("request", tp_sub(c, input_type), false, NULL), param("metadata", grpc_t("Metadata"), true, NULL), NULL);
	if (use_abort_signal())
		dts_add(&params, param("abortSignal", tref("AbortSignal", NULL), true, NULL));
	Node *ret = tp_sub(c, returns);
	Node *body;
	if (md->client_streaming) {
		body = block(dts_throw(newx(ID("Error"), SQ("ts-proto does not yet support client streaming!"), NULL)), NULL);
	} else {
		NodeList args = L(ID(xcat(s->name, md->name, "Desc")), mcall(tp_expr(tp_sub(c, request_message)), "fromPartial", ID("request"), NULL),
				  ID("metadata"), NULL);
		if (use_abort_signal())
			dts_add(&args, ID("abortSignal"));
		Node *call_ = dts_callv(dts_member(this_("rpc"), md->server_streaming ? "invoke" : "unary"), NULL, args);
		call_->hints |= H_TRAILING_COMMA;
		body = block(RET(call_), NULL);
	}
	c->node = BLANK(dts_class_method(dts_id(name), dts_function(params, NULL, ret, body, 0), 0, ACC_NONE, METHOD_METHOD));
	return c;
}

/* generateGrpcClientImpl */
static TpCode *generate_grpc_client_impl(TpdService *s)
{
	TpCode *c = tp_code(NULL);
	Node *cls = dts_class(NULL, NULL);
	Node *impl = dts_new(N_TS_EXPR_WITH_TYPE_ARGS);
	impl->a = ID(s->name);
	dts_add(&cls->list3, impl);
	tp_add(c, tp_code(NULL));
	tp_add(c, tp_code(NULL));
	dts_class_add(cls, BLANK(class_prop("rpc", tref("Rpc", NULL))));
	NodeList ctor_body = L(BLANK(ES(SET(this_("rpc"), ID("rpc")))), NULL);
	tp_add(c, tp_code(NULL));
	for (int k = 0; k < s->nmethod; k++) {
		const char *n = format_method_name(s->method[k]->name);
		dts_add(&ctor_body, ES(SET(this_(n), mcall(this_(n), "bind", dts_this(), NULL))));
		tp_add(c, tp_code(NULL));
	}
	tp_add(c, tp_code(NULL));
	dts_class_add(cls, BLANK(dts_constructor(L(param("rpc", tref("Rpc", NULL), false, NULL), NULL), blockl(ctor_body), ACC_NONE)));
	for (int k = 0; k < s->nmethod; k++) {
		TpCode *mc = tp_add(c, grpc_web_rpc_method(s, s->method[k]));
		dts_class_add(cls, tp_clone(mc->node));
	}
	tp_add(c, tp_code(NULL));
	c->node = dts_export(dts_class_decl(ID(xcat(s->name, "ClientImpl")), cls));
	return c;
}

/* generateGrpcServiceDesc */
static TpCode *generate_grpc_service_desc(TpdFile *f, TpdService *s)
{
	Node *o = obj(NL(kv("serviceName", dq(maybe_prefix_package(f, s->name))), LINE), NULL);
	o->hints |= H_TRAILING_COMMA;
	return tp_code(dts_export(const_(xcat(s->name, "Desc"), NULL, o)));
}

/* generateGrpcMethodDesc */
static TpCode *generate_grpc_method_desc(TpdService *s, TpdMethod *md)
{
	TpCode *c = tp_code(NULL);
	TpCode *in = request_type(md, false);
	TpCode *out = response_type(md);
	TpCode *rq = tp_add(c, tp_code(NULL)); /* requestFn */
	Node *ser = mcall(mcall(tp_expr(tp_sub(rq, in)), "encode", dts_this(), NULL), "finish", NULL);
	Node *req_fn = obj(NL(method("serializeBinary", NULL, (NodeList){0}, NULL, block(RET(ser), NULL)), LINE), NULL);
	req_fn->hints |= H_TRAILING_COMMA;
	TpCode *rs = tp_add(c, tp_code(NULL)); /* responseFn */
	Node *value = const_("value", NULL, mcall(tp_expr(tp_sub(rs, out)), "decode", ID("data"), NULL));
	Node *ro = obj(NL(dts_spread(ID("value")), LINE), NL(method("toObject", NULL, (NodeList){0}, NULL, block(RET(ID("value")), NULL)), LINE), NULL);
	ro->hints |= H_TRAILING_COMMA;
	Node *res_fn = obj(NL(method("deserializeBinary", NULL, L(param("data", tref("Uint8Array", NULL), false, NULL), NULL), NULL,
				     block(value, RET(ro), NULL)),
			      LINE),
			   NULL);
	Node *o = obj(NL(kv("methodName", dq(md->name)), LINE), NL(kv("service", ID(xcat(s->name, "Desc"))), LINE),
		      NL(kv("requestStream", dts_bool(false)), LINE), NL(kv("responseStream", dts_bool(md->server_streaming)), LINE),
		      NL(kv("requestType", dts_as(req_fn, KW("any"))), LINE), NL(kv("responseType", dts_as(res_fn, KW("any"))), LINE), NULL);
	o->hints |= H_TRAILING_COMMA;
	c->node = dts_export(const_(xcat(s->name, md->name, "Desc"), tref("UnaryMethodDefinitionish", NULL), o));
	return c;
}

/* `methodDesc: T, request: any, metadata: grpc.Metadata | undefined[, abortSignal?: AbortSignal]` */
static NodeList grpc_web_call_params(const char *request, bool sig)
{
	Node *md = dts_tunion(grpc_t("Metadata"), KW("undefined"), NULL);
	NodeList p = sig ? L(sparam("methodDesc", tref("T", NULL), false), sparam(request, KW("any"), false), sparam("metadata", md, false), NULL)
			 : L(param("methodDesc", tref("T", NULL), false, NULL), param(request, KW("any"), false, NULL), param("metadata", md, false, NULL),
			     NULL);
	if (use_abort_signal())
		dts_add(&p, sig ? sparam("abortSignal", tref("AbortSignal", NULL), true) : param("abortSignal", tref("AbortSignal", NULL), true, NULL));
	return p;
}

static Node *t_extends_unary(void) { return dts_tparams(dts_tparam("T", tref("UnaryMethodDefinitionish", NULL), NULL), NULL); }

/* generateGrpcWebRpcType */
static TpCode *generate_grpc_web_rpc_type(bool has_streaming)
{
	TpCode *c = tp_code(NULL);
	Node *i = dts_interface(ID("Rpc"), NULL);
	tp_add(c, tp_code(NULL));
	TpCode *u = tp_add(c, tp_code(NULL));
	Node *ret;
	if (opt_true(&O.opts, "returnObservable")) {
		Node *w = tp_sub(u, observable_type(false));
		w->b = dts_targs(L(KW("any"), NULL));
		ret = w;
	} else {
		ret = tref("Promise", KW("any"), NULL);
	}
	dts_interface_add(i, dts_method_sig(dts_id("unary"), t_extends_unary(), grpc_web_call_params("request", true), ret, false));
	if (has_streaming) {
		TpCode *v = tp_add(c, tp_code(NULL));
		Node *w = tp_sub(v, observable_type(false));
		w->b = dts_targs(L(KW("any"), NULL));
		dts_interface_add(i, dts_method_sig(dts_id("invoke"), t_extends_unary(), grpc_web_call_params("request", true), w, false));
	}
	tp_add(c, tp_code(NULL));
	c->node = i;
	return c;
}

/* `const request = { ..._request, ...methodDesc.requestType };` and maybeCombinedMetadata */
static void grpc_web_request_stmts(TpCode *c, NodeList *body)
{
	dts_add(body, const_("request", NULL, obj(dts_spread(ID("_request")), dts_spread(MEM(ID("methodDesc"), "requestType")), NULL)));
	Node *test = BIN(LOGICAL_AND, ID("metadata"), MEM(MEM(dts_this(), "options"), "metadata"));
	Node *headers = obj(dts_spread(MEM(dts_opt_member(MEM(dts_this(), "options"), "metadata", true), "headersMap")),
			    dts_spread(dts_opt_member(ID("metadata"), "headersMap", true)), NULL);
	Node *cons = newx(tp_ref(c, GW.BrowserHeaders), headers, NULL);
	Node *alt = BIN(NULLISH, ID("metadata"), MEM(MEM(dts_this(), "options"), "metadata"));
	dts_add(body, const_("maybeCombinedMetadata", NULL, HINT(dts_cond(test, cons, alt), H_OP_NL | H_OP2_NL)));
}

static Node *this_options(const char *p) { return MEM(MEM(dts_this(), "options"), p); }

/* the options of grpc.unary/grpc.invoke common to both */
static Node *grpc_web_debug(void) { return NL(kv("debug", BIN(NULLISH, this_options("debug"), dts_bool(false))), LINE); }

/* createPromiseUnaryMethod and createObservableUnaryMethod */
static TpCode *grpc_web_unary_method(bool observable)
{
	TpCode *c = tp_code(NULL);
	bool abort = use_abort_signal();
	Node *ret;
	if (observable) {
		ret = tp_sub(c, observable_type(false));
		ret->b = dts_targs(L(KW("any"), NULL));
	} else {
		ret = tref("Promise", KW("any"), NULL);
	}
	NodeList body = {0};
	grpc_web_request_stmts(c, &body);
	Node *transport = obj(kv("transport", this_options("transport")), NULL);
	Node *spread = dts_spread(dts_paren(dts_cond(BIN(NOTEQEQ, this_options("transport"), UNDEF()), transport, dts_obj())));
	Node *on_end;
	if (observable) {
		Node *err = const_("err", NULL, newx(tp_co(c, C.u->grpc_web_error), MEM(ID("next"), "statusMessage"), MEM(ID("next"), "status"),
						     MEM(ID("next"), "trailers"), NULL));
		Node *ifs = dts_if(BIN(NOTEQEQ, MEM(ID("next"), "status"), NUM("0")), block(err, ES(mcall(ID("observer"), "error", ID("err"), NULL)), NULL),
				   block(ES(mcall(ID("observer"), "next", dts_as(MEM(ID("next"), "message"), KW("any")), NULL)),
					 ES(mcall(ID("observer"), "complete", NULL)), NULL));
		on_end = arrow(L(dts_binding("next", NULL, false), NULL), NULL, block(ifs, NULL));
	} else {
		Node *err = const_("err", NULL, newx(tp_co(c, C.u->grpc_web_error), MEM(ID("response"), "statusMessage"), MEM(ID("response"), "status"),
						     MEM(ID("response"), "trailers"), NULL));
		Node *ok = BIN(EQEQEQ, MEM(ID("response"), "status"), MEM(MEM(ID("grpc"), "Code"), "OK"));
		Node *ifs = dts_if(ok, block(ES(call(ID("resolve"), mcall(dts_nonnull(MEM(ID("response"), "message")), "toObject", NULL), NULL)), NULL),
				   block(err, ES(call(ID("reject"), ID("err"), NULL)), NULL));
		on_end = dts_fn_expr(NULL, dts_function(L(param("response", NULL, false, NULL), NULL), NULL, NULL, block(ifs, NULL), 0));
	}
	Node *opts = obj(NL(shorthand("request"), LINE), NL(kv("host", MEM(dts_this(), "host")), LINE),
			 NL(kv("metadata", BIN(NULLISH, ID("maybeCombinedMetadata"), dts_obj())), LINE), NL(spread, LINE), grpc_web_debug(),
			 NL(kv("onEnd", on_end), LINE), NULL);
	opts->hints |= H_TRAILING_COMMA;
	Node *unary = mcall(tp_ref(c, GW.grpc), "unary", ID("methodDesc"), opts, NULL);
	NodeList inner = L(abort ? const_("client", NULL, unary) : ES(unary), NULL);
	if (abort) {
		Node *listener;
		if (observable)
			listener = block(ES(mcall(ID("observer"), "error", MEM(ID("abortSignal"), "reason"), NULL)), ES(mcall(ID("client"), "close", NULL)), NULL);
		else
			listener = block(ES(mcall(ID("client"), "close", NULL)), ES(call(ID("reject"), MEM(ID("abortSignal"), "reason"), NULL)), NULL);
		Node *add = mcall(ID("abortSignal"), "addEventListener", DQ("abort"), arrow((NodeList){0}, NULL, listener), NULL);
		dts_add(&inner, BLANK(dts_if(ID("abortSignal"), ES(add), NULL)));
	}
	Node *e;
	if (observable) {
		Node *a = HINT(arrow1("observer", blockl(inner)), H_NO_PARENS);
		Node *o = newx(ID("Observable"), a, NULL);
		e = mcall(o, "pipe", call(tp_ref(c, GW.take), NUM("1"), NULL), NULL);
	} else {
		Node *a = arrow(L(dts_binding("resolve", NULL, false), dts_binding("reject", NULL, false), NULL), NULL, blockl(inner));
		e = newx(ID("Promise"), a, NULL);
	}
	dts_add(&body, RET(e));
	c->node = BLANK(dts_class_method(dts_id("unary"), dts_function(grpc_web_call_params("_request", false), t_extends_unary(), ret, blockl(body), 0), 0,
					 ACC_NONE, METHOD_METHOD));
	return c;
}

/* createInvokeMethod */
static TpCode *grpc_web_invoke_method(void)
{
	TpCode *c = tp_code(NULL);
	bool abort = use_abort_signal();
	Node *ret = tp_sub(c, observable_type(false));
	ret->b = dts_targs(L(KW("any"), NULL));
	NodeList body = L(const_("upStreamCodes", NULL, BIN(NULLISH, this_options("upStreamRetryCodes"), dts_arr(NULL))),
			  const_("DEFAULT_TIMEOUT_TIME", KW("number"), NUM("3_000")), NULL);
	NodeList rq = {0};
	grpc_web_request_stmts(c, &rq);
	dts_add(&body, rq.v[0]);
	dts_add(&body, const_("transport", NULL, BIN(NULLISH, this_options("streamingTransport"), this_options("transport"))));
	dts_add(&body, rq.v[1]);
	Node *spread = dts_spread(dts_paren(dts_cond(BIN(NOTEQEQ, ID("transport"), UNDEF()), obj(shorthand("transport"), NULL), dts_obj())));
	Node *grpc1 = tp_ref(c, GW.grpc);
	Node *code_t = dts_tref_node(qname(tp_entity(c, GW.grpc), "Code"), (NodeList){0});
	Node *meta_t = dts_tref_node(qname(tp_entity(c, GW.grpc), "Metadata"), (NodeList){0});
	Node *err_stmts = block(const_("err", NULL, dts_as(newx(ID("Error"), ID("message"), NULL), KW("any"))),
				ES(SET(MEM(ID("err"), "code"), ID("code"))), ES(SET(MEM(ID("err"), "metadata"), ID("trailers"))),
				ES(mcall(ID("observer"), "error", ID("err"), NULL)), NULL);
	Node *ifs = dts_if(BIN(EQEQEQ, ID("code"), NUM("0")), block(ES(mcall(ID("observer"), "complete", NULL)), NULL),
			   dts_if(mcall(ID("upStreamCodes"), "includes", ID("code"), NULL),
				  block(ES(call(ID("setTimeout"), ID("upStream"), ID("DEFAULT_TIMEOUT_TIME"), NULL)), NULL), err_stmts));
	Node *on_end = arrow(L(dts_binding("code", code_t, false), dts_binding("message", KW("string"), false), dts_binding("trailers", meta_t, false), NULL),
			     NULL, block(ifs, NULL));
	Node *opts = obj(NL(kv("host", MEM(dts_this(), "host")), LINE), NL(shorthand("request"), LINE), NL(spread, LINE),
			 NL(kv("metadata", BIN(NULLISH, ID("maybeCombinedMetadata"), dts_obj())), LINE), grpc_web_debug(),
			 NL(kv("onMessage", arrow(L(dts_binding("next", NULL, false), NULL), NULL, mcall(ID("observer"), "next", ID("next"), NULL))), LINE),
			 NL(kv("onEnd", on_end), LINE), NULL);
	opts->hints |= H_TRAILING_COMMA;
	NodeList up = L(const_("client", NULL, mcall(grpc1, "invoke", ID("methodDesc"), opts, NULL)), NULL);
	Node *close_arrow = arrow((NodeList){0}, NULL, mcall(ID("client"), "close", NULL));
	if (abort) {
		Node *abort_fn = const_("abort", NULL,
					arrow((NodeList){0}, NULL,
					      block(ES(mcall(ID("observer"), "error", MEM(ID("abortSignal"), "reason"), NULL)), ES(mcall(ID("client"), "close", NULL)),
						    NULL)));
		Node *cleanup = block(dts_if(MEM(ID("abortSignal"), "aborted"), block(dts_return(NULL), NULL), NULL),
				      BLANK(ES(mcall(ID("abortSignal"), "removeEventListener", SQ("abort"), ID("abort"), NULL))),
				      ES(mcall(ID("client"), "close", NULL)), NULL);
		Node *then = block(abort_fn, ES(mcall(ID("abortSignal"), "addEventListener", DQ("abort"), ID("abort"), NULL)),
				   ES(mcall(ID("observer"), "add", arrow((NodeList){0}, NULL, cleanup), NULL)), NULL);
		dts_add(&up, BLANK(dts_if(ID("abortSignal"), then, block(ES(mcall(ID("observer"), "add", close_arrow, NULL)), NULL))));
	} else {
		dts_add(&up, ES(mcall(ID("observer"), "add", close_arrow, NULL)));
	}
	Node *up_stream = const_("upStream", NULL, dts_paren(arrow((NodeList){0}, NULL, blockl(up))));
	Node *a = HINT(arrow1("observer", block(up_stream, ES(call(ID("upStream"), NULL)), NULL)), H_NO_PARENS);
	Node *e = mcall(newx(ID("Observable"), a, NULL), "pipe", call(tp_ref(c, GW.share), NULL), NULL);
	dts_add(&body, RET(e));
	c->node = BLANK(dts_class_method(dts_id("invoke"), dts_function(grpc_web_call_params("_request", false), t_extends_unary(), ret, blockl(body), 0), 0,
					 ACC_NONE, METHOD_METHOD));
	return c;
}

/* the `options` type literal of generateGrpcWebImpl */
static Node *grpc_web_options_type(bool has_streaming)
{
	Node *lit = dts_ttype_lit();
	Node *nums = dts_tarray(KW("number"));
	Node *members[5] = {dts_prop_sig(dts_id("transport"), grpc_t("TransportFactory"), true, false),
			    has_streaming ? dts_prop_sig(dts_id("streamingTransport"), grpc_t("TransportFactory"), true, false) : NULL,
			    dts_prop_sig(dts_id("debug"), KW("boolean"), true, false), dts_prop_sig(dts_id("metadata"), grpc_t("Metadata"), true, false),
			    dts_prop_sig(dts_id("upStreamRetryCodes"), nums, true, false)};
	for (int i = 0; i < 5; i++) {
		if (!members[i])
			continue;
		members[i]->nl = i == 2 && !has_streaming ? NL_BLANK : NL_LINE;
		dts_ttype_lit_add(lit, HINT(members[i], H_SEP_COMMA));
	}
	lit->hints |= H_TRAILING_COMMA;
	return lit;
}

/* generateGrpcWebImpl */
static TpCode *generate_grpc_web_impl(bool has_streaming)
{
	TpCode *c = tp_code(NULL);
	TpCode *head = tp_add(c, tp_code(NULL));
	TpCode *ot = tp_add(head, tp_code(NULL)); /* options (twice) */
	tp_add(head, ot);
	Node *cls = dts_class(NULL, NULL);
	dts_class_add(cls, dts_class_prop(dts_id("host"), KW("string"), NULL, 0, ACC_PRIVATE));
	dts_class_add(cls, dts_class_prop(dts_id("options"), grpc_web_options_type(has_streaming), NULL, 0, ACC_PRIVATE));
	Node *ctor_body = block(ES(SET(this_("host"), ID("host"))), ES(SET(this_("options"), ID("options"))), NULL);
	dts_class_add(cls, BLANK(dts_constructor(L(param("host", KW("string"), false, NULL), param("options", grpc_web_options_type(has_streaming), false, NULL), NULL),
						 ctor_body, ACC_NONE)));
	TpCode *u = tp_add(c, grpc_web_unary_method(opt_true(&O.opts, "returnObservable")));
	dts_class_add(cls, tp_clone(u->node));
	if (has_streaming) {
		TpCode *iv = tp_add(c, grpc_web_invoke_method());
		dts_class_add(cls, tp_clone(iv->node));
	}
	tp_add(c, tp_code(NULL));
	c->node = dts_export(dts_class_decl(ID("GrpcWebImpl"), cls));
	return c;
}

/* addGrpcWebMisc */
static TpCode *add_grpc_web_misc(bool has_streaming)
{
	TpCode *parts[4];
	TpCode *p0 = tp_code(NULL);
	Node *i = dts_interface(ID("UnaryMethodDefinitionishR"), NULL);
	Node *ext = dts_new(N_TS_EXPR_WITH_TYPE_ARGS);
	ext->a = MEM(tp_ref(p0, GW.grpc), "UnaryMethodDefinition");
	ext->b = dts_targs(L(KW("any"), KW("any"), NULL));
	dts_add(&i->list, ext);
	dts_interface_add(i, NL(prop_sig("requestStream", KW("any"), false, false), SAME));
	dts_interface_add(i, NL(prop_sig("responseStream", KW("any"), false, false), SAME));
	p0->node = i;
	parts[0] = p0;
	parts[1] = tp_code(dts_type_alias(ID("UnaryMethodDefinitionish"), NULL, tref("UnaryMethodDefinitionishR", NULL)));
	parts[2] = generate_grpc_web_rpc_type(has_streaming);
	parts[3] = generate_grpc_web_impl(has_streaming);
	TpCode *c = tp_join(parts, 4);
	for (int k = 0; k < 4; k++)
		dts_add(&c->list, k ? BLANK(tp_clone(parts[k]->node)) : tp_clone(parts[k]->node));
	return c;
}
