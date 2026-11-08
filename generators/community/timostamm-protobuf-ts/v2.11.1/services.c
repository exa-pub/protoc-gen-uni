/* Copyright (c) 2016 Timo Stamm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/plugin/src/code-gen: service-type-generator.ts, method-info-generator.ts,
 * service-client-generator-base.ts, service-client-generator-generic.ts,
 * service-client-generator-grpc.ts, service-server-generator-generic.ts and
 * service-server-generator-grpc.ts. Imports are added in upstream's evaluation order. */

static DescMethod *find_method(DescService *s, const MethodInfo *mi) { return s->methods.v[mi->index]; }

/* ---- service-type-generator.ts, method-info-generator.ts ---- */

static void service_type_register_symbols(OutFile *f, DescService *s)
{
	symbols_register(&G.symbols, create_local_type_name(&s->h), &s->h, f, "default");
}

static TsNode *create_method_info_literal(OutFile *f, const MethodInfo *mi)
{
	TsNodeArray *props = ts_list(PROP("name", STR(mi->name)));
	if (!str_eq(mi->local_name, rt_lower_camel_case(mi->name)))
		ts_list_push(props, PROP("localName", STR(mi->local_name)));
	if (mi->idempotency)
		ts_list_push(props, PROP("idempotency", STR(mi->idempotency)));
	if (mi->server_streaming)
		ts_list_push(props, PROP("serverStreaming", ts_create_true()));
	if (mi->client_streaming)
		ts_list_push(props, PROP("clientStreaming", ts_create_true()));
	ts_list_push(props, PROP("options", literal_from_value(mi->options)));
	ts_list_push(props, PROP("I", ID(import_type(&G.imports, f, &mi->I->h, "default", false))));
	ts_list_push(props, PROP("O", ID(import_type(&G.imports, f, &mi->O->h, "default", false))));
	return ts_create_object_literal(props, false);
}

static void generate_service_type(OutFile *f, DescService *s)
{
	const char *my_service = import_type(&G.imports, f, &s->h, "default", false);
	const char *service_type = import_name(f, "ServiceType", G.o->runtime_rpc_import_path, false);
	ServiceType *t = get_service_type(&G.interp, s);
	TsNodeArray *args = ts_list(STR(t->type_name));
	TsNodeArray *infos = ts_list_new();
	for (int i = 0; i < t->n_methods; i++)
		ts_list_push(infos, create_method_info_literal(f, t->methods[i]));
	ts_list_push(args, ts_create_array_literal(infos, true));
	Str *keys;
	if (js_obj_keys(t->options, &keys))
		ts_list_push(args, literal_from_value(t->options));
	TsNode *st = ts_create_variable_statement(
		ts_list(TOK(TS_ExportKeyword)),
		ts_create_variable_declaration_list(ts_list(VAR(my_service, NULL, ts_create_new(ID(service_type), NULL, args))),
						    TS_NodeFlags_Const));
	add_statement(f, st, false);
	Buf c = {0};
	buf_adds(&c, make_deprecated_tag(&s->h));
	buf_adds(&c, "@generated ServiceType for ");
	Str tag = make_generated_tag(&s->h);
	buf_add(&c, tag.p + 16, tag.n - 16);
	add_comment_block_as_js_doc(st, buf_view(&c));
}

/* ---- service-client-generator-base.ts ---- */

typedef struct ClientGenerator ClientGenerator;
struct ClientGenerator {
	const char *kind_interface, *kind_implementation;
	/* createUnary, createServerStreaming, createClientStreaming, createDuplexStreaming */
	TsNode *(*create_method[4])(OutFile *f, DescService *s, const MethodInfo *mi);
	/* the signatures of the interface (protected create*Signatures) */
	TsNodeArray *(*create_signatures[4])(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi);
	void (*generate_implementation_class)(const ClientGenerator *g, OutFile *f, DescService *s);
};

static int method_style(const MethodInfo *mi)
{
	if (mi->server_streaming && mi->client_streaming)
		return 3;
	if (mi->server_streaming)
		return 1;
	if (mi->client_streaming)
		return 2;
	return 0;
}

static void client_register_symbols(const ClientGenerator *g, OutFile *f, DescService *s)
{
	const char *base = create_local_type_name(&s->h);
	symbols_register(&G.symbols, xcat("I", base, "Client"), &s->h, f, g->kind_interface);
	symbols_register(&G.symbols, xcat(base, "Client"), &s->h, f, g->kind_implementation);
}

