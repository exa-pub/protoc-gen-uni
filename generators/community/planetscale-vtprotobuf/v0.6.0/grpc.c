/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Port of features/grpc/grpc.go and features/grpc/grpc_plugin.go (vtprotobuf v0.6.0; a fork of
 * protoc-gen-go-grpc with requireUnimplemented always true). */

#define CONTEXT_PACKAGE "context"
#define GRPC_PACKAGE "google.golang.org/grpc"
#define CODES_PACKAGE "google.golang.org/grpc/codes"
#define STATUS_PACKAGE "google.golang.org/grpc/status"

#define DEPRECATION_COMMENT "// Deprecated: Do not use."

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
} Grpc;

static bool grpc_deprecated(const PbMsg *options, uint32_t num) { return options && pb_int(options, num, 0) != 0; }
static bool service_deprecated(const PgService *s) { return grpc_deprecated(s->Desc->d.options, GENID_ServiceOptions_Deprecated_field_number); }
static bool method_deprecated(const PgMethod *m) { return grpc_deprecated(m->Desc->d.options, GENID_MethodOptions_Deprecated_field_number); }

/* unexport: strings.ToLower(s[:1]) + s[1:] (a non-ASCII first byte is invalid UTF-8 alone and
 * becomes U+FFFD) */
static const char *unexport(const char *s)
{
	unsigned char c = (unsigned char)s[0];
	if (c >= 0x80)
		return xcat("\xef\xbf\xbd", s + 1);
	char first[2] = {(char)(c >= 'A' && c <= 'Z' ? c + 32 : c), 0};
	return xcat(first, s + (c ? 1 : 0));
}

static const char *client_signature(Grpc *g, const PgMethod *method)
{
	const char *s = xcat(method->GoName, "(ctx ", pg_qualified_go_ident(g->g, pg_ident(CONTEXT_PACKAGE, "Context")));
	if (!method->Desc->client_streaming)
		s = xcat(s, ", in *", pg_qualified_go_ident(g->g, method->Input->GoIdent));
	s = xcat(s, ", opts ...", pg_qualified_go_ident(g->g, pg_ident(GRPC_PACKAGE, "CallOption")), ") (");
	if (!method->Desc->client_streaming && !method->Desc->server_streaming)
		s = xcat(s, "*", pg_qualified_go_ident(g->g, method->Output->GoIdent));
	else
		s = xcat(s, method->Parent->GoName, "_", method->GoName, "Client");
	return xcat(s, ", error)");
}

static const char *server_signature(Grpc *g, const PgMethod *method)
{
	Vec req_args = {0};
	const char *ret = "error";
	if (!method->Desc->client_streaming && !method->Desc->server_streaming) {
		vec_push(&req_args, (void *)pg_qualified_go_ident(g->g, pg_ident(CONTEXT_PACKAGE, "Context")));
		ret = xcat("(*", pg_qualified_go_ident(g->g, method->Output->GoIdent), ", error)");
	}
	if (!method->Desc->client_streaming)
		vec_push(&req_args, xcat("*", pg_qualified_go_ident(g->g, method->Input->GoIdent)));
	if (method->Desc->client_streaming || method->Desc->server_streaming)
		vec_push(&req_args, xcat(method->Parent->GoName, "_", method->GoName, "Server"));
	return xcat(method->GoName, "(", str_join(&req_args, ", "), ") ", ret);
}

