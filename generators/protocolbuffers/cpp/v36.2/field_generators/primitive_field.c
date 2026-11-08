// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/primitive_field.cc (protobuf v36.2):
 * SingularPrimitive and RepeatedPrimitive. */

/* FixedSize: 0 for encodings without a fixed size */
static size_t fixed_size(int type)
{
	switch (type) {
	case PT_FIXED32:
	case PT_SFIXED32:
	case PT_FLOAT:
		return 4;
	case PT_FIXED64:
	case PT_SFIXED64:
	case PT_DOUBLE:
		return 8;
	case PT_BOOL:
		return 1;
	default:
		return 0;
	}
}

static void primitive_vars(const FieldGenerator *g, Subs *out)
{
	bool cold = should_split(g->field, g->options);
	subs_push(out, S_("Type", primitive_type_name(g->options, cpp_type(g->field))));
	subs_push(out, S_("kDefault", default_value(g->options, g->field)));
	subs_push(out, S_("_field_cached_byte_size_", make_varint_cached_size_field_name(g->field, cold)));
}

/* ---- SingularPrimitive ---- */

static void sp_private_members(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $Type$ $name$_;\n");
}

static void sp_message_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " this_.$field_$ = $kDefault$;\n");
}

static void sp_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $field_$ = $kDefault$;\n");
}

static void sp_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " _this->$field_$ = from.$field_$;\n");
}

static void sp_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_oneof)
		return;
	EMIT0(p, "\n"
		 " //~ A `using std::swap;` is already present in this function.\n"
		 " swap($field_$, other->$field_$);\n");
}

static void sp_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " _this->$field_$ = from.$field_$;\n");
}

static void sp_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " /*decltype($field_$)*/ $kDefault$,\n");
}

static void sp_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " decltype($field_$){$kDefault$},\n");
}

static void sp_copy_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " decltype($field_$){},\n");
}

static const char *const kAccessorPrefixesPlain[] = {"", "_internal_", "_internal_set_"};
static const char *const kAccessorPrefixesSet[] = {"set_"};

static void sp_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v = {0}, vs = {0};
	annotated_accessors(&v, g->field, kAccessorPrefixesPlain, 3, 0);
	annotated_accessors(&vs, g->field, kAccessorPrefixesSet, 1, SEM_SET);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ $Type$ $name$() const;\n"
		 " $DEPRECATED$ void $set_name$($Type$ value);\n"
		 "\n"
		 " private:\n"
		 " $Type$ $_internal_name$() const;\n"
		 " void $_internal_set_name$($Type$ value);\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void sp_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline $Type$ $Msg$::$name$() const {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	if (g->is_oneof)
		EMIT0(p, "\n"
			 " inline void $Msg$::set_$name$($Type$ value) {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   if ($not_has_field$) {\n"
			 "     clear_$oneof_name$();\n"
			 "     set_has_$name_internal$();\n"
			 "   }\n"
			 "   $field_$ = value;\n"
			 "   $annotate_set$;\n"
			 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
			 " }\n"
			 " inline $Type$ $Msg$::_internal_$name_internal$() const {\n"
			 "   if ($has_field$) {\n"
			 "     return $field_$;\n"
			 "   }\n"
			 "   return $kDefault$;\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline void $Msg$::set_$name$($Type$ value) {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   _internal_set_$name_internal$(value);\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_set$;\n"
			 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
			 " }\n"
			 " inline $Type$ $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return $field_$;\n"
			 " }\n"
			 " inline void $Msg$::_internal_set_$name_internal$($Type$ value) {\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   $field_$ = value;\n"
			 " }\n");
}

static void sp_serialize(const FieldGenerator *g, LpPrinter *p)
{
	int t = g->field->type;
	if (g->field->number < 16 && (t == PT_INT32 || t == PT_INT64 || t == PT_ENUM))
		EMIT0(p, "\n"
			 " target =\n"
			 "     $pbi$::WireFormatLite::Write$DeclaredType$ToArrayWithField<$number$>(\n"
			 "         stream, this_._internal_$name$(), target);\n");
	else
		EMIT0(p, "\n"
			 " target = stream->EnsureSpace(target);\n"
			 " target = ::_pbi::WireFormatLite::Write$DeclaredType$ToArray(\n"
			 "     $number$, this_._internal_$name$(), target);\n");
}

