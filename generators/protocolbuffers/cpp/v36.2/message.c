// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/message.{h,cc} (protobuf v36.2). Lambdas become
 * static callbacks taking a context struct; the open-source build has no split messages, lazy
 * or weak fields, profiles or verification, and the branches for them are ported where they
 * shape the output with their (always false) predicates. */

/* ConditionalToCheckBitmasks */
static char *conditional_to_check_bitmasks(const uint32_t *masks, int n, bool return_success, const char *has_bits_var)
{
	Vec parts = {0};
	for (int i = 0; i < n; ++i) {
		if (masks[i] == 0)
			continue;
		char *m = xfmt("0x%08x", masks[i]);
		vec_push(&parts, xcat("((", has_bits_var, "[", cpp_itoa(i), "] & ", m, ") ^ ", m, ")"));
	}
	if (!parts.n)
		fail("Check failed: !parts.empty()");
	char *result = parts.n == 1 ? parts.v[0] : xcat("(", str_join(&parts, "\n       | "), ")");
	return xcat(result, return_success ? " == 0" : " != 0");
}

/* GenerateConditionMaybeWithProbability (no probabilities without a profile) */
static char *generate_condition_maybe_with_probability(uint32_t mask, bool use_cached_has_bits, int has_array_index,
						      bool is_batch)
{
	if (use_cached_has_bits)
		return xfmt("%sCheckHasBit(cached_has_bits, 0x%08xU)", is_batch ? "Batch" : "", mask);
	return xfmt("%sCheckHasBit(this_._impl_._has_bits_[%d], 0x%08xU)", is_batch ? "Batch" : "", has_array_index, mask);
}

static char *generate_condition_for_field(int has_bit_index, const LpField *f, const Options *o)
{
	return generate_condition_maybe_with_probability(1u << (has_bit_index % 32), true, -1, false);
}

static char *generate_condition_for_group(uint32_t mask, const Options *o)
{
	return generate_condition_maybe_with_probability(mask, true, -1, true);
}

static void print_presence_check_condition(const LpField *f, const FieldLayout *l, LpPrinter *p, const Options *o)
{
	if (!is_weak_option(f))
		EMIT(p, "$condition$", S_("condition", generate_condition_for_field(layout_has_bit_index(l, f), f, o)));
	else
		EMIT0(p, "has_$name$()");
}

/* SortFieldsByNumber */
static const LpField **sort_fields_by_number(const LpMessage *d)
{
	int n;
	return get_ordered_fields(d, &n);
}

static bool is_pod(const LpField *f)
{
	if (is_repeated(f) || f->is_extension)
		return false;
	switch (cpp_type(f)) {
	case LP_CPPTYPE_STRING:
	case LP_CPPTYPE_MESSAGE:
		return false;
	default:
		return true;
	}
}

/* FindRuns: runs[field->index] = the length of the run starting at that field (0: none) */
static int *find_runs(const LpMessage *d, const LpField *const *fields, int n, bool (*predicate)(const LpField *, const void *),
		      const void *arg)
{
	int *runs = xalloc(sizeof(int) * (size_t)(d->n_fields + 1));
	const LpField *last_start = NULL;
	for (int i = 0; i < n; i++) {
		if (predicate(fields[i], arg)) {
			if (!last_start)
				last_start = fields[i];
			runs[last_start->index]++;
		} else {
			last_start = NULL;
		}
	}
	return runs;
}

typedef struct {
	const char *prefix;
	const LpField *field;
	const Options *options;
	bool split;
	LpEmitCallback emit_body;
	void *body_arg;
} NonDefaultCtx;

static void emit_non_default_check(LpPrinter *p, const char *prefix, const LpField *f, const Options *o)
{
	LpSub v[] = {S_("prefix", prefix), S_("name", field_name(f))};
	lp_push_vars(p, v, 2);
	if (!f->containing_oneof) {
		switch (cpp_type(f)) {
		case LP_CPPTYPE_STRING:
			EMIT0(p, "!$prefix$_internal_$name$().empty()");
			break;
		case LP_CPPTYPE_MESSAGE:
			EMIT0(p, "$prefix$_internal_has_$name$()");
			break;
		case LP_CPPTYPE_FLOAT:
			EMIT0(p, "::absl::bit_cast<::uint32_t>($prefix$_internal_$name$()) != 0");
			break;
		case LP_CPPTYPE_DOUBLE:
			EMIT0(p, "::absl::bit_cast<::uint64_t>($prefix$_internal_$name$()) != 0");
			break;
		default:
			EMIT0(p, "$prefix$_internal_$name$() != 0");
			break;
		}
	} else if (real_oneof(f)) {
		EMIT0(p, "$prefix$$has_field$");
	}
	POP_VARS(p);
}

static bool should_emit_non_default_check(const LpField *f, const Options *o)
{
	if (get_field_hasbit_mode(f, o) == HASBIT_TRUE)
		return false;
	return !is_repeated(f);
}

static void ndc_condition(LpPrinter *p, void *arg)
{
	const NonDefaultCtx *c = arg;
	emit_non_default_check(p, c->prefix, c->field, c->options);
}

static void ndc_body(LpPrinter *p, void *arg)
{
	const NonDefaultCtx *c = arg;
	c->emit_body(p, c->body_arg);
}

static void ndc_set_empty_string(LpPrinter *p, void *arg)
{
	const NonDefaultCtx *c = arg;
	EMIT(p,
	     "\n"
	     " if (_this->$field_$.IsDefault()) {\n"
	     "   _this->_internal_set_$name$(\"\");\n"
	     " }\n",
	     S_("prefix", c->prefix), S_("name", field_name(c->field)), S_("field_", field_member_name(c->field, c->split)));
}

static void emit_non_default_check_for_string(LpPrinter *p, const char *prefix, const LpField *f, bool split,
					      const Options *o, LpEmitCallback emit_body, void *body_arg)
{
	NonDefaultCtx c = {prefix, f, o, split, emit_body, body_arg};
	EMIT(p,
	     "\n"
	     " if ($condition$) {\n"
	     "   $emit_body$;\n"
	     " } else {\n"
	     "   $set_empty_string$;\n"
	     " }\n",
	     SCB("condition", ndc_condition, &c), SCB("emit_body", ndc_body, &c),
	     SCB("set_empty_string", ndc_set_empty_string, &c));
}

/* MayEmitIfNonDefaultCheck */
static void may_emit_if_non_default_check(LpPrinter *p, const char *prefix, const LpField *f, const Options *o,
					  LpEmitCallback emit_body, void *body_arg, bool with_enclosing_braces_always)
{
	NonDefaultCtx c = {prefix, f, o, false, emit_body, body_arg};
	if (should_emit_non_default_check(f, o)) {
		EMIT(p,
		     "\n"
		     " if ($condition$) {\n"
		     "   $emit_body$;\n"
		     " }\n",
		     SCB("condition", ndc_condition, &c), SCB("emit_body", ndc_body, &c));
		return;
	}
	if (with_enclosing_braces_always) {
		EMIT(p,
		     "\n"
		     " {\n"
		     "   //~ Force newline.\n"
		     "   $emit_body$;\n"
		     " }\n",
		     SCB("emit_body", ndc_body, &c));
		return;
	}
	emit_body(p, body_arg);
}

static void may_emit_mutable_if_non_default_check(LpPrinter *p, const char *prefix, const LpField *f, bool split,
						  const Options *o, LpEmitCallback emit_body, void *body_arg,
						  bool with_enclosing_braces_always)
{
	if (should_emit_non_default_check(f, o) && cpp_type(f) == LP_CPPTYPE_STRING && is_arena_string_ptr(f, o)) {
		emit_non_default_check_for_string(p, prefix, f, split, o, emit_body, body_arg);
		return;
	}
	may_emit_if_non_default_check(p, prefix, f, o, emit_body, body_arg, with_enclosing_braces_always);
}

static bool has_internal_has_method(const LpField *f, const Options *o)
{
	return !is_repeated(f) && !has_hasbit(f, o) && cpp_type(f) == LP_CPPTYPE_MESSAGE;
}

/* CollectMapInfo */
static void collect_map_info(const Options *o, const LpMessage *d, Subs *vars)
{
	const LpField *key = map_key(d), *val = map_value(d);
	subs_set(vars, "key_cpp", primitive_type_name(o, cpp_type(key)));
	switch (cpp_type(val)) {
	case LP_CPPTYPE_MESSAGE:
		subs_set(vars, "val_cpp", xcat("::", protobuf_namespace(o), "::Message"));
		break;
	case LP_CPPTYPE_ENUM:
		subs_set(vars, "val_cpp", qualified_enum_class_name(val->enum_type, o));
		break;
	default:
		subs_set(vars, "val_cpp", primitive_type_name(o, cpp_type(val)));
		break;
	}
	subs_set(vars, "key_wire_type", xcat("TYPE_", ascii_upper(declared_type_method_name(key->type))));
	subs_set(vars, "val_wire_type", xcat("TYPE_", ascii_upper(declared_type_method_name(val->type))));
}

static bool should_serialize_in_order(const LpMessage *d, const Options *o) { return true; }

static bool is_cross_file_map_field(const LpField *f)
{
	if (!is_map(f))
		return false;
	return is_cross_file_message(proto_message_field_by_number(f->message_type, 2));
}

static bool is_cross_file_maybe_map(const LpField *f) { return is_cross_file_map_field(f) || is_cross_file_message(f); }

static bool has_non_split_optional_string(const LpMessage *d, const Options *o)
{
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *f = &d->fields[i];
		if (is_string(f) && !is_repeated(f) && !real_oneof(f) && !should_split(f, o))
			return true;
	}
	return false;
}

static bool may_group_chunks_for_haswords_check(const FieldChunk *a, const FieldChunk *b)
{
	return a->has_hasbit == b->has_hasbit && a->is_rarely_present == b->is_rarely_present &&
	       a->should_split == b->should_split;
}

static int popcnt(uint32_t n)
{
	int result = 0;
	while (n != 0) {
		result += (int)(n & 1);
		n = n / 2;
	}
	return result;
}

/* MaybeEmitHaswordsCheck: only with a profile (IsProfileDriven), never in the open-source build */
static bool maybe_emit_haswords_check(const FieldChunks *cs, int it, int end, const Options *o)
{
	if (!cs->v[it].has_hasbit || !is_profile_driven(o) || end - it < 2 || !cs->v[it].is_rarely_present)
		return false;
	return false;
}

/* ClassVars */
static Subs class_vars(const LpMessage *d, const Options *o)
{
	Subs v = {0};
	subs_push(&v, S_("pkg", namespace_of_file(d->file)));
	subs_push(&v, S_("Msg", class_name(d)));
	subs_push(&v, S_("pkg::Msg", qualified_class_name(d, o)));
	subs_push(&v, S_("pkg.Msg", d->full_name));
	subs_push(&v, S_("classtype", qualified_class_name(d, o)));
	subs_push(&v, S_("full_name", d->full_name));
	subs_push(&v, S_("nodiscard", o->opensource_runtime ? "[[nodiscard]]" : ""));
	subs_push(&v, S_("unused", o->opensource_runtime ? "PROTOBUF_FUTURE_ADD_EARLY_WARN_UNUSED" : ""));
	subs_push(&v, S_("globals", msg_globals_instance_name(d, o)));
	subs_push(&v, S_("globals_type", msg_globals_instance_type(d, o)));
	subs_push(&v, SSUF("WeakDescriptorSelfPin",
			   using_implicit_weak_descriptor(d->file, o) ? xcat(strong_reference_to_type(d, o), ";") : "", ";"));
	Subs mv = {0};
	set_common_message_data_variables(d, &mv);
	subs_append(&v, &mv);
	Subs uv = {0};
	set_unknown_fields_variable(d, o, &uv);
	subs_append(&v, &uv);
	return v;
}

static Subs message_vars_of(const LpMessage *d)
{
	Subs v = {0};
	set_common_message_data_variables(d, &v);
	return v;
}

/* ---- MessageGenerator ---- */

static MessageGenerator *message_generator_new(const LpMessage *descriptor, int index_in_file_messages,
					       const Options *options)
{
	MessageGenerator *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->index_in_file_messages = index_in_file_messages;
	g->options = *options;
	g->field_layout = build_optimized_layout(descriptor, &g->options, &g->num_weak_fields);
	field_generator_table_build(&g->field_generators, descriptor, &g->options, &g->field_layout);
	for (int i = 0; i < descriptor->n_fields; ++i)
		if (proto_field_is_required(&descriptor->fields[i]))
			++g->num_required_fields;
	g->parse_function_generator =
		parse_function_generator_new(descriptor, layout_has_hasbits(&g->field_layout), layout_get_has_bit_index_cb,
					     &g->field_layout, &g->options, &g->variables, index_in_file_messages);
	return g;
}

static bool mg_should_generate_enclosing_if(const MessageGenerator *g, const LpField *f)
{
	if (layout_has_bit_index(&g->field_layout, f) < 0)
		return false;
	if (is_repeated(f))
		return true;
	return cpp_type(f) == LP_CPPTYPE_MESSAGE || cpp_type(f) == LP_CPPTYPE_STRING;
}

static Subs mg_has_bit_vars(const MessageGenerator *g, const LpField *f)
{
	int has_bit_index = layout_has_bit_index(&g->field_layout, f);
	Subs v = {0};
	subs_push(&v, S_("has_array_index", cpp_itoa(has_bit_index / 32)));
	subs_push(&v, S_("has_mask", xfmt("0x%08xU", 1u << (has_bit_index % 32))));
	return v;
}

static void mg_add_generators(MessageGenerator *g, Vec *enum_generators, Vec *extension_generators)
{
	for (int i = 0; i < g->descriptor->n_enums; ++i) {
		EnumGenerator *e = enum_generator_new(&g->descriptor->enums[i], &g->options);
		vec_push(enum_generators, e);
		vec_push(&g->enum_generators, e);
	}
	for (int i = 0; i < g->descriptor->n_extensions; ++i) {
		ExtensionGenerator *e = extension_generator_new(&g->descriptor->extensions[i], &g->options);
		vec_push(extension_generators, e);
		vec_push(&g->extension_generators, e);
	}
}

/* a callback context: the generator and a field */
typedef struct {
	MessageGenerator *g;
	const LpField *field;
	const char *name;
	const void *extra;
	int i;
	bool flag;
} MgCtx;

typedef struct {
	MessageGenerator *g;
	const LpField **fields;
	int n;
} MgFields;

static void mg_k_fields(LpPrinter *p, void *arg)
{
	const MgFields *c = arg;
	for (int i = 0; i < c->n; i++) {
		const LpField *f = c->fields[i];
		Subs v = {0};
		field_vars(&v, f, &c->g->options);
		PUSH_SUBS(p, &v);
		EMIT(p,
		     "\n"
		     " $kField$ = $number$,\n",
		     SANN("kField", field_constant_name(f), f));
		POP_VARS(p);
	}
}

static void mg_sizer(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	if (!is_repeated(c->field))
		return;
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED $int $name_size$() $const_impl$;\n",
	     SANN("name_size", xcat(c->name, "_size"), c->field));
	EMIT(p,
	     "\n"
	     " private:\n"
	     " int $_internal_name_size$() const;\n"
	     "\n"
	     " public:\n",
	     SANN("_internal_name_size", xcat("_internal_", c->name, "_size"), c->field));
}

static void mg_hazzer(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	if (!proto_field_has_presence(c->field))
		return;
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED $bool $has_name$() $const_impl$;\n",
	     SANN("has_name", xcat("has_", c->name), c->field));
}

static void mg_internal_hazzer(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	if (!has_internal_has_method(c->field, &c->g->options))
		return;
	EMIT(p,
	     "\n"
	     " private:\n"
	     " bool $_internal_has_name$() const;\n"
	     "\n"
	     " public:\n",
	     SANN("_internal_has_name", xcat("_internal_has_", c->name), c->field));
}

static void mg_clearer(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	EMIT(p,
	     "\n"
	     " $DEPRECATED $void $clear_name$() $impl$;\n",
	     SANNSEM("clear_name", xcat("clear_", c->name), c->field, SEM_SET));
}

static void mg_accessors(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	fg_generate_accessor_declarations(fg_get(&c->g->field_generators, c->field), p);
}

static void mg_generate_field_accessor_declarations(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	Subs mv = message_vars_of(d);
	PUSH_SUBS(p, &mv);
	const LpField **ordered = xalloc(sizeof(LpField *) * (size_t)(d->n_fields + 1));
	int n = 0;
	for (int i = 0; i < g->field_layout.n_fields; i++)
		ordered[n++] = g->field_layout.fields[i];
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *f = &d->fields[i];
		if (!real_oneof(f) && !is_weak_option(f))
			continue;
		ordered[n++] = f;
	}
	if (n) {
		MgFields c = {.g = g, .fields = ordered, .n = n};
		EMIT(p,
		     "\n"
		     " enum : int {\n"
		     "   $kFields$,\n"
		     " };\n",
		     SCB("kFields", mg_k_fields, &c));
	}
	for (int i = 0; i < n; i++) {
		const LpField *f = ordered[i];
		MgCtx c = {.g = g, .field = f, .name = field_name(f)};
		Subs v = {0};
		field_vars(&v, f, &g->options);
		PUSH_SUBS(p, &v);
		Subs t = make_tracker_calls_field(f, &g->options);
		PUSH_SUBS(p, &t);
		EMIT(p,
		     "\n"
		     " // $field_comment$\n"
		     " $sizer$;\n"
		     " $hazzer$;\n"
		     " $internal_hazzer$;\n"
		     " $clearer$;\n"
		     " $accessors$;\n",
		     S_("field_comment", field_comment(f, &g->options)), SSUF("const_impl", "const;", ";"),
		     SSUF("impl", ";", ";"), SCB("sizer", mg_sizer, &c), SCB("hazzer", mg_hazzer, &c),
		     SCB("internal_hazzer", mg_internal_hazzer, &c), SCB("clearer", mg_clearer, &c),
		     SCB("accessors", mg_accessors, &c));
		POP_VARS(p);
		POP_VARS(p);
	}
	if (d->n_extension_ranges > 0) {
		EMIT0(p, "\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           typename = typename _proto_TypeTraits::Singular>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline bool HasExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id) const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_has$;\n"
		"   return $extensions$.Has(id.number());\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void ClearExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $extensions$.ClearExtension(id.number());\n"
		"   $annotate_extension_clear$;\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           typename = typename _proto_TypeTraits::Repeated>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline int ExtensionSize(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id) const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_repeated_size$;\n"
		"   return $extensions$.ExtensionSize(id.number());\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           ::std::enable_if_t<!_proto_TypeTraits::kLifetimeBound, int> = 0>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline\n"
		"     typename _proto_TypeTraits::Singular::ConstType\n"
		"     GetExtension(const $pbi$::ExtensionIdentifier<\n"
		"                  $Msg$, _proto_TypeTraits, _field_type, _is_packed>& id)\n"
		"         const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_get$;\n"
		"   return _proto_TypeTraits::Get(id.number(), $extensions$, id.default_value());\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           ::std::enable_if_t<_proto_TypeTraits::kLifetimeBound, int> = 0>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline\n"
		"     typename _proto_TypeTraits::Singular::ConstType\n"
		"     GetExtension(const $pbi$::ExtensionIdentifier<\n"
		"                  $Msg$, _proto_TypeTraits, _field_type, _is_packed>& id)\n"
		"         const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_get$;\n"
		"   return _proto_TypeTraits::Get(GetArena(), id.number(), $extensions$,\n"
		"                                 id.default_value());\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline typename _proto_TypeTraits::Singular::MutableType MutableExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id)\n"
		"     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_mutable$;\n"
		"   return _proto_TypeTraits::Mutable(GetArena(), id.number(), _field_type,\n"
		"                                     &$extensions$);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void SetExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     typename _proto_TypeTraits::Singular::ConstType value) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   _proto_TypeTraits::Set(GetArena(), id.number(), _field_type, value,\n"
		"                          &$extensions$);\n"
		"   $annotate_extension_set$;\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void SetAllocatedExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     typename _proto_TypeTraits::Singular::MutableType value) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   _proto_TypeTraits::SetAllocated(GetArena(), id.number(), _field_type,\n"
		"                                   value, &$extensions$);\n"
		"   $annotate_extension_set$;\n"
		" }\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void UnsafeArenaSetAllocatedExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     typename _proto_TypeTraits::Singular::MutableType value) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   _proto_TypeTraits::UnsafeArenaSetAllocated(\n"
		"       GetArena(), id.number(), _field_type, value, &$extensions$);\n"
		"   $annotate_extension_set$;\n"
		" }\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" [[nodiscard]] inline typename _proto_TypeTraits::Singular::MutableType\n"
		" ReleaseExtension(const $pbi$::ExtensionIdentifier<\n"
		"                  $Msg$, _proto_TypeTraits, _field_type, _is_packed>& id) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_release$;\n"
		"   return _proto_TypeTraits::Release(GetArena(), id.number(), _field_type,\n"
		"                                     &$extensions$);\n"
		" }\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline typename _proto_TypeTraits::Singular::MutableType\n"
		" UnsafeArenaReleaseExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_extension_release$;\n"
		"   return _proto_TypeTraits::UnsafeArenaRelease(\n"
		"       GetArena(), id.number(), _field_type, &$extensions$);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           ::std::enable_if_t<!_proto_TypeTraits::kLifetimeBound, int> = 0>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline\n"
		"     typename _proto_TypeTraits::Repeated::ConstType\n"
		"     GetExtension(\n"
		"         const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                          _field_type, _is_packed>& id,\n"
		"         int index) const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_repeated_extension_get$;\n"
		"   return _proto_TypeTraits::Get(id.number(), $extensions$, index);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed,\n"
		"           ::std::enable_if_t<_proto_TypeTraits::kLifetimeBound, int> = 0>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline\n"
		"     typename _proto_TypeTraits::Repeated::ConstType\n"
		"     GetExtension(\n"
		"         const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                          _field_type, _is_packed>& id,\n"
		"         int index) const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_repeated_extension_get$;\n"
		"   return _proto_TypeTraits::Get(id.number(), $extensions$, index);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline typename _proto_TypeTraits::Repeated::MutableType MutableExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     int index) ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_repeated_extension_mutable$;\n"
		"   return _proto_TypeTraits::Mutable(id.number(), index, &$extensions$);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void SetExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     int index, typename _proto_TypeTraits::Repeated::ConstType value) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   _proto_TypeTraits::Set(id.number(), index, value, &$extensions$);\n"
		"   $annotate_repeated_extension_set$;\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline typename _proto_TypeTraits::Repeated::MutableType AddExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id)\n"
		"     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   typename _proto_TypeTraits::Repeated::MutableType to_add =\n"
		"       _proto_TypeTraits::Add(GetArena(), id.number(), _field_type,\n"
		"                              &$extensions$);\n"
		"   $annotate_repeated_extension_add_mutable$;\n"
		"   return to_add;\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline void AddExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id,\n"
		"     typename _proto_TypeTraits::Repeated::ConstType value) {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   _proto_TypeTraits::Add(GetArena(), id.number(), _field_type, _is_packed,\n"
		"                          value, &$extensions$);\n"
		"   $annotate_repeated_extension_add$;\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" PROTOBUF_FUTURE_ADD_NODISCARD inline const typename _proto_TypeTraits::\n"
		"     Repeated::RepeatedFieldType&\n"
		"     GetRepeatedExtension(\n"
		"         const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                          _field_type, _is_packed>& id)\n"
		"         const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_repeated_extension_list$;\n"
		"   return _proto_TypeTraits::GetRepeated(id.number(), $extensions$);\n"
		" }\n"
		"\n"
		" template <typename _proto_TypeTraits, $pbi$::FieldType _field_type,\n"
		"           bool _is_packed>\n"
		" inline typename _proto_TypeTraits::Repeated::RepeatedFieldType* $nonnull$\n"
		" MutableRepeatedExtension(\n"
		"     const $pbi$::ExtensionIdentifier<$Msg$, _proto_TypeTraits,\n"
		"                                      _field_type, _is_packed>& id)\n"
		"     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_repeated_extension_list_mutable$;\n"
		"   return _proto_TypeTraits::MutableRepeated(\n"
		"       GetArena(), id.number(), _field_type, _is_packed, &$extensions$);\n"
		" }\n");
		if (msg_option(d, CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT) && !g->options.opensource_runtime &&
		    !g->options.lite_implicit_weak_fields)
			EMIT0(p, "\n"
				 " GOOGLE_PROTOBUF_EXTENSION_MESSAGE_SET_ACCESSORS($Msg$);\n");
	}
	for (int i = 0; i < d->n_real_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		EMIT(p,
		     "\n"
		     " void $clear_oneof_name$();\n"
		     " $OneOfName$Case $oneof_name$_case() const;\n",
		     S_("oneof_name", oneof->name), SANNSEM("clear_oneof_name", xcat("clear_", oneof->name), oneof, SEM_SET),
		     S_("OneOfName", underscores_to_camel_case(oneof->name, true)));
	}
	POP_VARS(p);
}

