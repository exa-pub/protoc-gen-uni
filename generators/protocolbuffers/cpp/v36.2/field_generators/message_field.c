// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/message_field.cc (protobuf v36.2):
 * SingularMessage, OneofMessage (a SingularMessage) and RepeatedMessage. */

static void message_vars(const FieldGenerator *g, Subs *out, bool is_weak_, bool use_base_class)
{
	const LpField *f = g->field;
	const Options *o = g->options;
	bool split = should_split(f, o);
	bool is_foreign = is_cross_file_message(f);
	char *fname = field_member_name(f, split);
	char *qualified_type = field_message_type_name(f, o);
	char *base = xcat("::", protobuf_namespace(o), "::", has_descriptor_methods(f->file, o) ? "Message" : "MessageLite");
	subs_push(out, S_("Submsg", qualified_type));
	subs_push(out, S_("MemberType", use_base_class ? base : qualified_type));
	subs_push(out, S_("kDefaultRef", xcat("*::google::protobuf::internal::MessageGlobalsBase::ToDefaultInstance<",
					      qualified_type, ">(&",
					      qualified_msg_globals_instance_name(f->message_type, o), ")")));
	subs_push(out, (LpSub){.key = "cast_to_field",
			       .value = use_base_class ? xcat("reinterpret_cast<", base, "*>") : "",
			       .consume_parens_if_empty = true});
	subs_push(out, (LpSub){.key = "arena_cast",
			       .value = !is_foreign ? "" : xcat("reinterpret_cast<", base, "*>"),
			       .consume_parens_if_empty = true});
	subs_push(out, S_("cast_field_", use_base_class ? xcat("reinterpret_cast<", qualified_type, "*>(", fname, ")") : fname));
	subs_push(out, S_("Weak", is_weak_ ? "Weak" : ""));
	subs_push(out, S_(".weak", is_weak_ ? ".weak" : ""));
	subs_push(out, S_("_weak", is_weak_ ? "_weak" : ""));
	subs_push(out, SSUF("StrongRef", !is_weak_ ? "" : xcat(strong_reference_to_type(f->message_type, o), ";"), ";"));
}

static void sm_vars(const FieldGenerator *g, Subs *out) { message_vars(g, out, g->is_weak, g->is_weak); }

/* ---- SingularMessage ---- */

static void sm_private_members(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $MemberType$* $nullable$ $name$_;\n");
}

static const char *const kSmAccessorPrefixes[] = {"", "set_allocated_", "unsafe_arena_set_allocated_",
						   "unsafe_arena_release_"};

