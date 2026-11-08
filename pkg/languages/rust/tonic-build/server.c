/* Port of tonic-build 0.14.2 src/server.rs (https://github.com/hyperium/tonic, MIT, Copyright (c)
 * 2025 Lucio Franco): the quoted tokens built as the syn nodes they parse to. use_arc_self and
 * generate_default_stubs are off (README.md). */

/* tonic::Request<tonic::Streaming<req>> or tonic::Request<req> */
static PpNode *request_of(bool streaming, PpNode *req)
{
	return ty1("tonic::Request", streaming ? ty1("tonic::Streaming", req) : req);
}

static PpNode *stream_doc(const TonicMethod *m)
{
	return generate_doc_comment(str_c(xcat(" Server streaming response type for the ", m->identifier, " method.")));
}

static void generate_trait_methods(const TonicService *service, const char *proto_path, bool cwkt, PpNode *trait)
{
	for (int i = 0; i < service->n_methods; i++) {
		const TonicMethod *method = &service->methods[i];
		const char *name = format_ident(method->name);
		PpNode *req_message, *res_message;
		method->request_response_name(method, proto_path, cwkt, &req_message, &res_message);
		PpNode *res = res_message;
		if (method->server_streaming) {
			const char *stream = format_ident(xcat(method->identifier, "Stream"));
			PpNode *item = path1("tonic::codegen::tokio_stream::Stream",
					     assoc("Item", result_status(res_message)));
			PpNode *ty = pp_trait_type(stream, pp_bounds(bound(item), bound_s("std::marker::Send"), static_lt(), NULL), NULL);
			pp_push_attr(ty, stream_doc(method));
			pp_push(trait, ty);
			res = pp_ty(xcat("Self::", stream));
		}
		PpNode *sig = sig_recv(name, 1, result_status(ty1("tonic::Response", res)));
		sig->flags |= PP_F_ASYNC;
		pp_push(sig, arg("request", request_of(method->client_streaming, req_message)));
		PpNode *fn = pp_item_fn(0, sig, NULL);
		push_doc_comments(fn, method->comment, method->n_comment);
		pp_push(trait, fn);
	}
}

static PpNode *generate_trait(const TonicService *service, const char *proto_path, bool cwkt, const char *server_trait,
			      const Vec *trait_attributes)
{
	PpNode *trait = pp_item_trait(PP_VIS_PUB, server_trait,
				      pp_bounds(bound_s("std::marker::Send"), bound_s("std::marker::Sync"), static_lt(), NULL));
	generate_trait_methods(service, proto_path, cwkt, trait);
	pp_push_attr(trait, generate_doc_comment(str_c(xcat(
				    " Generated trait containing gRPC methods that should be implemented for use with ",
				    service->name, "Server."))));
	push_attrs(trait, trait_attributes);
	pp_push_attr(trait, attr_word("async_trait"));
	return trait;
}

static PpNode *generate_named(const char *server_service, const char *service_name, Vec *items)
{
	PpNode *c = pp_item_const(PP_VIS_PUB, "SERVICE_NAME", pp_ty_ref(NULL, false, pp_ty("str")), lit_str(service_name));
	pp_push_attr(c, generate_doc_comment(str_c(" Generated gRPC service name")));
	vec_push(items, c);
	PpNode *impl = pp_item_impl(pp_path("tonic::server::NamedService"), ty_of(server_service, "T"));
	pp_set_generics(impl, generics1(tparam("T", NULL)));
	pp_push(impl, pp_item_const(0, "NAME", pp_ty_ref("'static", false, pp_ty("str")), pp_e("SERVICE_NAME")));
	return impl;
}

/* The arm body of a method: generate_unary, generate_server_streaming, generate_client_streaming and
 * generate_streaming. */
