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

/* Ported from cmd/protoc-gen-go-grpc/grpc.go of grpc-go cmd/protoc-gen-go-grpc/v1.6.2 with
 * bufbuild/plugins' separate-package.patch (plugins/grpc/go/v1.6.2): the separate_package flag in
 * generateFile.
 *
 * The serviceGenerateHelperInterface of upstream has one implementation (serviceGenerateHelper),
 * called directly here. Go evaluates the operands of an expression and the arguments of a call
 * left to right, and QualifiedGoIdent assigns import names in the order of the calls, so every
 * call that qualifies an identifier is a statement of its own before the P that uses its
 * result. */

#define contextPackage "context"
#define grpcPackage "google.golang.org/grpc"
#define codesPackage "google.golang.org/grpc/codes"
#define statusPackage "google.golang.org/grpc/status"

#define deprecationComment "// Deprecated: Do not use."

static bool requireUnimplemented;
static bool separatePackage;

static bool service_deprecated(const PgService *s)
{
	return pb_int(s->Desc->d.options, GENID_ServiceOptions_Deprecated_field_number, 0) != 0;
}

static bool method_deprecated(const PgMethod *m)
{
	return pb_int(m->Desc->d.options, GENID_MethodOptions_Deprecated_field_number, 0) != 0;
}

static bool is_streaming_client(const PgMethod *m) { return m->Desc->client_streaming; }
static bool is_streaming_server(const PgMethod *m) { return m->Desc->server_streaming; }

static const char *qualify(PgGeneratedFile *g, const char *import_path, const char *name)
{
	return pg_qualified_go_ident(g, pg_ident(import_path, name));
}

/* unexport: strings.ToLower(s[:1]) + s[1:]; Go names of services are ASCII (protodesc accepts
 * only ASCII identifiers, GoCamelCase keeps them ASCII). */
static COLD const char *unexport(const char *s)
{
	char *r = xstrdup(s);
	if (r[0] >= 'A' && r[0] <= 'Z')
		r[0] = (char)(r[0] + ('a' - 'A'));
	return r;
}

/* formatFullMethodSymbol */
static COLD const char *format_full_method_symbol(const PgService *service, const PgMethod *method)
{
	return xcat(service->GoName, "_", method->GoName, "_FullMethodName");
}

/* genFullMethods */
static COLD void gen_full_methods(PgGeneratedFile *g, const PgService *service)
{
	if (service->nMethods == 0)
		return;

	P(g, "const (");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		const char *fm_symbol = format_full_method_symbol(service, method);
		const char *fm_name = xcat("/", service->Desc->d.full_name, "/", method->Desc->d.name);
		P(g, fm_symbol, " = \"", fm_name, "\"");
	}
	P(g, ")");
	P(g, "");
}

/* generateClientStruct */
static COLD void generate_client_struct(PgGeneratedFile *g, const char *client_name)
{
	P(g, "type ", unexport(client_name), " struct {");
	P(g, "cc ", pg_ident(grpcPackage, "ClientConnInterface"));
	P(g, "}");
	P(g, "");
}

/* generateNewClientDefinitions */
static COLD void generate_new_client_definitions(PgGeneratedFile *g, const char *client_name)
{
	P(g, "return &", unexport(client_name), "{cc}");
}

static const char *server_signature(PgGeneratedFile *g, const PgMethod *method);
static void gen_service_desc(PgFile *file, PgGeneratedFile *g, const char *service_desc_var, const char *server_type,
			     const PgService *service, const char **handler_names);
static const char *gen_server_method(PgGeneratedFile *g, const PgMethod *method);

