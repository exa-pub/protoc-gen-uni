/* GeneratedMessage.mergeFromBuffer of package:protobuf 6.1.0 (lib/src/protobuf/
 * coded_buffer_reader.dart, coded_buffer.dart _mergeFromCodedBufferReader, unknown_field_set.dart
 * mergeFieldFromBuffer; BSD-3-Clause, Copyright the Dart project authors) into pkg/pb messages:
 * the same fields, values, unknown fields and exceptions as the Dart runtime. */
#include "dart_protobuf.h"
#include "dart_std.h"

#include <string.h>

static const char dpb_truncated[] = "InvalidProtocolBufferException: While parsing a protocol message, the input ended "
				    "unexpectedly\nin the middle of a field. This could either mean that the input\nhas been "
				    "truncated or that an embedded message misreported its\nown length.\n";
static const char dpb_negative[] = "Invalid argument(s): CodedBufferReader encountered an embedded string or message "
				   "which claimed to have negative size.";

typedef struct {
	const uint8_t *buf;
	int64_t len, pos, limit; /* limit: _currentLimit */
	uint32_t lastTag;
	int depth;
} DpbReader;

NORETURN COLD static void dpb_throw(const char *text) { fail_str(str_c(text)); }

NORETURN COLD static void dpb_ipbe(const char *message)
{
	dpb_throw(xcat("InvalidProtocolBufferException: ", message));
}

static void dpb_check_limit(DpbReader *r, int64_t increment)
{
	r->pos += increment;
	if (r->pos > r->limit)
		dpb_throw(dpb_truncated);
}

/* _readRawVarint32: at most 10 bytes before the current limit; the low 32 bits. */
static uint32_t dpb_varint32(DpbReader *r)
{
	int64_t bytes = r->limit - r->pos;
	if (bytes > 10)
		bytes = 10;
	uint64_t result = 0;
	for (int i = 0; i < bytes; i++) {
		uint8_t b = r->buf[r->pos++];
		result |= (uint64_t)(b & 0x7f) << (i * 7);
		if (!(b & 0x80))
			return (uint32_t)result;
	}
	dpb_ipbe("CodedBufferReader encountered a malformed varint.");
}

static int32_t dpb_int32(DpbReader *r) { return (int32_t)dpb_varint32(r); }

/* _readRawVarint64: byte by byte against the limit (truncated), 10 bytes at most. */
static uint64_t dpb_varint64(DpbReader *r)
{
	uint64_t result = 0;
	for (int i = 0; i < 10; i++) {
		dpb_check_limit(r, 1);
		uint8_t b = r->buf[r->pos - 1];
		result |= (uint64_t)(b & 0x7f) << (i * 7);
		if (!(b & 0x80))
			return result;
	}
	dpb_ipbe("CodedBufferReader encountered a malformed varint.");
}

static uint64_t dpb_fixed(DpbReader *r, int n)
{
	dpb_check_limit(r, n);
	uint64_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | r->buf[r->pos - n + i];
	return v;
}

static uint32_t dpb_read_tag(DpbReader *r)
{
	if (r->pos >= r->limit)
		return r->lastTag = 0;
	r->lastTag = dpb_varint32(r);
	if (r->lastTag >> 3 == 0)
		dpb_ipbe("Protocol message contained an invalid tag (zero).");
	return r->lastTag;
}

static void dpb_check_last_tag_was(DpbReader *r, uint32_t value)
{
	if (r->lastTag != value)
		dpb_ipbe("Protocol message end-group tag did not match expected tag.");
}

/* RangeError.range(value, start, end, name).toString() */
COLD static char *dpb_range_error(int64_t value, int64_t start, int64_t end, const char *name)
{
	const char *explanation = start == end ? xfmt("Only valid value is %lld", (long long)start)
					       : xfmt("Not in inclusive range %lld..%lld", (long long)start, (long long)end);
	return xfmt("RangeError (%s): Invalid value: %s: %lld", name, explanation, (long long)value);
}

/* readBytes (string false) and readString: a negative length moves the position back and fails
 * in Uint8List.view or Utf8Decoder.convert. */
static Str dpb_read_bytes(DpbReader *r, bool string)
{
	int64_t length = dpb_int32(r), start = r->pos;
	dpb_check_limit(r, length);
	if (length < 0)
		dpb_throw(string ? dpb_range_error(start + length, start, r->len, "end")
				 : xfmt("RangeError: Value not in range: %lld", (long long)length));
	return (Str){(const char *)r->buf + start, (size_t)length};
}

