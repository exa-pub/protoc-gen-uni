// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/message_layout_helper.{h,cc}, padding_optimizer.h,
 * field_layout.{h,cc} and field_chunk.{h,cc} (protobuf v36.2). The open-source build uses the
 * PaddingOptimizer (no profile: every field is hot). */

/* ---- FieldGroup ---- */

typedef struct {
	const LpField **fields;
	int n;
	float preferred_location;
	uint32_t estimated_memory_size;
} FieldGroup;

static FieldGroup field_group_single(float preferred_location, const LpField *f)
{
	FieldGroup g = {0};
	g.fields = xalloc(sizeof(LpField *));
	g.fields[0] = f;
	g.n = 1;
	g.preferred_location = preferred_location;
	g.estimated_memory_size = (uint32_t)estimate_alignment_size(f);
	return g;
}

/* FieldGroup::Append (UpdatePreferredLocationAndInsertOtherFields) */
static void field_group_append(FieldGroup *g, const FieldGroup *other)
{
	if (other->n == 0)
		return;
	g->preferred_location = (g->preferred_location * (float)(size_t)g->n +
				 (other->preferred_location * (float)(size_t)other->n)) /
				(float)((size_t)g->n + (size_t)other->n);
	g->estimated_memory_size += other->estimated_memory_size;
	const LpField **fields = xalloc(sizeof(LpField *) * (size_t)(g->n + other->n));
	if (g->n)
		memcpy(fields, g->fields, sizeof(LpField *) * (size_t)g->n);
	memcpy(fields + g->n, other->fields, sizeof(LpField *) * (size_t)other->n);
	g->fields = fields;
	g->n += other->n;
}

typedef struct {
	FieldGroup *v;
	int n, cap;
} FieldGroups;

static void groups_push(FieldGroups *gs, FieldGroup g)
{
	if (gs->n == gs->cap) {
		int cap = gs->cap ? 2 * gs->cap : 8;
		gs->v = xrealloc(gs->v, sizeof(FieldGroup) * (size_t)gs->cap, sizeof(FieldGroup) * (size_t)cap);
		gs->cap = cap;
	}
	gs->v[gs->n++] = g;
}

typedef bool (*GroupLess)(const FieldGroup *a, const FieldGroup *b);

/* std::stable_sort (merge sort) */
static void groups_stable_sort(FieldGroups *gs, GroupLess less)
{
	int n = gs->n;
	if (n < 2)
		return;
	FieldGroup *tmp = xalloc(sizeof(FieldGroup) * (size_t)n);
	for (int width = 1; width < n; width *= 2) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = lo + width < n ? lo + width : n;
			int hi = lo + 2 * width < n ? lo + 2 * width : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi) {
				if (less(&gs->v[j], &gs->v[i]))
					tmp[k++] = gs->v[j++];
				else
					tmp[k++] = gs->v[i++];
			}
			while (i < mid)
				tmp[k++] = gs->v[i++];
			while (j < hi)
				tmp[k++] = gs->v[j++];
		}
		memcpy(gs->v, tmp, sizeof(FieldGroup) * (size_t)n);
	}
}

static bool group_less_location(const FieldGroup *a, const FieldGroup *b)
{
	return a->preferred_location < b->preferred_location;
}
static bool group_less_size(const FieldGroup *a, const FieldGroup *b)
{
	return a->estimated_memory_size < b->estimated_memory_size;
}
static bool group_greater_size(const FieldGroup *a, const FieldGroup *b)
{
	return a->estimated_memory_size > b->estimated_memory_size;
}

/* ---- MessageLayoutHelper ---- */

enum { HOT_SPLIT, HOT_COLD, HOT_WARM, HOT_HOT, HOT_REPEATED, HOT_FAST_PARSE, kMaxHotness };
enum { FAM_REPEATED = 0, FAM_STRING = 1, FAM_MESSAGE = 2, FAM_ZERO_INITIALIZABLE = 3, FAM_OTHER = 4, kMaxFamily };

typedef FieldGroups FieldPartitionArray[kMaxFamily][kMaxHotness];

static int get_field_family(const LpField *f, const Options *o)
{
	if (is_repeated(f))
		return should_split(f, o) ? FAM_OTHER : FAM_REPEATED;
	if (cpp_type(f) == LP_CPPTYPE_STRING)
		return FAM_STRING;
	if (cpp_type(f) == LP_CPPTYPE_MESSAGE)
		return FAM_MESSAGE;
	if (can_initialize_by_zeroing(f, o))
		return FAM_ZERO_INITIALIZABLE;
	return FAM_OTHER;
}

