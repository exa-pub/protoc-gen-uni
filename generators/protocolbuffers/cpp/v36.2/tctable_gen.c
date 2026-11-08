// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/generated_message_tctable_gen.{h,cc} (protobuf v36.2): the parse
 * tables (TailCallTableInfo) the cpp generator prints, with the constants of
 * generated_message_tctable_{decl,impl}.h it needs (field_layout type cards, the
 * TcParseFunction list) and TypeCardToString of generated_message_tctable_lite.cc. Only the
 * code generator uses this library function, so it lives with the generator. */

/* ---- generated_message_tctable_impl.h ---- */

enum {
	kSplitOffsetAuxIdx = 0,
	kSplitSizeAuxIdx = 1,
};

enum {
	kFkShift = 0, kFkBits = 3, kFkMask = ((1 << 3) - 1) << 0,
	kFkNone = 0, kFkVarint, kFkPackedVarint, kFkFixed, kFkPackedFixed, kFkString, kFkMessage, kFkMap,
	kSplitShift = 3, kSplitMask = 1 << 3, kSplitFalse = 0, kSplitTrue = 1 << 3,
	kFcShift = 4, kFcMask = 3 << 4,
	kFcSingular = 0, kFcOptional = 1 << 4, kFcRepeated = 2 << 4, kFcOneof = 3 << 4,
	kRepShift = 6, kRepMask = 7 << 6,
	kRep8Bits = 0, kRep32Bits = 2 << 6, kRep64Bits = 3 << 6,
	kRepAString = 0, kRepIString = 1 << 6, kRepCord = 2 << 6, kRepSPiece = 3 << 6, kRepSString = 4 << 6,
	kRepMString = 5 << 6, kRepMessage = 0, kRepGroup = 1 << 6, kRepLazy = 2 << 6,
	kTvShift = 9, kTvMask = 3 << 9,
	kTvZigZag = 1 << 9, kTvEnum = 2 << 9, kTvRange = 3 << 9, kTvUtf8 = 2 << 9,
	kTvDefault = 1 << 9, kTvTable = 2 << 9, kTvWeakPtr = 3 << 9, kTvEager = 1 << 9, kTvLazy = 2 << 9,
	kFmtShift = 11, kFmtMask = 3 << 11,
	kFmtUnsigned = 1 << 11, kFmtSigned = 2 << 11, kFmtFloating = 3 << 11, kFmtEnum = 3 << 11,
	kFmtUtf8 = 1 << 11, kFmtUtf8Escape = 2 << 11, kFmtArray = 1 << 11, kFmtShow = 1 << 11,
};

enum {
	kBool = 0 | kFkVarint | kRep8Bits,
	kFixed32 = 0 | kFkFixed | kRep32Bits | kFmtUnsigned,
	kUInt32 = 0 | kFkVarint | kRep32Bits | kFmtUnsigned,
	kSFixed32 = 0 | kFkFixed | kRep32Bits | kFmtSigned,
	kInt32 = 0 | kFkVarint | kRep32Bits | kFmtSigned,
	kSInt32 = 0 | kFkVarint | kRep32Bits | kFmtSigned | kTvZigZag,
	kFloat = 0 | kFkFixed | kRep32Bits | kFmtFloating,
	kEnum = 0 | kFkVarint | kRep32Bits | kFmtEnum | kTvEnum,
	kEnumRange = 0 | kFkVarint | kRep32Bits | kFmtEnum | kTvRange,
	kOpenEnum = 0 | kFkVarint | kRep32Bits | kFmtEnum,
	kFixed64 = 0 | kFkFixed | kRep64Bits | kFmtUnsigned,
	kUInt64 = 0 | kFkVarint | kRep64Bits | kFmtUnsigned,
	kSFixed64 = 0 | kFkFixed | kRep64Bits | kFmtSigned,
	kInt64 = 0 | kFkVarint | kRep64Bits | kFmtSigned,
	kSInt64 = 0 | kFkVarint | kRep64Bits | kFmtSigned | kTvZigZag,
	kDouble = 0 | kFkFixed | kRep64Bits | kFmtFloating,
	kPackedBool = 0 | kFkPackedVarint | kRep8Bits,
	kPackedFixed32 = 0 | kFkPackedFixed | kRep32Bits | kFmtUnsigned,
	kPackedUInt32 = 0 | kFkPackedVarint | kRep32Bits | kFmtUnsigned,
	kPackedSFixed32 = 0 | kFkPackedFixed | kRep32Bits | kFmtSigned,
	kPackedInt32 = 0 | kFkPackedVarint | kRep32Bits | kFmtSigned,
	kPackedSInt32 = 0 | kFkPackedVarint | kRep32Bits | kFmtSigned | kTvZigZag,
	kPackedFloat = 0 | kFkPackedFixed | kRep32Bits | kFmtFloating,
	kPackedEnum = 0 | kFkPackedVarint | kRep32Bits | kFmtEnum | kTvEnum,
	kPackedEnumRange = 0 | kFkPackedVarint | kRep32Bits | kFmtEnum | kTvRange,
	kPackedOpenEnum = 0 | kFkPackedVarint | kRep32Bits | kFmtEnum,
	kPackedFixed64 = 0 | kFkPackedFixed | kRep64Bits | kFmtUnsigned,
	kPackedUInt64 = 0 | kFkPackedVarint | kRep64Bits | kFmtUnsigned,
	kPackedSFixed64 = 0 | kFkPackedFixed | kRep64Bits | kFmtSigned,
	kPackedInt64 = 0 | kFkPackedVarint | kRep64Bits | kFmtSigned,
	kPackedSInt64 = 0 | kFkPackedVarint | kRep64Bits | kFmtSigned | kTvZigZag,
	kPackedDouble = 0 | kFkPackedFixed | kRep64Bits | kFmtFloating,
	kBytes = 0 | kFkString | kFmtArray,
	kUtf8String = 0 | kFkString | kFmtUtf8 | kTvUtf8,
	kMessage = kFkMessage,
	kMap = kFkMap,
};

