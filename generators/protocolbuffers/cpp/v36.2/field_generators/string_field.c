// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/string_field.cc (protobuf v36.2):
 * SingularString and RepeatedString. */

static void string_vars(const FieldGenerator *g, Subs *out)
{
	const LpField *f = g->field;
	const Options *o = g->options;
	char *trivial_default = xcat("::", protobuf_namespace(o), "::internal::GetEmptyStringAlreadyInited()");
	char *lazy_var = xcat(qualified_class_name(f->containing_type, o), "::", make_default_field_name(f));
	bool empty_default = f->default_value_string.n == 0;
	bool bytes = f->type == PT_BYTES;
	subs_push(out, S_("kDefault", default_value(o, f)));
	subs_push(out, SUINT("kDefaultLen", f->default_value_string.n));
	subs_push(out, S_("default_variable_name", make_default_name(f)));
	subs_push(out, S_("default_variable_field", make_default_field_name(f)));
	subs_push(out, S_("kDefaultStr", !empty_default ? xcat(lazy_var, ".get()") : trivial_default));
	subs_push(out, S_("kDefaultValue", !empty_default ? "nullptr" : xcat("&", trivial_default)));
	subs_push(out, S_("lazy_var", lazy_var));
	subs_push(out, SSUF("lazy_args", !empty_default ? xcat(lazy_var, ",") : "", ","));
	subs_push(out, S_("Set", bytes ? "SetBytes" : "Set"));
}

static bool ss_empty_default(const FieldGenerator *g) { return g->field->default_value_string.n == 0; }

static bool ss_is_inlined(const FieldGenerator *g) { return g->is_inlined; }

static void ss_private_members(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " $pbi$::$Str$ $name$_;\n",
	     S_("Str", g->is_inlined ? "InlinedStringField" : "ArenaStringPtr"));
}

static bool ss_requires_arena(const FieldGenerator *g) { return g->is_oneof; }

static void ss_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof)
		EMIT0(p, "\n"
			 " if (oneof_needs_init) {\n"
			 "   _this->$field_$.InitDefault();\n"
			 " }\n"
			 " _this->$field_$.Set(from._internal_$name$(), arena);\n");
	else
		EMIT0(p, "\n"
			 " _this->_internal_set_$name$(from._internal_$name$());\n");
}

static void ss_non_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	if (ss_empty_default(g))
		return;
	EMIT0(p, "\n"
		 " /*static*/ const ::_pbi::LazyString $Msg$::$default_variable_field${\n"
		 "     {{$kDefault$, $kDefaultLen$}},\n"
		 "     {nullptr},\n"
		 " };\n");
}

static void ss_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$ + $pbi$::WireFormatLite::$DeclaredType$Size(\n"
		 "                                 this_._internal_$name$());\n");
}

static void ss_member_constexpr_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined)
		EMIT0(p, "$name$_{}");
	else
		EMIT0(p, "$name$_(\n"
			 "    &$pbi$::fixed_address_empty_string,\n"
			 "    ::_pbi::ConstantInitialized())");
}

static void ss_member_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined)
		EMIT0(p, "$name$_{}");
	else if (ss_empty_default(g))
		EMIT0(p, "$name$_(arena)");
	else
		EMIT0(p, "$name$_(arena, $default_variable_field$)");
}

static void ss_member_copy_constructor(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined || ss_empty_default(g))
		EMIT0(p, "$name$_(arena, from.$name$_)");
	else
		EMIT0(p, "$name$_(arena, from.$name$_, $default_variable_name$)");
}

static void ss_oneof_copy_construct(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined || ss_empty_default(g))
		EMIT0(p, "new (&$field_$) decltype($field_$){arena, from.$field_$};\n");
	else
		EMIT0(p, "new (&$field_$) decltype($field_$){arena, from.$field_$, $default_variable_field$};\n");
}

static void ss_static_members(const FieldGenerator *g, LpPrinter *p)
{
	if (!ss_empty_default(g))
		EMIT0(p, "\n"
			 " static const $pbi$::LazyString $default_variable_name$;\n");
}

static const char *const kStrAccessorPrefixes[] = {"", "set_allocated_"};
static const char *const kStrAccessorPrefixesSet[] = {"set_"};
static const char *const kStrAccessorPrefixesAlias[] = {"mutable_"};

