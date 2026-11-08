/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/httprule/types.go: the String methods. */
#include "internal.h"

Str gw_httprule_segment_String(const segment *s)
{
	switch (s->kind) {
	case SEG_WILDCARD:
		return STR_LIT("*");
	case SEG_DEEP_WILDCARD:
		return STR_LIT("**");
	case SEG_LITERAL:
		return s->lit;
	}
	Buf b = {0};
	buf_addc(&b, '{');
	buf_add_str(&b, s->path);
	buf_addc(&b, '=');
	for (int i = 0; i < s->nsegs; i++) {
		if (i)
			buf_addc(&b, '/');
		buf_add_str(&b, gw_httprule_segment_String(s->segs[i]));
	}
	buf_addc(&b, '}');
	return buf_view(&b);
}

Str gw_httprule_String(const GwHttpruleTemplate *t)
{
	Buf b = {0};
	buf_addc(&b, '/');
	for (int i = 0; i < t->nsegments; i++) {
		if (i)
			buf_addc(&b, '/');
		buf_add_str(&b, gw_httprule_segment_String(t->segments[i]));
	}
	if (t->verb.n) {
		buf_addc(&b, ':');
		buf_add_str(&b, t->verb);
	}
	return buf_view(&b);
}
