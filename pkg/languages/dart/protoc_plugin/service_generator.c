/* Ports of protoc_plugin 25.1.0 lib/src/service_generator.dart (ServiceGenerator),
 * lib/src/client_generator.dart (ClientApiGenerator) and lib/src/grpc_generator.dart
 * (GrpcServiceGenerator, _GrpcMethod).
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static void svc_put(Map *m, Vec *keys, const char *key, void *val)
{
	if (!map_has(m, key))
		vec_push(keys, (void *)key);
	map_put(m, key, val);
}

static MsgGen *svc_messageType(GenerationContext *ctx, const char *fqname)
{
	Pc *t = ctx_getFieldType(ctx, fqname);
	if (t && t->kind != PC_MESSAGE)
		dart_throw("type 'EnumGenerator' is not a subtype of type 'MessageGenerator?' in type cast");
	return (MsgGen *)t;
}

static const char *serviceBaseName(const char *originalName)
{
	return has_suffix(originalName, "Service") ? xcat(originalName, "Base") : xcat(originalName, "ServiceBase");
}

static ServiceGen *svc_new(PbMsg *descriptor, FileGen *fileGen, Map *usedNames, int index)
{
	ServiceGen *s = xalloc(sizeof *s);
	s->descriptor = descriptor;
	s->fileGen = fileGen;
	s->serviceIndex = index;
	s->classname = disambiguateName(serviceBaseName(avoidInitialUnderscore(pb_str(descriptor, SDP_NAME))), usedNames,
					SUFFIX_DEFAULT, 0, NULL, NULL);
	return s;
}

static const char *dottedName(Pc *pc) { return xcat(".", pc->fullName); }

/* _addDepsRecursively, as an explicit stack (pre-order like the recursion). */
static void svc_addDepsRecursively(ServiceGen *s, MsgGen *root)
{
	typedef struct {
		MsgGen *mg;
		int next;
	} Frame;
	Frame *stack = NULL;
	int n = 0, cap = 0;
	const char *key = dottedName(&root->pc);
	if (map_has(&s->transitiveDeps, key)) {
		svc_put(&s->deps, &s->depKeys, key, root);
		return;
	}
	msg_checkResolved(root);
	svc_put(&s->deps, &s->depKeys, key, root);
	svc_put(&s->transitiveDeps, &s->transitiveKeys, key, root);
	cap = 16;
	stack = xalloc((size_t)cap * sizeof *stack);
	stack[n++] = (Frame){root, 0};
	while (n) {
		Frame *f = &stack[n - 1];
		if (f->next >= f->mg->fieldList.n) {
			n--;
			continue;
		}
		Field *field = VEC_AT(f->mg->fieldList, Field, f->next++);
		if (!(bt_isGroup(&field->baseType) || bt_isMessage(&field->baseType)))
			continue;
		MsgGen *mg = (MsgGen *)field->baseType.generator;
		key = dottedName(&mg->pc);
		if (map_has(&s->transitiveDeps, key))
			continue;
		msg_checkResolved(mg);
		svc_put(&s->transitiveDeps, &s->transitiveKeys, key, mg);
		if (n == cap) {
			stack = xrealloc(stack, (size_t)cap * sizeof *stack, (size_t)cap * 2 * sizeof *stack);
			cap *= 2;
		}
		stack[n++] = (Frame){mg, 0};
	}
}

static void svc_addDependency(ServiceGen *s, GenerationContext *ctx, const char *fqname, const char *location, bool recursive)
{
	if (map_has(&s->deps, fqname))
		return;
	MsgGen *mg = svc_messageType(ctx, fqname);
	if (!mg) {
		svc_put(&s->undefinedDeps, &s->undefinedKeys, fqname, (void *)location);
		return;
	}
	if (recursive) {
		svc_addDepsRecursively(s, mg);
	} else {
		msg_checkResolved(mg);
		svc_put(&s->deps, &s->depKeys, dottedName(&mg->pc), mg);
	}
}

