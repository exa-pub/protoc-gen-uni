// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/enum.{h,cc} (protobuf v36.2), with GenerateEnumData
 * of src/google/protobuf/generated_enum_util.cc. */

static bool enum_value_deprecated(const LpEnumValue *v) { return pb_int(v->options, CPP_EVOPT_DEPRECATED, 0) != 0; }

static bool enum_name_uses_string_view(const LpEnum *e)
{
	return pb_int(pb_msg(e->merged_features, PB_CPP), CPPF_ENUM_NAME_USES_STRING_VIEW, 0) != 0;
}

static Subs enum_gen_vars(const EnumGenerator *g)
{
	const LpEnum *e = g->enum_;
	Subs v = {0};
	char *classname = enum_class_name(e);
	subs_push(&v, S_("Enum", e->name));
	subs_push(&v, S_("Enum_", lp_cpp_resolve_known_name_collisions(e->name, e->containing_type ? LP_CPP_CTX_MESSAGE : LP_CPP_CTX_FILE,
								       LP_CPP_KIND_TYPE)));
	subs_push(&v, S_("Msg_Enum", classname));
	subs_push(&v, S_("::Msg_Enum", qualified_enum_class_name(e, &g->options)));
	subs_push(&v, S_("Msg_Enum_", e->containing_type ? xcat(classname, "_") : ""));
	subs_push(&v, SINT("kMin", g->limits_min->number));
	subs_push(&v, SINT("kMax", g->limits_max->number));
	subs_push(&v, S_("return_type", enum_name_uses_string_view(e) ? "::absl::string_view" : "const ::std::string&"));
	subs_push(&v, S_("nodiscard", g->options.opensource_runtime ? "[[nodiscard]]" : ""));
	return v;
}

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static EnumGenerator *enum_generator_new(const LpEnum *descriptor, const Options *options)
{
	EnumGenerator *g = xalloc(sizeof *g);
	g->enum_ = descriptor;
	g->options = *options;
	int32_t max_value = descriptor->values[0].number;
	for (int i = 0; i < descriptor->n_values; i++)
		if (descriptor->values[i].number > max_value)
			max_value = descriptor->values[i].number;
	g->generate_array_size = max_value != INT32_MAX;
	g->has_reflection = has_descriptor_methods(descriptor->file, options);
	g->limits_min = g->limits_max = &descriptor->values[0];
	for (int i = 1; i < descriptor->n_values; ++i) {
		if (descriptor->values[i].number < g->limits_min->number)
			g->limits_min = &descriptor->values[i];
		if (descriptor->values[i].number > g->limits_max->number)
			g->limits_max = &descriptor->values[i];
	}
	size_t values_range = (size_t)(int64_t)g->limits_max->number - (size_t)(int64_t)g->limits_min->number;
	size_t total_values = (size_t)descriptor->n_values;
	g->should_cache = g->has_reflection && (values_range < 16u || values_range < total_values * 2u);
	g->sorted_unique_values = xalloc(sizeof(int) * (size_t)(descriptor->n_values + 1));
	for (int i = 0; i < descriptor->n_values; ++i)
		g->sorted_unique_values[i] = descriptor->values[i].number;
	qsort(g->sorted_unique_values, (size_t)descriptor->n_values, sizeof(int), cmp_int);
	int k = 0;
	for (int i = 0; i < descriptor->n_values; i++)
		if (k == 0 || g->sorted_unique_values[k - 1] != g->sorted_unique_values[i])
			g->sorted_unique_values[k++] = g->sorted_unique_values[i];
	g->n_sorted_unique_values = k;
	return g;
}

typedef struct {
	const EnumGenerator *g;
} EnumCtx;

static void enum_values_cb(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	for (int i = 0; i < g->enum_->n_values; ++i) {
		const LpEnumValue *value = &g->enum_->values[i];
		EMIT(p,
		     "\n"
		     " $Msg_Enum_VALUE$$ DEPRECATED$ = $kNumber$,\n",
		     SANN("Msg_Enum_VALUE", xcat(lp_lookup_var(p, "Msg_Enum_"), lp_cpp_enum_value_name(value)), value),
		     S_("kNumber", int32_to_string(value->number)),
		     S_("DEPRECATED", enum_value_deprecated(value) ? "[[deprecated]]" : ""));
	}
}

