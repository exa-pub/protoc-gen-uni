/* Port of ts-proto v2.18.1 src/generate-nestjs.ts: the NestJS client and controller interfaces and
 * the controller methods decorator, plus the nestJs parts of generateFile (src/main.ts).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* the members of a joinCode(chunks, { on: "\n\n" }) interface: comment chunks and members, each
 * after a blank line */
static void nest_member(Node *iface, Node *sig, NodeComment *cm)
{
	sig->nl = NL_BLANK;
	if (cm) {
		dts_comment(sig, cm->text, true, 0);
		sig->nl_after_cm = NL_BLANK;
	}
	dts_interface_add(iface, sig);
}

static NodeList nest_params(TpCode *mcode, const TpdMethod *md)
{
	NodeList params = {0};
	TpCode *pj = tp_add(mcode, tp_code(NULL)); /* joinCode(params) */
	if (ctx_opt()) {
		tp_add(pj, tp_code(NULL));
		dts_add(&params, sparam("ctx", tref("Context", NULL), false));
	}
	TpCode *rc = tp_add(pj, tp_code(NULL));
	dts_add(&params, sparam("request", tp_sub(rc, request_type(md, false)), false));
	bool rest = opt_true(&O.opts, "addNestjsRestParameter");
	if (opt_true(&O.opts, "addGrpcMetadata")) {
		TpCode *mdc = tp_add(pj, tp_code(NULL));
		dts_add(&params, sparam("metadata", tp_tref(mdc, tp_imp("t:Metadata@@grpc/grpc-js"), (NodeList){0}), !rest));
	}
	if (rest) {
		tp_add(pj, tp_code(NULL));
		dts_add(&params, dts_rest_pat(dts_binding("rest", NULL, false), KW("any")));
	}
	return params;
}

/* generateNestjsServiceController */
/* the batch method of a NestJS interface (controller: typed parameter returning the output type) */
static void nest_batch_member(TpCode *c, Node *i, TpdFile *f, TpdService *s, TpdMethod *md, bool controller)
{
	BatchMethod bm;
	if (!dataloaders() || !detect_batch_method(f, s, md, &bm))
		return;
	TpCode *bc = tp_add(c, tp_code(NULL));
	NodeList ps = L(sparam("ctx", tref("Context", NULL), false), NULL);
	Node *in = tp_sub(bc, bm.input_type);
	dts_add(&ps, sparam(singular(bm.input_field_name), controller ? in : NULL, false));
	Node *ret = tref("Promise", controller ? tp_sub(bc, bm.output_type) : tp_clone(in), NULL);
	nest_member(i, dts_method_sig(dts_id(bm.single_method_name), NULL, ps, ret, false), NULL);
}

static TpCode *generate_nestjs_service_controller(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	bool deprecated = s->options && s->options->deprecated;
	NodeComment *cm = jsdoc_comment(si->self, deprecated, NULL);
	tp_add(c, tp_code(NULL));
	Node *i = dts_interface(ID(xcat(s->name, "Controller")), ctx_opt() ? context_type_params() : NULL);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		/* upstream passes the service's deprecation */
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), deprecated, NULL);
		TpCode *mcode = tp_add(c, tp_code(NULL));
		NodeList params = nest_params(mcode, md);
		Node *ret;
		if (str_eq(md->output_type, ".google.protobuf.Empty")) {
			tp_add(mcode, tp_code(NULL));
			ret = dts_tunion(KW("void"), tref("Promise", KW("void"), NULL), NULL);
		} else if (opt_true(&O.opts, "returnObservable") || md->server_streaming) {
			TpCode *r = tp_add(mcode, tp_code(NULL));
			ret = tp_sub(r, observable_of(response_type(md), false));
		} else {
			TpCode *r = tp_add(mcode, tp_code(NULL));
			TpCode *p = tp_code(NULL);
			p->node = tref("Promise", tp_sub(p, response_type(md)), NULL);
			Node *t1 = tp_sub(r, p);
			Node *t2 = tp_sub(r, observable_of(response_type(md), false));
			Node *t3 = tp_sub(r, response_type(md));
			ret = dts_tunion(t1, t2, t3, NULL);
		}
		Node *sig = dts_method_sig(dts_id(format_method_name(md->name)), NULL, params, ret, false);
		nest_member(i, sig, mc);
		nest_batch_member(c, i, f, s, md, true);
	}
	tp_add(c, tp_code(NULL));
	i->c->hints |= H_CLOSE_NL;
	Node *decl = dts_export(i);
	if (cm) {
		add_comment(decl, cm, 0);
		decl->nl_after_cm = NL_BLANK;
	}
	c->node = decl;
	return c;
}