static void svc_resolve(ServiceGen *s, GenerationContext *ctx)
{
	for (int i = 0; i < pb_count(s->descriptor, SDP_METHOD); i++) {
		PbMsg *m = pb_msg_at(s->descriptor, SDP_METHOD, i);
		const char *name = pb_str(m, MDP_NAME);
		svc_addDependency(s, ctx, pb_str(m, MDP_INPUT_TYPE), xcat("input type of ", name), true);
		svc_addDependency(s, ctx, pb_str(m, MDP_OUTPUT_TYPE), xcat("output type of ", name), true);
	}
}

static void svc_addImportsTo(ServiceGen *s, Vec *imports)
{
	for (int i = 0; i < s->depKeys.n; i++)
		set_add_gen(imports, ((MsgGen *)map_get(&s->deps, s->depKeys.v[i]))->pc.fileGen);
}

static void svc_addConstantImportsTo(ServiceGen *s, Vec *imports)
{
	for (int i = 0; i < s->transitiveKeys.n; i++)
		set_add_gen(imports, ((MsgGen *)map_get(&s->transitiveDeps, s->transitiveKeys.v[i]))->pc.fileGen);
}

/* _getDartClassName */
static const DType *svc_getDartClassName(ServiceGen *s, const char *fqname, bool forMainFile)
{
	MsgGen *generator = map_get(&s->deps, fqname);
	if (!generator) {
		const char *location = map_get(&s->undefinedDeps, fqname);
		dart_throw(xfmt("FAILURE: Unknown type reference (%s) for %s", fqname, location ? location : "null"));
	}
	if (forMainFile && strcmp(s->fileGen->protoFileUri, generator->pc.fileGen->protoFileUri) == 0)
		return dt_new(NULL, generator->pc.classname);
	return dt_new(importPrefix(s->fileGen, &generator->pc), generator->pc.classname);
}

static Node *future_of(Node *t) { return ds_type(asyncImportPrefix, "Future", ds_targs(t, (Node *)0), false); }

static Node *unknown_method_throw(void)
{
	return ds_expr_stmt(ds_throw(ds_call(ds_id(coreImportPrefix), "ArgumentError", NULL,
					     ds_args(ds_interp("'Unknown method: ", "methodName", "'"), (Node *)0))));
}

static Node *svc_switch(ServiceGen *s, bool dispatch)
{
	NodeList cases = {0};
	for (int i = 0; i < pb_count(s->descriptor, SDP_METHOD); i++) {
		PbMsg *m = pb_msg_at(s->descriptor, SDP_METHOD, i);
		const DType *inputClass = svc_getDartClassName(s, pb_str(m, MDP_INPUT_TYPE), false);
		Node *e = dispatch ? ds_call(NULL, lowerCaseFirstLetter(pb_str(m, MDP_NAME)), NULL,
					     ds_args(ds_id("ctx"), ds_as(ds_id("request"), dt_node(inputClass, false)), (Node *)0))
				   : ds_call(inputClass->prefix ? ds_id(inputClass->prefix) : NULL, inputClass->name, NULL,
					     ds_args((Node *)0));
		ds_push(&cases, ds_case(ds_str(xfmt("'%s'", pb_str(m, MDP_NAME))), NL(ds_return(e))));
	}
	ds_push(&cases, ds_default(NL(unknown_method_throw())));
	return ds_switch(ds_id("methodName"), cases);
}