static void sp_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	size_t tag_size = (size_t)wire_format_tag_size(g->field->number, g->field->type);
	size_t fs = fixed_size(g->field->type);
	if (fs) {
		EMIT(p,
		     "\n"
		     " total_size += $kFixedBytes$;\n",
		     SUINT("kFixedBytes", tag_size + fs));
		return;
	}
	if (tag_size == 1) {
		EMIT0(p, "\n"
			 " total_size += ::_pbi::WireFormatLite::$DeclaredType$SizePlusOne(\n"
			 "     this_._internal_$name$());\n");
		return;
	}
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$ + ::_pbi::WireFormatLite::$DeclaredType$Size(\n"
		 "                                 this_._internal_$name$());\n");
}

static const FieldGeneratorVtbl kSingularPrimitive = {
	.make_vars = primitive_vars,
	.generate_private_members = sp_private_members,
	.generate_message_clearing_code = sp_message_clearing_code,
	.generate_clearing_code = sp_clearing_code,
	.generate_merging_code = sp_merging_code,
	.generate_swapping_code = sp_swapping_code,
	.generate_copy_constructor_code = sp_copy_constructor_code,
	.generate_constexpr_aggregate_initializer = sp_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = sp_aggregate_initializer,
	.generate_copy_aggregate_initializer = sp_copy_aggregate_initializer,
	.generate_accessor_declarations = sp_accessor_declarations,
	.generate_inline_accessor_definitions = sp_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = sp_serialize,
	.generate_byte_size = sp_byte_size,
};

static void make_singular_primitive_generator(FieldGenerator *g) { g->vt = &kSingularPrimitive; }

/* ---- RepeatedPrimitive ---- */

static bool rp_has_cached_size(const FieldGenerator *g)
{
	bool is_packed_varint = proto_field_is_packed(g->field) && !fixed_size(g->field->type);
	return is_packed_varint && has_generated_methods(g->field->file, g->options) && !g->should_split;
}

static void rp_message_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "this_.$field_$.ClearIfNotDefault();\n");
	else
		EMIT0(p, "$field_$.Clear();\n");
}

static void rp_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "$field_$.ClearIfNotDefault();\n");
	else
		EMIT0(p, "$field_$.Clear();\n");
}

static void rp_merging_body(LpPrinter *p, void *arg)
{
	EMIT0(p, "\n"
		 " _this->_internal_mutable_$name$()->MergeFrom(from._internal_$name$());\n");
}

static void rp_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	if (!g->should_split)
		rp_merging_body(p, NULL);
	else
		EMIT(p,
		     "\n"
		     " if (!from.$field_$.IsDefault()) {\n"
		     "   $body$;\n"
		     " }\n",
		     SCB("body", rp_merging_body, NULL));
}

static void rp_swapping_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $field_$.InternalSwap(&other->$field_$);\n");
}

static void rp_destructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " this_.$field_$.DeleteIfNotDefault();\n");
}

static void rp_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " if (!from._internal_$name$().empty()) {\n"
			 "   _internal_mutable_$name$()->MergeFrom(from._internal_$name$());\n"
			 " }\n");
}

static void rp_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " /*decltype($field_$)*/ {},\n");
}

static void rp_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " decltype($field_$){$internal_metadata_offset$},\n",
	     INTERNAL_METADATA_OFFSET_SUB);
}

static void rp_copy_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " decltype($field_$){from.$field_$},\n");
}

static void rp_member_constexpr_constructor(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " $name$_ { visibility, $internal_metadata_offset$ }\n",
	     INTERNAL_METADATA_OFFSET_SUB);
}

static void rp_member_copy_constructor(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " $name$_ {\n"
	     "   visibility, $internal_metadata_offset$, arena, from.$name$_\n"
	     " }\n",
	     INTERNAL_METADATA_OFFSET_SUB);
}

static void fg_not_supported(const FieldGenerator *g, LpPrinter *p) { fail("Not supported"); }

static void rp_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " $pbi$::RawPtr<$pb$::RepeatedField<$Type$>> $name$_;\n");
	else
		EMIT0(p, "\n"
			 " $pb$::RepeatedField<$Type$> $name$_;\n");
}

static void rp_secondary_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (rp_has_cached_size(g))
		EMIT(p,
		     "\n"
		     " $pbi$::CachedSize $_cached_size_$;\n",
		     S_("_cached_size_", make_varint_cached_size_name(g->field)));
}