enum { kMaxFastFields = 32, kNoAuxIdx = 0xFFFF, kNoHasbit = -1, kMaxFastFieldHasbitIndex = 31 };
static const uint16_t kMiniParseTableTypeCardMask = kSplitMask | kFkMask;

/* PROTOBUF_TC_PARSE_FUNCTION_LIST: TcParseFunction values are the indexes of this table. */
#define TC_SINGLE(fn) #fn "S1", #fn "S2",
#define TC_REPEATED(fn) TC_SINGLE(fn) #fn "R1", #fn "R2",
#define TC_PACKED(fn) TC_REPEATED(fn) #fn "P1", #fn "P2",
static const char *const kTcParseFunctionNames[] = {
	"",
	TC_PACKED(FastV8) TC_PACKED(FastV32) TC_PACKED(FastV64) TC_PACKED(FastZ32) TC_PACKED(FastZ64)
	TC_PACKED(FastF32) TC_PACKED(FastF64) TC_PACKED(FastEv) TC_PACKED(FastEr) TC_PACKED(FastEr0)
	TC_PACKED(FastEr1) TC_REPEATED(FastB) TC_REPEATED(FastU) TC_SINGLE(FastBi) TC_SINGLE(FastUi)
	TC_REPEATED(FastBc) TC_REPEATED(FastUc) TC_SINGLE(FastBm) TC_SINGLE(FastUm) TC_REPEATED(FastGd)
	TC_REPEATED(FastGt) TC_REPEATED(FastMd) TC_REPEATED(FastMt) TC_SINGLE(FastMl)
	"FastEndG1", "FastEndG2", "MessageSetWireFormatParseLoopLite", "MessageSetWireFormatParseLoop",
	"ReflectionParseLoop", "FastMiniParse1", "FastMiniParse2", "GenericFallback", "GenericFallbackLite",
	"ReflectionFallback", "DiscardEverythingFallback", "MpUnknownFields",
};
#undef TC_SINGLE
#undef TC_REPEATED
#undef TC_PACKED

/* the TcParseFunction named "Fast..." + suffix ("FastV8S1") */
static TcParseFunction tc_func(const char *name)
{
	for (size_t i = 1; i < sizeof kTcParseFunctionNames / sizeof *kTcParseFunctionNames; i++)
		if (str_eq(kTcParseFunctionNames[i], name))
			return (TcParseFunction)i;
	fail("unknown TcParseFunction %s", name);
}

static const char *tc_func_name(TcParseFunction f) { return kTcParseFunctionNames[f]; }

/* TcParseTableBase::RecodeTagForFastParsing */
static uint32_t recode_tag_for_fast_parsing(uint32_t tag)
{
	uint32_t hibits = tag & 0xFFFFFF80u;
	if (hibits != 0)
		tag = tag + hibits + 0x80;
	return tag;
}

static uint32_t tag_to_idx(uint32_t tag, uint32_t fast_table_size) { return (tag >> 3) & (fast_table_size - 1); }

/* WireFormatLite wire types */
enum { WIRETYPE_VARINT = 0, WIRETYPE_FIXED64 = 1, WIRETYPE_LENGTH_DELIMITED = 2, WIRETYPE_START_GROUP = 3,
       WIRETYPE_END_GROUP = 4, WIRETYPE_FIXED32 = 5 };

static int wire_type_for_field_type(int type)
{
	switch (type) {
	case PT_DOUBLE:
	case PT_FIXED64:
	case PT_SFIXED64:
		return WIRETYPE_FIXED64;
	case PT_FLOAT:
	case PT_FIXED32:
	case PT_SFIXED32:
		return WIRETYPE_FIXED32;
	case PT_STRING:
	case PT_BYTES:
	case PT_MESSAGE:
		return WIRETYPE_LENGTH_DELIMITED;
	case PT_GROUP:
		return WIRETYPE_START_GROUP;
	default:
		return WIRETYPE_VARINT;
	}
}

/* WireFormat::MakeTag(field) */
static uint32_t wire_format_make_tag(const LpField *f)
{
	int wt = proto_field_is_packed(f) ? WIRETYPE_LENGTH_DELIMITED : wire_type_for_field_type(f->type);
	return ((uint32_t)f->number << 3) | (uint32_t)wt;
}

static int varint_size32(uint32_t v)
{
	int n = 1;
	while (v >= 0x80) {
		v >>= 7;
		n++;
	}
	return n;
}

/* WireFormat::TagSize(number, type) */
static int wire_format_tag_size(int number, int type)
{
	int result = varint_size32((uint32_t)number << 3);
	if (type == PT_GROUP)
		return result * 2;
	return result;
}

