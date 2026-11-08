// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/kotlin/field.{h,cc}: FieldGenerator, the members of a
 * field in the Kotlin DSL. printer->Print(text) is lp_print_subs (it sees the frames of
 * WithVars), printer->Emit(vars, text) lp_emit. */

typedef struct {
	const LpField *descriptor;
	LpSub *variables; /* variables_ (absl::flat_hash_map), as the subs of WithVars(variables_) */
	int n_variables;
	JvContext *context;
	bool lite;
} KtFieldGen;

/* AnnotationCollector::kSet as LpSub.semantic */
enum { KT_SEMANTIC_SET = 2 };

/* printer->Print(text) */
static void kt_print(LpPrinter *p, const char *text) { lp_print_subs(p, NULL, 0, text); }

static void kt_field_generator_init(KtFieldGen *g, const LpField *descriptor, JvContext *context, bool lite)
{
	g->descriptor = descriptor;
	g->context = context;
	g->lite = lite;
	LpVars variables = {0};
	jv_set_common_field_variables(descriptor, jv_context_field_info(context, descriptor), &variables);
	lp_vars_insert(&variables, "kt_deprecation",
		       jv_field_deprecated(descriptor)
			       ? xcat("@kotlin.Deprecated(message = \"Field ", lp_vars_get(&variables, "name"),
				      " is deprecated\") ")
			       : "");
	g->n_variables = variables.n;
	g->variables = xalloc(sizeof(LpSub) * (size_t)(variables.n + 1));
	for (int i = 0; i < variables.n; i++)
		g->variables[i] = LP_SUB(variables.kv[2 * i], variables.kv[2 * i + 1]);
}

static const char *kt_field_var(const KtFieldGen *g, const char *key)
{
	for (int i = 0; i < g->n_variables; i++)
		if (str_eq(g->variables[i].key, key))
			return g->variables[i].value;
	fail("Check failed: variables_.at(\"%s\")", key);
}

/* The callbacks {"jvm_name", [&] { java::JvmName(name, name_ctx); }} */
typedef struct {
	const char *name;
	const JvJvmNameContext *ctx;
} KtJvmName;

static void kt_jvm_name_cb(LpPrinter *p, void *arg)
{
	(void)p;
	const KtJvmName *a = arg;
	jv_jvm_name(a->name, a->ctx);
}

/* printer->Emit({{"jvm_name", JvmName(name)}}, format) */
static void kt_emit_jvm_name(LpPrinter *p, const JvJvmNameContext *ctx, const char *name, const char *format)
{
	KtJvmName a = {name, ctx};
	LpSub subs[] = {LP_SUB_CB("jvm_name", kt_jvm_name_cb, &a)};
	lp_emit(p, subs, 1, format);
}

/* printer->Emit({{"jvm_name_get", JvmName(get)}, {"jvm_name_set", JvmName(set)}}, format) */
static void kt_emit_jvm_name_get_set(LpPrinter *p, const JvJvmNameContext *ctx, const char *get, const char *set,
				     const char *format)
{
	KtJvmName a = {get, ctx}, b = {set, ctx};
	LpSub subs[] = {LP_SUB_CB("jvm_name_get", kt_jvm_name_cb, &a), LP_SUB_CB("jvm_name_set", kt_jvm_name_cb, &b)};
	lp_emit(p, subs, 2, format);
}

/* WriteFieldAccessorDocComment(printer, descriptor_, type, options, false, true) */
static void kt_accessor_doc(const KtFieldGen *g, LpPrinter *p, JvFieldAccessorType type)
{
	jv_write_field_accessor_doc_comment(p, g->descriptor, type, &g->context->options, false, true, false);
}

static void kt_field_doc(const KtFieldGen *g, LpPrinter *p)
{
	jv_write_field_doc_comment(p, g->descriptor, &g->context->options, true);
}

static const char kt_proxy_class[] =
	"/**\n"
	" * An uninstantiable, behaviorless type to represent the field in\n"
	" * generics.\n"
	" */\n"
	"@kotlin.OptIn"
	"(com.google.protobuf.kotlin.OnlyForUseByGeneratedProtoCode::class)\n"
	"public class ${$$kt_capitalized_name$Proxy$}$ private constructor()"
	" : com.google.protobuf.kotlin.DslProxy()\n";

static const char kt_clearer[] =
	"public fun $clearer$() {\n"
	"  $kt_dsl_builder$.${$clear$capitalized_name$$}$()\n"
	"}\n";