static void mg_assume(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	if (cpp_type(c->field) == LP_CPPTYPE_MESSAGE && !is_lazy(c->field, &c->g->options))
		EMIT0(p, "PROTOBUF_ASSUME(!value || $field_$ != nullptr);");
}

static void mg_generate_singular_field_has_bits(MessageGenerator *g, const LpField *f, LpPrinter *p)
{
	Subs t = make_tracker_calls_field(f, &g->options);
	PUSH_SUBS(p, &t);
	if (is_weak_option(f)) {
		EMIT0(p, "\n"
			 " inline bool $Msg$::has_$name$() const {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_has$;\n"
			 "   return $weak_field_map$.Has($number$);\n"
			 " }\n");
		POP_VARS(p);
		return;
	}
	if (get_field_hasbit_mode(f, &g->options) == HASBIT_TRUE) {
		Subs v = mg_has_bit_vars(g, f);
		PUSH_SUBS(p, &v);
		MgCtx c = {.g = g, .field = f};
		EMIT(p,
		     "\n"
		     " inline bool $Msg$::has_$name$() const {\n"
		     "   $WeakDescriptorSelfPin$;\n"
		     "   $annotate_has$;\n"
		     "   bool value = CheckHasBit($has_bits$[$has_array_index$], $has_mask$);\n"
		     "   $ASSUME$;\n"
		     "   return value;\n"
		     " }\n",
		     SCBSUF("ASSUME", mg_assume, &c, ";"));
		POP_VARS(p);
	}
	POP_VARS(p);
}

static void mg_generate_oneof_has_bits(MessageGenerator *g, LpPrinter *p)
{
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		const LpOneof *oneof = &g->descriptor->oneofs[i];
		EMIT(p,
		     "\n"
		     " inline bool $Msg$::has_$oneof_name$() const {\n"
		     "   return $oneof_name$_case() != $cap_oneof_name$_NOT_SET;\n"
		     " }\n"
		     " inline void $Msg$::clear_has_$oneof_name$() {\n"
		     "   $oneof_case$[$oneof_index$] = $cap_oneof_name$_NOT_SET;\n"
		     " }\n",
		     SINT("oneof_index", oneof->index), S_("oneof_name", oneof->name),
		     S_("cap_oneof_name", ascii_upper(oneof->name)));
	}
}

static void mg_generate_oneof_member_has_bits(MessageGenerator *g, const LpField *f, LpPrinter *p)
{
	if (proto_field_has_presence(f)) {
		Subs t = make_tracker_calls_field(f, &g->options);
		PUSH_SUBS(p, &t);
		EMIT0(p, "\n"
			 " inline bool $Msg$::has_$name$() const {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_has$;\n"
			 "   return $has_field$;\n"
			 " }\n");
		POP_VARS(p);
	}
	if (has_internal_has_method(f, &g->options))
		EMIT0(p, "\n"
			 " inline bool $Msg$::_internal_has_$name_internal$() const {\n"
			 "   return $has_field$;\n"
			 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::set_has_$name_internal$() {\n"
		 "   $oneof_case$[$oneof_index$] = k$field_name$;\n"
		 " }\n");
}

static void mg_clearing_code_cb(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	fg_generate_clearing_code(fg_get(&c->g->field_generators, c->field), p);
}

static void mg_field_clear_body(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	const LpField *f = c->field;
	if (real_oneof(f)) {
		EMIT(p, "\n"
		" if ($has_field$) {\n"
		"   $clearing_code$;\n"
		"   clear_has_$oneof_name$();\n"
		" }\n", SCB("clearing_code", mg_clearing_code_cb, c));
	} else {
		if (should_split(f, &c->g->options))
			EMIT0(p, "\n"
		" if (ABSL_PREDICT_TRUE(IsSplitMessageDefault()))\n"
		"   return;\n");
		fg_generate_clearing_code(fg_get(&c->g->field_generators, f), p);
		if (has_hasbit(f, &c->g->options)) {
			Subs v = mg_has_bit_vars(c->g, f);
			PUSH_SUBS(p, &v);
			EMIT0(p, "\n"
		" ClearHasBit($has_bits$[$has_array_index$], $has_mask$);\n");
			POP_VARS(p);
		}
	}
}

static void mg_generate_field_clear(MessageGenerator *g, const LpField *f, bool is_inline, LpPrinter *p)
{
	Subs t = make_tracker_calls_field(f, &g->options);
	PUSH_SUBS(p, &t);
	MgCtx c = {.g = g, .field = f};
	EMIT(p, "\n"
		" $inline $void $Msg$::clear_$name$() {\n"
		"   $pbi$::TSanWrite(&_impl_);\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $body$;\n"
		"   $annotate_clear$;\n"
		" }\n", S_("inline", is_inline ? "inline" : ""), SCB("body", mg_field_clear_body, &c));
	POP_VARS(p);
}

static void mg_generate_check_has_bit_consistency(MessageGenerator *g, LpPrinter *p, const char *prefix)
{
	EMIT(p, "\n"
		" if constexpr (::_pbi::DebugHardenCheckHasBitConsistency()) {\n"
		"   $prefix$CheckHasBitConsistency();\n"
		" }\n", S_("prefix", prefix));
}

static void mg_byte_size_body(LpPrinter *p, void *arg)
{
	const MgCtx *c = arg;
	fg_generate_byte_size(fg_get(&c->g->field_generators, c->field), p);
}

static void mg_check_nondefault_and_emit_body(LpPrinter *p, void *arg)
{
	MgCtx *c = (MgCtx *)arg;
	may_emit_if_non_default_check(p, "this_.", c->field, &c->g->options, mg_byte_size_body, c, false);
}

static void mg_emit_check_and_update_byte_size_for_field(const MessageGenerator *g, const LpField *f, LpPrinter *p)
{
	MgCtx c = {.g = (MessageGenerator *)g, .field = f};
	if (!has_hasbit(f, &g->options)) {
		may_emit_if_non_default_check(p, "this_.", f, &g->options, mg_byte_size_body, &c, true);
		return;
	}
	if (is_weak_option(f)) {
		EMIT(p, "\n"
		" if (has_$name$()) {\n"
		"   $emit_body$;\n"
		" }\n", SCB("emit_body", mg_byte_size_body, &c));
		return;
	}
	int has_bit_index = layout_has_bit_index(&g->field_layout, f);
	EMIT(p, "\n"
		" if ($condition$) {\n"
		"   $check_nondefault_and_emit_body$;\n"
		" }\n", S_("condition", generate_condition_for_field(has_bit_index, f, &g->options)),
	     SCB("check_nondefault_and_emit_body", mg_check_nondefault_and_emit_body, &c));
}

static void mg_maybe_emit_update_cached_hasbits(const MessageGenerator *g, const LpField *f, LpPrinter *p,
						int *cached_has_word_index)
{
	if (!has_hasbit(f, &g->options) || is_weak_option(f))
		return;
	int has_word_index = layout_has_word_index(&g->field_layout, f);
	if (*cached_has_word_index == has_word_index)
		return;
	*cached_has_word_index = has_word_index;
	EMIT(p, "\n"
		" cached_has_bits = this_.$has_bits$[$index$];\n", SINT("index", *cached_has_word_index));
}

typedef struct {
	const MessageGenerator *g;
	const LpField *field;
	int *cached_has_word_index;
} UpdateByteSizeCtx;

static void ubs_comment(LpPrinter *p, void *arg)
{
	const UpdateByteSizeCtx *c = arg;
	print_field_comment(p, NULL, c->field, &c->g->options);
}

static void ubs_update_cached(LpPrinter *p, void *arg)
{
	const UpdateByteSizeCtx *c = arg;
	mg_maybe_emit_update_cached_hasbits(c->g, c->field, p, c->cached_has_word_index);
}

static void ubs_check_and_update(LpPrinter *p, void *arg)
{
	const UpdateByteSizeCtx *c = arg;
	mg_emit_check_and_update_byte_size_for_field(c->g, c->field, p);
}

static void mg_emit_update_byte_size_for_field(const MessageGenerator *g, const LpField *f, LpPrinter *p,
					       int *cached_has_word_index)
{
	UpdateByteSizeCtx c = {.g = g, .field = f, .cached_has_word_index = cached_has_word_index};
	EMIT(p, "\n"
		" $comment$;\n"
		" $update_cached_has_bits$;\n"
		" $check_and_update_byte_size_for_field$;\n", SCB("comment", ubs_comment, &c), SCB("update_cached_has_bits", ubs_update_cached, &c),
	     SCB("check_and_update_byte_size_for_field", ubs_check_and_update, &c));
}

static void mg_generate_field_accessor_definitions(MessageGenerator *g, LpPrinter *p)
{
	EMIT0(p, "// $Msg$\n\n");
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *f = &g->descriptor->fields[i];
		print_field_comment(p, NULL, f, &g->options);
		Subs v = {0};
		field_vars(&v, f, &g->options);
		PUSH_SUBS(p, &v);
		Subs t = make_tracker_calls_field(f, &g->options);
		PUSH_SUBS(p, &t);
		if (is_repeated(f))
			EMIT0(p, "\n"
		" inline int $Msg$::_internal_$name_internal$_size() const {\n"
		"   return _internal_$name_internal$().size();\n"
		" }\n"
		" inline int $Msg$::$name$_size() const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_size$;\n"
		"   return _internal_$name_internal$_size();\n"
		" }\n");
		else if (real_oneof(f))
			mg_generate_oneof_member_has_bits(g, f, p);
		else
			mg_generate_singular_field_has_bits(g, f, p);
		if (!is_cross_file_maybe_map(f))
			mg_generate_field_clear(g, f, true, p);
		fg_generate_inline_accessor_definitions(fg_get(&g->field_generators, f), p);
		EMIT0(p, "\n");
		POP_VARS(p);
		POP_VARS(p);
	}
	mg_generate_oneof_has_bits(g, p);
}

static void mg_generate_annotation_decl(MessageGenerator *g, LpPrinter *p) {}

static void mg_nothing_cb(LpPrinter *p, void *arg) {}

static void mg_decl_annotate(LpPrinter *p, void *arg) { mg_generate_annotation_decl(arg, p); }

static void mg_alias_parse_table_type(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	pfg_generate_alias_parse_table_type(g->parse_function_generator, p);
}

static void mg_parse_decls(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	pfg_generate_data_decls(g->parse_function_generator, p);
}

static void mg_generate_map_entry_class_definition(MessageGenerator *g, LpPrinter *p)
{
	Subs vars = {0};
	collect_map_info(&g->options, g->descriptor, &vars);
	PUSH_SUBS(p, &vars);
	EMIT(p, "\n"
		" class $unused $$Msg$ final\n"
		"     : public $pbi$::MapEntry<$key_cpp$, $val_cpp$,\n"
		"                              $pbi$::WireFormatLite::$key_wire_type$,\n"
		"                              $pbi$::WireFormatLite::$val_wire_type$> {\n"
		"   using Super_ =\n"
		"       $pbi$::MapEntry<$key_cpp$, $val_cpp$,\n"
		"                       $pbi$::WireFormatLite::$key_wire_type$,\n"
		"                       $pbi$::WireFormatLite::$val_wire_type$>;\n"
		"\n"
		"  public:\n"
		"   $Msg$();\n"
		"   template <typename = void>\n"
		"   explicit constexpr $Msg$($pbi$::ConstantInitialized,\n"
		"                            const $pbi$::ClassData* $nonnull$\n"
		"                                class_data);\n"
		"   explicit $Msg$($pb$::Arena* $nullable$ arena);\n"
		"   static constexpr const void* $nonnull$ internal_message_globals() {\n"
		"     return &$globals$;\n"
		"   }\n"
		"\n"
		"   $decl_verify_func$;\n"
		"\n"
		"   static constexpr auto InternalGenerateClassData_(\n"
		"       const $pb$::MessageLite& prototype,\n"
		"       const $pbi$::TcParseTableBase* $nullable$ tc_table = nullptr);\n"
		"\n"
		"  private:\n"
		"   friend class $pb$::MessageLite;\n"
		"   friend struct ::$tablename$;\n"
		"   friend $pbi$::PrivateAccess;\n"
		"   friend $globals_type$;\n"
		"\n"
		"   $alias_parse_table_type$;\n"
		"   static constexpr ParseTableT_ InternalGenerateParseTable_(\n"
		"       const $pbi$::ClassData* $nonnull$ class_data);\n"
		"   $parse_decls$;\n"
		"   $decl_annotate$;\n"
		"\n"
		"   const $pbi$::ClassData* $nonnull$ GetClassData() const PROTOBUF_FINAL;\n"
		"   static void* $nonnull$ PlacementNew_(\n"
		"       //~\n"
		"       const void* $nonnull$, void* $nonnull$ mem,\n"
		"       $pb$::Arena* $nullable$ arena);\n"
		"   static constexpr auto InternalNewImpl_();\n"
		" };\n", SCB("decl_verify_func", mg_nothing_cb, NULL), SCB("decl_annotate", mg_decl_annotate, g),
	     SCB("alias_parse_table_type", mg_alias_parse_table_type, g), SCB("parse_decls", mg_parse_decls, g));
	POP_VARS(p);
}

typedef struct {
	MessageGenerator *g;
	bool need_to_emit_cached_size;
	const LpOneof *oneof;
} ImplCtx;

static void impl_extension_set(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (c->g->descriptor->n_extension_ranges == 0)
		return;
	EMIT0(p, "\n"
		" $pbi$::ExtensionSet _extensions_;\n");
}

static void impl_tracker(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (!has_tracker(c->g->descriptor, &c->g->options))
		return;
	EMIT0(p, "\n"
		" static $pb$::AccessListener<$Msg$> _tracker_;\n"
		" static void TrackerOnGetMetadata() { $annotate_reflection$; }\n");
}

static void impl_has_bits(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (!layout_has_hasbits(&c->g->field_layout))
		return;
	EMIT(p, "\n"
		" $pbi$::HasBits<$sizeof_has_bits$> _has_bits_;\n", SINT("sizeof_has_bits", layout_has_bits_size(&c->g->field_layout)));
	if (c->need_to_emit_cached_size) {
		EMIT0(p, "\n"
		" $pbi$::CachedSize _cached_size_;\n");
		c->need_to_emit_cached_size = false;
	}
}

static void impl_field_members(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	MessageGenerator *g = c->g;
	for (int i = 0; i < g->field_layout.n_fields; i++) {
		const LpField *f = g->field_layout.fields[i];
		fg_generate_static_members(fg_get(&g->field_generators, f), p);
		if (!should_split(f, &g->options))
			fg_generate_private_members(fg_get(&g->field_generators, f), p);
	}
}

static void impl_secondary_field_members(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	MessageGenerator *g = c->g;
	for (int i = 0; i < g->field_layout.n_fields; i++) {
		const LpField *f = g->field_layout.fields[i];
		if (!should_split(f, &g->options))
			fg_generate_secondary_private_members(fg_get(&g->field_generators, f), p);
	}
}

static void impl_decl_split(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (!should_split_message(c->g->descriptor, &c->g->options))
		return;
}

static void impl_oneof_fields(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	for (int i = 0; i < c->oneof->n_fields; i++)
		fg_generate_private_members(fg_get(&c->g->field_generators, proto_oneof_field(c->oneof, i)), p);
}

static void impl_oneof_members(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	const LpMessage *d = c->g->descriptor;
	for (int i = 0; i < d->n_real_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		c->oneof = oneof;
		EMIT(p, "\n"
		" union $camel_oneof_name$Union {\n"
		"   constexpr $camel_oneof_name$Union() : _constinit_{} {}\n"
		"   $pbi$::ConstantInitialized _constinit_;\n"
		"   $oneof_fields$;\n"
		" } $oneof_name$_;\n", S_("camel_oneof_name", underscores_to_camel_case(oneof->name, true)),
		     S_("oneof_name", oneof->name), SCB("oneof_fields", impl_oneof_fields, c));
		for (int j = 0; j < oneof->n_fields; j++)
			fg_generate_static_members(fg_get(&c->g->field_generators, proto_oneof_field(oneof, j)), p);
	}
}

static void impl_cached_size_if_no_hasbits(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (!c->need_to_emit_cached_size)
		return;
	c->need_to_emit_cached_size = false;
	EMIT0(p, "\n"
		" $pbi$::CachedSize _cached_size_;\n");
}

static void impl_oneof_case(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (c->g->descriptor->n_real_oneofs == 0)
		return;
	EMIT(p, "\n"
		" $uint32$ _oneof_case_[$count$];\n", SINT("count", c->g->descriptor->n_real_oneofs));
}

static void impl_weak_field_map(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (c->g->num_weak_fields == 0)
		return;
	EMIT0(p, "\n"
		" $pbi$::WeakFieldMap _weak_field_map_;\n");
}

static void impl_union_impl(LpPrinter *p, void *arg)
{
	ImplCtx *c = arg;
	if (!has_impl_data(c->g->descriptor, &c->g->options))
		return;
	EMIT0(p, "union { Impl_ _impl_; };");
}

static void mg_generate_impl_definition(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	ImplCtx c = {.g = g, .need_to_emit_cached_size = true};
	EMIT(p, "\n"
		" struct Impl_ {\n"
		"   //~ TODO: check if/when there is a need for an\n"
		"   //~ outline dtor.\n"
		"   inline explicit constexpr Impl_($pbi$::InternalVisibility visibility,\n"
		"                                   $pbi$::ConstantInitialized) noexcept;\n"
		"   inline explicit Impl_(\n"
		"       //~\n"
		"       $pbi$::InternalVisibility visibility,\n"
		"       $pb$::Arena* $nullable$ arena);\n"
		"   inline explicit Impl_(\n"
		"       //~\n"
		"       $pbi$::InternalVisibility visibility,\n"
		"       $pb$::Arena* $nullable$ arena, const Impl_& from,\n"
		"       const $Msg$& from_msg);\n"
		"   //~ Members assumed to align to 8 bytes:\n"
		"   $extension_set$;\n"
		"   $tracker$;\n"
		"   $has_bits$;\n"
		"   //~ Field members:\n"
		"   $field_members$;\n"
		"   $decl_split$;\n"
		"   $oneof_members$;\n"
		"   //~ Members assumed to align to 4 bytes:\n"
		"   $cached_size_if_no_hasbits$;\n"
		"   $oneof_case$;\n"
		"   $weak_field_map$;\n"
		"   $secondary_field_members$;\n"
		"   //~ For detecting when concurrent accessor calls cause races.\n"
		"   PROTOBUF_TSAN_DECLARE_MEMBER\n"
		" };\n"
		" $union_impl$;\n", SCB("extension_set", impl_extension_set, &c), SCB("tracker", impl_tracker, &c),
	     SCB("has_bits", impl_has_bits, &c), SCB("field_members", impl_field_members, &c),
	     SCB("secondary_field_members", impl_secondary_field_members, &c), SCB("decl_split", impl_decl_split, &c),
	     SCB("oneof_members", impl_oneof_members, &c),
	     SCB("cached_size_if_no_hasbits", impl_cached_size_if_no_hasbits, &c), SCB("oneof_case", impl_oneof_case, &c),
	     SCB("weak_field_map", impl_weak_field_map, &c), SCB("union_impl", impl_union_impl, &c));
}