/* TypeCardToString (generated_message_tctable_lite.cc) */
static char *type_card_to_string(uint16_t type_card)
{
	static const char *const kFieldCardNames[] = {"Singular", "Optional", "Repeated", "Oneof"};
	int rep_index = (type_card & kRepMask) >> kRepShift;
	int tv_index = (type_card & kTvMask) >> kTvShift;
	Buf out = {0};
	buf_adds(&out, "::_fl::kFc");
	buf_adds(&out, kFieldCardNames[(type_card & kFcMask) >> kFcShift]);
	switch (type_card & kFkMask) {
	case kFkString: {
		switch (type_card & ~kFcMask & ~kRepMask & ~kSplitMask) {
		case kBytes:
			buf_adds(&out, " | ::_fl::kBytes");
			break;
		case kUtf8String:
			buf_adds(&out, " | ::_fl::kUtf8String");
			break;
		default:
			fail("Unknown type_card: 0x%x", type_card);
		}
		static const char *const kRepNames[] = {"AString", "IString", "Cord", "SPiece", "SString", "MString"};
		buf_adds(&out, " | ::_fl::kRep");
		buf_adds(&out, kRepNames[rep_index]);
		break;
	}
	case kFkMessage: {
		buf_adds(&out, " | ::_fl::kMessage");
		static const char *const kRepNames[] = {NULL, "Group", "Lazy"};
		if (rep_index < 3 && kRepNames[rep_index]) {
			buf_adds(&out, " | ::_fl::kRep");
			buf_adds(&out, kRepNames[rep_index]);
		}
		static const char *const kXFormNames[2][4] = {{NULL, "Default", "Table", "WeakPtr"},
							      {NULL, "Eager", "Lazy", NULL}};
		const char *xform = kXFormNames[rep_index == 2][tv_index];
		if (xform) {
			buf_adds(&out, " | ::_fl::kTv");
			buf_adds(&out, xform);
		}
		break;
	}
	case kFkMap:
		buf_adds(&out, " | ::_fl::kMap");
		break;
	case kFkNone:
		break;
	case kFkVarint:
	case kFkPackedVarint:
	case kFkFixed:
	case kFkPackedFixed: {
		static const struct {
			uint16_t v;
			const char *name;
		} kCases[] = {
			{kBool, "Bool"}, {kFixed32, "Fixed32"}, {kUInt32, "UInt32"}, {kSFixed32, "SFixed32"},
			{kInt32, "Int32"}, {kSInt32, "SInt32"}, {kFloat, "Float"}, {kEnum, "Enum"},
			{kEnumRange, "EnumRange"}, {kOpenEnum, "OpenEnum"}, {kFixed64, "Fixed64"}, {kUInt64, "UInt64"},
			{kSFixed64, "SFixed64"}, {kInt64, "Int64"}, {kSInt64, "SInt64"}, {kDouble, "Double"},
			{kPackedBool, "PackedBool"}, {kPackedFixed32, "PackedFixed32"}, {kPackedUInt32, "PackedUInt32"},
			{kPackedSFixed32, "PackedSFixed32"}, {kPackedInt32, "PackedInt32"},
			{kPackedSInt32, "PackedSInt32"}, {kPackedFloat, "PackedFloat"}, {kPackedEnum, "PackedEnum"},
			{kPackedEnumRange, "PackedEnumRange"}, {kPackedOpenEnum, "PackedOpenEnum"},
			{kPackedFixed64, "PackedFixed64"}, {kPackedUInt64, "PackedUInt64"},
			{kPackedSFixed64, "PackedSFixed64"}, {kPackedInt64, "PackedInt64"},
			{kPackedSInt64, "PackedSInt64"}, {kPackedDouble, "PackedDouble"},
		};
		uint16_t v = type_card & ~kFcMask & ~kSplitMask;
		size_t i = 0;
		for (; i < sizeof kCases / sizeof *kCases; i++)
			if (kCases[i].v == v)
				break;
		if (i == sizeof kCases / sizeof *kCases)
			fail("Unknown type_card: 0x%x", type_card);
		buf_adds(&out, " | ::_fl::k");
		buf_adds(&out, kCases[i].name);
		break;
	}
	}
	if (type_card & kSplitMask)
		buf_adds(&out, " | ::_fl::kSplitTrue");
	return xstrdup(buf_str(&out));
}

/* ---- generated_message_tctable_gen.cc ---- */

static bool treat_enum_as_int(const LpField *f)
{
	return has_preserving_unknown_enum_semantics(f) ||
	       (f->enum_type && f->containing_type && is_map_entry_message(f->containing_type) &&
		map_value(f->containing_type) == f);
}

static bool get_enum_validation_range_slow(const LpEnum *e, int32_t *first, int32_t *last)
{
	int min = e->values[0].number, max = min;
	for (int i = 1; i < e->n_values; ++i) {
		if (e->values[i].number < min)
			min = e->values[i].number;
		if (e->values[i].number > max)
			max = e->values[i].number;
	}
	int64_t range = (int64_t)max - (int64_t)min + 1;
	if (e->n_values < range)
		return false;
	*first = min;
	*last = max;
	uint64_t *array = xalloc(sizeof(uint64_t) * (size_t)((range + 63) / 64));
	int unique_count = 0;
	for (int i = 0; i < e->n_values; ++i) {
		size_t index = (size_t)((int64_t)e->values[i].number - min);
		uint64_t *v = &array[index / 64];
		size_t bit_pos = index % 64;
		unique_count += (*v & ((uint64_t)1 << bit_pos)) == 0;
		*v |= (uint64_t)1 << bit_pos;
	}
	return unique_count == range;
}

static bool get_enum_validation_range(const LpEnum *e, int32_t *first, int32_t *last)
{
	if (!is_enum_fully_sequential(e))
		return get_enum_validation_range_slow(e, first, last);
	*first = e->values[0].number;
	*last = e->values[e->n_values - 1].number;
	return true;
}

