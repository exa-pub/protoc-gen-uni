// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/tf_decode_data.{h,cc} (protobuf v36.2):
 * TextFormatDecodeData, the blobs that let the runtime rebuild TextFormat names from ObjC names. */

typedef struct {
	int32_t key;
	Str data;
} TextFormatDataEntry;

typedef struct {
	TextFormatDataEntry *entries;
	int n, cap;
} TextFormatDecodeData;

/* DecodeDataBuilder */
enum {
	kAddUnderscore = 0x80,
	kOpAsIs = 0x00,
	kOpFirstUpper = 0x40,
	kOpFirstLower = 0x20,
	kOpAllUpper = 0x60,
	kMaxSegmentLen = 0x1f,
};

typedef struct {
	bool need_underscore;
	bool is_all_upper;
	uint8_t op;
	int segment_len;
	Buf decode_data;
} DecodeDataBuilder;

static bool tf_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static char tf_toupper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static char tf_tolower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

static void ddb_reset(DecodeDataBuilder *b)
{
	b->need_underscore = false;
	b->op = 0;
	b->segment_len = 0;
	b->is_all_upper = true;
}

static void ddb_add_char(DecodeDataBuilder *b, char desired)
{
	++b->segment_len;
	b->is_all_upper &= tf_isupper(desired);
}

static void ddb_push(DecodeDataBuilder *b)
{
	uint8_t op = (uint8_t)(b->op | b->segment_len);
	if (b->need_underscore)
		op |= kAddUnderscore;
	if (op != 0)
		buf_addc(&b->decode_data, (char)op);
	ddb_reset(b);
}

static bool ddb_add_first(DecodeDataBuilder *b, char desired, char input)
{
	if (desired == input)
		b->op = kOpAsIs;
	else if (desired == tf_toupper(input))
		b->op = kOpFirstUpper;
	else if (desired == tf_tolower(input))
		b->op = kOpFirstLower;
	else
		return false; /* can't be transformed to match */
	ddb_add_char(b, desired);
	return true;
}

static bool ddb_add_character(DecodeDataBuilder *b, char desired, char input)
{
	/* at the max size, push to start a new segment */
	if (b->segment_len == kMaxSegmentLen)
		ddb_push(b);
	if (b->segment_len == 0)
		return ddb_add_first(b, desired, input);
	if (desired == input) {
		/* not transforming, or upper casing and it is supposed to be upper case */
		if (b->op != kOpAllUpper || tf_isupper(desired)) {
			ddb_add_char(b, desired);
			return true;
		}
		ddb_push(b);
		return ddb_add_first(b, desired, input);
	}
	/* upper casing and everything so far was upper case: promote op to AllUpper */
	if (desired == tf_toupper(input) && b->is_all_upper) {
		b->op = kOpAllUpper;
		ddb_add_char(b, desired);
		return true;
	}
	ddb_push(b);
	return ddb_add_first(b, desired, input);
}

/* DirectDecodeString: a marker, the string and its end. */
static Str direct_decode_string(const char *str)
{
	Buf r = {0};
	buf_addc(&r, '\0');
	buf_adds(&r, str);
	buf_addc(&r, '\0');
	return buf_view(&r);
}

/* TextFormatDecodeData::DecodeDataForString */
static Str decode_data_for_string(const char *input_for_decode, const char *desired_output)
{
	if (!*input_for_decode || !*desired_output)
		fail("error: got empty string for making TextFormat data, input: \"%s\", desired: \"%s\".", input_for_decode,
		     desired_output);
	DecodeDataBuilder builder = {0};
	ddb_reset(&builder);
	size_t x = 0, in_len = strlen(input_for_decode);
	for (const char *y = desired_output; *y; y++) {
		char d = *y;
		if (d == '_') {
			ddb_push(&builder);
			builder.need_underscore = true;
			continue;
		}
		/* out of input, or no transformation for the next character: a full decode */
		if (x >= in_len)
			return direct_decode_string(desired_output);
		if (ddb_add_character(&builder, d, input_for_decode[x]))
			++x;
		else
			return direct_decode_string(desired_output);
	}
	/* extra input (suffix from name sanitizing?) */
	if (x != in_len)
		return direct_decode_string(desired_output);
	ddb_push(&builder);
	buf_addc(&builder.decode_data, '\0'); /* the end marker */
	return buf_view(&builder.decode_data);
}

/* AddString; the keys (field numbers, enum value indexes) are unique by construction, so the
 * duplicate key check of upstream cannot fire. */
static void tf_add_string(TextFormatDecodeData *t, int32_t key, const char *input_for_decode, const char *desired_output)
{
	if (t->n == t->cap) {
		int cap = t->cap ? 2 * t->cap : 16;
		t->entries = xrealloc(t->entries, sizeof(TextFormatDataEntry) * (size_t)t->cap,
				      sizeof(TextFormatDataEntry) * (size_t)cap);
		t->cap = cap;
	}
	t->entries[t->n++] = (TextFormatDataEntry){key, decode_data_for_string(input_for_decode, desired_output)};
}

/* Data(): the count and the keys as varints, each key followed by its data (WriteString: the
 * bytes, without length). */
static Str tf_data(const TextFormatDecodeData *t)
{
	Buf out = {0};
	if (t->n > 0) {
		pb_put_varint(&out, (uint32_t)t->n);
		for (int i = 0; i < t->n; i++) {
			pb_put_varint(&out, (uint32_t)t->entries[i].key);
			buf_add(&out, t->entries[i].data.p, t->entries[i].data.n);
		}
	}
	return (Str){out.p ? out.p : "", out.n};
}