static PbVal dpb_copy(Str s)
{
	PbVal v;
	v.s.p = xstrndup(s.p, s.n);
	v.s.n = s.n;
	return v;
}

static void dpb_push(PbSlot *s, PbVal v)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 2;
		s->v = xrealloc(s->v, sizeof(PbVal) * (size_t)s->cap, sizeof(PbVal) * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = v;
}

static void dpb_merge(DpbReader *r, PbMsg *m);

static void dpb_with_limit_check(DpbReader *r, int64_t byte_limit, int64_t *old)
{
	if (byte_limit < 0)
		dpb_throw(dpb_negative);
	byte_limit += r->pos;
	*old = r->limit;
	if (byte_limit > r->limit || byte_limit > r->len)
		dpb_throw(dpb_truncated);
	r->limit = byte_limit;
}

static void dpb_read_message(DpbReader *r, PbMsg *m)
{
	int64_t length = dpb_int32(r);
	if (r->depth >= 100)
		dpb_ipbe("Protocol message had too many levels of nesting. May be malicious.\nUse a CodedBufferReader with a "
			 "defined recursion depth limit if you need to\nparse deeply nested messages.\n");
	if (length < 0)
		dpb_throw(dpb_negative);
	int64_t old = r->limit;
	r->limit = r->pos + length;
	if (r->limit > old)
		dpb_throw(dpb_truncated);
	r->depth++;
	dpb_merge(r, m);
	dpb_check_last_tag_was(r, 0);
	r->depth--;
	r->limit = old;
}

static bool dpb_merge_unknown(DpbReader *r, uint32_t tag);

/* readGroup (m) and readUnknownFieldSetGroup (m NULL; the caller keeps the group as read). */
static void dpb_read_group(DpbReader *r, uint32_t num, PbMsg *m)
{
	if (r->depth >= 100)
		dpb_ipbe("Protocol message had too many levels of nesting. May be malicious.\nUse a CodedBufferReader with a "
			 "defined recursion depth limit if you need to\nparse deeply nested messages.\n");
	r->depth++;
	if (m)
		dpb_merge(r, m);
	else
		for (;;) {
			uint32_t tag = dpb_read_tag(r);
			if (tag == 0 || !dpb_merge_unknown(r, tag))
				break;
		}
	dpb_check_last_tag_was(r, num << 3 | WT_EGROUP);
	r->depth--;
}

/* UnknownFieldSet.mergeFieldFromBuffer: reads the value; the caller keeps the field as read. */
static bool dpb_merge_unknown(DpbReader *r, uint32_t tag)
{
	switch (tag & 7) {
	case WT_VARINT:
		dpb_varint64(r);
		return true;
	case WT_I64:
		dpb_fixed(r, 8);
		return true;
	case WT_LEN:
		dpb_read_bytes(r, false);
		return true;
	case WT_SGROUP:
		dpb_read_group(r, tag >> 3, NULL);
		return true;
	case WT_EGROUP:
		return false;
	case WT_I32:
		dpb_fixed(r, 4);
		return true;
	default:
		dpb_ipbe("Protocol message tag had invalid wire type.");
	}
}

static bool dpb_wire_type_matches(int type, int wt)
{
	switch (type) {
	case PT_BOOL: case PT_ENUM: case PT_INT32: case PT_INT64: case PT_SINT32: case PT_SINT64: case PT_UINT32:
	case PT_UINT64:
		return wt == WT_VARINT || wt == WT_LEN;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32:
		return wt == WT_I32 || wt == WT_LEN;
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64:
		return wt == WT_I64 || wt == WT_LEN;
	case PT_GROUP:
		return wt == WT_SGROUP;
	default:
		return wt == WT_LEN;
	}
}

/* One value of a scalar type at the reader (whatever the wire type of the tag was). */
static uint64_t dpb_read_scalar(DpbReader *r, int type)
{
	switch (type) {
	case PT_BOOL: return dpb_varint32(r) != 0;
	case PT_INT32: case PT_ENUM: return (uint64_t)(int64_t)dpb_int32(r);
	case PT_UINT32: return dpb_varint32(r);
	case PT_SINT32: {
		uint32_t x = dpb_varint32(r);
		return (uint64_t)(int64_t)(int32_t)((x >> 1) ^ (0u - (x & 1)));
	}
	case PT_INT64: case PT_UINT64: return dpb_varint64(r);
	case PT_SINT64: {
		uint64_t x = dpb_varint64(r);
		return (x >> 1) ^ (0u - (x & 1));
	}
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return dpb_fixed(r, 4);
	default: return dpb_fixed(r, 8);
	}
}

