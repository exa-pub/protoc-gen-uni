/* Copyright (c) ScalaPB authors (Nadav Samet and contributors).
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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/GrpcServicePrinter.scala. */

#define G_CHANNEL "_root_.io.grpc.Channel"
#define G_CALL_OPTIONS "_root_.io.grpc.CallOptions"
#define G_ABSTRACT_STUB "_root_.io.grpc.stub.AbstractStub"
#define G_STUB_FACTORY "_root_.io.grpc.stub.AbstractStub.StubFactory"
#define G_STREAM_OBSERVER "_root_.io.grpc.stub.StreamObserver"
#define G_SERVER_CALLS "_root_.io.grpc.stub.ServerCalls"
#define G_CLIENT_CALLS "_root_.scalapb.grpc.ClientCalls"
#define G_SERVER_SERVICE_DEF "_root_.io.grpc.ServerServiceDefinition"

enum { ST_UNARY, ST_CLIENT_STREAMING, ST_SERVER_STREAMING, ST_BIDI };

static int method_stream_type(const JMethod *m)
{
	bool c = method_client_streaming(m), s = method_server_streaming(m);
	return !c && !s ? ST_UNARY : c && !s ? ST_CLIENT_STREAMING : !c && s ? ST_SERVER_STREAMING : ST_BIDI;
}

static const char *observer(const char *t) { return xcat(G_STREAM_OBSERVER, "[", t, "]"); }

static const char *service_method_signature(JMethod *m, bool override_sig)
{
	const char *in = method_type_scala(m->input), *out = method_type_scala(m->output);
	const char *sig;
	switch (method_stream_type(m)) {
	case ST_UNARY: sig = xcat("(request: ", in, "): scala.concurrent.Future[", out, "]"); break;
	case ST_SERVER_STREAMING: sig = xcat("(request: ", in, ", responseObserver: ", observer(out), "): _root_.scala.Unit"); break;
	default: sig = xcat("(responseObserver: ", observer(out), "): ", observer(in)); break;
	}
	return xcat(method_deprecated_annotation(m), override_sig ? "override " : "", "def ", method_name(m), sig);
}

static const char *blocking_method_signature(JMethod *m, bool override_sig)
{
	const char *in = method_type_scala(m->input), *out = method_type_scala(m->output);
	const char *sig;
	switch (method_stream_type(m)) {
	case ST_UNARY: sig = xcat("(request: ", in, "): ", out); break;
	case ST_SERVER_STREAMING: sig = xcat("(request: ", in, "): scala.collection.Iterator[", out, "]"); break;
	default: java_exception("java.lang.IllegalArgumentException", "Invalid method type.");
	}
	return xcat(method_deprecated_annotation(m), override_sig ? "override " : "", "def ", method_name(m), sig);
}

static void grpc_service_trait(Printer *p, JService *s)
{
	p_scala_doc(p, service_comment(s));
	p_addf(p, "trait %s extends _root_.scalapb.grpc.AbstractService {", service_name(s));
	p_indent(p);
	p_addf(p, "override def serviceCompanion: _root_.scalapb.grpc.ServiceCompanion[%s] = %s", service_name(s), service_name(s));
	for (int i = 0; i < s->methods.n; i++) {
		JMethod *m = VEC_AT(s->methods, JMethod, i);
		p_scala_doc(p, method_comment(m));
		p_add(p, service_method_signature(m, false));
	}
	p_outdent(p);
	p_add(p, "}");
}

static void grpc_add_method_implementation(Printer *p, JMethod *m)
{
	p_add(p, ".addMethod(");
	p_addf(p, "  %s,", sn_name_symbol(method_grpc_descriptor(m)));
	p_indent(p);
	const char *in = method_type_scala(m->input), *out = method_type_scala(m->output);
	switch (method_stream_type(m)) {
	case ST_UNARY:
		p_addf(p,
		       "%s.asyncUnaryCall((request: %s, observer: %s[%s]) => {\n"
		       "  serviceImpl.%s(request).onComplete(scalapb.grpc.Grpc.completeObserver(observer))(\n"
		       "    %sexecutionContext)\n}))",
		       G_SERVER_CALLS, in, G_STREAM_OBSERVER, out, method_name(m), file_scala3(m->file) ? "using " : "");
		break;
	case ST_SERVER_STREAMING:
		p_addf(p, "%s.asyncServerStreamingCall((request: %s, observer: %s[%s]) => {\n  serviceImpl.%s(request, observer)\n}))",
		       G_SERVER_CALLS, in, G_STREAM_OBSERVER, out, method_name(m));
		break;
	default:
		p_addf(p, "%s.%s((observer: %s[%s]) => {\n  serviceImpl.%s(observer)\n}))", G_SERVER_CALLS,
		       method_stream_type(m) == ST_CLIENT_STREAMING ? "asyncClientStreamingCall" : "asyncBidiStreamingCall",
		       G_STREAM_OBSERVER, out, method_name(m));
		break;
	}
	p_outdent(p);
}