static void sm_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v1 = {0}, v2 = {0};
	annotated_accessors(&v1, g->field, kSmAccessorPrefixes, 4, 0);
	subs_push(&v1, SANN("release_name", safe_function_name(g->field->containing_type, g->field, "release_"), g->field));
	annotated_accessors(&v2, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	EMIT0(p, "\n"
		 " $DEPRECATED$ [[nodiscard]] const $Submsg$& $name$() const;\n"
		 " $DEPRECATED$ [[nodiscard]] $Submsg$* $nullable$ $release_name$();\n"
		 " $DEPRECATED$ $Submsg$* $nonnull$ $mutable_name$();\n"
		 " $DEPRECATED$ void $set_allocated_name$($Submsg$* $nullable$ value);\n"
		 " $DEPRECATED$ void $unsafe_arena_set_allocated_name$($Submsg$* $nullable$ value);\n"
		 " $DEPRECATED$ $Submsg$* $nullable$ $unsafe_arena_release_name$();\n"
		 "\n"
		 " private:\n"
		 " const $Submsg$& _internal_$name$() const;\n"
		 " $Submsg$* $nonnull$ _internal_mutable_$name$();\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void sm_update_hasbit(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (!g->has_hasbit)
		return;
	EMIT0(p, "\n"
		 " if (value != nullptr) {\n"
		 "   $set_hasbit$\n"
		 " } else {\n"
		 "   $clear_hasbit$\n"
		 " }\n");
}

static void sm_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	LpSub v[] = {S_("release_name", safe_function_name(g->field->containing_type, g->field, "release_"))};
	lp_push_vars(p, v, 1);
	EMIT(p,
	     "\n"
	     " inline const $Submsg$& $Msg$::_internal_$name_internal$() const {\n"
	     "   $TsanDetectConcurrentRead$;\n"
	     "   $StrongRef$;\n"
	     "   const $Submsg$* p = $cast_field_$;\n"
	     "   return p != nullptr ? *p : $kDefaultRef$;\n"
	     " }\n"
	     " inline const $Submsg$& $Msg$::$name$() const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $annotate_get$;\n"
	     "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
	     "   return _internal_$name_internal$();\n"
	     " }\n"
	     " inline void $Msg$::unsafe_arena_set_allocated_$name$(\n"
	     "     $Submsg$* $nullable$ value) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   //~ If we're not on an arena, free whatever we were holding before.\n"
	     "   //~ (If we are on arena, we can just forget the earlier pointer.)\n"
	     "   if (GetArena() == nullptr) {\n"
	     "     delete reinterpret_cast<$pb$::MessageLite*>($field_$);\n"
	     "   }\n"
	     "   $field_$ = reinterpret_cast<$MemberType$*>(value);\n"
	     "   $update_hasbit$;\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_unsafe_arena_set_allocated:$pkg.Msg.field$)\n"
	     " }\n"
	     " inline $Submsg$* $nullable$ $Msg$::$release_name$() {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $StrongRef$;\n"
	     "   $annotate_release$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "\n"
	     "   $clear_hasbit$;\n"
	     "   $Submsg$* released = $cast_field_$;\n"
	     "   $field_$ = nullptr;\n"
	     "   if ($pbi$::DebugHardenForceCopyInRelease()) {\n"
	     "     auto* old = reinterpret_cast<$pb$::MessageLite*>(released);\n"
	     "     released = $pbi$::DuplicateIfNonNull(released);\n"
	     "     if (GetArena() == nullptr) {\n"
	     "       delete old;\n"
	     "     }\n"
	     "   } else {\n"
	     "     if (GetArena() != nullptr) {\n"
	     "       released = $pbi$::DuplicateIfNonNull(released);\n"
	     "     }\n"
	     "   }\n"
	     "   return released;\n"
	     " }\n"
	     " inline $Submsg$* $nullable$ $Msg$::unsafe_arena_release_$name$() {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $annotate_release$;\n"
	     "   // @@protoc_insertion_point(field_release:$pkg.Msg.field$)\n"
	     "   $StrongRef$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "\n"
	     "   $clear_hasbit$;\n"
	     "   $Submsg$* temp = $cast_field_$;\n"
	     "   $field_$ = nullptr;\n"
	     "   return temp;\n"
	     " }\n"
	     " inline $Submsg$* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $StrongRef$;\n"
	     "   if ($field_$ == nullptr) {\n"
	     "     auto* p = Super_::DefaultConstruct<$Submsg$>(GetArena());\n"
	     "     $field_$ = reinterpret_cast<$MemberType$*>(p);\n"
	     "   }\n"
	     "   return $cast_field_$;\n"
	     " }\n"
	     " inline $Submsg$* $nonnull$ $Msg$::mutable_$name$()\n"
	     "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
	     "   //~ TODO: add tests to make sure all write accessors are\n"
	     "   //~ able to prepare split message allocation.\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   $set_hasbit$;\n"
	     "   $Submsg$* _msg = _internal_mutable_$name_internal$();\n"
	     "   $annotate_mutable$;\n"
	     "   // @@protoc_insertion_point(field_mutable:$pkg.Msg.field$)\n"
	     "   return _msg;\n"
	     " }\n"
	     " //~ We handle the most common case inline, and delegate less common\n"
	     " //~ cases to the slow fallback function.\n"
	     " inline void $Msg$::set_allocated_$name$($Submsg$* $nullable$ value) {\n"
	     "   $WeakDescriptorSelfPin$;\n"
	     "   $pb$::Arena* message_arena = GetArena();\n"
	     "   $TsanDetectConcurrentMutation$;\n"
	     "   $PrepareSplitMessageForWrite$;\n"
	     "   if (message_arena == nullptr) {\n"
	     "     delete reinterpret_cast<$pb$::MessageLite*>($field_$);\n"
	     "   }\n"
	     "\n"
	     "   if (value != nullptr) {\n"
	     "     $pb$::Arena* submessage_arena = $arena_cast$(value)->GetArena();\n"
	     "     if (message_arena != submessage_arena) {\n"
	     "       value = $pbi$::GetOwnedMessage(message_arena, value, submessage_arena);\n"
	     "     }\n"
	     "     $set_hasbit$;\n"
	     "   } else {\n"
	     "     $clear_hasbit$;\n"
	     "   }\n"
	     "\n"
	     "   $field_$ = reinterpret_cast<$MemberType$*>(value);\n"
	     "   $annotate_set$;\n"
	     "   // @@protoc_insertion_point(field_set_allocated:$pkg.Msg.field$)\n"
	     " }\n",
	     SCB("update_hasbit", sm_update_hasbit, g));
	POP_VARS(p);
}

