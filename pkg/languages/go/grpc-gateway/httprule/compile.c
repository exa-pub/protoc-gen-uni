/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/httprule/compile.go. */
#include "internal.h"

#include <string.h>

enum { opcodeVersion = 1 };

typedef struct {
	int code; /* the opcode of the operation */
	Str str;  /* a string operand of the code; num is ignored if str is not empty */
	int num;  /* a numeric operand of the code */
} op;

typedef struct {
	op *v;
	int n, cap;
} opVec;

static void push(opVec *ops, op o)
{
	if (ops->n == ops->cap) {
		int cap = ops->cap ? 2 * ops->cap : 8;
		ops->v = xrealloc(ops->v, sizeof *ops->v * (size_t)ops->cap, sizeof *ops->v * (size_t)cap);
		ops->cap = cap;
	}
	ops->v[ops->n++] = o;
}

static void compile(const segment *s, opVec *ops)
{
	switch (s->kind) {
	case SEG_WILDCARD:
		push(ops, (op){GW_OpPush, {NULL, 0}, 0});
		return;
	case SEG_DEEP_WILDCARD:
		push(ops, (op){GW_OpPushM, {NULL, 0}, 0});
		return;
	case SEG_LITERAL:
		push(ops, (op){GW_OpLitPush, s->lit, 0});
		return;
	}
	for (int i = 0; i < s->nsegs; i++)
		compile(s->segs[i], ops);
	push(ops, (op){GW_OpConcatN, {NULL, 0}, s->nsegs});
	push(ops, (op){GW_OpCapture, s->path, 0});
}

/* consts: the constant pool index of a string; open addressing over the pool (strings may
 * contain NUL, so Map with C string keys does not fit). */
typedef struct {
	int *slots; /* pool index + 1, 0: empty */
	size_t cap;
} constMap;

static size_t hash(Str s)
{
	size_t h = 14695981039346656037u;
	for (size_t i = 0; i < s.n; i++)
		h = (h ^ (unsigned char)s.p[i]) * 1099511628211u;
	return h;
}

GwTemplate gw_httprule_Compile(const GwHttpruleTemplate *t)
{
	opVec rawOps = {0};
	for (int i = 0; i < t->nsegments; i++)
		compile(t->segments[i], &rawOps);

	GwTemplate r = {.Version = opcodeVersion, .Verb = t->verb, .Template = t->tmpl};
	r.OpCodes = xalloc(sizeof *r.OpCodes * (size_t)(2 * rawOps.n + 1));
	r.Pool = xalloc(sizeof *r.Pool * (size_t)(rawOps.n + 1));
	r.Fields = xalloc(sizeof *r.Fields * (size_t)(rawOps.n + 1));
	constMap consts = {xalloc(sizeof(int) * (size_t)(2 * rawOps.n + 2)), (size_t)(2 * rawOps.n + 2)};
	for (int i = 0; i < rawOps.n; i++) {
		op o = rawOps.v[i];
		r.OpCodes[r.nOpCodes++] = o.code;
		if (o.str.n == 0) {
			r.OpCodes[r.nOpCodes++] = o.num;
		} else {
			/* eof segment literal represents the "/" path pattern */
			if (str_equal(o.str, HTTPRULE_EOF))
				o.str = (Str){"", 0};
			size_t k = hash(o.str) % consts.cap;
			while (consts.slots[k] && !str_equal(r.Pool[consts.slots[k] - 1], o.str))
				k = (k + 1) % consts.cap;
			if (!consts.slots[k]) {
				consts.slots[k] = r.nPool + 1;
				r.Pool[r.nPool++] = o.str;
			}
			r.OpCodes[r.nOpCodes++] = consts.slots[k] - 1;
		}
		if (o.code == GW_OpCapture)
			r.Fields[r.nFields++] = o.str;
	}
	return r;
}
