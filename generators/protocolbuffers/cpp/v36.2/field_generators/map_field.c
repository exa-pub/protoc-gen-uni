// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/field_generators/map_field.cc (protobuf v36.2). */

static char *ascii_upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *c = r; *c; c++)
		*c = ascii_toupper(*c);
	return r;
}

static bool map_lite(const FieldGenerator *g) { return !has_descriptor_methods(g->field->file, g->options); }

static void map_vars(const FieldGenerator *g, Subs *out)
{
	const Options *o = g->options;
	const LpField *key = g->key, *val = g->val;
	const char *key_type = primitive_type_name(o, cpp_type(key));
	const char *val_type;
	switch (cpp_type(val)) {
	case LP_CPPTYPE_MESSAGE:
		val_type = field_message_type_name(val, o);
		break;
	case LP_CPPTYPE_ENUM:
		val_type = qualified_enum_class_name(val->enum_type, o);
		break;
	default:
		val_type = primitive_type_name(o, cpp_type(val));
		break;
	}
	subs_push(out, S_("Map", xcat("::", protobuf_namespace(o), "::Map<", key_type, ", ", val_type, ">")));
	subs_push(out, S_("Entry", class_name(g->field->message_type)));
	subs_push(out, S_("Key", primitive_type_name(o, cpp_type(key))));
	subs_push(out, S_("Val", val_type));
	subs_push(out, S_("MapField", map_lite(g) ? "MapFieldLite" : "MapField"));
}

static void map_emit_funcs(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "_pbi::MapEntryFuncs<$Key$, $Val$,\n"
	     "                               _pbi::WireFormatLite::$key_wire_type$,\n"
	     "                               _pbi::WireFormatLite::$val_wire_type$>",
	     S_("key_wire_type", xcat("TYPE_", ascii_upper(declared_type_method_name(g->key->type)))),
	     S_("val_wire_type", xcat("TYPE_", ascii_upper(declared_type_method_name(g->val->type)))));
}

static void map_clearing_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " $field_$.Clear();\n");
}

static void map_merging_code(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " _this->$field_$.MergeFrom(from.$field_$);\n");
}

static void map_is_initialized(const FieldGenerator *g, LpPrinter *p)
{
	if (!sm_needs_is_initialized(g))
		return;
	EMIT0(p, "\n"
		 " if (!$pbi$::AllAreInitialized(this_.$field_$)) {\n"
		 "   return false;\n"
		 " }\n");
}

static void map_constexpr_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " /* decltype($field_$) */ {},\n");
}

static void map_aggregate_initializer(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " /* decltype($Msg$::Split::$name$_) */ {\n"
			 "     $pbi$::ArenaInitialized(),\n"
			 "     arena,\n"
			 " },\n");
	else
		EMIT0(p, "\n"
			 " /* decltype($field_$) */ {$pbi$::ArenaInitialized(), arena},\n");
}

static void map_destructor_code(const FieldGenerator *g, LpPrinter *p)
{
	if (g->should_split)
		EMIT0(p, "\n"
			 " $cached_split_ptr$->$name$_.~$MapField$();\n");
}

static void map_private_members(const FieldGenerator *g, LpPrinter *p)
{
	if (map_lite(g))
		EMIT0(p, "\n"
			 " $pbi$::MapFieldLite<$Key$, $Val$> $name$_;\n");
	else
		EMIT(p,
		     "\n"
		     " $pbi$::$MapField$<$Entry$, $Key$, $Val$> $name$_;\n",
		     S_("kKeyType", ascii_upper(declared_type_method_name(g->key->type))),
		     S_("kValType", ascii_upper(declared_type_method_name(g->val->type))));
}

