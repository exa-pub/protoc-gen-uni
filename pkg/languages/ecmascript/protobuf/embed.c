/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/protobuf/src/codegenv2/embed.ts (embedFileDesc, pathInFileDesc,
 * createFileDescriptorProtoBoot). */
#include "protobuf.h"

/* ---- base64 ---- */

static void mark_fields(PbMsg *m)
{
	for (int i = 0; i < pb_count(m, DP_FIELD); i++)
		pb_msg_at(m, DP_FIELD, i)->mark = 1;
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		mark_fields(pb_msg_at(m, DP_NESTED_TYPE, i));
}

/* Drops dependency and source_code_info of the file, and json_name of message fields if it is
 * the default (stripJsonNames). */
static bool embed_skip(void *ctx, const PbMsg *m, uint32_t num)
{
	if (m == ctx)
		return num == FDP_DEPENDENCY || num == FDP_SOURCE_CODE_INFO;
	if (m->mark && num == FD_JSON_NAME)
		return str_eqc(pb_bytes(m, FD_JSON_NAME), proto_camel_case(pb_str(m, FD_NAME)));
	return false;
}

const char *embed_base64(const PbMsg *file)
{
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++)
		mark_fields(pb_msg_at(file, FDP_MESSAGE_TYPE, i));
	Buf b = {0};
	pb_encode(file, &b, embed_skip, (void *)file);
	return base64_raw_std((const uint8_t *)b.p, b.n);
}

/* ---- boot ---- */

bool embed_bootable(const PbMsg *file) { return str_eqc(pb_bytes(file, FDP_NAME), "google/protobuf/descriptor.proto"); }

/* A JSON object like JSON.stringify writes it: keys in insertion order. */
typedef struct {
	Buf b;
} JsonObj;

COLD static void json_raw(JsonObj *o, const char *key, const char *value)
{
	buf_addc(&o->b, o->b.n ? ',' : '{');
	buf_addc(&o->b, '"');
	buf_adds(&o->b, key);
	buf_adds(&o->b, "\":");
	buf_adds(&o->b, value);
}

COLD static void json_str(JsonObj *o, const char *key, Str value) { json_raw(o, key, quote_json(value)); }
COLD static void json_num(JsonObj *o, const char *key, int64_t value) { json_raw(o, key, xfmt("%lld", (long long)value)); }

COLD static const char *json_end(JsonObj *o)
{
	if (!o->b.n)
		buf_addc(&o->b, '{');
	buf_addc(&o->b, '}');
	return buf_str(&o->b);
}

/* A JSON array of fn(element) for the repeated message field num of m. */
COLD static const char *json_array(const PbMsg *m, uint32_t num, const char *(*fn)(const PbMsg *))
{
	Buf b = {0};
	buf_addc(&b, '[');
	for (int i = 0; i < pb_count(m, num); i++) {
		if (i)
			buf_addc(&b, ',');
		buf_adds(&b, fn(pb_msg_at(m, num, i)));
	}
	buf_addc(&b, ']');
	return buf_str(&b);
}

/* Assert that condition is truthy or throw error (new Error(): an empty message). */
COLD static void boot_assert(bool cond)
{
	if (!cond)
		fail("%s", "");
}

static const char *enum_boot(const PbMsg *e);

COLD static const char *range_boot(const PbMsg *r)
{
	boot_assert(!pb_has(r, 3)); /* options */
	JsonObj o = {0};
	json_num(&o, "start", (int32_t)pb_int(r, ER_START, 0));
	json_num(&o, "end", (int32_t)pb_int(r, ER_END, 0));
	return json_end(&o);
}

COLD static const char *edition_default_boot(const PbMsg *d)
{
	JsonObj o = {0};
	json_str(&o, "value", pb_bytes(d, ED_VALUE));
	json_num(&o, "edition", (int32_t)pb_int(d, ED_EDITION, 0));
	return json_end(&o);
}

COLD static const char *field_options_boot(const PbMsg *opts)
{
	JsonObj o = {0};
	boot_assert(!pb_has(opts, 1)); /* ctype */
	if (pb_has(opts, FO_PACKED))
		json_raw(&o, "packed", pb_int(opts, FO_PACKED, 0) ? "true" : "false");
	boot_assert(!pb_has(opts, FO_JSTYPE) && !pb_has(opts, 5) && !pb_has(opts, 15)); /* jstype, lazy, unverified_lazy */
	if (pb_has(opts, FO_DEPRECATED))
		json_raw(&o, "deprecated", pb_int(opts, FO_DEPRECATED, 0) ? "true" : "false");
	boot_assert(!pb_has(opts, 10) && !pb_has(opts, 16)); /* weak, debug_redact */
	if (pb_has(opts, FO_RETENTION))
		json_num(&o, "retention", (int32_t)pb_int(opts, FO_RETENTION, 0));
	if (pb_has(opts, FO_TARGETS)) {
		Buf t = {0};
		buf_addc(&t, '[');
		for (int i = 0; i < pb_count(opts, FO_TARGETS); i++)
			buf_fmt(&t, i ? ",%d" : "%d", (int32_t)pb_int_at(opts, FO_TARGETS, i));
		buf_addc(&t, ']');
		json_raw(&o, "targets", buf_str(&t));
	}
	if (pb_has(opts, FO_EDITION_DEFAULTS))
		json_raw(&o, "editionDefaults", json_array(opts, FO_EDITION_DEFAULTS, edition_default_boot));
	boot_assert(!pb_has(opts, 21) && !pb_has(opts, 999)); /* features, uninterpreted_option */
	return json_end(&o);
}