static bool dpb_enum_known(const PbField *f, uint64_t v)
{
	if (!f->enum_vals)
		return true;
	for (int i = 0; i < f->n_enum_vals; i++)
		if ((uint64_t)(int64_t)f->enum_vals[i] == v)
			return true;
	return false;
}

/* A value of a scalar field: an unknown value of a closed enum goes to the unknown fields
 * (mergeVarintField(tagNumber, Int64(rawValue))). */
static void dpb_add_scalar(PbMsg *m, const PbField *f, PbSlot *s, uint64_t v)
{
	if (f->type == PT_ENUM && !dpb_enum_known(f, v)) {
		pb_put_tag(&m->unknown, f->num, WT_VARINT);
		pb_put_varint(&m->unknown, v);
	} else if (f->label == PL_OPT && s->n > 0) {
		s->v[0].u = v;
	} else {
		dpb_push(s, (PbVal){.u = v});
	}
}

static const PbField *dpb_field(const PbMsgDef *def, uint32_t num)
{
	int lo = 0, hi = def->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (def->f[mid].num == num)
			return &def->f[mid];
		if (def->f[mid].num < num)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

/* _mergeFromCodedBufferReader */
static void dpb_merge(DpbReader *r, PbMsg *m)
{
	for (;;) {
		int64_t start = r->pos;
		uint32_t tag = dpb_read_tag(r);
		if (tag == 0)
			return;
		int wt = (int)(tag & 7);
		const PbField *f = dpb_field(m->def, tag >> 3);
		if (!f || !dpb_wire_type_matches(f->type, wt)) {
			if (!dpb_merge_unknown(r, tag))
				return;
			buf_add(&m->unknown, r->buf + start, (size_t)(r->pos - start));
			continue;
		}
		PbSlot *s = &m->slots[f - m->def->f];
		bool repeated = f->label != PL_OPT;
		switch (f->type) {
		case PT_STRING: case PT_BYTES: {
			Str v = dpb_read_bytes(r, f->type == PT_STRING);
			if (f->type == PT_STRING)
				v = dart_utf8_decode_allow_malformed(v);
			if (!repeated && s->n > 0)
				s->v[0] = dpb_copy(v);
			else
				dpb_push(s, dpb_copy(v));
			break;
		}
		case PT_MESSAGE: case PT_GROUP: {
			PbMsg *sub = !repeated && s->n > 0 ? s->v[0].m : pb_new(f->msg);
			if (f->type == PT_MESSAGE)
				dpb_read_message(r, sub);
			else
				dpb_read_group(r, f->num, sub);
			if (repeated || s->n == 0)
				dpb_push(s, (PbVal){.m = sub});
			break;
		}
		default:
			if (repeated && wt == WT_LEN) {
				/* packed (`if (limit != 0) _withLimit(limit, ...)`; a limit of 0 reads nothing) */
				int64_t old;
				dpb_with_limit_check(r, dpb_int32(r), &old);
				while (r->pos < r->limit)
					dpb_add_scalar(m, f, s, dpb_read_scalar(r, f->type));
				r->limit = old;
			} else {
				dpb_add_scalar(m, f, s, dpb_read_scalar(r, f->type));
			}
		}
	}
}

typedef struct {
	PbMsg *m;
	DpbReader r;
} DpbMergeJob;

static void dpb_merge_job(void *arg)
{
	DpbMergeJob *job = arg;
	dpb_merge(&job->r, job->m);
	dpb_check_last_tag_was(&job->r, 0);
}

const char *dpb_merge_from_buffer(PbMsg *m, Str raw)
{
	DpbMergeJob job = {.m = m, .r = {.buf = (const uint8_t *)raw.p, .len = (int64_t)raw.n}};
	if (job.r.len > 0x7fffffff) /* sizeLimit */
		job.r.len = 0x7fffffff;
	job.r.limit = job.r.len;
	return try_run(dpb_merge_job, &job) ? fail_message().p : NULL;
}
