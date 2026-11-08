// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* The options of protobuf-c/protobuf-c.proto (v1.5.2) as protoc-gen-c sees them: the generated
 * code protobuf-c.pb.cc (protoc 3.21.12) is linked into the plugin, so its extensions of
 * FileOptions, MessageOptions and FieldOptions (pb_c_file, pb_c_msg, pb_c_field, all number
 * 1019) are known when the request is parsed, while the descriptor pool of libprotoc keeps them
 * as unknown fields. They are read from those unknown fields with the semantics of the
 * generated parser of protobuf v3.21.12 (parse_context.h): an occurrence with another wire type
 * stays unknown, occurrences merge (the last scalar wins), and a payload that does not parse
 * makes the whole request unparseable (pbc_request_parses). */

enum { PBC_EXTENSION = 1019, PBC_MAX_DEPTH = 100 /* io::CodedInputStream::GetDefaultRecursionLimit */ };

/* ---- parse_context.h of protobuf v3.21.12 ---- */

typedef struct {
	const uint8_t *p, *end;
} PbcReader;

/* ReadTag: up to 5 bytes, the bits beyond 32 dropped */
static bool pbc_read_tag(PbcReader *r, uint32_t *tag)
{
	uint32_t res = 0;
	for (int i = 0; i < 5; i++) {
		if (r->p >= r->end)
			return false;
		uint32_t byte = *r->p++;
		res += (byte - (i ? 1u : 0u)) << (7 * i);
		if (byte < 128) {
			*tag = res;
			return true;
		}
	}
	return false;
}

/* VarintParse (64 bits): up to 10 bytes, the bits beyond 64 dropped */
static bool pbc_read_varint(PbcReader *r, uint64_t *v)
{
	uint64_t res = 0;
	for (int i = 0; i < 10; i++) {
		if (r->p >= r->end)
			return false;
		uint64_t byte = *r->p++;
		res += (byte - (i ? 1u : 0u)) << (7 * i);
		if (byte < 128) {
			*v = res;
			return true;
		}
	}
	return false;
}

/* ReadSize and the limit it pushes: a size below 2 GiB that fits in the payload */
static bool pbc_read_len(PbcReader *r, PbcReader *sub)
{
	uint32_t res = 0;
	for (int i = 0; i < 5; i++) {
		if (r->p >= r->end)
			return false;
		uint32_t byte = *r->p++;
		if (i == 4 && byte >= 8)
			return false;
		res += (byte - (i ? 1u : 0u)) << (7 * i);
		if (byte < 128 || i == 4) {
			if (res > 0x7fffffffu - 16 || res > (uint64_t)(r->end - r->p))
				return false;
			*sub = (PbcReader){r->p, r->p + res};
			r->p += res;
			return true;
		}
	}
	return false;
}

/* UnknownFieldParse (FieldParser): the value of a field kept as unknown. depth: the messages and
 * groups the field is in. */
static bool pbc_skip(PbcReader *r, uint32_t tag, int depth)
{
	uint64_t v;
	PbcReader sub;
	if ((tag >> 3) == 0)
		return false;
	switch (tag & 7) {
	case 0: return pbc_read_varint(r, &v);
	case 1:
		if (r->end - r->p < 8)
			return false;
		r->p += 8;
		return true;
	case 2: return pbc_read_len(r, &sub);
	case 3: /* ParseGroup */
		if (depth + 1 > PBC_MAX_DEPTH)
			return false;
		for (;;) {
			uint32_t t;
			if (r->p >= r->end || !pbc_read_tag(r, &t))
				return false;
			if (t == 0 || (t & 7) == 4)
				return t == tag + 1; /* ConsumeEndGroup */
			if (!pbc_skip(r, t, depth + 1))
				return false;
		}
	case 5:
		if (r->end - r->p < 4)
			return false;
		r->p += 4;
		return true;
	default: return false;
	}
}

/* ---- the generated messages ---- */