static void map_accessor_declarations(const FieldGenerator *g, LpPrinter *p)
{
	Subs v1 = {0}, v2 = {0};
	annotated_accessors(&v1, g->field, kRepAccessorPrefixes, 3, 0);
	annotated_accessors(&v2, g->field, kRepAccessorPrefixesAlias, 1, SEM_ALIAS);
	PUSH_SUBS(p, &v1);
	PUSH_SUBS(p, &v2);
	EMIT0(p, "\n"
		 " [[nodiscard]] $DEPRECATED$ const $Map$& $name$() const;\n"
		 " [[nodiscard]] $DEPRECATED$ $Map$* $nonnull$ $mutable_name$();\n"
		 "\n"
		 " private:\n"
		 " const $Map$& $_internal_name$() const;\n"
		 " $Map$* $nonnull$ $_internal_mutable_name$();\n"
		 "\n"
		 " public:\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void map_inline_accessor_definitions(const FieldGenerator *g, LpPrinter *p)
{
	EMIT0(p, "\n"
		 " inline const $Map$& $Msg$::_internal_$name_internal$() const {\n"
		 "   $TsanDetectConcurrentRead$;\n"
		 "   return $field_$.GetMap();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline const $Map$& $Msg$::$name$() const ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $annotate_get$;\n"
		 "   // @@protoc_insertion_point(field_map:$pkg.Msg.field$)\n"
		 "   return _internal_$name_internal$();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Map$* $nonnull$ $Msg$::_internal_mutable_$name_internal$() {\n"
		 "   $PrepareSplitMessageForWrite$;\n"
		 "   $TsanDetectConcurrentMutation$;\n"
		 "   return $field_$.MutableMap();\n"
		 " }\n");
	EMIT0(p, "\n"
		 " inline $Map$* $nonnull$ $Msg$::mutable_$name$()\n"
		 "     ABSL_ATTRIBUTE_LIFETIME_BOUND {\n"
		 "   $WeakDescriptorSelfPin$;\n"
		 "   $set_hasbit$;\n"
		 "   $annotate_mutable$;\n"
		 "   // @@protoc_insertion_point(field_mutable_map:$pkg.Msg.field$)\n"
		 "   return _internal_mutable_$name_internal$();\n"
		 " }\n");
}

static void map_check_utf8(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->key->type == PT_STRING)
		generate_utf8_check_code_for_string(p, g->key, g->options, false,
						    "entry.first.data(), static_cast<int>(entry.first.length()),\n");
	if (g->val->type == PT_STRING)
		generate_utf8_check_code_for_string(p, g->val, g->options, false,
						    "entry.second.data(), static_cast<int>(entry.second.length()),\n");
}

static void map_funcs_semi(LpPrinter *p, void *arg)
{
	map_emit_funcs(arg, p);
	EMIT0(p, ";");
}

static void map_funcs(LpPrinter *p, void *arg) { map_emit_funcs(arg, p); }

static void map_serialize(const FieldGenerator *g, LpPrinter *p)
{
	bool string_key = g->key->type == PT_STRING;
	EMIT(p,
	     "\n"
	     " if (!this_._internal_$name$().empty()) {\n"
	     "   using MapType = $Map$;\n"
	     "   using WireHelper = $Funcs$;\n"
	     "   const auto& field = this_._internal_$name$();\n"
	     "\n"
	     "   if (stream->IsSerializationDeterministic() && field.size() > 1) {\n"
	     "     for (const auto& entry : $pbi$::$Sorter$<MapType>(field)) {\n"
	     "       target = WireHelper::InternalSerialize(\n"
	     "           $number$, entry.first, entry.second, target, stream);\n"
	     "       $CheckUtf8$;\n"
	     "     }\n"
	     "   } else {\n"
	     "     for (const auto& entry : field) {\n"
	     "       target = WireHelper::InternalSerialize(\n"
	     "           $number$, entry.first, entry.second, target, stream);\n"
	     "       $CheckUtf8$;\n"
	     "     }\n"
	     "   }\n"
	     " }\n",
	     S_("Sorter", string_key ? "MapSorterPtr" : "MapSorterFlat"), SCB("CheckUtf8", map_check_utf8, g),
	     SCB("Funcs", map_funcs_semi, g));
}

static void map_byte_size(const FieldGenerator *g, LpPrinter *p)
{
	EMIT(p,
	     "\n"
	     " total_size +=\n"
	     "     $kTagBytes$ * $pbi$::FromIntSize(this_._internal_$name$_size());\n"
	     " for (const auto& entry : this_._internal_$name$()) {\n"
	     "   total_size += $Funcs$::ByteSizeLong(entry.first, entry.second);\n"
	     " }\n",
	     SCB("Funcs", map_funcs, g));
}

static const FieldGeneratorVtbl kMapField = {
	.make_vars = map_vars,
	.generate_clearing_code = map_clearing_code,
	.generate_merging_code = map_merging_code,
	.generate_swapping_code = rp_swapping_code,
	.generate_copy_constructor_code = map_merging_code,
	.generate_is_initialized = map_is_initialized,
	.needs_is_initialized = sm_needs_is_initialized,
	.generate_constexpr_aggregate_initializer = map_constexpr_aggregate_initializer,
	.generate_copy_aggregate_initializer = map_constexpr_aggregate_initializer,
	.generate_aggregate_initializer = map_aggregate_initializer,
	.generate_destructor_code = map_destructor_code,
	.generate_private_members = map_private_members,
	.generate_accessor_declarations = map_accessor_declarations,
	.generate_inline_accessor_definitions = map_inline_accessor_definitions,
	.generate_serialize_with_cached_sizes_to_array = map_serialize,
	.generate_byte_size = map_byte_size,
};

static void make_map_generator(FieldGenerator *g)
{
	g->vt = &kMapField;
	g->key = map_key(g->field->message_type);
	g->val = map_value(g->field->message_type);
	g->has_required = scc_has_required_fields(g->options->scc_analyzer, g->field->message_type);
}