static void gen_client_method(Grpc *g, const PgMethod *method, int index)
{
	const PgService *service = method->Parent;
	const char *sname = xcat("/", service->Desc->d.full_name, "/", method->Desc->d.name);

	if (method_deprecated(method))
		P(g->g, DEPRECATION_COMMENT);
	const char *sig = client_signature(g, method);
	P(g->g, "func (c *", unexport(service->GoName), "Client) ", sig, "{");
	if (!method->Desc->server_streaming && !method->Desc->client_streaming) {
		vt_alloc(g->f, "out", method->Output, true);
		P(g->g, "err := c.cc.Invoke(ctx, \"", sname, "\", in, out, opts...)");
		P(g->g, "if err != nil { return nil, err }");
		P(g->g, "return out, nil");
		P(g->g, "}");
		P(g->g, "");
		return;
	}
	const char *stream_type = xcat(unexport(service->GoName), method->GoName, "Client");
	const char *service_desc_var = xcat(service->GoName, "_ServiceDesc");
	P(g->g, "stream, err := c.cc.NewStream(ctx, &", service_desc_var, ".Streams[", index, "], \"", sname, "\", opts...)");
	P(g->g, "if err != nil { return nil, err }");
	P(g->g, "x := &", stream_type, "{stream}");
	if (!method->Desc->client_streaming) {
		P(g->g, "if err := x.ClientStream.SendMsg(in); err != nil { return nil, err }");
		P(g->g, "if err := x.ClientStream.CloseSend(); err != nil { return nil, err }");
	}
	P(g->g, "return x, nil");
	P(g->g, "}");
	P(g->g, "");

	bool gen_send = method->Desc->client_streaming;
	bool gen_recv = method->Desc->server_streaming;
	bool gen_close_and_recv = !method->Desc->server_streaming;

	/* Stream auxiliary types and methods. */
	P(g->g, "type ", service->GoName, "_", method->GoName, "Client interface {");
	if (gen_send)
		P(g->g, "Send(*", method->Input->GoIdent, ") error");
	if (gen_recv)
		P(g->g, "Recv() (*", method->Output->GoIdent, ", error)");
	if (gen_close_and_recv)
		P(g->g, "CloseAndRecv() (*", method->Output->GoIdent, ", error)");
	P(g->g, pg_ident(GRPC_PACKAGE, "ClientStream"));
	P(g->g, "}");
	P(g->g, "");

	P(g->g, "type ", stream_type, " struct {");
	P(g->g, pg_ident(GRPC_PACKAGE, "ClientStream"));
	P(g->g, "}");
	P(g->g, "");

	if (gen_send) {
		P(g->g, "func (x *", stream_type, ") Send(m *", method->Input->GoIdent, ") error {");
		P(g->g, "return x.ClientStream.SendMsg(m)");
		P(g->g, "}");
		P(g->g, "");
	}
	if (gen_recv) {
		P(g->g, "func (x *", stream_type, ") Recv() (*", method->Output->GoIdent, ", error) {");
		vt_alloc(g->f, "m", method->Output, true);
		P(g->g, "if err := x.ClientStream.RecvMsg(m); err != nil { return nil, err }");
		P(g->g, "return m, nil");
		P(g->g, "}");
		P(g->g, "");
	}
	if (gen_close_and_recv) {
		P(g->g, "func (x *", stream_type, ") CloseAndRecv() (*", method->Output->GoIdent, ", error) {");
		P(g->g, "if err := x.ClientStream.CloseSend(); err != nil { return nil, err }");
		vt_alloc(g->f, "m", method->Output, true);
		P(g->g, "if err := x.ClientStream.RecvMsg(m); err != nil { return nil, err }");
		P(g->g, "return m, nil");
		P(g->g, "}");
		P(g->g, "");
	}
}