/* generate: the .pbserver.dart class. */
static void svc_generate(ServiceGen *s, Out *o)
{
	Node *cls = out_add(o, ds_class(NONE, true, s->classname, pb_type("GeneratedService"), NONE, NONE));
	OutBody body = out_begin_body(o, &cls->body->list);
	int nm = pb_count(s->descriptor, SDP_METHOD);
	for (int i = 0; i < nm; i++) {
		PbMsg *m = pb_msg_at(s->descriptor, SDP_METHOD, i);
		const DType *inputClass = svc_getDartClassName(s, pb_str(m, MDP_INPUT_TYPE), false);
		const DType *outputClass = svc_getDartClassName(s, pb_str(m, MDP_OUTPUT_TYPE), false);
		out_add(o, ds_method(NONE, KW_NONE, future_of(dt_node(outputClass, false)), KW_NONE,
				     lowerCaseFirstLetter(pb_str(m, MDP_NAME)),
				     ds_params(NL(ds_param(pb_type("ServerContext"), "ctx", NULL), ds_param(dt_node(inputClass, false), "request", NULL)), NONE, false),
				     ds_empty_body()));
	}
	out_blank(o);
	out_add(o, ds_method(NONE, KW_NONE, pb_type("GeneratedMessage"), KW_NONE, "createRequest",
			     ds_params(NL(ds_param(core_type("String"), "methodName", NULL)), NONE, false),
			     ds_bbody(KW_NONE, false, ds_block(NL(svc_switch(s, false))))));
	out_blank(o);
	out_add(o, ds_method(NONE, KW_NONE, future_of(pb_type("GeneratedMessage")), KW_NONE, "handleCall",
			     ds_params(NL(ds_param(pb_type("ServerContext"), "ctx", NULL), ds_param(core_type("String"), "methodName", NULL),
					  ds_param(pb_type("GeneratedMessage"), "request", NULL)), NONE, false),
			     ds_bbody(KW_NONE, false, ds_block(NL(svc_switch(s, true))))));
	out_blank(o);
	Node *jsonType = ds_type(coreImportPrefix, "Map", ds_targs(core_type("String"), core_type("dynamic"), (Node *)0), false);
	out_add(o, ds_method(NONE, KW_NONE, jsonType, KW_GET, "$json", NULL, ds_arrow(ds_id(xcat(s->classname, "$json")))));
	Node *messageJsonType = ds_type(coreImportPrefix, "Map",
					ds_targs(core_type("String"),
						 ds_type(coreImportPrefix, "Map", ds_targs(core_type("String"), core_type("dynamic"), (Node *)0), false),
						 (Node *)0), false);
	out_add(o, ds_method(NONE, KW_NONE, messageJsonType, KW_GET, "$messageJson", NULL,
			     ds_arrow(ds_id(xcat(s->classname, "$messageJson")))));
	out_end_body(o, body, cls->body->rb);
	out_blank(o);
}

static const char *svc_binaryDescriptorName(ServiceGen *s)
{
	char *prefix = lowerCaseFirstLetter(s->classname);
	if (has_suffix(prefix, "Base"))
		prefix[strlen(prefix) - 4] = 0;
	return xcat(prefix, "Descriptor");
}

/* generateConstants */
static void svc_generateConstants(ServiceGen *s, Out *o)
{
	Node *jsonType = ds_type(coreImportPrefix, "Map", ds_targs(core_type("String"), core_type("dynamic"), (Node *)0), false);
	out_add(o, ds_toplevel(NONE, KW_CONST, jsonType, xcat(s->classname, "$json"), writeJsonConst(dpb_json_map(s->descriptor))));
	out_blank(o);
	NodeList entries = {0};
	for (int i = 0; i < s->transitiveKeys.n; i++) {
		const char *key = s->transitiveKeys.v[i];
		MsgGen *mg = map_get(&s->transitiveDeps, key);
		ds_push(&entries, ds_entry(ds_str(xfmt("'%s'", key)), getJsonConstant(&mg->pc, s->fileGen)));
	}
	Node *map = ds_map(NULL, NULL, entries);
	map->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
	Node *messageJsonType = ds_type(coreImportPrefix, "Map",
					ds_targs(core_type("String"),
						 ds_type(coreImportPrefix, "Map", ds_targs(core_type("String"), core_type("dynamic"), (Node *)0), false),
						 (Node *)0), false);
	out_add(o, ds_toplevel(NL(deprecated_annot(xfmt("Use %s instead", svc_binaryDescriptorName(s)))), KW_CONST,
			       messageJsonType, xcat(s->classname, "$messageJson"), map));
	out_blank(o);
	if (s->undefinedKeys.n) {
		for (int i = 0; i < s->undefinedKeys.n; i++) {
			const char *name = s->undefinedKeys.v[i];
			out_comment(o, xfmt("// can't resolve (%s) used by %s", name, (const char *)map_get(&s->undefinedDeps, name)), 0);
		}
		out_blank(o);
	}
}

/* ---- ClientApiGenerator ---- */

