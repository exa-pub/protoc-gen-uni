/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* compiler/protogen of google.golang.org/protobuf v1.31.0, for the plugins built with it
 * (community/planetscale-vtprotobuf): protogen.h (v1.36.12) with PgOptions.Version = &pg_v1_31_0,
 * which gives v1.31.0's behaviour where it differs: no editions (protodesc v1.31.0 accepts only
 * proto2 and proto3, with its own validation texts), no API levels (default_api_level and
 * apilevelM go to ParamFunc), no strip_enum_prefix, no extensions linked by protogen itself,
 * Options.New processes every ProtoFile (not only the transitive dependencies of FileToGenerate),
 * and Response reports supported_features only without an error. The API-level and opaque names
 * of v1.36.12 (Field.camelCase, ...) are computed but not part of v1.31.0. */
#ifndef GO_PROTOGEN_V1_31_0_H
#define GO_PROTOGEN_V1_31_0_H

#include "protogen.h"

extern const PgVersion pg_v1_31_0;

/* The filedesc (protoreflect) methods with v1.31.0 semantics (syntax-based, no editions). */
bool pg_v131_field_has_presence(const GoField *f);
bool pg_v131_field_is_packed(const GoField *f);
bool pg_v131_field_has_optional_keyword(const GoField *f);
bool pg_v131_oneof_is_synthetic(const GoOneof *o);
/* MessageDescriptor.RequiredNumbers: the numbers of the required fields in declaration order;
 * returns their count. */
int pg_v131_message_required_numbers(const GoMessage *m, const int32_t **out);

#endif