static const char kt_haser[] =
	"public fun $haser$(): kotlin.Boolean {\n"
	"  return $kt_dsl_builder$.${$has$capitalized_name$$}$()\n"
	"}\n";

static const char kt_jvm_get[] = "${$get$kt_capitalized_name$$}$";
static const char kt_jvm_set[] = "${$set$kt_capitalized_name$$}$";

static const char kt_property[] =
	"$kt_deprecation$public var $kt_name$: $kt_type$\n"
	"  $jvm_name_get$"
	"  $getter$() = $kt_dsl_builder$.${$$kt_safe_name$$}$\n"
	"  $jvm_name_set$"
	"  $setter$(value) {\n"
	"    $kt_dsl_builder$.${$$kt_safe_name$$}$ = value\n"
	"  }\n";

static void kt_generate_repeated_pritimive_field(const KtFieldGen *g, LpPrinter *p);
static void kt_generate_repeated_message_field(const KtFieldGen *g, LpPrinter *p);
static void kt_generate_repeated_string_field(const KtFieldGen *g, LpPrinter *p);
static void kt_generate_repeated_enum_field(const KtFieldGen *g, LpPrinter *p);
static void kt_generate_map_field(const KtFieldGen *g, LpPrinter *p);

static void kt_generate_pritimive_field(const KtFieldGen *g, LpPrinter *p)
{
	JvJavaType java_type = jv_get_java_type(g->descriptor);
	LpSub vars[] = {LP_SUB("kt_type", jv_kotlin_type_name(java_type))};
	lp_push_vars(p, vars, 1);

	if (proto_field_is_repeated(g->descriptor)) {
		kt_generate_repeated_pritimive_field(g, p);
		lp_pop_vars(p);
		return;
	}

	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_field_doc(g, p);
	if (str_eq(g->descriptor->name, "is_initialized")) {
		kt_emit_jvm_name_get_set(p, &name_ctx, kt_jvm_get, kt_jvm_set,
					 "// TODO: b/336400327 - remove this hack; we should access properties\n"
					 "$kt_deprecation$public var $kt_name$: $kt_type$\n"
					 "  $jvm_name_get$"
					 "  $getter$() = $kt_dsl_builder$.get${$$kt_capitalized_name$$}$()\n"
					 "  $jvm_name_set$"
					 "  $setter$(value) {\n"
					 "    $kt_dsl_builder$.${$set$kt_capitalized_name$$}$(value)\n"
					 "  }\n");
	} else {
		kt_emit_jvm_name_get_set(p, &name_ctx, kt_jvm_get, kt_jvm_set, kt_property);
	}

	kt_accessor_doc(g, p, JV_CLEARER);
	kt_print(p, kt_clearer);

	if (proto_field_has_presence(g->descriptor)) {
		kt_accessor_doc(g, p, JV_HAZZER);
		kt_print(p, kt_haser);
	}
	lp_pop_vars(p);
}

/* The DslList extensions of GenerateRepeated{Pritimive,Message,Enum}Field; `end` is the end of
 * each format ("}" or "}\n"). */
static void kt_generate_dsl_list_members(const KtFieldGen *g, LpPrinter *p, bool newline)
{
	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
#define KT_END(s) (newline ? s "}\n" : s "}")
	kt_accessor_doc(g, p, JV_LIST_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "add$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"public fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"add(value: $kt_type$) {\n"
				"  $kt_dsl_builder$.${$add$capitalized_name$$}$(value)\n"));

	kt_accessor_doc(g, p, JV_LIST_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "plusAssign$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"@Suppress(\"NOTHING_TO_INLINE\")\n"
				"public inline operator fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"plusAssign(value: $kt_type$) {\n"
				"  add(value)\n"));

	kt_accessor_doc(g, p, JV_LIST_MULTI_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "addAll$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"public fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"addAll(values: kotlin.collections.Iterable<$kt_type$>) {\n"
				"  $kt_dsl_builder$.${$addAll$capitalized_name$$}$(values)\n"));

	kt_accessor_doc(g, p, JV_LIST_MULTI_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "plusAssignAll$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"@Suppress(\"NOTHING_TO_INLINE\")\n"
				"public inline operator fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"plusAssign(values: kotlin.collections.Iterable<$kt_type$>) {\n"
				"  addAll(values)\n"));

	kt_accessor_doc(g, p, JV_LIST_INDEXED_SETTER);
	kt_emit_jvm_name(p, &name_ctx, "set$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"public operator fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"set(index: kotlin.Int, value: $kt_type$) {\n"
				"  $kt_dsl_builder$.${$set$capitalized_name$$}$(index, value)\n"));

	kt_accessor_doc(g, p, JV_CLEARER);
	kt_emit_jvm_name(p, &name_ctx, "clear$kt_capitalized_name$",
			 KT_END("$jvm_synthetic$"
				"$jvm_name$"
				"public fun com.google.protobuf.kotlin.DslList"
				"<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>."
				"clear() {\n"
				"  $kt_dsl_builder$.${$clear$capitalized_name$$}$()\n"));
