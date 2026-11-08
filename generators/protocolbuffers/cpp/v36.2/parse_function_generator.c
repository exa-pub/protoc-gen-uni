// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/parse_function_generator.{h,cc} (protobuf v36.2). */

static int cmp_field_number(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	return (x->number > y->number) - (x->number < y->number);
}

/* GetOrderedFields: the fields by number */
static const LpField **get_ordered_fields(const LpMessage *d, int *n)
{
	const LpField **v = xalloc(sizeof(LpField *) * (size_t)(d->n_fields + 1));
	for (int i = 0; i < d->n_fields; i++)
		v[i] = &d->fields[i];
	qsort(v, (size_t)d->n_fields, sizeof *v, cmp_field_number);
	*n = d->n_fields;
	return v;
}

typedef int (*GetHasBitIndex)(const LpField *f, const void *arg); /* -1: none */

static const float kUnknownPresenceProbability = 0.5f;

/* ParseFunctionGenerator::BuildFieldOptions */
static TcFieldOptions *build_field_options(const LpMessage *d, const LpField *const *ordered_fields, int n,
					   GetHasBitIndex get_has_bit_index, const void *arg, const Options *o)
{
	TcFieldOptions *fields = xalloc(sizeof(TcFieldOptions) * (size_t)(n + 1));
	for (int i = 0; i < n; ++i) {
		const LpField *f = ordered_fields[i];
		int hasbit_index = get_has_bit_index ? get_has_bit_index(f, arg) : -1;
		int str_kind = 0;
		if (cpp_type(f) == LP_CPPTYPE_STRING) {
			if (is_string_inlined(f, o))
				str_kind = 1;
			else if (is_micro_string(f, o))
				fail("unsupported by protoc-gen-uni: experimental_cpp_micro_string");
		}
		fields[i] = (TcFieldOptions){
			.field = f,
			.has_bit_index = hasbit_index >= 0 ? hasbit_index : kNoHasbit,
			.presence_probability = kUnknownPresenceProbability,
			.lazy_opt = get_lazy_style(f, o),
			.is_implicitly_weak = is_implicit_weak_field(f, o),
			.use_direct_tcparser_table = true,
			.should_split = should_split(f, o),
			.str_kind = str_kind,
		};
	}
	return fields;
}

static TailCallTableInfo *build_tc_table_info_from_descriptor(const LpMessage *d, const Options *o,
							      const TcFieldOptions *field_options, int n)
{
	TcMessageOptions mo = {.is_lite = get_optimize_for(d->file, o) == OPT_LITE_RUNTIME, .uses_codegen = true};
	return tail_call_table_info_new(d, &mo, field_options, n);
}

static ParseFunctionGenerator *parse_function_generator_new(const LpMessage *d, bool has_hasbits,
							    GetHasBitIndex get_has_bit_index, const void *arg,
							    const Options *o, const Subs *vars,
							    int index_in_file_messages)
{
	ParseFunctionGenerator *g = xalloc(sizeof *g);
	g->descriptor = d;
	g->options = o;
	g->variables = subs_copy(vars);
	g->ordered_fields = get_ordered_fields(d, &g->n_ordered_fields);
	g->has_hasbits = has_hasbits;
	g->index_in_file_messages = index_in_file_messages;
	TcFieldOptions *fields = build_field_options(d, g->ordered_fields, g->n_ordered_fields, get_has_bit_index, arg, o);
	g->tc_table_info = build_tc_table_info_from_descriptor(d, o, fields, g->n_ordered_fields);
	set_common_message_data_variables(d, &g->variables);
	set_unknown_fields_variable(d, o, &g->variables);
	return g;
}

static int field_name_data_size(const TailCallTableInfo *t)
{
	return t->n_field_name_data == 0 ? 0 : (int)t->n_field_name_data + 1;
}

static void pfg_generate_alias_parse_table_type(ParseFunctionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT(p,
	     "\n"
	     " using ParseTableT_ =\n"
	     "     $pbi$::TcParseTable<$table_size_log2$, $num_field_entries$,\n"
	     "                         $num_field_aux$, $name_table_size$,\n"
	     "                         $field_lookup_size$>;\n",
	     SINT("table_size_log2", g->tc_table_info->table_size_log2),
	     SINT("num_field_entries", g->n_ordered_fields), SINT("num_field_aux", g->tc_table_info->n_aux_entries),
	     SINT("name_table_size", field_name_data_size(g->tc_table_info)),
	     SINT("field_lookup_size", num_to_entry_size16(&g->tc_table_info->num_to_entry_table)));
	POP_VARS(p);
}