static TailCallTableInfo *build_fast_parse_table(const LpMessage *d, const Options *o)
{
	int n = 0;
	const LpField **all = get_ordered_fields(d, &n);
	const LpField **ordered = xalloc(sizeof(LpField *) * (size_t)(n + 1));
	int k = 0;
	for (int i = 0; i < n; i++)
		if (is_layout_optimized(all[i], o))
			ordered[k++] = all[i];
	TcFieldOptions *field_options = build_field_options(d, ordered, k, NULL, NULL, o);
	return build_tc_table_info_from_descriptor(d, o, field_options, k);
}

static bool is_fast_path_field(const LpField *f, const TailCallTableInfo *t)
{
	if (!is_field_type_eligible_for_fast_parsing(f))
		return false;
	uint32_t coded_tag = recode_tag_for_fast_parsing(wire_format_make_tag(f));
	uint32_t fast_idx = tag_to_idx(coded_tag, (uint32_t)t->n_fast_path_fields);
	const FastFieldInfo *fast_field = &t->fast_path_fields[fast_idx];
	return fast_field->kind == TC_FAST_FIELD && fast_field->field == f;
}

static bool should_promote_to_fast_parse(const LpField *f, int hotness, const TailCallTableInfo *t)
{
	if (hotness < HOT_WARM)
		return false;
	return is_fast_path_field(f, t);
}

/* ConsolidateAlignedFieldGroups */
static void consolidate_aligned_field_groups(FieldGroups *field_groups, size_t alignment, size_t target_alignment,
					     FieldGroups *out)
{
	size_t size_inflation = target_alignment / alignment;
	for (int h = 0; h < kMaxHotness; ++h) {
		FieldGroups *partition = &field_groups[h];
		groups_stable_sort(partition, group_less_location);
		for (size_t i = 0; i < (size_t)partition->n; i += size_inflation) {
			FieldGroup field_group = {0};
			for (size_t j = i; j < (size_t)partition->n && j < i + size_inflation; ++j)
				field_group_append(&field_group, &partition->v[j]);
			groups_push(&out[h], field_group);
		}
	}
}

static void fill_padding_from_partition(FieldGroups *dst, FieldGroups *src, size_t alignment)
{
	groups_stable_sort(dst, group_less_size);
	groups_stable_sort(src, group_greater_size);
	int si = 0;
	for (int di = 0; di < dst->n && si < src->n; ++di) {
		while (si < src->n && dst->v[di].estimated_memory_size + src->v[si].estimated_memory_size > alignment)
			++si;
		if (si < src->n) {
			field_group_append(&dst->v[di], &src->v[si]);
			memmove(&src->v[si], &src->v[si + 1], sizeof(FieldGroup) * (size_t)(src->n - si - 1));
			src->n--;
		}
	}
}

static void maybe_merge_hot_into_fast(FieldPartitionArray field_groups)
{
	size_t num_fast_fields = 0, num_hot_fields = 0;
	for (int f = 0; f < kMaxFamily; ++f) {
		for (int i = 0; i < field_groups[f][HOT_FAST_PARSE].n; i++)
			num_fast_fields += (size_t)field_groups[f][HOT_FAST_PARSE].v[i].n;
		for (int i = 0; i < field_groups[f][HOT_HOT].n; i++)
			num_hot_fields += (size_t)field_groups[f][HOT_HOT].v[i].n;
	}
	if (num_fast_fields + num_hot_fields <= kMaxFastFieldHasbitIndex + 1) {
		for (int f = 0; f < kMaxFamily; ++f) {
			if (field_groups[f][HOT_HOT].n == 0)
				continue;
			fill_padding_from_partition(&field_groups[f][HOT_FAST_PARSE], &field_groups[f][HOT_HOT], 8);
			for (int i = 0; i < field_groups[f][HOT_HOT].n; i++)
				groups_push(&field_groups[f][HOT_FAST_PARSE], field_groups[f][HOT_HOT].v[i]);
			field_groups[f][HOT_HOT].n = 0;
		}
	}
}

static bool ends_with_msg_ptr(const LpField *last_field, const Options *o)
{
	return cpp_type(last_field) == LP_CPPTYPE_MESSAGE && !is_lazy(last_field, o) && !is_repeated(last_field);
}

