// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/string_view_field.cc (protobuf
 * v36.2): SingularStringView and RepeatedStringView. MicroString needs the
 * experimental_cpp_micro_string option, which the port rejects (see README), so its branches are
 * not ported. Methods that are textually the same as the string_field.c ones reuse them. */

static void string_view_vars(const FieldGenerator *g, Subs *out)
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
	subs_push(out, S_("byte", bytes ? "void" : "char"));
}

static const char *const kSvAccessorPrefixes[] = {""};

static void sv_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v1 = {0}, v2 = {0};
	annotated_accessors(&v1, g->field, kSvAccessorPrefixes, 1, 0);
	annotated_accessors(&v2, g->field, kStrAccessorPrefixesSet, 1, SEM_SET);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ ::absl::string_view $name$() const;\n"
		 " template <typename Arg_ = ::std::string&&>\n"
		 " $DEPRECATED$ void $set_name$(Arg_&& arg);\n"
		 "\n"
		 " private:\n"
		 " ::absl::string_view _internal_$name$() const;\n"
		 " PROTOBUF_ALWAYS_INLINE void _internal_set_$name$(::absl::string_view value);\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void sv_if_is_default(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (ss_empty_default(g) || g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " if ($field_$.IsDefault()) {\n"
		 "   return $default_variable_field$.get();\n"
		 " }\n");
}

static void sv_check_hasbit(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (!g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " if ($not_has_field$) {\n"
		 "   return $kDefaultStr$;\n"
		 " }\n");
}

static void sv_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " inline ::absl::string_view $Msg$::$name$() const\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_get$;\n"
	     "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
	     "   $if_IsDefault$;\n"
	     "   return _internal_$name_internal$();\n"
	     " }\n"
	     " template <typename Arg_>\n"
	     " PROTOBUF_ALWAYS_INLINE void $Msg$::set_$name$(Arg_&& arg) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   $update_hasbit$;\n"
	     "   $field_$.Set(static_cast<Arg_&&>(arg), GetArena());\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
	     " }\n"
	     " inline ::absl::string_view $Msg$::_internal_$name_internal$() const {\n"
	     "   $TsanDetectConcurrentRead$;\n"
	     "   $check_hasbit$;\n"
	     "   return $field_$.Get();\n"
	     " }\n"
	     " inline void $Msg$::_internal_set_$name_internal$(::absl::string_view value) {\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $update_hasbit$;\n"
	     "   $field_$.Set(value, GetArena());\n"
	     " }\n",
	     SCB("if_IsDefault", sv_if_is_default, g), SCB("update_hasbit", ss_update_hasbit, g),
	     SCB("check_hasbit", sv_check_hasbit, g));
}

static void sv_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof)
		return;
	if (!g->is_inlined) {
		EMIT0(p, "\n"
			 " $field_$.InternalSwap(&$field_$, &other->$field_$, arena);\n");
		return;
	}
	EMIT0(p, "\n"
		 " ::_pbi::InlinedStringField::InternalSwap(&$field_$, &other->$field_$,\n"
		 "                                          arena);\n");
}

static void sv_serialize(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " const ::absl::string_view _s = this_._internal_$name$();\n"
	     " $utf8_check$;\n"
	     " target = stream->Write$DeclaredType$MaybeAliased($number$, _s, target);\n",
	     SCB("utf8_check", ss_utf8_check, g));
}

static const FieldGeneratorVtbl kSingularStringView = {
	.make_vars = string_view_vars,
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
	.generate_accessor_declarations = sv_accessor_declarations,
	.generate_inline_accessor_definitions = sv_inline_accessor_definitions,
	.generate_clearing_code = ss_clearing_code,
	.generate_message_clearing_code = ss_message_clearing_code,
	.generate_swapping_code = sv_swapping_code,
	.generate_copy_constructor_code = ss_copy_constructor_code,
	.generate_destructor_code = ss_destructor_code,
	.generate_serialize_with_cached_sizes_to_array = sv_serialize,
	.generate_constexpr_aggregate_initializer = ss_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = ss_aggregate_initializer,
};

static void make_singular_string_view_generator(FieldGenerator *g) { g->vt = &kSingularStringView; }