/* generateUnimplementedServerType */
static COLD void generate_unimplemented_server_type(PgGeneratedFile *g, const PgService *service)
{
	const char *server_type = xcat(service->GoName, "Server");
	const char *must_or_should = "must";
	if (!requireUnimplemented)
		must_or_should = "should";
	/* Server Unimplemented struct for forward compatibility. */
	P(g, "// Unimplemented", server_type, " ", must_or_should, " be embedded to have");
	P(g, "// forward compatible implementations.");
	P(g, "//");
	P(g, "// NOTE: this should be embedded by value instead of pointer to avoid a nil");
	P(g, "// pointer dereference when methods are called.");
	P(g, "type Unimplemented", server_type, " struct {}");
	P(g, "");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		const char *nil_arg = "";
		if (!is_streaming_client(method) && !is_streaming_server(method))
			nil_arg = "nil,";
		const char *sig = server_signature(g, method);
		P(g, "func (Unimplemented", server_type, ") ", sig, "{");
		P(g, "return ", nil_arg, pg_ident(statusPackage, "Error"), "(", pg_ident(codesPackage, "Unimplemented"),
		  ", \"method ", method->GoName, " not implemented\")");
		P(g, "}");
	}
	if (requireUnimplemented)
		P(g, "func (Unimplemented", server_type, ") mustEmbedUnimplemented", server_type, "() {}");
	P(g, "func (Unimplemented", server_type, ") testEmbeddedByValue() {}");
	P(g, "");
}

/* generateServerFunctions */
static COLD void generate_server_functions(PgFile *file, PgGeneratedFile *g, const PgService *service, const char *server_type,
					   const char *service_desc_var)
{
	/* Server handler implementations. */
	const char **handler_names = xalloc(sizeof *handler_names * (size_t)(service->nMethods ? service->nMethods : 1));
	for (int i = 0; i < service->nMethods; i++)
		handler_names[i] = gen_server_method(g, service->Methods[i]);
	gen_service_desc(file, g, service_desc_var, server_type, service, handler_names);
}

/* FileDescriptorProto.package field number */
#define fileDescriptorProtoPackageFieldNumber 2
/* FileDescriptorProto.syntax field number */
#define fileDescriptorProtoSyntaxFieldNumber 12

static void gen_leading_comments(PgGeneratedFile *g, GoSourceLocation loc);
static void generate_file_content(PgPlugin *gen, PgFile *file, PgGeneratedFile *g);

/* protocVersion */
static COLD const char *protoc_version(PgPlugin *gen)
{
	const PbMsg *v = pb_msg(gen->Request, GENID_CodeGeneratorRequest_CompilerVersion_field_number);
	if (!v)
		return "(unknown)";
	Buf b = {0};
	buf_adds(&b, xcat("v", go_itoa((int32_t)pb_int(v, GENID_Version_Major_field_number, 0)), ".",
			  go_itoa((int32_t)pb_int(v, GENID_Version_Minor_field_number, 0)), ".",
			  go_itoa((int32_t)pb_int(v, GENID_Version_Patch_field_number, 0))));
	Str s = pb_bytes(v, GENID_Version_Suffix_field_number);
	if (s.n) {
		buf_addc(&b, '-');
		buf_add_str(&b, s);
	}
	return buf_str(&b);
}

/* generateFile generates a _grpc.pb.go file containing gRPC service definitions. */
static COLD PgGeneratedFile *generate_file(PgPlugin *gen, PgFile *file)
{
	if (file->nServices == 0)
		return NULL;
	PgGeneratedFile *g;
	if (!separatePackage) {
		const char *filename = xcat(file->GeneratedFilenamePrefix, "_grpc.pb.go");
		g = pg_new_generated_file(gen, filename, file->GoImportPath);
	} else {
		/* separate-package.patch */
		file->GoPackageName = xcat(file->GoPackageName, "grpc");
		const char *dir = pg_path_dir(file->GeneratedFilenamePrefix);
		const char *base = pg_path_base(file->GeneratedFilenamePrefix);
		file->GeneratedFilenamePrefix = pg_path_join(pg_path_join(dir, file->GoPackageName), base);
		g = pg_new_generated_file(gen, xcat(file->GeneratedFilenamePrefix, "_grpc.pb.go"),
					  pg_path_join(file->GoImportPath, file->GoPackageName));
	}
	/* Attach all comments associated with the syntax field. */
	int32_t syntax_path = fileDescriptorProtoSyntaxFieldNumber;
	gen_leading_comments(g, go_source_location_by_path(file->Desc, &syntax_path, 1));
	P(g, "// Code generated by protoc-gen-go-grpc. DO NOT EDIT.");
	P(g, "// versions:");
	P(g, "// - protoc-gen-go-grpc v", GRPC_GO_VERSION);
	P(g, "// - protoc             ", protoc_version(gen));
	if (pb_int(pb_msg(file->Proto, GENID_FileDescriptorProto_Options_field_number), GENID_FileOptions_Deprecated_field_number, 0))
		P(g, "// ", file->Desc->d.name, " is a deprecated file.");
	else
		P(g, "// source: ", file->Desc->d.name);
	P(g, "");
	/* Attach all comments associated with the package field. */
	int32_t package_path = fileDescriptorProtoPackageFieldNumber;
	gen_leading_comments(g, go_source_location_by_path(file->Desc, &package_path, 1));
	P(g, "package ", file->GoPackageName);
	P(g, "");
	generate_file_content(gen, file, g);
	return g;
}

