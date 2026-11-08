/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* internal/httprule of grpc-gateway v2.33.0: the types of types.go shared by parse.c, types.c
 * and compile.c. */
#ifndef GW_HTTPRULE_INTERNAL_H
#define GW_HTTPRULE_INTERNAL_H

#include "grpcgateway.h"

/* segment: wildcard, deepWildcard, literal or variable */
enum { SEG_WILDCARD, SEG_DEEP_WILDCARD, SEG_LITERAL, SEG_VARIABLE };
typedef struct segment segment;
struct segment {
	int kind;
	Str lit;        /* literal */
	Str path;       /* variable */
	segment **segs; /* variable */
	int nsegs;
};

struct GwHttpruleTemplate {
	segment **segments;
	int nsegments;
	Str verb;
	Str tmpl;
};

/* eof is the terminal symbol which always appears at the end of token sequence. */
#define HTTPRULE_EOF ((Str){"\0", 1})

/* fmt.Stringer of the segments */
Str gw_httprule_segment_String(const segment *s);

#endif
