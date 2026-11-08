// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/make_field_gens.{h,cc}. */

#define NEW_LITE_FIELD_GEN(cls) new_field_gen(&cls##_ops, cls##_init, field, message_bit_index, 0, context)

static JvFieldGen *create_immutable_lite_generator(const LpField *field, int message_bit_index, JvContext *context)
{
	if (proto_field_is_repeated(field)) {
		switch (jv_get_java_type(field)) {
		case JAVATYPE_MESSAGE:
			if (jv_is_map_entry(field->message_type))
				return NEW_LITE_FIELD_GEN(ImmutableMapFieldLiteGenerator);
			return NEW_LITE_FIELD_GEN(RepeatedImmutableMessageFieldLiteGenerator);
		case JAVATYPE_ENUM: return NEW_LITE_FIELD_GEN(RepeatedImmutableEnumFieldLiteGenerator);
		case JAVATYPE_STRING: return NEW_LITE_FIELD_GEN(RepeatedImmutableStringFieldLiteGenerator);
		default: return NEW_LITE_FIELD_GEN(RepeatedImmutablePrimitiveFieldLiteGenerator);
		}
	}
	if (jv_is_real_oneof(field)) {
		switch (jv_get_java_type(field)) {
		case JAVATYPE_MESSAGE: return NEW_LITE_FIELD_GEN(ImmutableMessageOneofFieldLiteGenerator);
		case JAVATYPE_ENUM: return NEW_LITE_FIELD_GEN(ImmutableEnumOneofFieldLiteGenerator);
		case JAVATYPE_STRING: return NEW_LITE_FIELD_GEN(ImmutableStringOneofFieldLiteGenerator);
		default: return NEW_LITE_FIELD_GEN(ImmutablePrimitiveOneofFieldLiteGenerator);
		}
	}
	switch (jv_get_java_type(field)) {
	case JAVATYPE_MESSAGE: return NEW_LITE_FIELD_GEN(ImmutableMessageFieldLiteGenerator);
	case JAVATYPE_ENUM: return NEW_LITE_FIELD_GEN(ImmutableEnumFieldLiteGenerator);
	case JAVATYPE_STRING: return NEW_LITE_FIELD_GEN(ImmutableStringFieldLiteGenerator);
	default: return NEW_LITE_FIELD_GEN(ImmutablePrimitiveFieldLiteGenerator);
	}
}

/* MakeImmutableFieldLiteGenerators */
static JvFieldGen **make_immutable_field_lite_generators(const LpMessage *descriptor, JvContext *context)
{
	int message_bit_index = 0;
	JvFieldGen **ret = xalloc(sizeof(JvFieldGen *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpField *field = &descriptor->fields[i];
		JvFieldGen *generator = create_immutable_lite_generator(field, message_bit_index, context);
		message_bit_index += generator->ops->GetNumBitsForMessage(generator);
		ret[i] = generator;
	}
	return ret;
}