static void sm_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " if ($field_$ != nullptr) $field_$->Clear();\n");
}

static void sm_message_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $DCHK$(this_.$field_$ != nullptr);\n"
		 " this_.$field_$->Clear();\n");
}

static bool sm_requires_arena(const FieldGenerator *g) { return !g->should_split; }

static void sm_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_weak)
		EMIT0(p, "\n"
			 " if (_this->$field_$ == nullptr) {\n"
			 "   _this->$field_$ = from.$field_$->New(arena);\n"
			 " }\n"
			 " _this->$field_$->CheckTypeAndMergeFrom(*from.$field_$);\n");
	else if (g->should_split)
		EMIT0(p, "\n"
			 " _this->_internal_mutable_$name$()->$Submsg$::MergeFrom(\n"
			 "     from._internal_$name$());\n");
	else
		EMIT0(p, "\n"
			 " $DCHK$(from.$field_$ != nullptr);\n"
			 " if (_this->$field_$ == nullptr) {\n"
			 "   _this->$field_$ = Super_::CopyConstruct(arena, *from.$field_$);\n"
			 " } else {\n"
			 "   _this->$field_$->MergeFrom(*from.$field_$);\n"
			 " }\n");
}

static void sm_swapping_code(const FieldGenerator *g, LpPrinter *p) { EMIT0(p, "swap($field_$, other->$field_$);\n"); }

static void sm_destructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " delete $cached_split_ptr$->$name$_;\n");
	else
		EMIT0(p, "\n"
			 " delete this_.$field_$;\n");
}

static void sm_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " if (CheckHasBit(from.$has_bits_array$, $has_mask$)) {\n"
		 "   _this->$field_$ = Super_::CopyConstruct(arena, *from.$field_$);\n"
		 " }\n");
}

static void sm_serialize(const FieldGenerator *g, LpPrinter *p)
{
	if (!g->is_group)
		EMIT0(p, "\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, *this_.$field_$, this_.$field_$->GetCachedSize(), target,\n"
			 "     stream);\n");
	else
		EMIT0(p, "\n"
			 " target = stream->EnsureSpace(target);\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, *this_.$field_$, target, stream);\n");
}

static void sm_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$ +\n"
		 "               $pbi$::WireFormatLite::$DeclaredType$Size(*this_.$field_$);\n");
}

static bool sm_needs_is_initialized(const FieldGenerator *g) { return g->has_required; }

static void sm_is_initialized(const FieldGenerator *g, LpPrinter *p)
{
	if (!sm_needs_is_initialized(g))
		return;
	if (has_hasbit(g->field, g->options))
		EMIT0(p, "\n"
			 " if (CheckHasBit(this_.$has_bits_array$, $has_mask$)) {\n"
			 "   if (!this_.$field_$->IsInitialized()) return false;\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " if (this_._internal_has_$name$()) {\n"
			 "   if (!this_.$field_$->IsInitialized()) return false;\n"
			 " }\n");
}

static void sm_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " /*decltype($field_$)*/ nullptr,\n");
}

static void sm_copy_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " decltype($field_$){nullptr},\n");
}

static void sm_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " decltype(Impl_::Split::$name$_){nullptr},\n");
	else
		EMIT0(p, "\n"
			 " decltype($field_$){nullptr},\n");
}

static void sm_member_constructor(const FieldGenerator *g, LpPrinter *p) { EMIT0(p, "$name$_{nullptr}"); }

static void sm_member_copy_constructor(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "$name$_{Super_::CopyConstruct(arena, *from.$name$_)}");
}

static void sm_oneof_copy_construct(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $field_$ = Super_::CopyConstruct(arena, *from.$field_$);\n");
}