static const char *const kRepAccessorPrefixes[] = {"", "_internal_", "_internal_mutable_"};
static const char *const kRepAccessorPrefixesSet[] = {"set_", "add_"};
static const char *const kRepAccessorPrefixesAlias[] = {"mutable_"};

static void rp_decl_field_accessors(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->cpp_repeated_type == CPP_REP_REPEATED)
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ const $pb$::RepeatedField<$Type$>& $name$()\n"
			 "     const;\n"
			 " $DEPRECATED$ $pb$::RepeatedField<$Type$>* $nonnull$ $mutable_name$();\n");
	else
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<const $Type$>\n"
			 " $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<$Type$> $mutable_name$();\n");
}

static void rp_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v = {0}, vs = {0}, va = {0};
	annotated_accessors(&v, g->field, kRepAccessorPrefixes, 3, 0);
	annotated_accessors(&vs, g->field, kRepAccessorPrefixesSet, 2, SEM_SET);
	annotated_accessors(&va, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	PUSH_SUBS(p, &va);
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED$ $Type$ $name$(int index) const;\n"
	     " $DEPRECATED$ void $set_name$(int index, $Type$ value);\n"
	     " $DEPRECATED$ void $add_name$($Type$ value);\n"
	     " $decl_field_accessors$;\n"
	     "\n"
	     " private:\n"
	     " const $pb$::RepeatedField<$Type$>& $_internal_name$() const;\n"
	     " $pb$::RepeatedField<$Type$>* $nonnull$ $_internal_mutable_name$();\n"
	     "\n"
	     " public:\n",
	     SCB("decl_field_accessors", rp_decl_field_accessors, g));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void rp_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline $Type$ $Msg$::$name$(int index) const {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   return _internal_$name_internal$().Get(index);\n"
		 " }\n");
	EMIT0(p, "\n"
		 " //~ Note: no need to set hasbit in set_$name$(int index). Hasbits only need\n"
		 " //~ to be updated if a new element is (potentially) added, not if an\n"
		 " //~ existing element is mutated.\n"
		 " inline void $Msg$::set_$name$(int index, $Type$ value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_set$;\n"
		 "   _internal_mutable_$name_internal$()->Set(index, value);\n"
		 "   // @@protoc_insertion_point(field_set:$pkg.Msg.field$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::add_$name$($Type$ value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $TsanDetectConcurrentMutation$;\n"
		 "   _internal_mutable_$name_internal$()\n"
		 "       ->InternalAddWithArena<const $pb$::MessageLite*>(\n"
		 "           internal_visibility(), this, value);\n"
		 "   $set_hasbit$;\n"
		 "   $annotate_add$;\n"
		 "   // @@protoc_insertion_point(field_add:$pkg.Msg.field$)\n"
		 " }\n");
	if (g->cpp_repeated_type == CPP_REP_REPEATED) {
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<$Type$>& $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return _internal_$name_internal$();\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedField<$Type$>* $nonnull$ $Msg$::mutable_$name$()\n"
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
			 " inline $pb$::RepeatedFieldProxy<const $Type$> $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       const $Type$>::Construct(_internal_$name_internal$());\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<$Type$> $Msg$::mutable_$name$()\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       $Type$>::Construct(*_internal_mutable_$name_internal$(),\n"
			 "                          GetArena());\n"
			 " }\n");
	}
	if (g->should_split)
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<$Type$>&\n"
			 " $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return *$field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedField<$Type$>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   if ($field_$.IsDefault()) {\n"
			 "     $field_$.Set($pb$::Arena::Create<$pb$::RepeatedField<$Type$>>(GetArena()));\n"
			 "   }\n"
			 "   return $field_$.Get();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedField<$Type$>&\n"
			 " $Msg$::_internal_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return $field_$;\n"
			 " }\n"
			 " inline $pb$::RepeatedField<$Type$>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return &$field_$;\n"
			 " }\n");
}

static void rp_byte_size_cb(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (rp_has_cached_size(g))
		EMIT0(p, "this_.$_field_cached_byte_size_$.Get();");
	else
		EMIT0(p, "\n"
			 " ::_pbi::WireFormatLite::$DeclaredType$Size(\n"
			 "     this_._internal_$name$());\n");
}