static void any_methods(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_descriptor_methods(g->descriptor->file, &g->options))
		EMIT0(p, "\n"
		" $nodiscard $bool PackFrom(const $pb$::Message& message) {\n"
		"   $DCHK$_NE(&message, this);\n"
		"   return $pbi$::InternalPackFrom(message, mutable_type_url(), mutable_value());\n"
		" }\n"
		" $nodiscard $bool\n"
		" PackFrom(const $pb$::Message& message,\n"
		"          ::absl::string_view type_url_prefix) {\n"
		"   $DCHK$_NE(&message, this);\n"
		"   return $pbi$::InternalPackFrom(message, type_url_prefix,\n"
		"                                  mutable_type_url(),\n"
		"                                  mutable_value());\n"
		" }\n"
		" $nodiscard $bool UnpackTo($pb$::Message* $nonnull$ message) const {\n"
		"   return $pbi$::InternalUnpackTo(_internal_type_url(),\n"
		"                                  _internal_value(), message);\n"
		" }\n"
		" $nodiscard $static bool GetAnyFieldDescriptors(\n"
		"     const $pb$::Message& message,\n"
		"     const $pb$::FieldDescriptor * $nullable$ *\n"
		"         $nonnull$ type_url_field,\n"
		"     const $pb$::FieldDescriptor * $nullable$ *\n"
		"         $nonnull$ value_field);\n"
		" template <\n"
		"     typename T,\n"
		"     class = typename std::enable_if<!std::is_convertible<\n"
		"         T, const $pb$::Message&>::value>::type>\n"
		" $nodiscard $bool PackFrom(const T& message) {\n"
		"   return $pbi$::InternalPackFrom<T>(\n"
		"       message, mutable_type_url(), mutable_value());\n"
		" }\n"
		" template <\n"
		"     typename T,\n"
		"     class = typename std::enable_if<!std::is_convertible<\n"
		"         T, const $pb$::Message&>::value>::type>\n"
		" $nodiscard $bool PackFrom(\n"
		"     const T& message, ::absl::string_view type_url_prefix) {\n"
		"   return $pbi$::InternalPackFrom<T>(message, type_url_prefix,\n"
		"                                     mutable_type_url(),\n"
		"                                     mutable_value());\n"
		" }\n"
		" template <\n"
		"     typename T,\n"
		"     class = typename std::enable_if<!std::is_convertible<\n"
		"         T, const $pb$::Message&>::value>::type>\n"
		" $nodiscard $bool UnpackTo(T* $nonnull$ message) const {\n"
		"   return $pbi$::InternalUnpackTo<T>(\n"
		"       _internal_type_url(), _internal_value(), message);\n"
		" }\n");
	else
		EMIT0(p, "\n"
		" template <typename T>\n"
		" $nodiscard $bool PackFrom(const T& message) {\n"
		"   return $pbi$::InternalPackFrom(message, mutable_type_url(), mutable_value());\n"
		" }\n"
		" template <typename T>\n"
		" $nodiscard $bool PackFrom(\n"
		"     const T& message, ::absl::string_view type_url_prefix) {\n"
		"   return $pbi$::InternalPackFrom(message, type_url_prefix,\n"
		"                                  mutable_type_url(),\n"
		"                                  mutable_value());\n"
		" }\n"
		" template <typename T>\n"
		" $nodiscard $bool UnpackTo(T* $nonnull$ message) const {\n"
		"   return $pbi$::InternalUnpackTo(_internal_type_url(),\n"
		"                                  _internal_value(), message);\n"
		" }\n");
}

static void mg_generate_any_method_definition(MessageGenerator *g, LpPrinter *p)
{
	EMIT(p, "\n"
		" // implements Any\n"
		" // -----------------------------------------------\n"
		"\n"
		" $any_methods$;\n"
		"\n"
		" template <typename T>\n"
		" $nodiscard $bool Is() const {\n"
		"   return $pbi$::InternalIs<T>(_internal_type_url());\n"
		" }\n"
		" $nodiscard $static bool ParseAnyTypeUrl(\n"
		"     //~\n"
		"     ::absl::string_view type_url,\n"
		"     std::string* $nonnull$ full_type_name);\n", SCB("any_methods", any_methods, g));
}

static ArenaDtorNeeds mg_needs_arena_destructor(const MessageGenerator *g);
static bool mg_needs_is_initialized(MessageGenerator *g);

static void cd_superclass(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options)) {
		EMIT0(p, "$pb$::MessageLite");
		return;
	}
	const char *simple_base = simple_base_class(g->descriptor, &g->options);
	if (!*simple_base)
		EMIT0(p, "$pb$::Message");
	else
		EMIT(p, "$pbi$::$simple_base$", S_("simple_base", simple_base));
}

static void cd_decl_dtor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT0(p, "\n"
		" ~$Msg$() PROTOBUF_FINAL;\n");
}

static void cd_descriptor_accessor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options) || msg_option(g->descriptor, 2))
		return;
	EMIT0(p, "\n"
		" $nodiscard $static const $pb$::Descriptor* $nonnull$ descriptor() {\n"
		"   return GetDescriptor();\n"
		" }\n");
}

static void cd_get_descriptor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options))
		return;
	EMIT0(p, "\n"
		" $nodiscard $static const $pb$::Descriptor* $nonnull$\n"
		" GetDescriptor() {\n"
		"   return default_instance().GetMetadata().descriptor;\n"
		" }\n"
		" $nodiscard $static const $pb$::Reflection* $nonnull$ GetReflection() {\n"
		"   return default_instance().GetMetadata().reflection;\n"
		" }\n");
}

static void cd_oneof_field(LpPrinter *p, void *arg)
{
	const LpOneof *oneof = arg;
	for (int i = 0; i < oneof->n_fields; i++) {
		const LpField *f = proto_oneof_field(oneof, i);
		EMIT(p, "\n"
		" $oneof_constant$ = $field_number$,\n", SANN("oneof_constant", lp_cpp_oneof_case_constant_name(f), f),
		     SINT("field_number", f->number));
	}
}

static void cd_decl_oneof(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		const LpOneof *oneof = &g->descriptor->oneofs[i];
		EMIT(p, "\n"
		" enum $oneof_camel_name$ {\n"
		"   $oneof_field$,\n"
		"   $upper_oneof_name$_NOT_SET = 0,\n"
		" };\n",
		     SANN("oneof_camel_name", xcat(underscores_to_camel_case(oneof->name, true), "Case"), oneof),
		     SCB("oneof_field", cd_oneof_field, oneof), S_("upper_oneof_name", ascii_upper(oneof->name)));
	}
}

static void cd_decl_any_methods(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!is_any_message(g->descriptor))
		return;
	mg_generate_any_method_definition(g, p);
}

static void cd_generated_methods(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	const LpFile *file = g->descriptor->file;
	if (!has_generated_methods(file, &g->options))
		return;
	if (has_descriptor_methods(file, &g->options)) {
		if (!has_simple_base_class(g->descriptor, &g->options))
			EMIT0(p, "\n"
		" using Super_::CopyFrom;\n"
		" void CopyFrom(const $Msg$& from);\n"
		" using Super_::MergeFrom;\n"
		" void MergeFrom(const $Msg$& from) { $Msg$::MergeImpl(*this, from); }\n"
		"\n"
		" private:\n"
		" static void MergeImpl($pb$::MessageLite& to_msg,\n"
		"                       const $pb$::MessageLite& from_msg);\n"
		"\n"
		" public:\n");
		else
			EMIT0(p, "\n"
		" using Super_::CopyFrom;\n"
		" inline void CopyFrom(const $Msg$& from) { Super_::CopyImpl(*this, from); }\n"
		" using Super_::MergeFrom;\n"
		" void MergeFrom(const $Msg$& from) { Super_::MergeImpl(*this, from); }\n"
		"\n"
		" public:\n");
	} else {
		EMIT0(p, "\n"
		" void CopyFrom(const $Msg$& from);\n"
		" void MergeFrom(const $Msg$& from) { $Msg$::MergeImpl(*this, from); }\n"
		"\n"
		" private:\n"
		" static void MergeImpl($pb$::MessageLite& to_msg,\n"
		"                       const $pb$::MessageLite& from_msg);\n"
		"\n"
		" public:\n");
	}
	if (mg_needs_is_initialized(g))
		EMIT0(p, "\n"
		" $nodiscard $bool IsInitialized() const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   return IsInitializedImpl(*this);\n"
		" }\n"
		"\n"
		" private:\n"
		" static bool IsInitializedImpl(const MessageLite& msg);\n"
		"\n"
		" public:\n");
	else
		EMIT0(p, "\n"
		" $nodiscard $bool IsInitialized() const {\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   return true;\n"
		" }\n");
	if (!has_simple_base_class(g->descriptor, &g->options))
		EMIT0(p, "\n"
		" ABSL_ATTRIBUTE_REINITIALIZES void Clear() PROTOBUF_FINAL;\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" private:\n"
		" $nodiscard $static ::size_t ByteSizeLong(const $pb$::MessageLite& msg);\n"
		" $nodiscard $static $uint8$* $nonnull$ _InternalSerialize(\n"
		"     const $pb$::MessageLite& msg, $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream);\n"
		"\n"
		" public:\n"
		" $nodiscard $::size_t ByteSizeLong() const { return ByteSizeLong(*this); }\n"
		" $nodiscard $$uint8$* $nonnull$ _InternalSerialize(\n"
		"     $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) const {\n"
		"   return _InternalSerialize(*this, target, stream);\n"
		" }\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" $nodiscard $::size_t ByteSizeLong() const final;\n"
		" $nodiscard $$uint8$* $nonnull$ _InternalSerialize(\n"
		"     //~\n"
		"     $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) const final;\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n");
}

static void cd_internal_field_number(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!g->options.inject_field_listener_events)
		return;
	EMIT(p, "\n"
		" static constexpr int _kInternalFieldNumber = $field_count$;\n", SINT("field_count", g->descriptor->n_fields));
}

static void cd_decl_non_simple_base(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT0(p, "\n"
		" $nodiscard $int GetCachedSize() const {\n"
		"   return $cached_size$.Get();\n"
		" }\n"
		"\n"
		" private:\n"
		" void SharedCtor($pb$::Arena* $nullable$ arena);\n"
		" static void SharedDtor(MessageLite& self);\n"
		" void InternalSwap($Msg$* $nonnull$ other);\n");
}

static void cd_arena_dtor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (mg_needs_arena_destructor(g) == ARENA_DTOR_REQUIRED)
		EMIT0(p, "\n"
		" private:\n"
		" static void ArenaDtor(void* $nonnull$ object);\n");
}

static void cd_get_metadata(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options))
		return;
	EMIT0(p, "\n"
		" $nodiscard $$pb$::Metadata GetMetadata() const;\n");
}

static void cd_nested_types(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_nested; ++i) {
		const LpMessage *nested = &g->descriptor->nested[i];
		if (!is_map_entry_message(nested))
			EMIT(p, "\n"
		" using $nested_name$ = $nested_full_name$;\n", SANN("nested_full_name", class_name(nested), nested),
			     SANN("nested_name",
				  lp_cpp_resolve_known_name_collisions(nested->name, LP_CPP_CTX_MESSAGE, LP_CPP_KIND_TYPE),
				  nested));
	}
}

static void cd_nested_enums(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_enums; ++i)
		enum_generate_symbol_imports(g->enum_generators.v[i], p);
}

static void cd_decl_field_accessors(LpPrinter *p, void *arg) { mg_generate_field_accessor_declarations(arg, p); }

static void cd_decl_extension_ids(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_extensions; ++i)
		ext_generate_declaration(g->extension_generators.v[i], p);
}

static void cd_decl_set_has(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *f = &g->descriptor->fields[i];
		if (!is_repeated(f) && !is_weak_option(f) && real_oneof(f))
			EMIT(p, "\n"
		" void set_has_$field_name$();\n", S_("field_name", field_name(f)));
	}
}

static void cd_decl_oneof_has(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++)
		EMIT(p, "\n"
		" [[nodiscard]] inline bool has_$oneof_name$() const;\n"
		" inline void clear_has_$oneof_name$();\n", S_("oneof_name", g->descriptor->oneofs[i].name));
}

static void cd_post_loop_handler(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!needs_post_loop_handler(g->descriptor, &g->options))
		return;
	EMIT0(p, "\n"
		" static const char* $nullable$ PostLoopHandler(\n"
		"     MessageLite* $nonnull$ msg, const char* $nullable$ ptr,\n"
		"     $pbi$::ParseContext* $nonnull$ ctx);\n");
}

static void cd_decl_impl(LpPrinter *p, void *arg) { mg_generate_impl_definition(arg, p); }

static void mg_generate_class_definition(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (!should_generate_class(d, &g->options))
		return;
	Subs v = class_vars(d, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(d, &g->options);
	PUSH_SUBS(p, &t);
	if (is_map_entry_message(d)) {
		mg_generate_map_entry_class_definition(g, p);
		POP_VARS(p);
		POP_VARS(p);
		return;
	}
	LpSub ann[] = {SANN("Msg", class_name(d), d)};
	lp_push_vars(p, ann, 1);
	EMIT(p, "\n"
		" class $dllexport_decl $ $unused $$Msg$ final : public $superclass$\n"
		" /* @@protoc_insertion_point(class_definition:$full_name$) */ {\n"
		"   using Super_ = $superclass$;\n"
		"\n"
		"  public:\n"
		"   inline $Msg$() : $Msg$(nullptr) {}\n"
		"   $decl_dtor$;\n"
		"\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"   //~ Define a derived `operator delete` to avoid dynamic dispatch when\n"
		"   //~ the type is statically known\n"
		"   void operator delete($Msg$* $nonnull$ msg, ::std::destroying_delete_t) {\n"
		"     SharedDtor(*msg);\n"
		"     $pbi$::SizedDelete(msg, sizeof($Msg$));\n"
		"   }\n"
		"#endif\n"
		"\n"
		"   //~ Templatize constexpr constructor as a workaround for a bug in\n"
		"   //~ gcc 12 (warning in gcc 13).\n"
		"   template <typename = void>\n"
		"   explicit constexpr $Msg$($pbi$::ConstantInitialized,\n"
		"                            const $pbi$::ClassData* $nonnull$\n"
		"                                class_data);\n"
		"\n"
		"   inline $Msg$(const $Msg$& from) : $Msg$(nullptr, from) {}\n"
		"   inline $Msg$($Msg$&& from) noexcept : $Msg$(nullptr, ::std::move(from)) {}\n"
		"   inline $Msg$& operator=(const $Msg$& from) {\n"
		"     CopyFrom(from);\n"
		"     return *this;\n"
		"   }\n"
		"   inline $Msg$& operator=($Msg$&& from) noexcept {\n"
		"     if (this == &from) return *this;\n"
		"     if ($pbi$::CanMoveWithInternalSwap(GetArena(), from.GetArena())) {\n"
		"       InternalSwap(&from);\n"
		"     } else {\n"
		"       CopyFrom(from);\n"
		"     }\n"
		"     return *this;\n"
		"   }\n"
		"   $decl_verify_func$;\n"
		"\n"
		"   $nodiscard $inline const $unknown_fields_type$& unknown_fields() const\n"
		"       ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"     $annotate_unknown_fields$;\n"
		"     return $unknown_fields$;\n"
		"   }\n"
		"   $nodiscard $inline $unknown_fields_type$* $nonnull$\n"
		"   mutable_unknown_fields() ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		"     $annotate_mutable_unknown_fields$;\n"
		"     return $mutable_unknown_fields$;\n"
		"   }\n"
		"\n"
		"   $descriptor_accessor$;\n"
		"   $get_descriptor$;\n"
		"   $nodiscard $static const $Msg$& default_instance() {\n"
		"     return *$pbi$::MessageGlobalsBase::ToDefaultInstance<$Msg$>(&$msg_globals$);\n"
		"   }\n"
		"   $decl_oneof$;\n"
		"   static constexpr int kIndexInFileMessages = $index_in_file_messages$;\n"
		"   $decl_any_methods$;\n"
		"   friend void swap($Msg$& a, $Msg$& b) { a.Swap(&b); }\n"
		"   inline void Swap($Msg$* $nonnull$ other) {\n"
		"     if (other == this) return;\n"
		"     if ($pbi$::CanUseInternalSwap(GetArena(), other->GetArena())) {\n"
		"       InternalSwap(other);\n"
		"     } else {\n"
		"       $pbi$::GenericSwap(this, other);\n"
		"     }\n"
		"   }\n"
		"   void UnsafeArenaSwap($Msg$* $nonnull$ other) {\n"
		"     if (other == this) return;\n"
		"     $DCHK$(GetArena() == other->GetArena());\n"
		"     InternalSwap(other);\n"
		"   }\n"
		"\n"
		"   // implements Message ----------------------------------------------\n"
		"\n"
		"   $nodiscard $$Msg$* $nonnull$\n"
		"   New($pb$::Arena* $nullable$ arena = nullptr) const {\n"
		"     return Super_::DefaultConstruct<$Msg$>(arena);\n"
		"   }\n"
		"   $generated_methods$;\n"
		"   $internal_field_number$;\n"
		"   $decl_non_simple_base$;\n"
		"  private:\n"
		"   static ::absl::string_view FullMessageName() { return \"$full_name$\"; }\n"
		"   $decl_annotate$;\n"
		"\n"
		"   explicit $Msg$($pb$::Arena* $nullable$ arena);\n"
		"   $Msg$($pb$::Arena* $nullable$ arena, const $Msg$& from);\n"
		"   $Msg$(\n"
		"       //~\n"
		"       $pb$::Arena* $nullable$ arena, $Msg$&& from) noexcept\n"
		"       : $Msg$(arena) {\n"
		"     *this = ::std::move(from);\n"
		"   }\n"
		"   $arena_dtor$;\n"
		"   const $pbi$::ClassData* $nonnull$ GetClassData() const PROTOBUF_FINAL;\n"
		"   static void* $nonnull$ PlacementNew_(\n"
		"       //~\n"
		"       const void* $nonnull$, void* $nonnull$ mem,\n"
		"       $pb$::Arena* $nullable$ arena);\n"
		"   static constexpr auto InternalNewImpl_();\n"
		"\n"
		"  public:\n"
		"   //~ We need this in the public section to call it from the initializer\n"
		"   //~ of T_class_data_. However, since it is `constexpr` and has an\n"
		"   //~ `auto` return type it is not callable from outside the .pb.cc\n"
		"   //~ without a definition so it is effectively private.\n"
		"   static constexpr auto InternalGenerateClassData_(\n"
		"       const MessageLite& prototype,\n"
		"       const $pbi$::TcParseTableBase* $nullable$ tc_table = nullptr);\n"
		"\n"
		"   $get_metadata$;\n"
		"   $decl_split_methods$;\n"
		"   // nested types ----------------------------------------------------\n"
		"   $nested_types$;\n"
		"   $nested_enums$;\n"
		"\n"
		"   // accessors -------------------------------------------------------\n"
		"   $decl_field_accessors$;\n"
		"   $decl_extension_ids$;\n"
		"   $proto2_message_sets$;\n"
		"   // @@protoc_insertion_point(class_scope:$full_name$)\n"
		"   //~ Generate private members.\n"
		"  private:\n"
		"   class _Internal;\n"
		"   $decl_set_has$;\n"
		"   $decl_oneof_has$;\n"
		"   $alias_parse_table_type$;\n"
		"   static constexpr ParseTableT_ InternalGenerateParseTable_(\n"
		"       const $pbi$::ClassData* $nonnull$ class_data);\n"
		"   $decl_data$;\n"
		"   $post_loop_handler$;\n"
		"\n"
		"   friend class $pb$::MessageLite;\n"
		"   friend class $pb$::Arena;\n"
		"   friend $pbi$::PrivateAccess;\n"
		"   template <typename T>\n"
		"   friend class $pb$::Arena::InternalHelper;\n"
		"   using InternalArenaConstructable_ = void;\n"
		"   using DestructorSkippable_ = void;\n"
		"   $decl_impl$;\n"
		"   $split_friend$;\n"
		"   //~ The TableStruct struct needs access to the private parts, in\n"
		"   //~ order to construct the offsets of all members.\n"
		"   friend struct ::$tablename$;\n"
		" };\n", SCBSUF("superclass", cd_superclass, g, ""), SCB("decl_dtor", cd_decl_dtor, g),
	     SCB("decl_annotate", mg_decl_annotate, g), SCB("decl_verify_func", mg_nothing_cb, NULL),
	     SCB("descriptor_accessor", cd_descriptor_accessor, g), SCB("get_descriptor", cd_get_descriptor, g),
	     SCB("decl_oneof", cd_decl_oneof, g), SINT("index_in_file_messages", g->index_in_file_messages),
	     SCB("decl_any_methods", cd_decl_any_methods, g), SCB("generated_methods", cd_generated_methods, g),
	     SCB("internal_field_number", cd_internal_field_number, g),
	     SCB("decl_non_simple_base", cd_decl_non_simple_base, g), SCB("arena_dtor", cd_arena_dtor, g),
	     SCB("get_metadata", cd_get_metadata, g), SCB("decl_split_methods", mg_nothing_cb, NULL),
	     SCB("nested_types", cd_nested_types, g), SCB("nested_enums", cd_nested_enums, g),
	     SCB("decl_field_accessors", cd_decl_field_accessors, g),
	     SCB("decl_extension_ids", cd_decl_extension_ids, g), SCB("proto2_message_sets", mg_nothing_cb, NULL),
	     SCB("decl_set_has", cd_decl_set_has, g), SCB("decl_oneof_has", cd_decl_oneof_has, g),
	     SCB("alias_parse_table_type", mg_alias_parse_table_type, g), SCB("decl_data", mg_parse_decls, g),
	     SCB("post_loop_handler", cd_post_loop_handler, g), SCB("decl_impl", cd_decl_impl, g),
	     S_("classdata_type", class_data_type(d, &g->options)),
	     S_("msg_globals", msg_globals_instance_name(d, &g->options)), SCB("split_friend", mg_nothing_cb, NULL));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void mg_generate_inline_methods(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (is_map_entry_message(d))
		return;
	Subs v = class_vars(d, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(d, &g->options);
	PUSH_SUBS(p, &t);
	mg_generate_field_accessor_definitions(g, p);
	for (int i = 0; i < d->n_real_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		EMIT(p, "\n"
		" inline $Msg$::$OneofName$ $Msg$::$oneof_name$() const {\n"
		"   return $Msg$::$OneofName$($oneof_case$[$oneof_index$]);\n"
		" }\n", SANN("oneof_name", xcat(oneof->name, "_case"), oneof),
		     S_("OneofName", xcat(underscores_to_camel_case(oneof->name, true), "Case")),
		     SINT("oneof_index", oneof->index));
	}
	POP_VARS(p);
	POP_VARS(p);
}

static void mg_generate_schema(MessageGenerator *g, LpPrinter *p, int offset)
{
	Subs v = class_vars(g->descriptor, &g->options);
	PUSH_SUBS(p, &v);
	EMIT(p, "\n"
		" {$offset$, sizeof($classtype$)},\n", SINT("offset", offset));
	POP_VARS(p);
}

static void mg_generate_verify(MessageGenerator *g, LpPrinter *p) {}
static void mg_generate_class_data(MessageGenerator *g, LpPrinter *p);
static void mg_generate_structors(MessageGenerator *g, LpPrinter *p);
static void mg_generate_oneof_clear(MessageGenerator *g, LpPrinter *p);
static void mg_generate_clear(MessageGenerator *g, LpPrinter *p);
static void mg_generate_serialize_with_cached_sizes_to_array(MessageGenerator *g, LpPrinter *p);
static void mg_generate_byte_size(MessageGenerator *g, LpPrinter *p);
static void mg_generate_class_specific_merge_impl(MessageGenerator *g, LpPrinter *p);
static void mg_generate_copy_from(MessageGenerator *g, LpPrinter *p);
static void mg_generate_is_initialized(MessageGenerator *g, LpPrinter *p);
static void mg_generate_swap(MessageGenerator *g, LpPrinter *p);

static void cm_annotate_accessors(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!g->options.annotate_accessor)
		return;
	for (int i = 0; i < g->descriptor->n_fields; i++)
		EMIT(p, "\n"
		" volatile bool $Msg$::$field$_AccessedNoStrip;\n", S_("field", field_name(&g->descriptor->fields[i])));
}

static void cm_verify(LpPrinter *p, void *arg) { mg_generate_verify(arg, p); }
static void cm_class_data(LpPrinter *p, void *arg) { mg_generate_class_data(arg, p); }

static void cm_any_field_descriptor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options))
		return;
	EMIT0(p, "\n"
		" $nodiscard $bool $Msg$::GetAnyFieldDescriptors(\n"
		"     const $pb$::Message& message,\n"
		"     const $pb$::FieldDescriptor** type_url_field,\n"
		"     const $pb$::FieldDescriptor** value_field) {\n"
		"   return ::_pbi::GetAnyFieldDescriptors(message, type_url_field, value_field);\n"
		" }\n");
}

