/* Internal header of the dart_style 3.1.13 port (unity build, see dart_style.c).
 *
 * Everything is `static` inside the single translation unit dart_style.c; the public API (the AST,
 * its constructors and the formatter entry point) is dart_style.h.
 *
 * Layout and conventions (keep them so upstream diffs can be ported file by file):
 *   - files mirror dart_style's lib/src/: short/chunk_builder.dart -> short/chunk_builder.c,
 *     same function order and names (Dart `ChunkBuilder.startSpan` -> `cb_startSpan`);
 *   - nodes.c is not a port: the AST constructors and the linearizer that produce the token stream
 *     (with `precedingComments`) and the unresolved AST shapes of package:analyzer 14;
 *   - Dart strings are UTF-8 bytes; lengths that dart_style measures (`String.length`, UTF-16
 *     code units) are kept separately (`len16`);
 *   - memory comes from the arena (xalloc) and is never freed; errors (dart_style's
 *     FormatterException and similar) are reported with fail(). */
#ifndef DART_STYLE_INTERNAL_H
#define DART_STYLE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

#if defined(__GNUC__)
#define DS_UNUSED __attribute__((unused))
#else
#define DS_UNUSED
#endif

#include "dart_style.h"

static inline int tok_end(const Token *t) { return t->offset + t->len; }
static inline bool tok_is(const Token *t, const char *s) { return t && strcmp(t->lexeme, s) == 0; }

/* Analyzer's LineInfo: the offsets of the line starts of the source text. */
typedef struct {
	int *starts;
	int n;
} LineInfo;

static int line_number(const LineInfo *li, int offset)
{
	int min = 0, max = li->n - 1;
	while (min < max) {
		int mid = (max - min + 1) / 2 + min;
		if (li->starts[mid] > offset)
			max = mid - 1;
		else
			min = mid;
	}
	return min + 1;
}

static int column_number(const LineInfo *li, int offset)
{
	int line = line_number(li, offset);
	return offset - li->starts[line - 1] + 1;
}

/* ---- short style formatter internals (short/) ------------------------------------------- */

typedef struct Rule Rule;
typedef struct Chunk Chunk;
typedef struct NestingLevel NestingLevel;
typedef struct Span Span;

typedef struct {
	void **v;
	int n, cap;
} PtrVec;

/* Identity set of pointers (Dart's LinkedHashSet<Object> without iteration order, which the
 * users below never depend on), and an identity map pointer -> int. */
typedef struct {
	void **keys;
	int *vals;
	int cap, n;
} PtrMap;

typedef enum { RK_RULE, RK_POSITIONAL, RK_NAMED, RK_COMBINATOR, RK_TYPE_ARGUMENT } RuleKind;

typedef struct {
	int min, max, otherValue;
} Constraint;

typedef struct {
	Rule *rule;
	Constraint *v;
	int n, cap;
} ConstraintEntry;

struct Rule {
	RuleKind kind;
	int cost;
	int index; /* -1: null */
	bool isHardened;
	bool splitContaining; /* SplitContainingRule (ArgumentRule subclasses) */
	bool trackInnerRules;
	/* _constraints, in insertion order */
	ConstraintEntry *cons;
	int ncons, capcons;
	/* allConstrainedRules cache */
	PtrVec *allConstrained;
	PtrMap allConstrainedSet;
	/* ArgumentRule._arguments / TypeArgumentRule._arguments */
	PtrVec args;
	int leadingCollections, trailingCollections;
	/* CombinatorRule */
	PtrVec combinators; /* chunks */
	PtrVec names;       /* PtrVec* per combinator */
};

struct NestingLevel {
	NestingLevel *parent;
	int indent;
	int totalUsedIndent; /* -1: null */
	bool isMarked;
};

struct Span {
	int cost;
	bool isMarked;
};

struct Chunk {
	Buf text;
	int len16; /* text length in UTF-16 code units */
	int indent;
	NestingLevel *nesting;
	Rule *rule;
	bool isDouble, flushLeft, spaceWhenUnsplit, canDivide;
	PtrVec spans;
	/* BlockChunk */
	bool isBlock;
	Chunk *argument;
	PtrVec children;
};

typedef struct DartFormatterOpts {
	int pageWidth;
	int indent;
} DartFormatterOpts;

#endif