static ClientApiGen *client_new(ServiceGen *service, Map *usedNames, int repeatedFieldIndex)
{
	ClientApiGen *c = xalloc(sizeof *c);
	c->service = service;
	c->repeatedFieldIndex = repeatedFieldIndex;
	c->className = disambiguateName(avoidInitialUnderscore(pb_str(service->descriptor, SDP_NAME)), usedNames, SUFFIX_DEFAULT, 0,
					NULL, NULL);
	reservedMemberNames(&c->usedMethodNames);
	return c;
}

static void client_generate(ClientApiGen *c, Out *o)
{
	FileGen *fg = c->service->fileGen;
	IntList servicePath = il_append(fg->pc.fieldPath, 6, c->repeatedFieldIndex);
	out_commentBlock(o, commentBlock(fg, servicePath));
	NodeList meta = {0};
	if (pb_int(pb_msg(c->service->descriptor, SDP_OPTIONS), SO_DEPRECATED, 0))
		ds_push(&meta, deprecated_annot("This service is deprecated"));
	Node *cls = out_add(o, ds_class(meta, false, xcat(c->className, "Api"), NULL, NONE, NONE));
	OutBody body = out_begin_body(o, &cls->body->list);
	out_add(o, ds_field(NONE, false, KW_FINAL, pb_type("RpcClient"), "_client", NULL));
	out_blank(o);
	out_add(o, ds_ctor(NONE, KW_NONE, xcat(c->className, "Api"), NULL, ds_params(NL(ds_fparam(KW_THIS, "_client")), NONE, false),
			   ds_empty_body()));
	out_blank(o);
	for (int i = 0; i < pb_count(c->service->descriptor, SDP_METHOD); i++) {
		PbMsg *method = pb_msg_at(c->service->descriptor, SDP_METHOD, i);
		const char *methodName = disambiguateName(avoidInitialUnderscore(lowerCaseFirstLetter(pb_str(method, MDP_NAME))),
							  &c->usedMethodNames, SUFFIX_DEFAULT, 0, NULL, NULL);
		const DType *inputType = svc_getDartClassName(c->service, pb_str(method, MDP_INPUT_TYPE), true);
		const DType *outputType = svc_getDartClassName(c->service, pb_str(method, MDP_OUTPUT_TYPE), true);
		out_commentBlock(o, commentBlock(fg, il_append(servicePath, 2, i)));
		NodeList mmeta = {0};
		if (pb_int(pb_msg(method, MDP_OPTIONS), MTHO_DEPRECATED, 0))
			ds_push(&mmeta, deprecated_annot("This method is deprecated"));
		Node *newOutput = ds_call(outputType->prefix ? ds_id(outputType->prefix) : NULL, outputType->name, NULL, ds_args((Node *)0));
		Node *invoke = ds_call(ds_id("_client"), "invoke", ds_targs(dt_node(outputType, false), (Node *)0),
				       ds_args(ds_id("ctx"), ds_str(xfmt("'%s'", c->className)), ds_str(xfmt("'%s'", pb_str(method, MDP_NAME))),
					       ds_id("request"), newOutput, (Node *)0));
		ds_nl(invoke, 1, 2);
		Node *arrow = ds_arrow(invoke);
		ds_tnl(arrow->semi, 1, 0);
		out_add(o, ds_method(mmeta, KW_NONE, future_of(dt_node(outputType, false)), KW_NONE, methodName,
				     ds_params(NL(ds_param(ds_type(protobufImportPrefix, "ClientContext", NULL, true), "ctx", NULL),
						  ds_param(dt_node(inputType, false), "request", NULL)), NONE, false),
				     arrow));
	}
	out_end_body(o, body, cls->body->rb);
	out_blank(o);
}

/* ---- GrpcServiceGenerator ---- */

typedef struct {
	const char *grpcName, *dartName, *serviceName;
	bool clientStreaming, serverStreaming, deprecated;
	const DType *requestType, *responseType;
} GrpcMethod;

