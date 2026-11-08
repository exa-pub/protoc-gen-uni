// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field.{h,cc} (protobuf v36.2): FieldVars,
 * FieldGeneratorBase (its default methods), FieldGenerator (the variable frames pushed around
 * every call) and FieldGeneratorTable. The subclasses are in field_generators/. */

static void field_vars(Subs *vars, const LpField *f, const Options *opts)
{
	bool split = should_split(f, opts);
	subs_push(vars, S_("name", field_name(f)));
	subs_push(vars, S_("name_internal", field_name(f)));
	subs_push(vars, SINT("index", f->index));
	subs_push(vars, SINT("number", f->number));
	subs_push(vars, S_("pkg.Msg.field", f->full_name));
	subs_push(vars, S_("field_", field_member_name(f, split)));
	subs_push(vars, S_("DeclaredType", declared_type_method_name(f->type)));
	subs_push(vars, S_("Oneof", real_oneof(f) ? "Oneof" : ""));
	subs_push(vars, S_("Utf8", is_strict_utf8_string(f, opts) ? "Utf8" : "Raw"));
	subs_push(vars, S_("StrType", is_strict_utf8_string(f, opts) ? "String" : "Bytes"));
	subs_push(vars, SINT("kTagBytes", wire_format_tag_size(f->number, f->type)));
	subs_push(vars, SSUF("PrepareSplitMessageForWrite", split ? "PrepareSplitMessageForWrite();" : "", ";"));
	subs_push(vars, SSUF("DEPRECATED", deprecated_attribute(opts, f), " "));
	subs_push(vars, S_("{", ""));
	subs_push(vars, S_("}", ""));
	subs_push(vars, S_("TsanDetectConcurrentMutation", xcat("::", protobuf_namespace(opts), "::internal::TSanWrite(&_impl_)")));
	subs_push(vars, S_("TsanDetectConcurrentRead", xcat("::", protobuf_namespace(opts), "::internal::TSanRead(&_impl_)")));
	subs_push(vars, S_("ns", namespace_of_file(f->file)));
	subs_push(vars, SSUF("WeakDescriptorSelfPin",
			     using_implicit_weak_descriptor(f->file, opts)
				     ? xcat(strong_reference_to_type(f->containing_type, opts), ";")
				     : "",
			     ";"));
	if (f->containing_oneof) {
		const LpOneof *oneof = f->containing_oneof;
		char *fname = underscores_to_camel_case(f->name, true);
		subs_push(vars, S_("oneof_name", oneof->name));
		subs_push(vars, S_("field_name", fname));
		subs_push(vars, SINT("oneof_index", oneof->index));
		subs_push(vars, S_("has_field", xcat(oneof->name, "_case() == k", fname)));
		subs_push(vars, S_("not_has_field", xcat(oneof->name, "_case() != k", fname)));
	}
}

/* FieldGeneratorBase::FieldGeneratorBase */
static void field_generator_base_init(FieldGenerator *g, const LpField *f, const Options *o)
{
	g->field = f;
	g->options = o;
	bool is_repeated_or_map = is_repeated(f);
	g->should_split = should_split(f, o);
	g->is_oneof = real_oneof(f) != NULL;
	switch (cpp_type(f)) {
	case LP_CPPTYPE_STRING:
		g->is_string = true;
		g->is_inlined = is_string_inlined(f, o);
		g->is_bytes = f->type == PT_BYTES;
		g->has_default_constexpr_constructor = is_repeated_or_map;
		break;
	case LP_CPPTYPE_MESSAGE:
		g->is_message = true;
		g->is_group = f->type == PT_GROUP;
		g->is_foreign = is_cross_file_message(f);
		g->is_weak = is_implicit_weak_field(f, o);
		g->is_lazy = is_lazy(f, o);
		g->has_trivial_value = !(is_repeated_or_map || g->is_lazy);
		g->has_default_constexpr_constructor = is_repeated_or_map || g->is_lazy;
		break;
	default:
		g->is_trivial = g->has_trivial_value = !is_repeated_or_map;
		g->has_default_constexpr_constructor = is_repeated_or_map;
		break;
	}
	g->has_trivial_zero_default = can_initialize_by_zeroing(f, o);
	g->has_brace_default_assign = g->has_trivial_zero_default && !g->is_lazy;
}

/* InternalMetadataOffsetSub */
static void internal_metadata_offset_cb(LpPrinter *p, void *arg)
{
	EMIT0(p, "\n"
		 " ::_pbi::InternalMetadataOffset::Build<\n"
		 "     $classtype$,\n"
		 "     PROTOBUF_FIELD_OFFSET($classtype$, _impl_.$name$_)>()\n");
}
#define INTERNAL_METADATA_OFFSET_SUB SCBSUF("internal_metadata_offset", internal_metadata_offset_cb, NULL, "")

