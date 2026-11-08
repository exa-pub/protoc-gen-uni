/*
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
 */

/* grpc/kotlin v1.5.0: protoc-gen-grpc-kotlin of grpc-kotlin v1.5.0
 * (https://github.com/grpc/grpc-kotlin/tree/v1.5.0/compiler), the buf.build plugin grpc/kotlin.
 * This file ports compiler/src/main/java/io/grpc/kotlin/generator/: GeneratorRunner.kt,
 * ProtoFileCodeGenerator.kt, ServiceCodeGenerator.kt and the four service generators
 * (ServiceNameGenerator.kt, GrpcClientStubGenerator.kt, GrpcCoroutineServerGenerator.kt,
 * TopLevelConstantsGenerator.kt); the protoc package is protoc.c. Kotlin is built with KotlinPoet
 * 2.2.0 (pkg/languages/kotlin/formatters/kotlinpoet), descriptors with protobuf-java 3.25
 * (pkg/languages/java/protobuf-java). */
#include "uni.h"

#include "protoc.c"

#define ARGS(...) (int)(sizeof((KpArg[]){__VA_ARGS__}) / sizeof(KpArg)), ((KpArg[]){__VA_ARGS__})
#define NAMED(...) (int)(sizeof((KpNamedArg[]){__VA_ARGS__}) / sizeof(KpNamedArg)), ((KpNamedArg[]){__VA_ARGS__})

/* The classes the generators name (KClass::asClassName) */
static KpType *grpc_class(const char *name) { return kp_class("io.grpc", name); }
static KpType *grpc_kotlin_class(const char *name) { return kp_class("io.grpc.kotlin", name); }
static KpType *flow_class(void) { return kp_class("kotlinx.coroutines.flow", "Flow"); }
static KpType *flow_of(KpType *t) { return kp_parameterized(flow_class(), 1, (KpType *[]){t}); }

/* Deprecated("The underlying service method is marked deprecated.") */
static KpAnnotation *deprecated_annotation(void)
{
	KpAnnotation *a = kp_annotation(kp_class("kotlin", "Deprecated"));
	kp_annotation_add_member(a, "%S", ARGS(kp_arg_str("The underlying service method is marked deprecated.")));
	return a;
}

static bool method_deprecated(const JMethod *m) { return jpb_bool(pb_msg(m->proto, MDP_OPTIONS), 33, false); }
static bool client_streaming(const JMethod *m) { return pb_int(m->proto, MDP_CLIENT_STREAMING, 0) != 0; }
static bool server_streaming(const JMethod *m) { return pb_int(m->proto, MDP_SERVER_STREAMING, 0) != 0; }

/* ---- ServiceCodeGenerator.kt ---- */

/* ServiceDescriptor.grpcClass: <java package>.<Service>Grpc */
static KpType *service_grpc_class(const JService *s)
{
	return package_nested_class(java_package(s->file), xcat(s->name, "Grpc"));
}

/* MethodDescriptor.descriptorCode: <Service>Grpc.get<Method>Method() */
static KpCode *descriptor_code(const JMethod *m)
{
	const char *getter = xcat(member_with_prefix("get", method_member_name(m->name)), "Method");
	return kp_code_of("%T.%L()", ARGS(kp_arg_type(service_grpc_class(m->service)), kp_arg_s(getter)));
}

/* The declarations of a service in its object: properties and types (Declarations, all for the
 * enclosing scope). */
typedef struct {
	Vec properties; /* KpProperty* */
	Vec types;      /* KpTypeSpec* */
} Declarations;

/* ---- ServiceNameGenerator.kt ---- */

static void service_name_generate(const JService *s, Declarations *d)
{
	KpProperty *p = kp_property("SERVICE_NAME", kp_class("kotlin", "String"), KM(KM_CONST));
	p->initializer = kp_code_of("%T.SERVICE_NAME", ARGS(kp_arg_type(service_grpc_class(s))));
	vec_push(&d->properties, p);
}

/* ---- GrpcClientStubGenerator.kt ---- */

static KpParam *headers_parameter(void)
{
	KpParam *p = kp_param("headers", grpc_class("Metadata"));
	p->default_value = kp_code_of("%T()", ARGS(kp_arg_type(grpc_class("Metadata"))));
	return p;
}

static KpParam *call_options_parameter(void)
{
	KpParam *p = kp_param("callOptions", grpc_class("CallOptions"));
	p->default_value = kp_code_of("%M", ARGS(kp_arg_member(kp_member(grpc_class("CallOptions"), "DEFAULT"))));
	return p;
}