static void gen_service(PgPlugin *gen, PgFile *file, PgGeneratedFile *g, const PgService *service);

/* generateFileContent generates the gRPC service definitions, excluding the package statement. */
static COLD void generate_file_content(PgPlugin *gen, PgFile *file, PgGeneratedFile *g)
{
	if (file->nServices == 0)
		return;

	P(g, "// This is a compile-time assertion to ensure that this generated file");
	P(g, "// is compatible with the grpc package it is being compiled against.");
	P(g, "// Requires gRPC-Go v1.64.0 or later.");
	P(g, "const _ = ", pg_ident(grpcPackage, "SupportPackageIsVersion9"));
	P(g, "");
	for (int i = 0; i < file->nServices; i++)
		gen_service(gen, file, g, file->Services[i]);
}

/* genServiceComments copies the comments from the RPC proto definitions to the corresponding
 * generated interface file. */
static COLD void gen_service_comments(PgGeneratedFile *g, const PgService *service)
{
	if (!pg_comments_empty(service->Comments.Leading)) {
		/* Add empty comment line to attach this service's comments to the godoc comments
		 * previously output for all services. */
		P(g, "//");
		P(g, go_trim_space(pg_comments_string(service->Comments.Leading)));
	}
}

static const char *client_signature(PgGeneratedFile *g, const PgMethod *method);
static void gen_client_method(PgGeneratedFile *g, const PgMethod *method, int index);

static COLD void gen_service(PgPlugin *gen, PgFile *file, PgGeneratedFile *g, const PgService *service)
{
	(void)gen;
	/* Full methods constants. */
	gen_full_methods(g, service);

	/* Client interface. */
	const char *client_name = xcat(service->GoName, "Client");

	P(g, "// ", client_name, " is the client API for ", service->GoName, " service.");
	P(g, "//");
	P(g, "// For semantics around ctx use and closing/ending streaming RPCs, please refer to "
	     "https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.");

	/* Copy comments from proto file. */
	gen_service_comments(g, service);

	if (service_deprecated(service)) {
		P(g, "//");
		P(g, deprecationComment);
	}
	pg_annotate_symbol(g, client_name, (PgAnnotation){service->Location, 0});
	P(g, "type ", client_name, " interface {");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		pg_annotate_symbol(g, xcat(client_name, ".", method->GoName), (PgAnnotation){method->Location, 0});
		if (method_deprecated(method))
			P(g, deprecationComment);
		const char *sig = client_signature(g, method);
		P(g, method->Comments.Leading, sig);
	}
	P(g, "}");
	P(g, "");

	/* Client structure. */
	generate_client_struct(g, client_name);

	/* NewClient factory. */
	if (service_deprecated(service))
		P(g, deprecationComment);
	P(g, "func New", client_name, " (cc ", pg_ident(grpcPackage, "ClientConnInterface"), ") ", client_name, " {");
	generate_new_client_definitions(g, client_name);
	P(g, "}");
	P(g, "");

	int method_index = 0, stream_index = 0;
	/* Client method implementations. */
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (!is_streaming_server(method) && !is_streaming_client(method)) {
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
	if (!requireUnimplemented)
		must_or_should = "should";

	/* Server interface. */
	const char *server_type = xcat(service->GoName, "Server");
	P(g, "// ", server_type, " is the server API for ", service->GoName, " service.");
	P(g, "// All implementations ", must_or_should, " embed Unimplemented", server_type);
	P(g, "// for forward compatibility.");

	/* Copy comments from proto file. */
	gen_service_comments(g, service);

	if (service_deprecated(service)) {
		P(g, "//");
		P(g, deprecationComment);
	}
	pg_annotate_symbol(g, server_type, (PgAnnotation){service->Location, 0});
	P(g, "type ", server_type, " interface {");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		pg_annotate_symbol(g, xcat(server_type, ".", method->GoName), (PgAnnotation){method->Location, 0});
		if (method_deprecated(method))
			P(g, deprecationComment);
		const char *sig = server_signature(g, method);
		P(g, method->Comments.Leading, sig);
	}
	if (requireUnimplemented)
		P(g, "mustEmbedUnimplemented", server_type, "()");
	P(g, "}");
	P(g, "");

	/* Server Unimplemented struct for forward compatibility. */
	generate_unimplemented_server_type(g, service);

	/* Unsafe Server interface to opt-out of forward compatibility. */
	P(g, "// Unsafe", server_type, " may be embedded to opt out of forward compatibility for this service.");
	P(g, "// Use of this interface is not recommended, as added methods to ", server_type, " will");
	P(g, "// result in compilation errors.");
	P(g, "type Unsafe", server_type, " interface {");
	P(g, "mustEmbedUnimplemented", server_type, "()");
	P(g, "}");

	/* Server registration. */
	if (service_deprecated(service))
		P(g, deprecationComment);
	const char *service_desc_var = xcat(service->GoName, "_ServiceDesc");
	P(g, "func Register", service->GoName, "Server(s ", pg_ident(grpcPackage, "ServiceRegistrar"), ", srv ", server_type, ") {");
	P(g, "// If the following call panics, it indicates Unimplemented", server_type, " was");
	P(g, "// embedded by pointer and is nil.  This will cause panics if an");
	P(g, "// unimplemented method is ever invoked, so we test this at initialization");
	P(g, "// time to prevent it from happening at runtime later due to I/O.");
	P(g, "if t, ok := srv.(interface { testEmbeddedByValue() }); ok {");
	P(g, "t.testEmbeddedByValue()");
	P(g, "}");
	P(g, "s.RegisterService(&", service_desc_var, ", srv)");
	P(g, "}");
	P(g, "");

	generate_server_functions(file, g, service, server_type, service_desc_var);
}