static void ss_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	if (is_string_field_with_privatized_accessors(g->field))
		EMIT0(p, "\n"
			 " private:  // Hidden due to unknown ctype option.\n");
	Subs v1 = {0}, v2 = {0}, v3 = {0};
	annotated_accessors(&v1, g->field, kStrAccessorPrefixes, 2, 0);
	subs_push(&v1, SANN("release_name", safe_function_name(g->field->containing_type, g->field, "release_"), g->field));
	annotated_accessors(&v2, g->field, kStrAccessorPrefixesSet, 1, SEM_SET);
	annotated_accessors(&v3, g->field, kStrAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	PUSH_SUBS(p, &v3);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ const ::std::string& $name$() const;\n"
		 " //~ Using `Arg_ = const std::string&` will make the type of `arg`\n"
		 " //~ default to `const std::string&`, due to reference collapse. This\n"
		 " //~ is necessary because there are a handful of users that rely on\n"
		 " //~ this default.\n"
		 " template <typename Arg_ = const ::std::string&, typename... Args_>\n"
		 " $DEPRECATED$ void $set_name$(Arg_&& arg, Args_... args);\n"
		 " $DEPRECATED$ ::std::string* $nonnull$ $mutable_name$();\n"
		 " $DEPRECATED$ [[nodiscard]] ::std::string* $nullable$ $release_name$();\n"
		 " $DEPRECATED$ void $set_allocated_name$(::std::string* $nullable$ value);\n"
		 "\n"
		 " private:\n"
		 " const ::std::string& _internal_$name$() const;\n"
		 " PROTOBUF_ALWAYS_INLINE void _internal_set_$name$(const ::std::string& value);\n"
		 " ::std::string* $nonnull$ _internal_mutable_$name$();\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

/* UpdateHasbitSet */
static void update_hasbit_set(LpPrinter *p, bool is_oneof)
{
	if (!is_oneof) {
		EMIT0(p, "\n"
			 " $set_hasbit$;\n");
		return;
	}
	EMIT0(p, "\n"
		 " if ($not_has_field$) {\n"
		 "   clear_$oneof_name$();\n"
		 "\n"
		 "   set_has_$name_internal$();\n"
		 "   $field_$.InitDefault();\n"
		 " }\n");
}

static void ss_release_impl(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->is_oneof) {
		EMIT0(p, "\n"
			 " if ($not_has_field$) {\n"
			 "   return nullptr;\n"
			 " }\n"
			 " clear_has_$oneof_name$();\n"
			 " return $field_$.Release();\n");
		return;
	}
	if (!has_hasbit(g->field, g->options)) {
		EMIT0(p, "\n"
			 " return $field_$.Release();\n");
		return;
	}
	if (g->is_inlined) {
		EMIT0(p, "\n"
			 " if (!$has_hasbit$) {\n"
			 "   return nullptr;\n"
			 " }\n"
			 " $clear_hasbit$;\n"
			 "\n"
			 " return $field_$.Release(GetArena());\n");
		return;
	}
	EMIT0(p, "\n"
		 " if (!$has_hasbit$) {\n"
		 "   return nullptr;\n"
		 " }\n"
		 " $clear_hasbit$;\n");
	if (!ss_empty_default(g)) {
		EMIT0(p, "\n"
			 " return $field_$.Release();\n");
		return;
	}
	EMIT0(p, "\n"
		 " auto* released = $field_$.Release();\n"
		 " if ($pbi$::DebugHardenForceCopyDefaultString()) {\n"
		 "   $field_$.Set(\"\", GetArena());\n"
		 " }\n"
		 " return released;\n");
}

static void ss_set_allocated_impl(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->is_oneof) {
		EMIT0(p, "\n"
			 " if (has_$oneof_name$()) {\n"
			 "   clear_$oneof_name$();\n"
			 " }\n"
			 " if (value != nullptr) {\n"
			 "   set_has_$name_internal$();\n"
			 "   $field_$.InitAllocated(value, GetArena());\n"
			 " }\n");
		return;
	}
	if (has_hasbit(g->field, g->options))
		EMIT0(p, "\n"
			 " if (value != nullptr) {\n"
			 "   $set_hasbit$\n"
			 " } else {\n"
			 "   $clear_hasbit$\n"
			 " }\n");
	EMIT0(p, "\n"
		 " $field_$.SetAllocated(value, GetArena());\n");
	if (g->is_inlined)
		return;
	if (ss_empty_default(g))
		EMIT0(p, "\n"
			 " if ($pbi$::DebugHardenForceCopyDefaultString() && $field_$.IsDefault()) {\n"
			 "   $field_$.Set(\"\", GetArena());\n"
			 " }\n");
}

