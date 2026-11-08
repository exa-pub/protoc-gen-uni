// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/primitive_field.{h,cc} (protobuf v36.2).
 * PrimitiveFieldGenerator (the BOOL storage in the has bits) and RepeatedPrimitiveFieldGenerator
 * are the FIELD_PRIMITIVE and FIELD_REPEATED_PRIMITIVE cases of field.c. */

/* PrimitiveObjFieldGenerator */
static void primitive_obj_field_init(FieldGenerator *g) { subst_set(&g->variables, "property_storage_attribute", "copy"); }