static void grpc_bind_service(Printer *p, JService *s)
{
	p_addf(p, "def bindService(serviceImpl: %s, executionContext: scala.concurrent.ExecutionContext): %s =", service_name(s),
	       G_SERVER_SERVICE_DEF);
	p_indent(p);
	p_addf(p, "%s.builder(%s)", G_SERVER_SERVICE_DEF, sn_name_symbol(sn_child(service_companion_object(s), "SERVICE")));
	for (int i = 0; i < s->methods.n; i++)
		grpc_add_method_implementation(p, VEC_AT(s->methods, JMethod, i));
	p_add(p, ".build()");
	p_outdent(p);
}

static void grpc_service_trait_companion(Printer *p, JService *s)
{
	p_addf(p, "object %s extends _root_.scalapb.grpc.ServiceCompanion[%s] {", service_name(s), service_name(s));
	p_indent(p);
	p_addf(p, "implicit def serviceCompanion: _root_.scalapb.grpc.ServiceCompanion[%s] = this", service_name(s));
	p_addf(p, "def javaDescriptor: _root_.com.google.protobuf.Descriptors.ServiceDescriptor = %s", service_java_descriptor_source(s));
	p_addf(p, "def scalaDescriptor: _root_.scalapb.descriptors.ServiceDescriptor = %s", service_scala_descriptor_source(s));
	grpc_bind_service(p, s);
	p_outdent(p);
	p_add(p, "}");
}

static bool method_can_be_blocking(const JMethod *m) { return !method_client_streaming(m); }

static void grpc_blocking_client_trait(Printer *p, JService *s)
{
	p_scala_doc(p, service_comment(s));
	p_addf(p, "trait %sBlockingClient {", s->name);
	p_indent(p);
	p_addf(p, "def serviceCompanion: _root_.scalapb.grpc.ServiceCompanion[%s] = %s", service_name(s), service_name(s));
	for (int i = 0; i < s->methods.n; i++) {
		JMethod *m = VEC_AT(s->methods, JMethod, i);
		if (!method_can_be_blocking(m))
			continue;
		p_scala_doc(p, method_comment(m));
		p_add(p, blocking_method_signature(m, false));
	}
	p_outdent(p);
	p_add(p, "}");
}

static void grpc_client_method_impl(Printer *p, JMethod *m, bool blocking)
{
	const char *sig = xcat(blocking ? blocking_method_signature(m, true) : service_method_signature(m, true), " = {");
	const char *kind;
	switch (method_stream_type(m)) {
	case ST_UNARY: kind = "UnaryCall"; break;
	case ST_SERVER_STREAMING: kind = "ServerStreamingCall"; break;
	case ST_CLIENT_STREAMING: kind = "ClientStreamingCall"; break;
	default: kind = "BidiStreamingCall"; break;
	}
	Vec args = {0};
	vec_push(&args, "channel");
	vec_push(&args, (void *)sn_name_symbol(method_grpc_descriptor(m)));
	vec_push(&args, "options");
	if (!method_client_streaming(m))
		vec_push(&args, "request");
	if ((method_client_streaming(m) || method_server_streaming(m)) && !blocking)
		vec_push(&args, "responseObserver");
	p_scala_doc(p, method_comment(m));
	p_add(p, sig);
	p_indent(p);
	p_addf(p, "%s.%s%s(%s)", G_CLIENT_CALLS, blocking ? "blocking" : "async", kind, join(&args, ", "));
	p_outdent(p);
	p_add(p, "}");
	p_newline(p);
}

static void grpc_stub_implementation(Printer *p, JService *s, const char *class_name, const char *base_class, bool blocking)
{
	p_addf(p, "class %s(channel: %s, options: %s = %s.DEFAULT) extends %s[%s](channel, options) with %s {", class_name, G_CHANNEL,
	       G_CALL_OPTIONS, G_CALL_OPTIONS, G_ABSTRACT_STUB, class_name, base_class);
	p_indent(p);
	for (int i = 0; i < s->methods.n; i++) {
		JMethod *m = VEC_AT(s->methods, JMethod, i);
		if (blocking && !method_can_be_blocking(m))
			continue;
		grpc_client_method_impl(p, m, blocking);
	}
	p_addf(p, "override def build(channel: %s, options: %s): %s = new %s(channel, options)", G_CHANNEL, G_CALL_OPTIONS, class_name,
	       class_name);
	p_outdent(p);
	p_add(p, "}");
}

