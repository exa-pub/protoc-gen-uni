// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/enum_field.cc (protobuf v36.2):
 * SingularEnum and RepeatedEnum. Methods that are textually the same as RepeatedPrimitive's
 * reuse them (primitive_field.c). */

static void enum_vars(const FieldGenerator *g, Subs *out)
{
	const LpField *f = g->field;
	const LpEnumValue *dv = default_value_enum(f);
	bool split = should_split(f, g->options);
	bool is_open = has_preserving_unknown_enum_semantics(f);
	char *enum_name = qualified_enum_class_name(f->enum_type, g->options);
	subs_push(out, S_("Enum", enum_name));
	subs_push(out, S_("kDefault", int32_to_string(dv->number)));
	subs_push(out, SSUF("assert_valid",
			    is_open ? ""
				    : xcat("\n"
					   "                                          assert(::",
					   protobuf_namespace(g->options),
					   "::internal::ValidateEnum(\n"
					   "                                              value, ",
					   enum_name,
					   "_internal_data_));\n"
					   "                                        "),
			    ";"));
	subs_push(out, S_("cached_size_name", make_varint_cached_size_name(f)));
	subs_push(out, S_("cached_size_", make_varint_cached_size_field_name(f, split)));
}

/* ---- SingularEnum ---- */

static void se_private_members(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " int $name$_;\n");
}

static void se_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " swap($field_$, other->$field_$);\n");
}

static void se_serialize(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " target = stream->EnsureSpace(target);\n"
		 " target = ::_pbi::WireFormatLite::WriteEnumToArray(\n"
		 "     $number$, this_._internal_$name$(), target);\n");
}

static void se_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$ +\n"
		 "               ::_pbi::WireFormatLite::EnumSize(this_._internal_$name$());\n");
}

static void se_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " decltype(Impl_::Split::$name$_){$kDefault$},\n");
	else
		EMIT0(p, "\n"
			 " decltype($field_$){$kDefault$},\n");
}

static void se_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v = {0}, vs = {0};
	annotated_accessors(&v, g->field, kAccessorPrefixesPlain, 3, 0);
	annotated_accessors(&vs, g->field, kAccessorPrefixesSet, 1, SEM_SET);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ $Enum$ $name$() const;\n"
		 " $DEPRECATED$ void $set_name$($Enum$ value);\n"
		 "\n"
		 " private:\n"
		 " $Enum$ $_internal_name$() const;\n"
		 " void $_internal_set_name$($Enum$ value);\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void se_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline $Enum$ $Msg$::$name$() const {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	if (g->is_oneof)
		EMIT0(p, "\n"
			 " inline void $Msg$::set_$name$($Enum$ value) {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   $assert_valid$;\n"
			 "   if ($not_has_field$) {\n"
			 "     clear_$oneof_name$();\n"
			 "     set_has_$name_internal$();\n"
			 "   }\n"
			 "   $field_$ = value;\n"
			 "   $annotate_set$;\n"
			 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
			 " }\n"
			 " inline $Enum$ $Msg$::_internal_$name_internal$() const {\n"
			 "   if ($has_field$) {\n"
			 "     return static_cast<$Enum$>($field_$);\n"
			 "   }\n"
			 "   return static_cast<$Enum$>($kDefault$);\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline void $Msg$::set_$name$($Enum$ value) {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   _internal_set_$name_internal$(value);\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_set$;\n"
			 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
			 " }\n"
			 " inline $Enum$ $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return static_cast<$Enum$>($field_$);\n"
			 " }\n"
			 " inline void $Msg$::_internal_set_$name_internal$($Enum$ value) {\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   $assert_valid$;\n"
			 "   $field_$ = value;\n"
			 " }\n");
}

static const FieldGeneratorVtbl kSingularEnum = {
	.make_vars = enum_vars,
	.generate_private_members = se_private_members,
	.generate_message_clearing_code = sp_message_clearing_code,
	.generate_clearing_code = sp_clearing_code,
	.generate_merging_code = sp_merging_code,
	.generate_swapping_code = se_swapping_code,
	.generate_copy_constructor_code = sp_copy_constructor_code,
	.generate_serialize_with_cached_sizes_to_array = se_serialize,
	.generate_byte_size = se_byte_size,
	.generate_constexpr_aggregate_initializer = sp_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = se_aggregate_initializer,
	.generate_copy_aggregate_initializer = sp_copy_aggregate_initializer,
	.generate_accessor_declarations = se_accessor_declarations,
	.generate_inline_accessor_definitions = se_inline_accessor_definitions,
};