static void cm_get_metadata(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_descriptor_methods(g->descriptor->file, &g->options))
		return;
	EMIT0(p, "\n"
		" $pb$::Metadata $Msg$::GetMetadata() const {\n"
		"   return Super_::GetMetadataImpl(GetClassData()->full());\n"
		" }\n");
}

static void cm_post_loop_handler(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!needs_post_loop_handler(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" const char* $nullable$ $Msg$::PostLoopHandler(\n"
		"     MessageLite* $nonnull$ msg,\n"
		"     const char* $nullable$ ptr,\n"
		"     ::_pbi::ParseContext* $nonnull$ ctx) {\n"
		"   $Msg$* _this = static_cast<$Msg$*>(msg);\n"
		"   $annotate_deserialize$;\n"
		"   $required$;\n"
		"   return ptr;\n"
		" }\n", SCB("required", mg_nothing_cb, NULL));
}

static void cm_tracker_decl(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_tracker(g->descriptor, &g->options))
		return;
	EMIT0(p, "\n"
		" $pb$::AccessListener<$classtype$> $Msg$::$tracker$(\n"
		"     &FullMessageName);\n");
}

static void mg_generate_class_methods(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (!should_generate_class(d, &g->options))
		return;
	Subs v = class_vars(d, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(d, &g->options);
	PUSH_SUBS(p, &t);
	if (is_map_entry_message(d)) {
		EMIT(p, "\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" $Msg$::$Msg$() : Super_($Msg$_get_class_data()) {}\n"
		" $Msg$::$Msg$($pb$::Arena* $nullable$ arena)\n"
		"     : Super_(arena, $Msg$_get_class_data()) {}\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" $Msg$::$Msg$() : Super_() {}\n"
		" $Msg$::$Msg$($pb$::Arena* $nullable$ arena) : Super_(arena) {}\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		" $annotate_accessors$;\n"
		" $verify$;\n"
		" $class_data$;\n", SCB("annotate_accessors", cm_annotate_accessors, g), SCB("verify", cm_verify, g),
		     SCB("class_data", cm_class_data, g));
		pfg_generate_data_definitions(g->parse_function_generator, p);
		POP_VARS(p);
		POP_VARS(p);
		return;
	}
	if (is_any_message(d))
		EMIT(p, "\n"
		" $any_field_descriptor$;\n"
		" $nodiscard $bool $Msg$::ParseAnyTypeUrl(\n"
		"     //~\n"
		"     ::absl::string_view type_url,\n"
		"     std::string* $nonnull$ full_type_name) {\n"
		"   return ::_pbi::ParseAnyTypeUrl(type_url, full_type_name);\n"
		" }\n", SCB("any_field_descriptor", cm_any_field_descriptor, g));
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *f = &d->fields[i];
		Subs fv = {0};
		field_vars(&fv, f, &g->options);
		PUSH_SUBS(p, &fv);
		Subs ft = make_tracker_calls_field(f, &g->options);
		PUSH_SUBS(p, &ft);
		fg_generate_non_inline_accessor_definitions(fg_get(&g->field_generators, f), p);
		if (is_cross_file_maybe_map(f))
			mg_generate_field_clear(g, f, false, p);
		POP_VARS(p);
		POP_VARS(p);
	}
	mg_generate_structors(g, p);
	EMIT0(p, "\n");
	if (d->n_real_oneofs > 0) {
		mg_generate_oneof_clear(g, p);
		EMIT0(p, "\n");
	}
	mg_generate_class_data(g, p);
	pfg_generate_data_definitions(g->parse_function_generator, p);
	if (has_generated_methods(d->file, &g->options)) {
		mg_generate_clear(g, p);
		EMIT0(p, "\n");
		mg_generate_serialize_with_cached_sizes_to_array(g, p);
		EMIT0(p, "\n");
		mg_generate_byte_size(g, p);
		EMIT0(p, "\n");
		mg_generate_class_specific_merge_impl(g, p);
		EMIT0(p, "\n");
		mg_generate_copy_from(g, p);
		EMIT0(p, "\n");
		mg_generate_is_initialized(g, p);
		EMIT0(p, "\n");
	}
	mg_generate_verify(g, p);
	mg_generate_swap(g, p);
	EMIT0(p, "\n");
	EMIT(p, "\n"
		" $annotate_accessor_definition$;\n"
		" $get_metadata$;\n"
		" $post_loop_handler$;\n"
		" $message_set_definition$;\n"
		" $tracker_decl$;\n", SCB("annotate_accessor_definition", cm_annotate_accessors, g),
	     SCB("get_metadata", cm_get_metadata, g), SCB("post_loop_handler", cm_post_loop_handler, g),
	     SCB("message_set_definition", mg_nothing_cb, NULL), SCB("tracker_decl", cm_tracker_decl, g));
	POP_VARS(p);
	POP_VARS(p);
}

static size_t mg_generate_offsets(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	Subs v = class_vars(d, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(d, &g->options);
	PUSH_SUBS(p, &t);
	bool has_has_bits = layout_has_hasbits(&g->field_layout) || is_map_entry_message(d);
	bool has_extensions = d->n_extension_ranges > 0;
	bool has_oneofs = d->n_real_oneofs > 0;
	bool has_weak_fields = g->num_weak_fields > 0;
	bool has_inline_strings = false;
	bool has_split = should_split_message(d, &g->options);
	bool bits[] = {has_has_bits, has_extensions, has_oneofs, has_weak_fields, has_inline_strings,
		       has_split, has_split, has_has_bits, has_inline_strings};
	int num_generated_indices = 1;
	uint32_t res = 0;
	for (int i = 0; i < 9; i++) {
		res |= (uint32_t)bits[i] << i;
		num_generated_indices += bits[i];
	}
	FORMAT(p, NULL, "$1$, // bitmap\n", xfmt("0x%03x", res));
	if (has_has_bits)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, $has_bits$),\n");
	if (has_extensions)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, $extensions$),\n");
	if (has_oneofs)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, $oneof_case$[0]),\n");
	if (has_weak_fields)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, $weak_field_map$),\n");
	if (has_split)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, $split$),\nsizeof($classtype$::Impl_::Split),\n");
	size_t offsets = (size_t)num_generated_indices + (size_t)d->n_fields + (size_t)d->n_real_oneofs;
	if (has_has_bits)
		FORMAT(p, NULL, "$1$, // hasbit index offset\n", cpp_utoa(offsets));
	size_t entries = offsets;
	for (int i = 0; i < d->n_fields; i++) {
		const LpField *f = &d->fields[i];
		if (is_weak_option(f))
			FORMAT(p, NULL, "::_pbi::kInvalidFieldOffsetTag");
		else if (real_oneof(f))
			FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, _impl_.$1$_)", real_oneof(f)->name);
		else
			FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$$1$, $2$)",
			       should_split(f, &g->options) ? "::Impl_::Split" : "",
			       should_split(f, &g->options) ? xcat(field_name(f), "_") : field_member_name(f, false));
		if (should_split(f, &g->options))
			FORMAT(p, NULL, " | ::_pbi::kSplitFieldOffsetTag");
		if (is_eagerly_verified_lazy(f, &g->options))
			FORMAT(p, NULL, " | ::_pbi::kLazyOffsetTag");
		else if (is_string_inlined(f, &g->options))
			FORMAT(p, NULL, " | ::_pbi::kInlinedOffsetTag");
		else if (is_micro_string(f, &g->options))
			FORMAT(p, NULL, " | ::_pbi::kMicroStringOffsetTag");
		FORMAT(p, NULL, ",\n");
	}
	for (int i = 0; i < d->n_real_oneofs; i++)
		FORMAT(p, NULL, "PROTOBUF_FIELD_OFFSET($classtype$, _impl_.$1$_),\n", d->oneofs[i].name);
	if (is_map_entry_message(d)) {
		entries += 2;
		FORMAT(p, NULL, "0,\n1,\n");
	} else if (layout_has_hasbits(&g->field_layout)) {
		layout_print_has_bit_indices_for_schema(&g->field_layout, p, &entries);
	}
	POP_VARS(p);
	POP_VARS(p);
	return entries;
}

static void mg_emit_pending_zero_fields(const MessageGenerator *g, LpPrinter *p, const LpField **first,
					const LpField *last)
{
	if (!*first)
		return;
	if (*first != last || !fg_get(&g->field_generators, *first)->has_brace_default_assign)
		EMIT(p, "\n"
		" ::memset(reinterpret_cast<char*>(&$impl$) +\n"
		"              offsetof($Impl$, $first$_),\n"
		"          0,\n"
		"          offsetof($Impl$, $last$_) -\n"
		"              offsetof($Impl$, $first$_) +\n"
		"              sizeof($Impl$::$last$_));\n", S_("first", field_name(*first)), S_("last", field_name(last)), S_("Impl", "Impl_"),
		     S_("impl", "_impl_"));
	else
		EMIT(p, "\n"
		" $field_$ = {};\n", S_("field_", field_member_name(*first, false)));
	*first = NULL;
}

static void mg_generate_zero_init_fields(const MessageGenerator *g, LpPrinter *p)
{
	const LpField *first = NULL;
	const FieldLayout *l = &g->field_layout;
	int i = 0;
	for (; i < l->n_fields && !should_split(l->fields[i], &g->options); ++i) {
		if (fg_get(&g->field_generators, l->fields[i])->has_trivial_zero_default) {
			if (!first)
				first = l->fields[i];
		} else {
			mg_emit_pending_zero_fields(g, p, &first, i > 0 ? l->fields[i - 1] : NULL);
		}
	}
	mg_emit_pending_zero_fields(g, p, &first, i > 0 ? l->fields[i - 1] : NULL);
}

/* MemberInitSeparator */
typedef struct {
	LpPrinter *p;
	bool separators;
} MemberInitSeparator;

static void member_init_separator(MemberInitSeparator *s)
{
	if (s->separators) {
		EMIT0(s->p, ",\n");
	} else {
		EMIT0(s->p, ": ");
		lp_indent(s->p);
		s->separators = true;
	}
}

typedef enum { INIT_CONSTEXPR, INIT_ARENA, INIT_ARENA_COPY } InitType;

static void mg_generate_impl_member_init(MessageGenerator *g, LpPrinter *p, InitType init_type)
{
	const LpMessage *d = g->descriptor;
	lp_indent(p);
	MemberInitSeparator sep = {p, false};
	if (d->n_extension_ranges > 0 && init_type != INIT_CONSTEXPR) {
		member_init_separator(&sep);
		EMIT0(p, "\n"
		" _extensions_ {}\n");
	}
	if (layout_has_hasbits(&g->field_layout) && init_type == INIT_ARENA_COPY) {
		member_init_separator(&sep);
		EMIT0(p, "_has_bits_{from._has_bits_}");
	}
	for (int i = 0; i < g->field_layout.n_fields; i++) {
		const LpField *f = g->field_layout.fields[i];
		if (should_split(f, &g->options))
			continue;
		const FieldGenerator *gen = fg_get(&g->field_generators, f);
		switch (init_type) {
		case INIT_CONSTEXPR:
			member_init_separator(&sep);
			fg_generate_member_constexpr_constructor(gen, p);
			break;
		case INIT_ARENA:
			if (!gen->has_trivial_zero_default) {
				member_init_separator(&sep);
				fg_generate_member_constructor(gen, p);
			}
			break;
		case INIT_ARENA_COPY:
			if (!gen->has_trivial_value) {
				member_init_separator(&sep);
				fg_generate_member_copy_constructor(gen, p);
			}
			break;
		}
	}
	for (int i = 0; i < d->n_real_oneofs; i++) {
		member_init_separator(&sep);
		EMIT(p, "$name$_{}", S_("name", d->oneofs[i].name));
	}
	if (d->n_real_oneofs) {
		member_init_separator(&sep);
		if (init_type == INIT_ARENA_COPY) {
			Buf cases = {0};
			for (int i = 0; i < d->n_real_oneofs; ++i)
				buf_adds(&cases, xcat(i ? ", " : "", "from._oneof_case_[", cpp_itoa(i), "]"));
			EMIT(p, "_oneof_case_{$cases$}", S_("cases", xstrdup(buf_str(&cases))));
		} else {
			EMIT0(p, "_oneof_case_{}");
		}
	}
	if (g->num_weak_fields && init_type != INIT_CONSTEXPR) {
		member_init_separator(&sep);
		if (init_type == INIT_ARENA_COPY)
			EMIT0(p, "_weak_field_map_{visibility, arena, from._weak_field_map_}");
		else
			EMIT0(p, "_weak_field_map_{visibility, arena}");
	}
	if (sep.separators)
		lp_outdent(p);
	lp_outdent(p);
}

static void sc_init_impl(LpPrinter *p, void *arg) { mg_generate_impl_member_init(arg, p, INIT_ARENA); }
static void sc_zero_init(LpPrinter *p, void *arg) { mg_generate_zero_init_fields(arg, p); }

static void mg_generate_shared_constructor_code(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" PROTOBUF_NDEBUG_INLINE $Msg$::Impl_::Impl_(\n"
		"     [[maybe_unused]] $pbi$::InternalVisibility visibility,\n"
		"     [[maybe_unused]] $pb$::Arena* $nullable$ arena)\n"
		"     //~\n"
		"     $init_impl$ {}\n"
		"\n"
		" inline void $Msg$::SharedCtor(::_pb::Arena* $nullable$ arena) {\n"
		"   new (&_impl_) Impl_(internal_visibility(), arena);\n"
		"   $zero_init$;\n"
		" }\n", SCB("init_impl", sc_init_impl, g), SCB("zero_init", sc_zero_init, g));
}

static void sd_has_bit_consistency(LpPrinter *p, void *arg) { mg_generate_check_has_bit_consistency(arg, p, "this_."); }

static void sd_field_dtors(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->field_layout.n_fields; i++) {
		const LpField *f = g->field_layout.fields[i];
		if (should_split(f, &g->options))
			continue;
		fg_generate_destructor_code(fg_get(&g->field_generators, f), p);
	}
}

static void sd_oneof_field_dtors(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++)
		EMIT(p, "\n"
		" if (this_.has_$name$()) {\n"
		"   this_.clear_$name$();\n"
		" }\n", S_("name", g->descriptor->oneofs[i].name));
}

static void sd_weak_fields_dtor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->num_weak_fields == 0)
		return;
	EMIT0(p, "\n"
		" this_.$weak_field_map$.ClearAll();\n");
}

static void sd_impl_dtor(LpPrinter *p, void *arg) { EMIT0(p, "this_._impl_.~Impl_();\n"); }

static void mg_generate_shared_destructor_code(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" inline void $Msg$::SharedDtor(MessageLite& self) {\n"
		"   $Msg$& this_ = static_cast<$Msg$&>(self);\n"
		"   $has_bit_consistency$;\n"
		"   this_._internal_metadata_.Delete<$unknown_fields_type$>();\n"
		"   $DCHK$(this_.GetArena() == nullptr);\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $field_dtors$;\n"
		"   $split_field_dtors$;\n"
		"   $oneof_field_dtors$;\n"
		"   $weak_fields_dtor$;\n"
		"   $impl_dtor$;\n"
		" }\n", SCB("has_bit_consistency", sd_has_bit_consistency, g),
	     SCB("field_dtors", sd_field_dtors, g), SCB("split_field_dtors", mg_nothing_cb, NULL),
	     SCB("oneof_field_dtors", sd_oneof_field_dtors, g), SCB("weak_fields_dtor", sd_weak_fields_dtor, g),
	     SCB("impl_dtor", sd_impl_dtor, NULL));
}

static ArenaDtorNeeds mg_needs_arena_destructor(const MessageGenerator *g)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return ARENA_DTOR_NONE;
	ArenaDtorNeeds needs = ARENA_DTOR_NONE;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		ArenaDtorNeeds n = fg_needs_arena_destructor(&g->field_generators.fields[i]);
		if (n > needs)
			needs = n;
	}
	return needs;
}

static void ad_field_dtors(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->field_layout.n_fields; i++) {
		const LpField *f = g->field_layout.fields[i];
		if (should_split(f, &g->options))
			continue;
		fg_generate_arena_destructor_code(fg_get(&g->field_generators, f), p);
	}
}

static void ad_oneof_field_dtors(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		const LpOneof *oneof = &g->descriptor->oneofs[i];
		for (int j = 0; j < oneof->n_fields; j++)
			fg_generate_arena_destructor_code(fg_get(&g->field_generators, proto_oneof_field(oneof, j)), p);
	}
}

static void mg_generate_arena_destructor_code(MessageGenerator *g, LpPrinter *p)
{
	EMIT(p, "\n"
		" void $Msg$::ArenaDtor(void* $nonnull$ object) {\n"
		"   $Msg$* _this = reinterpret_cast<$Msg$*>(object);\n"
		"   $field_dtors$;\n"
		"   $split_field_dtors$;\n"
		"   $oneof_field_dtors$;\n"
		" }\n", SCB("field_dtors", ad_field_dtors, g), SCB("split_field_dtors", mg_nothing_cb, NULL),
	     SCB("oneof_field_dtors", ad_oneof_field_dtors, g));
}