static const char *client_stream_interface(PgGeneratedFile *g, const PgMethod *method);

/* clientSignature */
static COLD const char *client_signature(PgGeneratedFile *g, const PgMethod *method)
{
	const char *s = xcat(method->GoName, "(ctx ", qualify(g, contextPackage, "Context"));
	if (!is_streaming_client(method))
		s = xcat(s, ", in *", pg_qualified_go_ident(g, method->Input->GoIdent));
	s = xcat(s, ", opts ...", qualify(g, grpcPackage, "CallOption"), ") (");
	if (!is_streaming_client(method) && !is_streaming_server(method))
		s = xcat(s, "*", pg_qualified_go_ident(g, method->Output->GoIdent));
	else
		s = xcat(s, client_stream_interface(g, method));
	return xcat(s, ", error)");
}

/* typeParam: Input, Output (qualified in this order) */
static COLD const char *type_param(PgGeneratedFile *g, const PgMethod *method)
{
	const char *in = pg_qualified_go_ident(g, method->Input->GoIdent);
	const char *out = pg_qualified_go_ident(g, method->Output->GoIdent);
	return xcat(in, ", ", out);
}

/* clientStreamInterface */
static COLD const char *client_stream_interface(PgGeneratedFile *g, const PgMethod *method)
{
	const char *tp = type_param(g, method);
	if (is_streaming_client(method) && is_streaming_server(method))
		return xcat(qualify(g, grpcPackage, "BidiStreamingClient"), "[", tp, "]");
	if (is_streaming_client(method))
		return xcat(qualify(g, grpcPackage, "ClientStreamingClient"), "[", tp, "]");
	const char *iface = qualify(g, grpcPackage, "ServerStreamingClient");
	return xcat(iface, "[", pg_qualified_go_ident(g, method->Output->GoIdent), "]");
}