/* the signature of the method that create* builds (createMethodSignature from the declaration) */
static TsNodeArray *base_signatures(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi)
{
	TsNode *m = g->create_method[method_style(mi)](f, s, mi);
	return ts_list(ts_create_method_signature(m->type_parameters, m->parameters, m->type, m->name, m->question_token));
}

static void client_generate_interface(const ClientGenerator *g, OutFile *f, DescService *s)
{
	ServiceType *t = get_service_type(&G.interp, s);
	const char *name = import_type(&G.imports, f, &s->h, g->kind_interface, false);
	TsNodeArray *sigs = ts_list_new();
	for (int i = 0; i < t->n_methods; i++) {
		const MethodInfo *mi = t->methods[i];
		TsNodeArray *sig = g->create_signatures[method_style(mi)](g, f, s, mi);
		if (sig->n)
			add_comments_for_descriptor(sig->v[0], &find_method(s, mi)->h, APPEND_TO_LEADING_BLOCK);
		push_all(sigs, sig);
	}
	TsNode *st = ts_create_interface_declaration(NULL, ts_list(TOK(TS_ExportKeyword)), ID(name), NULL, NULL, sigs);
	add_comments_for_descriptor(st, &s->h, APPEND_TO_LEADING_BLOCK);
	add_statement(f, st, false);
}

/* makeI / makeO */
static TsNode *make_i(OutFile *f, const MethodInfo *mi) { return TREF(import_type(&G.imports, f, &mi->I->h, "default", true)); }
static TsNode *make_o(OutFile *f, const MethodInfo *mi) { return TREF(import_type(&G.imports, f, &mi->O->h, "default", true)); }

/* the generic client's generateImplementationClass (of the base class) */
static void generic_generate_implementation_class(const ClientGenerator *g, OutFile *f, DescService *s)
{
	ServiceType *t = get_service_type(&G.interp, s);
	const char *service_type = import_type(&G.imports, f, &s->h, "default", false);
	const char *client = import_type(&G.imports, f, &s->h, g->kind_implementation, false);
	const char *iclient = import_type(&G.imports, f, &s->h, g->kind_interface, false);
	const char *service_info = import_name(f, "ServiceInfo", G.o->runtime_rpc_import_path, true);
	const char *rpc_transport = import_name(f, "RpcTransport", G.o->runtime_rpc_import_path, true);
	TsNodeArray *members = ts_list(
		ts_create_property(NULL, NULL, ID("typeName"), NULL, NULL, PA(ID(service_type), "typeName")),
		ts_create_property(NULL, NULL, ID("methods"), NULL, NULL, PA(ID(service_type), "methods")),
		ts_create_property(NULL, NULL, ID("options"), NULL, NULL, PA(ID(service_type), "options")),
		ts_create_constructor(NULL, NULL,
				      ts_list(ts_create_parameter(ts_list_new(), ts_list(TOK(TS_PrivateKeyword), TOK(TS_ReadonlyKeyword)),
								  NULL, ID("_transport"), NULL, TREF(rpc_transport), NULL)),
				      ts_create_block(ts_list_new(), true)));
	for (int i = 0; i < t->n_methods; i++) {
		const MethodInfo *mi = t->methods[i];
		TsNode *decl = g->create_method[method_style(mi)](f, s, mi);
		add_comments_for_descriptor(decl, &find_method(s, mi)->h, APPEND_TO_LEADING_BLOCK);
		ts_list_push(members, decl);
	}
	TsNode *st = ts_create_class_declaration(
		ts_list_new(), ts_list(TOK(TS_ExportKeyword)), ID(client), NULL,
		ts_list(ts_create_heritage_clause(TS_ImplementsKeyword,
						  ts_list(ts_create_expression_with_type_arguments(NULL, ID(iclient)),
							  ts_create_expression_with_type_arguments(NULL, ID(service_info))))),
		members);
	add_statement(f, st, false);
	add_comments_for_descriptor(st, &s->h, APPEND_TO_LEADING_BLOCK);
}

/* ---- service-client-generator-generic.ts ---- */