static void ss_if_is_default(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (ss_empty_default(g) || g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " if ($field_$.IsDefault()) {\n"
		 "   return $default_variable_field$.get();\n"
		 " }\n");
}

static void ss_update_hasbit(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	update_hasbit_set(p, g->is_oneof);
}

static void ss_check_hasbit(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (!g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " if ($not_has_field$) {\n"
		 "   return $kDefaultStr$;\n"
		 " }\n");
}

static void ss_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " inline const ::std::string& $Msg$::$name$() const\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_get$;\n"
	     "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
	     "   $if_IsDefault$;\n"
	     "   return _internal_$name_internal$();\n"
	     " }\n"
	     " template <typename Arg_, typename... Args_>\n"
	     " PROTOBUF_ALWAYS_INLINE void $Msg$::set_$name$(Arg_&& arg, Args_... args) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   $update_hasbit$;\n"
	     "   $field_$.$Set$(static_cast<Arg_&&>(arg), args..., GetArena());\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
	     " }\n"
	     " inline ::std::string* $nonnull$ $Msg$::mutable_$name$()\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   $update_hasbit$;\n"
	     "   ::std::string* _s = _internal_mutable_$name_internal$();\n"
	     "   $annotate_mutable$;\n"
	     "   // @@protoc_insertion_point(field_mutable:$pkg.Msg.field$)\n"
	     "   return _s;\n"
	     " }\n"
	     " inline const ::std::string& $Msg$::_internal_$name_internal$() const {\n"
	     "   $TsanDetectConcurrentRead$;\n"
	     "   $check_hasbit$;\n"
	     "   return $field_$.Get();\n"
	     " }\n"
	     " inline void $Msg$::_internal_set_$name_internal$(const ::std::string& value) {\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   //~ Don't use $Set$ here; we always want the std::string variant\n"
	     "   //~ regardless of whether this is a `bytes` field.\n"
	     "   $field_$.Set(value, GetArena());\n"
	     " }\n"
	     " inline ::std::string* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   return $field_$.Mutable($lazy_args$, GetArena());\n"
	     " }\n"
	     " inline ::std::string* $nullable$ $Msg$::$release_name$() {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $annotate_release$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   // @@protoc_insertion_point(field_release:$pkg.Msg.field$)\n"
	     "   $release_impl$;\n"
	     " }\n"
	     " inline void $Msg$::set_allocated_$name$(::std::string* $nullable$ value) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   $set_allocated_impl$;\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set_allocated:$pkg.Msg.field$)\n"
	     " }",
	     SCB("if_IsDefault", ss_if_is_default, g), SCB("update_hasbit", ss_update_hasbit, g),
	     SCB("check_hasbit", ss_check_hasbit, g),
	     S_("release_name", safe_function_name(g->field->containing_type, g->field, "release_")),
	     SCB("release_impl", ss_release_impl, g), SCB("set_allocated_impl", ss_set_allocated_impl, g));
}

static void ss_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof) {
		EMIT0(p, "\n"
			 " $field_$.Destroy();\n");
		return;
	}
	if (ss_empty_default(g)) {
		EMIT0(p, "\n"
			 " $field_$.ClearToEmpty();\n");
		return;
	}
	EMIT0(p, "\n"
		 " $field_$.ClearToDefault($lazy_var$, GetArena());\n");
}

static void ss_message_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof) {
		EMIT0(p, "\n"
			 " $field_$.Destroy();\n");
		return;
	}
	if (g->is_inlined && has_hasbit(g->field, g->options))
		EMIT0(p, "\n"
			 " $DCHK$(!$field_$.IsDefault());\n");
	if (!ss_empty_default(g)) {
		EMIT0(p, "\n"
			 " this_.$field_$.ClearToDefault($lazy_var$, this_.GetArena());\n");
		return;
	}
	EMIT(p,
	     "\n"
	     " this_.$field_$.$Clear$();\n",
	     S_("Clear", has_hasbit(g->field, g->options) ? "ClearNonDefaultToEmpty" : "ClearToEmpty"));
}

static void ss_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof)
		return;
	if (!g->is_inlined) {
		EMIT0(p, "\n"
			 " ::_pbi::ArenaStringPtr::InternalSwap(&$field_$, &other->$field_$, arena);\n");
		return;
	}
	EMIT0(p, "\n"
		 " ::_pbi::InlinedStringField::InternalSwap(&$field_$, &other->$field_$,\n"
		 "                                          arena);\n");
}