/* genClientMethod */
static COLD void gen_client_method(PgGeneratedFile *g, const PgMethod *method, int index)
{
	const PgService *service = method->Parent;
	const char *fm_symbol = format_full_method_symbol(service, method);

	if (method_deprecated(method))
		P(g, deprecationComment);
	const char *sig = client_signature(g, method);
	P(g, "func (c *", unexport(service->GoName), "Client) ", sig, "{");
	P(g, "cOpts := append([]", pg_ident(grpcPackage, "CallOption"), "{", pg_ident(grpcPackage, "StaticMethod()"), "}, opts...)");
	if (!is_streaming_server(method) && !is_streaming_client(method)) {
		P(g, "out := new(", method->Output->GoIdent, ")");
		P(g, "err := c.cc.Invoke(ctx, ", fm_symbol, ", in, out, cOpts...)");
		P(g, "if err != nil { return nil, err }");
		P(g, "return out, nil");
		P(g, "}");
		P(g, "");
		return;
	}

	const char *tp = type_param(g, method);
	const char *stream_impl = xcat(qualify(g, grpcPackage, "GenericClientStream"), "[", tp, "]");
	const char *service_desc_var = xcat(service->GoName, "_ServiceDesc");
	P(g, "stream, err := c.cc.NewStream(ctx, &", service_desc_var, ".Streams[", index, "], ", fm_symbol, ", cOpts...)");
	P(g, "if err != nil { return nil, err }");
	P(g, "x := &", stream_impl, "{ClientStream: stream}");
	if (!is_streaming_client(method)) {
		P(g, "if err := x.ClientStream.SendMsg(in); err != nil { return nil, err }");
		P(g, "if err := x.ClientStream.CloseSend(); err != nil { return nil, err }");
	}
	P(g, "return x, nil");
	P(g, "}");
	P(g, "");

	/* Auxiliary types aliases, for backwards compatibility. */
	P(g, "// This type alias is provided for backwards compatibility with existing code that references the prior "
	     "non-generic stream type by name.");
	const char *iface = client_stream_interface(g, method);
	P(g, "type ", service->GoName, "_", method->GoName, "Client = ", iface);
	P(g, "");
}

static const char *server_stream_interface(PgGeneratedFile *g, const PgMethod *method);

/* serverSignature */
static COLD const char *server_signature(PgGeneratedFile *g, const PgMethod *method)
{
	Vec req_args = {0};
	const char *ret = "error";
	if (!is_streaming_client(method) && !is_streaming_server(method)) {
		vec_push(&req_args, (void *)qualify(g, contextPackage, "Context"));
		ret = xcat("(*", pg_qualified_go_ident(g, method->Output->GoIdent), ", error)");
	}
	if (!is_streaming_client(method))
		vec_push(&req_args, (void *)xcat("*", pg_qualified_go_ident(g, method->Input->GoIdent)));
	if (is_streaming_client(method) || is_streaming_server(method))
		vec_push(&req_args, (void *)server_stream_interface(g, method));
	return xcat(method->GoName, "(", str_join(&req_args, ", "), ") ", ret);
}

/* genServiceDesc */
static COLD void gen_service_desc(PgFile *file, PgGeneratedFile *g, const char *service_desc_var, const char *server_type,
				  const PgService *service, const char **handler_names)
{
	/* Service descriptor. */
	P(g, "// ", service_desc_var, " is the ", pg_ident(grpcPackage, "ServiceDesc"), " for ", service->GoName, " service.");
	P(g, "// It's only intended for direct use with ", pg_ident(grpcPackage, "RegisterService"), ",");
	P(g, "// and not to be introspected or modified (even as a copy)");
	P(g, "var ", service_desc_var, " = ", pg_ident(grpcPackage, "ServiceDesc"), " {");
	P(g, "ServiceName: ", quote_go(str_c(service->Desc->d.full_name)), ",");
	P(g, "HandlerType: (*", server_type, ")(nil),");
	P(g, "Methods: []", pg_ident(grpcPackage, "MethodDesc"), "{");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (is_streaming_client(method) || is_streaming_server(method))
			continue;
		P(g, "{");
		P(g, "MethodName: ", quote_go(str_c(method->Desc->d.name)), ",");
		P(g, "Handler: ", handler_names[i], ",");
		P(g, "},");
	}
	P(g, "},");
	P(g, "Streams: []", pg_ident(grpcPackage, "StreamDesc"), "{");
	for (int i = 0; i < service->nMethods; i++) {
		const PgMethod *method = service->Methods[i];
		if (!is_streaming_client(method) && !is_streaming_server(method))
			continue;
		P(g, "{");
		P(g, "StreamName: ", quote_go(str_c(method->Desc->d.name)), ",");
		P(g, "Handler: ", handler_names[i], ",");
		if (is_streaming_server(method))
			P(g, "ServerStreams: true,");
		if (is_streaming_client(method))
			P(g, "ClientStreams: true,");
		P(g, "},");
	}
	P(g, "},");
	P(g, "Metadata: \"", file->Desc->d.name, "\",");
	P(g, "}");
	P(g, "");
}