static TsNode *generic_client_method(OutFile *f, DescService *s, const MethodInfo *mi, const char *call_type,
				     const char *style, bool has_input)
{
	(void)s;
	const char *rpc_options = import_name(f, "RpcOptions", G.o->runtime_rpc_import_path, true);
	const char *call = import_name(f, call_type, G.o->runtime_rpc_import_path, true);
	TsNodeArray *params = ts_list_new();
	if (has_input)
		ts_list_push(params, PARAM("input", NULL, make_i(f, mi)));
	ts_list_push(params, PARAM("options", TOK(TS_QuestionToken), TREF(rpc_options)));
	TsNode *ri = make_i(f, mi);
	TsNode *ro = make_o(f, mi);
	TsNode *ret_type = ts_create_type_reference_node(ID(call), ts_list(ri, ro));
	TsNode *vars = VARS(TS_NodeFlags_Const,
			    ts_list(VAR("method", NULL, ts_create_element_access(PA(THIS(), "methods"), NUMI(mi->index))),
				    VAR("opt", NULL, CALL(PA(PA(THIS(), "_transport"), "mergeOptions"), ts_list(ID("options"))))));
	const char *intercept = import_name(f, "stackIntercept", G.o->runtime_rpc_import_path, false);
	TsNode *ti = make_i(f, mi);
	TsNode *to = make_o(f, mi);
	TsNodeArray *args = ts_list(STR(style), PA(THIS(), "_transport"), ID("method"), ID("opt"));
	if (has_input)
		ts_list_push(args, ID("input"));
	TsNode *ret = ts_create_return(ts_create_call(ID(intercept), ts_list(ti, to), args));
	return ts_create_method(NULL, NULL, NULL, ID(mi->local_name), NULL, NULL, params, ret_type,
				ts_create_block(ts_list(vars, ret), true));
}

static TsNode *generic_unary(OutFile *f, DescService *s, const MethodInfo *mi)
{
	return generic_client_method(f, s, mi, "UnaryCall", "unary", true);
}
static TsNode *generic_server_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	return generic_client_method(f, s, mi, "ServerStreamingCall", "serverStreaming", true);
}
static TsNode *generic_client_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	return generic_client_method(f, s, mi, "ClientStreamingCall", "clientStreaming", false);
}
static TsNode *generic_duplex_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	return generic_client_method(f, s, mi, "DuplexStreamingCall", "duplex", false);
}

static const ClientGenerator client_generic = {
	"call-client-interface",
	"call-client",
	{generic_unary, generic_server_streaming, generic_client_streaming, generic_duplex_streaming},
	{base_signatures, base_signatures, base_signatures, base_signatures},
	generic_generate_implementation_class,
};

/* ---- service-client-generator-grpc.ts ---- */

static const char *grpc_ns(OutFile *f, bool type_only) { return import_namespace(f, "grpc", "@grpc/grpc-js", type_only); }

/* grpc.<name> as a type */
static TsNode *grpc_type(const char *grpc, const char *name, TsNodeArray *args)
{
	return ts_create_type_reference_node(ts_create_qualified_name(ID(grpc), ID(name)), args);
}

/* (err: grpc.ServiceError | null, value?: O) => void */
static TsNode *grpc_callback_type(OutFile *f, const char *grpc, const MethodInfo *mi)
{
	TsNode *err = PARAM("err", NULL, ts_create_union_type_node(ts_list(grpc_type(grpc, "ServiceError", NULL), KW(TS_NullKeyword))));
	TsNode *value = PARAM("value", TOK(TS_QuestionToken), make_o(f, mi));
	return ts_create_function_type_node(NULL, ts_list(err, value), KW(TS_VoidKeyword));
}

static TsNode *grpc_sig(const MethodInfo *mi, TsNodeArray *params, TsNode *type)
{
	return ts_create_method_signature(NULL, params, type, ID(mi->local_name), NULL);
}

static TsNodeArray *grpc_unary_signatures(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi)
{
	(void)g, (void)s;
	const char *grpc = grpc_ns(f, false);
	TsNodeArray *r = ts_list_new();
	/* (input, metadata, options, callback), (input, metadata, callback), (input, options, callback), (input, callback) */
	static const int variants[4][2] = {{1, 1}, {1, 0}, {0, 1}, {0, 0}};
	for (int v = 0; v < 4; v++) {
		TsNodeArray *p = ts_list(PARAM("input", NULL, make_i(f, mi)));
		if (variants[v][0])
			ts_list_push(p, PARAM("metadata", NULL, grpc_type(grpc, "Metadata", NULL)));
		if (variants[v][1])
			ts_list_push(p, PARAM("options", NULL, grpc_type(grpc, "CallOptions", NULL)));
		ts_list_push(p, PARAM("callback", NULL, grpc_callback_type(f, grpc, mi)));
		ts_list_push(r, grpc_sig(mi, p, grpc_type(grpc, "ClientUnaryCall", NULL)));
	}
	return r;
}