static void ss_hazzer(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (has_hasbit(g->field, g->options))
		EMIT0(p, "CheckHasBit(from.$has_bits_array$, $has_mask$)");
	else
		EMIT0(p, "!from._internal_$name$().empty()");
}

static void ss_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (!(g->is_inlined && ss_empty_default(g)) && !g->is_oneof) {
		EMIT0(p, "\n"
			 " $field_$.InitDefault();\n");
		if (is_string(g->field) && ss_empty_default(g))
			EMIT0(p, "\n"
				 " if ($pbi$::DebugHardenForceCopyDefaultString()) {\n"
				 "   $field_$.Set(\"\", GetArena());\n"
				 " }\n");
	}
	if (g->is_inlined)
		EMIT0(p, "\n"
			 " new (&_this->$field_$)::_pbi::InlinedStringField;\n");
	EMIT(p,
	     "\n"
	     " if ($hazzer$) {\n"
	     "   _this->$field_$.Set(from._internal_$name$(), _this->GetArena());\n"
	     " }\n",
	     SCB("hazzer", ss_hazzer, g));
}

static void ss_destructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined)
		return;
	if (g->should_split) {
		EMIT0(p, "\n"
			 " $cached_split_ptr$->$name$_.Destroy();\n");
		return;
	}
	EMIT0(p, "\n"
		 " this_.$field_$.Destroy();\n");
}

static void ss_utf8_check(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	generate_utf8_check_code_for_string(p, g->field, g->options, false, "_s.data(), static_cast<int>(_s.length()),");
}

static void ss_serialize(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " const ::std::string& _s = this_._internal_$name$();\n"
	     " $utf8_check$;\n"
	     " target = stream->Write$DeclaredType$MaybeAliased($number$, _s, target);\n",
	     SCB("utf8_check", ss_utf8_check, g));
}

static void ss_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_inlined)
		EMIT0(p, "\n"
			 " /*decltype($field_$)*/ {nullptr, false},\n");
	else
		EMIT0(p, "\n"
			 " /*decltype($field_$)*/ {\n"
			 "     &::_pbi::fixed_address_empty_string,\n"
			 "     ::_pbi::ConstantInitialized{},\n"
			 " },\n");
}

static void ss_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " decltype(Impl_::Split::$name$_){},\n");
	else if (!g->is_inlined)
		EMIT0(p, "\n"
			 " decltype($field_$){},\n");
	else
		EMIT0(p, "\n"
			 " decltype($field_$){arena},\n");
}

static const FieldGeneratorVtbl kSingularString = {
	.make_vars = string_vars,
	.is_inlined = ss_is_inlined,
	.generate_private_members = ss_private_members,
	.requires_arena = ss_requires_arena,
	.generate_merging_code = ss_merging_code,
	.generate_non_inline_accessor_definitions = ss_non_inline_accessor_definitions,
	.generate_byte_size = ss_byte_size,
	.generate_copy_aggregate_initializer = sp_copy_aggregate_initializer,
	.generate_member_constexpr_constructor = ss_member_constexpr_constructor,
	.generate_member_constructor = ss_member_constructor,
	.generate_member_copy_constructor = ss_member_copy_constructor,
	.generate_oneof_copy_construct = ss_oneof_copy_construct,
	.generate_static_members = ss_static_members,
	.generate_accessor_declarations = ss_accessor_declarations,
	.generate_inline_accessor_definitions = ss_inline_accessor_definitions,
	.generate_clearing_code = ss_clearing_code,
	.generate_message_clearing_code = ss_message_clearing_code,
	.generate_swapping_code = ss_swapping_code,
	.generate_copy_constructor_code = ss_copy_constructor_code,
	.generate_destructor_code = ss_destructor_code,
	.generate_serialize_with_cached_sizes_to_array = ss_serialize,
	.generate_constexpr_aggregate_initializer = ss_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = ss_aggregate_initializer,
};

static void make_singular_string_generator(FieldGenerator *g) { g->vt = &kSingularString; }

/* ---- RepeatedString ---- */

static void rs_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " $pbi$::RawPtr<$pb$::RepeatedPtrField<::std::string>> $name$_;\n");
	else
		EMIT0(p, "\n"
			 " $pb$::RepeatedPtrField<::std::string> $name$_;\n");
}

static bool fg_true(const FieldGenerator *g) { return true; }