static void enum_open_sentinels_cb(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	if (proto_enum_is_closed(g->enum_))
		return;
	EMIT(p,
	     "\n"
	     " $Msg_Enum_Msg_Enum_$INT_MIN_SENTINEL_DO_NOT_USE_ =\n"
	     "     ::std::numeric_limits<::int32_t>::min(),\n"
	     " $Msg_Enum_Msg_Enum_$INT_MAX_SENTINEL_DO_NOT_USE_ =\n"
	     "     ::std::numeric_limits<::int32_t>::max(),\n",
	     S_("Msg_Enum_Msg_Enum_", xcat(lp_lookup_var(p, "Msg_Enum"), "_", lp_lookup_var(p, "Msg_Enum_"))));
}

static void enum_write_assert(LpPrinter *p, void *arg)
{
	EMIT0(p, "\n"
		 " static_assert(::std::is_same<T, $Msg_Enum$>::value ||\n"
		 "                   ::std::is_integral<T>::value,\n"
		 "               \"Incorrect type passed to $Enum$_Name().\");\n");
}

static void enum_generate_is_valid(const EnumGenerator *g, LpPrinter *p);

static void enum_generate_definition(EnumGenerator *g, LpPrinter *p)
{
	Subs v1 = enum_gen_vars(g);
	PUSH_SUBS(p, &v1);
	LpSub v2[] = {
		SANN("Msg_Enum_Enum_MIN", xcat(lp_lookup_var(p, "Msg_Enum_"), g->enum_->name, "_MIN"), g->enum_),
		SANN("Msg_Enum_Enum_MAX", xcat(lp_lookup_var(p, "Msg_Enum_"), g->enum_->name, "_MAX"), g->enum_),
	};
	lp_push_vars(p, v2, 2);
	EMIT(p,
	     "\n"
	     " enum $Msg_Enum_annotated$ : int {\n"
	     "   $values$,\n"
	     "   $open_enum_sentinels$,\n"
	     " };\n"
	     "\n"
	     " $dllexport_decl $extern const uint32_t $Msg_Enum$_internal_data_[];\n"
	     " inline constexpr $Msg_Enum$ $Msg_Enum_Enum_MIN$ =\n"
	     "     static_cast<$Msg_Enum$>($kMin$);\n"
	     " inline constexpr $Msg_Enum$ $Msg_Enum_Enum_MAX$ =\n"
	     "     static_cast<$Msg_Enum$>($kMax$);\n",
	     SCB("values", enum_values_cb, g), SANN("Msg_Enum_annotated", lp_lookup_var(p, "Msg_Enum"), g->enum_),
	     SCB("open_enum_sentinels", enum_open_sentinels_cb, g));
	enum_generate_is_valid(g, p);
	if (g->generate_array_size)
		EMIT(p,
		     "\n"
		     " inline constexpr int $Msg_Enum_Enum_ARRAYSIZE$ = $kMax$ + 1;\n",
		     SANN("Msg_Enum_Enum_ARRAYSIZE", xcat(lp_lookup_var(p, "Msg_Enum_"), g->enum_->name, "_ARRAYSIZE"),
			  g->enum_));
	if (g->has_reflection)
		EMIT0(p, "\n"
			 " $nodiscard $$dllexport_decl $const $pb$::EnumDescriptor* $nonnull$\n"
			 " $Msg_Enum$_descriptor();\n"
			 " //~ ADL Hook\n"
			 " $nodiscard $inline auto ProtobufInternalGetEnumDescriptor($Msg_Enum$) {\n"
			 "   return $Msg_Enum$_descriptor();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " $nodiscard $$return_type$ $Msg_Enum$_Name($Msg_Enum$ value);\n");
	if (g->should_cache || !g->has_reflection) {
		EMIT(p,
		     "\n"
		     " template <typename T>\n"
		     " $nodiscard $$return_type$ $Msg_Enum$_Name(T value) {\n"
		     "   $static_assert$;\n"
		     "   return $Msg_Enum$_Name(static_cast<$Msg_Enum$>(value));\n"
		     " }\n",
		     SCB("static_assert", enum_write_assert, NULL));
		if (g->should_cache)
			EMIT0(p, "\n"
				 " template <>\n"
				 " $nodiscard $inline $return_type$ $Msg_Enum$_Name($Msg_Enum$ value) {\n"
				 "   return $pbi$::NameOfDenseEnum<$Msg_Enum$_descriptor, $kMin$, $kMax$>(\n"
				 "       static_cast<int>(value));\n"
				 " }\n");
	} else {
		EMIT(p,
		     "\n"
		     " template <typename T>\n"
		     " $nodiscard $$return_type$ $Msg_Enum$_Name(T value) {\n"
		     "   $static_assert$;\n"
		     "   return $pbi$::NameOfEnum($Msg_Enum$_descriptor(), value);\n"
		     " }\n",
		     SCB("static_assert", enum_write_assert, NULL));
	}
	if (g->has_reflection)
		EMIT0(p, "\n"
			 " $nodiscard $inline bool $Msg_Enum$_Parse(\n"
			 "     //~\n"
			 "     ::absl::string_view name, $Msg_Enum$* $nonnull$ value) {\n"
			 "   return $pbi$::ParseNamedEnum<$Msg_Enum$>($Msg_Enum$_descriptor(), name,\n"
			 "                                            value);\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " $nodiscard $bool $Msg_Enum$_Parse(\n"
			 "     //~\n"
			 "     ::absl::string_view name, $Msg_Enum$* $nonnull$ value);\n");
	POP_VARS(p);
	POP_VARS(p);
}

static void enum_generate_get_enum_descriptor_specializations(EnumGenerator *g, LpPrinter *p)
{
	Subs v = enum_gen_vars(g);
	PUSH_SUBS(p, &v);
	EMIT0(p, "\n"
		 " template <>\n"
		 " struct is_proto_enum<$::Msg_Enum$> : std::true_type {};\n");
	if (g->has_reflection)
		EMIT0(p, "\n"
			 " template <>\n"
			 " inline const EnumDescriptor* $nonnull$ GetEnumDescriptor<$::Msg_Enum$>() {\n"
			 "   return $::Msg_Enum$_descriptor();\n"
			 " }\n");
	else
		EMIT0(p, "\n"
			 " template <>\n"
			 " struct internal::LiteEnumFuncs<$::Msg_Enum$> {\n"
			 "   static constexpr bool kIsDefined = true;\n"
			 "   static constexpr auto kParseFunc = $::Msg_Enum$_Parse;\n"
			 "   static constexpr auto kNameFunc = $::Msg_Enum$_Name<int>;\n"
			 " };\n");
	POP_VARS(p);
}

static void enum_generate_symbol_imports(const EnumGenerator *g, LpPrinter *p)
{
	Subs v = enum_gen_vars(g);
	PUSH_SUBS(p, &v);
	EMIT(p,
	     "\n"
	     " using $Enum_$ = $Msg_Enum$;\n",
	     SANN("Enum_", lp_lookup_var(p, "Enum_"), g->enum_));
	for (int j = 0; j < g->enum_->n_values; ++j) {
		const LpEnumValue *value = &g->enum_->values[j];
		bool deprecated = enum_value_deprecated(value);
		EMIT(p,
		     "\n"
		     " $IGNORE_DEPRECATION_START$\n"
		     " $DEPRECATED $static constexpr $Enum_$ $VALUE$ = $Msg_Enum$_$VALUE$;\n"
		     " $IGNORE_DEPRECATION_STOP$\n",
		     SANN("VALUE", lp_cpp_enum_value_name(value), value), S_("DEPRECATED", deprecated ? "[[deprecated]]" : ""),
		     S_("IGNORE_DEPRECATION_START", deprecated ? "PROTOBUF_IGNORE_DEPRECATION_START" : ""),
		     S_("IGNORE_DEPRECATION_STOP", deprecated ? "PROTOBUF_IGNORE_DEPRECATION_STOP" : ""));
	}
	EMIT(p,
	     "\n"
	     " $nodiscard $static inline bool $Enum$_IsValid(int value) {\n"
	     "   return $Msg_Enum$_IsValid(value);\n"
	     " }\n"
	     " static constexpr $Enum_$ $Enum_MIN$ = $Msg_Enum$_$Enum$_MIN;\n"
	     " static constexpr $Enum_$ $Enum_MAX$ = $Msg_Enum$_$Enum$_MAX;\n",
	     SANN("Enum_MIN", xcat(g->enum_->name, "_MIN"), g->enum_),
	     SANN("Enum_MAX", xcat(g->enum_->name, "_MAX"), g->enum_));
	if (g->generate_array_size)
		EMIT(p,
		     "\n"
		     " static constexpr int $Enum_ARRAYSIZE$ = $Msg_Enum$_$Enum$_ARRAYSIZE;\n",
		     SANN("Enum_ARRAYSIZE", xcat(g->enum_->name, "_ARRAYSIZE"), g->enum_));
	if (g->has_reflection)
		EMIT0(p, "\n"
			 " static inline const $pb$::EnumDescriptor* $nonnull$ $Enum$_descriptor() {\n"
			 "   return $Msg_Enum$_descriptor();\n"
			 " }\n");
	EMIT0(p, "\n"
		 " template <typename T>\n"
		 " $nodiscard $static inline $return_type$ $Enum$_Name(T value) {\n"
		 "   return $Msg_Enum$_Name(value);\n"
		 " }\n"
		 " $nodiscard $static inline bool $Enum$_Parse(\n"
		 "     //~\n"
		 "     ::absl::string_view name, $Enum_$* $nonnull$ value) {\n"
		 "   return $Msg_Enum$_Parse(name, value);\n"
		 " }\n");
	POP_VARS(p);
}

static void enum_generate_is_valid(const EnumGenerator *g, LpPrinter *p)
{
	Subs v = enum_gen_vars(g);
	PUSH_SUBS(p, &v);
	const int *s = g->sorted_unique_values;
	int n = g->n_sorted_unique_values;
	if ((int64_t)s[0] + (int64_t)n - 1 == (int64_t)s[n - 1]) {
		EMIT(p,
		     "\n"
		     " $nodiscard $inline bool $Msg_Enum$_IsValid(int value) {\n"
		     "   return $min$ <= value && value <= $max$;\n"
		     " }\n",
		     SINT("min", s[0]), SINT("max", s[n - 1]));
	} else if (s[0] >= 0 && s[n - 1] < 64) {
		uint64_t bitmap = 0;
		for (int i = 0; i < n; i++)
			bitmap |= (uint64_t)1 << s[i];
		EMIT(p,
		     "\n"
		     " $nodiscard $inline bool $Msg_Enum$_IsValid(int value) {\n"
		     "   return 0 <= value && value <= $max$ && (($bitmap$u >> value) & 1) != 0;\n"
		     " }\n",
		     SUINT("bitmap", bitmap), SINT("max", s[n - 1]));
	} else {
		EMIT0(p, "\n"
			 " $nodiscard $inline bool $Msg_Enum$_IsValid(int value) {\n"
			 "   return $pbi$::ValidateEnum(value, $Msg_Enum$_internal_data_);\n"
			 " }\n");
	}
	POP_VARS(p);
}

/* ---- GenerateEnumData (generated_enum_util.cc) ---- */

static void eytzinger_sort(const int32_t *input, size_t n, uint32_t *output, size_t *i, size_t output_index)
{
	if (output_index < n) {
		eytzinger_sort(input, n, output, i, 2 * output_index + 1);
		output[output_index] = (uint32_t)input[(*i)++];
		eytzinger_sort(input, n, output, i, 2 * output_index + 2);
	}
}

static uint32_t *generate_enum_data(const int32_t *values, int n_values, size_t *n_out)
{
	const size_t kBitmapBlockSize = 32;
	int32_t *too_large = xalloc(sizeof(int32_t) * (size_t)(n_values + 1));
	int32_t *after_bitmap = xalloc(sizeof(int32_t) * (size_t)(n_values + 1));
	size_t n_too_large = 0, n_after_bitmap = 0;
	uint32_t *bitmap_values = NULL;
	size_t n_bitmap = 0;
	bool has_start = false;
	int16_t start_sequence = 0;
	uint32_t sequence_length = 0;
	for (int i = 0; i < n_values; i++) {
		int32_t v = values[i];
		if (!has_start) {
			if ((int16_t)v != v) {
				too_large[n_too_large++] = v;
				continue;
			}
			has_start = true;
			start_sequence = (int16_t)v;
			sequence_length = 1;
			continue;
		}
		if (v == (int32_t)start_sequence + (int32_t)sequence_length && sequence_length < 0xFFFF) {
			++sequence_length;
			continue;
		}
#define ENUM_ADJUST(x) ((uint32_t)((int64_t)(x) - start_sequence - (int64_t)sequence_length))
#define ENUM_ADD_BIT(bit) (bitmap_values[(bit) / kBitmapBlockSize] |= (uint32_t)1 << ((bit) % kBitmapBlockSize))
		uint32_t adjusted = ENUM_ADJUST(v);
		if (adjusted < kBitmapBlockSize * n_bitmap) {
			ENUM_ADD_BIT(adjusted);
			continue;
		}
		size_t cost_if_fallback = n_bitmap + (1 + n_after_bitmap);
		size_t rounded_bitmap_size = ((size_t)adjusted + kBitmapBlockSize) / kBitmapBlockSize;
		size_t cost_if_collapse = rounded_bitmap_size;
		if (cost_if_collapse <= cost_if_fallback && kBitmapBlockSize * rounded_bitmap_size < 0x10000) {
			bitmap_values = xrealloc(bitmap_values, sizeof(uint32_t) * n_bitmap, sizeof(uint32_t) * rounded_bitmap_size);
			n_bitmap = rounded_bitmap_size;
			for (size_t j = 0; j < n_after_bitmap; j++)
				ENUM_ADD_BIT(ENUM_ADJUST(after_bitmap[j]));
			n_after_bitmap = 0;
			ENUM_ADD_BIT(adjusted);
		} else {
			after_bitmap[n_after_bitmap++] = v;
		}
#undef ENUM_ADJUST
#undef ENUM_ADD_BIT
	}
	/* std::merge of the two sorted fallback lists */
	size_t n_fallback = n_too_large + n_after_bitmap;
	int32_t *fallback = xalloc(sizeof(int32_t) * (n_fallback + 1));
	size_t a = 0, b = 0, k = 0;
	while (a < n_too_large && b < n_after_bitmap)
		fallback[k++] = after_bitmap[b] < too_large[a] ? after_bitmap[b++] : too_large[a++];
	while (a < n_too_large)
		fallback[k++] = too_large[a++];
	while (b < n_after_bitmap)
		fallback[k++] = after_bitmap[b++];
	size_t n = 2 + n_bitmap + n_fallback;
	uint32_t *out = xalloc(sizeof(uint32_t) * n);
	out[0] = (uint32_t)(uint16_t)(has_start ? start_sequence : 0) | (sequence_length << 16);
	out[1] = (uint32_t)(kBitmapBlockSize * n_bitmap) | (uint32_t)(n_fallback << 16);
	if (n_bitmap)
		memcpy(out + 2, bitmap_values, sizeof(uint32_t) * n_bitmap);
	size_t idx = 0;
	eytzinger_sort(fallback, n_fallback, out + 2 + n_bitmap, &idx, 0);
	*n_out = n;
	return out;
}

static void enum_encoded_cb(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	size_t n = 0;
	uint32_t *data = generate_enum_data(g->sorted_unique_values, g->n_sorted_unique_values, &n);
	for (size_t i = 0; i < n; i++)
		EMIT(p, "$n$u, ", SUINT("n", data[i]));
}

typedef struct {
	const char *name;
	int number;
	size_t index, byte_offset, len;
} EnumOffset;

typedef struct {
	const EnumGenerator *g;
	EnumOffset *by_name; /* name_to_number in name order (unique names) */
	int n_names;
	EnumOffset *by_number;
	int n_by_number;
	Map canonical; /* number -> first name */
} EnumLiteCtx;

static int cmp_enum_offset_name(const void *a, const void *b)
{
	return strcmp(((const EnumOffset *)a)->name, ((const EnumOffset *)b)->name);
}

/* stable sort by number: ties keep the name order (index) */
static int cmp_enum_offset_number(const void *a, const void *b)
{
	const EnumOffset *x = a, *y = b;
	if (x->number != y->number)
		return x->number < y->number ? -1 : 1;
	return (x->index > y->index) - (x->index < y->index);
}

static void enum_names_cb(LpPrinter *p, void *arg)
{
	const EnumLiteCtx *c = arg;
	for (int i = 0; i < c->n_names; i++)
		EMIT(p,
		     "\n"
		     " \"$name$\"\n",
		     S_("name", c->by_name[i].name));
}

static void enum_entries_cb(LpPrinter *p, void *arg)
{
	const EnumLiteCtx *c = arg;
	for (int i = 0; i < c->n_names; i++)
		EMIT(p,
		     "\n"
		     " {{&$Msg_Enum$_names[$offset$], $len$}, $number$},\n",
		     SINT("number", c->by_name[i].number), SUINT("offset", c->by_name[i].byte_offset),
		     SUINT("len", c->by_name[i].len));
}

static void enum_entries_by_number_cb(LpPrinter *p, void *arg)
{
	const EnumLiteCtx *c = arg;
	for (int i = 0; i < c->n_by_number; i++)
		EMIT(p,
		     "\n"
		     " $index$,  // $number$ -> $name$\n",
		     SINT("number", c->by_number[i].number), SUINT("index", c->by_number[i].index),
		     S_("name", (const char *)map_get(&c->canonical, cpp_itoa(c->by_number[i].number))));
}

static void enum_generate_methods(EnumGenerator *g, int idx, LpPrinter *p)
{
	Subs v = enum_gen_vars(g);
	PUSH_SUBS(p, &v);
	if (g->has_reflection)
		EMIT(p,
		     "\n"
		     " $nodiscard $const $pb$::EnumDescriptor* $nonnull$\n"
		     " $Msg_Enum$_descriptor() {\n"
		     "   $pbi$::AssignDescriptors(&$desc_table$);\n"
		     "   return $file_level_enum_descriptors$[$idx$];\n"
		     " }\n",
		     SINT("idx", idx));
	EMIT(p,
	     "\n"
	     " PROTOBUF_CONSTINIT const uint32_t $Msg_Enum$_internal_data_[] = {\n"
	     "     $encoded$};\n",
	     SCB("encoded", enum_encoded_cb, g));
	if (!g->has_reflection) {
		const LpEnum *e = g->enum_;
		EnumLiteCtx c = {.g = g};
		/* name_to_number: btree_map (sorted by name, first emplace wins) */
		EnumOffset *names = xalloc(sizeof(EnumOffset) * (size_t)(e->n_values + 1));
		int n = 0;
		for (int i = 0; i < e->n_values; ++i) {
			const LpEnumValue *value = &e->values[i];
			names[n++] = (EnumOffset){.name = value->name, .number = value->number}; /* names are unique */
			char *key = cpp_itoa(value->number);
			if (!map_has(&c.canonical, key))
				map_put(&c.canonical, key, (void *)value->name);
		}
		qsort(names, (size_t)n, sizeof(EnumOffset), cmp_enum_offset_name);
		size_t index = 0, offset = 0;
		for (int i = 0; i < n; i++) {
			names[i].index = index++;
			names[i].byte_offset = offset;
			names[i].len = strlen(names[i].name);
			offset += names[i].len;
		}
		c.by_name = names;
		c.n_names = n;
		/* offsets_by_number: stable sort by number, unique by number */
		EnumOffset *by_number = xalloc(sizeof(EnumOffset) * (size_t)(n + 1));
		memcpy(by_number, names, sizeof(EnumOffset) * (size_t)n);
		qsort(by_number, (size_t)n, sizeof(EnumOffset), cmp_enum_offset_number);
		int k = 0;
		for (int i = 0; i < n; i++)
			if (k == 0 || by_number[k - 1].number != by_number[i].number)
				by_number[k++] = by_number[i];
		c.by_number = by_number;
		c.n_by_number = k;
		LpSub vn[] = {SUINT("num_unique", c.canonical.n)};
		lp_push_vars(p, vn, 1);
		EMIT(p,
		     "\n"
		     " static const char $Msg_Enum$_names[] = {\n"
		     "     $names$,\n"
		     " };\n"
		     "\n"
		     " static const $pbi$::EnumEntry $Msg_Enum$_entries[] = {\n"
		     "     $entries$,\n"
		     " };\n"
		     "\n"
		     " static const int $Msg_Enum$_entries_by_number[] = {\n"
		     "     $entries_by_number$,\n"
		     " };\n"
		     "\n"
		     " $nodiscard $bool $Msg_Enum$_Parse(::absl::string_view name,\n"
		     "                                   $Msg_Enum$* $nonnull$ value) {\n"
		     "   int int_value;\n"
		     "   bool success = $pbi$::LookUpEnumValue(\n"
		     "       $Msg_Enum$_entries, $num_declared$, name, &int_value);\n"
		     "   if (success) {\n"
		     "     *value = static_cast<$Msg_Enum$>(int_value);\n"
		     "   }\n"
		     "   return success;\n"
		     " }\n",
		     SINT("num_declared", e->n_values), SCB("names", enum_names_cb, &c), SCB("entries", enum_entries_cb, &c),
		     SCB("entries_by_number", enum_entries_by_number_cb, &c));
		if (enum_name_uses_string_view(e))
			EMIT0(p, "\n"
				 " $nodiscard $::absl::string_view $Msg_Enum$_Name($Msg_Enum$ value) {\n"
				 "   int idx = $pbi$::LookUpEnumName($Msg_Enum$_entries,\n"
				 "                                   $Msg_Enum$_entries_by_number,\n"
				 "                                   $num_unique$, value);\n"
				 "   return idx == -1\n"
				 "              ? \"\"\n"
				 "              : $Msg_Enum$_entries[$Msg_Enum$_entries_by_number[idx]]\n"
				 "                    .name;\n"
				 " }\n");
		else
			EMIT0(p, "\n"
				 " static $pbi$::ExplicitlyConstructed<::std::string>\n"
				 "     $Msg_Enum$_strings[$num_unique$] = {};\n"
				 "\n"
				 " $nodiscard $const ::std::string& $Msg_Enum$_Name($Msg_Enum$ value) {\n"
				 "   static const bool kDummy = $pbi$::InitializeEnumStrings(\n"
				 "       $Msg_Enum$_entries, $Msg_Enum$_entries_by_number, $num_unique$,\n"
				 "       $Msg_Enum$_strings);\n"
				 "   (void)kDummy;\n"
				 "\n"
				 "   int idx = $pbi$::LookUpEnumName($Msg_Enum$_entries,\n"
				 "                                   $Msg_Enum$_entries_by_number,\n"
				 "                                   $num_unique$, value);\n"
				 "   return idx == -1 ? $pbi$::GetEmptyString() : $Msg_Enum$_strings[idx].get();\n"
				 " }\n");
		POP_VARS(p);
	}
	POP_VARS(p);
}