static KpCode *rpc_stub_kdoc(const JMethod *m, KpParam *parameter, KpParam *headers)
{
	bool cs = client_streaming(m), ss = server_streaming(m);
	Vec parts = {0};
	vec_push(&parts, ss ? "Returns a [%flow:T] that, when collected, executes this RPC and emits responses from the\n"
			      "server as they arrive.  That flow finishes normally if the server closes its response with\n"
			      "[`Status.OK`][%status:T], and fails by throwing a [%statusException:T] otherwise.  If\n"
			      "collecting the flow downstream fails exceptionally (including via cancellation), the RPC\n"
			      "is cancelled with that exception as a cause."
			    : "Executes this RPC and returns the response message, suspending until the RPC completes\n"
			      "with [`Status.OK`][%status:T].  If the RPC completes with another status, a corresponding\n"
			      "[%statusException:T] is thrown.  If this coroutine is cancelled, the RPC is also cancelled\n"
			      "with the corresponding exception as a cause.");
	if (cs && ss)
		vec_push(&parts, "The [%flow:T] of requests is collected once each time the [%flow:T] of responses is\n"
				 "collected. If collection of the [%flow:T] of responses completes normally or\n"
				 "exceptionally before collection of `%parameter:N` completes, the collection of\n"
				 "`%parameter:N` is cancelled.  If the collection of `%parameter:N` completes\n"
				 "exceptionally for any other reason, then the collection of the [%flow:T] of responses\n"
				 "completes exceptionally for the same reason and the RPC is cancelled with that reason.");
	else if (cs)
		vec_push(&parts, "This function collects the [%flow:T] of requests.  If the server terminates the RPC\n"
				 "for any reason before collection of requests is complete, the collection of requests\n"
				 "will be cancelled.  If the collection of requests completes exceptionally for any other\n"
				 "reason, the RPC will be cancelled for that reason and this method will throw that\n"
				 "exception.");
	vec_push(&parts, cs ? "@param %parameter:N A [%flow:T] of request messages."
			    : "@param %parameter:N The request message to send to the server.");
	vec_push(&parts, "@param %headers:N Metadata to attach to the request.  Most users will not need this.");
	vec_push(&parts, ss ? "@return A flow that, when collected, emits the responses from the server."
			    : "@return The single response from the server.");
	KpCode *c = kp_code();
	kp_add_named(c, str_join(&parts, "\n\n"),
		     NAMED({"parameter", kp_arg_name(parameter->name)}, {"flow", kp_arg_type(flow_class())},
			   {"status", kp_arg_type(grpc_class("Status"))}, {"statusException", kp_arg_type(grpc_class("StatusException"))},
			   {"headers", kp_arg_name(headers->name)}));
	return c;
}

static KpFun *generate_rpc_stub(const JMethod *m)
{
	const char *name = method_member_name(m->name);
	message_class_line = 155;
	KpType *request_type = message_class(m->input);
	bool cs = client_streaming(m), ss = server_streaming(m);
	KpParam *parameter = cs ? kp_param("requests", flow_of(request_type)) : kp_param("request", request_type);
	message_class_line = 163;
	KpType *response_type = message_class(m->output);
	message_class_line = 0;
	KpType *return_type = ss ? flow_of(response_type) : response_type;
	static const char *const helpers[] = {"unaryRpc", "clientStreamingRpc", "serverStreamingRpc", "bidiStreamingRpc"};
	KpMember *helper = kp_member(grpc_kotlin_class("ClientCalls"), helpers[(cs ? 1 : 0) + (ss ? 2 : 0)]);
	KpParam *headers = headers_parameter();
	KpFun *f = kp_fun(name);
	vec_push(&f->params, parameter);
	vec_push(&f->params, headers);
	f->ret = return_type;
	kp_add_code(f->kdoc, rpc_stub_kdoc(m, parameter, headers));
	if (method_deprecated(m))
		vec_push(&f->annotations, deprecated_annotation());
	if (!ss)
		f->modifiers |= KM(KM_SUSPEND);
	kp_add_named(f->body, "return %helperMethod:M(\n  channel,\n  %methodDescriptor:L,\n  %parameter:N,\n  callOptions,\n  %headers:N\n)",
		     NAMED({"helperMethod", kp_arg_member(helper)}, {"methodDescriptor", kp_arg_code(descriptor_code(m))},
			   {"parameter", kp_arg_name(parameter->name)}, {"headers", kp_arg_name(headers->name)}));
	kp_fun_build(f);
	return f;
}