static void make_singular_enum_generator(FieldGenerator *g) { g->vt = &kSingularEnum; }

/* ---- RepeatedEnum ---- */

static bool re_has_cached_size(const FieldGenerator *g)
{
	return proto_field_is_packed(g->field) && has_generated_methods(g->field->file, g->options) && !g->should_split;
}

static void re_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " $pbi$::RawPtr<$pb$::RepeatedField<int>> $name$_;\n");
	else
		EMIT0(p, "\n"
			 " $pb$::RepeatedField<int> $name$_;\n");
}

static void re_secondary_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (re_has_cached_size(g))
		EMIT0(p, "\n"
			 " $pbi$::CachedSize $cached_size_name$;\n");
}

static void re_copy_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " decltype($field_$){from._internal_$name$()},\n");
}

static void re_decl_field_accessors(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->cpp_repeated_type == CPP_REP_REPEATED)
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ const $pb$::RepeatedField<int>& $name$()\n"
			 "     const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedField<int>* $nonnull$ $mutable_name$();\n");
	else
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<const $Enum$>\n"
			 " $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<$Enum$> $mutable_name$();\n");
}

static void re_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v = {0}, vs = {0}, vm = {0};
	annotated_accessors(&v, g->field, kRepAccessorPrefixes, 3, 0);
	annotated_accessors(&vs, g->field, kRepAccessorPrefixesSet, 2, SEM_SET);
	annotated_accessors(&vm, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	PUSH_SUBS(p, &vm);
	EMIT(p,
	     "\n"
	     " public:\n"
	     " [[nodiscard]] $DEPRECATED$ $Enum$ $name$(int index) const;\n"
	     " $DEPRECATED$ void $set_name$(int index, $Enum$ value);\n"
	     " $DEPRECATED$ void $add_name$($Enum$ value);\n"
	     " $decl_field_accessors$;\n"
	     "\n"
	     " private:\n"
	     " const $pb$::RepeatedField<int>& $_internal_name$() const;\n"
	     " $pb$::RepeatedField<int>* $nonnull$ $_internal_mutable_name$();\n"
	     "\n"
	     " public:\n",
	     SCB("decl_field_accessors", re_decl_field_accessors, g));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void re_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline $Enum$ $Msg$::$name$(int index) const {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   return static_cast<$Enum$>(_internal_$name_internal$().Get(index));\n"
		 " }\n");
	EMIT0(p, "\n"
		 " //~ Note: no need to set hasbit in set_$name$(int index). Hasbits only\n"
		 " //~ need to be updated if a new element is (potentially) added, not if an\n"
		 " //~ existing element is mutated.\n"
		 " inline void $Msg$::set_$name$(int index, $Enum$ value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $assert_valid$;\n"
		 "   _internal_mutable_$name_internal$()->Set(index, value);\n"
		 "   $annotate_set$\n"
		 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::add_$name$($Enum$ value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $assert_valid$;\n"
		 "   $TsanDetectConcurrentMutation$;\n"
		 "   _internal_mutable_$name_internal$()\n"
		 "       ->InternalAddWithArena<const $pb$::MessageLite*>(\n"
		 "           internal_visibility(), this, value);\n"
		 "   $set_hasbit$;\n"
		 "   $annotate_add$\n"
		 "   // @@protoc_insertion_point(field_add:$pkg.Msg.field$)\n"
		 " }\n");
	if (g->cpp_repeated_type == CPP_REP_REPEATED) {
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<int>& $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return _internal_$name_internal$();\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedField<int>* $nonnull$ $Msg$::mutable_$name$()\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return _internal_mutable_$name_internal$();\n"
			 " }\n");
	} else {
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<const $Enum$> $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       const $Enum$>::Construct(_internal_$name_internal$());\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<$Enum$> $Msg$::mutable_$name$()\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       $Enum$>::Construct(*_internal_mutable_$name_internal$(),\n"
			 "                          GetArena());\n"
			 " }\n");
	}
	if (g->should_split)
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<int>& $Msg$::_internal_$name_internal$()\n"
			 "     const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return *$field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedField<int>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   if ($field_$.IsDefault()) {\n"
			 "     $field_$.Set($pb$::Arena::Create<$pb$::RepeatedField<int>>(GetArena()));\n"
			 "   }\n"
			 "   return $field_$.Get();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<int>& $Msg$::_internal_$name_internal$()\n"
			 "     const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return $field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedField<int>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return &$field_$;\n"
			 " }\n");
}