#undef KT_END
}

/* The proxy class and the DslList property of GenerateRepeated{Pritimive,Message,Enum}Field */
static void kt_generate_dsl_list_property(const KtFieldGen *g, LpPrinter *p)
{
	kt_print(p, kt_proxy_class);
	kt_field_doc(g, p);
	kt_print(p, "$kt_deprecation$ public val $kt_name$: "
		    "com.google.protobuf.kotlin.DslList"
		    "<$kt_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
		    "$  jvm_synthetic$"
		    "  get() = com.google.protobuf.kotlin.DslList(\n"
		    "    $kt_dsl_builder$.${$$kt_property_name$List$}$\n"
		    "  )\n");
}

static void kt_generate_repeated_pritimive_field(const KtFieldGen *g, LpPrinter *p)
{
	kt_generate_dsl_list_property(g, p);
	kt_generate_dsl_list_members(g, p, false);
}

static void kt_generate_message_field(const KtFieldGen *g, LpPrinter *p)
{
	LpSub vars[] = {
		LP_SUB("kt_type", jv_escape_kotlin_keywords(jv_immutable_class_name_message(g->descriptor->message_type)))};
	lp_push_vars(p, vars, 1);

	if (proto_field_is_repeated(g->descriptor)) {
		kt_generate_repeated_message_field(g, p);
		lp_pop_vars(p);
		return;
	}

	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_field_doc(g, p);
	kt_emit_jvm_name_get_set(p, &name_ctx, kt_jvm_get, kt_jvm_set, kt_property);

	kt_accessor_doc(g, p, JV_CLEARER);
	kt_print(p, kt_clearer);

	kt_accessor_doc(g, p, JV_HAZZER);
	kt_print(p, kt_haser);
	if (proto_field_has_presence(g->descriptor) && !proto_field_real_oneof(g->descriptor)) {
		kt_print(p, "$kt_deprecation$\n"
			    "public val $classname$Kt.Dsl.$name$OrNull: $kt_type$?\n"
			    "  get() = $kt_dsl_builder$.$name$OrNull\n");
	}
	lp_pop_vars(p);
}

static void kt_generate_repeated_message_field(const KtFieldGen *g, LpPrinter *p)
{
	kt_generate_dsl_list_property(g, p);
	kt_generate_dsl_list_members(g, p, true);
}

static void kt_generate_string_field(const KtFieldGen *g, LpPrinter *p)
{
	if (proto_field_is_repeated(g->descriptor)) {
		kt_generate_repeated_string_field(g, p);
		return;
	}
	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_field_doc(g, p);
	kt_emit_jvm_name_get_set(p, &name_ctx, kt_jvm_get, kt_jvm_set,
				 "$kt_deprecation$public var $kt_name$: kotlin.String\n"
				 "  $jvm_name_get$"
				 "  $getter$() = $kt_dsl_builder$.${$$kt_safe_name$$}$\n"
				 "  $jvm_name_set$"
				 "  $setter$(value) {\n"
				 "    $kt_dsl_builder$.${$$kt_safe_name$$}$ = value\n"
				 "  }\n");

	kt_accessor_doc(g, p, JV_CLEARER);
	kt_print(p, kt_clearer);

	if (proto_field_has_presence(g->descriptor)) {
		kt_accessor_doc(g, p, JV_HAZZER);
		kt_print(p, kt_haser);
	}
}