/* buildFun(stubName): the override of AbstractCoroutineStub.build */
static KpFun *build_fun(KpType *stub_name)
{
	KpFun *f = kp_fun("build");
	f->ret = stub_name;
	f->modifiers = KM(KM_OVERRIDE);
	vec_push(&f->params, kp_param("channel", grpc_class("Channel")));
	vec_push(&f->params, kp_param("callOptions", grpc_class("CallOptions")));
	kp_add_statement(f->body, "return %T(%N, %N)", ARGS(kp_arg_type(stub_name), kp_arg_name("channel"), kp_arg_name("callOptions")));
	kp_fun_build(f);
	return f;
}

static void client_stub_generate(const JService *s, Declarations *d)
{
	const char *stub_name = xcat(s->name, "CoroutineStub");
	/* not actually a type variable: it keeps the name from being imported */
	KpType *self = kp_type_variable(stub_name);
	KpTypeSpec *t = kp_class_builder(stub_name);
	kp_type_superclass(t, kp_parameterized(grpc_kotlin_class("AbstractCoroutineStub"), 1, (KpType *[]){self}));
	kp_add(t->kdoc, "A stub for issuing RPCs to a(n) %L service as suspending coroutines.", ARGS(kp_arg_s(s->full_name)));
	KpAnnotation *stub_for = kp_annotation(grpc_kotlin_class("StubFor"));
	kp_annotation_add_member(stub_for, "%T::class", ARGS(kp_arg_type(service_grpc_class(s))));
	vec_push(&t->annotations, stub_for);
	KpFun *ctor = kp_constructor();
	vec_push(&ctor->params, kp_param("channel", grpc_class("Channel")));
	vec_push(&ctor->params, call_options_parameter());
	vec_push(&ctor->annotations, kp_annotation(kp_class("kotlin.jvm", "JvmOverloads")));
	kp_type_primary_constructor(t, ctor);
	kp_type_add_superclass_ctor_param(t, "%N", ARGS(kp_arg_name("channel")));
	kp_type_add_superclass_ctor_param(t, "%N", ARGS(kp_arg_name("callOptions")));
	vec_push(&t->funs, build_fun(self));
	for (int i = 0; i < s->methods.n; i++)
		vec_push(&t->funs, generate_rpc_stub(s->methods.v[i]));
	kp_type_build(t);
	vec_push(&d->types, t);
}

/* ---- GrpcCoroutineServerGenerator.kt ---- */

static KpCode *stub_kdoc(const JMethod *m, KpParam *request_param)
{
	Vec sections = {0};
	if (server_streaming(m)) {
		vec_push(&sections, "Returns a [%flow:T] of responses to an RPC for %methodName:L.");
		vec_push(&sections, "If creating or collecting the returned flow fails with a [%statusException:T], the RPC\n"
				    "will fail with the corresponding [%status:T].  If it fails with a\n"
				    "[%cancellationException:T], the RPC will fail with status `Status.CANCELLED`.  If creating\n"
				    "or collecting the returned flow fails for any other reason, the RPC will fail with\n"
				    "`Status.UNKNOWN` with the exception as a cause.");
	} else {
		vec_push(&sections, "Returns the response to an RPC for %methodName:L.");
		vec_push(&sections, "If this method fails with a [%statusException:T], the RPC will fail with the corresponding\n"
				    "[%status:T].  If this method fails with a [%cancellationException:T], the RPC will fail\n"
				    "with status `Status.CANCELLED`.  If this method fails for any other reason, the RPC will\n"
				    "fail with `Status.UNKNOWN` with the exception as a cause.");
	}
	vec_push(&sections, client_streaming(m) ? "@param %requestParam:N A [%flow:T] of requests from the client.  This flow can be\n"
						  "       collected only once and throws [%illegalStateException:T] on attempts to collect\n"
						  "       it more than once."
						: "@param %requestParam:N The request from the client.");
	KpCode *c = kp_code();
	kp_add_named(c, str_join(&sections, "\n\n"),
		     NAMED({"requestParam", kp_arg_name(request_param->name)}, {"methodName", kp_arg_s(m->full_name)},
			   {"flow", kp_arg_type(flow_class())}, {"status", kp_arg_type(grpc_class("Status"))},
			   {"statusException", kp_arg_type(grpc_class("StatusException"))},
			   {"cancellationException", kp_arg_type(kp_class("java.util.concurrent", "CancellationException"))},
			   {"illegalStateException", kp_arg_type(kp_class("java.lang", "IllegalStateException"))}));
	return c;
}