static const FieldGeneratorVtbl kSingularMessage = {
	.make_vars = sm_vars,
	.generate_private_members = sm_private_members,
	.requires_arena = sm_requires_arena,
	.generate_non_inline_accessor_definitions = fg_nothing,
	.generate_accessor_declarations = sm_accessor_declarations,
	.generate_inline_accessor_definitions = sm_inline_accessor_definitions,
	.generate_clearing_code = sm_clearing_code,
	.generate_message_clearing_code = sm_message_clearing_code,
	.generate_merging_code = sm_merging_code,
	.generate_swapping_code = sm_swapping_code,
	.generate_destructor_code = sm_destructor_code,
	.generate_copy_constructor_code = sm_copy_constructor_code,
	.generate_serialize_with_cached_sizes_to_array = sm_serialize,
	.generate_byte_size = sm_byte_size,
	.generate_is_initialized = sm_is_initialized,
	.needs_is_initialized = sm_needs_is_initialized,
	.generate_constexpr_aggregate_initializer = sm_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = sm_aggregate_initializer,
	.generate_copy_aggregate_initializer = sm_copy_aggregate_initializer,
	.generate_member_constexpr_constructor = sm_member_constructor,
	.generate_member_constructor = sm_member_constructor,
	.generate_member_copy_constructor = sm_member_copy_constructor,
	.generate_oneof_copy_construct = sm_oneof_copy_construct,
};

static void make_singular_message_generator(FieldGenerator *g)
{
	g->vt = &kSingularMessage;
	g->has_required = scc_has_required_fields(g->options->scc_analyzer, g->field->message_type);
	g->has_hasbit = has_hasbit(g->field, g->options);
}

/* ---- OneofMessage ---- */

static bool om_use_base_class(const FieldGenerator *g)
{
	if (g->is_weak)
		return true;
	const LpOneof *oneof = g->field->containing_oneof;
	int num_message_fields_in_oneof = 0;
	for (int i = 0; i < oneof->n_fields; ++i)
		num_message_fields_in_oneof += cpp_type(proto_oneof_field(oneof, i)) == LP_CPPTYPE_MESSAGE;
	return num_message_fields_in_oneof >= 3 && !is_likely_present(g->field, g->options);
}

static void om_vars(const FieldGenerator *g, Subs *out) { message_vars(g, out, g->is_weak, om_use_base_class(g)); }

static void om_non_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " void $Msg$::set_allocated_$name$($Submsg$* $nullable$ $name$) {\n"
		 "   $pb$::Arena* message_arena = GetArena();\n"
		 "   clear_$oneof_name$();\n"
		 "   if ($name$) {\n"
		 "     $pb$::Arena* submessage_arena = $arena_cast$($name$)->GetArena();\n"
		 "     if (message_arena != submessage_arena) {\n"
		 "       $name$ = $pbi$::GetOwnedMessage(message_arena, $name$, submessage_arena);\n"
		 "     }\n"
		 "     set_has_$name$();\n"
		 "     $field_$ = $name$;\n"
		 "   }\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_set_allocated:$pkg.Msg.field$)\n"
		 " }\n");
}

