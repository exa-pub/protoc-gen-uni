// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Internal declarations of the libprotoc java unity build (libprotoc_java.c): the data this
 * library keeps behind ProtoFile.user and ProtoMessage.user. */
#ifndef LIBPROTOC_JAVA_INTERNAL_H
#define LIBPROTOC_JAVA_INTERNAL_H

#include "libprotoc_java.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ProtoFile.user */
typedef struct {
	char *immutable_class_name;   /* GetFileImmutableClassName, memoized */
	char *java_package;           /* FileJavaPackage */
	LpSourceLocations *locations; /* GetSourceLocation table */
} JvFileData;

/* ProtoMessage.user */
typedef struct {
	int has_required;                 /* HasRequiredFields: 0 unknown, 1 false, 2 true */
	JvFieldGeneratorInfo *field_info; /* per field, for messages of a Context's file */
	JvOneofGeneratorInfo *oneof_info; /* per oneof */
	char *class_name;                 /* GetClassName(d, true), memoized */
} JvMessageData;

static JvFileData *jv_file_data(const LpFile *f);
static JvMessageData *jv_message_data(const LpMessage *m);

static bool jv_ascii_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static bool jv_ascii_islower(char c) { return c >= 'a' && c <= 'z'; }
static char jv_ascii_toupper(char c) { return jv_ascii_islower(c) ? (char)(c - 'a' + 'A') : c; }
static char jv_ascii_tolower(char c) { return jv_ascii_isupper(c) ? (char)(c - 'A' + 'a') : c; }
static char *jv_itoa(int64_t v) { return xfmt("%lld", (long long)v); }
static char *jv_ascii_str_to_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		*p = jv_ascii_toupper(*p);
	return r;
}

#endif
