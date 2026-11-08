/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of google.golang.org/protobuf v1.36.12 encoding/protojson (encode.go,
 * well_known_types.go) and internal/encoding/json (encode.go), as far as plugins use it: the JSON
 * of a google.protobuf.Value (types/known/structpb). */
#ifndef GO_PROTOJSON_H
#define GO_PROTOJSON_H

#include "protodesc.h"

/* protojson.Marshal of a google.protobuf.Value message v (the fields of struct.proto: Value 1-6,
 * Struct.fields 1, ListValue.values 1). The output has no whitespace: MarshalOptions.Indent
 * only adds whitespace (with a random extra space of internal/detrand), and the plugins that set
 * it compact the result again (json.RawMessage in encoding/json), so it is not reproduced.
 * Returns NULL, or the error text ("proto: ..."). */
const char *protojson_marshal_value(const PbMsg *v, Buf *out);

#endif
