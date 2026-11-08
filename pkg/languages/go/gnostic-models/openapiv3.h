/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic-models). */

/* Port of github.com/google/gnostic-models v0.7.0: openapiv3 (the OpenAPIv3.proto messages as
 * compiled into Go, their ToRawInfo methods of OpenAPIv3.go, Document.YAMLValue of document.go)
 * and the yaml.Node constructors of compiler/helpers.go.
 *
 * The messages are pkg/pb PbMsg values with protobuf-go semantics (pkg/languages/go/protodesc
 * definitions built by oa_init): gopb_decode parses them, the pb.h accessors read them. A
 * proto3 scalar outside a oneof is unset when it holds its zero value. The tables come from the
 * upstream sources (mkopenapiv3.py). */
#ifndef GNOSTIC_OPENAPIV3_H
#define GNOSTIC_OPENAPIV3_H

#include "openapiv3_fields.h"
#include "protodesc.h"
#include "yaml.h"

/* Builds the message definitions (arena memory: call once per request, after arena_reset). */
void oa_init(void);
/* The definition of message OA_<Message>. */
const GoMsgDef *oa_def(int msg);
PbMsg *oa_new(int msg);

/* Setters of singular fields (a oneof member clears the other members). */
void oa_set_str(PbMsg *m, uint32_t num, Str s);
void oa_set_cstr(PbMsg *m, uint32_t num, const char *s);
void oa_set_bool(PbMsg *m, uint32_t num, bool v);
void oa_set_double(PbMsg *m, uint32_t num, double v);
void oa_set_int(PbMsg *m, uint32_t num, int64_t v);
/* Sets a message field to sub (NULL clears it). */
void oa_set_msg(PbMsg *m, uint32_t num, PbMsg *sub);
/* Appends to a repeated field. */
void oa_add_msg(PbMsg *m, uint32_t num, PbMsg *sub);
void oa_add_str(PbMsg *m, uint32_t num, Str s);
/* Clears a field (a repeated field becomes empty). */
void oa_clear(PbMsg *m, uint32_t num);
/* Getters: the zero value when unset (or m is NULL). */
Str oa_str(const PbMsg *m, uint32_t num);
bool oa_bool(const PbMsg *m, uint32_t num);
/* The message in a singular field, or NULL (a Go nil pointer). */
PbMsg *oa_msg(const PbMsg *m, uint32_t num);

/* proto.Merge(dst, src) of protobuf-go v1.36.7 (proto/merge.go) for messages of one type: src
 * may be NULL (a typed nil message, merged as an empty one). */
void oa_merge(PbMsg *dst, const PbMsg *src);

/* ToRawInfo of message type msg; m may be NULL (a nil receiver). Fails (a Go panic) where the
 * upstream method dereferences a nil message. */
YamlNode *oa_to_raw_info(const PbMsg *m, int msg);
/* Document.YAMLValue(comment): yaml.Marshal of the document node; returns NULL and appends to
 * out, or the error. */
const char *oa_document_yaml_value(const PbMsg *d, Str comment, Buf *out);

/* compiler/helpers.go */
YamlNode *compiler_new_null_node(void);
YamlNode *compiler_new_mapping_node(void);
YamlNode *compiler_new_sequence_node(void);
YamlNode *compiler_new_scalar_node_for_string(Str s);
YamlNode *compiler_new_scalar_node_for_bool(bool b);
YamlNode *compiler_new_scalar_node_for_float(double f);
YamlNode *compiler_new_scalar_node_for_int(int64_t i);

#endif