static TsNodeArray *grpc_server_streaming_signatures(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi)
{
	(void)g, (void)s;
	const char *grpc = grpc_ns(f, false);
	TsNodeArray *p1 = ts_list(PARAM("input", NULL, make_i(f, mi)),
				  PARAM("metadata", TOK(TS_QuestionToken), grpc_type(grpc, "Metadata", NULL)),
				  PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL)));
	TsNode *s1 = grpc_sig(mi, p1, grpc_type(grpc, "ClientReadableStream", ts_list(make_o(f, mi))));
	TsNodeArray *p2 = ts_list(PARAM("input", NULL, make_i(f, mi)),
				  PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL)));
	TsNode *s2 = grpc_sig(mi, p2, grpc_type(grpc, "ClientReadableStream", ts_list(make_o(f, mi))));
	return ts_list(s1, s2);
}

static TsNodeArray *grpc_client_streaming_signatures(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi)
{
	(void)g, (void)s;
	const char *grpc = grpc_ns(f, false);
	TsNodeArray *r = ts_list_new();
	static const int variants[4][2] = {{1, 1}, {1, 0}, {0, 1}, {0, 0}};
	for (int v = 0; v < 4; v++) {
		TsNodeArray *p = ts_list_new();
		if (variants[v][0])
			ts_list_push(p, PARAM("metadata", NULL, grpc_type(grpc, "Metadata", NULL)));
		if (variants[v][1])
			ts_list_push(p, PARAM("options", NULL, grpc_type(grpc, "CallOptions", NULL)));
		ts_list_push(p, PARAM("callback", NULL, grpc_callback_type(f, grpc, mi)));
		ts_list_push(r, grpc_sig(mi, p, grpc_type(grpc, "ClientWritableStream", ts_list(make_i(f, mi)))));
	}
	return r;
}

static TsNodeArray *grpc_duplex_signatures(const ClientGenerator *g, OutFile *f, DescService *s, const MethodInfo *mi)
{
	(void)g, (void)s;
	const char *grpc = grpc_ns(f, false);
	TsNodeArray *p1 = ts_list(PARAM("metadata", NULL, grpc_type(grpc, "Metadata", NULL)),
				  PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL)));
	TsNode *i1 = make_i(f, mi);
	TsNode *s1 = grpc_sig(mi, p1, grpc_type(grpc, "ClientDuplexStream", ts_list(i1, make_o(f, mi))));
	TsNodeArray *p2 = ts_list(PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL)));
	TsNode *i2 = make_i(f, mi);
	TsNode *s2 = grpc_sig(mi, p2, grpc_type(grpc, "ClientDuplexStream", ts_list(i2, make_o(f, mi))));
	return ts_list(s1, s2);
}

/* the body of the implementation: const method = Service.methods[i]; return this.<request>(...) */
static TsNode *grpc_method_body(OutFile *f, const MethodInfo *mi, const char *service_type, const char *request,
				bool has_input, bool cast_options, bool has_callback)
{
	TsNode *vars = VARS(TS_NodeFlags_Const,
			    ts_list(VAR("method", NULL, ts_create_element_access(PA(ID(service_type), "methods"), NUMI(mi->index)))));
	TsNode *ti = make_i(f, mi);
	TsNode *to = make_o(f, mi);
	TsNode *path = ts_create_template_expression(
		ts_create_template_literal_like_node(TS_TemplateHead, STR_LIT("/"), true),
		ts_list(ts_create_template_span(PA(ID(service_type), "typeName"),
						ts_create_template_literal_like_node(TS_TemplateMiddle, STR_LIT("/"), true)),
			ts_create_template_span(PA(ID("method"), "name"),
						ts_create_template_literal_like_node(TS_TemplateTail, STR_LIT(""), true))));
	TsNode *ser_param = PARAM("value", NULL, make_i(f, mi));
	TsNode *ser = ts_create_arrow_function(
		NULL, NULL, ts_list(ser_param), TREF("Buffer"), TOK(TS_EqualsGreaterThanToken),
		CALL(PA(ID("Buffer"), "from"),
		     ts_list(CALL(PA(PA(ID("method"), "I"), "toBinary"), ts_list(ID("value"), PA(THIS(), "_binaryOptions"))))));
	TsNode *deser = ts_create_arrow_function(
		NULL, NULL, ts_list(PARAM("value", NULL, TREF("Buffer"))), make_o(f, mi), TOK(TS_EqualsGreaterThanToken),
		CALL(PA(PA(ID("method"), "O"), "fromBinary"), ts_list(ID("value"), PA(THIS(), "_binaryOptions"))));
	TsNodeArray *args = ts_list(path, ser, deser);
	if (has_input)
		ts_list_push(args, ID("input"));
	ts_list_push(args, ts_create_as_expression(ID("metadata"), KW(TS_AnyKeyword)));
	ts_list_push(args, cast_options ? ts_create_as_expression(ID("options"), KW(TS_AnyKeyword)) : ID("options"));
	if (has_callback)
		ts_list_push(args, ts_create_as_expression(ID("callback"), KW(TS_AnyKeyword)));
	TsNode *ret = ts_create_return(ts_create_call(PA(THIS(), request), ts_list(ti, to), args));
	return ts_create_block(ts_list(vars, ret), true);
}