static void cc_init(LpPrinter *p, void *arg) { mg_generate_impl_member_init(arg, p, INIT_CONSTEXPR); }

static void mg_generate_constexpr_constructor(MessageGenerator *g, LpPrinter *p)
{
	if (is_map_entry_message(g->descriptor) || !has_impl_data(g->descriptor, &g->options)) {
		EMIT0(p, "\n"
		" //~ Templatize constexpr constructor as a workaround for a bug in\n"
		" //~ gcc 12 (warning in gcc 13).\n"
		" template <typename>\n"
		" constexpr $Msg$::$Msg$(::_pbi::ConstantInitialized,\n"
		"                        const ::_pbi::ClassData* $nonnull$ class_data)\n"
		"     : Super_(\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"           class_data\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"       ) {\n"
		" }\n");
		return;
	}
	EMIT0(p, "\n");
	EMIT(p, "\n"
		" inline constexpr $Msg$::Impl_::Impl_(\n"
		"     [[maybe_unused]] $pbi$::InternalVisibility visibility,\n"
		"     ::_pbi::ConstantInitialized) noexcept\n"
		"     //~\n"
		"     $init$ {}\n", SCB("init", cc_init, g));
	EMIT0(p, "\n");
	EMIT0(p, "\n"
		" template <typename>\n"
		" constexpr $Msg$::$Msg$(::_pbi::ConstantInitialized,\n"
		"                        const ::_pbi::ClassData* $nonnull$ class_data)\n"
		"     : Super_(\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"           class_data\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"           ),\n"
		"       _impl_(internal_visibility(), ::_pbi::ConstantInitialized()) {\n"
		" }\n");
}

static bool mg_can_use_trivial_copy(const MessageGenerator *g)
{
	if (should_split_message(g->descriptor, &g->options) || has_simple_base_class(g->descriptor, &g->options) ||
	    g->descriptor->n_extension_ranges > 0 || g->num_weak_fields > 0)
		return false;
	for (int i = 0; i < g->descriptor->n_fields; i++)
		if (!fg_has_trivial_copy(&g->field_generators.fields[i]))
			return false;
	return true;
}

typedef struct {
	const MessageGenerator *g;
	const LpField *first;
	int has_bit_word_index;
	const LpField *field;
	const LpOneof *oneof;
} CopyInitCtx;

static void ci_emit_pending_copy_fields(CopyInitCtx *c, LpPrinter *p, const LpField *last)
{
	if (!c->first)
		return;
	if (c->first != last)
		EMIT(p, "\n"
		" ::memcpy(reinterpret_cast<char*>(&_impl_) +\n"
		"              offsetof(Impl_, $first$_),\n"
		"          reinterpret_cast<const char*>(&from._impl_) +\n"
		"              offsetof(Impl_, $first$_),\n"
		"          offsetof(Impl_, $last$_) -\n"
		"              offsetof(Impl_, $first$_) +\n"
		"              sizeof(Impl_::$last$_));\n", S_("first", field_name(c->first)), S_("last", field_name(last)));
	else
		EMIT(p, "\n"
		" $field_$ = from.$field_$;\n", S_("field_", field_member_name(c->first, false)));
	c->first = NULL;
}

static void ci_has_msg(LpPrinter *p, void *arg)
{
	CopyInitCtx *c = arg;
	if (!layout_has_hasbits(&c->g->field_layout))
		EMIT0(p, "from.$field_$ != nullptr");
	else
		EMIT(p, "$condition$",
		     S_("condition", generate_condition_for_field(layout_has_bit_index(&c->g->field_layout, c->field), c->field,
								  &c->g->options)));
}

static void ci_emit_copy_message(CopyInitCtx *c, LpPrinter *p, const LpField *f)
{
	if (layout_has_hasbits(&c->g->field_layout)) {
		int has_word_index = layout_has_word_index(&c->g->field_layout, f);
		if (c->has_bit_word_index != has_word_index) {
			EMIT(p, "$declare$cached_has_bits = _impl_._has_bits_[$index$];\n",
			     S_("declare", c->has_bit_word_index < 0 ? "::uint32_t " : ""), SINT("index", has_word_index));
			c->has_bit_word_index = has_word_index;
		}
	}
	c->field = f;
	EMIT(p, "\n"
		" $field_$ = ($has_msg$)\n"
		"                ? Super_::CopyConstruct(arena, *from.$field_$)\n"
		"                : nullptr;\n", SCB("has_msg", ci_has_msg, c), S_("submsg", field_message_type_name(f, &c->g->options)));
}

static void ci_copy_fields(LpPrinter *p, void *arg)
{
	CopyInitCtx *c = arg;
	const FieldLayout *l = &c->g->field_layout;
	for (int i = 0; i < l->n_fields; i++) {
		const LpField *f = l->fields[i];
		const FieldGenerator *gen = fg_get(&c->g->field_generators, f);
		Subs v = {0};
		field_vars(&v, f, &c->g->options);
		PUSH_SUBS(p, &v);
		const LpField *prev = i > 0 ? l->fields[i - 1] : NULL;
		if (!gen->has_trivial_value || gen->should_split) {
			ci_emit_pending_copy_fields(c, p, prev);
		} else if (gen->is_message) {
			ci_emit_pending_copy_fields(c, p, prev);
			ci_emit_copy_message(c, p, f);
		} else if (!c->first) {
			c->first = f;
		}
		POP_VARS(p);
	}
	ci_emit_pending_copy_fields(c, p, l->n_fields ? l->fields[l->n_fields - 1] : NULL);
}

static void ci_oneof_case_body(LpPrinter *p, void *arg)
{
	CopyInitCtx *c = arg;
	fg_generate_oneof_copy_construct(fg_get(&c->g->field_generators, c->field), p);
}

static void ci_oneof_cases(LpPrinter *p, void *arg)
{
	CopyInitCtx *c = arg;
	for (int i = 0; i < c->oneof->n_fields; i++) {
		const LpField *f = proto_oneof_field(c->oneof, i);
		c->field = f;
		EMIT(p, "\n"
		" case k$Name$:\n"
		"   $body$;\n"
		"   break;\n", S_("Name", underscores_to_camel_case(f->name, true)),
		     SCB("body", ci_oneof_case_body, c));
	}
}

static void ci_copy_oneof_fields(LpPrinter *p, void *arg)
{
	CopyInitCtx *c = arg;
	const LpMessage *d = c->g->descriptor;
	for (int i = 0; i < d->n_real_oneofs; i++) {
		c->oneof = &d->oneofs[i];
		EMIT(p, "\n"
		" switch ($name$_case()) {\n"
		"   case $NAME$_NOT_SET:\n"
		"     break;\n"
		"     $cases$;\n"
		" }\n", S_("name", c->oneof->name), S_("NAME", ascii_upper(c->oneof->name)),
		     SCB("cases", ci_oneof_cases, c));
	}
}

static void mg_generate_copy_init_fields(const MessageGenerator *g, LpPrinter *p)
{
	CopyInitCtx c = {.g = g, .has_bit_word_index = -1};
	if (g->descriptor->n_extension_ranges > 0)
		EMIT0(p, "\n"
		" _impl_._extensions_.MergeFrom(arena, this, from._impl_._extensions_,\n"
		"                               from.GetArena());\n");
	EMIT(p, "\n"
		" $copy_fields$;\n"
		" $copy_oneof_fields$;\n", SCB("copy_fields", ci_copy_fields, &c), SCB("copy_oneof_fields", ci_copy_oneof_fields, &c));
}

static void cc_init_copy(LpPrinter *p, void *arg) { mg_generate_impl_member_init(arg, p, INIT_ARENA_COPY); }

static void ac_copy_construct_impl(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!has_simple_base_class(g->descriptor, &g->options))
		EMIT0(p, "\n"
		" new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);\n");
}

static void ac_copy_init_fields(LpPrinter *p, void *arg) { mg_generate_copy_init_fields(arg, p); }

static void ac_maybe_register_arena_dtor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (mg_needs_arena_destructor(g) == ARENA_DTOR_REQUIRED)
		EMIT0(p, "\n"
		" if (arena != nullptr) {\n"
		"   arena->OwnCustomDestructor(this, &$Msg$::ArenaDtor);\n"
		" }\n");
}

static void mg_generate_arena_enabled_copy_constructor(MessageGenerator *g, LpPrinter *p)
{
	if (!has_simple_base_class(g->descriptor, &g->options)) {
		EMIT(p, "\n"
		" PROTOBUF_NDEBUG_INLINE $Msg$::Impl_::Impl_(\n"
		"     [[maybe_unused]] $pbi$::InternalVisibility visibility,\n"
		"     [[maybe_unused]] $pb$::Arena* $nullable$ arena, const Impl_& from,\n"
		"     [[maybe_unused]] const $classtype$& from_msg)\n"
		"     //~\n"
		"     $init$ {}\n", SCB("init", cc_init_copy, g));
		EMIT0(p, "\n");
	}
	EMIT(p, "\n"
		" $Msg$::$Msg$(\n"
		"     //~ force alignment\n"
		"     $pb$::Arena* $nullable$ arena,\n"
		"     //~ force alignment\n"
		"     const $Msg$& from)\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"     : Super_(arena, $Msg$_get_class_data()) {\n"
		"\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		"     : Super_(arena) {\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $Msg$* const _this = this;\n"
		"   (void)_this;\n"
		"   _internal_metadata_.MergeFrom<$unknown_fields_type$>(\n"
		"       from._internal_metadata_);\n"
		"   $copy_construct_impl$;\n"
		"   $copy_init_fields$;\n"
		"   $force_allocation$;\n"
		"   $maybe_register_arena_dtor$;\n"
		"\n"
		"   // @@protoc_insertion_point(copy_constructor:$full_name$)\n"
		" }\n", SCB("copy_construct_impl", ac_copy_construct_impl, g),
	     SCB("copy_init_fields", ac_copy_init_fields, g), SCB("force_allocation", mg_nothing_cb, NULL),
	     SCB("maybe_register_arena_dtor", ac_maybe_register_arena_dtor, g));
}

static void st_ctor_body(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT0(p, "SharedCtor(arena);");
	if (mg_needs_arena_destructor(g) == ARENA_DTOR_REQUIRED)
		EMIT0(p, "\n"
		" if (arena != nullptr) {\n"
		"   arena->OwnCustomDestructor(this, &$Msg$::ArenaDtor);\n"
		" }\n");
}

static void mg_generate_structors(MessageGenerator *g, LpPrinter *p)
{
	EMIT(p, "\n"
		" $Msg$::$Msg$($pb$::Arena* $nullable$ arena)\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"     : Super_(arena, $Msg$_get_class_data()) {\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		"     : Super_(arena) {\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $ctor_body$;\n"
		"   // @@protoc_insertion_point(arena_constructor:$full_name$)\n"
		" }\n", SCB("ctor_body", st_ctor_body, g));
	if (using_implicit_weak_fields(g->descriptor->file, &g->options))
		EMIT0(p, "\n"
		" $Msg$::$Msg$(\n"
		"     //~ Force alignment\n"
		"     $pb$::Arena* $nullable$ arena, const $Msg$& from)\n"
		"     : $Msg$(arena) {\n"
		"   MergeFrom(from);\n"
		" }\n");
	else if (mg_can_use_trivial_copy(g))
		EMIT0(p, "\n"
		" $Msg$::$Msg$(\n"
		"     //~ Force alignment\n"
		"     $pb$::Arena* $nullable$ arena, const $Msg$& from)\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"     : Super_(arena, $Msg$_get_class_data()),\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		"     : Super_(arena),\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"       _impl_(from._impl_) {\n"
		"   _internal_metadata_.MergeFrom<$unknown_fields_type$>(\n"
		"       from._internal_metadata_);\n"
		" }\n");
	else
		mg_generate_arena_enabled_copy_constructor(g, p);
	mg_generate_shared_constructor_code(g, p);
	if (!has_simple_base_class(g->descriptor, &g->options))
		EMIT0(p, "\n"
		" $Msg$::~$Msg$() {\n"
		"   // @@protoc_insertion_point(destructor:$full_name$)\n"
		"   SharedDtor(*this);\n"
		" }\n");
	mg_generate_shared_destructor_code(g, p);
	if (mg_needs_arena_destructor(g) > ARENA_DTOR_NONE)
		mg_generate_arena_destructor_code(g, p);
}

static void mg_generate_source_in_proto2_namespace(MessageGenerator *g, LpPrinter *p)
{
	Subs v = class_vars(g->descriptor, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(g->descriptor, &g->options);
	PUSH_SUBS(p, &t);
	if (should_generate_extern_specializations(&g->options) && should_generate_class(g->descriptor, &g->options)) {
		EMIT0(p, "\n"
		" template void* $nonnull$ Arena::DefaultConstruct<$classtype$>(Arena* $nullable$);\n");
		if (!is_map_entry_message(g->descriptor))
			EMIT0(p, "\n"
		" template void* $nonnull$ Arena::CopyConstruct<$classtype$>(Arena* $nullable$, const void* $nonnull$);\n");
	}
	POP_VARS(p);
	POP_VARS(p);
}

typedef struct {
	const MessageGenerator *g;
	int chunk_count;
	bool merge_zero_init;
	bool is_split;
} ClearChunkPred;

static bool clear_chunk_equiv(const LpField *a, const LpField *b, const void *arg)
{
	ClearChunkPred *c = (ClearChunkPred *)arg;
	const Options *o = &c->g->options;
	c->chunk_count++;
	bool same = layout_has_byte_index(&c->g->field_layout, a) == layout_has_byte_index(&c->g->field_layout, b) &&
		    is_likely_present(a, o) == is_likely_present(b, o) &&
		    (can_clear_by_zeroing(a) == can_clear_by_zeroing(b) ||
		     (can_clear_by_zeroing(a) && (c->chunk_count == 1 || c->merge_zero_init)));
	if (!same)
		c->chunk_count = 0;
	return same;
}

static bool split_filter(const LpField *f, const void *arg)
{
	const ClearChunkPred *c = arg;
	return should_split(f, &c->g->options) == c->is_split;
}

static bool chunk_equal_group(const FieldChunk *a, const FieldChunk *b) { return may_group_chunks_for_haswords_check(a, b); }

typedef struct {
	const MessageGenerator *g;
	const FieldChunk *chunk;
	const LpField *memset_start, *memset_end;
	bool is_split;
	int *cached_has_word_index;
	const LpField *field;
} ClearFieldCtx;

static void clear_maybe_update_cached_hasbits(LpPrinter *p, int *cached_has_word_index, int has_word_index)
{
	if (*cached_has_word_index != has_word_index) {
		*cached_has_word_index = has_word_index;
		EMIT(p, "\n"
		" cached_has_bits = this_.$has_bits$[$cached_has_word_index$];\n", SINT("cached_has_word_index", has_word_index));
	}
}

static void cf_cond(LpPrinter *p, void *arg)
{
	const ClearFieldCtx *c = arg;
	print_presence_check_condition(c->field, &c->g->field_layout, p, &c->g->options);
}

static void cf_clear_field(LpPrinter *p, void *arg)
{
	const ClearFieldCtx *c = arg;
	fg_generate_message_clearing_code(fg_get(&c->g->field_generators, c->field), p);
}

static void cf_print_clear_field_chunk(LpPrinter *p, void *arg)
{
	ClearFieldCtx *c = arg;
	const MessageGenerator *g = c->g;
	if (c->memset_start) {
		if (c->memset_start == c->memset_end)
			fg_generate_message_clearing_code(fg_get(&g->field_generators, c->memset_start), p);
		else
			EMIT(p, "\n"
		" ::memset(&this_.$start$, 0,\n"
		"          static_cast<::size_t>(\n"
		"              reinterpret_cast<char*>(&this_.$end$) -\n"
		"              reinterpret_cast<char*>(&this_.$start$)) +\n"
		"              sizeof($end$));\n", S_("start", field_member_name(c->memset_start, c->is_split)),
			     S_("end", field_member_name(c->memset_end, c->is_split)));
	}
	for (int i = 0; i < c->chunk->n; i++) {
		const LpField *f = c->chunk->fields[i];
		if (can_clear_by_zeroing(f))
			continue;
		c->field = f;
		if (mg_should_generate_enclosing_if(g, f)) {
			int has_word_index = layout_has_word_index(&g->field_layout, f);
			if (has_word_index >= 0)
				clear_maybe_update_cached_hasbits(p, c->cached_has_word_index, has_word_index);
			EMIT(p, "\n"
		" if ($cond$) {\n"
		"   $clear_field$;\n"
		" }\n", SCB("cond", cf_cond, c), SCB("clear_field", cf_clear_field, c));
		} else {
			cf_clear_field(p, c);
		}
	}
}

static void mg_emit_clear_chunks(MessageGenerator *g, LpPrinter *p, bool is_split)
{
	const int kMaxUnconditionalPrimitiveBytesClear = 4;
	const FieldLayout *l = &g->field_layout;
	int zero_init_bytes = 0;
	for (int i = 0; i < l->n_fields; i++)
		if (can_clear_by_zeroing(l->fields[i]))
			zero_init_bytes += estimate_alignment_size(l->fields[i]);
	ClearChunkPred pred = {g, 0, zero_init_bytes > kMaxUnconditionalPrimitiveBytesClear, is_split};
	FieldChunks chunks = collect_fields(l->fields, l->n_fields, &g->options, clear_chunk_equiv, split_filter, &pred);
	int it = 0, end = chunks.n;
	int cached_has_word_index = -1;
	while (it != end) {
		int next = find_next_unequal_chunk(&chunks, it, end, chunk_equal_group);
		bool has_haswords_check = maybe_emit_haswords_check(&chunks, it, next, &g->options);
		while (it != next) {
			const FieldChunk *chunk = &chunks.v[it];
			ClearFieldCtx c = {.g = g, .chunk = chunk, .is_split = is_split, .cached_has_word_index = &cached_has_word_index};
			bool saw_non_zero_init = false;
			for (int i = 0; i < chunk->n; i++) {
				const LpField *f = chunk->fields[i];
				if (can_clear_by_zeroing(f)) {
					if (saw_non_zero_init)
						fail("Check failed: !saw_non_zero_init");
					if (!c.memset_start)
						c.memset_start = f;
					c.memset_end = f;
				} else {
					saw_non_zero_init = true;
				}
			}
			bool check_has_byte = layout_has_bit_index(l, chunk->fields[0]) >= 0 && chunk->n > 1 &&
					      !is_likely_present(chunk->fields[chunk->n - 1], &g->options) &&
					      (c.memset_end != chunk->fields[chunk->n - 1] || pred.merge_zero_init);
			if (check_has_byte) {
				uint32_t chunk_mask = gen_chunk_mask_fields(chunk->fields, chunk->n, l);
				clear_maybe_update_cached_hasbits(p, &cached_has_word_index,
								  layout_has_word_index(l, chunk->fields[0]));
				EMIT(p, "\n"
		" if ($cond$) {\n"
		"   $clear_field_chunk$;\n"
		" }\n", S_("cond", generate_condition_for_group(chunk_mask, &g->options)),
				     SCB("clear_field_chunk", cf_print_clear_field_chunk, &c));
			} else {
				cf_print_clear_field_chunk(p, &c);
			}
			++it;
		}
		if (has_haswords_check) {
			lp_outdent(p);
			EMIT0(p, "\n"
		" }\n");
			cached_has_word_index = -1;
		}
	}
}

static void gc_maybe_clear_extensions(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->descriptor->n_extension_ranges > 0)
		EMIT0(p, "$extensions$.Clear();");
}

static void gc_clear_fields(LpPrinter *p, void *arg) { mg_emit_clear_chunks(arg, p, false); }

static void gc_clear_oneofs(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++)
		EMIT(p, "\n"
		" clear_$name$();\n", S_("name", g->descriptor->oneofs[i].name));
}

static void gc_maybe_clear_weak_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->num_weak_fields)
		EMIT0(p, "\n"
		" $weak_field_map$.ClearAll();\n");
}

static void gc_maybe_clear_hasbits(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (layout_has_hasbits(&g->field_layout))
		EMIT0(p, "\n"
		" $has_bits$.Clear();\n");
}

static void mg_generate_clear(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" PROTOBUF_NOINLINE void $Msg$::Clear() {\n"
		"   auto& this_ [[maybe_unused]] = *this;\n"
		"   // @@protoc_insertion_point(message_clear_start:$full_name$)\n"
		"   $pbi$::TSanWrite(&_impl_);\n"
		"   ::uint32_t cached_has_bits [[maybe_unused]] = 0;\n"
		"\n"
		"   $maybe_clear_extensions$;\n"
		"   $clear_fields$;\n"
		"   $clear_oneofs$;\n"
		"   $maybe_clear_weak_fields$;\n"
		"   $maybe_clear_hasbits$;\n"
		"   _internal_metadata_.Clear<$unknown_fields_type$>();\n"
		" }\n", SCB("maybe_clear_extensions", gc_maybe_clear_extensions, g),
	     SCB("clear_fields", gc_clear_fields, g), SCB("clear_oneofs", gc_clear_oneofs, g),
	     SCB("maybe_clear_weak_fields", gc_maybe_clear_weak_fields, g),
	     SCB("maybe_clear_hasbits", gc_maybe_clear_hasbits, g));
}