static PpNode *generate_method_stream(const TonicMethod *method, const char *proto_path, bool cwkt,
				      const char *method_ident, const char *server_trait)
{
	bool cs = method->client_streaming, ss = method->server_streaming;
	const char *service_ident = format_ident(xcat(method->identifier, "Svc"));
	PpNode *request, *response;
	method->request_response_name(method, proto_path, cwkt, &request, &response);
	PpNode *bounded_t = generics1(tparam("T", pp_bounds(bound_s(server_trait), NULL)));

	PpNode *st = pp_item_struct(0, service_ident, PP_F_UNNAMED);
	pp_set_generics(st, bounded_t);
	pp_push(st, pp_field(PP_VIS_PUB, NULL, ty_of("Arc", "T")));
	pp_push_attr(st, attr_src("allow", "non_camel_case_types", false));

	const char *trait = cs ? (ss ? "tonic::server::StreamingService" : "tonic::server::ClientStreamingService")
			       : (ss ? "tonic::server::ServerStreamingService" : "tonic::server::UnaryService");
	PpNode *impl = pp_item_impl(path1(trait, request), ty_of(service_ident, "T"));
	pp_set_generics(impl, generics1(tparam("T", pp_bounds(bound_s(server_trait), NULL))));
	pp_push(impl, pp_impl_type(0, "Response", response));
	if (ss)
		pp_push(impl, pp_impl_type(0, "ResponseStream", pp_ty(xcat("T::", format_ident(xcat(method->identifier, "Stream"))))));
	pp_push(impl, pp_impl_type(0, "Future", ty2("BoxFuture", ty1("tonic::Response", pp_ty(ss ? "Self::ResponseStream" : "Self::Response")),
						      pp_ty("tonic::Status"))));
	PpNode *sig = sig_recv("call", 2, pp_ty("Self::Future"));
	pp_push(sig, arg("request", request_of(cs, request)));
	PpNode *call = pp_e_call(pp_e_qpath(pp_ty("T"), 1, pp_path(xcat(server_trait, "::", method_ident))),
				 pp_e_ref(false, pp_e("inner")), pp_e("request"), NULL);
	pp_push(impl, pp_item_fn(0, sig,
				 pp_block(s_let("inner", false, call1("Arc::clone", pp_e_ref(false, self_f("0")))),
					  s_let("fut", false, pp_e_async(pp_block(s_expr(pp_e_await(call)), NULL), true)),
					  s_expr(box_pin(pp_e("fut"))), NULL)));

	PpNode *grpc = pp_e_method(pp_e_method(call1("tonic::server::Grpc::new", pp_e("codec")), "apply_compression_config",
					       pp_e("accept_compression_encodings"), pp_e("send_compression_encodings"), NULL),
				   "apply_max_message_size_config", pp_e("max_decoding_message_size"),
				   pp_e("max_encoding_message_size"), NULL);
	const char *serve = cs ? (ss ? "streaming" : "client_streaming") : (ss ? "server_streaming" : "unary");
	PpNode *fut = pp_e_async(
		pp_block(s_let("method", false, call1(service_ident, pp_e("inner"))), s_let("codec", false, codec_default(method)),
			 s_let("grpc", true, grpc),
			 s_let("res", false, pp_e_await(pp_e_method(pp_e("grpc"), serve, pp_e("method"), pp_e("req"), NULL))),
			 s_expr(call1("Ok", pp_e("res"))), NULL),
		true);
	return pp_e_block(pp_block(pp_s_item(st), pp_s_item(impl),
				   s_let("accept_compression_encodings", false, self_f("accept_compression_encodings")),
				   s_let("send_compression_encodings", false, self_f("send_compression_encodings")),
				   s_let("max_decoding_message_size", false, self_f("max_decoding_message_size")),
				   s_let("max_encoding_message_size", false, self_f("max_encoding_message_size")),
				   s_let("inner", false, pp_e_method(self_f("inner"), "clone", NULL)), s_let("fut", false, fut),
				   s_expr(box_pin(pp_e("fut"))), NULL));
}

/* generate_methods: the match arms of `call`. */
static void generate_methods(const TonicService *service, bool emit_package, const char *proto_path, bool cwkt, PpNode *match)
{
	for (int i = 0; i < service->n_methods; i++) {
		const TonicMethod *method = &service->methods[i];
		char *path = format_method_path(service, method, emit_package);
		const char *ident = format_ident(method->name);
		const char *server_trait = format_ident(service->name);
		PpNode *body = generate_method_stream(method, proto_path, cwkt, ident, server_trait);
		pp_push(match, pp_arm(pp_p_lit(lit_str(path)), NULL, body));
	}
}