static ServiceGen *grpc_new(PbMsg *descriptor, FileGen *fileGen, int index)
{
	ServiceGen *s = xalloc(sizeof *s);
	s->descriptor = descriptor;
	s->fileGen = fileGen;
	s->serviceIndex = index;
	const char *name = pb_str(descriptor, SDP_NAME);
	const char *package = fileGen->pc.package;
	s->fullServiceName = *package ? xfmt("%s.%s", package, name) : name;
	s->clientClassname = has_suffix(name, "Client") ? name : xcat(name, "Client");
	s->serviceClassname = has_suffix(name, "Service") ? xcat(name, "Base") : xcat(name, "ServiceBase");
	return s;
}

static void grpc_resolve(ServiceGen *s, GenerationContext *ctx)
{
	for (int i = 0; i < pb_count(s->descriptor, SDP_METHOD); i++) {
		PbMsg *method = pb_msg_at(s->descriptor, SDP_METHOD, i);
		GrpcMethod *m = xalloc(sizeof *m);
		m->grpcName = pb_str(method, MDP_NAME);
		m->dartName = lowerCaseFirstLetter(m->grpcName);
		m->clientStreaming = pb_int(method, MDP_CLIENT_STREAMING, 0) != 0;
		m->serverStreaming = pb_int(method, MDP_SERVER_STREAMING, 0) != 0;
		svc_addDependency(s, ctx, pb_str(method, MDP_INPUT_TYPE), xcat("input type of ", m->grpcName), false);
		svc_addDependency(s, ctx, pb_str(method, MDP_OUTPUT_TYPE), xcat("output type of ", m->grpcName), false);
		m->requestType = svc_getDartClassName(s, pb_str(method, MDP_INPUT_TYPE), false);
		m->responseType = svc_getDartClassName(s, pb_str(method, MDP_OUTPUT_TYPE), false);
		m->serviceName = s->fullServiceName;
		m->deprecated = pb_int(pb_msg(method, MDP_OPTIONS), MTHO_DEPRECATED, 0) != 0;
		vec_push(&s->methods, m);
	}
}

static Node *grpc_generic(const char *prefix, const char *name, const DType *arg)
{
	return ds_type(prefix, name, ds_targs(dt_node(arg, false), (Node *)0), false);
}

static Node *grpc_argumentType(GrpcMethod *m)
{
	return m->clientStreaming ? grpc_generic(asyncImportPrefix, "Stream", m->requestType) : dt_node(m->requestType, false);
}

static Node *grpc_serverReturnType(GrpcMethod *m)
{
	return grpc_generic(asyncImportPrefix, m->serverStreaming ? "Stream" : "Future", m->responseType);
}

static Node *grpc_serviceName(ServiceGen *s) { return annot_call(protobufImportPrefix, "GrpcServiceName", ds_str(xfmt("'%s'", s->fullServiceName))); }

static Node *grpc_writeToBuffer_closure(const DType *type)
{
	return ds_fnexpr(ds_params(NL(ds_param(dt_node(type, false), "value", NULL)), NONE, false),
			 ds_arrow(ds_call(ds_id("value"), "writeToBuffer", NULL, ds_args((Node *)0))));
}