typedef enum { ENUM_RANGE_NONE, ENUM_RANGE_CONTIGUOUS, ENUM_RANGE_CONTIGUOUS0, ENUM_RANGE_CONTIGUOUS1 } EnumRangeInfo;

static EnumRangeInfo get_enum_range_info(const LpField *f, uint8_t *rmax_value)
{
	int32_t first, last;
	if (!get_enum_validation_range(f->enum_type, &first, &last))
		return ENUM_RANGE_NONE;
	if (last <= 127 && (first == 0 || first == 1)) {
		*rmax_value = (uint8_t)last;
		return first == 0 ? ENUM_RANGE_CONTIGUOUS0 : ENUM_RANGE_CONTIGUOUS1;
	}
	return ENUM_RANGE_CONTIGUOUS;
}

static bool has_lazy_rep(const LpField *f, const TcFieldOptions *o)
{
	return f->type == PT_MESSAGE && !is_repeated(f) && o->lazy_opt != 0;
}

/* PROTOBUF_PICK_*_FUNCTION of MakeFastFieldEntry: the function name "<fn><card><width>" */
static TcParseFunction pick(const LpField *f, const char *fn, const char *card)
{
	return tc_func(xcat(fn, card, f->number < 16 ? "1" : "2"));
}
static TcParseFunction pick_single(const LpField *f, const char *fn) { return pick(f, fn, "S"); }
static TcParseFunction pick_repeatable(const LpField *f, const char *fn)
{
	return pick(f, fn, is_repeated(f) ? "R" : "S");
}
static TcParseFunction pick_packable(const LpField *f, const char *fn)
{
	return pick(f, fn, proto_field_is_packed(f) ? "P" : is_repeated(f) ? "R" : "S");
}
static TcParseFunction pick_string(const LpField *f, const char *fn, const TcFieldOptions *o)
{
	if (cpp_string_type(f) == CPP_STR_CORD)
		return pick_repeatable(f, xcat(fn, "c"));
	if (cpp_string_type(f) == CPP_STR_VIEW && o->str_kind == 2)
		return pick(f, xcat(fn, "m"), "S");
	if (o->str_kind == 1)
		return pick(f, xcat(fn, "i"), "S");
	return pick_repeatable(f, fn);
}

static FastFieldInfo make_fast_field_entry(const FieldEntryInfo *entry, const TcFieldOptions *o,
					   const TcMessageOptions *message_options)
{
	FastFieldInfo info = {.kind = TC_FAST_FIELD};
	const LpField *f = entry->field;
	info.aux_idx = (uint8_t)entry->aux_idx;
	TcParseFunction picked = 0;
	switch (f->type) {
	case PT_BOOL:
		picked = pick_packable(f, "FastV8");
		break;
	case PT_INT32:
	case PT_UINT32:
		picked = pick_packable(f, "FastV32");
		break;
	case PT_SINT32:
		picked = pick_packable(f, "FastZ32");
		break;
	case PT_INT64:
	case PT_UINT64:
		picked = pick_packable(f, "FastV64");
		break;
	case PT_SINT64:
		picked = pick_packable(f, "FastZ64");
		break;
	case PT_FLOAT:
	case PT_FIXED32:
	case PT_SFIXED32:
		picked = pick_packable(f, "FastF32");
		break;
	case PT_DOUBLE:
	case PT_FIXED64:
	case PT_SFIXED64:
		picked = pick_packable(f, "FastF64");
		break;
	case PT_ENUM:
		if (treat_enum_as_int(f)) {
			picked = pick_packable(f, "FastV32");
		} else {
			switch (get_enum_range_info(f, &info.aux_idx)) {
			case ENUM_RANGE_NONE:
				picked = pick_packable(f, "FastEv");
				break;
			case ENUM_RANGE_CONTIGUOUS:
				picked = pick_packable(f, "FastEr");
				break;
			case ENUM_RANGE_CONTIGUOUS0:
				picked = pick_packable(f, "FastEr0");
				break;
			case ENUM_RANGE_CONTIGUOUS1:
				picked = pick_packable(f, "FastEr1");
				break;
			}
		}
		break;
	case PT_BYTES:
		picked = pick_string(f, "FastB", o);
		break;
	case PT_STRING:
		picked = pick_string(f, entry->utf8_check_mode == UTF8_STRICT ? "FastU" : "FastB", o);
		break;
	case PT_MESSAGE:
		picked = has_lazy_rep(f, o)		  ? pick_single(f, "FastMl")
			 : o->use_direct_tcparser_table ? pick_repeatable(f, "FastMt")
							: pick_repeatable(f, "FastMd");
		break;
	case PT_GROUP:
		picked = o->use_direct_tcparser_table ? pick_repeatable(f, "FastGt") : pick_repeatable(f, "FastGd");
		break;
	}
	info.func = picked;
	info.presence_probability = o->presence_probability;
	return info;
}

/* IsFieldTypeEligibleForFastParsing */
static bool is_field_type_eligible_for_fast_parsing(const LpField *f)
{
	if (is_map(f) || real_oneof(f) || is_weak_option(f))
		return false;
	if (f->number >= 1 << 11)
		return false;
	return true;
}

static bool is_field_eligible_for_fast_parsing(const FieldEntryInfo *entry, const TcFieldOptions *o,
					       const TcMessageOptions *message_options)
{
	const LpField *f = entry->field;
	if (!is_field_type_eligible_for_fast_parsing(f) || o->is_implicitly_weak || o->should_split)
		return false;
	if (has_lazy_rep(f, o) && !message_options->uses_codegen)
		return false;
	if (has_lazy_rep(f, o) && o->lazy_opt == kTvLazy)
		return false;
	if (entry->hasbit_idx > kMaxFastFieldHasbitIndex)
		return false;
	if (entry->aux_idx > 255)
		return false;
	return true;
}