/* serverStreamInterface */
static COLD const char *server_stream_interface(PgGeneratedFile *g, const PgMethod *method)
{
	const char *tp = type_param(g, method);
	if (is_streaming_client(method) && is_streaming_server(method))
		return xcat(qualify(g, grpcPackage, "BidiStreamingServer"), "[", tp, "]");
	if (is_streaming_client(method))
		return xcat(qualify(g, grpcPackage, "ClientStreamingServer"), "[", tp, "]");
	const char *iface = qualify(g, grpcPackage, "ServerStreamingServer");
	return xcat(iface, "[", pg_qualified_go_ident(g, method->Output->GoIdent), "]");
}

/* genServerMethod; hnameFuncNameFormatter is the identity (serviceGenerateHelper). */
static COLD const char *gen_server_method(PgGeneratedFile *g, const PgMethod *method)
{
	const PgService *service = method->Parent;
	const char *hname = xcat("_", service->GoName, "_", method->GoName, "_Handler");

	if (!is_streaming_client(method) && !is_streaming_server(method)) {
		P(g, "func ", hname, "(srv interface{}, ctx ", pg_ident(contextPackage, "Context"), ", dec func(interface{}) error, interceptor ",
		  pg_ident(grpcPackage, "UnaryServerInterceptor"), ") (interface{}, error) {");
		P(g, "in := new(", method->Input->GoIdent, ")");
		P(g, "if err := dec(in); err != nil { return nil, err }");
		P(g, "if interceptor == nil { return srv.(", service->GoName, "Server).", method->GoName, "(ctx, in) }");
		P(g, "info := &", pg_ident(grpcPackage, "UnaryServerInfo"), "{");
		P(g, "Server: srv,");
		const char *fm_symbol = format_full_method_symbol(service, method);
		P(g, "FullMethod: ", fm_symbol, ",");
		P(g, "}");
		P(g, "handler := func(ctx ", pg_ident(contextPackage, "Context"), ", req interface{}) (interface{}, error) {");
		P(g, "return srv.(", service->GoName, "Server).", method->GoName, "(ctx, req.(*", method->Input->GoIdent, "))");
		P(g, "}");
		P(g, "return interceptor(ctx, in, info, handler)");
		P(g, "}");
		P(g, "");
		return hname;
	}

	const char *tp = type_param(g, method);
	const char *stream_impl = xcat(qualify(g, grpcPackage, "GenericServerStream"), "[", tp, "]");

	P(g, "func ", hname, "(srv interface{}, stream ", pg_ident(grpcPackage, "ServerStream"), ") error {");
	if (!is_streaming_client(method)) {
		P(g, "m := new(", method->Input->GoIdent, ")");
		P(g, "if err := stream.RecvMsg(m); err != nil { return err }");
		P(g, "return srv.(", service->GoName, "Server).", method->GoName, "(m, &", stream_impl, "{ServerStream: stream})");
	} else {
		P(g, "return srv.(", service->GoName, "Server).", method->GoName, "(&", stream_impl, "{ServerStream: stream})");
	}
	P(g, "}");
	P(g, "");

	/* Auxiliary types aliases, for backwards compatibility. */
	P(g, "// This type alias is provided for backwards compatibility with existing code that references the prior "
	     "non-generic stream type by name.");
	const char *iface = server_stream_interface(g, method);
	P(g, "type ", service->GoName, "_", method->GoName, "Server = ", iface);
	P(g, "");
	return hname;
}

/* genLeadingComments */
static COLD void gen_leading_comments(PgGeneratedFile *g, GoSourceLocation loc)
{
	for (int i = 0; i < loc.n_detached; i++) {
		P(g, (PgComments){loc.detached[i]});
		P(g, "");
	}
	if (loc.leading.n) {
		P(g, (PgComments){loc.leading});
		P(g, "");
	}
}
