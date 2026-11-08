// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/cord_field.cc (protobuf v36.2):
 * CordFieldGenerator and CordOneofFieldGenerator. */

static void set_cord_variables(FieldGenerator *g)
{
	const LpField *d = g->field;
	const Options *o = g->options;
	Subs *v = &g->variables;
	subs_set(v, "default", xcat("\"", lp_cescape(d->default_value_string), "\""));
	subs_set(v, "default_length", cpp_utoa(d->default_value_string.n));
	subs_set(v, "full_name", d->full_name);
	subs_set(v, "default_variable_name", make_default_name(d));
	subs_set(v, "default_variable_field", make_default_field_name(d));
	subs_set(v, "default_variable",
		 d->default_value_string.n == 0
			 ? xcat("::", protobuf_namespace(o), "::internal::GetEmptyCordAlreadyInited()")
			 : xcat(qualified_class_name(d->containing_type, o), "::", make_default_field_name(d)));
}

static ArenaDtorNeeds cord_needs_arena_destructor(const FieldGenerator *g) { return ARENA_DTOR_REQUIRED; }

static void cord_member_constexpr_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (g->field->default_value_string.n == 0)
		EMIT0(p, "$name$_ {}");
	else
		EMIT(p,
		     "\n"
		     " $name$_ {\n"
		     "   ::absl::strings_internal::MakeStringConstant(\n"
		     "       $Msg$::Impl_::$Split$_default_$name$_func_{})\n"
		     " }\n",
		     S_("Split", should_split(g->field, g->options) ? "Split::" : ""));
}

static void cord_member_constructor(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (g->field->default_value_string.n == 0)
		EMIT0(p, "$name$_ {}");
	else
		EMIT0(p, "$name$_ { ::absl::string_view($default$, $default_length$) }\n"
			 "      ");
	POP_VARS(p);
}

static void cord_member_copy_constructor(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "$name$_ { from.$name$_ }");
	POP_VARS(p);
}

static void cord_oneof_copy_construct(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " $field_$ = $pb$::Arena::Create<absl::Cord>(arena, *from.$field_$);\n");
	POP_VARS(p);
}

static void cord_private_members(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " ::absl::Cord $name$_;\n");
	if (g->field->default_value_string.n)
		EMIT0(p, "\n"
			 " struct _default_$name$_func_ {\n"
			 "   constexpr ::absl::string_view operator()() const {\n"
			 "     return ::absl::string_view($default$, $default_length$);\n"
			 "   }\n"
			 " };\n");
	POP_VARS(p);
}

static const char *const kCordAccessorPrefixes[] = {"", "_internal_", "_internal_mutable_", "_internal_set_"};

static void cord_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	Subs v = {0}, vs = {0}, va = {0};
	annotated_accessors(&v, g->field, kCordAccessorPrefixes, 4, 0);
	annotated_accessors(&vs, g->field, kRepAccessorPrefixesSet, 2, SEM_SET);
	annotated_accessors(&va, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	PUSH_SUBS(p, &va);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ const ::absl::Cord& $name$() const;\n"
		 " $DEPRECATED$void $set_name$(const ::absl::Cord& value);\n"
		 " $DEPRECATED$void $set_name$(::absl::string_view value);\n"
		 "\n"
		 " private:\n"
		 " const ::absl::Cord& $_internal_name$() const;\n"
		 " void $_internal_set_name$(const ::absl::Cord& value);\n"
		 " ::absl::Cord* $nonnull$ $_internal_mutable_name$();\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void cord_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " inline const ::absl::Cord& $Msg$::_internal_$name_internal$() const {\n"
		 "   return $field_$;\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline const ::absl::Cord& $Msg$::$name$() const\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$full_name$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::_internal_set_$name_internal$(\n"
		 "     const ::absl::Cord& value) {\n"
		 "   $set_hasbit$;\n"
		 "   $field_$ = value;\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::set_$name$(const ::absl::Cord& value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $PrepareSplitMessageForWrite$;\n"
		 "   _internal_set_$name_internal$(value);\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_set:$full_name$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::set_$name$(::absl::string_view value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $PrepareSplitMessageForWrite$;\n"
		 "   $set_hasbit$;\n"
		 "   $field_$ = value;\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_set_string_piece:$full_name$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline ::absl::Cord* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
		 "   $set_hasbit$;\n"
		 "   return &$field_$;\n"
		 " }\n");
	POP_VARS(p);
}