static TsNode *grpc_method(const MethodInfo *mi, TsNodeArray *params, TsNode *type, TsNode *body)
{
	return ts_create_method(NULL, NULL, NULL, ID(mi->local_name), NULL, NULL, params, type, body);
}

/* createUnary */
static TsNode *grpc_unary(OutFile *f, DescService *s, const MethodInfo *mi)
{
	const char *grpc = grpc_ns(f, false);
	const char *service_type = import_type(&G.imports, f, &s->h, "default", false);
	TsNode *input = PARAM("input", NULL, make_i(f, mi));
	TsNode *metadata = PARAM("metadata", NULL,
				 ts_create_union_type_node(ts_list(grpc_type(grpc, "Metadata", NULL), grpc_type(grpc, "CallOptions", NULL),
								   ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)))));
	TsNode *options = PARAM("options", TOK(TS_QuestionToken),
				ts_create_union_type_node(ts_list(grpc_type(grpc, "CallOptions", NULL),
								  ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)))));
	TsNode *callback = PARAM("callback", TOK(TS_QuestionToken), ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)));
	TsNode *type = grpc_type(grpc, "ClientUnaryCall", NULL);
	TsNode *body = grpc_method_body(f, mi, service_type, "makeUnaryRequest", true, true, true);
	return grpc_method(mi, ts_list(input, metadata, options, callback), type, body);
}

/* createServerStreaming */
static TsNode *grpc_server_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	const char *grpc = grpc_ns(f, false);
	const char *service_type = import_type(&G.imports, f, &s->h, "default", false);
	TsNode *input = PARAM("input", NULL, make_i(f, mi));
	TsNode *metadata = PARAM("metadata", TOK(TS_QuestionToken),
				 ts_create_union_type_node(ts_list(grpc_type(grpc, "Metadata", NULL), grpc_type(grpc, "CallOptions", NULL))));
	TsNode *options = PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL));
	TsNode *type = grpc_type(grpc, "ClientReadableStream", ts_list(make_o(f, mi)));
	TsNode *body = grpc_method_body(f, mi, service_type, "makeServerStreamRequest", true, false, false);
	return grpc_method(mi, ts_list(input, metadata, options), type, body);
}

/* createClientStreaming */
static TsNode *grpc_client_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	const char *grpc = grpc_ns(f, false);
	const char *service_type = import_type(&G.imports, f, &s->h, "default", false);
	TsNode *metadata = PARAM("metadata", NULL,
				 ts_create_union_type_node(ts_list(grpc_type(grpc, "Metadata", NULL), grpc_type(grpc, "CallOptions", NULL),
								   ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)))));
	TsNode *options = PARAM("options", TOK(TS_QuestionToken),
				ts_create_union_type_node(ts_list(grpc_type(grpc, "CallOptions", NULL),
								  ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)))));
	TsNode *callback = PARAM("callback", TOK(TS_QuestionToken), ts_create_parenthesized_type(grpc_callback_type(f, grpc, mi)));
	TsNode *type = grpc_type(grpc, "ClientWritableStream", ts_list(make_i(f, mi)));
	TsNode *body = grpc_method_body(f, mi, service_type, "makeClientStreamRequest", false, true, true);
	return grpc_method(mi, ts_list(metadata, options, callback), type, body);
}