static bool fast_is_better_fast(const FastFieldInfo *info, double presence_probability)
{
	if (info->kind < TC_FAST_FIELD)
		return true;
	if (info->kind > TC_FAST_FIELD)
		return false;
	return presence_probability > info->presence_probability;
}

static bool fast_is_better_mp_fast(const FastFieldInfo *info, double presence_probability)
{
	if (info->kind < TC_FAST_MP_FIELD)
		return true;
	if (info->kind > TC_FAST_MP_FIELD)
		return false;
	return presence_probability > info->presence_probability;
}

/* operator< of FastFieldInfo */
static bool fast_less(const FastFieldInfo *a, const FastFieldInfo *b)
{
	if (a->kind != b->kind)
		return a->kind < b->kind;
	if (a->kind == TC_FAST_FIELD || a->kind == TC_FAST_MP_FIELD)
		return a->presence_probability < b->presence_probability;
	return false;
}

/* GetEndGroupTag: 0 if none (a real tag is never 0) */
static uint32_t get_end_group_tag(const LpMessage *d)
{
	const LpMessage *parent = d->containing_type;
	if (!parent)
		return 0;
	for (int i = 0; i < parent->n_fields; ++i) {
		const LpField *f = &parent->fields[i];
		if (f->type == PT_GROUP && f->message_type == d)
			return ((uint32_t)f->number << 3) | WIRETYPE_END_GROUP;
	}
	return 0;
}

static uint32_t fast_parse_table_size(size_t num_fields, uint32_t end_group_tag)
{
	if (end_group_tag)
		return kMaxFastFields;
	size_t ceil = 1;
	while (ceil < num_fields + 1)
		ceil <<= 1;
	size_t v = ceil < kMaxFastFields ? ceil : kMaxFastFields;
	return (uint32_t)(v < 1 ? 1 : v);
}

static void populate_fast_fields(uint32_t end_group_tag, const FieldEntryInfo *field_entries, int n_entries,
				 const TcMessageOptions *message_options, const TcFieldOptions *fields,
				 FastFieldInfo *result, uint32_t result_size, uint32_t *important_fields)
{
	if (end_group_tag && (end_group_tag >> 14) == 0) {
		uint32_t tag = recode_tag_for_fast_parsing(end_group_tag);
		uint32_t fast_idx = tag_to_idx(tag, result_size);
		FastFieldInfo *info = &result[fast_idx];
		*info = (FastFieldInfo){
			.kind = TC_FAST_NON_FIELD,
			.func = tc_func(end_group_tag < 128 ? "FastEndG1" : "FastEndG2"),
			.coded_tag = (uint16_t)tag,
			.nonfield_info = (uint16_t)end_group_tag,
		};
		*important_fields |= (uint32_t)1 << fast_idx;
	}
	for (int i = 0; i < n_entries; ++i) {
		const FieldEntryInfo *entry = &field_entries[i];
		const LpField *f = entry->field;
		if (f->number >= 1 << 11)
			continue;
		const TcFieldOptions *o = &fields[i];
		uint32_t tag = recode_tag_for_fast_parsing(wire_format_make_tag(f));
		uint32_t fast_idx = tag_to_idx(tag, result_size);
		FastFieldInfo *info = &result[fast_idx];
		if (is_field_eligible_for_fast_parsing(entry, o, message_options)) {
			if (!fast_is_better_fast(info, o->presence_probability))
				continue;
			*info = make_fast_field_entry(entry, o, message_options);
			info->field = f;
			info->coded_tag = (uint16_t)tag;
			info->hasbit_idx = (uint8_t)(entry->hasbit_idx >= 0 ? entry->hasbit_idx : 63);
			*important_fields |= (uint32_t)(o->presence_probability >= 0.05f) << fast_idx;
		} else {
			if (!fast_is_better_mp_fast(info, o->presence_probability))
				continue;
			*info = (FastFieldInfo){
				.kind = TC_FAST_MP_FIELD,
				.func = tc_func(f->number < 16 ? "FastMiniParse1" : "FastMiniParse2"),
				.field = f,
				.coded_tag = (uint16_t)tag,
				.function_index = (uint8_t)(entry->type_card & kMiniParseTableTypeCardMask),
				.field_index = (uint32_t)i,
				.presence_probability = o->presence_probability,
			};
		}
	}
}

static void generate_field_names(TailCallTableInfo *info, const LpMessage *d)
{
	const size_t kMaxNameLength = 255;
	size_t field_name_total_size = 0;
	for (int i = 0; i < info->n_field_entries; i++)
		if (info->field_entries[i].utf8_check_mode != UTF8_NONE)
			field_name_total_size += strlen(info->field_entries[i].field->name);
	if (field_name_total_size == 0)
		return;
	const char *message_name = d->full_name;
	size_t mlen = strlen(message_name);
	uint8_t message_name_size = (uint8_t)(mlen < kMaxNameLength ? mlen : kMaxNameLength);
	size_t total_byte_size =
		((1 + (size_t)info->n_field_entries + 7) & ~(size_t)7) + message_name_size + field_name_total_size;
	uint8_t *out = xalloc(total_byte_size);
	uint8_t *out_it = out;
	int count = 1;
	*out_it++ = message_name_size;
	for (int i = 0; i < info->n_field_entries; i++) {
		if (info->field_entries[i].utf8_check_mode != UTF8_NONE)
			*out_it++ = (uint8_t)strlen(info->field_entries[i].field->name);
		else
			++out_it;
		++count;
	}
	out_it += -count & 7;
	if (mlen > kMaxNameLength) {
		const size_t kNameHalfLength = (kMaxNameLength - 3) / 2;
		memcpy(out_it, message_name, kNameHalfLength);
		out_it += kNameHalfLength;
		memcpy(out_it, "...", 3);
		out_it += 3;
		memcpy(out_it, message_name + mlen - kNameHalfLength, kNameHalfLength);
		out_it += kNameHalfLength;
	} else {
		memcpy(out_it, message_name, mlen);
		out_it += mlen;
	}
	for (int i = 0; i < info->n_field_entries; i++) {
		if (info->field_entries[i].utf8_check_mode != UTF8_NONE) {
			const char *name = info->field_entries[i].field->name;
			size_t n = strlen(name);
			memcpy(out_it, name, n);
			out_it += n;
		}
	}
	info->field_name_data = out;
	info->n_field_name_data = total_byte_size;
}