static void mg_generate_oneof_clear(MessageGenerator *g, LpPrinter *p)
{
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		const LpOneof *oneof = &g->descriptor->oneofs[i];
		LpSub v[] = {S_("oneofname", oneof->name)};
		lp_push_vars(p, v, 1);
		FORMAT(p, NULL, "void $Msg$::clear_$oneofname$() {\n// @@protoc_insertion_point(one_of_clear_start:$full_name$)\n");
		lp_indent(p);
		FORMAT(p, NULL, "$pbi$::TSanWrite(&_impl_);\n");
		FORMAT(p, NULL, "switch ($oneofname$_case()) {\n");
		lp_indent(p);
		for (int j = 0; j < oneof->n_fields; j++) {
			const LpField *f = proto_oneof_field(oneof, j);
			FORMAT(p, NULL, "case k$1$: {\n", underscores_to_camel_case(f->name, true));
			lp_indent(p);
			if (!is_string_or_message(f))
				FORMAT(p, NULL, "// No need to clear\n");
			else
				fg_generate_clearing_code(fg_get(&g->field_generators, f), p);
			FORMAT(p, NULL, "break;\n");
			lp_outdent(p);
			FORMAT(p, NULL, "}\n");
		}
		FORMAT(p, NULL, "case $1$_NOT_SET: {\n  break;\n}\n", ascii_upper(oneof->name));
		lp_outdent(p);
		FORMAT(p, NULL, "}\n$oneof_case$[$1$] = $2$_NOT_SET;\n", cpp_itoa(i), ascii_upper(oneof->name));
		lp_outdent(p);
		FORMAT(p, NULL, "}\n\n");
		POP_VARS(p);
	}
}

static bool swap_run_pred(const LpField *f, const void *arg)
{
	const MessageGenerator *g = arg;
	return !should_split(f, &g->options) && has_trivial_swap(f, &g->options);
}

static void mg_generate_swap(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (has_simple_base_class(d, &g->options))
		return;
	FORMAT(p, NULL, "void $Msg$::InternalSwap($Msg$* PROTOBUF_RESTRICT $nonnull$ other) {\n");
	lp_indent(p);
	FORMAT(p, NULL, "using ::std::swap;\n");
	FORMAT(p, NULL, "$WeakDescriptorSelfPin$");
	if (has_generated_methods(d->file, &g->options)) {
		if (d->n_extension_ranges > 0)
			FORMAT(p, NULL, "$extensions$.InternalSwap(&other->$extensions$);\n");
		if (has_non_split_optional_string(d, &g->options))
			EMIT0(p, "\n"
		" auto* arena = GetArena();\n"
		" ABSL_DCHECK_EQ(arena, other->GetArena());\n");
		FORMAT(p, NULL, "_internal_metadata_.InternalSwap(&other->_internal_metadata_);\n");
		if (layout_has_hasbits(&g->field_layout))
			for (int i = 0; i < layout_has_bits_size(&g->field_layout); ++i)
				FORMAT(p, NULL, "swap($has_bits$[$1$], other->$has_bits$[$1$]);\n", cpp_itoa(i));
		const FieldLayout *l = &g->field_layout;
		int *runs = find_runs(d, l->fields, l->n_fields, swap_run_pred, g);
		for (int i = 0; i < l->n_fields; ++i) {
			const LpField *f = l->fields[i];
			if (should_split(f, &g->options))
				continue;
			int run_length = runs[f->index];
			if (run_length > 1) {
				LpSub v[] = {S_("first", field_member_name(f, false)),
					     S_("last", field_member_name(l->fields[i + run_length - 1], false))};
				lp_push_vars(p, v, 2);
				FORMAT(p, NULL,
				       "$pbi$::memswap<\n"
				       "    PROTOBUF_FIELD_OFFSET($Msg$, $last$)\n"
				       "    + sizeof($Msg$::$last$)\n"
				       "    - PROTOBUF_FIELD_OFFSET($Msg$, $first$)>(\n"
				       "        reinterpret_cast<char*>(&$first$),\n"
				       "        reinterpret_cast<char*>(&other->$first$));\n");
				POP_VARS(p);
				i += run_length - 1;
			} else {
				fg_generate_swapping_code(fg_get(&g->field_generators, f), p);
			}
		}
		for (int i = 0; i < d->n_real_oneofs; i++)
			FORMAT(p, NULL, "swap(_impl_.$1$_, other->_impl_.$1$_);\n", d->oneofs[i].name);
		for (int i = 0; i < d->n_real_oneofs; ++i)
			FORMAT(p, NULL, "swap($oneof_case$[$1$], other->$oneof_case$[$1$]);\n", cpp_itoa(i));
		if (g->num_weak_fields)
			FORMAT(p, NULL, "$weak_field_map$.UnsafeArenaSwap(&other->$weak_field_map$);\n");
	} else {
		FORMAT(p, NULL, "GetReflection()->Swap(this, other);");
	}
	lp_outdent(p);
	FORMAT(p, NULL, "}\n");
}

/* GetNewOp: needs_to_run_constructor (bit 1) and needs_memcpy (bit 0) */
static void mg_get_new_op(const MessageGenerator *g, bool *needs_memcpy, bool *needs_to_run_constructor)
{
	*needs_memcpy = *needs_to_run_constructor = false;
	if (is_bootstrap_proto(&g->options, g->descriptor->file)) {
		*needs_to_run_constructor = true;
		return;
	}
	if (mg_needs_arena_destructor(g) == ARENA_DTOR_REQUIRED)
		*needs_to_run_constructor = true;
	if (g->num_weak_fields != 0)
		*needs_to_run_constructor = true;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *f = &g->descriptor->fields[i];
		if (should_split(f, &g->options)) {
			*needs_memcpy = true;
		} else if (real_oneof(f)) {
		} else if (is_repeated(f) || is_map(f)) {
			*needs_memcpy = true;
		} else if (!fg_get(&g->field_generators, f)->has_trivial_zero_default) {
			switch (cpp_type(f)) {
			case LP_CPPTYPE_STRING:
				if (cpp_string_type(f) != CPP_STR_CORD)
					*needs_memcpy = true;
				break;
			case LP_CPPTYPE_MESSAGE:
				fail("Message should be zero initializable.");
			default:
				*needs_memcpy = true;
				break;
			}
		}
	}
}

static void mg_generate_new_op(const MessageGenerator *g, LpPrinter *p)
{
	bool needs_memcpy, needs_to_run_constructor;
	mg_get_new_op(g, &needs_memcpy, &needs_to_run_constructor);
	if (needs_to_run_constructor)
		EMIT0(p, "\n"
		" constexpr auto $Msg$::InternalNewImpl_() {\n"
		"   return $pbi$::MessageCreator(&$Msg$::PlacementNew_, sizeof($Msg$),\n"
		"                                alignof($Msg$));\n"
		" }\n");
	else
		EMIT(p, "\n"
		" constexpr auto $Msg$::InternalNewImpl_() {\n"
		"   return $pbi$::MessageCreator::$copy_type$(sizeof($Msg$), alignof($Msg$));\n"
		" }\n", S_("copy_type", needs_memcpy ? "CopyInit" : "ZeroInit"));
}

static void gcd_is_initialized(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (mg_needs_is_initialized(g))
		EMIT0(p, "\n"
		" $Msg$::IsInitializedImpl,\n");
	else
		EMIT0(p, "\n"
		" nullptr,  // IsInitialized\n");
}

static void gcd_custom_vtable_methods(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_generated_methods(g->descriptor->file, &g->options) && !is_map_entry_message(g->descriptor))
		EMIT0(p, "\n"
		" Super_::GetClearImpl<$Msg$>(), &$Msg$::ByteSizeLong,\n"
		"     &$Msg$::_InternalSerialize,\n");
	else
		EMIT0(p, "\n"
		" static_cast<void ($pb$::MessageLite::*)()>(&$Msg$::ClearImpl),\n"
		"     Super_::ByteSizeLongImpl, Super_::_InternalSerializeImpl,\n");
}

static void gcd_tracker_on_get_metadata(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (has_tracker(g->descriptor, &g->options))
		EMIT0(p, "\n"
		" &Impl_::TrackerOnGetMetadata,\n");
	else
		EMIT0(p, "\n"
		" nullptr,  // tracker\n");
}

static void mg_generate_internal_generate_class_data(MessageGenerator *g, LpPrinter *p)
{
	if (has_descriptor_methods(g->descriptor->file, &g->options))
		EMIT(p, "\n"
		" constexpr auto $Msg$::InternalGenerateClassData_(\n"
		"     const MessageLite& prototype,\n"
		"     const $pbi$::TcParseTableBase* tc_table) {\n"
		"   return $pbi$::ClassDataFull{\n"
		"       $pbi$::ClassData{\n"
		"           &prototype,\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		"           &_table_.header,\n"
		"#else\n"
		"           tc_table,\n"
		"#endif\n"
		"           $is_initialized$,\n"
		"           &$Msg$::MergeImpl,\n"
		"           Super_::GetNewImpl<$Msg$>(),\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"           &$Msg$::SharedDtor,\n"
		"           $custom_vtable_methods$,\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"           PROTOBUF_FIELD_OFFSET($Msg$, $cached_size$),\n"
		"           false,\n"
		"       },\n"
		"#ifdef PROTOBUF_MESSAGE_GLOBALS\n"
		"       &file_reflection_data[$index_in_file_messages$],\n"
		"#else   // !PROTOBUF_MESSAGE_GLOBALS\n"
		"       &::_pbi::kDescriptorMethods,\n"
		"       &$desc_table$,\n"
		"       $tracker_on_get_metadata$,\n"
		"#endif  // PROTOBUF_MESSAGE_GLOBALS\n"
		"   };\n"
		" }\n", SCB("is_initialized", gcd_is_initialized, g),
		     SCB("custom_vtable_methods", gcd_custom_vtable_methods, g),
		     SINT("index_in_file_messages", g->index_in_file_messages),
		     SCB("tracker_on_get_metadata", gcd_tracker_on_get_metadata, g));
	else
		EMIT(p, "\n"
		" constexpr auto $Msg$::InternalGenerateClassData_(\n"
		"     const MessageLite& prototype,\n"
		"     const $pbi$::TcParseTableBase* tc_table) {\n"
		"   return $pbi$::ClassDataLite{\n"
		"       {\n"
		"           &prototype,\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		"           &_table_.header,\n"
		"#else\n"
		"           tc_table,\n"
		"#endif\n"
		"           $is_initialized$,\n"
		"           &$Msg$::MergeImpl,\n"
		"           Super_::GetNewImpl<$Msg$>(),\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		"           &$Msg$::SharedDtor,\n"
		"           $custom_vtable_methods$,\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"           PROTOBUF_FIELD_OFFSET($Msg$, $cached_size$),\n"
		"           true,\n"
		"       },\n"
		"       \"$full_name$\",\n"
		"   };\n"
		" }\n", SCB("is_initialized", gcd_is_initialized, g),
		     SCB("custom_vtable_methods", gcd_custom_vtable_methods, g));
}

static void cd_pin_weak_descriptor(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!using_implicit_weak_descriptor(g->descriptor->file, &g->options))
		return;
}

static void mg_generate_class_data(MessageGenerator *g, LpPrinter *p)
{
	if (has_descriptor_methods(g->descriptor->file, &g->options)) {
		if (is_map_entry_message(g->descriptor))
			EMIT(p, "\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		" PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 const\n"
		"     $pbi$::ClassDataFull $Msg$_class_data_ =\n"
		"         $Msg$::InternalGenerateClassData_($globals$._default);\n"
		"\n"
		" //~ This function needs to be marked as weak to avoid significantly\n"
		" //~ slowing down compilation times.  This breaks up LLVM's SCC\n"
		" //~ in the .pb.cc translation units. Large translation units see a\n"
		" //~ reduction of roughly 50% of walltime for optimized builds.\n"
		" //~ Without the weak attribute all the messages in the file,\n"
		" // including ~ all the vtables and everything they use become part\n"
		" // of the same ~ SCC.\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   $pin_weak_descriptor$;\n"
		"   $pbi$::PrefetchToLocalCache(&$Msg$_class_data_);\n"
		"   $pbi$::PrefetchToLocalCache($Msg$_class_data_.tc_table);\n"
		"   return $Msg$_class_data_.base();\n"
		" }\n"
		"#else\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   $pin_weak_descriptor$;\n"
		"   $pbi$::PrefetchToLocalCache(&$globals$);\n"
		"   $pbi$::PrefetchToLocalCache(\n"
		"       $pbi$::MessageGlobalsBase::ToParseTableBase(&$globals$));\n"
		"   return $globals$.GetClassData();\n"
		" }\n"
		"#endif  // !PROTOBUF_MESSAGE_GLOBALS\n", SCB("pin_weak_descriptor", cd_pin_weak_descriptor, g));
		else
			EMIT(p, "\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		" PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 const\n"
		"     $pbi$::ClassDataFull $Msg$_class_data_ =\n"
		"         $Msg$::InternalGenerateClassData_($globals$._default);\n"
		"\n"
		" //~ This function needs to be marked as weak to avoid significantly\n"
		" //~ slowing down compilation times.  This breaks up LLVM's SCC\n"
		" //~ in the .pb.cc translation units. Large translation units see a\n"
		" //~ reduction of roughly 50% of walltime for optimized builds.\n"
		" //~ Without the weak attribute all the messages in the file,\n"
		" //~ including all the vtables and everything they use become part\n"
		" //~ of the same SCC.\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   $pin_weak_descriptor$;\n"
		"   $pbi$::PrefetchToLocalCache(&$Msg$_class_data_);\n"
		"   $pbi$::PrefetchToLocalCache($Msg$_class_data_.tc_table);\n"
		"   return $Msg$_class_data_.base();\n"
		" }\n"
		"#else\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   $pin_weak_descriptor$;\n"
		"   $pbi$::PrefetchToLocalCache(&$globals$);\n"
		"   $pbi$::PrefetchToLocalCache(\n"
		"       $pbi$::MessageGlobalsBase::ToParseTableBase(&$globals$));\n"
		"   return $globals$.GetClassData();\n"
		" }\n"
		"#endif  // !PROTOBUF_MESSAGE_GLOBALS\n", SCB("pin_weak_descriptor", cd_pin_weak_descriptor, g));
	} else {
		EMIT0(p, "\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		" PROTOBUF_CONSTINIT\n"
		" PROTOBUF_ATTRIBUTE_INIT_PRIORITY1\n"
		" const $pbi$::ClassDataLite $Msg$_class_data_ =\n"
		"     $Msg$::InternalGenerateClassData_($globals$._default);\n"
		"\n"
		" //~ This function needs to be marked as weak to avoid significantly\n"
		" //~ slowing down compilation times.  This breaks up LLVM's SCC\n"
		" //~ in the .pb.cc translation units. Large translation units see a\n"
		" //~ reduction of roughly 50% of walltime for optimized builds.\n"
		" //~ Without the weak attribute all the messages in the file, including\n"
		" //~ all the vtables and everything they use become part of the same\n"
		" //~ SCC.\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   return $Msg$_class_data_.base();\n"
		" }\n"
		"#else\n"
		" PROTOBUF_ATTRIBUTE_WEAK const $pbi$::ClassData* $nonnull$\n"
		" $Msg$::GetClassData() const {\n"
		"   return $globals$.GetClassData();\n"
		" }\n"
		"#endif  // !PROTOBUF_MESSAGE_GLOBALS\n");
	}
}

static bool mg_requires_arena(const MessageGenerator *g, bool is_split)
{
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const FieldGenerator *gen = &g->field_generators.fields[i];
		if (gen->should_split == is_split && fg_requires_arena(gen))
			return true;
	}
	return !is_split && g->descriptor->n_extension_ranges > 0;
}

typedef struct {
	MessageGenerator *g;
	bool is_split;
	int cached_has_word_index;
	const LpField *field;
	bool check_has_byte;
} MergeCtx;

static void mc_prepare_split(LpPrinter *p, const MergeCtx *c)
{
	if (c->is_split)
		EMIT0(p, "\n"
		" _this->PrepareSplitMessageForWrite();\n");
}

static void mc_body_prepare_and_merge(LpPrinter *p, void *arg)
{
	MergeCtx *c = arg;
	mc_prepare_split(p, c);
	fg_generate_merging_code(fg_get(&c->g->field_generators, c->field), p);
}

static void mc_body_merge(LpPrinter *p, void *arg)
{
	MergeCtx *c = arg;
	fg_generate_merging_code(fg_get(&c->g->field_generators, c->field), p);
}

static void mc_merge_field_cached(LpPrinter *p, void *arg)
{
	MergeCtx *c = arg;
	const LpField *f = c->field;
	const FieldGenerator *gen = fg_get(&c->g->field_generators, f);
	mc_prepare_split(p, c);
	if (get_field_hasbit_mode(f, &c->g->options) == HASBIT_HINT) {
		may_emit_mutable_if_non_default_check(p, "from.", f, c->is_split, &c->g->options, mc_body_merge, c, false);
	} else if (c->check_has_byte && is_pod(f)) {
		fg_generate_copy_constructor_code(gen, p);
	} else {
		fg_generate_merging_code(gen, p);
	}
}

static void mc_emit_merge_chunk(MergeCtx *c, LpPrinter *p, const FieldChunk *chunk)
{
	MessageGenerator *g = c->g;
	const FieldLayout *l = &g->field_layout;
	int has_word_index = layout_has_word_index(l, chunk->fields[0]);
	bool cache_has_bits = has_word_index >= 0;
	bool check_has_byte = cache_has_bits && chunk->n > 1 && !is_likely_present(chunk->fields[chunk->n - 1], &g->options);
	if (cache_has_bits && c->cached_has_word_index != has_word_index) {
		c->cached_has_word_index = has_word_index;
		EMIT(p, "\n"
		" cached_has_bits = from.$has_bits$[$cached_has_word_index$];\n", SINT("cached_has_word_index", has_word_index));
	}
	if (check_has_byte) {
		uint32_t chunk_mask = gen_chunk_mask_fields(chunk->fields, chunk->n, l);
		EMIT(p, "\n"
		" if ($condition$) {\n", S_("condition", generate_condition_for_group(chunk_mask, &g->options)));
		lp_indent(p);
	}
	for (int i = 0; i < chunk->n; i++) {
		const LpField *f = chunk->fields[i];
		c->field = f;
		c->check_has_byte = check_has_byte;
		if (!proto_field_is_required(f) && !has_hasbit(f, &g->options)) {
			may_emit_mutable_if_non_default_check(p, "from.", f, c->is_split, &g->options, mc_body_prepare_and_merge,
							      c, true);
		} else if (is_weak_option(f) || c->cached_has_word_index != layout_has_word_index(l, f)) {
			Subs v = mg_has_bit_vars(g, f);
			PUSH_SUBS(p, &v);
			EMIT(p, "\n"
		" if (CheckHasBit(from.$has_bits$[$has_array_index$], $has_mask$)) {\n"
		"   $merge_field$;\n"
		" }\n", SCB("merge_field", mc_body_prepare_and_merge, c));
			POP_VARS(p);
		} else {
			int has_bit_index = layout_has_bit_index(l, f);
			EMIT(p, "\n"
		" if ($condition$) {\n"
		"   $merge_field$;\n"
		" }\n", S_("condition", generate_condition_for_field(has_bit_index, f, &g->options)),
			     SCB("merge_field", mc_merge_field_cached, c));
		}
	}
	if (check_has_byte) {
		lp_outdent(p);
		EMIT0(p, "\n"
		" }\n");
	}
}

static bool merge_chunk_equiv(const LpField *a, const LpField *b, const void *arg)
{
	const MergeCtx *c = arg;
	return layout_has_byte_index(&c->g->field_layout, a) == layout_has_byte_index(&c->g->field_layout, b) &&
	       is_likely_present(a, &c->g->options) == is_likely_present(b, &c->g->options);
}

static bool merge_split_filter(const LpField *f, const void *arg)
{
	const MergeCtx *c = arg;
	return should_split(f, &c->g->options) == c->is_split;
}

static bool mg_emit_merge_chunks(MessageGenerator *g, LpPrinter *p, bool is_split)
{
	MergeCtx c = {.g = g, .is_split = is_split, .cached_has_word_index = -1};
	FieldChunks chunks = collect_fields(g->field_layout.fields, g->field_layout.n_fields, &g->options, merge_chunk_equiv,
					    merge_split_filter, &c);
	int it = 0, end = chunks.n;
	while (it != end) {
		int next = find_next_unequal_chunk(&chunks, it, end, chunk_equal_group);
		bool has_haswords_check = maybe_emit_haswords_check(&chunks, it, next, &g->options);
		while (it != next) {
			mc_emit_merge_chunk(&c, p, &chunks.v[it]);
			++it;
		}
		if (has_haswords_check) {
			lp_outdent(p);
			EMIT0(p, "\n"
		" }\n");
			c.cached_has_word_index = -1;
		}
	}
	return c.cached_has_word_index >= 0;
}

typedef struct {
	MessageGenerator *g;
	bool populated;
	const LpOneof *oneof;
	const LpField *field;
} MergeImplCtx;

static void mi_has_bit_consistency(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	mg_generate_check_has_bit_consistency(c->g, p, "from.");
}

static void mi_get_arena(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	if (mg_requires_arena(c->g, false))
		EMIT0(p, "\n"
		" $pb$::Arena* arena = _this->GetArena();\n");
}

static void mi_merge_fields(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	c->populated = mg_emit_merge_chunks(c->g, p, false);
}

static void mi_merge_hasbits(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	int size = layout_has_bits_size(&c->g->field_layout);
	if (size == 1 && c->populated)
		EMIT0(p, "\n"
		" _this->$has_bits$[0] |= cached_has_bits;\n");
	else if (size >= 1)
		EMIT0(p, "\n"
		" _this->$has_bits$.Or(from.$has_bits$);\n");
}

static void mi_case_body(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	fg_generate_merging_code(fg_get(&c->g->field_generators, c->field), p);
}

