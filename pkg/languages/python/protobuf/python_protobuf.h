/* Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* python-protobuf (PyPI protobuf 7.36.2, the pure-Python implementation): the parts protoc
 * plugins written in Python rely on.
 *
 *   - text_format.c: google/protobuf/text_format.py text_format.Merge for messages of singular
 *     and repeated scalar, enum and string fields, with the ParseError texts and tracebacks.
 *
 * Tracebacks name the files of the package as installed in a Python 3.13 virtual environment at
 * /app (the layout of the buf.build plugin images). */
#ifndef PYTHON_PROTOBUF_H
#define PYTHON_PROTOBUF_H

#include "pb.h"
#include "python.h"

/* A closed enum: its full name and values. */
typedef struct {
	const char *name;
	int32_t number;
} PypbEnumValue;
typedef struct {
	const char *full_name;
	int n;
	const PypbEnumValue *v;
} PypbEnum;
/* A field: the name, the type (PT_INT32, PT_UINT32, PT_BOOL, PT_STRING or PT_ENUM) and whether
 * it is repeated. */
typedef struct {
	const char *name;
	uint8_t type;
	bool repeated;
	const PypbEnum *enm;
} PypbField;
typedef struct {
	const char *full_name;
	int n;
	const PypbField *f;
} PypbMessage;

/* A parsed value of field f: i for integers, bools and enums, s for strings. */
typedef void (*PypbSet)(void *ctx, int f, int64_t i, Str s);

/* An exception that text_format.Merge raised: the tracebacks of the exceptions it occurred
 * during (each followed by "During handling of the above exception, another exception
 * occurred:"), the frames from Merge down to the raising line, and the last line. */
typedef struct {
	const char *chained, *frames, *exc;
} PypbError;

/* text_format.Merge(text, message): calls set for every value in order. Returns false with
 * *err set when Merge raises; a construct this port does not implement fails with an
 * "unsupported by protoc-gen-uni" error. */
bool pypb_text_merge(const PypbMessage *m, Str text, PypbSet set, void *ctx, PypbError *err);

#endif