static void cord_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (g->field->default_value_string.n == 0)
		EMIT0(p, "\n"
			 " $field_$.Clear();\n");
	else
		EMIT0(p, "\n"
			 " $field_$ = ::absl::string_view($default$, $default_length$);\n");
	POP_VARS(p);
}

static void cord_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " _this->_internal_set_$name$(from._internal_$name$());\n");
	POP_VARS(p);
}

static void cord_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " $field_$.swap(other->$field_$);\n");
	POP_VARS(p);
}

static void cord_arena_destructor_code(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " _this->$field_$.::absl::Cord::~Cord();\n");
	POP_VARS(p);
}

static void cord_serialize(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (g->field->type == PT_STRING)
		generate_utf8_check_code_for_cord(p, g->field, g->options, false,
						  xcat("this_._internal_", lp_lookup_var(p, "name"), "(), "));
	EMIT0(p, "\n"
		 " target =\n"
		 "     stream->Write$DeclaredType$($number$, this_._internal_$name$(), target);\n");
	POP_VARS(p);
}

static void cord_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$ + $pbi$::WireFormatLite::$DeclaredType$Size(\n"
		 "                                 this_._internal_$name$());\n");
	POP_VARS(p);
}

static void cord_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->field->default_value_string.n == 0)
		EMIT0(p, "\n"
			 " /*decltype($field_$)*/ {},\n");
	else
		EMIT(p,
		     "\n"
		     " /*decltype($field_$)*/ {::absl::strings_internal::MakeStringConstant(\n"
		     "     $Msg$::Impl_::$Split$_default_$name$_func_{})},\n",
		     S_("Split", g->should_split ? "Split::" : ""));
}

static void cord_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " decltype(Impl_::Split::$name$_){},\n");
	else
		EMIT0(p, "\n"
			 " decltype($field_$){},\n");
}

static const FieldGeneratorVtbl kCordField = {
	.generate_private_members = cord_private_members,
	.generate_accessor_declarations = cord_accessor_declarations,
	.generate_inline_accessor_definitions = cord_inline_accessor_definitions,
	.generate_clearing_code = cord_clearing_code,
	.generate_merging_code = cord_merging_code,
	.generate_swapping_code = cord_swapping_code,
	.generate_arena_destructor_code = cord_arena_destructor_code,
	.generate_serialize_with_cached_sizes_to_array = cord_serialize,
	.generate_byte_size = cord_byte_size,
	.generate_aggregate_initializer = cord_aggregate_initializer,
	.generate_constexpr_aggregate_initializer = cord_constexpr_aggregate_initializer,
	.needs_arena_destructor = cord_needs_arena_destructor,
	.generate_member_constexpr_constructor = cord_member_constexpr_constructor,
	.generate_member_constructor = cord_member_constructor,
	.generate_member_copy_constructor = cord_member_copy_constructor,
	.generate_oneof_copy_construct = cord_oneof_copy_construct,
};

static void make_singular_cord_generator(FieldGenerator *g)
{
	g->vt = &kCordField;
	set_cord_variables(g);
}

/* ---- CordOneofFieldGenerator ---- */

static void cord1_private_members(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " ::absl::Cord* $nonnull$ $name$_;\n");
	POP_VARS(p);
}

static void cord1_static_members(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (g->field->default_value_string.n)
		EMIT0(p, "\n"
			 " struct _default_$name$_func_ {\n"
			 "   constexpr ::absl::string_view operator()() const {\n"
			 "     return ::absl::string_view($default$, $default_length$);\n"
			 "   }\n"
			 " };\n"
			 " static const ::absl::Cord $default_variable_name$;\n");
	POP_VARS(p);
}