static void mi_cases(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	for (int i = 0; i < c->oneof->n_fields; i++) {
		c->field = proto_oneof_field(c->oneof, i);
		EMIT(p, "\n"
		" case k$Label$: {\n"
		"   $body$;\n"
		"   break;\n"
		" }\n", S_("Label", underscores_to_camel_case(c->field->name, true)),
		     SCB("body", mi_case_body, c));
	}
}

static void mi_merge_oneof(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	const LpMessage *d = c->g->descriptor;
	for (int i = 0; i < d->n_real_oneofs; i++) {
		c->oneof = &d->oneofs[i];
		EMIT(p, "\n"
		" if (const uint32_t oneof_from_case =\n"
		"         from.$oneof_case$[$index$]) {\n"
		"   const uint32_t oneof_to_case = _this->$oneof_case$[$index$];\n"
		"   const bool oneof_needs_init = oneof_to_case != oneof_from_case;\n"
		"   if (oneof_needs_init) {\n"
		"     if (oneof_to_case != 0) {\n"
		"       _this->clear_$name$();\n"
		"     }\n"
		"     _this->$oneof_case$[$index$] = oneof_from_case;\n"
		"   }\n"
		"\n"
		"   switch (oneof_from_case) {\n"
		"     $cases$;\n"
		"     case $NAME$_NOT_SET:\n"
		"       break;\n"
		"   }\n"
		" }\n", S_("name", c->oneof->name), S_("NAME", ascii_upper(c->oneof->name)),
		     SINT("index", c->oneof->index), SCB("cases", mi_cases, c));
	}
}

static void mi_merge_weak_fields(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	if (c->g->num_weak_fields)
		EMIT0(p, "\n"
		" _this->$weak_field_map$.MergeFrom(from.$weak_field_map$);\n");
}

static void mi_merge_extensions(LpPrinter *p, void *arg)
{
	MergeImplCtx *c = arg;
	if (c->g->descriptor->n_extension_ranges > 0)
		EMIT0(p, "\n"
		" _this->$extensions$.MergeFrom(arena, &default_instance(),\n"
		"                               from.$extensions$, from.GetArena());\n");
}

static void mg_generate_class_specific_merge_impl(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	MergeImplCtx c = {.g = g};
	EMIT(p, "\n"
		" void $Msg$::MergeImpl($pb$::MessageLite& to_msg,\n"
		"                       const $pb$::MessageLite& from_msg) {\n"
		"   $WeakDescriptorSelfPin$ auto* const _this = static_cast<$Msg$*>(&to_msg);\n"
		"   auto& from = static_cast<const $Msg$&>(from_msg);\n"
		"   $has_bit_consistency$;\n"
		"   $get_arena$;\n"
		"   $annotate_mergefrom$;\n"
		"   // @@protoc_insertion_point(class_specific_merge_from_start:$full_name$)\n"
		"   $DCHK$_NE(&from, _this);\n"
		"   $uint32$ cached_has_bits = 0;\n"
		"   (void)cached_has_bits;\n"
		"\n"
		"   $merge_fields$;\n"
		"   $merge_split$;\n"
		"   $merge_hasbits$;\n"
		"   $merge_oneof$;\n"
		"   $merge_weak_fields$;\n"
		"   $merge_extensions$;\n"
		"   _this->_internal_metadata_.MergeFrom<$unknown_fields_type$>(\n"
		"       from._internal_metadata_);\n"
		" }\n", SCB("has_bit_consistency", mi_has_bit_consistency, &c),
	     SCB("get_arena", mi_get_arena, &c), SCB("merge_fields", mi_merge_fields, &c),
	     SCB("merge_split", mg_nothing_cb, NULL), SCB("merge_hasbits", mi_merge_hasbits, &c),
	     SCB("merge_oneof", mi_merge_oneof, &c), SCB("merge_weak_fields", mi_merge_weak_fields, &c),
	     SCB("merge_extensions", mi_merge_extensions, &c));
}

static void cf_clear(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!g->options.opensource_runtime && has_message_field_or_extension(g->descriptor)) {
		if (has_descriptor_methods(g->descriptor->file, &g->options))
			EMIT0(p, "\n"
		" $DCHK$(!::_pbi::IsDescendant(*this, from))\n"
		"     << \"Source of CopyFrom cannot be a descendant of the \"\n"
		"        \"target.\";\n"
		" $DCHK$(!::_pbi::IsDescendant(from, *this))\n"
		"     << \"Target of CopyFrom cannot be a descendant of the \"\n"
		"        \"source.\";\n"
		" Clear();");
		else
			EMIT0(p, "\n"
		"#ifndef NDEBUG\n"
		" ::size_t from_size = from.ByteSizeLong();\n"
		"#endif\n"
		" Clear();\n"
		"#ifndef NDEBUG\n"
		" $CHK$_EQ(from_size, from.ByteSizeLong())\n"
		"     << \"Source of CopyFrom changed when clearing target.  \"\n"
		"        \"Either \"\n"
		"        \"source is a nested message in target (not allowed), or \"\n"
		"        \"another thread is modifying the source.\";\n"
		"#endif\n");
	} else {
		EMIT0(p, "\n"
		" Clear();\n");
	}
}

static void mg_generate_copy_from(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" void $Msg$::CopyFrom(const $Msg$& from) {\n"
		"   // @@protoc_insertion_point(class_specific_copy_from_start:$full_name$)\n"
		"   if (&from == this) return;\n"
		"   $clear$;\n"
		"   MergeFrom(from);\n"
		" }\n", SCB("clear", cf_clear, g));
}

typedef struct {
	MessageGenerator *g;
	const LpField **fields;
	int n;
	const LpField *field;
	bool use_cached;
} SerCtx;

static void ser_case_body(LpPrinter *p, void *arg)
{
	SerCtx *c = arg;
	fg_generate_serialize_with_cached_sizes_to_array(fg_get(&c->g->field_generators, c->field), p);
}

static void ser_oneof_cases(LpPrinter *p, void *arg)
{
	SerCtx *c = arg;
	for (int i = 0; i < c->n; i++) {
		c->field = c->fields[i];
		EMIT(p, "\n"
		" case k$Name$: {\n"
		"   $body$;\n"
		"   break;\n"
		" }\n", S_("Name", underscores_to_camel_case(c->field->name, true)),
		     SCB("body", ser_case_body, c));
	}
}

static void mg_generate_serialize_one_field(MessageGenerator *g, LpPrinter *p, const LpField *f, int cached_has_bits_index);

static void mg_generate_serialize_oneof_fields(MessageGenerator *g, LpPrinter *p, const LpField **fields, int n)
{
	if (n == 1) {
		mg_generate_serialize_one_field(g, p, fields[0], -1);
		return;
	}
	SerCtx c = {.g = g, .fields = fields, .n = n};
	EMIT(p, "\n"
		" switch (this_.$name$_case()) {\n"
		"   $cases$;\n"
		"   default:\n"
		"     break;\n"
		" }\n", S_("name", fields[0]->containing_oneof->name), SCB("cases", ser_oneof_cases, &c));
}

static void ser_one_body(LpPrinter *p, void *arg)
{
	SerCtx *c = arg;
	may_emit_if_non_default_check(p, "this_.", c->field, &c->g->options, ser_case_body, c, false);
}

static void mg_generate_serialize_one_field(MessageGenerator *g, LpPrinter *p, const LpField *f, int cached_has_bits_index)
{
	Subs v = {0};
	field_vars(&v, f, &g->options);
	PUSH_SUBS(p, &v);
	SerCtx c = {.g = g, .field = f};
	if (is_weak_option(f)) {
		ser_case_body(p, &c);
		EMIT0(p, "\n");
		POP_VARS(p);
		return;
	}
	print_field_comment(p, NULL, f, &g->options);
	if (has_hasbit(f, &g->options)) {
		int has_bit_index = layout_has_bit_index(&g->field_layout, f);
		int has_word_index = layout_has_word_index(&g->field_layout, f);
		bool use_cached_has_bits = cached_has_bits_index == has_word_index;
		EMIT(p, "\n"
		" if ($cond$) {\n"
		"   $body$;\n"
		" }\n", SCB("body", ser_one_body, &c),
		     S_("cond", generate_condition_maybe_with_probability(1u << (has_bit_index % 32), use_cached_has_bits,
									     has_word_index, false)));
	} else if (!proto_field_is_required(f) && !is_repeated(f)) {
		may_emit_if_non_default_check(p, "this_.", f, &g->options, ser_case_body, &c, true);
	} else {
		ser_case_body(p, &c);
	}
	EMIT0(p, "\n");
	POP_VARS(p);
}

static void mg_generate_serialize_one_extension_range(MessageGenerator *g, LpPrinter *p, int start, int end)
{
	PUSH_SUBS(p, &g->variables);
	EMIT(p, "\n"
		" // Extension range [$start$, $end$)\n"
		" target = this_.$extensions$._InternalSerialize(\n"
		"     &default_instance(), $start$, $end$, target, stream);\n", SINT("start", start), SINT("end", end));
	POP_VARS(p);
}

static void mg_generate_serialize_all_extensions(MessageGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		" // All extensions.\n"
		" target = this_.$extensions$._InternalSerializeAll(&default_instance(),\n"
		"                                                   target, stream);\n");
	POP_VARS(p);
}

static void ser_has_bit_consistency(LpPrinter *p, void *arg) { mg_generate_check_has_bit_consistency(arg, p, "this_."); }

static void mg_generate_serialize_with_cached_sizes_body(MessageGenerator *g, LpPrinter *p);

static void ser_ndebug(LpPrinter *p, void *arg) { mg_generate_serialize_with_cached_sizes_body(arg, p); }

static void ser_ifdef(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (should_serialize_in_order(g->descriptor, &g->options))
		EMIT0(p, "$ndebug$");
}

static void mg_generate_serialize_with_cached_sizes_to_array(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	if (msg_option(g->descriptor, CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT)) {
		EMIT0(p, "\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" $uint8$* $nonnull$ $Msg$::_InternalSerialize(\n"
		"     const $pb$::MessageLite& base, $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) {\n"
		"   const $Msg$& this_ = static_cast<const $Msg$&>(base);\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" $uint8$* $nonnull$ $Msg$::_InternalSerialize(\n"
		"     $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) const {\n"
		"   const $Msg$& this_ = *this;\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $annotate_serialize$ target =\n"
		"       this_.$extensions$\n"
		"           .InternalSerializeMessageSetWithCachedSizesToArray(\n"
		"               &default_instance(), target, stream);\n"
		"   target = ::_pbi::InternalSerializeUnknownMessageSetItemsToArray(\n"
		"       this_.$unknown_fields$, target, stream);\n"
		"   return target;\n"
		" }\n");
		return;
	}
	EMIT(p, "\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" $uint8$* $nonnull$ $Msg$::_InternalSerialize(\n"
		"     const $pb$::MessageLite& base, $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) {\n"
		"   const $Msg$& this_ = static_cast<const $Msg$&>(base);\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" $uint8$* $nonnull$ $Msg$::_InternalSerialize(\n"
		"     $uint8$* $nonnull$ target,\n"
		"     $pb$::io::EpsCopyOutputStream* $nonnull$ stream) const {\n"
		"   const $Msg$& this_ = *this;\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $annotate_serialize$;\n"
		"   $has_bit_consistency$;\n"
		"   // @@protoc_insertion_point(serialize_to_array_start:$full_name$)\n"
		"   $ifdef$;\n"
		"   // @@protoc_insertion_point(serialize_to_array_end:$full_name$)\n"
		"   return target;\n"
		" }\n", SCB("has_bit_consistency", ser_has_bit_consistency, g), SCB("ndebug", ser_ndebug, g),
	     SCB("debug", mg_nothing_cb, NULL), SCB("ifdef", ser_ifdef, g));
}

/* LazySerializerEmitter */
typedef struct {
	MessageGenerator *mg;
	LpPrinter *p;
	bool is_split_open;
	const LpField **v;
	int n_v;
	int cached_has_bit_index;
} LazySerializerEmitter;

static void lse_close_split(LazySerializerEmitter *e)
{
	if (!e->is_split_open)
		return;
	e->is_split_open = false;
	lp_outdent(e->p);
	EMIT0(e->p, "\n"
		" }\n");
	e->cached_has_bit_index = -1;
}

static void lse_open_split(LazySerializerEmitter *e)
{
	if (e->is_split_open)
		return;
	e->is_split_open = true;
	EMIT0(e->p, "\n"
		" if (ABSL_PREDICT_FALSE(serialize_split_fields)) {\n");
	lp_indent(e->p);
}

static void lse_flush(LazySerializerEmitter *e)
{
	lse_close_split(e);
	if (e->n_v) {
		mg_generate_serialize_oneof_fields(e->mg, e->p, e->v, e->n_v);
		e->n_v = 0;
	}
}

static void lse_emit(LazySerializerEmitter *e, const LpField *f)
{
	const Options *o = &e->mg->options;
	if (!proto_field_has_presence(f) || (e->n_v && e->v[0]->containing_oneof != f->containing_oneof))
		lse_flush(e);
	if (real_oneof(f)) {
		e->v[e->n_v++] = f;
	} else {
		if (should_split(f, o))
			lse_open_split(e);
		else
			lse_close_split(e);
		if (has_hasbit(f, o)) {
			int has_word_index = layout_has_word_index(&e->mg->field_layout, f);
			if (e->cached_has_bit_index != has_word_index) {
				EMIT(e->p, "\n"
		" cached_has_bits = this_._impl_._has_bits_[$index$];\n", SINT("index", has_word_index));
				e->cached_has_bit_index = has_word_index;
			}
		}
		mg_generate_serialize_one_field(e->mg, e->p, f, e->cached_has_bit_index);
	}
}

static void lse_emit_if_not_null(LazySerializerEmitter *e, const LpField *f)
{
	if (f)
		lse_emit(e, f);
}

/* LazyExtensionRangeEmitter */
typedef struct {
	MessageGenerator *mg;
	LpPrinter *p;
	int range_count;
	bool has_current_range;
	int min_start, max_end;
} LazyExtensionRangeEmitter;

static void lere_add_to_range(LazyExtensionRangeEmitter *r, const LpExtensionRange *range)
{
	if (!r->has_current_range) {
		r->min_start = range->start;
		r->max_end = range->end;
		r->has_current_range = true;
	} else {
		if (range->start < r->min_start)
			r->min_start = range->start;
		if (range->end > r->max_end)
			r->max_end = range->end;
	}
}

static void lere_flush(LazyExtensionRangeEmitter *r, bool is_last_range)
{
	if (!r->has_current_range)
		return;
	r->has_current_range = false;
	++r->range_count;
	if (is_last_range && r->range_count == 1)
		mg_generate_serialize_all_extensions(r->mg, r->p);
	else
		mg_generate_serialize_one_extension_range(r->mg, r->p, r->min_start, r->max_end);
}

static int cmp_ext_range_start(const void *a, const void *b)
{
	const LpExtensionRange *x = *(const LpExtensionRange *const *)a, *y = *(const LpExtensionRange *const *)b;
	return (x->start > y->start) - (x->start < y->start);
}

static void sb_handle_weak_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->num_weak_fields == 0)
		return;
	EMIT0(p, "\n"
		" ::_pbi::WeakFieldMap::FieldWriter field_writer(\n"
		"     this_.$weak_field_map$);\n");
}

static void sb_handle_lazy_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	const LpMessage *d = g->descriptor;
	const LpField **ordered = sort_fields_by_number(d);
	const LpExtensionRange **sorted = xalloc(sizeof(LpExtensionRange *) * (size_t)(d->n_extension_ranges + 1));
	for (int i = 0; i < d->n_extension_ranges; i++)
		sorted[i] = &d->extension_ranges[i];
	qsort(sorted, (size_t)d->n_extension_ranges, sizeof *sorted, cmp_ext_range_start);
	LazySerializerEmitter e = {.mg = g, .p = p, .v = xalloc(sizeof(LpField *) * (size_t)(d->n_fields + 1)), .cached_has_bit_index = kNoHasbit};
	LazyExtensionRangeEmitter re = {.mg = g, .p = p};
	const LpField *largest_weak = NULL;
	int i = 0, j = 0;
	while (i < d->n_fields || j < d->n_extension_ranges) {
		bool no_more_extensions = j == d->n_extension_ranges;
		if (no_more_extensions || (i < d->n_fields && ordered[i]->number < sorted[j]->start)) {
			const LpField *f = ordered[i++];
			lere_flush(&re, no_more_extensions);
			if (is_weak_option(f)) {
				if (!largest_weak || largest_weak->number < f->number)
					largest_weak = f;
				print_field_comment(p, NULL, f, &g->options);
			} else {
				const LpField *w = largest_weak;
				largest_weak = NULL;
				lse_emit_if_not_null(&e, w);
				lse_emit(&e, f);
			}
		} else {
			const LpField *w = largest_weak;
			largest_weak = NULL;
			lse_emit_if_not_null(&e, w);
			lse_flush(&e);
			lere_add_to_range(&re, sorted[j++]);
		}
	}
	lere_flush(&re, true);
	lse_emit_if_not_null(&e, largest_weak);
	lse_flush(&e);
}

static void sb_handle_unknown_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (use_unknown_field_set(g->descriptor->file, &g->options))
		EMIT0(p, "\n"
		" target =\n"
		"     ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(\n"
		"         this_.$unknown_fields$, target, stream);\n");
	else
		EMIT0(p, "\n"
		" target = stream->WriteRaw(\n"
		"     this_.$unknown_fields$.data(),\n"
		"     static_cast<int>(this_.$unknown_fields$.size()), target);\n");
}

static void mg_generate_serialize_with_cached_sizes_body(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	EMIT(p, "\n"
		" $handle_weak_fields$;\n"
		" $uint32$ cached_has_bits = 0;\n"
		" (void)cached_has_bits;\n"
		" $serialize_split_var$;\n"
		"\n"
		" $handle_lazy_fields$;\n"
		" if (ABSL_PREDICT_FALSE(this_.$have_unknown_fields$)) {\n"
		"   $handle_unknown_fields$;\n"
		" }\n", SCB("handle_weak_fields", sb_handle_weak_fields, g),
	     SCB("serialize_split_var", mg_nothing_cb, NULL), SCB("handle_lazy_fields", sb_handle_lazy_fields, g),
	     SCB("handle_unknown_fields", sb_handle_unknown_fields, g));
}

/* RequiredFieldsBitMask */
static uint32_t *mg_required_fields_bit_mask(const MessageGenerator *g, int *n)
{
	*n = layout_has_bits_size(&g->field_layout);
	uint32_t *masks = xalloc(sizeof(uint32_t) * (size_t)(*n + 1));
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *f = &g->descriptor->fields[i];
		if (!proto_field_is_required(f))
			continue;
		int has_bit_index = layout_has_bit_index(&g->field_layout, f);
		masks[has_bit_index / 32] |= (uint32_t)1 << (has_bit_index % 32);
	}
	return masks;
}

/* FixedSize (message.cc): 0 if none */
static int msg_fixed_size(const LpField *f)
{
	if (is_repeated(f) || real_oneof(f) || !proto_field_has_presence(f))
		return 0;
	size_t fs = fixed_size(f->type);
	if (!fs || f->type == PT_ENUM)
		return 0;
	return wire_format_tag_size(f->number, f->type) + (int)fs;
}

typedef struct {
	const MessageGenerator *g;
} ByteSizeChunkPred;

static bool fixed_chunk_equiv(const LpField *a, const LpField *b, const void *arg)
{
	const MessageGenerator *g = arg;
	return layout_has_word_index(&g->field_layout, a) == layout_has_word_index(&g->field_layout, b) &&
	       msg_fixed_size(a) == msg_fixed_size(b);
}

static bool rest_chunk_equiv(const LpField *a, const LpField *b, const void *arg)
{
	const MessageGenerator *g = arg;
	return proto_field_is_required(a) == proto_field_is_required(b) &&
	       layout_has_byte_index(&g->field_layout, a) == layout_has_byte_index(&g->field_layout, b) &&
	       is_likely_present(a, &g->options) == is_likely_present(b, &g->options);
}

static const MessageGenerator *fixed_sort_g;
static int cmp_fixed_tuple(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	int wx = layout_has_word_index(&fixed_sort_g->field_layout, x), wy = layout_has_word_index(&fixed_sort_g->field_layout, y);
	if (wx != wy)
		return wx < wy ? -1 : 1;
	int sx = msg_fixed_size(x), sy = msg_fixed_size(y);
	return (sx > sy) - (sx < sy);
}

typedef struct {
	const MessageGenerator *g;
	const FieldChunk *chunk;
	int *cached_has_word_index;
	bool check_has_byte;
} ByteSizeCtx;

static void bs_update_cached_has_bits(const MessageGenerator *g, LpPrinter *p, const FieldChunk *chunk,
				      int *cached_has_word_index)
{
	int has_word_index = layout_has_word_index(&g->field_layout, chunk->fields[0]);
	if (*cached_has_word_index == has_word_index)
		return;
	*cached_has_word_index = has_word_index;
	EMIT(p, "\n"
		" cached_has_bits = this_.$has_bits$[$index$];\n", SINT("index", has_word_index));
}

static void bs_update_byte_size_for_chunk(LpPrinter *p, void *arg)
{
	ByteSizeCtx *c = arg;
	for (int i = 0; i < c->chunk->n; i++)
		mg_emit_update_byte_size_for_field(c->g, c->chunk->fields[i], p, c->cached_has_word_index);
}

