/* Copyright (c) 2014 - 2025 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/BytecodeWriter.swift and ProtoNameInstructionWriter.swift, with
 * the constants of the SwiftProtobuf runtime (latestBytecodeProgramFormat in BytecodeReader.swift,
 * ProtoNameInstruction in NameMap.swift). */

enum {
	OP_SAME_NEXT = 1, OP_SAME_DELTA = 2, OP_STANDARD_NEXT = 3, OP_STANDARD_DELTA = 4,
	OP_UNIQUE_NEXT = 5, OP_UNIQUE_DELTA = 6, OP_GROUP_NEXT = 7, OP_GROUP_DELTA = 8,
	OP_ALIAS_NEXT = 9, OP_ALIAS_DELTA = 10, OP_RESERVED_NAME = 11, OP_RESERVED_NUMBERS = 12,
};
#define LATEST_BYTECODE_PROGRAM_FORMAT 0

typedef struct {
	Buf code;
	bool has_data;
	bool has_previous;
	int32_t previous_number;
} NameWriter;

/* append(escapingIfNecessary:) for one byte of the UTF-8 encoding: bytes of multi-byte sequences
 * pass through like their scalar does. */
static void bc_append_escaping(NameWriter *w, unsigned char c)
{
	if (c == '\\' || c == '"') {
		buf_addc(&w->code, '\\');
		buf_addc(&w->code, (char)c);
	} else if (c < ' ' || c == 0x7f) {
		buf_fmt(&w->code, "\\u{%x}", c);
	} else {
		buf_addc(&w->code, (char)c);
	}
	w->has_data = true;
}

static void bc_append_value(NameWriter *w, uint64_t value)
{
	if (value == 0) {
		buf_adds(&w->code, "\\0");
		w->has_data = true;
	} else if (value >= 0x20 && value < 0x7f) { /* isprint in the C locale */
		bc_append_escaping(w, (unsigned char)value);
	} else {
		buf_fmt(&w->code, "\\u{%llx}", (long long)value);
		w->has_data = true;
	}
}

static void bc_write_uint64(NameWriter *w, uint64_t v)
{
	while (v > 0x3f) {
		bc_append_value(w, (v & 0x3f) | 0x40);
		v >>= 6;
	}
	bc_append_value(w, v);
}

static void bc_write_int32(NameWriter *w, int32_t v) { bc_write_uint64(w, (uint32_t)v); }

static void bc_write_string(NameWriter *w, const char *s)
{
	for (; *s; s++)
		bc_append_escaping(w, (unsigned char)*s);
	bc_write_uint64(w, 0);
}

static void name_writer_init(NameWriter *w)
{
	*w = (NameWriter){0};
	bc_write_uint64(w, LATEST_BYTECODE_PROGRAM_FORMAT);
	w->has_data = false;
}

/* BytecodeWriter.stringLiteral */
static char *name_writer_literal(NameWriter *w) { return xcat("\"", buf_str(&w->code), "\""); }

static int32_t nw_delta(NameWriter *w, int32_t number)
{
	if (!w->has_previous) {
		w->has_previous = true;
		w->previous_number = number;
		return number;
	}
	if (number < w->previous_number)
		fail("field/case numbers must be written in ascending order");
	int64_t delta = (int64_t)number - w->previous_number;
	if (delta > INT32_MAX)
		fail("arithmetic overflow");
	w->previous_number = number;
	return (int32_t)delta;
}

static void nw_opcode(NameWriter *w, int32_t number, int next_op)
{
	int32_t delta = nw_delta(w, number);
	if (delta == 1) {
		bc_write_uint64(w, (uint64_t)next_op);
	} else {
		bc_write_uint64(w, (uint64_t)next_op + 1);
		bc_write_int32(w, delta);
	}
}

static void nw_write_same(NameWriter *w, int32_t number, const char *name)
{
	nw_opcode(w, number, OP_SAME_NEXT);
	bc_write_string(w, name);
}

static void nw_write_standard(NameWriter *w, int32_t number, const char *name)
{
	nw_opcode(w, number, OP_STANDARD_NEXT);
	bc_write_string(w, name);
}

static void nw_write_unique(NameWriter *w, int32_t number, const char *proto_name, const char *json_name)
{
	nw_opcode(w, number, OP_UNIQUE_NEXT);
	bc_write_string(w, proto_name);
	bc_write_string(w, json_name ? json_name : "");
}

static void nw_write_group(NameWriter *w, int32_t number, const char *name)
{
	nw_opcode(w, number, OP_GROUP_NEXT);
	bc_write_string(w, name);
}

static void nw_write_aliased(NameWriter *w, const SwEnumValue *v, const Vec *aliases)
{
	nw_opcode(w, v->number, OP_ALIAS_NEXT);
	bc_write_string(w, v->name);
	bc_write_uint64(w, (uint64_t)aliases->n);
	for (int i = 0; i < aliases->n; i++)
		bc_write_string(w, VEC_AT(*aliases, SwEnumValue, i)->name);
}

static void nw_write_reserved_name(NameWriter *w, const char *name)
{
	bc_write_uint64(w, OP_RESERVED_NAME);
	bc_write_string(w, name);
}

static void nw_write_reserved_numbers(NameWriter *w, SwRange r)
{
	bc_write_uint64(w, OP_RESERVED_NUMBERS);
	bc_write_int32(w, r.start);
	int64_t size = (int64_t)r.end - r.start;
	if (size > INT32_MAX)
		fail("arithmetic overflow");
	bc_write_int32(w, (int32_t)size);
}
