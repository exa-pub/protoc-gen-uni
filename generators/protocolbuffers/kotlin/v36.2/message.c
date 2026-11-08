// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/kotlin/message.{h,cc}: MessageGenerator, the Kotlin DSL of
 * a message (its `<Name>Kt` object and the top-level members). */

typedef struct {
	JvContext *context;
	const LpMessage *descriptor;
	const LpOneof **oneofs; /* oneofs_ (absl::btree_map by index): the real oneofs of the fields */
	int n_oneofs;
	bool lite, jvm_dsl, dsl_use_concrete_types;
	KtFieldGen *field_generators; /* by field index */
} KtMessageGen;

static void kt_message_generator_init(KtMessageGen *g, const LpMessage *descriptor, JvContext *context)
{
	g->context = context;
	g->descriptor = descriptor;
	g->lite = !jv_has_descriptor_methods(jv_context_enforce_lite(context));
	g->jvm_dsl = !g->lite || context->options.jvm_dsl;
	g->dsl_use_concrete_types = g->lite && context->options.dsl_use_concrete_types;
	bool *seen = xalloc((size_t)descriptor->n_oneofs + 1);
	g->oneofs = xalloc(sizeof(LpOneof *) * ((size_t)descriptor->n_oneofs + 1));
	g->n_oneofs = 0;
	for (int i = 0; i < descriptor->n_fields; i++) {
		const LpField *field = &descriptor->fields[i];
		if (jv_is_real_oneof(field))
			seen[field->containing_oneof->index] = true;
	}
	for (int i = 0; i < descriptor->n_oneofs; i++)
		if (seen[i])
			g->oneofs[g->n_oneofs++] = &descriptor->oneofs[i];
	g->field_generators = xalloc(sizeof(KtFieldGen) * ((size_t)descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++)
		kt_field_generator_init(&g->field_generators[i], &descriptor->fields[i], context, g->lite);
}

static void kt_message_generator_generate_extensions(const KtMessageGen *g, LpPrinter *p);

/* JvmName(name) of a callback, with the JvmNameContext of the message */
typedef struct {
	const char *name;
	JvJvmNameContext ctx;
} KtMsgJvmName;

static void kt_msg_jvm_name_cb(LpPrinter *p, void *arg)
{
	(void)p;
	KtMsgJvmName *a = arg;
	jv_jvm_name(a->name, &a->ctx);
}

/* MessageGenerator::Generate */
static void kt_message_generator_generate(const KtMessageGen *g, LpPrinter *p)
{
	const JvContext *context = g->context;
	char *message = jv_escape_kotlin_keywords(jv_immutable_class_name_message(g->descriptor));
	kt_print(p, "@kotlin.OptIn"
		    "(com.google.protobuf.kotlin.OnlyForUseByGeneratedProtoCode::class)\n"
		    "@com.google.protobuf.kotlin.ProtoDslMarker\n");
	LpSub dsl_vars[] = {LP_SUB("jvm_synthetic", jv_jvm_synthetic(g->jvm_dsl)), LP_SUB("message", message)};
	lp_print_subs(p, dsl_vars, 2,
		      "public class Dsl private constructor(\n"
		      "  private val _builder: $message$.Builder\n"
		      ") {\n"
		      "  public companion object {\n"
		      "    $jvm_synthetic$"
		      "    @kotlin.PublishedApi\n"
		      "    internal fun _create(builder: $message$.Builder): Dsl = "
		      "Dsl(builder)\n"
		      "  }\n"
		      "\n"
		      "  $jvm_synthetic$"
		      "  @kotlin.PublishedApi\n"
		      "  internal fun _build(): $message$ = _builder.build()\n");

	lp_indent(p);

	for (int i = 0; i < g->descriptor->n_fields; i++) {
		kt_print(p, "\n");
		kt_field_generator_generate(&g->field_generators[i], p);
	}

	for (int i = 0; i < g->n_oneofs; i++) {
		const LpOneof *oneof = g->oneofs[i];
		const JvOneofGeneratorInfo *info = jv_context_oneof_info(context, oneof);
		const char *oneof_name = info->name;
		const char *oneof_capitalized_name = info->capitalized_name;
		char *oneof_case_getter = g->lite ? xcat(jv_get_kotlin_property_name(oneof_name), "Case")
						  : xcat("get", oneof_capitalized_name, "Case()");
		KtMsgJvmName jvm_name = {"get$oneof_capitalized_name$Case", {&context->options, p, g->lite}};
		LpSub subs[] = {
			LP_SUB_CB("jvm_name", kt_msg_jvm_name_cb, &jvm_name),
			LP_SUB_ANN("oneof_name_case", xcat(oneof_name, "Case"), oneof),
			LP_SUB("oneof_capitalized_name", oneof_capitalized_name),
			LP_SUB("oneof_case_getter", oneof_case_getter),
			{.key = "oneof_case_clearer",
			 .value = xcat("clear", oneof_capitalized_name),
			 .annotate = oneof,
			 .semantic = KT_SEMANTIC_SET},
			LP_SUB("message", message),
		};
		lp_emit(p, subs, 6,
			"public val $oneof_name_case$: $message$.$oneof_capitalized_name$Case\n"
			"$jvm_name$"
			"  get() = _builder.$oneof_case_getter$\n\n"
			"public fun $oneof_case_clearer$() {\n"
			"  _builder.$oneof_case_clearer$()\n"
			"}\n");
	}

	if (g->descriptor->n_extension_ranges > 0)
		kt_message_generator_generate_extensions(g, p);

	lp_outdent(p);
	kt_print(p, "}\n");
}

static void kt_message_generator_generate_members(const KtMessageGen *g, LpPrinter *p);

typedef struct {
	const KtMessageGen *g;
} KtBody;

/* The "body" callback of GenerateMembers */
static void kt_message_body_cb(LpPrinter *p, void *arg)
{
	const KtMessageGen *g = ((KtBody *)arg)->g;
	kt_message_generator_generate(g, p);
	for (int i = 0; i < g->descriptor->n_nested; i++) {
		const LpMessage *nested = &g->descriptor->nested[i];
		if (jv_is_map_entry(nested))
			continue;
		KtMessageGen nested_gen;
		kt_message_generator_init(&nested_gen, nested, g->context);
		kt_message_generator_generate_members(&nested_gen, p);
	}
}

/* MessageGenerator::GenerateMembers */
static void kt_message_generator_generate_members(const KtMessageGen *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	char *factory_name = jv_get_kotlin_factory_name(d);
	if (g->jvm_dsl) {
		LpSub vars[] = {LP_SUB("camelcase_name", factory_name)};
		lp_print_subs(p, vars, 1, "@kotlin.jvm.JvmName(\"-initialize$camelcase_name$\")\n");
	}

	LpSub subs[] = {
		LP_SUB_ANN("camelcase_name", factory_name, d),
		LP_SUB("message_kt", jv_escape_kotlin_keywords(jv_get_kotlin_extensions_class_name(d))),
		LP_SUB("message", jv_escape_kotlin_keywords(jv_immutable_class_name_message(d))),
	};
	lp_emit(p, subs, 3,
		"public inline fun $camelcase_name$(block: $message_kt$.Dsl.() -> "
		"kotlin.Unit): $message$ =\n"
		"  $message_kt$.Dsl._create($message$.newBuilder()).apply { block() "
		"}._build()\n");

	jv_write_message_doc_comment(p, d, &g->context->options, true);
	KtBody body = {g};
	LpSub body_subs[] = {
		LP_SUB_ANN("name_kt", xcat(d->name, "Kt"), d),
		LP_SUB_CB("body", kt_message_body_cb, &body),
	};
	lp_emit(p, body_subs, 2, "\n"
				 "    public object $name_kt$ {\n"
				 "      $body$;\n"
				 "    }\n"
				 "  ");
}

static void kt_message_generator_generate_or_null(const KtMessageGen *g, LpPrinter *p);

/* MessageGenerator::GenerateTopLevelMembers */
static void kt_message_generator_generate_top_level_members(const KtMessageGen *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (!g->lite)
		kt_print(p, "@kotlin.jvm.JvmSynthetic\n");
	LpSub vars[] = {
		LP_SUB("message", jv_escape_kotlin_keywords(jv_immutable_class_name_message(d))),
		LP_SUB("message_kt", jv_get_kotlin_extensions_class_name_escaped(d)),
	};
	lp_print_subs(p, vars, 2,
		      "public inline fun $message$.copy(block: $message_kt$.Dsl.() -> "
		      "kotlin.Unit): $message$ =\n"
		      "  $message_kt$.Dsl._create(this.toBuilder()).apply { block() "
		      "}._build()\n\n");

	for (int i = 0; i < d->n_nested; i++) {
		const LpMessage *nested = &d->nested[i];
		if (jv_is_map_entry(nested))
			continue;
		KtMessageGen nested_gen;
		kt_message_generator_init(&nested_gen, nested, g->context);
		kt_message_generator_generate_top_level_members(&nested_gen, p);
	}

	kt_message_generator_generate_or_null(g, p);
}

/* MessageGenerator::GenerateOrNull */
static void kt_message_generator_generate_or_null(const KtMessageGen *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *field = &d->fields[i];
		if (!proto_field_has_presence(field) || jv_get_java_type(field) != JAVATYPE_MESSAGE)
			continue;
		const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
		LpSub vars[] = {
			LP_SUB("full_classname", jv_escape_kotlin_keywords(jv_immutable_class_name_message(d))),
			LP_SUB("camelcase_name", info->name),
			LP_SUB("full_name", jv_escape_kotlin_keywords(jv_immutable_class_name_message(field->message_type))),
			LP_SUB("capitalized_name", info->capitalized_name),
			LP_SUB("name", jv_escape_kotlin_keywords(jv_get_kotlin_property_name(info->capitalized_name))),
			LP_SUB_ANN("getter_name", xcat(info->name, "OrNull"), field),
		};
		lp_push_vars(p, vars, 6);
		if (jv_field_deprecated(field)) {
			lp_emit(p, NULL, 0, "\n"
					    "          @kotlin.Deprecated(message = \"Field $camelcase_name$ is deprecated\")\n"
					    "      ");
		}
		if (!g->dsl_use_concrete_types) {
			/* We can use `FooOrBuilder`, and it saves code size to generate only one method
			 * instead of two. */
			lp_emit(p, NULL, 0, "\n"
					    "        public val $full_classname$OrBuilder.$getter_name$: $full_name$?\n"
					    "          get() = if (has$capitalized_name$()) get$capitalized_name$() else null\n"
					    "      ");
			kt_print(p, "\n");
		} else {
			/* We don't have `FooOrBuilder`, so we generate `Foo` and `Foo.Builder` methods. */
			lp_emit(p, NULL, 0, "\n"
					    "        public val $full_classname$.$getter_name$: $full_name$?\n"
					    "          get() = if (has$capitalized_name$()) this.$name$ else null\n"
					    "      ");
			kt_print(p, "\n");
			if (jv_field_deprecated(field)) {
				lp_emit(p, NULL, 0, "\n"
						    "          @kotlin.Deprecated(message = \"Field $camelcase_name$ is deprecated\")\n"
						    "        ");
			}
			lp_emit(p, NULL, 0, "\n"
					    "        public val $full_classname$.Builder.$getter_name$: $full_name$?\n"
					    "          get() = if (has$capitalized_name$()) this.$name$ else null\n"
					    "      ");
			kt_print(p, "\n");
		}
		lp_pop_vars(p);
	}
}

