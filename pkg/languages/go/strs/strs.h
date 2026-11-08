/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* String manipulation specific to protobuf, ported from google.golang.org/protobuf v1.36.12
 * internal/strs/strings.go (the functions used by compiler/protogen). Results are arena
 * strings. */
#ifndef GO_STRS_H
#define GO_STRS_H

#include "util.h"

/* GoCamelCase camel-cases a protobuf name for use as a Go identifier. */
char *strs_go_camel_case(const char *s);
/* GoSanitized converts a string (s[0:n], may be any bytes) to a valid Go identifier. */
char *strs_go_sanitized(const char *s, size_t n);
/* TrimEnumPrefix trims the enum name prefix (all lowercase without underscores) from an enum value
 * name. */
const char *strs_trim_enum_prefix(const char *s, const char *prefix);
/* JSONCamelCase converts a snake_case identifier to a camelCase identifier. */
char *strs_json_camel_case(const char *s);

#endif