static const char *gen_server_method(Grpc *g, const PgMethod *method)
{
	const PgService *service = method->Parent;
	const char *hname = xcat("_", service->GoName, "_", method->GoName, "_Handler");

	if (!method->Desc->client_streaming && !method->Desc->server_streaming) {
		P(g->g, "func ", hname, "(srv interface{}, ctx ", pg_ident(CONTEXT_PACKAGE, "Context"),
		  ", dec func(interface{}) error, interceptor ", pg_ident(GRPC_PACKAGE, "UnaryServerInterceptor"),
		  ") (interface{}, error) {");
		vt_alloc(g->f, "in", method->Input, true);
		P(g->g, "if err := dec(in); err != nil { return nil, err }");
		P(g->g, "if interceptor == nil { return srv.(", service->GoName, "Server).", method->GoName, "(ctx, in) }");
		P(g->g, "info := &", pg_ident(GRPC_PACKAGE, "UnaryServerInfo"), "{");
		P(g->g, "Server: srv,");
		P(g->g, "FullMethod: ", quote_go(str_c(xcat("/", service->Desc->d.full_name, "/", method->Desc->d.name))), ",");
		P(g->g, "}");
		P(g->g, "handler := func(ctx ", pg_ident(CONTEXT_PACKAGE, "Context"), ", req interface{}) (interface{}, error) {");
		P(g->g, "return srv.(", service->GoName, "Server).", method->GoName, "(ctx, req.(*", method->Input->GoIdent, "))");
		P(g->g, "}");
		P(g->g, "return interceptor(ctx, in, info, handler)");
		P(g->g, "}");
		P(g->g, "");
		return hname;
	}
	const char *stream_type = xcat(unexport(service->GoName), method->GoName, "Server");
	P(g->g, "func ", hname, "(srv interface{}, stream ", pg_ident(GRPC_PACKAGE, "ServerStream"), ") error {");
	if (!method->Desc->client_streaming) {
		vt_alloc(g->f, "m", method->Input, true);
		P(g->g, "if err := stream.RecvMsg(m); err != nil { return err }");
		P(g->g, "return srv.(", service->GoName, "Server).", method->GoName, "(m, &", stream_type, "{stream})");
	} else {
		P(g->g, "return srv.(", service->GoName, "Server).", method->GoName, "(&", stream_type, "{stream})");
	}
	P(g->g, "}");
	P(g->g, "");

	bool gen_send = method->Desc->server_streaming;
	bool gen_send_and_close = !method->Desc->server_streaming;
	bool gen_recv = method->Desc->client_streaming;

	/* Stream auxiliary types and methods. */
	P(g->g, "type ", service->GoName, "_", method->GoName, "Server interface {");
	if (gen_send)
		P(g->g, "Send(*", method->Output->GoIdent, ") error");
	if (gen_send_and_close)
		P(g->g, "SendAndClose(*", method->Output->GoIdent, ") error");
	if (gen_recv)
		P(g->g, "Recv() (*", method->Input->GoIdent, ", error)");
	P(g->g, pg_ident(GRPC_PACKAGE, "ServerStream"));
	P(g->g, "}");
	P(g->g, "");

	P(g->g, "type ", stream_type, " struct {");
	P(g->g, pg_ident(GRPC_PACKAGE, "ServerStream"));
	P(g->g, "}");
	P(g->g, "");

	if (gen_send) {
		P(g->g, "func (x *", stream_type, ") Send(m *", method->Output->GoIdent, ") error {");
		P(g->g, "return x.ServerStream.SendMsg(m)");
		P(g->g, "}");
		P(g->g, "");
	}
	if (gen_send_and_close) {
		P(g->g, "func (x *", stream_type, ") SendAndClose(m *", method->Output->GoIdent, ") error {");
		P(g->g, "return x.ServerStream.SendMsg(m)");
		P(g->g, "}");
		P(g->g, "");
	}
	if (gen_recv) {
		P(g->g, "func (x *", stream_type, ") Recv() (*", method->Input->GoIdent, ", error) {");
		vt_alloc(g->f, "m", method->Input, true);
		P(g->g, "if err := x.ServerStream.RecvMsg(m); err != nil { return nil, err }");
		P(g->g, "return m, nil");
		P(g->g, "}");
		P(g->g, "");
	}
	return hname;
}

