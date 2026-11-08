// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/make_field_gens.{h,cc}. */

static JvFieldGen *new_field_gen(const JvFieldGenOps *ops, void (*init)(JvFieldGen *), const LpField *field,
				 int message_bit_index, int builder_bit_index, JvContext *context)
{
	JvFieldGen *g = xalloc(sizeof *g);
	g->ops = ops;
	g->descriptor = field;
	g->message_bit_index = message_bit_index;
	g->builder_bit_index = builder_bit_index;
	g->context = context;
	init(g);
	return g;
}

#define NEW_FIELD_GEN(cls) new_field_gen(&cls##_ops, cls##_init, field, message_bit_index, builder_bit_index, context)

static JvFieldGen *make_immutable_generator(const LpField *field, int message_bit_index, int builder_bit_index,
					    JvContext *context)
{
	if (proto_field_is_repeated(field)) {
		switch (jv_get_java_type(field)) {
		case JAVATYPE_MESSAGE:
			if (jv_is_map_entry(field->message_type))
				return NEW_FIELD_GEN(ImmutableMapFieldGenerator);
			return NEW_FIELD_GEN(RepeatedImmutableMessageFieldGenerator);
		case JAVATYPE_ENUM: return NEW_FIELD_GEN(RepeatedImmutableEnumFieldGenerator);
		case JAVATYPE_STRING: return NEW_FIELD_GEN(RepeatedImmutableStringFieldGenerator);
		default: return NEW_FIELD_GEN(RepeatedImmutablePrimitiveFieldGenerator);
		}
	}
	if (jv_is_real_oneof(field)) {
		switch (jv_get_java_type(field)) {
		case JAVATYPE_MESSAGE: return NEW_FIELD_GEN(ImmutableMessageOneofFieldGenerator);
		case JAVATYPE_ENUM: return NEW_FIELD_GEN(ImmutableEnumOneofFieldGenerator);
		case JAVATYPE_STRING: return NEW_FIELD_GEN(ImmutableStringOneofFieldGenerator);
		default: return NEW_FIELD_GEN(ImmutablePrimitiveOneofFieldGenerator);
		}
	}
	switch (jv_get_java_type(field)) {
	case JAVATYPE_MESSAGE: return NEW_FIELD_GEN(ImmutableMessageFieldGenerator);
	case JAVATYPE_ENUM: return NEW_FIELD_GEN(ImmutableEnumFieldGenerator);
	case JAVATYPE_STRING: return NEW_FIELD_GEN(ImmutableStringFieldGenerator);
	default: return NEW_FIELD_GEN(ImmutablePrimitiveFieldGenerator);
	}
}

/* MakeImmutableFieldGenerators */
static JvFieldGen **make_immutable_field_generators(const LpMessage *descriptor, JvContext *context)
{
	int message_bit_index = 0;
	int builder_bit_index = 0;
	JvFieldGen **ret = xalloc(sizeof(JvFieldGen *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpField *field = &descriptor->fields[i];
		JvFieldGen *generator = make_immutable_generator(field, message_bit_index, builder_bit_index, context);
		message_bit_index += generator->ops->GetNumBitsForMessage(generator);
		builder_bit_index += generator->ops->GetNumBitsForBuilder(generator);
		ret[i] = generator;
	}
	return ret;
}