static void kt_generate_repeated_string_field(const KtFieldGen *g, LpPrinter *p)
{
	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_print(p, kt_proxy_class);

	/* property for List<String> */
	kt_accessor_doc(g, p, JV_LIST_GETTER);
	kt_print(p, "$kt_deprecation$public val $kt_name$: "
		    "com.google.protobuf.kotlin.DslList"
		    "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>\n"
		    "@kotlin.OptIn"
		    "(com.google.protobuf.kotlin.OnlyForUseByGeneratedProtoCode::class)\n"
		    "  get() = com.google.protobuf.kotlin.DslList(\n"
		    "    $kt_dsl_builder$.${$$kt_property_name$List$}$\n"
		    "  )\n");

	/* List<String>.add(String) */
	kt_accessor_doc(g, p, JV_LIST_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "add$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "add(value: kotlin.String) {\n"
			 "  $kt_dsl_builder$.${$add$capitalized_name$$}$(value)\n"
			 "}\n");

	/* List<String> += String */
	kt_accessor_doc(g, p, JV_LIST_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "plusAssign$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "@Suppress(\"NOTHING_TO_INLINE\")\n"
			 "public inline operator fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "plusAssign(value: kotlin.String) {\n"
			 "  add(value)\n"
			 "}\n");

	/* List<String>.addAll(Iterable<String>) */
	kt_accessor_doc(g, p, JV_LIST_MULTI_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "addAll$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "addAll(values: kotlin.collections.Iterable<kotlin.String>) {\n"
			 "  $kt_dsl_builder$.${$addAll$capitalized_name$$}$(values)\n"
			 "}\n");

	/* List<String> += Iterable<String> */
	kt_accessor_doc(g, p, JV_LIST_MULTI_ADDER);
	kt_emit_jvm_name(p, &name_ctx, "plusAssignAll$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "@Suppress(\"NOTHING_TO_INLINE\")\n"
			 "public inline operator fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "plusAssign(values: kotlin.collections.Iterable<kotlin.String>) {\n"
			 "  addAll(values)\n"
			 "}\n");

	/* List<String>[Int] = String */
	kt_accessor_doc(g, p, JV_LIST_INDEXED_SETTER);
	kt_emit_jvm_name(p, &name_ctx, "set$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public operator fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "set(index: kotlin.Int, value: kotlin.String) {\n"
			 "  $kt_dsl_builder$.${$set$capitalized_name$$}$(index, value)\n"
			 "}");

	/* upstream names the JVM method of clear() "set..." */
	kt_accessor_doc(g, p, JV_CLEARER);
	kt_emit_jvm_name(p, &name_ctx, "set$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslList"
			 "<kotlin.String, ${$$kt_capitalized_name$Proxy$}$>."
			 "clear() {\n"
			 "  $kt_dsl_builder$.${$clear$capitalized_name$$}$()\n"
			 "}");
}

static void kt_generate_enum_field(const KtFieldGen *g, LpPrinter *p)
{
	LpSub vars[] = {
		LP_SUB("kt_type", jv_escape_kotlin_keywords(jv_immutable_class_name_enum(g->descriptor->enum_type)))};
	lp_push_vars(p, vars, 1);

	if (proto_field_is_repeated(g->descriptor)) {
		kt_generate_repeated_enum_field(g, p);
		lp_pop_vars(p);
		return;
	}

	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_field_doc(g, p);
	kt_emit_jvm_name_get_set(p, &name_ctx, kt_jvm_get, kt_jvm_set, kt_property);

	if (jv_support_unknown_enum_value(g->descriptor)) {
		kt_emit_jvm_name_get_set(p, &name_ctx, "${$get$kt_capitalized_name$Value$}$",
					 "${$set$kt_capitalized_name$Value$}$",
					 "$kt_deprecation$public var $kt_name$Value: kotlin.Int\n"
					 "  $jvm_name_get$"
					 "  $getter$() = $kt_dsl_builder$.${$$kt_property_name$Value$}$\n"
					 "  $jvm_name_set$"
					 "  $setter$(value) {\n"
					 "    $kt_dsl_builder$.${$$kt_property_name$Value$}$ = value\n"
					 "  }\n");
	}

	kt_accessor_doc(g, p, JV_CLEARER);
	kt_print(p, kt_clearer);

	if (proto_field_has_presence(g->descriptor)) {
		kt_accessor_doc(g, p, JV_HAZZER);
		kt_print(p, kt_haser);
	}
	lp_pop_vars(p);
}

static void kt_generate_repeated_enum_field(const KtFieldGen *g, LpPrinter *p)
{
	kt_generate_dsl_list_property(g, p);
	kt_generate_dsl_list_members(g, p, false);
}

/* KotlinTypeName(field, name_resolver) of the anonymous namespace */
static char *kt_field_kotlin_type_name(const LpField *field)
{
	switch (jv_get_java_type(field)) {
	case JAVATYPE_MESSAGE: return jv_immutable_class_name_message(field->message_type);
	case JAVATYPE_ENUM: return jv_immutable_class_name_enum(field->enum_type);
	default: return (char *)jv_kotlin_type_name(jv_get_java_type(field));
	}
}

static void kt_generate_map_field(const KtFieldGen *g, LpPrinter *p)
{
	const LpField *key = jv_map_key_field(g->descriptor);
	const LpField *value = jv_map_value_field(g->descriptor);
	LpSub vars[] = {LP_SUB("kt_key_type", kt_field_kotlin_type_name(key)),
			LP_SUB("kt_value_type", kt_field_kotlin_type_name(value))};
	lp_push_vars(p, vars, 2);

	JvJvmNameContext name_ctx = {&g->context->options, p, g->lite};
	kt_print(p, kt_proxy_class);

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "get$kt_capitalized_name$Map",
			 "$kt_deprecation$ public val $kt_name$: "
			 "com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "$  jvm_synthetic$"
			 "$jvm_name$"
			 "  get() = com.google.protobuf.kotlin.DslMap(\n"
			 "    $kt_dsl_builder$.${$$kt_property_name$Map$}$\n"
			 "  )\n");

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "put$kt_capitalized_name$",
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "  .put(key: $kt_key_type$, value: $kt_value_type$) {\n"
			 "     $kt_dsl_builder$.${$put$capitalized_name$$}$(key, value)\n"
			 "   }\n");

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "set$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "@Suppress(\"NOTHING_TO_INLINE\")\n"
			 "public inline operator fun com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "  .set(key: $kt_key_type$, value: $kt_value_type$) {\n"
			 "     put(key, value)\n"
			 "   }\n");

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "remove$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "  .remove(key: $kt_key_type$) {\n"
			 "     $kt_dsl_builder$.${$remove$capitalized_name$$}$(key)\n"
			 "   }\n");

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "putAll$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "  .putAll(map: kotlin.collections.Map<$kt_key_type$, $kt_value_type$>) "
			 "{\n"
			 "     $kt_dsl_builder$.${$putAll$capitalized_name$$}$(map)\n"
			 "   }\n");

	kt_field_doc(g, p);
	kt_emit_jvm_name(p, &name_ctx, "clear$kt_capitalized_name$",
			 "$jvm_synthetic$"
			 "$jvm_name$"
			 "public fun com.google.protobuf.kotlin.DslMap"
			 "<$kt_key_type$, $kt_value_type$, ${$$kt_capitalized_name$Proxy$}$>\n"
			 "  .clear() {\n"
			 "     $kt_dsl_builder$.${$clear$capitalized_name$$}$()\n"
			 "   }\n");
	lp_pop_vars(p);
}

/* FieldGenerator::Generate */
static void kt_field_generator_generate(const KtFieldGen *g, LpPrinter *p)
{
	const LpField *d = g->descriptor;
	lp_push_vars(p, g->variables, g->n_variables);
	const char *kt_capitalized_name = kt_field_var(g, "kt_capitalized_name");
	LpSub vars[] = {
		LP_SUB_ANN("kt_name", kt_field_var(g, "kt_name"), d),
		LP_SUB_ANN("getter", "get", d),
		{.key = "setter", .value = "set", .annotate = d, .semantic = KT_SEMANTIC_SET},
		{.key = "clearer", .value = xcat("clear", kt_capitalized_name), .annotate = d, .semantic = KT_SEMANTIC_SET},
		LP_SUB_ANN("haser", xcat("has", kt_capitalized_name), d),
	};
	lp_push_vars(p, vars, 5);
	switch (jv_get_java_type(d)) {
	case JAVATYPE_MESSAGE:
		if (proto_field_is_repeated(d) && jv_is_map_entry(d->message_type))
			kt_generate_map_field(g, p);
		else
			kt_generate_message_field(g, p);
		break;
	case JAVATYPE_STRING: kt_generate_string_field(g, p); break;
	case JAVATYPE_ENUM: kt_generate_enum_field(g, p); break;
	default: kt_generate_pritimive_field(g, p); break;
	}
	lp_pop_vars(p);
	lp_pop_vars(p);
}