static void grpc_generateClient(ServiceGen *s, Out *o)
{
	FileGen *fg = s->fileGen;
	IntList servicePath = il_append(fg->pc.fieldPath, 6, s->serviceIndex);
	out_commentBlock(o, commentBlock(fg, servicePath));
	NodeList meta = {0};
	PbMsg *options = pb_msg(s->descriptor, SDP_OPTIONS);
	if (pb_int(options, SO_DEPRECATED, 0))
		ds_push(&meta, deprecated_annot("This service is deprecated"));
	ds_push(&meta, grpc_serviceName(s));
	Node *cls = out_add(o, ds_class(meta, false, s->clientClassname, ds_type(grpcImportPrefix, "Client", NULL, false), NONE, NONE));
	OutBody body = out_begin_body(o, &cls->body->list);
	/* getExtension returns the default value ('') of an extension that is not set, so the checks
	 * for null upstream are always true. */
	{
		out_comment(o, "/// The hostname for this service.", 0);
		out_add(o, ds_field(NONE, true, KW_CONST, core_type("String"), "defaultHost",
				    ds_str(xfmt("'%s'", pb_str(options, DPB_EXT_DEFAULT_HOST)))));
		out_blank(o);
	}
	{
		out_comment(o, "/// OAuth scopes needed for the client.", 0);
		NodeList scopes = {0};
		Vec parts = str_split(pb_str(options, DPB_EXT_OAUTH_SCOPES), ',');
		for (int i = 0; i < parts.n; i++)
			ds_push(&scopes, ds_str(xfmt("'%s'", VEC_AT(parts, char, i))));
		Node *list = ds_list(NULL, NULL, scopes);
		list->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
		out_add(o, ds_field(NONE, true, KW_CONST, ds_type(coreImportPrefix, "List", ds_targs(core_type("String"), (Node *)0), false),
				    "oauthScopes", list));
		out_blank(o);
	}
	out_add(o, ds_ctor(NONE, KW_NONE, s->clientClassname, NULL,
			   ds_params(NL(ds_fparam(KW_SUPER, "channel")), NL(ds_fparam(KW_SUPER, "options"), ds_fparam(KW_SUPER, "interceptors")), true),
			   ds_empty_body()));
	for (int i = 0; i < s->methods.n; i++) {
		GrpcMethod *m = VEC_AT(s->methods, GrpcMethod, i);
		out_blank(o);
		out_commentBlock(o, commentBlock(fg, il_append(servicePath, 2, i)));
		NodeList mmeta = {0};
		if (m->deprecated)
			ds_push(&mmeta, deprecated_annot("This method is deprecated"));
		const char *method = xcat("_$", m->dartName);
		Node *call;
		if (m->clientStreaming && m->serverStreaming)
			call = ds_call(NULL, "$createStreamingCall", NULL,
				       ds_args(ds_id(method), ds_id("request"), ds_named("options", ds_id("options")), (Node *)0));
		else if (m->clientStreaming)
			call = ds_prop(ds_call(NULL, "$createStreamingCall", NULL,
					       ds_args(ds_id(method), ds_id("request"), ds_named("options", ds_id("options")), (Node *)0)),
				       "single");
		else if (m->serverStreaming)
			call = ds_call(NULL, "$createStreamingCall", NULL,
				       ds_args(ds_id(method),
					       ds_call(ds_path(asyncImportPrefix, "Stream", (char *)0), "fromIterable", NULL,
						       ds_args(ds_list(NULL, NULL, NL(ds_id("request"))), (Node *)0)),
					       ds_named("options", ds_id("options")), (Node *)0));
		else
			call = ds_call(NULL, "$createUnaryCall", NULL,
				       ds_args(ds_id(method), ds_id("request"), ds_named("options", ds_id("options")), (Node *)0));
		Node *params = ds_params(NL(ds_param(grpc_argumentType(m), "request", NULL)),
					 NL(ds_param(ds_type(grpcImportPrefix, "CallOptions", NULL, true), "options", NULL)), true);
		params->flags |= DS_TRAILING_COMMA;
		out_add(o, ds_method(mmeta, KW_NONE,
				     grpc_generic(grpcImportPrefix, m->serverStreaming ? "ResponseStream" : "ResponseFuture", m->responseType),
				     KW_NONE, m->dartName, params, ds_bbody(KW_NONE, false, ds_block(NL(ds_return(call))))));
	}
	out_blank(o);
	if (s->methods.n) {
		out_comment(o, "// method descriptors", 2);
		out_blank(o);
		for (int i = 0; i < s->methods.n; i++) {
			GrpcMethod *m = VEC_AT(s->methods, GrpcMethod, i);
			Node *a1 = ds_str(xfmt("'/%s/%s'", m->serviceName, m->grpcName));
			Node *a2 = grpc_writeToBuffer_closure(m->requestType);
			Node *a3 = ds_prop(dt_expr(m->responseType), "fromBuffer");
			ds_nl(a1, 1, 4);
			ds_nl(a2, 1, 4);
			ds_nl(a3, 1, 4);
			Node *init = ds_call(ds_id(grpcImportPrefix), "ClientMethod",
					     ds_targs(dt_node(m->requestType, false), dt_node(m->responseType, false), (Node *)0),
					     ds_args(a1, a2, a3, (Node *)0));
			out_add(o, ds_field(NONE, true, KW_FINAL, NULL, xcat("_$", m->dartName), init));
		}
	}
	out_end_body(o, body, cls->body->rb);
}