/* createDuplexStreaming */
static TsNode *grpc_duplex_streaming(OutFile *f, DescService *s, const MethodInfo *mi)
{
	const char *grpc = grpc_ns(f, false);
	const char *service_type = import_type(&G.imports, f, &s->h, "default", false);
	TsNode *metadata = PARAM("metadata", TOK(TS_QuestionToken),
				 ts_create_union_type_node(ts_list(grpc_type(grpc, "Metadata", NULL), grpc_type(grpc, "CallOptions", NULL))));
	TsNode *options = PARAM("options", TOK(TS_QuestionToken), grpc_type(grpc, "CallOptions", NULL));
	TsNode *ti = make_i(f, mi);
	TsNode *type = grpc_type(grpc, "ClientDuplexStream", ts_list(ti, make_o(f, mi)));
	TsNode *body = grpc_method_body(f, mi, service_type, "makeBidiStreamRequest", false, false, false);
	return grpc_method(mi, ts_list(metadata, options), type, body);
}

/* ServiceClientGeneratorGrpc.generateImplementationClass */
static void grpc_generate_implementation_class(const ClientGenerator *g, OutFile *f, DescService *s)
{
	ServiceType *t = get_service_type(&G.interp, s);
	const char *client = import_type(&G.imports, f, &s->h, g->kind_implementation, false);
	const char *iclient = import_type(&G.imports, f, &s->h, g->kind_interface, false);
	const char *read_options = import_name(f, "BinaryReadOptions", G.o->runtime_import_path, true);
	const char *write_options = import_name(f, "BinaryWriteOptions", G.o->runtime_import_path, true);
	const char *grpc = grpc_ns(f, false);
#define BINARY_OPTIONS() \
	ts_create_type_reference_node(ID("Partial"), ts_list(ts_create_intersection_type_node(ts_list(TREF(read_options), TREF(write_options)))))
	TsNodeArray *members = ts_list(
		ts_create_property(NULL, ts_list(TOK(TS_PrivateKeyword), TOK(TS_ReadonlyKeyword)), ID("_binaryOptions"), NULL,
				   BINARY_OPTIONS(), NULL),
		ts_create_constructor(
			NULL, NULL,
			ts_list(PARAM("address", NULL, KW(TS_StringKeyword)),
				PARAM("credentials", NULL, grpc_type(grpc, "ChannelCredentials", NULL)),
				ts_create_parameter(NULL, NULL, NULL, ID("options"), NULL, grpc_type(grpc, "ClientOptions", NULL),
						    ts_create_object_literal(ts_list_new(), false)),
				ts_create_parameter(NULL, NULL, NULL, ID("binaryOptions"), NULL, BINARY_OPTIONS(),
						    ts_create_object_literal(ts_list_new(), false))),
			ts_create_block(ts_list(EXPR(CALL(ts_create_super(), ts_list(ID("address"), ID("credentials"), ID("options")))),
						EXPR(BIN(PA(THIS(), "_binaryOptions"), TS_EqualsToken, ID("binaryOptions")))),
					true)));
#undef BINARY_OPTIONS
	for (int i = 0; i < t->n_methods; i++) {
		const MethodInfo *mi = t->methods[i];
		TsNode *decl = g->create_method[method_style(mi)](f, s, mi);
		add_comments_for_descriptor(decl, &find_method(s, mi)->h, APPEND_TO_LEADING_BLOCK);
		ts_list_push(members, decl);
	}
	TsNode *st = ts_create_class_declaration(
		NULL, ts_list(TOK(TS_ExportKeyword)), ID(client), NULL,
		ts_list(ts_create_heritage_clause(TS_ExtendsKeyword, ts_list(ts_create_expression_with_type_arguments(NULL, PA(ID(grpc), "Client")))),
			ts_create_heritage_clause(TS_ImplementsKeyword, ts_list(ts_create_expression_with_type_arguments(NULL, ID(iclient))))),
		members);
	add_statement(f, st, false);
	add_comments_for_descriptor(st, &s->h, APPEND_TO_LEADING_BLOCK);
}

static const ClientGenerator client_grpc = {
	"grpc1-client-interface",
	"grpc1-client",
	{grpc_unary, grpc_server_streaming, grpc_client_streaming, grpc_duplex_streaming},
	{grpc_unary_signatures, grpc_server_streaming_signatures, grpc_client_streaming_signatures, grpc_duplex_signatures},
	grpc_generate_implementation_class,
};

/* ---- service-server-generator-generic.ts ---- */