COLD static const char *field_boot(const PbMsg *f)
{
	boot_assert(pb_has(f, FD_NAME) && pb_has(f, FD_NUMBER) && pb_has(f, FD_TYPE) && !pb_has(f, FD_ONEOF_INDEX));
	/* json_name is stripped if it is the default (stripJsonNames) */
	boot_assert(!pb_has(f, FD_JSON_NAME) ||
		    str_eqc(pb_bytes(f, FD_JSON_NAME), proto_camel_case(pb_str(f, FD_NAME))));
	JsonObj o = {0};
	json_str(&o, "name", pb_bytes(f, FD_NAME));
	json_num(&o, "number", (int32_t)pb_int(f, FD_NUMBER, 0));
	json_num(&o, "type", (int32_t)pb_int(f, FD_TYPE, PT_DOUBLE));
	if (pb_has(f, FD_LABEL))
		json_num(&o, "label", (int32_t)pb_int(f, FD_LABEL, 0));
	if (pb_has(f, FD_TYPE_NAME))
		json_str(&o, "typeName", pb_bytes(f, FD_TYPE_NAME));
	if (pb_has(f, FD_EXTENDEE))
		json_str(&o, "extendee", pb_bytes(f, FD_EXTENDEE));
	if (pb_has(f, FD_DEFAULT_VALUE))
		json_str(&o, "defaultValue", pb_bytes(f, FD_DEFAULT_VALUE));
	if (pb_has(f, FD_OPTIONS))
		json_raw(&o, "options", field_options_boot(pb_msg(f, FD_OPTIONS)));
	return json_end(&o);
}

COLD static const char *descriptor_boot(const PbMsg *m)
{
	boot_assert(!pb_has(m, DP_EXTENSION) && !pb_has(m, DP_ONEOF_DECL) && !pb_has(m, DP_OPTIONS) && !pb_has(m, 11));
	JsonObj o = {0};
	json_str(&o, "name", pb_bytes(m, DP_NAME));
	if (pb_has(m, DP_FIELD))
		json_raw(&o, "field", json_array(m, DP_FIELD, field_boot));
	if (pb_has(m, DP_NESTED_TYPE))
		json_raw(&o, "nestedType", json_array(m, DP_NESTED_TYPE, descriptor_boot));
	if (pb_has(m, DP_ENUM_TYPE))
		json_raw(&o, "enumType", json_array(m, DP_ENUM_TYPE, enum_boot));
	if (pb_has(m, DP_EXTENSION_RANGE))
		json_raw(&o, "extensionRange", json_array(m, DP_EXTENSION_RANGE, range_boot));
	return json_end(&o);
}

COLD static const char *enum_value_boot(const PbMsg *v)
{
	boot_assert(!pb_has(v, EVDP_OPTIONS));
	JsonObj o = {0};
	json_str(&o, "name", pb_bytes(v, EVDP_NAME));
	json_num(&o, "number", (int32_t)pb_int(v, EVDP_NUMBER, 0));
	return json_end(&o);
}

COLD static const char *enum_boot(const PbMsg *e)
{
	boot_assert(!pb_has(e, EDP_OPTIONS) && !pb_has(e, 6)); /* visibility */
	JsonObj o = {0};
	json_str(&o, "name", pb_bytes(e, EDP_NAME));
	json_raw(&o, "value", json_array(e, EDP_VALUE, enum_value_boot));
	return json_end(&o);
}

const char *embed_boot(const PbMsg *file)
{
	/* the name is google/protobuf/descriptor.proto (embed_bootable); dependency and
	 * source_code_info are cleared by embedFileDesc */
	const char *syntax = pb_str(file, FDP_SYNTAX);
	boot_assert(str_eqc(pb_bytes(file, FDP_PACKAGE), "google.protobuf"));
	boot_assert(!pb_has(file, FDP_PUBLIC_DEPENDENCY) && !pb_has(file, FDP_WEAK_DEPENDENCY) && !pb_has(file, 15));
	boot_assert(!pb_has(file, FDP_SERVICE) && !pb_has(file, FDP_EXTENSION));
	boot_assert(!*syntax || str_eq(syntax, "proto2"));
	boot_assert(!pb_has(pb_msg(file, FDP_OPTIONS), 50)); /* we're dropping file options */
	boot_assert(pb_int(file, FDP_EDITION, 0) == 0);
	JsonObj o = {0};
	json_str(&o, "name", pb_bytes(file, FDP_NAME));
	json_str(&o, "package", pb_bytes(file, FDP_PACKAGE));
	json_raw(&o, "messageType", json_array(file, FDP_MESSAGE_TYPE, descriptor_boot));
	json_raw(&o, "enumType", json_array(file, FDP_ENUM_TYPE, enum_boot));
	return json_end(&o);
}

/* ---- paths ---- */

Vec path_in_file_desc(const Desc *d)
{
	Vec path = {0};
	if (d->kind == DK_SERVICE) {
		vec_push(&path, (void *)(intptr_t)d->pos);
		return path;
	}
	DescMessage *parent = NULL;
	if (d->kind == DK_ENUM)
		parent = ((const DescEnum *)d)->parent;
	else if (d->kind == DK_MESSAGE)
		parent = ((const DescMessage *)d)->parent;
	else if (d->kind == DK_FIELD)
		parent = ((const DescField *)d)->parent;
	Vec chain = {0}; /* d, its parent, ... (innermost first) */
	vec_push(&chain, (void *)d);
	for (DescMessage *p = parent; p; p = p->parent)
		vec_push(&chain, p);
	for (int i = chain.n - 1; i >= 0; i--)
		vec_push(&path, (void *)(intptr_t)((const Desc *)chain.v[i])->pos);
	return path;
}