static void pfg_section(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (!is_profile_driven(g->options))
		return;
}

static void pfg_generate_data_decls(ParseFunctionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT(p,
	     "\n"
	     " friend class $pbi$::TcParser;\n"
	     "#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
	     " $SECTION$\n"
	     " static const ParseTableT_ _table_;\n"
	     "#endif\n",
	     SCB("SECTION", pfg_section, g));
	POP_VARS(p);
}

static void pfg_generate_data_definitions(ParseFunctionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT0(p, "\n"
		 "#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
		 " PROTOBUF_CONSTINIT\n"
		 " PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 const $Msg$::ParseTableT_\n"
		 "     $Msg$::_table_ =\n"
		 "         $Msg$::InternalGenerateParseTable_($Msg$_class_data_.base());\n"
		 "#endif  // !PROTOBUF_MESSAGE_GLOBALS\n");
	POP_VARS(p);
}

static char *tc_parse_function_name(TcParseFunction func) { return xcat("::_pbi::TcParser::", tc_func_name(func)); }

static void pfg_has_bits_offset(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->has_hasbits || is_map_entry_message(g->descriptor))
		EMIT0(p, "PROTOBUF_FIELD_OFFSET($Msg$, _impl_._has_bits_),\n");
	else
		EMIT0(p, "\n"
			 " PROTOBUF_FIELD_OFFSET($Msg$,\n"
			 "                       _impl_._cached_size_),  // no hasbits\n");
}

static void pfg_extension_offset(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->descriptor->n_extension_ranges != 0)
		EMIT0(p, "PROTOBUF_FIELD_OFFSET($Msg$, $extensions$),\n");
	else
		EMIT0(p, "0, // no _extensions_\n");
}

static void pfg_field_entries_offset(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->n_ordered_fields == 0)
		EMIT0(p, "\n"
			 " offsetof(ParseTableT_, field_names),  // no field_entries\n");
	else
		EMIT0(p, "\n"
			 " offsetof(ParseTableT_, field_entries),\n");
}

static void pfg_aux_offset(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->tc_table_info->n_aux_entries == 0)
		EMIT0(p, "\n"
			 " offsetof(ParseTableT_, field_names),  // no aux_entries\n");
	else
		EMIT0(p, "\n"
			 " offsetof(ParseTableT_, aux_entries),\n");
}

static void pfg_post_loop_handler(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (needs_post_loop_handler(g->descriptor, g->options))
		EMIT0(p, "&$Msg$::PostLoopHandler,\n");
	else
		EMIT0(p, "nullptr,  // post_loop_handler\n");
}

static void pfg_to_prefetch(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const TailCallTableInfo *t = g->tc_table_info;
	const LpField **subtable_fields = xalloc(sizeof(LpField *) * (size_t)(t->n_aux_entries + 1));
	int n = 0;
	for (int i = 0; i < t->n_aux_entries; i++)
		if (t->aux_entries[i].type == AUX_SUB_TABLE)
			subtable_fields[n++] = t->aux_entries[i].field;
	const LpField *hottest = find_hottest_field(subtable_fields, n, g->options);
	EMIT(p,
	     "\n"
	     "#ifdef PROTOBUF_PREFETCH_PARSE_TABLE\n"
	     " ::_pbi::TcParser::GetTable<$hot_type$>(),  // to_prefetch\n"
	     "#endif  // PROTOBUF_PREFETCH_PARSE_TABLE",
	     S_("hot_type", qualified_class_name(hottest ? hottest->message_type : g->descriptor, g->options)));
}

static void pfg_table_base(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const TailCallTableInfo *t = g->tc_table_info;
	EMIT(p,
	     "\n"
	     " $has_bits_offset$,\n"
	     " $extension_offset$,\n"
	     " $max_field_number$, $fast_idx_mask$,  // max_field_number, fast_idx_mask\n"
	     " offsetof(ParseTableT_, field_lookup_table),\n"
	     " $skipmap32$,  // skipmap\n"
	     " $field_entries_offset$,\n"
	     " $num_field_entries$,  // num_field_entries\n"
	     " $num_aux_entries$,  // num_aux_entries\n"
	     " $aux_offset$,\n"
	     " class_data,\n"
	     " $post_loop_handler$,\n"
	     " $fallback$,  // fallback\n"
	     " $to_prefetch$",
	     SCB("has_bits_offset", pfg_has_bits_offset, g), SCB("extension_offset", pfg_extension_offset, g),
	     SINT("max_field_number", g->n_ordered_fields ? g->ordered_fields[g->n_ordered_fields - 1]->number : 0),
	     SINT("fast_idx_mask", ((1 << t->table_size_log2) - 1) << 3),
	     SUINT("skipmap32", t->num_to_entry_table.skipmap32),
	     SCB("field_entries_offset", pfg_field_entries_offset, g), SINT("num_field_entries", g->n_ordered_fields),
	     SINT("num_aux_entries", t->n_aux_entries), SCB("aux_offset", pfg_aux_offset, g),
	     SCB("post_loop_handler", pfg_post_loop_handler, g),
	     S_("fallback", tc_parse_function_name(t->fallback_function)), SCB("to_prefetch", pfg_to_prefetch, g));
}

