// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* io::Printer of protobuf v3.21.12 (printer_3.21.12.c, library cpp_libprotoc_3.21.12): the
 * printer before the rewrite that libprotoc 36.2 implements (printer.c). Print substitutes
 * variables in the text as written: a line of the text keeps its leading spaces, a substituted
 * value is copied as it is (its newlines get no indent), and the indent is written before the
 * first byte of a line that is not a newline. Annotations (AnnotationCollector, ${n$ ... $}$)
 * are not ported: no plugin of this version uses them. The printer of 3.19.1 is the same. */
#ifndef LIBPROTOC_PRINTER_3_21_12_H
#define LIBPROTOC_PRINTER_3_21_12_H

#include "libprotoc.h"

typedef struct {
	Buf *out;
	char delim;            /* variable_delimiter_ */
	size_t indent;         /* indent_.size() */
	bool at_start_of_line; /* at_start_of_line_ */
} LpPrinter3;

/* The std::map<std::string, std::string> passed to Print: lookups by key, values may contain
 * NUL bytes. */
typedef struct {
	const char *key;
	Str value;
} LpVar3;
typedef struct {
	LpVar3 *v;
	int n, cap;
} LpVars3;

/* (*vars)[key] = value (values are not copied) */
void lp3_vars_set(LpVars3 *vars, const char *key, const char *value);
void lp3_vars_set_s(LpVars3 *vars, const char *key, Str value);
/* NULL if absent */
const Str *lp3_vars_get(const LpVars3 *vars, const char *key);

/* Printer(output, variable_delimiter) */
void lp3_printer_init(LpPrinter3 *p, Buf *out, char delim);
/* Printer::Print(variables, text) */
void lp3_print_vars(LpPrinter3 *p, const LpVars3 *vars, const char *text);
/* Printer::Print(text, "var1", value1, "var2", value2, ..., NULL) */
void lp3_print(LpPrinter3 *p, const char *text, ...);
/* Printer::PrintRaw(const char *data): WriteRaw(data, strlen(data)), no variables; the indent
 * goes only before its first byte, and a final newline does not start a line for the next
 * Print. */
void lp3_print_raw(LpPrinter3 *p, const char *data);
void lp3_indent(LpPrinter3 *p);
void lp3_outdent(LpPrinter3 *p);

#endif
