// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/csharp/csharp_enum_field.{h,cc} (protobuf v36.2):
 * EnumFieldGenerator and EnumOneofFieldGenerator. Their parsing, serialization, size and merging
 * code are single Prints, in the dispatch of csharp_field_base.c. */

static void cs_enum_generate_codec_code(CsFieldGen *g, LpPrinter *printer)
{
	cs_fg_print(g, printer, "pb::FieldCodec.ForEnum($tag$, x => (int) x, x => ($type_name$) x, $default_value$)");
}