static void num_to_entry_push_block(NumToEntryTable *t, uint32_t fnum)
{
	if (t->n_blocks == t->cap_blocks) {
		int cap = t->cap_blocks ? 2 * t->cap_blocks : 4;
		t->blocks = xrealloc(t->blocks, sizeof(SkipEntryBlock) * (size_t)t->cap_blocks, sizeof(SkipEntryBlock) * (size_t)cap);
		t->cap_blocks = cap;
	}
	t->blocks[t->n_blocks++] = (SkipEntryBlock){.first_fnum = fnum};
}

static void block_push_entry(SkipEntryBlock *b, SkipEntry16 e)
{
	if (b->n_entries == b->cap_entries) {
		int cap = b->cap_entries ? 2 * b->cap_entries : 4;
		b->entries = xrealloc(b->entries, sizeof(SkipEntry16) * (size_t)b->cap_entries, sizeof(SkipEntry16) * (size_t)cap);
		b->cap_entries = cap;
	}
	b->entries[b->n_entries++] = e;
}

static NumToEntryTable make_num_to_entry_table(const TcFieldOptions *ordered_fields, int n)
{
	NumToEntryTable t = {0};
	t.skipmap32 = (uint32_t)-1;
	uint16_t field_entry_index = 0;
	uint16_t N = (uint16_t)n;
	for (; field_entry_index != N; ++field_entry_index) {
		const LpField *fd = ordered_fields[field_entry_index].field;
		if (fd->number > 32)
			break;
		int skipmap32_index = fd->number - 1;
		t.skipmap32 -= (uint32_t)1 << skipmap32_index;
	}
	if (field_entry_index == N)
		return t;
	SkipEntryBlock *block = NULL;
	bool start_new_block = true;
	uint32_t last_skip_entry_start = 0;
	for (; field_entry_index != N; ++field_entry_index) {
		const LpField *fd = ordered_fields[field_entry_index].field;
		uint32_t fnum = (uint32_t)fd->number;
		if (!start_new_block && fnum - last_skip_entry_start > 96)
			start_new_block = true;
		if (start_new_block) {
			num_to_entry_push_block(&t, fnum);
			block = &t.blocks[t.n_blocks - 1];
			start_new_block = false;
		}
		uint32_t skip_entry_num = (fnum - block->first_fnum) / 16;
		uint32_t skip_entry_index = (fnum - block->first_fnum) % 16;
		while (skip_entry_num >= (uint32_t)block->n_entries)
			block_push_entry(block, (SkipEntry16){0xFFFF, field_entry_index});
		block->entries[skip_entry_num].skipmap -= (uint16_t)(1u << skip_entry_index);
		last_skip_entry_start = fnum - skip_entry_index;
	}
	return t;
}

static int num_to_entry_size16(const NumToEntryTable *t)
{
	int size = 2;
	for (int i = 0; i < t->n_blocks; i++)
		size += 3 + t->blocks[i].n_entries * 2;
	return size;
}

static uint16_t packed_or(const LpField *f, uint16_t packed, uint16_t plain)
{
	return is_repeated(f) && proto_field_is_packed(f) ? packed : plain;
}