static void gen_service(Grpc *g, const PgFile *file, const PgService *service)
{
	const char *client_name = xcat(service->GoName, "Client");

	P(g->g, "// ", client_name, " is the client API for ", service->GoName, " service.");
	P(g->g, "//");
	P(g->g, "// For semantics around ctx use and closing/ending streaming RPCs, please refer to "
		"https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.");

	/* Client interface. */
	if (service_deprecated(service)) {
		P(g->g, "//");
		P(g->g, DEPRECATION_COMMENT);
	}
	pg_annotate_symbol(g->g, client_name, (PgAnnotation){service->Location, 0});
	P(g->g, "type ", client_name, " interface {");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		pg_annotate_symbol(g->g, xcat(client_name, ".", method->GoName), (PgAnnotation){method->Location, 0});
		if (method_deprecated(method))
			P(g->g, DEPRECATION_COMMENT);
		const char *sig = client_signature(g, method);
		P(g->g, method->Comments.Leading, sig);
	}
	P(g->g, "}");
	P(g->g, "");

	/* Client structure. */
	P(g->g, "type ", unexport(client_name), " struct {");
	P(g->g, "cc ", pg_ident(GRPC_PACKAGE, "ClientConnInterface"));
	P(g->g, "}");
	P(g->g, "");

	/* NewClient factory. */
	if (service_deprecated(service))
		P(g->g, DEPRECATION_COMMENT);
	P(g->g, "func New", client_name, " (cc ", pg_ident(GRPC_PACKAGE, "ClientConnInterface"), ") ", client_name, " {");
	P(g->g, "return &", unexport(client_name), "{cc}");
	P(g->g, "}");
	P(g->g, "");

	int method_index = 0, stream_index = 0;
	/* Client method implementations. */
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (!method->Desc->server_streaming && !method->Desc->client_streaming) {
			/* Unary RPC method */
			gen_client_method(g, method, method_index);
			method_index++;
		} else {
			/* Streaming RPC method */
			gen_client_method(g, method, stream_index);
			stream_index++;
		}
	}

	const char *must_or_should = "must";

	/* Server interface. */
	const char *server_type = xcat(service->GoName, "Server");
	P(g->g, "// ", server_type, " is the server API for ", service->GoName, " service.");
	P(g->g, "// All implementations ", must_or_should, " embed Unimplemented", server_type);
	P(g->g, "// for forward compatibility");
	if (service_deprecated(service)) {
		P(g->g, "//");
		P(g->g, DEPRECATION_COMMENT);
	}
	pg_annotate_symbol(g->g, server_type, (PgAnnotation){service->Location, 0});
	P(g->g, "type ", server_type, " interface {");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		pg_annotate_symbol(g->g, xcat(server_type, ".", method->GoName), (PgAnnotation){method->Location, 0});
		if (method_deprecated(method))
			P(g->g, DEPRECATION_COMMENT);
		const char *sig = server_signature(g, method);
		P(g->g, method->Comments.Leading, sig);
	}
	P(g->g, "mustEmbedUnimplemented", server_type, "()");
	P(g->g, "}");
	P(g->g, "");

	/* Server Unimplemented struct for forward compatibility. */
	P(g->g, "// Unimplemented", server_type, " ", must_or_should, " be embedded to have forward compatible implementations.");
	P(g->g, "type Unimplemented", server_type, " struct {");
	P(g->g, "}");
	P(g->g, "");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		const char *nil_arg = "";
		if (!method->Desc->client_streaming && !method->Desc->server_streaming)
			nil_arg = "nil,";
		const char *sig = server_signature(g, method);
		P(g->g, "func (Unimplemented", server_type, ") ", sig, "{");
		P(g->g, "return ", nil_arg, pg_ident(STATUS_PACKAGE, "Errorf"), "(", pg_ident(CODES_PACKAGE, "Unimplemented"), ", \"method ",
		  method->GoName, " not implemented\")");
		P(g->g, "}");
	}
	P(g->g, "func (Unimplemented", server_type, ") mustEmbedUnimplemented", server_type, "() {}");
	P(g->g, "");

	/* Unsafe Server interface to opt-out of forward compatibility. */
	P(g->g, "// Unsafe", server_type, " may be embedded to opt out of forward compatibility for this service.");
	P(g->g, "// Use of this interface is not recommended, as added methods to ", server_type, " will");
	P(g->g, "// result in compilation errors.");
	P(g->g, "type Unsafe", server_type, " interface {");
	P(g->g, "mustEmbedUnimplemented", server_type, "()");
	P(g->g, "}");

	/* Server registration. */
	if (service_deprecated(service))
		P(g->g, DEPRECATION_COMMENT);
	const char *service_desc_var = xcat(service->GoName, "_ServiceDesc");
	P(g->g, "func Register", service->GoName, "Server(s ", pg_ident(GRPC_PACKAGE, "ServiceRegistrar"), ", srv ", server_type, ") {");
	P(g->g, "s.RegisterService(&", service_desc_var, ", srv)");
	P(g->g, "}");
	P(g->g, "");

	/* Server handler implementations. */
	const char **handler_names = xalloc(sizeof *handler_names * (size_t)(service->nMethods ? service->nMethods : 1));
	for (int i = 0; i < service->nMethods; i++)
		handler_names[i] = gen_server_method(g, service->Methods[i]);

	/* Service descriptor. */
	P(g->g, "// ", service_desc_var, " is the ", pg_ident(GRPC_PACKAGE, "ServiceDesc"), " for ", service->GoName, " service.");
	P(g->g, "// It's only intended for direct use with ", pg_ident(GRPC_PACKAGE, "RegisterService"), ",");
	P(g->g, "// and not to be introspected or modified (even as a copy)");
	P(g->g, "var ", service_desc_var, " = ", pg_ident(GRPC_PACKAGE, "ServiceDesc"), " {");
	P(g->g, "ServiceName: ", quote_go(str_c(service->Desc->d.full_name)), ",");
	P(g->g, "HandlerType: (*", server_type, ")(nil),");
	P(g->g, "Methods: []", pg_ident(GRPC_PACKAGE, "MethodDesc"), "{");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (method->Desc->client_streaming || method->Desc->server_streaming)
			continue;
		P(g->g, "{");
		P(g->g, "MethodName: ", quote_go(str_c(method->Desc->d.name)), ",");
		P(g->g, "Handler: ", handler_names[i], ",");
		P(g->g, "},");
	}
	P(g->g, "},");
	P(g->g, "Streams: []", pg_ident(GRPC_PACKAGE, "StreamDesc"), "{");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (!method->Desc->client_streaming && !method->Desc->server_streaming)
			continue;
		P(g->g, "{");
		P(g->g, "StreamName: ", quote_go(str_c(method->Desc->d.name)), ",");
		P(g->g, "Handler: ", handler_names[i], ",");
		if (method->Desc->server_streaming)
			P(g->g, "ServerStreams: true,");
		if (method->Desc->client_streaming)
			P(g->g, "ClientStreams: true,");
		P(g->g, "},");
	}
	P(g->g, "},");
	P(g->g, "Metadata: \"", file->Desc->d.name, "\",");
	P(g->g, "}");
	P(g->g, "");
}

/* generateFileContent generates the gRPC service definitions, excluding the package statement. */
static void grpc_generate_file_content(Grpc *g, const PgFile *file)
{
	if (file->nServices == 0)
		return;
	P(g->g, "// This is a compile-time assertion to ensure that this generated file");
	P(g->g, "// is compatible with the grpc package it is being compiled against.");
	P(g->g, "// Requires gRPC-Go v1.32.0 or later.");
	P(g->g, "const _ = ", pg_ident(GRPC_PACKAGE, "SupportPackageIsVersion7")); /* When changing, update version number above. */
	P(g->g, "");
	for (int i = 0; i < file->nServices; i++)
		gen_service(g, file, file->Services[i]);
}

/* grpc_plugin.go */
static bool grpc_generate_file(VtFile *gen, PgFile *file)
{
	if (file->nServices == 0)
		return false;
	Grpc g = {gen, gen->g};
	grpc_generate_file_content(&g, file);
	return true;
}
