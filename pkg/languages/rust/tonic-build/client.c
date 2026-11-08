/* Port of tonic-build 0.14.2 src/client.rs (https://github.com/hyperium/tonic, MIT, Copyright (c)
 * 2025 Lucio Franco), with the `transport` feature (a default feature): the quoted tokens built as
 * the syn nodes they parse to. */

/* self.inner = self.inner.<method>(<arg>); */
static PpNode *set_inner(const char *method, const char *arg)
{
	return s_semi(pp_e_assign(self_f("inner"), pp_e_method(self_f("inner"), method, pp_e(arg), NULL)));
}

static PpNode *generate_connect(const char *service_ident, bool enabled)
{
	if (!enabled)
		return NULL;
	PpNode *sig = pp_sig("connect", ty2("Result", pp_ty("Self"), pp_ty("tonic::transport::Error")));
	sig->flags |= PP_F_ASYNC;
	pp_push(sig, arg("dst", pp_ty("D")));
	PpNode *g = generics1(tparam("D", NULL));
	where_add(g, pp_ty("D"), pp_bounds(bound(path1("TryInto", pp_ty("tonic::transport::Endpoint"))), NULL));
	where_add(g, pp_ty("D::Error"), pp_bounds(bound(path1("Into", pp_ty("StdError"))), NULL));
	pp_set_generics(sig, g);
	PpNode *conn = pp_e_try(pp_e_await(pp_e_method(pp_e_try(call1("tonic::transport::Endpoint::new", pp_e("dst"))),
						       "connect", NULL)));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig,
				pp_block(s_let("conn", false, conn), s_expr(call1("Ok", call1("Self::new", pp_e("conn")))), NULL));
	pp_push_attr(fn, qdoc(" Attempt to create a new client by connecting to a given endpoint."));
	PpNode *impl = pp_item_impl(NULL, ty1(service_ident, pp_ty("tonic::transport::Channel")));
	pp_push(impl, fn);
	return impl;
}

static PpNode *generate_method(const TonicService *service, const TonicMethod *method, bool emit_package,
			       const char *proto_path, bool compile_well_known_types)
{
	PpNode *request, *response;
	method->request_response_name(method, proto_path, compile_well_known_types, &request, &response);
	bool cs = method->client_streaming, ss = method->server_streaming;
	const char *ident = format_ident(method->name);
	char *service_name = format_service_name(service, emit_package);
	char *path = format_method_path(service, method, emit_package);

	PpNode *req_ty = cs ? path1("tonic::IntoStreamingRequest", assoc("Message", request))
			    : path1("tonic::IntoRequest", request);
	PpNode *resp = ss ? ty1("tonic::codec::Streaming", response) : response;
	PpNode *sig = sig_recv(ident, 2, result_status(ty1("tonic::Response", resp)));
	sig->flags |= PP_F_ASYNC;
	pp_push(sig, arg("request", pp_ty_impl(pp_bounds(bound(req_ty), NULL))));

	PpNode *fmt = pp_macro(pp_path("format"), PP_DELIM_PAREN);
	pp_tt_push_src(fmt, "\"Service was not ready: {}\", e.into()");
	PpNode *closure = pp_e_closure(pp_e_block(pp_block(s_expr(call1("tonic::Status::unknown", pp_e_macro(fmt))), NULL)), false);
	pp_push(closure, pp_p_ident("e", false, false));
	PpNode *ready = pp_e_try(pp_e_method(pp_e_await(pp_e_method(self_f("inner"), "ready", NULL)), "map_err", closure, NULL));
	const char *call = cs ? (ss ? "streaming" : "client_streaming") : (ss ? "server_streaming" : "unary");
	PpNode *grpc_method = pp_e_call(pp_e("GrpcMethod::new"), lit_str(service_name), lit_str(method->identifier), NULL);
	PpNode *body = pp_block(
		s_semi(ready), s_let("codec", false, codec_default(method)),
		s_let("path", false, call1("http::uri::PathAndQuery::from_static", lit_str(path))),
		s_let("req", true, pp_e_method(pp_e("request"), cs ? "into_streaming_request" : "into_request", NULL)),
		s_semi(pp_e_method(pp_e_method(pp_e("req"), "extensions_mut", NULL), "insert", grpc_method, NULL)),
		s_expr(pp_e_await(pp_e_method(self_f("inner"), call, pp_e("req"), pp_e("path"), pp_e("codec"), NULL))), NULL);
	return pp_item_fn(PP_VIS_PUB, sig, body);
}