static void om_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	LpSub v[] = {S_("release_name", safe_function_name(g->field->containing_type, g->field, "release_"))};
	lp_push_vars(p, v, 1);
	EMIT0(p, "\n"
		 " inline $Submsg$* $nullable$ $Msg$::$release_name$() {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_release$;\n"
		 "   // @@protoc_insertion_point(field_release:$pkg.Msg.field$)\n"
		 "   $StrongRef$;\n"
		 "   if ($has_field$) {\n"
		 "     clear_has_$oneof_name$();\n"
		 "     auto* temp = $cast_field_$;\n"
		 "     if (GetArena() != nullptr) {\n"
		 "       temp = $pbi$::DuplicateIfNonNull(temp);\n"
		 "     }\n"
		 "     $field_$ = nullptr;\n"
		 "     return temp;\n"
		 "   } else {\n"
		 "     return nullptr;\n"
		 "   }\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline const $Submsg$& $Msg$::_internal_$name_internal$() const {\n"
		 "   $StrongRef$;\n"
		 "   return $has_field$ ? static_cast<const $Submsg$&>(*$cast_field_$)\n"
		 "                      : $kDefaultRef$;\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline const $Submsg$& $Msg$::$name$() const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Submsg$* $nullable$ $Msg$::unsafe_arena_release_$name$() {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_release$;\n"
		 "   // @@protoc_insertion_point(field_unsafe_arena_release:$pkg.Msg.field$)\n"
		 "   $StrongRef$;\n"
		 "   if ($has_field$) {\n"
		 "     clear_has_$oneof_name$();\n"
		 "     auto* temp = $cast_field_$;\n"
		 "     $field_$ = nullptr;\n"
		 "     return temp;\n"
		 "   } else {\n"
		 "     return nullptr;\n"
		 "   }\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline void $Msg$::unsafe_arena_set_allocated_$name$(\n"
		 "     $Submsg$* $nullable$ value) {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   // We rely on the oneof clear method to free the earlier contents\n"
		 "   // of this oneof. We can directly use the pointer we're given to\n"
		 "   // set the new value.\n"
		 "   clear_$oneof_name$();\n"
		 "   if (value) {\n"
		 "     set_has_$name_internal$();\n"
		 "     $field_$ = $cast_to_field$(value);\n"
		 "   }\n"
		 "   $annotate_set$;\n"
		 "   // @@protoc_insertion_point(field_unsafe_arena_set_allocated:$pkg.Msg.field$)\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Submsg$* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
		 "   $StrongRef$;\n"
		 "   if ($not_has_field$) {\n"
		 "     clear_$oneof_name$();\n"
		 "     set_has_$name_internal$();\n"
		 "     $field_$ = $cast_to_field$(Super_::DefaultConstruct<$Submsg$>(GetArena()));\n"
		 "   }\n"
		 "   return $cast_field_$;\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Submsg$* $nonnull$ $Msg$::mutable_$name$()\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $Submsg$* _msg = _internal_mutable_$name_internal$();\n"
		 "   $annotate_mutable$;\n"
		 "   // @@protoc_insertion_point(field_mutable:$pkg.Msg.field$)\n"
		 "   return _msg;\n"
		 " }\n");
	POP_VARS(p);
}

static void om_poison_or_clear(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (has_descriptor_methods(g->field->file, g->options))
		EMIT0(p, "\n"
			 " $pbi$::MaybePoisonAfterClear($field_$);\n");
	else
		EMIT0(p, "\n"
			 " if ($field_$ != nullptr) {\n"
			 "   $field_$->Clear();\n"
			 " }\n");
}

static void om_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " if (GetArena() == nullptr) {\n"
	     "   delete $field_$;\n"
	     " } else if ($pbi$::DebugHardenClearOneofMessageOnArena()) {\n"
	     "   $poison_or_clear$;\n"
	     " }\n",
	     SCB("poison_or_clear", om_poison_or_clear, g));
}

static void om_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " if (from._internal_has_$name$()) {\n"
		 "   _this->$field_$ = Super_::CopyConstruct(arena, *from.$field_$);\n"
		 " }\n");
}

static void om_is_initialized(const FieldGenerator *g, LpPrinter *p)
{
	if (!sm_needs_is_initialized(g))
		return;
	EMIT0(p, "\n"
		 " if (this_.$has_field$ && !this_.$field_$->IsInitialized())\n"
		 "   return false;\n");
}

static void om_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " if (oneof_needs_init) {\n"
	     "   _this->$field_$ = Super_::CopyConstruct(arena, *from.$field_$);\n"
	     " } else {\n"
	     "   _this->$field_$->$merge$(*from.$field_$);\n"
	     " }\n",
	     S_("merge", om_use_base_class(g) && !has_descriptor_methods(g->field->file, g->options)
				 ? "CheckTypeAndMergeFrom"
				 : "MergeFrom"));
}

static const FieldGeneratorVtbl kOneofMessage = {
	.make_vars = om_vars,
	.generate_private_members = sm_private_members,
	.requires_arena = fg_true,
	.generate_non_inline_accessor_definitions = om_non_inline_accessor_definitions,
	.generate_accessor_declarations = sm_accessor_declarations,
	.generate_inline_accessor_definitions = om_inline_accessor_definitions,
	.generate_clearing_code = om_clearing_code,
	.generate_message_clearing_code = om_clearing_code,
	.generate_merging_code = om_merging_code,
	.generate_swapping_code = fg_nothing,
	.generate_destructor_code = fg_nothing,
	.generate_copy_constructor_code = om_copy_constructor_code,
	.generate_serialize_with_cached_sizes_to_array = sm_serialize,
	.generate_byte_size = sm_byte_size,
	.generate_is_initialized = om_is_initialized,
	.needs_is_initialized = sm_needs_is_initialized,
	.generate_constexpr_aggregate_initializer = sm_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = sm_aggregate_initializer,
	.generate_copy_aggregate_initializer = sm_copy_aggregate_initializer,
	.generate_member_constexpr_constructor = sm_member_constructor,
	.generate_member_constructor = sm_member_constructor,
	.generate_member_copy_constructor = sm_member_copy_constructor,
	.generate_oneof_copy_construct = sm_oneof_copy_construct,
};