/* MessageLayoutHelper::OptimizeLayout (DoOptimizeLayout) */
static void optimize_layout(const LpMessage *d, const Options *o, const LpField **fields, int n)
{
	FieldPartitionArray aligned_to_1, aligned_to_4, aligned_to_8;
	memset(aligned_to_1, 0, sizeof aligned_to_1);
	memset(aligned_to_4, 0, sizeof aligned_to_4);
	memset(aligned_to_8, 0, sizeof aligned_to_8);
	/* BuildFieldAlignmentGroups */
	TailCallTableInfo *fast = build_fast_parse_table(d, o);
	for (int i = 0; i < n; i++) {
		const LpField *f = fields[i];
		int family = get_field_family(f, o);
		int hotness;
		if (should_split(f, o))
			hotness = HOT_SPLIT;
		else if (is_repeated(f))
			hotness = HOT_REPEATED;
		else
			hotness = HOT_HOT; /* PaddingOptimizer::GetFieldHotness */
		if (should_promote_to_fast_parse(f, hotness, fast))
			hotness = HOT_FAST_PARSE;
		FieldGroup fg = field_group_single((float)f->number, f);
		switch (estimate_alignment_size(f)) {
		case 1:
			groups_push(&aligned_to_1[family][hotness], fg);
			break;
		case 4:
			groups_push(&aligned_to_4[family][hotness], fg);
			break;
		default:
			groups_push(&aligned_to_8[family][hotness], fg);
			break;
		}
	}
	/* MergeFieldAlignmentGroups */
	for (int f = 0; f < kMaxFamily; ++f) {
		FieldGroups aligned_1_to_4[kMaxHotness];
		memset(aligned_1_to_4, 0, sizeof aligned_1_to_4);
		consolidate_aligned_field_groups(aligned_to_1[f], 1, 4, aligned_1_to_4);
		for (int h = 0; h < kMaxHotness; ++h)
			for (int i = 0; i < aligned_1_to_4[h].n; i++)
				groups_push(&aligned_to_4[f][h], aligned_1_to_4[h].v[i]);
		FieldGroups aligned_4_to_8[kMaxHotness];
		memset(aligned_4_to_8, 0, sizeof aligned_4_to_8);
		consolidate_aligned_field_groups(aligned_to_4[f], 4, 8, aligned_4_to_8);
		for (int h = 0; h < kMaxHotness; ++h)
			for (int i = 0; i < aligned_4_to_8[h].n; i++)
				groups_push(&aligned_to_8[f][h], aligned_4_to_8[h].v[i]);
	}
	maybe_merge_hot_into_fast(aligned_to_8);
	/* BuildFieldDescriptorOrder (no profile: default_orders) */
	static const int default_orders[kMaxFamily] = {FAM_REPEATED, FAM_STRING, FAM_MESSAGE, FAM_ZERO_INITIALIZABLE,
						       FAM_OTHER};
	int k = 0;
	bool incomplete_block_at_end = false;
	for (int h = kMaxHotness - 1; h >= 0; --h) {
		for (int fi = 0; fi < kMaxFamily; ++fi) {
			FieldGroups *partition = &aligned_to_8[default_orders[fi]][h];
			int it = 0;
			while (it < partition->n && partition->v[it].estimated_memory_size > 4)
				it++;
			if (it < partition->n) {
				if (incomplete_block_at_end) {
					partition->v[it].preferred_location = -1;
					incomplete_block_at_end = false;
				} else {
					partition->v[it].preferred_location = (float)(double)536870911;
					incomplete_block_at_end = true;
				}
			} else if (partition->n) {
				incomplete_block_at_end = false;
			}
			groups_stable_sort(partition, group_less_location);
			for (int i = 0; i < partition->n; i++)
				for (int j = 0; j < partition->v[i].n; j++)
					fields[k++] = partition->v[i].fields[j];
		}
		/* the recipe only matters with a profile */
		(void)ends_with_msg_ptr;
	}
	if (k != n)
		fail("Check failed: initial_size == optimized_order.size()");
}

/* ---- FieldLayout ---- */

static FieldLayout build_optimized_layout(const LpMessage *d, const Options *o, int *num_weak_fields)
{
	FieldLayout l = {0};
	Vec order = {0};
	*num_weak_fields = collect_fields_excluding_weak_and_oneof(d, o, &order);
	l.fields = xalloc(sizeof(LpField *) * (size_t)(order.n + 1));
	for (int i = 0; i < order.n; i++)
		l.fields[i] = order.v[i];
	l.n_fields = order.n;
	optimize_layout(d, o, l.fields, l.n_fields);
	for (int i = 0; i < l.n_fields; i++) {
		const LpField *f = l.fields[i];
		if (has_hasbit(f, o)) {
			if (!l.has_bit_indices) {
				l.n_has_bit_indices = d->n_fields;
				l.has_bit_indices = xalloc(sizeof(int) * (size_t)(d->n_fields + 1));
				for (int j = 0; j < d->n_fields; j++)
					l.has_bit_indices[j] = kNoHasbit;
			}
			l.has_bit_indices[f->index] = l.max_has_bit_index++;
		}
	}
	return l;
}