typedef struct {
	KpFun *method_spec;
	KpCode *server_method_def;
} MethodImplStub;

static MethodImplStub service_method_stub(const JMethod *m)
{
	KpType *request_type = message_class(m->input);
	bool cs = client_streaming(m), ss = server_streaming(m);
	KpParam *request_param = cs ? kp_param("requests", flow_of(request_type)) : kp_param("request", request_type);
	KpFun *f = kp_fun(method_member_name(m->name));
	f->modifiers = KM(KM_OPEN);
	vec_push(&f->params, request_param);
	kp_add_statement(f->body, "throw %T(%M.withDescription(%S))",
			 ARGS(kp_arg_type(grpc_class("StatusException")), kp_arg_member(kp_member(grpc_class("Status"), "UNIMPLEMENTED")),
			      kp_arg_str(xcat("Method ", m->full_name, " is unimplemented"))));
	if (method_deprecated(m))
		vec_push(&f->annotations, deprecated_annotation());
	KpType *response_type = message_class(m->output);
	if (ss) {
		f->ret = flow_of(response_type);
	} else {
		f->ret = response_type;
		f->modifiers |= KM(KM_SUSPEND);
	}
	kp_add_code(f->kdoc, stub_kdoc(m, request_param));
	kp_fun_build(f);
	static const char *const factories[] = {"unaryServerMethodDefinition", "clientStreamingServerMethodDefinition",
						"serverStreamingServerMethodDefinition", "bidiStreamingServerMethodDefinition"};
	KpMember *factory = kp_member(grpc_kotlin_class("ServerCalls"), factories[(cs ? 1 : 0) + (ss ? 2 : 0)]);
	if (!f->name[0])
		/* the NoSuchElementException of escapeIfNecessary of the empty name */
		error_trace = FRAME("kotlin.text.StringsKt___StringsKt.first(_Strings.kt:76)")
			FRAME("com.squareup.kotlinpoet.UtilKt.escapeIfNotJavaIdentifier(Util.kt:343)")
				FRAME("com.squareup.kotlinpoet.UtilKt.escapeIfNecessary(Util.kt:283)")
					FRAME("com.squareup.kotlinpoet.UtilKt.escapeIfNecessary$default(Util.kt:283)")
						FRAME("com.squareup.kotlinpoet.CodeBlock$Builder.addArgument(CodeBlock.kt:346)")
							FRAME("com.squareup.kotlinpoet.CodeBlock$Builder.add(CodeBlock.kt:322)")
								FRAME("com.squareup.kotlinpoet.CodeBlock$Companion.of(CodeBlock.kt:489)")
									FRAME("io.grpc.kotlin.generator.GrpcCoroutineServerGenerator.serviceMethodStub(GrpcCoroutineServerGenerator.kt:192)")
										FRAME("io.grpc.kotlin.generator.GrpcCoroutineServerGenerator.implClass(GrpcCoroutineServerGenerator.kt:87)")
											FRAME("io.grpc.kotlin.generator.GrpcCoroutineServerGenerator.generate(GrpcCoroutineServerGenerator.kt:81)")
												FRAMES_GENERATE;
	MethodImplStub stub = {f, kp_code_of("%M(\n  context = this.context,\n  descriptor = %L,\n  implementation = ::%N\n)",
					     ARGS(kp_arg_member(factory), kp_arg_code(descriptor_code(m)), kp_arg_name(f->name)))};
	return stub;
}