/* A field of the options messages: a bool (varint) or a string (length-delimited). */
typedef struct {
	uint8_t number;
	bool is_string;
} PbcField;

typedef struct {
	bool has;
	bool b;
	Str s;
} PbcValue;

/* _InternalParse of a payload, merged into values (one per field). */
static bool pbc_parse(PbcReader r, const PbcField *fields, int n, PbcValue *values, int depth)
{
	if (depth > PBC_MAX_DEPTH)
		return false;
	while (r.p < r.end) {
		uint32_t tag;
		if (!pbc_read_tag(&r, &tag) || tag == 0 || (tag & 7) == 4)
			return false; /* ends the message before its limit */
		int i = 0;
		while (i < n && fields[i].number != tag >> 3)
			i++;
		if (i < n && tag == ((tag >> 3) << 3 | (fields[i].is_string ? 2u : 0u))) {
			values[i].has = true;
			if (fields[i].is_string) {
				PbcReader s;
				if (!pbc_read_len(&r, &s))
					return false;
				values[i].s = (Str){(const char *)s.p, (size_t)(s.end - s.p)};
			} else {
				uint64_t v;
				if (!pbc_read_varint(&r, &v))
					return false;
				values[i].b = v != 0;
			}
		} else if (!pbc_skip(&r, tag, depth)) {
			return false;
		}
	}
	return true;
}

static const PbcField pbc_file_fields[] = {
	{1, false}, /* no_generate */
	{2, false}, /* gen_pack_helpers */
	{3, false}, /* gen_init_helpers */
	{4, false}, /* const_strings */
	{5, false}, /* use_oneof_field_name */
	{6, true},  /* c_package */
};
static const PbcField pbc_message_fields[] = {
	{1, false}, /* gen_pack_helpers */
	{2, false}, /* gen_init_helpers */
	{3, true},  /* base_field_name */
};
static const PbcField pbc_field_fields[] = {
	{1, false}, /* string_as_bytes */
};

/* GetExtension of an options message (NULL: the default instance): merges the payloads of the
 * extension's occurrences in the unknown fields. The request parsed (pbc_request_parses), so
 * they do. */
static void pbc_get_extension(const PbMsg *options, const PbcField *fields, int n, PbcValue *values)
{
	if (!options)
		return;
	PbcReader r = {(const uint8_t *)options->unknown.p, (const uint8_t *)options->unknown.p + options->unknown.n};
	while (r.p < r.end) {
		uint32_t tag;
		if (!pbc_read_tag(&r, &tag))
			return;
		if (tag == (PBC_EXTENSION << 3 | 2)) {
			PbcReader payload;
			if (!pbc_read_len(&r, &payload) || !pbc_parse(payload, fields, n, values, 0))
				return;
		} else if (!pbc_skip(&r, tag, 0)) {
			return;
		}
	}
}

/* A string option as a C string; a NUL byte in it is not supported. */
static const char *pbc_string(Str s, const char *option)
{
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: a NUL byte in the protobuf-c option %s", option);
	return xstrndup(s.p, s.n);
}

/* file->options().GetExtension(pb_c_file), parsed once per file */
static const PbcFileOptions *pbc_file_options(const LpFile *file)
{
	PbcFileOptions *opt = map_get(&cache.file_options, file->name);
	if (opt)
		return opt;
	PbcValue v[6] = {0};
	pbc_get_extension(file->options, pbc_file_fields, 6, v);
	opt = xalloc(sizeof *opt);
	*opt = (PbcFileOptions){
		.no_generate = v[0].b,
		.has_gen_pack_helpers = v[1].has,
		.gen_pack_helpers = v[1].has ? v[1].b : true,
		.gen_init_helpers = v[2].has ? v[2].b : true,
		.const_strings = v[3].b,
		.use_oneof_field_name = v[4].b,
		.has_c_package = v[5].has,
		.c_package = v[5].has ? pbc_string(v[5].s, "c_package") : "",
	};
	map_put(&cache.file_options, file->name, opt);
	return opt;
}