static bool layout_has_hasbits(const FieldLayout *l) { return l->has_bit_indices != NULL; }
static int layout_has_bits_size(const FieldLayout *l) { return (l->max_has_bit_index + 31) / 32; }

/* GetHasBitIndex: -1 if none */
static int layout_has_bit_index(const FieldLayout *l, const LpField *f)
{
	if (!l->has_bit_indices || l->has_bit_indices[f->index] == kNoHasbit)
		return -1;
	return l->has_bit_indices[f->index];
}

static int layout_has_byte_index(const FieldLayout *l, const LpField *f)
{
	int h = layout_has_bit_index(l, f);
	return h < 0 ? -1 : h / 8;
}

static int layout_has_word_index(const FieldLayout *l, const LpField *f)
{
	int h = layout_has_bit_index(l, f);
	return h < 0 ? -1 : h / 32;
}

static int layout_get_has_bit_index_cb(const LpField *f, const void *arg) { return layout_has_bit_index(arg, f); }

static void layout_print_has_bit_indices_for_schema(const FieldLayout *l, LpPrinter *p, size_t *entries)
{
	if (!l->has_bit_indices)
		return;
	*entries += (size_t)l->n_has_bit_indices;
	for (int i = 0; i < l->n_has_bit_indices; ++i)
		EMIT(p, "$index$,\n",
		     S_("index", l->has_bit_indices[i] != kNoHasbit ? cpp_itoa(l->has_bit_indices[i]) : "~0u"));
}

/* ---- field_chunk ---- */

static FieldChunk *chunks_push(FieldChunks *cs, FieldChunk c)
{
	if (cs->n == cs->cap) {
		int cap = cs->cap ? 2 * cs->cap : 8;
		cs->v = xrealloc(cs->v, sizeof(FieldChunk) * (size_t)cs->cap, sizeof(FieldChunk) * (size_t)cap);
		cs->cap = cap;
	}
	cs->v[cs->n] = c;
	return &cs->v[cs->n++];
}

static void chunk_push_field(FieldChunk *c, const LpField *f)
{
	if (c->n == c->cap) {
		int cap = c->cap ? 2 * c->cap : 8;
		c->fields = xrealloc(c->fields, sizeof(LpField *) * (size_t)c->cap, sizeof(LpField *) * (size_t)cap);
		c->cap = cap;
	}
	c->fields[c->n++] = f;
}

typedef bool (*FieldEquiv)(const LpField *a, const LpField *b, const void *arg);
typedef bool (*FieldFilter)(const LpField *f, const void *arg);

/* CollectFields */
static FieldChunks collect_fields(const LpField *const *fields, int n, const Options *o, FieldEquiv equivalent,
				  FieldFilter filter, const void *arg)
{
	FieldChunks chunks = {0};
	bool force_new_chunk = true;
	for (int i = 0; i < n; i++) {
		const LpField *f = fields[i];
		if (filter && !filter(f, arg)) {
			force_new_chunk = true;
			continue;
		}
		if (force_new_chunk || !equivalent(chunks.v[chunks.n - 1].fields[chunks.v[chunks.n - 1].n - 1], f, arg)) {
			force_new_chunk = false;
			chunks_push(&chunks, (FieldChunk){.has_hasbit = has_hasbit(f, o), .is_rarely_present = is_rarely_present(f, o), .should_split = should_split(f, o)});
		}
		chunk_push_field(&chunks.v[chunks.n - 1], f);
	}
	return chunks;
}

typedef bool (*ChunkEqual)(const FieldChunk *a, const FieldChunk *b);

/* FindNextUnequalChunk: the index */
static int find_next_unequal_chunk(const FieldChunks *cs, int start, int end, ChunkEqual equal)
{
	int it = start;
	while (++it != end)
		if (!equal(&cs->v[start], &cs->v[it]))
			return it;
	return end;
}


/* GenChunkMask(fields) */
static uint32_t gen_chunk_mask_fields(const LpField *const *fields, int n, const FieldLayout *l)
{
	if (!n)
		return 0;
	int first_index_offset = layout_has_word_index(l, fields[0]);
	uint32_t chunk_mask = 0;
	for (int i = 0; i < n; i++) {
		int bit_idx = layout_has_bit_index(l, fields[i]);
		if (first_index_offset != bit_idx / 32)
			fail("Check failed: first_index_offset == bit_idx / 32");
		chunk_mask |= (uint32_t)1 << (bit_idx % 32);
	}
	return chunk_mask;
}

