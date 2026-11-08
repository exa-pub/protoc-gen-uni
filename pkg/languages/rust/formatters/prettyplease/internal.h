/* Internal header of the prettyplease 0.2.37 port (unity build prettyplease.c; public API
 * prettyplease.h). The printer state is `Pp` (prettyplease's `Printer`); the printing functions keep
 * the names of the Rust methods with a `p_` prefix and live in the file named like the Rust module
 * they port. Everything is static. */
#ifndef PRETTYPLEASE_INTERNAL_H
#define PRETTYPLEASE_INTERNAL_H

#include "prettyplease.h"

#include <stdint.h>
#include <string.h>

/* lib.rs */
#define MARGIN 89
#define INDENT 4
#define MIN_SPACE 60

/* ---- algorithm.rs ---- */

enum { BREAKS_CONSISTENT, BREAKS_INCONSISTENT };
enum { TOK_STRING, TOK_BREAK, TOK_BEGIN, TOK_END };

typedef struct {
	intptr_t offset;
	intptr_t blank_space;
	char pre_break;          /* Option<char>: 0 is None */
	const char *post_break;  /* "" if none */
	char no_break;           /* Option<char> */
	bool if_nonempty, never_break;
} BreakToken;

typedef struct {
	uint8_t kind; /* TOK_* */
	uint8_t breaks;
	BreakToken brk;   /* TOK_BREAK; TOK_BEGIN: offset */
	const char *s;    /* TOK_STRING */
	size_t n;
} Token;

typedef struct {
	Token token;
	intptr_t size;
} BufEntry;

/* ring.rs: abstract indexes [offset, offset + len) over a power-of-two circular array. */
typedef struct {
	BufEntry *data;
	size_t cap, head, len, offset;
} RingBuffer;

/* VecDeque<usize> */
typedef struct {
	size_t *data;
	size_t cap, head, len;
} Deque;

typedef struct {
	bool broken;
	uint8_t breaks;
	intptr_t indent;
} PrintFrame;

typedef struct {
	Buf out;
	intptr_t space;
	RingBuffer buf;
	intptr_t left_total, right_total;
	Deque scan_stack;
	PrintFrame *print_stack;
	size_t print_len, print_cap;
	intptr_t indent;
	size_t pending_indentation;
} Pp;

#define SIZE_INFINITY 0xffff

#endif