static void cord1_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " inline const ::absl::Cord& $Msg$::_internal_$name_internal$() const {\n"
		 "   if ($has_field$) {\n"
		 "     return *$field_$;\n"
		 "   }\n"
		 "   return $default_variable$;\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline const ::absl::Cord& $Msg$::$name$() const\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$full_name$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::set_$name$(const ::absl::Cord& value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   if ($not_has_field$) {\n"
		 "     clear_$oneof_name$();\n"
		 "     set_has_$name_internal$();\n"
		 "     $field_$ = $pb$::Arena::Create<::absl::Cord>(GetArena());\n"
		 "   }\n"
		 "   *$field_$ = value;\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_set:$full_name$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::set_$name$(::absl::string_view value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   if ($not_has_field$) {\n"
		 "     clear_$oneof_name$();\n"
		 "     set_has_$name_internal$();\n"
		 "     $field_$ = $pb$::Arena::Create<::absl::Cord>(GetArena());\n"
		 "   }\n"
		 "   *$field_$ = value;\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_set_string_piece:$full_name$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline ::absl::Cord* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
		 "   if ($not_has_field$) {\n"
		 "     clear_$oneof_name$();\n"
		 "     set_has_$name_internal$();\n"
		 "     $field_$ = $pb$::Arena::Create<::absl::Cord>(GetArena());\n"
		 "   }\n"
		 "   return $field_$;\n"
		 " }\n");
	POP_VARS(p);
}

static void cord1_non_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (g->field->default_value_string.n)
		EMIT0(p, "\n"
			 " PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT const ::absl::Cord\n"
			 "     $Msg$::$default_variable_field$(\n"
			 "         ::absl::strings_internal::MakeStringConstant(\n"
			 "             _default_$name$_func_{}));\n");
	POP_VARS(p);
}

static void cord1_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 " if (GetArena() == nullptr) {\n"
		 "   delete $field_$;\n"
		 " }\n");
	POP_VARS(p);
}

static void cord1_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " if (oneof_needs_init) {\n"
		 "   _this->$field_$ = $pb$::Arena::Create<absl::Cord>(arena);\n"
		 " }\n"
		 " *_this->$field_$ = *from.$field_$;\n");
}

static ArenaDtorNeeds cord1_needs_arena_destructor(const FieldGenerator *g) { return ARENA_DTOR_NONE; }

static const FieldGeneratorVtbl kCordOneofField = {
	.generate_private_members = cord1_private_members,
	.generate_static_members = cord1_static_members,
	.generate_accessor_declarations = cord_accessor_declarations,
	.generate_inline_accessor_definitions = cord1_inline_accessor_definitions,
	.generate_non_inline_accessor_definitions = cord1_non_inline_accessor_definitions,
	.requires_arena = fg_true,
	.generate_clearing_code = cord1_clearing_code,
	.generate_merging_code = cord1_merging_code,
	.generate_swapping_code = fg_nothing,
	.generate_arena_destructor_code = fg_nothing,
	.generate_serialize_with_cached_sizes_to_array = cord_serialize,
	.generate_byte_size = cord_byte_size,
	.generate_aggregate_initializer = cord_aggregate_initializer,
	.generate_constexpr_aggregate_initializer = cord_constexpr_aggregate_initializer,
	.needs_arena_destructor = cord1_needs_arena_destructor,
	.generate_member_constexpr_constructor = cord_member_constexpr_constructor,
	.generate_member_constructor = cord_member_constructor,
	.generate_member_copy_constructor = cord_member_copy_constructor,
	.generate_oneof_copy_construct = cord_oneof_copy_construct,
};

static void make_oneof_cord_generator(FieldGenerator *g)
{
	g->vt = &kCordOneofField;
	set_cord_variables(g);
}
