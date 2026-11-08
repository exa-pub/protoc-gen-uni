// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/message_field.{h,cc} (protobuf v36.2):
 * MessageFieldGenerator and RepeatedMessageFieldGenerator (their DetermineObjectiveCClassDefinitions
 * and DetermineNeededFiles are in field.c). */

/* SetMessageVariables */
static void message_field_init(FieldGenerator *g)
{
	char *message_type = lp_objc_class_name(g->descriptor->message_type);
	char *containing_class = lp_objc_class_name(g->descriptor->containing_type);
	subst_set(&g->variables, "msg_type", message_type);
	subst_set(&g->variables, "containing_class", containing_class);
	subst_set(&g->variables, "dataTypeSpecific_value", objc_class(message_type));
}

static void message_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
							 bool include_external_types)
{
	/* Local references always need a forward declaration (no order of the messages within a
	 * file), external ones (not WKTs) when requested. */
	const LpField *d = g->descriptor;
	if ((include_external_types && !lp_objc_is_protobuf_library_bundled_proto_file(d->message_type->file)) ||
	    d->file == d->message_type->file)
		string_set_insert(fwd_decls, xcat("@class ", field_variable(g, "msg_type"), ";"));
}