static void rs_merging_body(LpPrinter *p, void *arg)
{
	EMIT0(p, "\n"
		 " _this->_internal_mutable_$name$()->InternalMergeFromWithArena(\n"
		 "     $pb$::MessageLite::internal_visibility(), arena,\n"
		 "     from._internal_$name$());\n");
}

static void rs_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	if (!g->should_split)
		rs_merging_body(p, NULL);
	else
		EMIT(p,
		     "\n"
		     " if (!from.$field_$.IsDefault()) {\n"
		     "   $body$;\n"
		     " }\n",
		     SCB("body", rs_merging_body, NULL));
}

static void rs_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " if (!from._internal_$name$().empty()) {\n"
			 "   _internal_mutable_$name$()->InternalMergeFromWithArena(\n"
			 "       $pb$::MessageLite::internal_visibility(), arena,\n"
			 "       from._internal_$name$());\n"
			 " }\n");
}

static void rs_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " total_size +=\n"
		 "     $kTagBytes$ * $pbi$::FromIntSize(this_._internal_$name$().size());\n"
		 " for (int i = 0, n = this_._internal_$name$().size(); i < n; ++i) {\n"
		 "   total_size += $pbi$::WireFormatLite::$DeclaredType$Size(\n"
		 "       this_._internal_$name$().Get(i));\n"
		 " }\n");
}

static const char *const kRsAccessorPrefixes[] = {"", "_internal_"};

static void rs_decl_field_accessors(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->cpp_repeated_type == CPP_REP_REPEATED)
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ const\n"
			 "     $pb$::RepeatedPtrField<::std::string>&\n"
			 "     $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedPtrField<::std::string>*\n"
			 "     $nonnull$\n"
			 "     $mutable_name$();\n");
	else
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$\n"
			 "     $pb$::RepeatedFieldProxy<const ::std::string>\n"
			 "     $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<::std::string>\n"
			 " $mutable_name$();\n");
}

static void rs_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	if (is_string_field_with_privatized_accessors(g->field))
		EMIT0(p, "\n"
			 " private:  // Hidden due to unknown ctype option.\n");
	Subs v1 = {0}, v2 = {0}, v3 = {0};
	annotated_accessors(&v1, g->field, kRsAccessorPrefixes, 2, 0);
	annotated_accessors(&v2, g->field, kRepAccessorPrefixesSet, 2, SEM_SET);
	annotated_accessors(&v3, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	PUSH_SUBS(p, &v3);
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED$ const ::std::string& $name$(int index) const;\n"
	     " $DEPRECATED$ ::std::string* $nonnull$ $mutable_name$(int index);\n"
	     " template <typename Arg_ = const ::std::string&, typename... Args_>\n"
	     " $DEPRECATED$ void set_$name$(int index, Arg_&& value, Args_... args);\n"
	     " $DEPRECATED$ ::std::string* $nonnull$ $add_name$();\n"
	     " template <typename Arg_ = const ::std::string&, typename... Args_>\n"
	     " $DEPRECATED$ void $add_name$(Arg_&& value, Args_... args);\n"
	     " $decl_field_accessors$;\n"
	     "\n"
	     " private:\n"
	     " const $pb$::RepeatedPtrField<::std::string>& _internal_$name$() const;\n"
	     " $pb$::RepeatedPtrField<::std::string>* $nonnull$ _internal_mutable_$name$();\n"
	     "\n"
	     " public:\n",
	     SCB("decl_field_accessors", rs_decl_field_accessors, g));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void rs_bytes_tag(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->field->type == PT_BYTES)
		EMIT0(p, ", $pbi$::BytesTag{}");
}