/* generateNestjsServiceClient */
static TpCode *generate_nestjs_service_client(TpdFile *f, SourceInfo *si, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	NodeComment *cm = jsdoc_comment(si->self, false, NULL);
	tp_add(c, tp_code(NULL));
	Node *i = dts_interface(ID(xcat(s->name, "Client")), ctx_opt() ? context_type_params() : NULL);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		TpCode *mcode = tp_add(c, tp_code(NULL));
		NodeList params = nest_params(mcode, md);
		TpCode *r = tp_add(mcode, tp_code(NULL));
		Node *ret = tp_sub(r, observable_of(response_type(md), false));
		NodeComment *mc = jsdoc_comment(si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		Node *sig = dts_method_sig(dts_id(format_method_name(md->name)), NULL, params, ret, false);
		nest_member(i, sig, mc);
		nest_batch_member(c, i, f, s, md, false);
	}
	tp_add(c, tp_code(NULL));
	i->c->hints |= H_CLOSE_NL;
	Node *decl = dts_export(i);
	if (cm) {
		add_comment(decl, cm, 0);
		decl->nl_after_cm = NL_BLANK;
	}
	c->node = decl;
	return c;
}

/* `const descriptor: any = ...; Method('svc', method)(...); Object.defineProperty(...);` loop */
static Node *nest_methods_loop(TpCode *c, const char *list, TpImport *decorator, const char *service_name)
{
	Node *proto = MEM(ID("constructor"), "prototype");
	Node *d = const_("descriptor", KW("any"), mcall(ID("Reflect"), "getOwnPropertyDescriptor", MEM(ID("constructor"), "prototype"), ID("method"), NULL));
	Node *dec = call(call(tp_ref(c, decorator), sq(service_name), ID("method"), NULL), dts_index(proto, ID("method")), ID("method"),
			 ID("descriptor"), NULL);
	Node *def = mcall(ID("Object"), "defineProperty", MEM(ID("constructor"), "prototype"), ID("method"), ID("descriptor"), NULL);
	Node *loop = dts_for_of(dts_var_decl(VAR_CONST), ID(list), block(d, ES(dec), ES(def), NULL), false);
	dts_add(&loop->a->list, dts_declarator(dts_binding("method", NULL, false), NULL));
	return loop;
}

/* generateNestjsGrpcServiceMethodsDecorator */
static TpCode *generate_nestjs_grpc_service_methods_decorator(TpdFile *f, TpdService *s)
{
	TpCode *c = tp_code(NULL);
	TpImport *grpc_method = tp_imp("GrpcMethod@@nestjs/microservices");
	TpImport *grpc_stream_method = tp_imp("GrpcStreamMethod@@nestjs/microservices");
	const char *service_name = opt_true(&O.opts, "useNestjsFqnServiceName") ? maybe_prefix_package(f, s->name) : s->name;
	Node *lists[2];
	for (int stream = 0; stream < 2; stream++) {
		Node *a = dts_arr(NULL);
		for (int k = 0; k < s->nmethod; k++) {
			if (s->method[k]->client_streaming != (stream == 1))
				continue;
			Node *e = dts_new(N_EXPR_OR_SPREAD);
			e->a = dq(format_method_name(s->method[k]->name));
			dts_add(&a->list, e);
		}
		lists[stream] = a;
	}
	Node *body = block(const_("grpcMethods", dts_tarray(KW("string")), lists[0]), nest_methods_loop(c, "grpcMethods", grpc_method, service_name),
			   const_("grpcStreamMethods", dts_tarray(KW("string")), lists[1]),
			   nest_methods_loop(c, "grpcStreamMethods", grpc_stream_method, service_name), NULL);
	Node *fn = dts_fn_expr(NULL, dts_function(L(param("constructor", tref("Function", NULL), false, NULL), NULL), NULL, NULL, body, 0));
	c->node = dts_export(fn_decl(xcat(s->name, "ControllerMethods"), NULL, (NodeList){0}, NULL, block(RET(fn), NULL)));
	return c;
}

