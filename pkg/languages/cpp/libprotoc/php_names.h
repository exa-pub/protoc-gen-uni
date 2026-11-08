// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 36.2: src/google/protobuf/compiler/php/names.h, the PHP class names shared by the php
 * generator of protoc and grpc's php plugin (php_names.c). Strings are arena allocated. */
#ifndef LIBPROTOC_PHP_NAMES_H
#define LIBPROTOC_PHP_NAMES_H

#include "libprotoc.h"

/* IsReservedName: whether a name is a PHP reserved word (ASCII case insensitive). */
bool lp_php_is_reserved_name(const char *name);

/* ReservedNamePrefix: "GPB" (package google.protobuf) or "PB" for a reserved name, else "". The
 * reserved words are those of the protobuf version of the file's pool (LpVersion). */
const char *lp_php_reserved_name_prefix(const char *classname, const LpFile *file);

/* ClassNamePrefix(classname, desc) for a message or enum: the file's php_class_prefix if set,
 * else ReservedNamePrefix. */
const char *lp_php_class_name_prefix(const char *classname, const ProtoDesc *desc);

/* GeneratedClassName: the class name without namespace, nested messages joined by '\'. */
char *lp_php_generated_class_name_message(const LpMessage *desc);
char *lp_php_generated_class_name_enum(const LpEnum *desc);
char *lp_php_generated_class_name_service(const LpService *desc);

#endif