static void grpc_stub_companion(Printer *p, const char *class_name)
{
	p_addf(p, "object %s extends %s[%s] {", class_name, G_STUB_FACTORY, class_name);
	p_indent(p);
	p_addf(p, "override def newStub(channel: %s, options: %s): %s = new %s(channel, options)", G_CHANNEL, G_CALL_OPTIONS, class_name,
	       class_name);
	p_newline(p);
	p_addf(p, "implicit val stubFactory: %s[%s] = this", G_STUB_FACTORY, class_name);
	p_outdent(p);
	p_add(p, "}");
}

static const char *grpc_marshaller(JMsg *t)
{
	if (method_type_custom(t))
		return xcat("_root_.scalapb.grpc.Marshaller.forTypeMappedType[", method_type_base(t), ", ", method_type_scala(t), "]");
	return xcat("_root_.scalapb.grpc.Marshaller.forMessage[", method_type_scala(t), "]");
}

static void grpc_method_descriptor(Printer *p, JMethod *m)
{
	static const char *const types[] = {"UNARY", "CLIENT_STREAMING", "SERVER_STREAMING", "BIDI_STREAMING"};
	const char *gmd = "_root_.io.grpc.MethodDescriptor";
	p_addf(p,
	       "%sval %s: %s[%s, %s] =\n  %s.newBuilder()\n    .setType(%s.MethodType.%s)\n"
	       "    .setFullMethodName(%s.generateFullMethodName(\"%s\", \"%s\"))\n    .setSampledToLocalTracing(true)\n"
	       "    .setRequestMarshaller(%s)\n    .setResponseMarshaller(%s)\n"
	       "    .setSchemaDescriptor(_root_.scalapb.grpc.ConcreteProtoMethodDescriptorSupplier.fromMethodDescriptor(%s))\n    .build()\n",
	       method_deprecated_annotation(m), sn_name_symbol(method_grpc_descriptor(m)), gmd, method_type_scala(m->input),
	       method_type_scala(m->output), gmd, gmd, types[method_stream_type(m)], gmd, m->service->full_name, m->name,
	       grpc_marshaller(m->input), grpc_marshaller(m->output), method_java_descriptor_source(m));
}

static void grpc_service_descriptor(Printer *p, JService *s)
{
	const char *gsd = "_root_.io.grpc.ServiceDescriptor";
	p_addf(p, "val %s: %s =", sn_name_symbol(sn_child(service_companion_object(s), "SERVICE")), gsd);
	p_indent(p);
	p_addf(p, "%s.newBuilder(\"%s\")", gsd, s->full_name);
	p_indent(p);
	p_addf(p, ".setSchemaDescriptor(new _root_.scalapb.grpc.ConcreteProtoFileDescriptorSupplier(%s.javaDescriptor))",
	       sn_full_name(file_descriptor_object(s->file)));
	for (int i = 0; i < s->methods.n; i++)
		p_addf(p, ".addMethod(%s)", sn_name_symbol(method_grpc_descriptor(VEC_AT(s->methods, JMethod, i))));
	p_add(p, ".build()");
	p_outdent(p);
	p_outdent(p);
	p_newline(p);
}

static void print_service(Printer *p, JService *s)
{
	const char *blocking_stub = xcat(s->name, "BlockingStub"), *stub = xcat(s->name, "Stub");
	/* the stubs are built when the printer is created (vals), before the companion object */
	p_add(p, "");
	p_addf(p, "%sobject %s {", service_deprecated_annotation(s), sn_name_symbol(service_companion_object(s)));
	p_indent(p);
	for (int i = 0; i < s->methods.n; i++)
		grpc_method_descriptor(p, VEC_AT(s->methods, JMethod, i));
	grpc_service_descriptor(p, s);
	grpc_service_trait(p, s);
	p_newline(p);
	grpc_service_trait_companion(p, s);
	p_newline(p);
	grpc_blocking_client_trait(p, s);
	p_newline(p);
	grpc_stub_implementation(p, s, blocking_stub, xcat(s->name, "BlockingClient"), true);
	p_newline(p);
	grpc_stub_implementation(p, s, stub, service_name(s), false);
	p_newline(p);
	grpc_stub_companion(p, stub);
	p_newline(p);
	p_addf(p, "def bindService(serviceImpl: %s, executionContext: scala.concurrent.ExecutionContext): %s = %s.bindService(serviceImpl, executionContext)",
	       service_name(s), G_SERVER_SERVICE_DEF, service_name(s));
	p_newline(p);
	p_addf(p, "def blockingStub(channel: %s): %s = new %s(channel)", G_CHANNEL, blocking_stub, blocking_stub);
	p_newline(p);
	p_addf(p, "def stub(channel: %s): %s = new %s(channel)", G_CHANNEL, stub, stub);
	p_newline(p);
	p_addf(p, "def javaDescriptor: _root_.com.google.protobuf.Descriptors.ServiceDescriptor = %s", service_java_descriptor_source(s));
	p_newline(p);
	p_outdent(p);
	p_add(p, "}");
}