static void rp_serialize(const FieldGenerator *g, LpPrinter *p)
{
	if (!proto_field_is_packed(g->field)) {
		EMIT0(p, "\n"
			 " for (int i = 0, n = this_._internal_$name$_size(); i < n; ++i) {\n"
			 "   target = stream->EnsureSpace(target);\n"
			 "   target = ::_pbi::WireFormatLite::Write$DeclaredType$ToArray(\n"
			 "       $number$, this_._internal_$name$().Get(i), target);\n"
			 " }\n");
		return;
	}
	if (fixed_size(g->field->type)) {
		EMIT0(p, "\n"
			 " if (this_._internal_$name$_size() > 0) {\n"
			 "   target = stream->WriteFixedPacked($number$, this_._internal_$name$(), target);\n"
			 " }\n");
		return;
	}
	EMIT(p,
	     "\n"
	     " {\n"
	     "   int byte_size = $byte_size$;\n"
	     "   if (byte_size > 0) {\n"
	     "     target = stream->Write$DeclaredType$Packed(\n"
	     "         $number$, this_._internal_$name$(), byte_size, target);\n"
	     "   }\n"
	     " }\n",
	     SCB("byte_size", rp_byte_size_cb, g));
}

static void rp_data_size_cb(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	size_t fs = fixed_size(g->field->type);
	if (fs)
		EMIT(p,
		     "\n"
		     " ::size_t{$kFixed$} *\n"
		     "     ::_pbi::FromIntSize(this_._internal_$name$_size());\n",
		     SUINT("kFixed", fs));
	else
		EMIT0(p, "\n"
			 " ::_pbi::WireFormatLite::$DeclaredType$Size(\n"
			 "     this_._internal_$name$());\n");
}

static void rp_tag_size_cb(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (proto_field_is_packed(g->field))
		EMIT0(p, "\n"
			 " data_size == 0\n"
			 "     ? 0\n"
			 "     : $kTagBytes$ + ::_pbi::WireFormatLite::Int32Size(\n"
			 "                         static_cast<::int32_t>(data_size));\n");
	else
		EMIT0(p, "\n"
			 " ::size_t{$kTagBytes$} *\n"
			 "     ::_pbi::FromIntSize(this_._internal_$name$_size());\n");
}

static void rp_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	if (rp_has_cached_size(g)) {
		EMIT0(p, "\n"
			 " total_size +=\n"
			 "     ::_pbi::WireFormatLite::$DeclaredType$SizeWithPackedTagSize(\n"
			 "         this_._internal_$name$(), $kTagBytes$,\n"
			 "         this_.$_field_cached_byte_size_$);\n");
		return;
	}
	EMIT(p,
	     "\n"
	     " ::size_t data_size = $data_size$;\n"
	     " ::size_t tag_size = $tag_size$;\n"
	     " total_size += tag_size + data_size;\n",
	     SCB("data_size", rp_data_size_cb, g), SCB("tag_size", rp_tag_size_cb, g));
}

static const FieldGeneratorVtbl kRepeatedPrimitive = {
	.make_vars = primitive_vars,
	.generate_message_clearing_code = rp_message_clearing_code,
	.generate_clearing_code = rp_clearing_code,
	.generate_merging_code = rp_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_destructor_code = rp_destructor_code,
	.generate_copy_constructor_code = rp_copy_constructor_code,
	.generate_constexpr_aggregate_initializer = rp_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = rp_aggregate_initializer,
	.generate_copy_aggregate_initializer = rp_copy_aggregate_initializer,
	.generate_member_constexpr_constructor = rp_member_constexpr_constructor,
	.generate_member_constructor = rp_member_constexpr_constructor,
	.generate_member_copy_constructor = rp_member_copy_constructor,
	.generate_oneof_copy_construct = fg_not_supported,
	.generate_private_members = rp_private_members,
	.generate_secondary_private_members = rp_secondary_private_members,
	.generate_accessor_declarations = rp_accessor_declarations,
	.generate_inline_accessor_definitions = rp_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = rp_serialize,
	.generate_byte_size = rp_byte_size,
};

static void make_repeated_primitive_generator(FieldGenerator *g)
{
	g->vt = &kRepeatedPrimitive;
	g->cpp_repeated_type = calculate_cpp_repeated_type(g->field);
}