/* generate_internal */
void tonic_generate_server(const TonicCodeGenBuilder *b, const TonicService *service, const char *proto_path, Vec *items)
{
	bool emit_package = b->emit_package, cwkt = b->compile_well_known_types;
	PpNode *match = pp_e_match(pp_e_method(pp_e_method(pp_e("req"), "uri", NULL), "path", NULL));
	generate_methods(service, emit_package, proto_path, cwkt, match);
	const char *server_service = format_ident(xcat(service->name, "Server"));
	const char *server_trait = format_ident(service->name);
	const char *server_mod = format_ident(xcat(tonic_naive_snake_case(service->name), "_server"));
	Vec trait_attributes = attributes_for(&b->attributes.trait_attributes, service->name);
	PpNode *generated_trait = generate_trait(service, proto_path, cwkt, server_trait, &trait_attributes);
	const char *package = emit_package ? service->package : "";
	char *service_name = format_service_name(service, emit_package);
	Vec named = {0};
	PpNode *named_impl = generate_named(server_service, service_name, &named);
	Vec mod_attributes = attributes_for(&b->attributes.module, package);
	Vec struct_attributes = attributes_for(&b->attributes.structure, service_name);

	PpNode *mod = pp_item_mod(PP_VIS_PUB, server_mod, true);
	pp_push_attr(mod, qdoc(" Generated server implementations."));
	push_attrs(mod, &mod_attributes);
	pp_push_attr(mod, attr_src("allow",
				   "unused_variables, dead_code, missing_docs, clippy::wildcard_imports, "
				   "clippy::let_unit_value,",
				   true));
	pp_push(mod, pp_item_use(0, pp_use_tree("tonic::codegen::*"), false));
	pp_push(mod, generated_trait);

	PpNode *st = pp_item_struct(PP_VIS_PUB, server_service, PP_F_NAMED);
	pp_set_generics(st, generics1(tparam("T", NULL)));
	pp_push(st, pp_field(0, "inner", ty_of("Arc", "T")));
	pp_push(st, pp_field(0, "accept_compression_encodings", pp_ty("EnabledCompressionEncodings")));
	pp_push(st, pp_field(0, "send_compression_encodings", pp_ty("EnabledCompressionEncodings")));
	pp_push(st, pp_field(0, "max_decoding_message_size", ty_of("Option", "usize")));
	pp_push(st, pp_field(0, "max_encoding_message_size", ty_of("Option", "usize")));
	push_doc_comments(st, service->comment, service->n_comment);
	push_attrs(st, &struct_attributes);
	pp_push_attr(st, attr_src("derive", "Debug", false));
	pp_push(mod, st);

	/* impl<T> Server<T> */
	PpNode *impl = pp_item_impl(NULL, ty_of(server_service, "T"));
	pp_set_generics(impl, generics1(tparam("T", NULL)));
	PpNode *sig = sig_recv("new", 0, pp_ty("Self"));
	pp_push(sig, arg("inner", pp_ty("T")));
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(call1("Self::from_arc", call1("Arc::new", pp_e("inner")))), NULL)));
	sig = sig_recv("from_arc", 0, pp_ty("Self"));
	pp_push(sig, arg("inner", ty_of("Arc", "T")));
	PpNode *init = pp_e_struct(pp_path("Self"), NULL);
	pp_push(init, pp_field_value("inner", NULL));
	pp_push(init, pp_field_value("accept_compression_encodings", pp_e_call(pp_e("Default::default"), NULL)));
	pp_push(init, pp_field_value("send_compression_encodings", pp_e_call(pp_e("Default::default"), NULL)));
	pp_push(init, pp_field_value("max_decoding_message_size", pp_e("None")));
	pp_push(init, pp_field_value("max_encoding_message_size", pp_e("None")));
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(init), NULL)));
	sig = sig_recv("with_interceptor", 0, ty2("InterceptedService", pp_ty("Self"), pp_ty("F")));
	pp_push(sig, arg("inner", pp_ty("T")));
	pp_push(sig, arg("interceptor", pp_ty("F")));
	PpNode *g = generics1(tparam("F", NULL));
	where_add(g, pp_ty("F"), pp_bounds(bound_s("tonic::service::Interceptor"), NULL));
	pp_set_generics(sig, g);
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig,
				 pp_block(s_expr(pp_e_call(pp_e("InterceptedService::new"), call1("Self::new", pp_e("inner")),
							   pp_e("interceptor"), NULL)),
					  NULL)));
	PpNode *enable = pp_e_method(self_f("accept_compression_encodings"), "enable", pp_e("encoding"), NULL);
	pp_push(impl, builder_fn("accept_compressed", "encoding", pp_ty("CompressionEncoding"), s_semi(enable),
					" Enable decompressing requests with the given encoding.", NULL));
	enable = pp_e_method(self_f("send_compression_encodings"), "enable", pp_e("encoding"), NULL);
	pp_push(impl, builder_fn("send_compressed", "encoding", pp_ty("CompressionEncoding"), s_semi(enable),
					" Compress responses with the given encoding, if the client supports it.", NULL));
	pp_push(impl, builder_fn("max_decoding_message_size", "limit", pp_ty("usize"),
					s_semi(pp_e_assign(self_f("max_decoding_message_size"), call1("Some", pp_e("limit")))),
					" Limits the maximum size of a decoded message.", "", " Default: `4MB`", NULL));
	pp_push(impl, builder_fn("max_encoding_message_size", "limit", pp_ty("usize"),
					s_semi(pp_e_assign(self_f("max_encoding_message_size"), call1("Some", pp_e("limit")))),
					" Limits the maximum size of an encoded message.", "", " Default: `usize::MAX`", NULL));
	pp_push(mod, impl);

	/* impl<T, B> tonic::codegen::Service<http::Request<B>> for Server<T> */
	impl = pp_item_impl(path1("tonic::codegen::Service", ty_of("http::Request", "B")), ty_of(server_service, "T"));
	g = generics1(tparam("T", NULL));
	pp_push(g, tparam("B", NULL));
	where_add(g, pp_ty("T"), pp_bounds(bound_s(server_trait), NULL));
	where_add(g, pp_ty("B"), pp_bounds(bound_s("Body"), bound_s("std::marker::Send"), static_lt(), NULL));
	where_add(g, pp_ty("B::Error"),
		  pp_bounds(bound(path1("Into", pp_ty("StdError"))), bound_s("std::marker::Send"), static_lt(), NULL));
	pp_set_generics(impl, g);
	pp_push(impl, pp_impl_type(0, "Response", ty_of("http::Response", "tonic::body::Body")));
	pp_push(impl, pp_impl_type(0, "Error", pp_ty("std::convert::Infallible")));
	pp_push(impl, pp_impl_type(0, "Future", ty2("BoxFuture", pp_ty("Self::Response"), pp_ty("Self::Error"))));
	sig = sig_recv("poll_ready", 2, ty1("Poll", ty2("std::result::Result", pp_ty_tuple(NULL), pp_ty("Self::Error"))));
	pp_push(sig, arg("_cx", pp_ty_ref(NULL, true, ty1("Context", pp_lifetime("'_")))));
	pp_push(impl, pp_item_fn(0, sig, pp_block(s_expr(call1("Poll::Ready", call1("Ok", pp_e_tuple(NULL)))), NULL)));
	sig = sig_recv("call", 2, pp_ty("Self::Future"));
	pp_push(sig, arg("req", ty_of("http::Request", "B")));
	PpNode *unimplemented = pp_e_async(
		pp_block(s_let("response", true, call1("http::Response::new", pp_e_call(pp_e("tonic::body::Body::default"), NULL))),
			 s_let("headers", false, pp_e_method(pp_e("response"), "headers_mut", NULL)),
			 s_semi(pp_e_method(pp_e("headers"), "insert", pp_e("tonic::Status::GRPC_STATUS"),
					    pp_e_method(pp_e_paren(pp_e_cast(pp_e("tonic::Code::Unimplemented"), pp_ty("i32"))),
							"into", NULL),
					    NULL)),
			 s_semi(pp_e_method(pp_e("headers"), "insert", pp_e("http::header::CONTENT_TYPE"),
					    pp_e("tonic::metadata::GRPC_CONTENT_TYPE"), NULL)),
			 s_expr(call1("Ok", pp_e("response"))), NULL),
		true);
	pp_push(match, pp_arm(pp_p_wild(), NULL, box_pin(unimplemented)));
	pp_push(impl, pp_item_fn(0, sig, pp_block(s_expr(match), NULL)));
	pp_push(mod, impl);

	/* impl<T> Clone for Server<T> */
	impl = pp_item_impl(pp_path("Clone"), ty_of(server_service, "T"));
	pp_set_generics(impl, generics1(tparam("T", NULL)));
	init = pp_e_struct(pp_path("Self"), NULL);
	pp_push(init, pp_field_value("inner", NULL));
	static const char *const fields[] = {"accept_compression_encodings", "send_compression_encodings",
					     "max_decoding_message_size", "max_encoding_message_size"};
	for (int i = 0; i < 4; i++)
		pp_push(init, pp_field_value(fields[i], self_f(fields[i])));
	pp_push(impl, pp_item_fn(0, sig_recv("clone", 1, pp_ty("Self")),
				 pp_block(s_let("inner", false, pp_e_method(self_f("inner"), "clone", NULL)), s_expr(init), NULL)));
	pp_push(mod, impl);

	pp_push(mod, named.v[0]);
	pp_push(mod, named_impl);
	vec_push(items, mod);
}