static uint16_t make_type_card_for_field(const LpField *f, bool has_hasbit_, const TcFieldOptions *o,
					 Utf8CheckMode utf8_check_mode)
{
	uint16_t type_card;
	if (is_repeated(f))
		type_card = kFcRepeated;
	else if (has_hasbit_)
		type_card = kFcOptional;
	else if (real_oneof(f))
		type_card = kFcOneof;
	else
		type_card = kFcSingular;
	switch (f->type) {
	case PT_DOUBLE:
		type_card |= packed_or(f, kPackedDouble, kDouble);
		break;
	case PT_FLOAT:
		type_card |= packed_or(f, kPackedFloat, kFloat);
		break;
	case PT_FIXED32:
		type_card |= packed_or(f, kPackedFixed32, kFixed32);
		break;
	case PT_SFIXED32:
		type_card |= packed_or(f, kPackedSFixed32, kSFixed32);
		break;
	case PT_FIXED64:
		type_card |= packed_or(f, kPackedFixed64, kFixed64);
		break;
	case PT_SFIXED64:
		type_card |= packed_or(f, kPackedSFixed64, kSFixed64);
		break;
	case PT_BOOL:
		type_card |= packed_or(f, kPackedBool, kBool);
		break;
	case PT_ENUM:
		if (treat_enum_as_int(f)) {
			type_card |= packed_or(f, kPackedOpenEnum, kOpenEnum);
		} else {
			int32_t first, last;
			if (get_enum_validation_range(f->enum_type, &first, &last))
				type_card |= packed_or(f, kPackedEnumRange, kEnumRange);
			else
				type_card |= packed_or(f, kPackedEnum, kEnum);
		}
		break;
	case PT_UINT32:
		type_card |= packed_or(f, kPackedUInt32, kUInt32);
		break;
	case PT_SINT32:
		type_card |= packed_or(f, kPackedSInt32, kSInt32);
		break;
	case PT_INT32:
		type_card |= packed_or(f, kPackedInt32, kInt32);
		break;
	case PT_UINT64:
		type_card |= packed_or(f, kPackedUInt64, kUInt64);
		break;
	case PT_SINT64:
		type_card |= packed_or(f, kPackedSInt64, kSInt64);
		break;
	case PT_INT64:
		type_card |= packed_or(f, kPackedInt64, kInt64);
		break;
	case PT_BYTES:
		type_card |= kBytes;
		break;
	case PT_STRING:
		type_card |= utf8_check_mode == UTF8_STRICT ? kUtf8String : kBytes;
		break;
	case PT_GROUP:
		type_card |= 0 | kMessage | kRepGroup;
		if (o->is_implicitly_weak)
			type_card |= kTvWeakPtr;
		else if (o->use_direct_tcparser_table)
			type_card |= kTvTable;
		else
			type_card |= kTvDefault;
		break;
	case PT_MESSAGE:
		if (is_map(f)) {
			type_card |= kMap;
		} else {
			type_card |= kMessage;
			if (has_lazy_rep(f, o)) {
				type_card |= kRepLazy | o->lazy_opt;
			} else if (o->is_implicitly_weak) {
				type_card |= kTvWeakPtr;
			} else if (o->use_direct_tcparser_table) {
				type_card |= kTvTable;
			} else {
				type_card |= kTvDefault;
			}
		}
		break;
	}
	if (f->type == PT_BYTES || f->type == PT_STRING) {
		switch (cpp_string_type(f)) {
		case CPP_STR_CORD:
			type_card |= kRepCord;
			break;
		case CPP_STR_VIEW:
		case CPP_STR_STRING:
			if (is_repeated(f))
				type_card |= kRepSString;
			else
				type_card |= o->str_kind == 2 ? kRepMString : kRepAString;
			break;
		}
	}
	if (o->should_split)
		type_card |= kSplitTrue;
	return type_card;
}

static bool tc_has_weak_fields(const LpMessage *d)
{
	for (int i = 0; i < d->n_fields; i++)
		if (is_weak_option(&d->fields[i]))
			return true;
	return false;
}

static int aux_push(TailCallTableInfo *t, AuxEntry e)
{
	if (t->n_aux_entries == t->cap_aux_entries) {
		int cap = t->cap_aux_entries ? 2 * t->cap_aux_entries : 8;
		t->aux_entries = xrealloc(t->aux_entries, sizeof(AuxEntry) * (size_t)t->cap_aux_entries, sizeof(AuxEntry) * (size_t)cap);
		t->cap_aux_entries = cap;
	}
	t->aux_entries[t->n_aux_entries] = e;
	return t->n_aux_entries++;
}

static void build_field_entries(TailCallTableInfo *t, const LpMessage *d, const TcMessageOptions *message_options,
				const TcFieldOptions *ordered_fields, int n)
{
	t->field_entries = xalloc(sizeof(FieldEntryInfo) * (size_t)(n + 1));
	t->n_field_entries = 0;
	size_t num_non_cold_subtables = 0;
	for (int i = 0; i < n; i++) {
		const TcFieldOptions *o = &ordered_fields[i];
		const LpField *f = o->field;
		bool non_cold = o->presence_probability >= 0.005;
		if ((f->type == PT_MESSAGE || f->type == PT_GROUP) && !is_map(f) && !is_weak_option(f) &&
		    !has_lazy_rep(f, o) && !o->is_implicitly_weak && o->use_direct_tcparser_table && non_cold)
			num_non_cold_subtables++;
	}
	int subtable_aux_idx = t->n_aux_entries;
	for (size_t i = 0; i < num_non_cold_subtables; i++)
		aux_push(t, (AuxEntry){.type = AUX_NOTHING});
	for (int i = 0; i < n; i++) {
		const TcFieldOptions *o = &ordered_fields[i];
		const LpField *f = o->field;
		FieldEntryInfo *entry = &t->field_entries[t->n_field_entries++];
		*entry = (FieldEntryInfo){.field = f, .hasbit_idx = o->has_bit_index};
		entry->utf8_check_mode = get_utf8_check_mode(f, message_options->is_lite);
		entry->type_card = make_type_card_for_field(f, entry->hasbit_idx >= 0, o, entry->utf8_check_mode);
		if (f->type == PT_MESSAGE || f->type == PT_GROUP) {
			if (is_map(f)) {
				entry->aux_idx = (uint16_t)aux_push(t, (AuxEntry){.type = AUX_MAP_AUX_INFO, .field = f});
				if (message_options->uses_codegen) {
					const LpField *mv = map_value(f->message_type);
					if (mv->message_type)
						aux_push(t, (AuxEntry){.type = AUX_SUB_TABLE, .field = mv});
					else if (mv->type == PT_ENUM && !has_preserving_unknown_enum_semantics(mv))
						aux_push(t, (AuxEntry){.type = AUX_ENUM_VALIDATOR, .field = mv});
				}
			} else if (is_weak_option(f)) {
				entry->type_card = 0;
			} else if (has_lazy_rep(f, o)) {
				if (message_options->uses_codegen) {
					entry->aux_idx = (uint16_t)aux_push(t, (AuxEntry){.type = AUX_SUB_MESSAGE_GLOBALS, .field = f});
					if (o->lazy_opt == kTvEager)
						aux_push(t, (AuxEntry){.type = AUX_MESSAGE_VERIFY_FUNC, .field = f});
					else
						aux_push(t, (AuxEntry){.type = AUX_NOTHING});
				} else {
					entry->aux_idx = kNoAuxIdx;
				}
			} else {
				AuxType type = o->is_implicitly_weak	       ? AUX_SUB_MESSAGE_GLOBALS_WEAK
					       : o->use_direct_tcparser_table ? AUX_SUB_TABLE
									      : AUX_SUB_MESSAGE_GLOBALS;
				if (type == AUX_SUB_TABLE && o->presence_probability >= 0.005) {
					t->aux_entries[subtable_aux_idx] = (AuxEntry){.type = type, .field = f};
					entry->aux_idx = (uint16_t)subtable_aux_idx;
					++subtable_aux_idx;
				} else {
					entry->aux_idx = (uint16_t)aux_push(t, (AuxEntry){.type = type, .field = f});
				}
			}
		} else if (f->type == PT_ENUM && !treat_enum_as_int(f)) {
			int idx = aux_push(t, (AuxEntry){.type = AUX_NOTHING});
			entry->aux_idx = (uint16_t)idx;
			AuxEntry *aux = &t->aux_entries[idx];
			if (get_enum_validation_range(f->enum_type, &aux->first, &aux->last)) {
				aux->type = AUX_ENUM_RANGE;
			} else {
				aux->type = AUX_ENUM_VALIDATOR;
				aux->field = f;
			}
		} else if (o->str_kind == 2) {
			entry->aux_idx = o->micro_string_sso;
		}
	}
}