static void pfg_aux_entries_list(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const Options *o = g->options;
	const TailCallTableInfo *t = g->tc_table_info;
	for (int i = 0; i < t->n_aux_entries; i++) {
		const AuxEntry *aux = &t->aux_entries[i];
		switch (aux->type) {
		case AUX_NOTHING:
			EMIT0(p, "{},\n");
			break;
		case AUX_SPLIT_OFFSET:
			EMIT0(p, "{_fl::Offset{offsetof($Msg$, _impl_._split_)}},\n");
			break;
		case AUX_SPLIT_SIZEOF:
			EMIT0(p, "{_fl::Offset{sizeof($Msg$::Impl_::Split)}},\n");
			break;
		case AUX_SUB_MESSAGE_GLOBALS:
			EMIT(p,
			     "\n"
			     " {::_pbi::FieldAuxMessageGlobals{}, &$name$},\n",
			     S_("name", qualified_msg_globals_instance_name(aux->field->message_type, o)));
			break;
		case AUX_SUB_TABLE:
			EMIT(p,
			     "\n"
			     "#ifndef PROTOBUF_MESSAGE_GLOBALS\n"
			     " {::_pbi::TcParser::GetTable<$sub_type$>()},\n"
			     "#else\n"
			     " {::_pbi::FieldAuxMessageGlobals(), &$sub_globals$},\n"
			     "#endif\n",
			     S_("sub_type", qualified_class_name(aux->field->message_type, o)),
			     S_("sub_globals", qualified_msg_globals_instance_name(aux->field->message_type, o)));
			break;
		case AUX_SUB_MESSAGE_GLOBALS_WEAK:
			EMIT(p, "{::_pbi::FieldAuxMessageGlobals{}, &$ptr$},\n",
			     S_("ptr", qualified_msg_globals_instance_ptr(aux->field->message_type, o)));
			break;
		case AUX_MESSAGE_VERIFY_FUNC:
			EMIT(p, "{$name$::InternalVerify},\n", S_("name", qualified_class_name(aux->field->message_type, o)));
			break;
		case AUX_SELF_VERIFY_FUNC:
			if (should_verify_message(g->descriptor, o))
				EMIT0(p, "{&InternalVerify},\n");
			else
				EMIT0(p, "{},\n");
			break;
		case AUX_ENUM_RANGE:
			EMIT(p, "{$first$, $last$},\n", SINT("first", aux->first), SINT("last", aux->last));
			break;
		case AUX_ENUM_VALIDATOR:
			EMIT(p, "{::_pbi::FieldAuxEnumData{}, $name$_internal_data_},\n",
			     S_("name", qualified_enum_class_name(aux->field->enum_type, o)));
			break;
		case AUX_NUMERIC_OFFSET:
			EMIT(p, "{_fl::Offset{$offset$}},\n", SUINT("offset", aux->offset));
			break;
		case AUX_MAP_AUX_INFO: {
			Utf8CheckMode utf8_check =
				get_utf8_check_mode(aux->field, get_optimize_for(aux->field->file, o) == OPT_LITE_RUNTIME);
			const LpField *mk = map_key(aux->field->message_type);
			const LpField *mv = map_value(aux->field->message_type);
			bool validated_enum = mv->type == PT_ENUM && !has_preserving_unknown_enum_semantics(mv);
			EMIT(p,
			     "\n"
			     " {::_pbi::TcParser::GetMapAuxInfo(\n"
			     "     $strict$, $validate$, $key_wire$, $value_wire$, $is_lite$)},\n",
			     SBOOL("strict", utf8_check == UTF8_STRICT), SBOOL("validate", validated_enum),
			     SINT("key_wire", mk->type), SINT("value_wire", mv->type),
			     SBOOL("is_lite", !has_descriptor_methods(aux->field->file, o)));
			break;
		}
		}
	}
}

