// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/io/printer.{h,cc} of protobuf v3.21.12 (printer_3.21.12.h):
 * Print(map, text), the variadic Print, PrintRaw, Indent, Outdent and WriteRaw. Failed checks are
 * those of a release build (GOOGLE_LOG(DFATAL) logs and goes on): an undefined variable prints
 * nothing, an unclosed one prints the delimiter, Outdent() without Indent() does nothing. */
#include "printer_3.21.12.h"

#include <stdarg.h>
#include <string.h>

void lp3_vars_set_s(LpVars3 *vars, const char *key, Str value)
{
	for (int i = 0; i < vars->n; i++)
		if (strcmp(vars->v[i].key, key) == 0) {
			vars->v[i].value = value;
			return;
		}
	if (vars->n == vars->cap) {
		int cap = vars->cap ? 2 * vars->cap : 16;
		vars->v = xrealloc(vars->v, sizeof(LpVar3) * (size_t)vars->cap, sizeof(LpVar3) * (size_t)cap);
		vars->cap = cap;
	}
	vars->v[vars->n++] = (LpVar3){key, value};
}

void lp3_vars_set(LpVars3 *vars, const char *key, const char *value) { lp3_vars_set_s(vars, key, str_c(value)); }

const Str *lp3_vars_get(const LpVars3 *vars, const char *key)
{
	for (int i = 0; i < vars->n; i++)
		if (strcmp(vars->v[i].key, key) == 0)
			return &vars->v[i].value;
	return NULL;
}

static const Str *vars_find(const LpVars3 *vars, const char *name, size_t n)
{
	for (int i = 0; i < vars->n; i++)
		if (strlen(vars->v[i].key) == n && memcmp(vars->v[i].key, name, n) == 0)
			return &vars->v[i].value;
	return NULL;
}

void lp3_printer_init(LpPrinter3 *p, Buf *out, char delim)
{
	*p = (LpPrinter3){.out = out, .delim = delim, .at_start_of_line = true};
}

/* Printer::WriteRaw */
static void write_raw(LpPrinter3 *p, const char *data, size_t size)
{
	if (size == 0)
		return;
	if (p->at_start_of_line && data[0] != '\n') {
		/* Insert an indent. */
		p->at_start_of_line = false;
		for (size_t i = 0; i < p->indent; i++)
			buf_addc(p->out, ' ');
	}
	buf_add(p->out, data, size);
}

void lp3_print_vars(LpPrinter3 *p, const LpVars3 *vars, const char *text)
{
	size_t size = strlen(text);
	size_t pos = 0; /* The number of bytes we've written so far. */
	for (size_t i = 0; i < size; i++) {
		if (text[i] == '\n') {
			/* Saw newline: write what we have so far, including the '\n'; the next write
			 * inserts an indent. */
			write_raw(p, text + pos, i - pos + 1);
			pos = i + 1;
			p->at_start_of_line = true;
		} else if (text[i] == p->delim) {
			/* Saw the start of a variable name: write what we have so far. */
			write_raw(p, text + pos, i - pos);
			pos = i + 1;
			/* Find closing delimiter (unclosed: an empty name). */
			const char *end = strchr(text + pos, p->delim);
			size_t endpos = end ? (size_t)(end - text) : pos;
			if (endpos == pos) {
				/* Two delimiters in a row reduce to a literal delimiter character. */
				write_raw(p, &p->delim, 1);
			} else {
				const Str *v = vars_find(vars, text + pos, endpos - pos);
				if (v)
					write_raw(p, v->p, v->n);
			}
			/* Advance past this variable. */
			i = endpos;
			pos = endpos + 1;
		}
	}
	/* Write the rest. */
	if (pos < size)
		write_raw(p, text + pos, size - pos);
}

void lp3_print(LpPrinter3 *p, const char *text, ...)
{
	LpVars3 vars = {0};
	LpVar3 v[16];
	vars.v = v;
	vars.cap = 16;
	va_list ap;
	va_start(ap, text);
	for (const char *k = va_arg(ap, const char *); k && vars.n < 16; k = va_arg(ap, const char *))
		lp3_vars_set(&vars, k, va_arg(ap, const char *));
	va_end(ap);
	lp3_print_vars(p, &vars, text);
}

void lp3_print_raw(LpPrinter3 *p, const char *data) { write_raw(p, data, strlen(data)); }

void lp3_indent(LpPrinter3 *p) { p->indent += 2; }

void lp3_outdent(LpPrinter3 *p)
{
	if (p->indent >= 2)
		p->indent -= 2;
}