static void make_oneof_message_generator(FieldGenerator *g)
{
	g->vt = &kOneofMessage;
	g->has_required = scc_has_required_fields(g->options->scc_analyzer, g->field->message_type);
	g->has_hasbit = has_hasbit(g->field, g->options);
}

/* ---- RepeatedMessage ---- */

static void rm_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " $pbi$::RawPtr<$pb$::$Weak$RepeatedPtrField<$Submsg$>> $name$_;\n");
	else
		EMIT0(p, "$pb$::$Weak$RepeatedPtrField< $Submsg$ > $name$_;\n");
}

static const char *const kRmAccessorPrefixesSet[] = {"add_"};

static void rm_decl_field_accessors(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->cpp_repeated_type == CPP_REP_REPEATED)
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ const $pb$::RepeatedPtrField<$Submsg$>&\n"
			 " $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedPtrField<$Submsg$>* $nonnull$\n"
			 " $mutable_name$();\n");
	else
		EMIT0(p, "\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<const $Submsg$>\n"
			 " $name$() const;\n"
			 " [[nodiscard]] $DEPRECATED$ $pb$::RepeatedFieldProxy<$Submsg$> $mutable_name$();\n");
}

static void rm_maybe_weak_internal_accessors(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->is_weak)
		EMIT0(p, "\n"
			 " const $pb$::WeakRepeatedPtrField<$Submsg$>& _internal_weak_$name$()\n"
			 "     const;\n"
			 " $pb$::WeakRepeatedPtrField<$Submsg$>* $nonnull$ _internal_mutable_weak_$name$();\n");
}

static void rm_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v = {0}, vs = {0}, vm = {0};
	annotated_accessors(&v, g->field, kRepAccessorPrefixes, 3, 0);
	annotated_accessors(&vs, g->field, kRmAccessorPrefixesSet, 1, SEM_SET);
	annotated_accessors(&vm, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v);
	PUSH_SUBS(p, &vs);
	PUSH_SUBS(p, &vm);
	EMIT(p,
	     "\n"
	     " [[nodiscard]] $DEPRECATED$ const $Submsg$& $name$(int index) const;\n"
	     " [[nodiscard]] $DEPRECATED$ $Submsg$* $nonnull$ $mutable_name$(int index);\n"
	     " $DEPRECATED$ $Submsg$* $nonnull$ $add_name$();\n"
	     " $decl_field_accessors$;\n"
	     "\n"
	     " private:\n"
	     " const $pb$::RepeatedPtrField<$Submsg$>& $_internal_name$() const;\n"
	     " $pb$::RepeatedPtrField<$Submsg$>* $nonnull$ $_internal_mutable_name$();\n"
	     " $maybe_weak_internal_accessors$;\n"
	     "\n"
	     " public:\n",
	     SCB("decl_field_accessors", rm_decl_field_accessors, g),
	     SCB("maybe_weak_internal_accessors", rm_maybe_weak_internal_accessors, g));
	POP_VARS(p);
	POP_VARS(p);
	POP_VARS(p);
}