/* ---- RepeatedStringView ---- */

static void rsv_decl_field_accessors(LpPrinter *p, void *arg)
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
			 "     $pb$::RepeatedFieldProxy<const ::absl::string_view>\n"
			 "     $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$\n"
			 "     $pb$::RepeatedFieldProxy<::absl::string_view>\n"
			 "     $mutable_name$();\n");
}

static void rsv_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v1 = {0}, v2 = {0}, v3 = {0};
	annotated_accessors(&v1, g->field, kRsAccessorPrefixes, 2, 0);
	annotated_accessors(&v2, g->field, kRepAccessorPrefixesSet, 2, SEM_SET);
	annotated_accessors(&v3, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	PUSH_SUBS(p, &v3);
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED$ ::absl::string_view $name$(int index) const;\n"
	     " template <typename Arg_ = ::std::string&&>\n"
	     " $DEPRECATED$ void set_$name$(int index, Arg_&& value);\n"
	     " template <typename Arg_ = ::std::string&&>\n"
	     " $DEPRECATED$ void add_$name$(Arg_&& value);\n"
	     " $decl_field_accessors$;\n"
	     "\n"
	     " private:\n"
	     " const $pb$::RepeatedPtrField<::std::string>& _internal_$name$() const;\n"
	     " $pb$::RepeatedPtrField<::std::string>* $nonnull$ _internal_mutable_$name$();\n"
	     "\n"
	     " public:\n",
	     SCB("decl_field_accessors", rsv_decl_field_accessors, g));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void rsv_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " inline ::absl::string_view $Msg$::$name$(int index) const\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_get$;\n"
	     "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
	     "   return _internal_$name_internal$().Get(index);\n"
	     " }\n"
	     " //~ Note: no need to set hasbit in set_$name$(int index). Hasbits only\n"
	     " //~ need to be updated if a new element is (potentially) added, not if\n"
	     " //~ an existing element is mutated.\n"
	     " template <typename Arg_>\n"
	     " inline void $Msg$::set_$name$(int index, Arg_&& value) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $pbi$::AssignToString(\n"
	     "       *_internal_mutable_$name_internal$()->Mutable(index),\n"
	     "       ::std::forward<Arg_>(value) $bytes_tag$);\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
	     " }\n"
	     " template <typename Arg_>\n"
	     " inline void $Msg$::add_$name$(Arg_&& value) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $pbi$::AddToRepeatedPtrField($pb$::MessageLite::internal_visibility(),\n"
	     "                                GetArena(),\n"
	     "                                *_internal_mutable_$name_internal$(),\n"
	     "                                ::std::forward<Arg_>(value) $bytes_tag$);\n"
	     "   $set_hasbit$;\n"
	     "   $annotate_add$;\n"
	     "   // @@protoc_insertion_point(field_add:$pkg.Msg.field$)\n"
	     " }\n",
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
			 " inline $pb$::RepeatedFieldProxy<const ::absl::string_view>\n"
			 " $Msg$::$name$() const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       const ::absl::string_view>::\n"
			 "       Construct(_internal_$name_internal$());\n"
			 " }\n"
			 " inline $pb$::RepeatedFieldProxy<::absl::string_view>\n"
			 " $Msg$::mutable_$name$() ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       ::absl::string_view>::\n"
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

static const FieldGeneratorVtbl kRepeatedStringView = {
	.make_vars = string_view_vars,
	.generate_private_members = rs_private_members,
	.generate_message_clearing_code = rp_message_clearing_code,
	.generate_clearing_code = rp_clearing_code,
	.requires_arena = fg_true,
	.generate_merging_code = rs_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_destructor_code = rp_destructor_code,
	.generate_copy_constructor_code = rs_copy_constructor_code,
	.generate_byte_size = rs_byte_size,
	.generate_accessor_declarations = rsv_accessor_declarations,
	.generate_inline_accessor_definitions = rsv_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = rs_serialize,
};

static void make_repeated_string_view_generator(FieldGenerator *g)
{
	g->vt = &kRepeatedStringView;
	g->cpp_repeated_type = calculate_cpp_repeated_type(g->field);
}