static void rs_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " inline ::std::string* $nonnull$ $Msg$::add_$name$()\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   ::std::string* _s =\n"
	     "       _internal_mutable_$name_internal$()->InternalAddWithArena(\n"
	     "           $pb$::MessageLite::internal_visibility(), GetArena());\n"
	     "   $set_hasbit$;\n"
	     "   $annotate_add_mutable$;\n"
	     "   // @@protoc_insertion_point(field_add_mutable:$pkg.Msg.field$)\n"
	     "   return _s;\n"
	     " }\n"
	     " inline const ::std::string& $Msg$::$name$(int index) const\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_get$;\n"
	     "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
	     "   return _internal_$name_internal$().Get(index);\n"
	     " }\n"
	     " //~ Note: no need to set hasbit in mutable_$name$(int index). Hasbits\n"
	     " //~ only need to be updated if a new element is (potentially) added, not\n"
	     " //~ if an existing element is mutated.\n"
	     " inline ::std::string* $nonnull$ $Msg$::mutable_$name$(int index)\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_mutable$;\n"
	     "   // @@protoc_insertion_point(field_mutable:$pkg.Msg.field$)\n"
	     "   return _internal_mutable_$name_internal$()->Mutable(index);\n"
	     " }\n"
	     " //~ Note: no need to set hasbit in set_$name$(int index). Hasbits only\n"
	     " //~ need to be updated if a new element is (potentially) added, not if\n"
	     " //~ an existing element is mutated.\n"
	     " template <typename Arg_, typename... Args_>\n"
	     " inline void $Msg$::set_$name$(int index, Arg_&& value, Args_... args) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $pbi$::AssignToString(\n"
	     "       *_internal_mutable_$name_internal$()->Mutable(index),\n"
	     "       ::std::forward<Arg_>(value), args... $bytes_tag$);\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
	     " }\n"
	     " template <typename Arg_, typename... Args_>\n"
	     " inline void $Msg$::add_$name$(Arg_&& value, Args_... args) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $pbi$::AddToRepeatedPtrField(\n"
	     "       $pb$::MessageLite::internal_visibility(), GetArena(),\n"
	     "       *_internal_mutable_$name_internal$(), ::std::forward<Arg_>(value),\n"
	     "       args... $bytes_tag$);\n"
	     "   $set_hasbit$;\n"
	     "   $annotate_add$;\n"
	     "   // @@protoc_insertion_point(field_add:$pkg.Msg.field$)\n"
	     " }",
	     SCB("bytes_tag", rs_bytes_tag, g));
	if (g->cpp_repeated_type == CPP_REP_REPEATED)
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedPtrField<::std::string>& $Msg$::$name$()\n"
			 "     const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return _internal_$name_internal$();\n"
			 " }\n"
			 " inline $pb$::RepeatedPtrField<::std::string>* $nonnull$\n"
			 " $Msg$::mutable_$name$() ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return _internal_mutable_$name_internal$();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<const ::std::string> $Msg$::$name$()\n"
			 "     const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       const ::std::string>::Construct(_internal_$name_internal$());\n"
			 " }\n"
			 " inline $pb$::RepeatedFieldProxy<::std::string>\n"
			 " $Msg$::mutable_$name$() ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       ::std::string>::\n"
			 "       Construct(*_internal_mutable_$name_internal$(), GetArena());\n"
			 " }\n");
	if (g->should_split)
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedPtrField<::std::string>&\n"
			 " $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return *$field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedPtrField<::std::string>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   if ($field_$.IsDefault()) {\n"
			 "     $field_$.Set(\n"
			 "         $pb$::Arena::Create<$pb$::RepeatedPtrField<::std::string>>(\n"
			 "             GetArena()));\n"
			 "   }\n"
			 "   return $field_$.Get();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedPtrField<::std::string>&\n"
			 " $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return $field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedPtrField<::std::string>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return &$field_$;\n"
			 " }\n");
}

static void rs_utf8_check(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	generate_utf8_check_code_for_string(p, g->field, g->options, false, "s.data(), static_cast<int>(s.length()),");
}

static void rs_serialize(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " for (int i = 0, n = this_._internal_$name$_size(); i < n; ++i) {\n"
	     "   const auto& s = this_._internal_$name$().Get(i);\n"
	     "   $utf8_check$;\n"
	     "   target = stream->Write$DeclaredType$($number$, s, target);\n"
	     " }\n",
	     SCB("utf8_check", rs_utf8_check, g));
}

static const FieldGeneratorVtbl kRepeatedString = {
	.make_vars = string_vars,
	.generate_private_members = rs_private_members,
	.generate_message_clearing_code = rp_message_clearing_code,
	.generate_clearing_code = rp_clearing_code,
	.requires_arena = fg_true,
	.generate_merging_code = rs_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_destructor_code = rp_destructor_code,
	.generate_copy_constructor_code = rs_copy_constructor_code,
	.generate_byte_size = rs_byte_size,
	.generate_accessor_declarations = rs_accessor_declarations,
	.generate_inline_accessor_definitions = rs_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = rs_serialize,
};

static void make_repeated_string_generator(FieldGenerator *g)
{
	g->vt = &kRepeatedString;
	g->cpp_repeated_type = calculate_cpp_repeated_type(g->field);
}