static void pfg_fast_entries(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const Options *o = g->options;
	const TailCallTableInfo *t = g->tc_table_info;
	for (int i = 0; i < t->n_fast_path_fields; i++) {
		const FastFieldInfo *info = &t->fast_path_fields[i];
		if (info->kind == TC_FAST_NON_FIELD) {
			EMIT(p,
			     "\n"
			     " {$target$, {$coded_tag$, ::uint16_t{$nonfield_info$}}},\n",
			     S_("target", tc_parse_function_name(info->func)), SUINT("coded_tag", info->coded_tag),
			     SUINT("nonfield_info", info->nonfield_info));
		} else if (info->kind == TC_FAST_MP_FIELD) {
			print_field_comment(p, &g->variables, info->field, o);
			EMIT(p,
			     "\n"
			     " {$target$,\n"
			     "  {$coded_tag$, ::uint8_t{$function_index$},\n"
			     "   ::uint32_t{\n"
			     "       PROTOBUF_FIELD_OFFSET(ParseTableT_, field_entries) +\n"
			     "       sizeof(_pbi::TcParseTableBase::FieldEntry) * $index$}}},\n",
			     S_("target", tc_parse_function_name(info->func)), SUINT("coded_tag", info->coded_tag),
			     SUINT("function_index", info->function_index), SUINT("index", info->field_index));
		} else if (info->kind == TC_FAST_FIELD) {
			print_field_comment(p, &g->variables, info->field, o);
			char *func_name = tc_parse_function_name(info->func);
			if (get_optimize_for(info->field->file, o) == OPT_SPEED) {
				if (has_suffix(func_name, "V8S1") || has_suffix(func_name, "V32S1") ||
				    has_suffix(func_name, "V64S1")) {
					const char *field_type = has_suffix(func_name, "V8S1")	  ? "bool"
								 : has_suffix(func_name, "V32S1") ? "::uint32_t"
												  : "::uint64_t";
					func_name = xcat("::_pbi::TcParser::SingularVarintNoZag1<", field_type, ", offsetof(",
							 class_name(info->field->containing_type), ", ",
							 field_member_name(info->field, false), "), ",
							 cpp_utoa(info->hasbit_idx), ">()");
				}
			}
			EMIT(p,
			     "\n"
			     " {$target$,\n"
			     "  {$coded_tag$, $hasbit_idx$, $aux_idx$,\n"
			     "   PROTOBUF_FIELD_OFFSET($Msg$, $field_name$)}},\n",
			     S_("target", func_name), SUINT("coded_tag", info->coded_tag),
			     SUINT("hasbit_idx", info->hasbit_idx), SUINT("aux_idx", info->aux_idx),
			     S_("field_name", field_member_name(info->field, false)));
		} else {
			EMIT0(p, "\n"
				 " {::_pbi::TcParser::MiniParse, {}},\n");
		}
	}
}

static void pfg_field_lookup_table(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const NumToEntryTable *t = &g->tc_table_info->num_to_entry_table;
	for (int i = 0; i < t->n_blocks; i++) {
		const SkipEntryBlock *b = &t->blocks[i];
		EMIT(p, "$lower$, $upper$, $size$,\n", SUINT("lower", b->first_fnum & 65535),
		     SUINT("upper", b->first_fnum / 65536), SINT("size", b->n_entries));
		for (int j = 0; j < b->n_entries; j++)
			EMIT(p,
			     "\n"
			     " $skipmap$, $offset$,\n",
			     SUINT("skipmap", b->entries[j].skipmap), SUINT("offset", b->entries[j].field_entry_offset));
	}
	EMIT0(p, "65535, 65535\n");
}

static void pfg_field_entries(LpPrinter *p, void *arg);

static void pfg_aux_entries(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->tc_table_info->n_aux_entries == 0)
		EMIT0(p, "// no aux_entries\n");
	else
		EMIT(p,
		     "\n"
		     " {{\n"
		     "     $aux_entries_list$\n"
		     " }},\n",
		     SCB("aux_entries_list", pfg_aux_entries_list, g));
}

static void pfg_field_and_aux_entries(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	if (g->n_ordered_fields == 0 && !msg_option(g->descriptor, CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT)) {
		EMIT0(p, "// no field_entries, or aux_entries\n");
		return;
	}
	EMIT(p,
	     "\n"
	     " {{\n"
	     "   $field_entries$,\n"
	     " }},\n"
	     " $aux_entries$",
	     SCB("field_entries", pfg_field_entries, g), SCB("aux_entries", pfg_aux_entries, g));
}