static void base_generate_member_constexpr_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (is_repeated(g->field) || is_map(g->field))
		EMIT(p,
		     "\n"
		     " $name$_ { visibility, $internal_metadata_offset$ }\n",
		     INTERNAL_METADATA_OFFSET_SUB);
	else
		EMIT(p, "$name$_{$default$}", S_("default", default_value(g->options, g->field)));
}

static void base_generate_member_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (is_repeated(g->field) || is_map(g->field)) {
		if (should_split(g->field, g->options))
			EMIT0(p, "$name$_{}");
		else
			EMIT(p,
			     "\n"
			     " $name$_ { visibility, $internal_metadata_offset$ }\n",
			     INTERNAL_METADATA_OFFSET_SUB);
	} else {
		EMIT(p, "$name$_{$default$}", S_("default", default_value(g->options, g->field)));
	}
}

static void base_generate_member_copy_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (is_repeated(g->field) || is_map(g->field))
		EMIT(p,
		     "\n"
		     " $name$_ {\n"
		     "   visibility, $internal_metadata_offset$, arena, from.$name$_\n"
		     " }\n",
		     INTERNAL_METADATA_OFFSET_SUB);
	else
		EMIT0(p, "$name$_{from.$name$_}");
}

static void base_generate_oneof_copy_construct(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "$field_$ = from.$field_$;\n");
}




static void base_generate_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		FORMAT(p, &g->variables, "$field_$ = from.$field_$;\n");
}

/* field_generators/generators.h */
static void make_map_generator(FieldGenerator *g);
static void make_repeated_message_generator(FieldGenerator *g);
static void make_repeated_string_view_generator(FieldGenerator *g);
static void make_repeated_string_generator(FieldGenerator *g);
static void make_repeated_enum_generator(FieldGenerator *g);
static void make_repeated_primitive_generator(FieldGenerator *g);
static void make_oneof_message_generator(FieldGenerator *g);
static void make_singular_message_generator(FieldGenerator *g);
static void make_singular_enum_generator(FieldGenerator *g);
static void make_singular_string_view_generator(FieldGenerator *g);
static void make_oneof_cord_generator(FieldGenerator *g);
static void make_singular_cord_generator(FieldGenerator *g);
static void make_singular_string_generator(FieldGenerator *g);
static void make_singular_primitive_generator(FieldGenerator *g);

/* MakeGenerator */
static void make_generator(FieldGenerator *g)
{
	const LpField *f = g->field;
	if (is_map(f)) {
		make_map_generator(g);
		return;
	}
	if (is_repeated(f)) {
		switch (cpp_type(f)) {
		case LP_CPPTYPE_MESSAGE:
			make_repeated_message_generator(g);
			return;
		case LP_CPPTYPE_STRING:
			if (cpp_string_type(f) == CPP_STR_VIEW)
				make_repeated_string_view_generator(g);
			else
				make_repeated_string_generator(g);
			return;
		case LP_CPPTYPE_ENUM:
			make_repeated_enum_generator(g);
			return;
		default:
			make_repeated_primitive_generator(g);
			return;
		}
	}
	if (real_oneof(f) && cpp_type(f) == LP_CPPTYPE_MESSAGE) {
		make_oneof_message_generator(g);
		return;
	}
	switch (cpp_type(f)) {
	case LP_CPPTYPE_MESSAGE:
		make_singular_message_generator(g);
		return;
	case LP_CPPTYPE_ENUM:
		make_singular_enum_generator(g);
		return;
	case LP_CPPTYPE_STRING:
		switch (cpp_string_type(f)) {
		case CPP_STR_VIEW:
			make_singular_string_view_generator(g);
			return;
		case CPP_STR_CORD:
			if (f->type == PT_BYTES) {
				if (real_oneof(f))
					make_oneof_cord_generator(g);
				else
					make_singular_cord_generator(g);
				return;
			}
			make_singular_string_generator(g);
			return;
		default:
			make_singular_string_generator(g);
			return;
		}
	default:
		make_singular_primitive_generator(g);
		return;
	}
}

static void has_bit_vars(const LpField *f, const Options *o, int idx, Subs *vars)
{
	if (idx < 0) {
		subs_push(vars, SSUF("set_hasbit", "", ";"));
		subs_push(vars, SSUF("clear_hasbit", "", ";"));
		subs_push(vars, S_("exclude_mask", "0xFFFFFFFFU"));
		return;
	}
	int index = idx / 32;
	char *mask = xfmt("0x%08xU", 1u << (idx % 32));
	const char *has_bits = is_map_entry_message(f->containing_type) ? "_has_bits_" : "_impl_._has_bits_";
	char *has_bits_array = xcat(has_bits, "[", cpp_itoa(index), "]");
	subs_push(vars, S_("has_bits_array", has_bits_array));
	subs_push(vars, S_("has_mask", mask));
	subs_push(vars, S_("has_hasbit", xcat("CheckHasBit(", has_bits_array, ", ", mask, ")")));
	subs_push(vars, SSUF("set_hasbit", xcat("SetHasBit(", has_bits_array, ", ", mask, ");"), ";"));
	subs_push(vars, SSUF("clear_hasbit", xcat("ClearHasBit(", has_bits_array, ", ", mask, ");"), ";"));
	subs_push(vars, S_("exclude_mask", xfmt("0x%08xU", ~(1u << (idx % 32)))));
}