static void rm_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline const $Submsg$& $Msg$::$name$(int index) const\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_get:$pkg.Msg.field$)\n"
		 "   $StrongRef$;\n"
		 "   return _internal_$name_internal$().Get(index);\n"
		 " }\n");
	EMIT0(p, "\n"
		 " //~ Note: no need to set hasbit in mutable_$name$(int index).\n"
		 " //~ Hasbits only need to be updated if a new element is\n"
		 " //~ (potentially) added, not if an existing element is mutated.\n"
		 " inline $Submsg$* $nonnull$ $Msg$::mutable_$name$(int index)\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_mutable$;\n"
		 "   // @@protoc_insertion_point(field_mutable:$pkg.Msg.field$)\n"
		 "   $StrongRef$;\n"
		 "   return _internal_mutable_$name_internal$()->Mutable(index);\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Submsg$* $nonnull$ $Msg$::add_$name$()\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $TsanDetectConcurrentMutation$;\n"
		 "   $Submsg$* _add =\n"
		 "       _internal_mutable_$name_internal$()->InternalAddWithArena(\n"
		 "           $pb$::MessageLite::internal_visibility(), GetArena());\n"
		 "   $set_hasbit$;\n"
		 "   $annotate_add_mutable$;\n"
		 "   // @@protoc_insertion_point(field_add:$pkg.Msg.field$)\n"
		 "   return _add;\n"
		 " }\n");
	if (g->cpp_repeated_type == CPP_REP_REPEATED) {
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedPtrField<$Submsg$>& $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   $StrongRef$;\n"
			 "   return _internal_$name_internal$();\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedPtrField<$Submsg$>* $nonnull$\n"
			 " $Msg$::mutable_$name$() ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $StrongRef$;\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return _internal_mutable_$name_internal$();\n"
			 " }\n");
	} else {
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<const $Submsg$> $Msg$::$name$() const\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $annotate_list$;\n"
			 "   // @@protoc_insertion_point(field_list:$pkg.Msg.field$)\n"
			 "   $StrongRef$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       const $Submsg$>::Construct(_internal_$name_internal$());\n"
			 " }\n");
		EMIT0(p, "\n"
			 " inline $pb$::RepeatedFieldProxy<$Submsg$> $Msg$::mutable_$name$()\n"
			 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
			 "   $WeakDescriptorSelfPin$;\n"
			 "   $set_hasbit$;\n"
			 "   $annotate_mutable_list$;\n"
			 "   // @@protoc_insertion_point(field_mutable_list:$pkg.Msg.field$)\n"
			 "   $StrongRef$;\n"
			 "   $TsanDetectConcurrentMutation$;\n"
			 "   return $pbi$::RepeatedFieldProxyInternalPrivateAccessHelper<\n"
			 "       $Submsg$>::Construct(*_internal_mutable_$name_internal$(),\n"
			 "                            GetArena());\n"
			 " }\n");
	}
	if (g->should_split)
		EMIT0(p, "\n"
			 " inline const $pb$::$Weak$RepeatedPtrField<$Submsg$>&\n"
			 " $Msg$::_internal$_weak$_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return *$field_$;\n"
			 " }\n"
			 " inline $pb$::$Weak$RepeatedPtrField<$Submsg$>* $nonnull$\n"
			 " $Msg$::_internal_mutable$_weak$_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   $PrepareSplitMessageForWrite$;\n"
			 "   if ($field_$.IsDefault()) {\n"
			 "     $field_$.Set(\n"
			 "         Super_::DefaultConstruct<$pb$::$Weak$RepeatedPtrField<$Submsg$>>(\n"
			 "             GetArena()));\n"
			 "   }\n"
			 "   return $field_$.Get();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " inline const $pb$::$Weak$RepeatedPtrField<$Submsg$>&\n"
			 " $Msg$::_internal$_weak$_$name_internal$() const {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return $field_$;\n"
			 " }\n"
			 " inline $pb$::$Weak$RepeatedPtrField<$Submsg$>* $nonnull$\n"
			 " $Msg$::_internal_mutable$_weak$_$name_internal$() {\n"
			 "   $TsanDetectConcurrentRead$;\n"
			 "   return &$field_$;\n"
			 " }\n");
	if (g->is_weak)
		EMIT0(p, "\n"
			 " inline const $pb$::RepeatedPtrField<$Submsg$>&\n"
			 " $Msg$::_internal_$name_internal$() const {\n"
			 "   return _internal_weak_$name_internal$().weak;\n"
			 " }\n"
			 " inline $pb$::RepeatedPtrField<$Submsg$>* $nonnull$\n"
			 " $Msg$::_internal_mutable_$name_internal$() {\n"
			 "   return &_internal_mutable_weak_$name_internal$()->weak;\n"
			 " }\n");
}

static void rm_merging_body(LpPrinter *p, void *arg)
{
	EMIT0(p, "\n"
		 " _this->_internal_mutable$_weak$_$name$()->InternalMergeFromWithArena(\n"
		 "     $pb$::MessageLite::internal_visibility(), arena,\n"
		 "     from._internal$_weak$_$name$());\n");
}