/* MessageGenerator::GenerateExtensions */
static void kt_message_generator_generate_extensions(const KtMessageGen *g, LpPrinter *p)
{
	KtMsgJvmName jvm_name = {"-getRepeatedExtension", {&g->context->options, p, g->lite}};
	LpSub vars[] = {
		LP_SUB("jvm_synthetic", jv_jvm_synthetic(g->jvm_dsl)),
		LP_SUB("message", jv_escape_kotlin_keywords(jv_immutable_class_name_message(g->descriptor))),
		LP_SUB_CB("jvm_name", kt_msg_jvm_name_cb, &jvm_name),
	};

	lp_print_subs(p, vars, 2,
		      "@Suppress(\"UNCHECKED_CAST\")\n"
		      "$jvm_synthetic$"
		      "public operator fun <T : kotlin.Any> get(extension: "
		      "com.google.protobuf.ExtensionLite<$message$, T>): T {\n"
		      "  return if (extension.isRepeated) {\n"
		      "    get(extension as com.google.protobuf.ExtensionLite<$message$, "
		      "kotlin.collections.List<*>>) as T\n"
		      "  } else {\n"
		      "    _builder.getExtension(extension)\n"
		      "  }\n"
		      "}\n\n");

	if (g->jvm_dsl) {
		/* TODO: generate this on Kotlin Native once the [Mutable]List issue is resolved. */
		lp_emit(p, vars, 3,
			"$jvm_synthetic$"
			"@kotlin.OptIn"
			"(com.google.protobuf.kotlin.OnlyForUseByGeneratedProtoCode::class)\n"
			"$jvm_name$"
			"public operator fun <E : kotlin.Any> get(\n"
			"  extension: com.google.protobuf.ExtensionLite<$message$, "
			"kotlin.collections.List<E>>\n"
			"): com.google.protobuf.kotlin.ExtensionList<E, $message$> {\n"
			"  return com.google.protobuf.kotlin.ExtensionList(extension, "
			"_builder.getExtension(extension))\n"
			"}\n\n");
	}

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public operator fun contains(extension: "
		      "com.google.protobuf.ExtensionLite<$message$, *>): "
		      "kotlin.Boolean {\n"
		      "  return _builder.hasExtension(extension)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public fun clear(extension: "
		      "com.google.protobuf.ExtensionLite<$message$, *>) "
		      "{\n"
		      "  _builder.clearExtension(extension)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public fun <T : kotlin.Any> setExtension(extension: "
		      "com.google.protobuf.ExtensionLite<$message$, T>, "
		      "value: T) {\n"
		      "  _builder.setExtension(extension, value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline operator fun <T : kotlin.Comparable<T>> set(\n"
		      "  extension: com.google.protobuf.ExtensionLite<$message$, T>,\n"
		      "  value: T\n"
		      ") {\n"
		      "  setExtension(extension, value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline operator fun set(\n"
		      "  extension: com.google.protobuf.ExtensionLite<$message$, "
		      "com.google.protobuf.ByteString>,\n"
		      "  value: com.google.protobuf.ByteString\n"
		      ") {\n"
		      "  setExtension(extension, value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline operator fun <T : com.google.protobuf.MessageLite> set(\n"
		      "  extension: com.google.protobuf.ExtensionLite<$message$, T>,\n"
		      "  value: T\n"
		      ") {\n"
		      "  setExtension(extension, value)\n"
		      "}\n\n");

	/* TODO: generate these methods on Kotlin Native once the [Mutable]List issue is resolved. */
	if (!g->jvm_dsl)
		return;

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public fun <E : kotlin.Any> com.google.protobuf.kotlin.ExtensionList<E, "
		      "$message$>.add(value: E) {\n"
		      "  _builder.addExtension(this.extension, value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline operator fun <E : kotlin.Any> "
		      "com.google.protobuf.kotlin.ExtensionList<E, "
		      "$message$>.plusAssign"
		      "(value: E) {\n"
		      "  add(value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public fun <E : kotlin.Any> com.google.protobuf.kotlin.ExtensionList<E, "
		      "$message$>.addAll(values: kotlin.collections.Iterable<E>) {\n"
		      "  for (value in values) {\n"
		      "    add(value)\n"
		      "  }\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline operator fun <E : kotlin.Any> "
		      "com.google.protobuf.kotlin.ExtensionList<E, "
		      "$message$>.plusAssign(values: "
		      "kotlin.collections.Iterable<E>) {\n"
		      "  addAll(values)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "public operator fun <E : kotlin.Any> "
		      "com.google.protobuf.kotlin.ExtensionList<E, "
		      "$message$>.set(index: Int, value: "
		      "E) {\n"
		      "  _builder.setExtension(this.extension, index, value)\n"
		      "}\n\n");

	lp_print_subs(p, vars, 2,
		      "$jvm_synthetic$"
		      "@Suppress(\"NOTHING_TO_INLINE\")\n"
		      "public inline fun com.google.protobuf.kotlin.ExtensionList<*, "
		      "$message$>.clear() {\n"
		      "  clear(extension)\n"
		      "}\n\n");
}
