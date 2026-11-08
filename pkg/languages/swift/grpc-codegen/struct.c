/* Copyright (c) 2023 Apple Inc. and the SwiftOpenAPIGenerator project authors
 * Licensed under Apache License v2.0
 *
 * See LICENSE.txt for license information
 * See CONTRIBUTORS.txt for the list of SwiftOpenAPIGenerator project authors
 *
 * SPDX-License-Identifier: Apache-2.0 */

/* Port of the conveniences of Internal/StructuredSwiftRepresentation.swift and of
 * Internal/Namer.swift (grpc-swift-2 2.4.4, Copyright 2024, gRPC Authors, Apache-2.0). */
#include "structured_swift.h"

COLD Vec gc_vec(void *first, ...)
{
	Vec v = {0};
	va_list ap;
	va_start(ap, first);
	for (void *p = first; p; p = va_arg(ap, void *))
		vec_push(&v, p);
	va_end(ap);
	return v;
}

COLD GcComment *gc_comment(GcCommentKind kind, Str text)
{
	GcComment *c = xalloc(sizeof *c);
	c->kind = kind;
	c->text = text;
	return c;
}

COLD GcComment *gc_doc(const char *text) { return gc_comment(GC_DOC, str_c(text)); }

COLD GcType *gc_type(GcTypeKind kind, GcType *inner)
{
	GcType *t = xalloc(sizeof *t);
	t->kind = kind;
	t->inner = inner;
	return t;
}

COLD GcType *gc_member(const char *name)
{
	GcType *t = gc_type(GT_MEMBER, NULL);
	t->member = name;
	return t;
}

COLD GcType *gc_generic(GcType *wrapper, GcType *wrapped, GcType *wrapped2)
{
	GcType *t = gc_type(GT_GENERIC, wrapper);
	t->wrapped = gc_vec(wrapped, wrapped2, NULL);
	return t;
}

/* @Sendable @escaping (<param_type>) async throws -> <return_type> */
COLD GcType *gc_closure_type(GcType *param_type, const char *return_type)
{
	GcType *t = gc_type(GT_CLOSURE, NULL);
	t->closure = xalloc(sizeof *t->closure);
	t->closure->parameters = gc_vec(gc_param(NULL, NULL, param_type, NULL), NULL);
	t->closure->keywords = GC_KW_ASYNC_THROWS;
	t->closure->return_type = gc_pattern(return_type);
	t->closure->sendable = t->closure->escaping = true;
	return t;
}

static GcExpr *expr(GcExprKind kind)
{
	GcExpr *e = xalloc(sizeof *e);
	e->kind = kind;
	return e;
}

COLD GcExpr *gc_string(const char *s)
{
	GcExpr *e = expr(GE_STRING);
	e->s = s;
	return e;
}

COLD GcExpr *gc_empty_dictionary(void) { return expr(GE_DICTIONARY); }

COLD GcExpr *gc_array(Vec items)
{
	GcExpr *e = expr(GE_ARRAY);
	e->items = items;
	return e;
}

COLD GcExpr *gc_pattern(const char *s)
{
	GcExpr *e = expr(GE_PATTERN);
	e->s = s;
	return e;
}

COLD GcExpr *gc_identifier_type(GcType *t)
{
	GcExpr *e = expr(GE_TYPE);
	e->type = t;
	return e;
}

COLD GcExpr *gc_dot(GcExpr *left, const char *right)
{
	GcExpr *e = expr(GE_MEMBER);
	e->left = left;
	e->s = right;
	return e;
}

COLD GcExpr *gc_call(GcExpr *called, Vec args)
{
	GcExpr *e = expr(GE_CALL);
	e->left = called;
	e->items = args;
	return e;
}

COLD GcArg *gc_arg(const char *label, GcExpr *e)
{
	GcArg *a = xalloc(sizeof *a);
	a->label = label;
	a->expr = e;
	return a;
}

COLD GcExpr *gc_assign(GcExpr *left, GcExpr *right)
{
	GcExpr *e = expr(GE_ASSIGN);
	e->left = left;
	e->right = right;
	return e;
}

COLD GcExpr *gc_keyword(GcKeyword kind, GcExpr *operand)
{
	GcExpr *e = expr(GE_KEYWORD);
	e->keyword = kind;
	e->right = operand;
	return e;
}