/* descriptor->options().GetExtension(pb_c_msg) */
static PbcMessageOptions pbc_message_options(const LpMessage *message)
{
	PbcValue v[3] = {0};
	pbc_get_extension(message->options, pbc_message_fields, 3, v);
	return (PbcMessageOptions){
		.has_gen_pack_helpers = v[0].has,
		.gen_pack_helpers = v[0].b,
		.has_gen_init_helpers = v[1].has,
		.gen_init_helpers = v[1].has ? v[1].b : true,
		.base_field_name = v[2].has ? pbc_string(v[2].s, "base_field_name") : "base",
	};
}

/* field->options().GetExtension(pb_c_field) */
static PbcFieldOptions pbc_field_options(const LpField *field)
{
	PbcValue v[1] = {0};
	pbc_get_extension(field->options, pbc_field_fields, 1, v);
	return (PbcFieldOptions){.string_as_bytes = v[0].b};
}

/* ---- the extensions in the request ---- */

/* Where descriptor.proto has the options messages: the fields of each message that hold one,
 * walked as the generated parser does (a field with another wire type is unknown). */
typedef enum { W_REQUEST, W_FILE, W_MESSAGE, W_FIELD, W_FILE_OPTIONS, W_MESSAGE_OPTIONS, W_FIELD_OPTIONS } PbcWalk;

static bool pbc_walk(PbcReader r, PbcWalk kind, int depth);

/* The message a length-delimited field of kind holds, or -1 (not walked). */
static int pbc_walk_child(PbcWalk kind, uint32_t number)
{
	switch (kind) {
	case W_REQUEST: return number == 15 ? W_FILE : -1;                                     /* proto_file */
	case W_FILE: return number == 4 ? W_MESSAGE : number == 7 ? W_FIELD : number == 8 ? W_FILE_OPTIONS : -1;
	case W_MESSAGE:
		return number == 2 || number == 6 ? W_FIELD : number == 3 ? W_MESSAGE : number == 7 ? W_MESSAGE_OPTIONS : -1;
	case W_FIELD: return number == 8 ? W_FIELD_OPTIONS : -1;
	default: return -1;
	}
}

/* Whether the extension payloads in r (a message of kind at depth) parse; other errors are left
 * to libprotoc, which fails on them too. */
static bool pbc_walk(PbcReader r, PbcWalk kind, int depth)
{
	while (r.p < r.end) {
		uint32_t tag;
		if (!pbc_read_tag(&r, &tag) || tag == 0 || (tag & 7) == 4)
			return true;
		int child = (tag & 7) == 2 ? pbc_walk_child(kind, tag >> 3) : -1;
		bool ext = (tag & 7) == 2 && tag >> 3 == PBC_EXTENSION && kind >= W_FILE_OPTIONS;
		if (child >= 0 || ext) {
			PbcReader sub;
			if (!pbc_read_len(&r, &sub))
				return true;
			if (depth + 1 > PBC_MAX_DEPTH)
				return false; /* ParseMessage: too deep */
			if (ext) {
				PbcValue v[6] = {0};
				const PbcField *fields = kind == W_FILE_OPTIONS	   ? pbc_file_fields
							 : kind == W_MESSAGE_OPTIONS ? pbc_message_fields
										     : pbc_field_fields;
				int n = kind == W_FILE_OPTIONS ? 6 : kind == W_MESSAGE_OPTIONS ? 3 : 1;
				if (!pbc_parse(sub, fields, n, v, depth + 1))
					return false;
			} else if (!pbc_walk(sub, (PbcWalk)child, depth + 1)) {
				return false;
			}
		} else if (!pbc_skip(&r, tag, depth)) {
			return true;
		}
	}
	return true;
}

/* Whether CodeGeneratorRequest::ParseFromFileDescriptor would not fail on the extensions. */
static bool pbc_request_parses(Str raw)
{
	PbcReader r = {(const uint8_t *)raw.p, (const uint8_t *)raw.p + raw.n};
	return pbc_walk(r, W_REQUEST, 0);
}