/* FieldGenerator::FieldGenerator */
static void field_generator_init(FieldGenerator *g, const LpField *f, const Options *o, int hasbit_index)
{
	field_generator_base_init(g, f, o);
	make_generator(g);
	field_vars(&g->field_vars, f, o);
	g->tracker_vars = make_tracker_calls_field(f, o);
	if (g->vt->make_vars)
		g->vt->make_vars(g, &g->per_generator_vars);
	has_bit_vars(f, o, hasbit_index, &g->field_vars);
}

/* FieldGeneratorTable::Build */
static void field_generator_table_build(FieldGeneratorTable *t, const LpMessage *d, const Options *o,
					const FieldLayout *l)
{
	t->descriptor = d;
	t->n = d->n_fields;
	t->fields = xalloc(sizeof(FieldGenerator) * (size_t)(d->n_fields + 1));
	for (int i = 0; i < d->n_fields; i++)
		field_generator_init(&t->fields[i], &d->fields[i], o, layout_has_bit_index(l, &d->fields[i]));
}

static const FieldGenerator *fg_get(const FieldGeneratorTable *t, const LpField *f)
{
	if (f->containing_type != t->descriptor)
		fail("Check failed: field->containing_type() == descriptor_");
	return &t->fields[f->index];
}

/* PushVarsForCall */
static void fg_push(const FieldGenerator *g, LpPrinter *p)
{
	lp_push_vars(p, g->field_vars.v, g->field_vars.n);
	lp_push_vars(p, g->tracker_vars.v, g->tracker_vars.n);
	lp_push_vars(p, g->per_generator_vars.v, g->per_generator_vars.n);
}

static void fg_pop(LpPrinter *p)
{
	lp_pop_vars(p);
	lp_pop_vars(p);
	lp_pop_vars(p);
}

#define FG_CALL(name, deflt)                                                                       \
	static void fg_##name(const FieldGenerator *g, LpPrinter *p)                               \
	{                                                                                          \
		fg_push(g, p);                                                                     \
		if (g->vt->name)                                                                   \
			g->vt->name(g, p);                                                         \
		else                                                                               \
			deflt(g, p);                                                               \
		fg_pop(p);                                                                         \
	}

static void fg_nothing(const FieldGenerator *g, LpPrinter *p) {}
static void fg_default_message_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	g->vt->generate_clearing_code(g, p);
}

FG_CALL(generate_private_members, fg_nothing)
FG_CALL(generate_secondary_private_members, fg_nothing)
FG_CALL(generate_static_members, fg_nothing)
FG_CALL(generate_member_constructor, base_generate_member_constructor)
FG_CALL(generate_member_copy_constructor, base_generate_member_copy_constructor)
FG_CALL(generate_oneof_copy_construct, base_generate_oneof_copy_construct)
FG_CALL(generate_member_constexpr_constructor, base_generate_member_constexpr_constructor)
FG_CALL(generate_accessor_declarations, fg_nothing)
FG_CALL(generate_inline_accessor_definitions, fg_nothing)
FG_CALL(generate_non_inline_accessor_definitions, fg_nothing)
FG_CALL(generate_clearing_code, fg_nothing)
FG_CALL(generate_message_clearing_code, fg_default_message_clearing_code)
FG_CALL(generate_merging_code, fg_nothing)
FG_CALL(generate_copy_constructor_code, base_generate_copy_constructor_code)
FG_CALL(generate_swapping_code, fg_nothing)
FG_CALL(generate_destructor_code, fg_nothing)
FG_CALL(generate_arena_destructor_code, fg_nothing)
FG_CALL(generate_serialize_with_cached_sizes_to_array, fg_nothing)
FG_CALL(generate_byte_size, fg_nothing)
FG_CALL(generate_is_initialized, fg_nothing)

static bool fg_requires_arena(const FieldGenerator *g) { return g->vt->requires_arena ? g->vt->requires_arena(g) : false; }
static bool fg_needs_is_initialized(const FieldGenerator *g)
{
	return g->vt->needs_is_initialized ? g->vt->needs_is_initialized(g) : false;
}
static ArenaDtorNeeds fg_needs_arena_destructor(const FieldGenerator *g)
{
	return g->vt->needs_arena_destructor ? g->vt->needs_arena_destructor(g) : ARENA_DTOR_NONE;
}
static bool fg_has_trivial_copy(const FieldGenerator *g) { return g->is_trivial; }