COLD GcExpr *gc_closure(Vec argument_names, Vec *body)
{
	GcExpr *e = expr(GE_CLOSURE);
	e->items = argument_names;
	e->body = body;
	return e;
}

COLD GcParam *gc_param(const char *label, const char *name, GcType *type, GcExpr *default_value)
{
	GcParam *p = xalloc(sizeof *p);
	p->label = label;
	p->name = name;
	p->type = type;
	p->default_value = default_value;
	return p;
}

COLD GcCodeBlock *gc_block(GcComment *comment, GcDecl *decl, GcExpr *e)
{
	GcCodeBlock *b = xalloc(sizeof *b);
	b->comment = comment;
	b->decl = decl;
	b->expr = e;
	return b;
}

COLD Vec *gc_blocks(Vec v)
{
	Vec *p = xalloc(sizeof *p);
	*p = v;
	return p;
}

COLD GcDecl *gc_decl(GcDeclKind kind)
{
	GcDecl *d = xalloc(sizeof *d);
	d->kind = kind;
	return d;
}

COLD GcDecl *gc_commentable(GcComment *comment, GcDecl *inner)
{
	GcDecl *d = gc_decl(GD_COMMENTABLE);
	d->comment = comment;
	d->inner = inner;
	return d;
}

COLD GcDecl *gc_guarded(const GcAvailability *availability, GcDecl *inner)
{
	GcDecl *d = gc_decl(GD_GUARDED);
	d->availability = availability;
	d->inner = inner;
	return d;
}

/* [access] [static] let <left>[: type][ = right] */
COLD GcDecl *gc_variable(GcAccess access, bool is_static, const char *left, GcType *type, GcExpr *right)
{
	GcDecl *d = gc_decl(GD_VARIABLE);
	d->access = access;
	d->is_static = is_static;
	d->is_let = true;
	d->left = gc_pattern(left);
	d->type = type;
	d->right = right;
	return d;
}

COLD GcDecl *gc_extension(const char *on_type, Vec declarations)
{
	GcDecl *d = gc_decl(GD_EXTENSION);
	d->name = on_type;
	d->members = declarations;
	return d;
}

COLD GcDecl *gc_function(GcSignature sig, Vec *body)
{
	GcDecl *d = gc_decl(GD_FUNCTION);
	d->sig = sig;
	d->body = body;
	return d;
}

COLD GcWhere *gc_where(const char *left, const char *right)
{
	GcWhere *w = xalloc(sizeof *w);
	w->left = left;
	w->right = right;
	return w;
}

/* ---- Namer.swift ---- */

COLD char *gc_literal_namespaced_type(const GcNamer *n, const char *type) { return xcat(n->grpc_core, ".", type); }

COLD GcType *gc_grpc_core(const GcNamer *n, const char *type) { return gc_member(gc_literal_namespaced_type(n, type)); }

COLD GcType *gc_grpc_core_generic(const GcNamer *n, const char *type, const char *over)
{
	return gc_generic(gc_grpc_core(n, type), gc_member(over), NULL);
}

static GcType *request_response(const GcNamer *n, const char *type, bool is_request, bool streaming, bool client)
{
	char *name = xcat(streaming ? "Streaming" : "", client ? "Client" : "Server", is_request ? "Request" : "Response");
	return type ? gc_grpc_core_generic(n, name, type) : gc_grpc_core(n, name);
}

COLD GcType *gc_server_request(const GcNamer *n, const char *type, bool streaming)
{
	return request_response(n, type, true, streaming, false);
}

COLD GcType *gc_server_response(const GcNamer *n, const char *type, bool streaming)
{
	return request_response(n, type, false, streaming, false);
}

COLD GcType *gc_client_request(const GcNamer *n, const char *type, bool streaming)
{
	return request_response(n, type, true, streaming, true);
}

COLD GcType *gc_client_response(const GcNamer *n, const char *type, bool streaming)
{
	return request_response(n, type, false, streaming, true);
}

COLD GcType *gc_rpc_async_sequence(const GcNamer *n, const char *type)
{
	return gc_generic(gc_grpc_core(n, "RPCAsyncSequence"), gc_member(type), gc_type(GT_ANY, gc_member("Swift.Error")));
}