static void server_generic_register_symbols(OutFile *f, DescService *s)
{
	symbols_register(&G.symbols, xcat("I", create_local_type_name(&s->h)), &s->h, f, "generic-server-interface");
}

static TsNode *server_generic_signature(OutFile *f, const MethodInfo *mi)
{
	TsNode *I = TREF(import_type(&G.imports, f, &mi->I->h, "default", false));
	TsNode *O = TREF(import_type(&G.imports, f, &mi->O->h, "default", false));
	TsNode *context = PARAM("context", NULL, TREF("T"));
	TsNodeArray *params;
	TsNode *ret;
	switch (method_style(mi)) {
	case 0: /* createUnary */
		params = ts_list(PARAM("request", NULL, I), context);
		ret = ts_create_type_reference_node(ID("Promise"), ts_list(O));
		break;
	case 1: { /* createServerStreaming */
		const char *in = import_name(f, "RpcInputStream", G.o->runtime_rpc_import_path, false);
		params = ts_list(PARAM("request", NULL, I), PARAM("responses", NULL, ts_create_type_reference_node(ID(in), ts_list(O))), context);
		ret = ts_create_type_reference_node(ID("Promise"), ts_list(KW(TS_VoidKeyword)));
		break;
	}
	case 2: { /* createClientStreaming */
		const char *out = import_name(f, "RpcOutputStream", G.o->runtime_rpc_import_path, false);
		params = ts_list(PARAM("requests", NULL, ts_create_type_reference_node(ID(out), ts_list(I))), context);
		ret = ts_create_type_reference_node(ID("Promise"), ts_list(O));
		break;
	}
	default: { /* createBidi */
		const char *out = import_name(f, "RpcOutputStream", G.o->runtime_rpc_import_path, false);
		const char *in = import_name(f, "RpcInputStream", G.o->runtime_rpc_import_path, false);
		params = ts_list(PARAM("requests", NULL, ts_create_type_reference_node(ID(out), ts_list(I))),
				 PARAM("responses", NULL, ts_create_type_reference_node(ID(in), ts_list(O))), context);
		ret = ts_create_type_reference_node(ID("Promise"), ts_list(KW(TS_VoidKeyword)));
	}
	}
	return ts_create_method_signature(NULL, params, ret, ID(mi->local_name), NULL);
}

static void server_generic_generate_interface(OutFile *f, DescService *s)
{
	ServiceType *t = get_service_type(&G.interp, s);
	const char *name = import_type(&G.imports, f, &s->h, "generic-server-interface", false);
	const char *ctx = import_name(f, "ServerCallContext", G.o->runtime_rpc_import_path, false);
	TsNodeArray *sigs = ts_list_new();
	for (int i = 0; i < t->n_methods; i++) {
		TsNode *sig = server_generic_signature(f, t->methods[i]);
		add_comments_for_descriptor(sig, &find_method(s, t->methods[i])->h, APPEND_TO_LEADING_BLOCK);
		ts_list_push(sigs, sig);
	}
	TsNode *st = ts_create_interface_declaration(
		NULL, ts_list(TOK(TS_ExportKeyword)), ID(name),
		ts_list(ts_create_type_parameter_declaration(ID("T"), NULL, TREF(ctx))), NULL, sigs);
	add_comments_for_descriptor(st, &s->h, APPEND_TO_LEADING_BLOCK);
	add_statement(f, st, false);
}

/* ---- service-server-generator-grpc.ts ---- */

static void server_grpc_register_symbols(OutFile *f, DescService *s)
{
	const char *base = create_local_type_name(&s->h);
	char *def = xcat(base, "Definition");
	if (def[0] >= 'A' && def[0] <= 'Z')
		def[0] = (char)(def[0] + 32);
	symbols_register(&G.symbols, xcat("I", base), &s->h, f, "grpc-server-interface");
	symbols_register(&G.symbols, def, &s->h, f, "grpc-server-definition");
}