static void grpc_generateService(ServiceGen *s, Out *o)
{
	Node *cls = out_add(o, ds_class(NL(grpc_serviceName(s)), true, s->serviceClassname,
					ds_type(grpcImportPrefix, "Service", NULL, false), NONE, NONE));
	OutBody body = out_begin_body(o, &cls->body->list);
	out_add(o, ds_method(NONE, KW_NONE, core_type("String"), KW_GET, "$name", NULL,
			     ds_arrow(ds_str(xfmt("'%s'", s->fullServiceName)))));
	out_blank(o);
	NodeList regs = {0};
	for (int i = 0; i < s->methods.n; i++) {
		GrpcMethod *m = VEC_AT(s->methods, GrpcMethod, i);
		Node *args[6] = {
			ds_str(xfmt("'%s'", m->grpcName)),
			ds_id(m->clientStreaming ? m->dartName : xcat(m->dartName, "_Pre")),
			ds_lit(N_BooleanLiteral, TT_IDENTIFIER, m->clientStreaming ? "true" : "false"),
			ds_lit(N_BooleanLiteral, TT_IDENTIFIER, m->serverStreaming ? "true" : "false"),
			ds_fnexpr(ds_params(NL(ds_param(ds_type(coreImportPrefix, "List", ds_targs(core_type("int"), (Node *)0), false), "value", NULL)), NONE, false),
				  ds_arrow(ds_call(dt_expr(m->requestType), "fromBuffer", NULL, ds_args(ds_id("value"), (Node *)0)))),
			grpc_writeToBuffer_closure(m->responseType),
		};
		NodeList al = {0};
		for (int k = 0; k < 6; k++)
			ds_push(&al, ds_nl(args[k], 1, 4));
		Node *sm = ds_call(ds_id(grpcImportPrefix), "ServiceMethod",
				   ds_targs(dt_node(m->requestType, false), dt_node(m->responseType, false), (Node *)0), ds_argl(al));
		ds_push(&regs, ds_expr_stmt(ds_call(NULL, "$addMethod", NULL, ds_args(sm, (Node *)0))));
	}
	out_add(o, ds_ctor(NONE, KW_NONE, s->serviceClassname, NULL, ds_params(NONE, NONE, false), ds_bbody(KW_NONE, false, ds_block(regs))));
	out_blank(o);
	for (int i = 0; i < s->methods.n; i++) {
		GrpcMethod *m = VEC_AT(s->methods, GrpcMethod, i);
		if (!m->clientStreaming) {
			Node *call = ds_call(NULL, m->dartName, NULL, ds_args(ds_id("$call"), ds_await(ds_id("$request")), (Node *)0));
			Node *stmt = m->serverStreaming ? ds_yield(true, call) : ds_return(call);
			out_add(o, ds_method(NONE, KW_NONE, grpc_serverReturnType(m), KW_NONE, xcat(m->dartName, "_Pre"),
					     ds_params(NL(ds_param(ds_type(grpcImportPrefix, "ServiceCall", NULL, false), "$call", NULL),
							  ds_param(grpc_generic(asyncImportPrefix, "Future", m->requestType), "$request", NULL)), NONE, false),
					     ds_bbody(KW_ASYNC, m->serverStreaming, ds_block(NL(stmt)))));
			out_blank(o);
		}
		out_add(o, ds_method(NONE, KW_NONE, grpc_serverReturnType(m), KW_NONE, m->dartName,
				     ds_params(NL(ds_param(ds_type(grpcImportPrefix, "ServiceCall", NULL, false), "call", NULL),
						  ds_param(grpc_argumentType(m), "request", NULL)), NONE, false),
				     ds_empty_body()));
		out_blank(o);
	}
	out_end_body(o, body, cls->body->rb);
}

static void grpc_generate(ServiceGen *s, Out *o)
{
	grpc_generateClient(s, o);
	out_blank(o);
	grpc_generateService(s, o);
}
