/* Copyright 2025 Google LLC. Licensed under the Apache License, Version 2.0 (see LICENSE of
 * google.golang.org/genproto). */

/* Port of google.golang.org/genproto/googleapis/api/annotations (v0.0.0-20250811160224):
 * http.pb.go (HttpRule, CustomHttpPattern) and the registered extensions. */
#include "annotations.h"

#include <string.h>

const char *const genproto_annotations_extensions[] = {
	"google.api.http", "google.api.routing", "google.api.method_signature", "google.api.default_host",
	"google.api.oauth_scopes", "google.api.api_version", "google.api.field_behavior", "google.api.field_info",
	"google.api.resource_reference", "google.api.resource_definition", "google.api.resource", NULL,
};

const char *const genproto_visibility_extensions[] = {
	"google.api.enum_visibility", "google.api.value_visibility", "google.api.field_visibility",
	"google.api.message_visibility", "google.api.method_visibility", "google.api.api_visibility", NULL,
};

enum { M_HttpRule, M_CustomHttpPattern, M_COUNT };

static GoMsgDef *defs;

static COLD void build(GoMsgDef *def, const char *full_name, const PbField *src, const int *oneof, const char *const *names, int n)
{
	PbField *f = xalloc(sizeof *f * (size_t)n);
	uint8_t *flags = xalloc((size_t)n);
	int *oo = xalloc(sizeof *oo * (size_t)n);
	const char **nm = xalloc(sizeof *nm * (size_t)n);
	int *order = xalloc(sizeof *order * (size_t)n);
	for (int i = 0; i < n; i++) {
		f[i] = src[i];
		if (f[i].type == PT_STRING)
			flags[i] |= GOF_UTF8; /* proto3 */
		if (f[i].label == PL_OPT && !oneof[i] && f[i].type != PT_MESSAGE)
			flags[i] |= GOF_IMPLICIT;
		oo[i] = oneof[i];
		nm[i] = xcat(full_name, ".", names[i]);
		order[i] = i;
	}
	*def = (GoMsgDef){.pb = {n, f}, .full_name = full_name, .flags = flags, .oneof = oo, .names = nm, .order = order};
}

COLD void genproto_annotations_init(void)
{
	defs = xalloc(sizeof *defs * M_COUNT);
	const PbField rule[] = {
		{1, PT_STRING, PL_OPT, NULL, NULL, 0},
		{2, PT_STRING, PL_OPT, NULL, NULL, 0},
		{3, PT_STRING, PL_OPT, NULL, NULL, 0},
		{4, PT_STRING, PL_OPT, NULL, NULL, 0},
		{5, PT_STRING, PL_OPT, NULL, NULL, 0},
		{6, PT_STRING, PL_OPT, NULL, NULL, 0},
		{7, PT_STRING, PL_OPT, NULL, NULL, 0},
		{8, PT_MESSAGE, PL_OPT, &defs[M_CustomHttpPattern].pb, NULL, 0},
		{11, PT_MESSAGE, PL_REP, &defs[M_HttpRule].pb, NULL, 0},
		{12, PT_STRING, PL_OPT, NULL, NULL, 0},
	};
	static const int rule_oneof[] = {0, 1, 1, 1, 1, 1, 0, 1, 0, 0};
	static const char *const rule_names[] = {"selector", "get", "put", "post", "delete", "patch", "body", "custom", "additional_bindings", "response_body"};
	build(&defs[M_HttpRule], "google.api.HttpRule", rule, rule_oneof, rule_names, 10);
	const PbField custom[] = {{1, PT_STRING, PL_OPT, NULL, NULL, 0}, {2, PT_STRING, PL_OPT, NULL, NULL, 0}};
	static const int custom_oneof[] = {0, 0};
	static const char *const custom_names[] = {"kind", "path"};
	build(&defs[M_CustomHttpPattern], "google.api.CustomHttpPattern", custom, custom_oneof, custom_names, 2);
}

const GoMsgDef *genproto_http_rule(void) { return &defs[M_HttpRule]; }

Str genproto_ext_message(const PbMsg *opts, uint32_t num, bool *present)
{
	*present = false;
	Buf b = {0};
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != num || f.wt != WT_LEN)
			continue;
		*present = true;
		buf_add(&b, f.val, f.vlen);
	}
	return (Str){b.p ? b.p : "", b.n};
}

Str genproto_ext_string(const PbMsg *opts, uint32_t num)
{
	Str s = {"", 0};
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f))
		if (f.num == num && f.wt == WT_LEN)
			s = (Str){(const char *)f.val, f.vlen};
	return s;
}

int genproto_ext_enums(const PbMsg *opts, uint32_t num, int32_t **out)
{
	int n = 0;
	int32_t *vals = NULL;
	PbReader r = pb_unknown_fields(opts);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != num)
			continue;
		if (f.wt == WT_VARINT) {
			vals = xrealloc(vals, sizeof *vals * (size_t)n, sizeof *vals * (size_t)(n + 1));
			vals[n++] = (int32_t)(uint32_t)f.v;
		} else if (f.wt == WT_LEN) {
			PbReader pr = {f.val, f.val + f.vlen};
			uint64_t x;
			while (pr.p < pr.end && pb_read_varint(&pr, &x)) {
				vals = xrealloc(vals, sizeof *vals * (size_t)n, sizeof *vals * (size_t)(n + 1));
				vals[n++] = (int32_t)(uint32_t)x;
			}
		}
	}
	*out = vals;
	return n;
}
