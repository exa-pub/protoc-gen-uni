/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/CodePrinter.swift (swift-protobuf 1.39.0); the
 * single indent is a number of spaces (two by default). */
#include "swift_protobuf.h"

#include <stdarg.h>

void sw_printer_init_indent(SwPrinter *p, int width, bool newlines)
{
	*p = (SwPrinter){0};
	p->at_line_start = true;
	p->indent_width = width;
	p->newlines = newlines;
}

void sw_printer_init(SwPrinter *p, bool newlines) { sw_printer_init_indent(p, 2, newlines); }

void sw_printer_init_from(SwPrinter *p, const SwPrinter *parent)
{
	sw_printer_init_indent(p, parent->indent_width, parent->newlines);
	p->indent = parent->indent;
}

static void add_indent(SwPrinter *p)
{
	for (int i = 0; i < p->indent * p->indent_width; i++)
		buf_addc(&p->content, ' ');
}

/* printInternal */
static void print_internal(SwPrinter *p, const char *s, size_t n, bool add_newline)
{
	for (size_t i = 0; i < n; i++) {
		if (p->at_line_start && s[i] != '\n')
			add_indent(p);
		buf_addc(&p->content, s[i]);
		p->at_line_start = s[i] == '\n';
	}
	if (add_newline) {
		buf_addc(&p->content, '\n');
		p->at_line_start = true;
	}
}

void sw_print_nl(SwPrinter *p, Str s, bool newline) { print_internal(p, s.p, s.n, newline); }
void sw_print_str(SwPrinter *p, Str s) { print_internal(p, s.p, s.n, p->newlines); }
void sw_print(SwPrinter *p, const char *s) { sw_print_str(p, str_c(s)); }

void sw_printf(SwPrinter *p, const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	sw_print_str(p, buf_view(&b));
}

void sw_print_blank(SwPrinter *p)
{
	buf_addc(&p->content, '\n');
	p->at_line_start = true;
}

void sw_print_indented(SwPrinter *p, const char *s)
{
	sw_indent(p);
	sw_print(p, s);
	sw_outdent(p);
}

void sw_indent(SwPrinter *p) { p->indent++; }

void sw_outdent(SwPrinter *p)
{
	if (p->indent <= 0)
		fail("Cannot outdent past the left margin");
	p->indent--;
}

void sw_append(SwPrinter *p, const SwPrinter *other, bool indenting)
{
	if (indenting) {
		print_internal(p, other->content.p, other->content.n, false);
	} else {
		buf_add(&p->content, other->content.p, other->content.n);
		p->at_line_start = other->at_line_start;
	}
}

bool sw_printer_is_empty(const SwPrinter *p) { return p->content.n == 0; }