/* the nestJs part of visitServices */
static void generate_nestjs_service(FileOut *out, TpdFile *f, SourceInfo *ssi, TpdService *s)
{
	out_chunk(out, generate_nestjs_service_client(f, ssi, s));
	out_chunk(out, generate_nestjs_service_controller(f, ssi, s));
	out_chunk(out, generate_nestjs_grpc_service_methods_decorator(f, s));
	char *snake = camel_to_snake(s->name);
	Buf lower = {0};
	for (const char *p = s->name; *p; p++)
		buf_addc(&lower, (char)tolower((unsigned char)*p));
	const char *name = xcat(snake, has_suffix(buf_str(&lower), "service") ? "_NAME" : "_SERVICE_NAME");
	out_chunk(out, tp_code(dts_export(const_(name, NULL, dq(s->name)))));
}

/* ---- main.ts: the nestJs chunks of generateFile ---- */

static bool has_field(const TpdMessage *m, bool (*pred)(const TpdField *), int depth)
{
	if (depth > 100)
		fail("Maximum call stack size exceeded");
	for (int i = 0; i < m->nfield; i++)
		if (pred(m->field[i]))
			return true;
	for (int i = 0; i < m->nnested_type; i++)
		if (has_field(m->nested_type[i], pred, depth + 1))
			return true;
	return false;
}

static bool file_has_field(const TpdFile *f, bool (*pred)(const TpdField *))
{
	for (int i = 0; i < f->nmessage_type; i++)
		if (has_field(f->message_type[i], pred, 0))
			return true;
	return false;
}

/* `${wrappers}['.google.protobuf.X'] = { ... } as any;` */
static TpCode *protobuf_wrapper(const char *type, Node *(*value)(TpCode *c))
{
	TpCode *c = tp_code(NULL);
	Node *w = tp_ref(c, tp_imp("wrappers@protobufjs"));
	Node *v = value(c);
	c->node = ES(SET(dts_index(w, sq(xcat(".google.protobuf.", type))), dts_as(v, KW("any"))));
	return c;
}

static Node *timestamp_wrapper_value(TpCode *c)
{
	(void)c;
	Node *get_time = mcall(ID("value"), "getTime", NULL);
	Node *from = obj(NL(kv("seconds", BIN(DIV, get_time, NUM("1000"))), LINE),
			 NL(kv("nanos", BIN(MUL, dts_paren(BIN(MOD, mcall(ID("value"), "getTime", NULL), NUM("1000"))), NUM("1e6"))), LINE), NULL);
	from->hints |= H_TRAILING_COMMA;
	Node *lit = dts_ttype_lit();
	dts_ttype_lit_add(lit, prop_sig("seconds", KW("number"), false, false));
	dts_ttype_lit_add(lit, HINT(prop_sig("nanos", KW("number"), false, false), H_NO_SEMI));
	Node *to = newx(ID("Date"), BIN(ADD, BIN(MUL, MEM(ID("message"), "seconds"), NUM("1000")), BIN(DIV, MEM(ID("message"), "nanos"), NUM("1e6"))), NULL);
	Node *o = obj(NL(method("fromObject", NULL, L(param("value", tref("Date", NULL), false, NULL), NULL), NULL, block(RET(from), NULL)), LINE),
		      NL(method("toObject", NULL, L(param("message", lit, false, NULL), NULL), NULL, block(RET(to), NULL)), LINE), NULL);
	o->hints |= H_TRAILING_COMMA;
	return o;
}

static Node *struct_wrapper_value(TpCode *c)
{
	TpImport *st = imp_proto("google/protobuf/struct", wrap_type_name("Struct", false));
	Node *from = MEM(tp_ref(c, st), "wrap");
	Node *to = MEM(tp_ref(c, st), "unwrap");
	Node *o = obj(NL(kv("fromObject", from), LINE), NL(kv("toObject", to), LINE), NULL);
	o->hints |= H_TRAILING_COMMA;
	return o;
}
