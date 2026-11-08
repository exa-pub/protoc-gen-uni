// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/{full,lite}/generator_factory.{h,cc} and the
 * MessageGenerator constructors (generator_factory.h, full/message.cc, lite/message.cc): the
 * virtual methods of the generators dispatch on factory->lite. */

/* the constructors of EnumNonLiteGenerator/EnumLiteGenerator (canonical values and aliases) and
 * ImmutableExtensionGenerator/ImmutableExtensionLiteGenerator (scope) */
static JvEnumGen *new_enum_gen(const LpEnum *descriptor, bool immutable_api, JvContext *context)
{
	JvEnumGen *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->immutable_api = immutable_api;
	g->context = context;
	g->canonical_values = xalloc(sizeof(LpEnumValue *) * (size_t)(descriptor->n_values + 1));
	g->aliases = xalloc(sizeof(JvEnumAlias) * (size_t)(descriptor->n_values + 1));
	/* FindValueByNumber: the first value with the number */
	Map first = {0};
	for (int i = 0; i < descriptor->n_values; i++) {
		const LpEnumValue *value = &descriptor->values[i];
		const char *key = jv_i(value->number);
		const LpEnumValue *canonical_value = map_get(&first, key);
		if (!canonical_value) {
			map_put(&first, key, (void *)value);
			canonical_value = value;
		}
		if (value == canonical_value)
			g->canonical_values[g->n_canonical_values++] = value;
		else
			g->aliases[g->n_aliases++] = (JvEnumAlias){value, canonical_value};
	}
	return g;
}

static JvExtensionGen *new_extension_gen(const LpField *descriptor, JvContext *context)
{
	JvExtensionGen *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->context = context;
	g->scope = descriptor->extension_scope ? jv_immutable_class_name(descriptor->extension_scope)
					       : jv_immutable_class_name(descriptor->file);
	return g;
}

/* MessageGenerator::MessageGenerator: oneofs_ (btree_map by index) of the real oneofs */
static void init_message_generator(JvMessageGen *g, const LpMessage *descriptor, JvContext *context, bool lite)
{
	g->descriptor = descriptor;
	g->context = context;
	g->lite = lite;
	g->oneofs = xalloc(sizeof(LpOneof *) * (size_t)(descriptor->n_oneofs + 1));
	bool *seen = xalloc((size_t)(descriptor->n_oneofs + 1));
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpOneof *oneof = proto_field_real_oneof(&descriptor->fields[i]);
		if (oneof && !seen[oneof->index]) {
			seen[oneof->index] = true;
			g->n_oneofs++;
		}
	}
	int k = 0;
	for (int i = 0; i < descriptor->n_oneofs; i++)
		if (seen[i])
			g->oneofs[k++] = &descriptor->oneofs[i];
	g->field_generators = lite ? make_immutable_field_lite_generators(descriptor, context)
				   : make_immutable_field_generators(descriptor, context);
}

static JvMessageGen *new_message_gen(const LpMessage *descriptor, JvContext *context, bool lite)
{
	JvMessageGen *g = xalloc(sizeof *g);
	init_message_generator(g, descriptor, context, lite);
	return g;
}

static JvMessageGen *jv_new_message_generator(const JvGeneratorFactory *f, const LpMessage *descriptor)
{
	return new_message_gen(descriptor, f->context, f->lite);
}

static void jv_message_generator_generate_static_variables(JvMessageGen *g, LpPrinter *printer, int *bytecode_estimate)
{
	if (g->lite)
		ImmutableMessageLiteGenerator_GenerateStaticVariables(g, printer, bytecode_estimate);
	else
		ImmutableMessageGenerator_GenerateStaticVariables(g, printer, bytecode_estimate);
}

static int jv_message_generator_generate_static_variable_initializers(JvMessageGen *g, LpPrinter *printer)
{
	return g->lite ? ImmutableMessageLiteGenerator_GenerateStaticVariableInitializers(g, printer)
		       : ImmutableMessageGenerator_GenerateStaticVariableInitializers(g, printer);
}

static void jv_message_generator_generate(JvMessageGen *g, LpPrinter *printer)
{
	if (g->lite)
		ImmutableMessageLiteGenerator_Generate(g, printer);
	else
		ImmutableMessageGenerator_Generate(g, printer);
}

static void jv_message_generator_generate_interface(JvMessageGen *g, LpPrinter *printer)
{
	if (g->lite)
		ImmutableMessageLiteGenerator_GenerateInterface(g, printer);
	else
		ImmutableMessageGenerator_GenerateInterface(g, printer);
}

static void jv_message_generator_generate_extension_registration_code(JvMessageGen *g, LpPrinter *printer)
{
	if (g->lite)
		ImmutableMessageLiteGenerator_GenerateExtensionRegistrationCode(g, printer);
	else
		ImmutableMessageGenerator_GenerateExtensionRegistrationCode(g, printer);
}

static void jv_enum_generator_generate(const JvGeneratorFactory *f, const LpEnum *descriptor, LpPrinter *printer)
{
	JvEnumGen *g = new_enum_gen(descriptor, true, f->context);
	if (f->lite)
		EnumLiteGenerator_Generate(g, printer);
	else
		EnumNonLiteGenerator_Generate(g, printer);
}

static JvExtensionGen *jv_new_extension_generator(const JvGeneratorFactory *f, const LpField *descriptor)
{
	return new_extension_gen(descriptor, f->context);
}

static void jv_extension_generator_generate(const JvGeneratorFactory *f, JvExtensionGen *g, LpPrinter *printer)
{
	if (f->lite)
		ImmutableExtensionLiteGenerator_Generate(g, printer);
	else
		ImmutableExtensionGenerator_Generate(g, printer);
}

static int jv_extension_generator_generate_non_nested_initialization_code(const JvGeneratorFactory *f, JvExtensionGen *g,
									  LpPrinter *printer)
{
	return f->lite ? ImmutableExtensionLiteGenerator_GenerateNonNestedInitializationCode(g, printer)
		       : ImmutableExtensionGenerator_GenerateNonNestedInitializationCode(g, printer);
}

static int jv_extension_generator_generate_registration_code(const JvGeneratorFactory *f, JvExtensionGen *g,
							     LpPrinter *printer)
{
	return f->lite ? ImmutableExtensionLiteGenerator_GenerateRegistrationCode(g, printer)
		       : ImmutableExtensionGenerator_GenerateRegistrationCode(g, printer);
}

static void jv_service_generator_generate(const JvGeneratorFactory *f, const LpService *descriptor, LpPrinter *printer)
{
	JvServiceGen g = {descriptor, f->context};
	ImmutableServiceGenerator_Generate(&g, printer);
}