/* generate_internal */
void tonic_generate_client(const TonicCodeGenBuilder *b, const TonicService *service, const char *proto_path, Vec *items)
{
	bool emit_package = b->emit_package;
	const char *service_ident = format_ident(xcat(service->name, "Client"));
	const char *client_mod = format_ident(xcat(tonic_naive_snake_case(service->name), "_client"));
	Vec methods = {0};
	for (int i = 0; i < service->n_methods; i++) {
		const TonicMethod *m = &service->methods[i];
		PpNode *fn = generate_method(service, m, emit_package, proto_path, b->compile_well_known_types);
		push_doc_comments(fn, m->comment, m->n_comment);
		vec_push(&methods, fn);
	}
	PpNode *connect = generate_connect(service_ident, b->build_transport);
	const char *package = emit_package ? service->package : "";
	char *service_name = format_service_name(service, emit_package);
	Vec mod_attributes = attributes_for(&b->attributes.module, package);
	Vec struct_attributes = attributes_for(&b->attributes.structure, service_name);

	PpNode *mod = pp_item_mod(PP_VIS_PUB, client_mod, true);
	pp_push_attr(mod, qdoc(" Generated client implementations."));
	push_attrs(mod, &mod_attributes);
	pp_push_attr(mod, attr_src("allow",
				   "unused_variables, dead_code, missing_docs, clippy::wildcard_imports, "
				   "clippy::let_unit_value,",
				   true));
	pp_push(mod, pp_item_use(0, pp_use_tree("tonic::codegen::*"), false));
	pp_push(mod, pp_item_use(0, pp_use_tree("tonic::codegen::http::Uri"), false));

	PpNode *st = pp_item_struct(PP_VIS_PUB, service_ident, PP_F_NAMED);
	pp_set_generics(st, generics1(tparam("T", NULL)));
	pp_push(st, pp_field(0, "inner", ty_of("tonic::client::Grpc", "T")));
	push_doc_comments(st, service->comment, service->n_comment);
	push_attrs(st, &struct_attributes);
	pp_push_attr(st, attr_src("derive", "Debug, Clone", false));
	pp_push(mod, st);
	if (connect)
		pp_push(mod, connect);

	PpNode *impl = pp_item_impl(NULL, ty_of(service_ident, "T"));
	PpNode *g = generics1(tparam("T", NULL));
	where_add(g, pp_ty("T"), pp_bounds(bound(path1("tonic::client::GrpcService", pp_ty("tonic::body::Body"))), NULL));
	where_add(g, pp_ty("T::Error"), pp_bounds(bound(path1("Into", pp_ty("StdError"))), NULL));
	where_add(g, pp_ty("T::ResponseBody"),
		  pp_bounds(bound(path1("Body", assoc("Data", pp_ty("Bytes")))), bound_s("std::marker::Send"), static_lt(), NULL));
	where_add(g, pp_ty_qpath(pp_ty("T::ResponseBody"), 1, pp_path("Body::Error")),
		  pp_bounds(bound(path1("Into", pp_ty("StdError"))), bound_s("std::marker::Send"), NULL));
	pp_set_generics(impl, g);

	PpNode *sig = sig_recv("new", 0, pp_ty("Self"));
	pp_push(sig, arg("inner", pp_ty("T")));
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig,
				 pp_block(s_let("inner", false, call1("tonic::client::Grpc::new", pp_e("inner"))),
					  s_expr(self_shorthand("inner")), NULL)));

	sig = sig_recv("with_origin", 0, pp_ty("Self"));
	pp_push(sig, arg("inner", pp_ty("T")));
	pp_push(sig, arg("origin", pp_ty("Uri")));
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig,
				 pp_block(s_let("inner", false,
						pp_e_call(pp_e("tonic::client::Grpc::with_origin"), pp_e("inner"), pp_e("origin"), NULL)),
					  s_expr(self_shorthand("inner")), NULL)));

	sig = sig_recv("with_interceptor", 0, ty1(service_ident, ty2("InterceptedService", pp_ty("T"), pp_ty("F"))));
	pp_push(sig, arg("inner", pp_ty("T")));
	pp_push(sig, arg("interceptor", pp_ty("F")));
	g = generics1(tparam("F", NULL));
	where_add(g, pp_ty("F"), pp_bounds(bound_s("tonic::service::Interceptor"), NULL));
	where_add(g, pp_ty("T::ResponseBody"), pp_bounds(bound_s("Default"), NULL));
	PpNode *grpc_service = path1("tonic::client::GrpcService", pp_ty("tonic::body::Body"));
	pp_path_push(grpc_service, "ResponseBody", NULL);
	PpNode *service_args = pp_angle(ty1("http::Request", pp_ty("tonic::body::Body")),
					assoc("Response", ty1("http::Response", pp_ty_qpath(pp_ty("T"), 3, grpc_service))), NULL);
	where_add(g, pp_ty("T"), pp_bounds(bound(pp_path_args(pp_path("tonic::codegen::Service"), service_args)), NULL));
	PpNode *service_error = path1("tonic::codegen::Service", ty1("http::Request", pp_ty("tonic::body::Body")));
	pp_path_push(service_error, "Error", NULL);
	where_add(g, pp_ty_qpath(pp_ty("T"), 3, service_error),
		  pp_bounds(bound(path1("Into", pp_ty("StdError"))), bound_s("std::marker::Send"), bound_s("std::marker::Sync"), NULL));
	pp_set_generics(sig, g);
	PpNode *intercepted = pp_e_call(pp_e("InterceptedService::new"), pp_e("inner"), pp_e("interceptor"), NULL);
	pp_push(impl, pp_item_fn(PP_VIS_PUB, sig,
				 pp_block(s_expr(call1(xcat(service_ident, "::new"), intercepted)), NULL)));

	pp_push(impl, builder_fn("send_compressed", "encoding", pp_ty("CompressionEncoding"),
				 set_inner("send_compressed", "encoding"), " Compress requests with the given encoding.", "",
				 " This requires the server to support it otherwise it might respond with an", " error.", NULL));
	pp_push(impl, builder_fn("accept_compressed", "encoding", pp_ty("CompressionEncoding"),
				 set_inner("accept_compressed", "encoding"), " Enable decompressing responses.", NULL));
	pp_push(impl, builder_fn("max_decoding_message_size", "limit", pp_ty("usize"),
				 set_inner("max_decoding_message_size", "limit"), " Limits the maximum size of a decoded message.", "",
				 " Default: `4MB`", NULL));
	pp_push(impl, builder_fn("max_encoding_message_size", "limit", pp_ty("usize"),
				 set_inner("max_encoding_message_size", "limit"), " Limits the maximum size of an encoded message.",
				 "", " Default: `usize::MAX`", NULL));

	for (int i = 0; i < methods.n; i++)
		pp_push(impl, methods.v[i]);
	pp_push(mod, impl);
	vec_push(items, mod);
}