typedef struct {
	const ParseFunctionGenerator *g;
	const FieldEntryInfo *entry;
	bool weak, split;
	const LpOneof *oneof;
} PfgEntryCtx;

static void pfg_entry_offset(LpPrinter *p, void *arg)
{
	const PfgEntryCtx *c = arg;
	if (c->weak)
		EMIT0(p, "/* weak */ 0,");
	else if (c->split)
		EMIT0(p, "PROTOBUF_FIELD_OFFSET($Msg$::Impl_::Split, $field_name$_),");
	else
		EMIT0(p, "PROTOBUF_FIELD_OFFSET($Msg$, $field_member_name$),");
}

static void pfg_entry_has_idx(LpPrinter *p, void *arg)
{
	const PfgEntryCtx *c = arg;
	if (c->oneof) {
		EMIT0(p, xcat("_Internal::kOneofCaseOffset + ", cpp_itoa(4 * c->oneof->index), ","));
	} else {
		char *hb = c->entry->hasbit_idx >= 0 ? xcat("_Internal::kHasBitsOffset + ", cpp_itoa(c->entry->hasbit_idx), ",")
						    : "-1,";
		EMIT0(p, hb);
	}
}

static void pfg_field_entries(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const Options *o = g->options;
	const TailCallTableInfo *t = g->tc_table_info;
	for (int i = 0; i < t->n_field_entries; i++) {
		const FieldEntryInfo *entry = &t->field_entries[i];
		const LpField *f = entry->field;
		print_field_comment(p, &g->variables, f, o);
		PfgEntryCtx c = {g, entry, is_weak(f, o), should_split(f, o), real_oneof(f)};
		LpSub v[] = {S_("field_name", field_name(f)), S_("field_member_name", field_member_name(f, false))};
		lp_push_vars(p, v, 2);
		EMIT(p,
		     "\n"
		     " {$offset$, $has_idx$, $aux_idx$, (0 | $type_card$)},\n",
		     SCB("offset", pfg_entry_offset, &c), SCB("has_idx", pfg_entry_has_idx, &c),
		     SUINT("aux_idx", entry->aux_idx), S_("type_card", type_card_to_string(entry->type_card)));
		POP_VARS(p);
	}
}

static void pfg_field_names(LpPrinter *p, void *arg)
{
	const ParseFunctionGenerator *g = arg;
	const TailCallTableInfo *t = g->tc_table_info;
	if (t->n_field_name_data == 0)
		return;
	int total_sizes = (int)((((size_t)t->n_field_entries + 1) + 7) & ~(size_t)7);
	const uint8_t *q = t->field_name_data;
	const uint8_t *sizes = q;
	const uint8_t *sizes_end = sizes + total_sizes;
	FORMAT(p, &g->variables, "\"");
	for (int i = 0; i < total_sizes; ++i) {
		int size = *q++;
		int octal_size = ((size >> 6) & 3) * 100 + ((size >> 3) & 7) * 10 + ((size >> 0) & 7);
		FORMAT(p, &g->variables, "\\$1$", cpp_itoa(octal_size));
	}
	FORMAT(p, &g->variables, "\"\n");
	for (; sizes < sizes_end; q += *sizes++)
		if (*sizes != 0)
			FORMAT(p, &g->variables, "\"$1$\"\n", xstrndup((const char *)q, *sizes));
}

static void pfg_generate_parse_table_helper_definition(ParseFunctionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	EMIT(p,
	     "\n"
	     "constexpr $Msg$::ParseTableT_ $Msg$::InternalGenerateParseTable_(const ::_pbi::ClassData* class_data) {\n"
	     "  return ParseTableT_{\n"
	     "    {\n"
	     "      $table_base$\n"
	     "    }, {{\n"
	     "      $fast_entries$\n"
	     "    }}, {{\n"
	     "      $field_lookup_table$\n"
	     "    }}, $field_and_aux_entries$,\n"
	     "    {{\n"
	     "      $field_names$,\n"
	     "    }},\n"
	     "  };\n"
	     "}\n",
	     S_("const", is_file_descriptor_proto(g->descriptor->file, g->options)
				 ? "constexpr"
				 : "PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1\nconst"),
	     SCB("table_base", pfg_table_base, g), SCB("fast_entries", pfg_fast_entries, g),
	     SCB("field_lookup_table", pfg_field_lookup_table, g),
	     SCB("field_and_aux_entries", pfg_field_and_aux_entries, g), SCB("field_names", pfg_field_names, g));
	POP_VARS(p);
}