static void bs_may_update_cached(LpPrinter *p, void *arg)
{
	ByteSizeCtx *c = arg;
	if (!c->check_has_byte)
		return;
	bs_update_cached_has_bits(c->g, p, c->chunk, c->cached_has_word_index);
}

static void bs_check_if_chunk_present(LpPrinter *p, void *arg)
{
	ByteSizeCtx *c = arg;
	if (!c->check_has_byte)
		return;
	uint32_t chunk_mask = gen_chunk_mask_fields(c->chunk->fields, c->chunk->n, &c->g->field_layout);
	EMIT(p, "if ($condition$)", S_("condition", generate_condition_for_group(chunk_mask, &c->g->options)));
}

static void mg_emit_byte_size_chunks(MessageGenerator *g, LpPrinter *p, bool is_split)
{
	const FieldLayout *l = &g->field_layout;
	const LpField **fixed = xalloc(sizeof(LpField *) * (size_t)(l->n_fields + 1));
	const LpField **rest = xalloc(sizeof(LpField *) * (size_t)(l->n_fields + 1));
	int n_fixed = 0, n_rest = 0;
	for (int i = 0; i < l->n_fields; i++) {
		const LpField *f = l->fields[i];
		if (should_split(f, &g->options) != is_split)
			continue;
		if (msg_fixed_size(f))
			fixed[n_fixed++] = f;
		else
			rest[n_rest++] = f;
	}
	fixed_sort_g = g;
	qsort(fixed, (size_t)n_fixed, sizeof *fixed, cmp_fixed_tuple);
	FieldChunks fixed_chunks = collect_fields(fixed, n_fixed, &g->options, fixed_chunk_equiv, NULL, g);
	FieldChunks chunks = collect_fields(rest, n_rest, &g->options, rest_chunk_equiv, NULL, g);
	for (int k = 0; k < fixed_chunks.n; k++) {
		FieldChunk chunk = fixed_chunks.v[k];
		int w = layout_has_word_index(l, chunk.fields[0]);
		int pos = 0;
		while (pos < chunks.n && layout_has_word_index(l, chunks.v[pos].fields[0]) != w)
			pos++;
		chunks_push(&chunks, chunk);
		memmove(&chunks.v[pos + 1], &chunks.v[pos], sizeof(FieldChunk) * (size_t)(chunks.n - 1 - pos));
		chunks.v[pos] = chunk;
	}
	int it = 0, end = chunks.n;
	int cached_has_word_index = -1;
	while (it != end) {
		int next = find_next_unequal_chunk(&chunks, it, end, chunk_equal_group);
		bool has_haswords_check = maybe_emit_haswords_check(&chunks, it, next, &g->options);
		while (it != next) {
			const FieldChunk *chunk = &chunks.v[it];
			int fsize = msg_fixed_size(chunk->fields[0]);
			if (fsize) {
				bs_update_cached_has_bits(g, p, chunk, &cached_has_word_index);
				uint32_t mask = gen_chunk_mask_fields(chunk->fields, chunk->n, l);
				EMIT(p, "\n"
		" //~\n"
		" total_size += $popcount$($mask$ & cached_has_bits) * $fsize$;\n", S_("mask", xfmt("0x%08xU", mask)),
				     S_("popcount", popcnt(mask) == 1 ? "static_cast<bool>" : "::absl::popcount"),
				     SINT("fsize", fsize));
				++it;
				continue;
			}
			ByteSizeCtx c = {.g = g, .chunk = chunk, .cached_has_word_index = &cached_has_word_index,
					 .check_has_byte = chunk->n > 1 && layout_has_word_index(l, chunk->fields[0]) >= 0 &&
						 !is_likely_present(chunk->fields[chunk->n - 1], &g->options)};
			EMIT(p, "\n"
		" $may_update_cached_has_word_index$;\n"
		" $check_if_chunk_present$ {\n"
		"   //~ Force newline.\n"
		"   $update_byte_size_for_chunk$;\n"
		" }\n", SCB("update_byte_size_for_chunk", bs_update_byte_size_for_chunk, &c),
			     SCB("may_update_cached_has_word_index", bs_may_update_cached, &c),
			     SCB("check_if_chunk_present", bs_check_if_chunk_present, &c));
			++it;
		}
		if (has_haswords_check) {
			EMIT0(p, "\n"
		" }\n");
			cached_has_word_index = -1;
		}
	}
}

static void gbs_handle_extension_set(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->descriptor->n_extension_ranges == 0)
		return;
	EMIT0(p, "\n"
		" total_size += this_.$extensions$.ByteSize();\n");
}

static void gbs_prefetch(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	bool generate_prefetch = false;
	switch (g->field_layout.n_fields) {
	case 1:
		generate_prefetch = is_repeated(g->field_layout.fields[0]);
		break;
	case 0:
		break;
	default:
		generate_prefetch = true;
	}
	if (!generate_prefetch || !is_present_message(g->descriptor, &g->options))
		return;
	EMIT0(p, "\n"
		" ::_pbi::Prefetch5LinesFrom7Lines(&this_);\n");
}

static void gbs_handle_fields(LpPrinter *p, void *arg) { mg_emit_byte_size_chunks(arg, p, false); }

typedef struct {
	MessageGenerator *g;
	const LpOneof *oneof;
	const LpField *field;
} OneofBsCtx;

static void gbs_field_byte_size(LpPrinter *p, void *arg)
{
	OneofBsCtx *c = arg;
	fg_generate_byte_size(fg_get(&c->g->field_generators, c->field), p);
}

static void gbs_case_per_field(LpPrinter *p, void *arg)
{
	OneofBsCtx *c = arg;
	for (int i = 0; i < c->oneof->n_fields; i++) {
		c->field = proto_oneof_field(c->oneof, i);
		print_field_comment(p, NULL, c->field, &c->g->options);
		EMIT(p, "\n"
		" case k$field_name$: {\n"
		"   $field_byte_size$;\n"
		"   break;\n"
		" }\n", S_("field_name", underscores_to_camel_case(c->field->name, true)),
		     SCB("field_byte_size", gbs_field_byte_size, c));
	}
}

static void gbs_handle_oneof_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	OneofBsCtx c = {.g = g};
	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		c.oneof = &g->descriptor->oneofs[i];
		EMIT(p, "\n"
		" switch (this_.$oneof_name$_case()) {\n"
		"   $case_per_field$;\n"
		"   case $oneof_case_name$_NOT_SET: {\n"
		"     break;\n"
		"   }\n"
		" }\n", S_("oneof_name", c.oneof->name), S_("oneof_case_name", ascii_upper(c.oneof->name)),
		     SCB("case_per_field", gbs_case_per_field, &c));
	}
}

static void gbs_handle_weak_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->num_weak_fields == 0)
		return;
	EMIT0(p, "\n"
		" total_size += this_.$weak_field_map$.ByteSizeLong();\n");
}

static void gbs_handle_unknown_fields(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (use_unknown_field_set(g->descriptor->file, &g->options))
		EMIT0(p, "\n"
		" return this_.MaybeComputeUnknownFieldsSize(total_size,\n"
		"                                            &this_.$cached_size$);\n");
	else
		EMIT0(p, "\n"
		" if (ABSL_PREDICT_FALSE(this_.$have_unknown_fields$)) {\n"
		"   total_size += this_.$unknown_fields$.size();\n"
		" }\n"
		" this_.$cached_size$.Set(::_pbi::ToCachedSize(total_size));\n"
		" return total_size;\n");
}

static void mg_generate_byte_size(MessageGenerator *g, LpPrinter *p)
{
	if (has_simple_base_class(g->descriptor, &g->options))
		return;
	if (msg_option(g->descriptor, CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT)) {
		EMIT0(p, "\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" ::size_t $Msg$::ByteSizeLong(const MessageLite& base) {\n"
		"   const $Msg$& this_ = static_cast<const $Msg$&>(base);\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" ::size_t $Msg$::ByteSizeLong() const {\n"
		"   const $Msg$& this_ = *this;\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_bytesize$;\n"
		"   // @@protoc_insertion_point(message_set_byte_size_start:$full_name$)\n"
		"   ::size_t total_size = this_.$extensions$.MessageSetByteSize();\n"
		"   if (ABSL_PREDICT_FALSE(this_.$have_unknown_fields$)) {\n"
		"     total_size += ::_pbi::ComputeUnknownMessageSetItemsSize(\n"
		"         this_.$unknown_fields$);\n"
		"   }\n"
		"   this_.$cached_size$.Set(::_pbi::ToCachedSize(total_size));\n"
		"   return total_size;\n"
		" }\n");
		EMIT0(p, "\n");
		return;
	}
	EMIT(p, "\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" ::size_t $Msg$::ByteSizeLong(const MessageLite& base) {\n"
		"   const $Msg$& this_ = static_cast<const $Msg$&>(base);\n"
		"#else   // PROTOBUF_CUSTOM_VTABLE\n"
		" ::size_t $Msg$::ByteSizeLong() const {\n"
		"   const $Msg$& this_ = *this;\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n"
		"   $WeakDescriptorSelfPin$;\n"
		"   $annotate_bytesize$;\n"
		"   // @@protoc_insertion_point(message_byte_size_start:$full_name$)\n"
		"   ::size_t total_size = 0;\n"
		"   $handle_extension_set$;\n"
		"\n"
		"   ::uint32_t cached_has_bits [[maybe_unused]] = 0;\n"
		"\n"
		"   $prefetch$;\n"
		"   $handle_fields$;\n"
		"   $handle_oneof_fields$;\n"
		"   $handle_weak_fields$;\n"
		"   $handle_unknown_fields$;\n"
		" }\n", SCB("handle_extension_set", gbs_handle_extension_set, g), SCB("prefetch", gbs_prefetch, g),
	     SCB("handle_fields", gbs_handle_fields, g), SCB("handle_oneof_fields", gbs_handle_oneof_fields, g),
	     SCB("handle_weak_fields", gbs_handle_weak_fields, g),
	     SCB("handle_unknown_fields", gbs_handle_unknown_fields, g));
}

static bool mg_needs_is_initialized(MessageGenerator *g)
{
	const LpMessage *d = g->descriptor;
	if (has_simple_base_class(d, &g->options))
		return false;
	if (d->n_extension_ranges != 0 || g->num_required_fields != 0)
		return true;
	for (int i = 0; i < g->field_layout.n_fields; i++)
		if (fg_needs_is_initialized(fg_get(&g->field_generators, g->field_layout.fields[i])))
			return true;
	if (g->num_weak_fields != 0)
		return true;
	for (int i = 0; i < d->n_real_oneofs; i++)
		for (int j = 0; j < d->oneofs[i].n_fields; j++)
			if (fg_needs_is_initialized(fg_get(&g->field_generators, proto_oneof_field(&d->oneofs[i], j))))
				return true;
	return false;
}

typedef struct {
	MessageGenerator *g;
	const LpOneof *oneof;
	const LpField *field;
} IsInitCtx;

static void ii_test_extensions(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	if (c->g->descriptor->n_extension_ranges == 0)
		return;
	EMIT0(p, "\n"
		" if (!this_.$extensions$.IsInitialized(this_.GetArena(),\n"
		"                                       &default_instance())) {\n"
		"   return false;\n"
		" }\n");
}

static void ii_test_required_fields(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	if (c->g->num_required_fields == 0)
		return;
	EMIT0(p, "\n"
		" if (_Internal::MissingRequiredFields(this_.$has_bits$)) {\n"
		"   return false;\n"
		" }\n");
}

static void ii_test_ordinary_fields(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	for (int i = 0; i < c->g->field_layout.n_fields; i++)
		fg_generate_is_initialized(fg_get(&c->g->field_generators, c->g->field_layout.fields[i]), p);
}

static void ii_test_weak_fields(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	if (c->g->num_weak_fields == 0)
		return;
	EMIT0(p, "\n"
		" if (!this_.$weak_field_map$.IsInitialized())\n"
		"   return false;\n");
}

static void ii_body(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	fg_generate_is_initialized(fg_get(&c->g->field_generators, c->field), p);
}

static void ii_cases(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	for (int i = 0; i < c->oneof->n_fields; i++) {
		c->field = proto_oneof_field(c->oneof, i);
		EMIT(p, "\n"
		" case k$Name$: {\n"
		"   $body$;\n"
		"   break;\n"
		" }\n", S_("Name", underscores_to_camel_case(c->field->name, true)), SCB("body", ii_body, c));
	}
}

static void ii_test_oneof_fields(LpPrinter *p, void *arg)
{
	IsInitCtx *c = arg;
	const LpMessage *d = c->g->descriptor;
	for (int i = 0; i < d->n_real_oneofs; i++) {
		const LpOneof *oneof = &d->oneofs[i];
		bool has_required_field = false;
		for (int j = 0; j < oneof->n_fields && !has_required_field; j++) {
			const LpField *f = proto_oneof_field(oneof, j);
			has_required_field = cpp_type(f) == LP_CPPTYPE_MESSAGE && !should_ignore_required_field_check(f, &c->g->options) &&
					     scc_has_required_fields(c->g->options.scc_analyzer, f->message_type);
		}
		if (!has_required_field)
			continue;
		c->oneof = oneof;
		EMIT(p, "\n"
		" switch (this_.$name$_case()) {\n"
		"   $cases$;\n"
		"   case $NAME$_NOT_SET: {\n"
		"     break;\n"
		"   }\n"
		" }\n", S_("name", oneof->name), S_("NAME", ascii_upper(oneof->name)), SCB("cases", ii_cases, c));
	}
}

static void mg_generate_is_initialized(MessageGenerator *g, LpPrinter *p)
{
	if (!mg_needs_is_initialized(g))
		return;
	IsInitCtx c = {.g = g};
	EMIT(p, "\n"
		" PROTOBUF_NOINLINE bool $Msg$::IsInitializedImpl(\n"
		"     const MessageLite& msg) {\n"
		"   auto& this_ = static_cast<const $Msg$&>(msg);\n"
		"   $test_extensions$;\n"
		"   $test_required_fields$;\n"
		"   $test_ordinary_fields$;\n"
		"   $test_weak_fields$;\n"
		"   $test_oneof_fields$;\n"
		"   return true;\n"
		" }\n", SCB("test_extensions", ii_test_extensions, &c),
	     SCB("test_required_fields", ii_test_required_fields, &c),
	     SCB("test_ordinary_fields", ii_test_ordinary_fields, &c), SCB("test_weak_fields", ii_test_weak_fields, &c),
	     SCB("test_oneof_fields", ii_test_oneof_fields, &c));
}

static void sdi_has_bit(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!layout_has_hasbits(&g->field_layout))
		return;
	EMIT0(p, "\n"
		" using HasBits = decltype(::std::declval<$Msg$>().$has_bits$);\n"
		" static constexpr ::int32_t kHasBitsOffset =\n"
		"     8 * PROTOBUF_FIELD_OFFSET($Msg$, _impl_._has_bits_);\n");
}

static void sdi_oneof(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->descriptor->n_real_oneofs == 0)
		return;
	EMIT0(p, "\n"
		" static constexpr ::int32_t kOneofCaseOffset =\n"
		"     PROTOBUF_FIELD_OFFSET($classtype$, $oneof_case$);\n");
}

static void sdi_required(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (g->num_required_fields == 0)
		return;
	int n = 0;
	uint32_t *masks = mg_required_fields_bit_mask(g, &n);
	EMIT(p, "\n"
		" static bool MissingRequiredFields(const HasBits& has_bits) {\n"
		"   return $check_bit_mask$;\n"
		" }\n", S_("check_bit_mask", conditional_to_check_bitmasks(masks, n, false, "has_bits")));
}

static void sdi_constexpr(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!is_file_descriptor_proto(g->descriptor->file, &g->options))
		EMIT0(p, "constexpr");
	else
		EMIT0(p, "\n"
		"#if defined(PROTOBUF_CONSTINIT_DEFAULT_INSTANCES)\n"
		" constexpr\n"
		"#endif  // PROTOBUF_CONSTINIT_DEFAULT_INSTANCES\n");
}

static void sdi_file_descriptor_proto_init(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (!is_file_descriptor_proto(g->descriptor->file, &g->options))
		return;
	EMIT0(p, "\n"
		"#if !defined(PROTOBUF_CONSTINIT_DEFAULT_INSTANCES)\n"
		" void Init() { ::new (&_default) $Msg$(); }\n"
		"#endif  // !PROTOBUF_CONSTINIT_DEFAULT_INSTANCES\n");
}

static void sdi_section(LpPrinter *p, void *arg)
{
	MessageGenerator *g = arg;
	if (is_file_descriptor_proto(g->descriptor->file, &g->options))
		return;
	EMIT(p, "PROTOBUF_MESSAGE_GLOBALS_SECTION($section_name$)", S_("section_name", ".data.rel.ro"));
}

static void mg_generate_source_default_instance(MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	if (!should_generate_class(d, &g->options))
		return;
	Subs v = class_vars(d, &g->options);
	PUSH_SUBS(p, &v);
	Subs t = make_tracker_calls_message(d, &g->options);
	PUSH_SUBS(p, &t);
	if (!is_map_entry_message(d)) {
		EMIT(p, "\n"
		" class $Msg$::_Internal {\n"
		"  public:\n"
		"   $has_bit$;\n"
		"   $oneof$;\n"
		"   $required$;\n"
		"   $split$;\n"
		" };\n", SCB("has_bit", sdi_has_bit, g), SCB("oneof", sdi_oneof, g),
		     SCB("required", sdi_required, g), SCB("split", mg_nothing_cb, NULL));
		EMIT0(p, "\n");
	}
	pfg_generate_parse_table_helper_definition(g->parse_function_generator, p);
	EMIT0(p, "\n");
	mg_generate_constexpr_constructor(g, p);
	EMIT0(p, "\n"
		" inline void* $nonnull$ $Msg$::PlacementNew_(\n"
		"     //~\n"
		"     const void* $nonnull$, void* $nonnull$ mem,\n"
		"     $pb$::Arena* $nullable$ arena) {\n"
		"   return ::new (mem) $Msg$(arena);\n"
		" }\n");
	mg_generate_new_op(g, p);
	mg_generate_internal_generate_class_data(g, p);
	bool is_fdp = is_file_descriptor_proto(d->file, &g->options);
	EMIT(p, "\n"
		" struct $globals_type$ : ::_pbi::MessageGlobalsBase {\n"
		"   $constexpr$ $globals_type$()\n"
		"       :\n"
		"#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		"         _default(::_pbi::ConstantInitialized{},\n"
		"                  $Msg$_class_data_.base())\n"
		"#else   // !PROTOBUF_MESSAGE_GLOBALS\n"
		"         MessageGlobalsBase($Msg$::InternalGenerateClassData_(\n"
		"             _default, &$globals$._table.header)),\n"
		"         _default(::_pbi::ConstantInitialized{}, GetClassData()),\n"
		"         _table(::_pbi::PrivateAccess::GenerateParseTable<$Msg$>(\n"
		"             GetClassData()))\n"
		"#endif  // PROTOBUF_MESSAGE_GLOBALS\n"
		"   {\n"
		"   }\n"
		"   //~ File descriptor proto only initializer.\n"
		"   $file_descriptor_proto_init$;\n"
		"   ~$globals_type$() {}\n"
		"   //~ _default must be the first member.\n"
		"   union {\n"
		"     alignas(::_pbi::kMaxMessageAlignment) $Msg$ _default;\n"
		"   };\n"
		"#ifdef PROTOBUF_MESSAGE_GLOBALS\n"
		"   decltype(::_pbi::PrivateAccess::GenerateParseTable<$Msg$>(\n"
		"       ::std::declval<const ::_pbi::ClassData*>())) _table;\n"
		"#endif\n"
		"   //~ Implicit weak descriptor depends on \"tail\" at the end of the\n"
		"   //~ struct.\n"
		"   $implicit_weak_descriptor_tail$;\n"
		" };\n"
		"#ifdef PROTOBUF_MESSAGE_GLOBALS\n"
		" static_assert(PROTOBUF_FIELD_OFFSET($globals_type$, _default) ==\n"
		"               ::_pbi::MessageGlobalsBase::OffsetToDefault());\n"
		"#endif  // PROTOBUF_MESSAGE_GLOBALS\n"
		"\n"
		" PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT$ dllexport_decl$\n"
		"     PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 $const $$globals_type$ $globals$\n"
		"         $SECTION$;\n"
		"#if defined(PROTOBUF_CUSTOM_VTABLE)\n"
		" namespace {\n"
		" const ::_pbi::ClassData* $Msg$_get_class_data() {\n"
		"#ifdef PROTOBUF_MESSAGE_GLOBALS\n"
		"   return $globals$.GetClassData();\n"
		"#else\n"
		"   return $Msg$_class_data_.base();\n"
		"#endif  // PROTOBUF_MESSAGE_GLOBALS\n"
		" }\n"
		" }  // namespace\n"
		"#endif  // PROTOBUF_CUSTOM_VTABLE\n", SCB("constexpr", sdi_constexpr, g),
	     SCB("file_descriptor_proto_init", sdi_file_descriptor_proto_init, g),
	     SCB("implicit_weak_descriptor_tail", mg_nothing_cb, NULL), SCBSUF("SECTION", sdi_section, g, ""),
	     S_("const", is_fdp ? "" : "PROTOBUF_MESSAGE_GLOBALS_CONST"));
	if (g->options.lite_implicit_weak_fields)
		EMIT(p, "\n"
		" PROTOBUF_CONSTINIT const void* $ptr$ = &$globals$;\n", S_("ptr", msg_globals_instance_ptr(d, &g->options)),
		     S_("name", msg_globals_instance_name(d, &g->options)));
	POP_VARS(p);
	POP_VARS(p);
}