static void re_byte_size_cb(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (re_has_cached_size(g))
		EMIT0(p, "::size_t byte_size = this_.$cached_size_$.Get();");
	else
		EMIT0(p, "\n"
			 " ::size_t byte_size = 0;\n"
			 " auto count = static_cast<::size_t>(this_._internal_$name$_size());\n"
			 "\n"
			 " for (::size_t i = 0; i < count; ++i) {\n"
			 "   byte_size += ::_pbi::WireFormatLite::EnumSize(\n"
			 "       this_._internal_$name$().Get(static_cast<int>(i)));\n"
			 " }\n");
}

static void re_serialize(const FieldGenerator *g, LpPrinter *p)
{
	if (proto_field_is_packed(g->field)) {
		EMIT(p,
		     "\n"
		     " {\n"
		     "   $byte_size$;\n"
		     "   if (byte_size > 0) {\n"
		     "     target = stream->WriteEnumPacked(\n"
		     "         $number$, this_._internal_$name$(), byte_size, target);\n"
		     "   }\n"
		     " }\n",
		     SCB("byte_size", re_byte_size_cb, g));
		return;
	}
	EMIT0(p, "\n"
		 " for (int i = 0, n = this_._internal_$name$_size(); i < n; ++i) {\n"
		 "   target = stream->EnsureSpace(target);\n"
		 "   target = ::_pbi::WireFormatLite::WriteEnumToArray(\n"
		 "       $number$, static_cast<$Enum$>(this_._internal_$name$().Get(i)),\n"
		 "       target);\n"
		 " }\n");
}

static void re_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	if (re_has_cached_size(g)) {
		EMIT0(p, "\n"
			 " total_size += ::_pbi::WireFormatLite::EnumSizeWithPackedTagSize(\n"
			 "     this_._internal_$name$(), $kTagBytes$, this_.$cached_size_$);\n");
		return;
	}
	EMIT(p,
	     "\n"
	     " ::size_t data_size =\n"
	     "     ::_pbi::WireFormatLite::EnumSize(this_._internal_$name$());\n"
	     " ::size_t tag_size = $tag_size$;\n"
	     " total_size += data_size + tag_size;\n",
	     SCB("tag_size", rp_tag_size_cb, g));
}

static const FieldGeneratorVtbl kRepeatedEnum = {
	.make_vars = enum_vars,
	.generate_private_members = re_private_members,
	.generate_secondary_private_members = re_secondary_private_members,
	.generate_message_clearing_code = rp_message_clearing_code,
	.generate_clearing_code = rp_clearing_code,
	.generate_merging_code = rp_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_destructor_code = rp_destructor_code,
	.generate_constexpr_aggregate_initializer = rp_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = rp_aggregate_initializer,
	.generate_copy_aggregate_initializer = re_copy_aggregate_initializer,
	.generate_member_constexpr_constructor = rp_member_constexpr_constructor,
	.generate_member_constructor = rp_member_constexpr_constructor,
	.generate_member_copy_constructor = rp_member_copy_constructor,
	.generate_oneof_copy_construct = fg_not_supported,
	.generate_copy_constructor_code = rp_copy_constructor_code,
	.generate_accessor_declarations = re_accessor_declarations,
	.generate_inline_accessor_definitions = re_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = re_serialize,
	.generate_byte_size = re_byte_size,
};

static void make_repeated_enum_generator(FieldGenerator *g)
{
	g->vt = &kRepeatedEnum;
	g->cpp_repeated_type = calculate_cpp_repeated_type(g->field);
}