static void rm_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	if (!g->should_split)
		rm_merging_body(p, NULL);
	else
		EMIT(p,
		     "\n"
		     " if (!from.$field_$.IsDefault()) {\n"
		     "   $body$;\n"
		     " }\n",
		     SCB("body", rm_merging_body, NULL));
}

static void rm_copy_constructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " if (!from._internal$_weak$_$name$().empty()) {\n"
			 "   _internal_mutable$_weak$_$name$()->InternalMergeFromWithArena(\n"
			 "       $pb$::MessageLite::internal_visibility(), arena,\n"
			 "       from._internal$_weak$_$name$());\n"
			 " }\n");
}

static void rm_serialize_field_weak(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->field->type == PT_MESSAGE)
		EMIT0(p, "\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, **it, (**it).GetCachedSize(), target, stream);\n");
	else
		EMIT0(p, "\n"
			 " target = stream->EnsureSpace(target);\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, **it, target, stream);\n");
}

static void rm_serialize_field(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->field->type == PT_MESSAGE)
		EMIT0(p, "\n"
			 " const auto& repfield = this_._internal_$name$().Get(i);\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, repfield, repfield.GetCachedSize(), target,\n"
			 "     stream);\n");
	else
		EMIT0(p, "\n"
			 " target = stream->EnsureSpace(target);\n"
			 " target = $pbi$::WireFormatLite::InternalWrite$DeclaredType$(\n"
			 "     $number$, this_._internal_$name$().Get(i), target,\n"
			 "     stream);\n");
}

static void rm_serialize(const FieldGenerator *g, LpPrinter *p)
{
	if (g->is_weak)
		EMIT(p,
		     "\n"
		     " for (auto it = this_.$field_$.pointer_begin(),\n"
		     "           end = this_.$field_$.pointer_end();\n"
		     "      it < end; ++it) {\n"
		     "   $serialize_field$;\n"
		     " }\n",
		     SCB("serialize_field", rm_serialize_field_weak, g));
	else
		EMIT(p,
		     "\n"
		     " for (unsigned i = 0, n = static_cast<unsigned>(\n"
		     "                          this_._internal_$name$_size());\n"
		     "      i < n; i++) {\n"
		     "   $serialize_field$;\n"
		     " }\n",
		     SCB("serialize_field", rm_serialize_field, g));
}

static void rm_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " total_size += $kTagBytes$UL * this_._internal_$name$_size();\n"
		 " for (const auto& msg : this_._internal$_weak$_$name$()) {\n"
		 "   total_size += $pbi$::WireFormatLite::$DeclaredType$Size(msg);\n"
		 " }\n");
}

static void rm_is_initialized(const FieldGenerator *g, LpPrinter *p)
{
	if (!sm_needs_is_initialized(g))
		return;
	if (g->is_weak)
		EMIT0(p, "\n"
			 " if (!$pbi$::AllAreInitializedWeak(this_.$field_$.weak))\n"
			 "   return false;\n");
	else
		EMIT0(p, "\n"
			 " if (!$pbi$::AllAreInitialized(this_._internal_$name$()))\n"
			 "   return false;\n");
}

static const FieldGeneratorVtbl kRepeatedMessage = {
	.make_vars = sm_vars,
	.generate_private_members = rm_private_members,
	.generate_accessor_declarations = rm_accessor_declarations,
	.generate_inline_accessor_definitions = rm_inline_accessor_definitions,
	.generate_clearing_code = rp_clearing_code,
	.generate_message_clearing_code = rp_message_clearing_code,
	.generate_merging_code = rm_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_copy_constructor_code = rm_copy_constructor_code,
	.generate_destructor_code = rp_destructor_code,
	.generate_serialize_with_cached_sizes_to_array = rm_serialize,
	.generate_byte_size = rm_byte_size,
	.generate_is_initialized = rm_is_initialized,
	.needs_is_initialized = sm_needs_is_initialized,
	.requires_arena = fg_true,
};

static void make_repeated_message_generator(FieldGenerator *g)
{
	g->vt = &kRepeatedMessage;
	g->has_required = scc_has_required_fields(g->options->scc_analyzer, g->field->message_type);
	g->cpp_repeated_type = calculate_cpp_repeated_type(g->field);
}