static void server_grpc_generate_interface(OutFile *f, DescService *s)
{
	ServiceType *t = get_service_type(&G.interp, s);
	const char *name = import_type(&G.imports, f, &s->h, "grpc-server-interface", false);
	const char *grpc = grpc_ns(f, true);
	TsNodeArray *members = ts_list_new();
	for (int i = 0; i < t->n_methods; i++) {
		const MethodInfo *mi = t->methods[i];
		const char *g = grpc_ns(f, true);
		const char *handler = mi->server_streaming && mi->client_streaming ? "handleBidiStreamingCall"
				      : mi->server_streaming			 ? "handleServerStreamingCall"
				      : mi->client_streaming			 ? "handleClientStreamingCall"
										 : "handleUnaryCall";
		TsNode *I = TREF(import_type(&G.imports, f, &mi->I->h, "default", false));
		TsNode *O = TREF(import_type(&G.imports, f, &mi->O->h, "default", false));
		TsNode *sig = ts_create_property_signature(NULL, ID(mi->local_name), NULL, grpc_type(g, handler, ts_list(I, O)), NULL);
		add_comments_for_descriptor(sig, &find_method(s, mi)->h, APPEND_TO_LEADING_BLOCK);
		ts_list_push(members, sig);
	}
	TsNode *st = ts_create_interface_declaration(
		NULL, ts_list(TOK(TS_ExportKeyword)), ID(name), NULL,
		ts_list(ts_create_heritage_clause(
			TS_ExtendsKeyword, ts_list(ts_create_expression_with_type_arguments(NULL, PA(ID(grpc), "UntypedServiceImplementation"))))),
		members);
	add_comments_for_descriptor(st, &s->h, APPEND_TO_LEADING_BLOCK);
	add_statement(f, st, false);
}

static TsNode *server_grpc_definition_property(OutFile *f, DescService *s, const MethodInfo *mi)
{
	const char *I = import_type(&G.imports, f, &mi->I->h, "default", false);
	const char *O = import_type(&G.imports, f, &mi->O->h, "default", false);
	TsNode *bytes_arrow_o = ARROW(ts_list(PARAM("bytes", NULL, NULL)), CALL(PA(ID(O), "fromBinary"), ts_list(ID("bytes"))));
	TsNode *bytes_arrow_i = ARROW(ts_list(PARAM("bytes", NULL, NULL)), CALL(PA(ID(I), "fromBinary"), ts_list(ID("bytes"))));
	TsNode *ser_o = ARROW(ts_list(PARAM("value", NULL, NULL)),
			      CALL(PA(ID("Buffer"), "from"), ts_list(CALL(PA(ID(O), "toBinary"), ts_list(ID("value"))))));
	TsNode *ser_i = ARROW(ts_list(PARAM("value", NULL, NULL)),
			      CALL(PA(ID("Buffer"), "from"), ts_list(CALL(PA(ID(I), "toBinary"), ts_list(ID("value"))))));
	TsNodeArray *props = ts_list(PROP("path", STR(xcat("/", s->type_name, "/", mi->name))), PROP("originalName", STR(mi->name)),
				     PROP("requestStream", mi->client_streaming ? ts_create_true() : ts_create_false()),
				     PROP("responseStream", mi->server_streaming ? ts_create_true() : ts_create_false()),
				     PROP("responseDeserialize", bytes_arrow_o), PROP("requestDeserialize", bytes_arrow_i),
				     PROP("responseSerialize", ser_o), PROP("requestSerialize", ser_i));
	return PROP(mi->local_name, ts_create_object_literal(props, true));
}

static void server_grpc_generate_definition(OutFile *f, DescService *s)
{
	const char *def = import_type(&G.imports, f, &s->h, "grpc-server-definition", false);
	const char *iface = import_type(&G.imports, f, &s->h, "grpc-server-interface", false);
	ServiceType *t = get_service_type(&G.interp, s);
	const char *grpc = grpc_ns(f, true);
	TsNodeArray *props = ts_list_new();
	for (int i = 0; i < t->n_methods; i++)
		ts_list_push(props, server_grpc_definition_property(f, s, t->methods[i]));
	TsNode *st = ts_create_variable_statement(
		ts_list(TOK(TS_ExportKeyword)),
		ts_create_variable_declaration_list(
			ts_list(VAR(def, grpc_type(grpc, "ServiceDefinition", ts_list(TREF(iface))), ts_create_object_literal(props, true))),
			TS_NodeFlags_Const));
	const char *doc = xcat("@grpc/grpc-js definition for the protobuf ", desc_string(&s->h), ".\n\nUsage: Implement the interface ",
			       iface, " and add to a grpc server.\n\n```typescript\nconst server = new grpc.Server();\nconst service: ",
			       iface, " = ...\nserver.addService(", def, ", service);\n```");
	add_comment_block_as_js_doc(st, str_c(doc));
	add_statement(f, st, false);
}