static void server_generate(const JService *s, Declarations *d)
{
	const char *impl_name = xcat(s->name, "CoroutineImplBase");
	MethodImplStub *stubs = xalloc(sizeof *stubs * (size_t)(s->methods.n ? s->methods.n : 1));
	for (int i = 0; i < s->methods.n; i++)
		stubs[i] = service_method_stub(s->methods.v[i]);
	KpTypeSpec *t = kp_class_builder(impl_name);
	t->modifiers = KM(KM_ABSTRACT);
	kp_add(t->kdoc, "Skeletal implementation of the %L service based on Kotlin coroutines.", ARGS(kp_arg_s(s->full_name)));
	KpFun *ctor = kp_constructor();
	KpParam *coroutine_context = kp_param("coroutineContext", kp_class("kotlin.coroutines", "CoroutineContext"));
	coroutine_context->default_value = kp_code_of("%T", ARGS(kp_arg_type(kp_class("kotlin.coroutines", "EmptyCoroutineContext"))));
	vec_push(&ctor->params, coroutine_context);
	kp_type_primary_constructor(t, ctor);
	kp_type_superclass(t, grpc_kotlin_class("AbstractCoroutineServerImpl"));
	kp_type_add_superclass_ctor_param(t, "%N", ARGS(kp_arg_name("coroutineContext")));
	KpCode *definition = kp_code_of("%M(%M())", ARGS(kp_arg_member(kp_member(grpc_class("ServerServiceDefinition"), "builder")),
							 kp_arg_member(kp_member(service_grpc_class(s), "getServiceDescriptor"))));
	for (int i = 0; i < s->methods.n; i++) {
		vec_push(&t->funs, stubs[i].method_spec);
		definition = kp_code_of("%L\n  .addMethod(%L)", ARGS(kp_arg_code(definition), kp_arg_code(stubs[i].server_method_def)));
	}
	KpFun *bind = kp_fun("bindService");
	bind->modifiers = KM(KM_OVERRIDE) | KM(KM_FINAL);
	bind->ret = grpc_class("ServerServiceDefinition");
	kp_add_statement(bind->body, "return %L.build()", ARGS(kp_arg_code(definition)));
	kp_fun_build(bind);
	vec_push(&t->funs, bind);
	kp_type_build(t);
	vec_push(&d->types, t);
}

/* ---- TopLevelConstantsGenerator.kt ---- */

static KpAnnotation *jvm_static(void) { return kp_annotation(kp_class("kotlin.jvm", "JvmStatic")); }

static void top_level_constants_generate(const JService *s, Declarations *d)
{
	KpProperty *sd = kp_property("serviceDescriptor", grpc_class("ServiceDescriptor"), 0);
	vec_push(&sd->annotations, jvm_static());
	sd->getter = kp_getter();
	kp_add_statement(sd->getter->body, "return %T.getServiceDescriptor()", ARGS(kp_arg_type(service_grpc_class(s))));
	kp_fun_build(sd->getter);
	vec_push(&d->properties, sd);
	for (int i = 0; i < s->methods.n; i++) {
		const JMethod *m = s->methods.v[i];
		const char *name = method_member_name(m->name);
		KpType *type = kp_parameterized(grpc_class("MethodDescriptor"), 2, (KpType *[]){message_class(m->input), message_class(m->output)});
		KpProperty *p = kp_property(xcat(name, "Method"), type, 0);
		p->getter = kp_getter();
		vec_push(&p->getter->annotations, jvm_static());
		kp_add_statement(p->getter->body, "return %T.%L()",
				 ARGS(kp_arg_type(service_grpc_class(s)), kp_arg_s(xcat(member_with_prefix("get", name), "Method"))));
		kp_fun_build(p->getter);
		vec_push(&d->properties, p);
	}
}

/* ---- ProtoFileCodeGenerator.kt, GeneratorRunner.kt ---- */

static KpFile *generate_code_for_file(const JFile *file)
{
	const char *outer_type_name = xcat(outer_class_simple_name(file), "GrpcKt");
	KpFile *spec = kp_file(java_package(file), outer_type_name);
	bool wrote_anything = false;
	for (int i = 0; i < file->services.n; i++) {
		const JService *s = file->services.v[i];
		/* the generators in order: ServiceNameGenerator, GrpcClientStubGenerator,
		 * GrpcCoroutineServerGenerator, TopLevelConstantsGenerator */
		Declarations d = {0};
		service_name_generate(s, &d);
		client_stub_generate(s, &d);
		server_generate(s, &d);
		top_level_constants_generate(s, &d);
		/* every declaration is for the enclosing scope: the object of the service */
		wrote_anything = true;
		KpTypeSpec *o = kp_object_builder(xcat(s->name, "GrpcKt"));
		kp_add(o->kdoc, "Holder for Kotlin coroutine-based client and server APIs for %L.", ARGS(kp_arg_s(s->full_name)));
		o->properties = d.properties;
		o->types = d.types;
		kp_type_build(o);
		kp_file_add_type(spec, o);
	}
	return wrote_anything ? spec : NULL;
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	return runner_main(req, out);
}

const UniGenerator gen_grpc_kotlin_v1_5_0 = {
	.name = "grpc/kotlin",
	.version = "v1.5.0",
	.generate = generate,
};
