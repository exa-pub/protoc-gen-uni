/* Copyright (c) 2014 - 2023 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/CodeGenerator.swift, CodeGeneratorParameter.swift,
 * GeneratorOutputs.swift and StringUtils.swift (swift-protobuf 1.39.0), plus the checks that
 * SwiftProtobuf's binary decoder applies to the request. */
#include "swift_protobuf.h"

#include <string.h>

extern const PbMsgDef pbd_EnumValueOptions, pbd_NamePart;

/* trimWhitespace: trimmingCharacters(in: .whitespacesAndNewlines) */
Str sw_trim_whitespace(Str s)
{
	size_t a = 0, b = s.n;
	while (a < b) {
		uint32_t c;
		int w = sw_next_scalar(s.p + a, b - a, &c);
		if (!sw_is_whitespace_or_newline(c))
			break;
		a += (size_t)w;
	}
	while (b > a) {
		/* find the start of the last scalar */
		size_t k = b - 1;
		while (k > a && ((unsigned char)s.p[k] & 0xc0) == 0x80)
			k--;
		uint32_t c;
		sw_next_scalar(s.p + k, b - k, &c);
		if (!sw_is_whitespace_or_newline(c))
			break;
		b = k;
	}
	return (Str){s.p + a, b - a};
}

int sw_parse_parameter(Str parameter, SwParamPair **pairs)
{
	*pairs = NULL;
	if (!parameter.n)
		return 0;
	Str *parts;
	int n = str_split_s(parameter, ',', &parts);
	*pairs = xalloc(sizeof(SwParamPair) * (size_t)n);
	for (int i = 0; i < n; i++) {
		Str s = parts[i];
		const char *eq = memchr(s.p, '=', s.n);
		if (!eq) {
			(*pairs)[i].key = sw_trim_whitespace(s);
			(*pairs)[i].value = STR_LIT("");
			continue;
		}
		size_t k = (size_t)(eq - s.p);
		(*pairs)[i].key = sw_trim_whitespace((Str){s.p, k});
		(*pairs)[i].value = sw_trim_whitespace((Str){s.p + k + 1, s.n - k - 1});
	}
	return n;
}

void sw_outputs_add(SwOutputs *o, const char *name, Str content)
{
	if (map_has(&o->seen, name))
		fail("Generator tried to generate two files named %s.", name);
	map_put(&o->seen, name, (void *)name);
	vec_push(&o->names, (void *)name);
	Str *c = xalloc(sizeof *c);
	*c = content;
	vec_push(&o->contents, c);
}

/* ---- request checks ---- */

static bool valid_utf8(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xfffd && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

static const char *check_message(const PbMsg *m, bool enum_value_json)
{
	if (m->def == &pbd_NamePart && (!pb_has(m, 1) || !pb_has(m, 2)))
		return "missingRequiredFields";
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		for (int j = 0; j < s->n; j++) {
			if (f->type == PT_STRING && !valid_utf8(s->v[j].s))
				return "invalidUTF8";
			if (f->type == PT_MESSAGE) {
				const char *err = check_message(s->v[j].m, enum_value_json);
				if (err)
					return err;
			}
		}
	}
	if (enum_value_json && m->def == &pbd_EnumValueOptions) {
		/* The pb.enumvalue.json extension is decoded too. */
		PbReader r = pb_unknown_fields(m);
		PbRawField f;
		while (pb_next_field(&r, &f)) {
			if (f.num != 998 || f.wt != WT_LEN)
				continue;
			PbReader ir = {f.val, f.val + f.vlen};
			PbRawField g;
			while (pb_next_field(&ir, &g))
				if (g.num == 1 && g.wt == WT_LEN && !valid_utf8((Str){(const char *)g.val, g.vlen}))
					return "invalidUTF8";
		}
	}
	return NULL;
}

const char *sw_check_request(const PbMsg *req, bool enum_value_json) { return check_message(req, enum_value_json); }

/* ---- generateCode(request:generator:) ---- */

typedef struct {
	PbMsg *req;
	const SwCodeGenerator *gen;
	SwOutputs outputs;
	const char *error; /* not thrown: protoc asked for an unknown file */
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	SwDescriptorSet *set = sw_descriptor_set(c->req, c->gen->edition_defaults);
	int n = pb_count(c->req, CGR_FILE_TO_GENERATE);
	SwFile **files = xalloc(sizeof(SwFile *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		const char *name = sw_ascii_name(pb_bytes_at(c->req, CGR_FILE_TO_GENERATE, i),
						 "CodeGeneratorRequest.file_to_generate");
		files[i] = sw_file_named(set, name);
		if (!files[i]) {
			c->error = xcat("protoc asked plugin to generate a file but did not provide a descriptor for the file: ",
					name);
			return;
		}
	}
	c->gen->generate(files, n, pb_bytes(c->req, CGR_PARAMETER), &c->outputs);
}

void sw_generate_code(PbMsg *req, const SwCodeGenerator *gen, Buf *out)
{
	RunCtx c = {0};
	c.req = req;
	c.gen = gen;
	Str error = {0};
	if (try_run(run, &c))
		error = fail_message();
	else if (c.error)
		error = str_c(c.error);
	if (error.p)
		pb_put_bytes(out, CGRESP_ERROR, error);
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, gen->supported_features);
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, (uint64_t)gen->minimum_edition);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, (uint64_t)gen->maximum_edition);
	if (error.p)
		return;
	for (int i = 0; i < c.outputs.names.n; i++) {
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(c.outputs.names.v[i]));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, *(Str *)c.outputs.contents.v[i]);
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
}