/* TailCallTableInfo::TailCallTableInfo */
static TailCallTableInfo *tail_call_table_info_new(const LpMessage *d, const TcMessageOptions *message_options,
						   const TcFieldOptions *ordered_fields, int n)
{
	TailCallTableInfo *t = xalloc(sizeof *t);
	t->fallback_function = msg_option(d, CPP_MSGOPT_MAP_ENTRY)			     ? tc_func("DiscardEverythingFallback")
			       : !message_options->uses_codegen || tc_has_weak_fields(d) ? tc_func("ReflectionFallback")
			       : d->n_extension_ranges == 0			     ? tc_func("MpUnknownFields")
			       : message_options->is_lite			     ? tc_func("GenericFallbackLite")
										     : tc_func("GenericFallback");
	if (msg_option(d, CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT)) {
		t->fast_path_fields = xalloc(sizeof(FastFieldInfo));
		t->n_fast_path_fields = 1;
		if (message_options->uses_codegen) {
			t->fast_path_fields[0] = (FastFieldInfo){
				.kind = TC_FAST_NON_FIELD,
				.func = tc_func(message_options->is_lite ? "MessageSetWireFormatParseLoopLite"
									 : "MessageSetWireFormatParseLoop"),
			};
			t->n_aux_entries = 0;
			aux_push(t, (AuxEntry){.type = AUX_SELF_VERIFY_FUNC});
		} else {
			t->fast_path_fields[0] = (FastFieldInfo){.kind = TC_FAST_NON_FIELD, .func = tc_func("ReflectionParseLoop")};
		}
		t->table_size_log2 = 0;
		t->num_to_entry_table = make_num_to_entry_table(ordered_fields, n);
		generate_field_names(t, d);
		return t;
	}
	bool any_split = false;
	for (int i = 0; i < n; i++)
		any_split |= ordered_fields[i].should_split;
	if (any_split) {
		aux_push(t, (AuxEntry){.type = AUX_SPLIT_OFFSET});
		aux_push(t, (AuxEntry){.type = AUX_SPLIT_SIZEOF});
	}
	build_field_entries(t, d, message_options, ordered_fields, n);
	uint32_t end_group_tag = get_end_group_tag(d);
	FastFieldInfo fast_fields[kMaxFastFields];
	memset(fast_fields, 0, sizeof fast_fields);
	uint32_t important_fields = 0;
	size_t num_fast_fields = fast_parse_table_size((size_t)n, end_group_tag);
	populate_fast_fields(end_group_tag, t->field_entries, t->n_field_entries, message_options, ordered_fields,
			     fast_fields, (uint32_t)num_fast_fields, &important_fields);
	while (num_fast_fields > 1 && (important_fields & (important_fields >> num_fast_fields / 2)) == 0) {
		num_fast_fields /= 2;
		for (size_t i = 0; i < num_fast_fields; ++i) {
			size_t merge_i = i + num_fast_fields;
			if (((important_fields >> merge_i) & 1) != 0 || fast_less(&fast_fields[i], &fast_fields[merge_i]))
				fast_fields[i] = fast_fields[merge_i];
		}
		important_fields |= important_fields >> num_fast_fields;
	}
	t->fast_path_fields = xalloc(sizeof(FastFieldInfo) * num_fast_fields);
	memcpy(t->fast_path_fields, fast_fields, sizeof(FastFieldInfo) * num_fast_fields);
	t->n_fast_path_fields = (int)num_fast_fields;
	int width = 0;
	for (size_t v = num_fast_fields; v; v >>= 1)
		width++;
	t->table_size_log2 = width - 1;
	t->num_to_entry_table = make_num_to_entry_table(ordered_fields, n);
	generate_field_names(t, d);
	return t;
}
