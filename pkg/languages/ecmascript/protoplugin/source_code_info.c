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

/* Ported from packages/protoplugin/src/source-code-info.ts: comments of descriptors, feature
 * options and declaration strings. Comments and default values are bytes from the request and
 * may contain NUL. */
#include "internal.h"

#include <string.h>

bool comments_normalize_line_breaks = true;

/* comment.replace(/\r\n|[\r  ]/g, "\n") */
static Str normalize_line_breaks(Str s)
{
	if (!comments_normalize_line_breaks)
		return s;
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		const unsigned char *p = (const unsigned char *)s.p + i;
		size_t rest = s.n - i;
		if (p[0] == '\r') {
			buf_addc(&b, '\n');
			i += rest > 1 && p[1] == '\n' ? 2 : 1;
		} else if (rest >= 3 && p[0] == 0xe2 && p[1] == 0x80 && (p[2] == 0xa8 || p[2] == 0xa9)) {
			buf_addc(&b, '\n');
			i += 3;
		} else {
			buf_addc(&b, (char)p[0]);
			i++;
		}
	}
	return buf_view(&b);
}

/* The key of a source path in DescFile.locations. */
static const char *path_key(const int64_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, i ? ",%lld" : "%lld", (long long)path[i]);
	return buf_str(&b);
}

/* findComments: the first location with the path. Upstream searches the locations for every
 * lookup; they are indexed once per file here. */
static Comments find_comments(DescFile *file, const Vec *path)
{
	if (!file->locations_done) {
		file->locations_done = true;
		const PbMsg *sci = pb_msg(file->h.proto, FDP_SOURCE_CODE_INFO);
		for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
			PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
			const PbSlot *ps = pb_slot(loc, LOC_PATH);
			int64_t *p = xalloc(sizeof(int64_t) * (size_t)(ps && ps->n ? ps->n : 1));
			for (int j = 0; ps && j < ps->n; j++)
				p[j] = (int64_t)ps->v[j].u;
			const char *key = path_key(p, ps ? ps->n : 0);
			if (!map_has(&file->locations, key))
				map_put(&file->locations, key, loc);
		}
	}
	int64_t *p = xalloc(sizeof(int64_t) * (size_t)(path->n ? path->n : 1));
	for (int j = 0; j < path->n; j++)
		p[j] = (intptr_t)path->v[j];
	const PbMsg *loc = map_get(&file->locations, path_key(p, path->n));
	Comments c = {0};
	if (!loc)
		return c;
	c.n_leading_detached = pb_count(loc, LOC_LEADING_DETACHED);
	c.leading_detached = xalloc(sizeof(Str) * (size_t)(c.n_leading_detached + 1));
	for (int j = 0; j < c.n_leading_detached; j++)
		c.leading_detached[j] = normalize_line_breaks(pb_bytes_at(loc, LOC_LEADING_DETACHED, j));
	if (pb_has(loc, LOC_LEADING))
		c.leading = normalize_line_breaks(pb_bytes(loc, LOC_LEADING));
	if (pb_has(loc, LOC_TRAILING))
		c.trailing = normalize_line_breaks(pb_bytes(loc, LOC_TRAILING));
	return c;
}

static void path_add(Vec *v, int a, int b)
{
	vec_push(v, (void *)(intptr_t)a);
	vec_push(v, (void *)(intptr_t)b);
}

/* Appends the source path of d (field numbers and indexes in descriptor.proto) to v. */
static void source_path(const Desc *d, Vec *v)
{
	switch (d->kind) {
	case DK_ENUM: {
		const DescEnum *e = (const DescEnum *)d;
		if (e->parent)
			source_path(&e->parent->h, v), path_add(v, DP_ENUM_TYPE, d->index);
		else
			path_add(v, FDP_ENUM_TYPE, d->index);
		break;
	}
	case DK_ONEOF:
		source_path(&((const DescOneof *)d)->parent->h, v);
		path_add(v, DP_ONEOF_DECL, d->index);
		break;
	case DK_MESSAGE: {
		const DescMessage *m = (const DescMessage *)d;
		if (m->parent)
			source_path(&m->parent->h, v), path_add(v, DP_NESTED_TYPE, d->index);
		else
			path_add(v, FDP_MESSAGE_TYPE, d->index);
		break;
	}
	case DK_ENUM_VALUE:
		source_path(&((const DescEnumValue *)d)->parent->h, v);
		path_add(v, EDP_VALUE, d->index);
		break;
	case DK_FIELD: {
		const DescField *f = (const DescField *)d;
		if (!f->is_extension)
			source_path(&f->parent->h, v), path_add(v, DP_FIELD, d->index);
		else if (f->parent)
			source_path(&f->parent->h, v), path_add(v, DP_EXTENSION, d->index);
		else
			path_add(v, FDP_EXTENSION, d->index);
		break;
	}
	case DK_SERVICE:
		path_add(v, FDP_SERVICE, d->index);
		break;
	case DK_METHOD:
		source_path(&((const DescMethod *)d)->parent->h, v);
		path_add(v, SDP_METHOD, d->index);
		break;
	}
}

Comments get_comments(const Desc *d)
{
	Vec path = {0};
	source_path(d, &path);
	return find_comments(desc_file_of(d), &path);
}

static Comments file_comments(DescFile *f, int num)
{
	Vec path = {0};
	vec_push(&path, (void *)(intptr_t)num);
	return find_comments(f, &path);
}

Comments get_syntax_comments(DescFile *f) { return file_comments(f, FDP_SYNTAX); }
Comments get_package_comments(DescFile *f) { return file_comments(f, FDP_PACKAGE); }

/* The enum fields of google.protobuf.FeatureSet known to @bufbuild/protobuf v2.17.0. */
static const char *const feature_names[9] = {
	"field_presence", "enum_type", "repeated_field_encoding", "utf8_validation", "message_encoding",
	"json_format", "enforce_naming_style", "default_symbol_visibility", "enforce_proto_limits",
};

static const char *feature_value_name(int field, int64_t v)
{
	static const char *const vals[9][5] = {
		{"FIELD_PRESENCE_UNKNOWN", "EXPLICIT", "IMPLICIT", "LEGACY_REQUIRED"},
		{"ENUM_TYPE_UNKNOWN", "OPEN", "CLOSED"},
		{"REPEATED_FIELD_ENCODING_UNKNOWN", "PACKED", "EXPANDED"},
		{"UTF8_VALIDATION_UNKNOWN", NULL, "VERIFY", "NONE"},
		{"MESSAGE_ENCODING_UNKNOWN", "LENGTH_PREFIXED", "DELIMITED"},
		{"JSON_FORMAT_UNKNOWN", "ALLOW", "LEGACY_BEST_EFFORT"},
		{"ENFORCE_NAMING_STYLE_UNKNOWN", "STYLE2024", "STYLE_LEGACY", "STYLE2026"},
		{"DEFAULT_SYMBOL_VISIBILITY_UNKNOWN", "EXPORT_ALL", "EXPORT_TOP_LEVEL", "LOCAL_ALL", "STRICT"},
		{"PROTO_LIMITS_UNKNOWN", "LEGACY_NO_EXPLICIT_LIMITS", "PROTO_LIMITS2026"},
	};
	return v >= 0 && v < 5 ? vals[field][v] : NULL;
}

/* The feature options set on the element in the protobuf source, e.g.
 * "features.field_presence = IMPLICIT". */
Vec feature_option_strings(const Desc *d)
{
	Vec out = {0};
	const PbMsg *fs = desc_features(d);
	if (!fs)
		return out;
	for (int i = 0; i < 9; i++) {
		if (!pb_has(fs, (uint32_t)i + 1))
			continue;
		const char *name = feature_value_name(i, pb_int(fs, (uint32_t)i + 1, 0));
		if (name)
			vec_push(&out, xcat("features.", feature_names[i], " = ", name));
	}
	return out;
}

static const char *ascii_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	return r;
}

/* A string that matches the definition of a field or enum value in the protobuf source. Does
 * not take custom options into account. */
Str declaration_string(const Desc *d)
{
	Buf b = {0};
	if (d->kind == DK_ENUM_VALUE) {
		const DescEnumValue *v = (const DescEnumValue *)d;
		buf_fmt(&b, "%s = %d", v->name, v->number);
		if (pb_int(desc_options(d), 1, 0))
			buf_adds(&b, " [deprecated = true]");
		return buf_view(&b);
	}
	const DescField *f = (const DescField *)d;
	const char *type_name = f->message ? f->message->type_name : f->enm ? f->enm->type_name : NULL;
	if (!type_name)
		type_name = ascii_lower(scalar_name(f->scalar));
	int edition = desc_file_of(d)->edition;
	int64_t label = pb_int(d->proto, FD_LABEL, LABEL_OPTIONAL);
	switch (f->field_kind) {
	case FK_SCALAR: case FK_ENUM: case FK_MESSAGE:
		if (edition == ED_PROTO2 && label == LABEL_REQUIRED)
			buf_adds(&b, "required ");
		if ((edition == ED_PROTO2 && !f->oneof && label == LABEL_OPTIONAL) ||
		    (edition == ED_PROTO3 && pb_int(d->proto, FD_PROTO3_OPTIONAL, 0)))
			buf_adds(&b, "optional ");
		buf_adds(&b, type_name);
		break;
	case FK_LIST:
		buf_adds(&b, xcat("repeated ", type_name));
		break;
	case FK_MAP:
		buf_adds(&b, xcat("map<", ascii_lower(scalar_name(f->map_key)), ", ", type_name, ">"));
		break;
	}
	buf_fmt(&b, " %s = %d", f->name, f->number);
	Buf o = {0}; /* options, ", "-separated */
	const PbMsg *po = pb_msg(d->proto, FD_OPTIONS);
	if (pb_has(po, FO_PACKED))
		buf_adds(&o, pb_int(po, FO_PACKED, 0) ? ", packed = true" : ", packed = false");
	if (pb_has(d->proto, FD_DEFAULT_VALUE)) {
		Str dv = pb_bytes(d->proto, FD_DEFAULT_VALUE);
		int64_t t = pb_int(d->proto, FD_TYPE, 1);
		buf_adds(&o, ", default = ");
		if (t == PT_BYTES || t == PT_STRING) {
			/* '"' + defaultValue.replace('"', '\\"') + '"': the first quote only */
			const char *q = memchr(dv.p, '"', dv.n);
			buf_addc(&o, '"');
			if (q) {
				buf_add(&o, dv.p, (size_t)(q - dv.p));
				buf_adds(&o, "\\\"");
				buf_add(&o, q + 1, dv.n - (size_t)(q - dv.p) - 1);
			} else {
				buf_add_str(&o, dv);
			}
			buf_addc(&o, '"');
		} else {
			buf_add_str(&o, dv);
		}
	}
	if (!f->is_extension && !str_eqc(f->json_name, proto_camel_case(f->name))) {
		buf_adds(&o, ", json_name = \"");
		buf_add_str(&o, f->json_name);
		buf_addc(&o, '"');
	}
	if (pb_has(po, FO_JSTYPE)) {
		static const char *const js[] = {"JS_NORMAL", "JS_STRING", "JS_NUMBER"};
		int64_t j = pb_int(po, FO_JSTYPE, 0);
		buf_adds(&o, xcat(", jstype = ", j >= 0 && j < 3 ? js[j] : xfmt("%d", (int)j)));
	}
	if (pb_has(po, FO_DEPRECATED))
		buf_adds(&o, ", deprecated = true");
	Vec fo = feature_option_strings(d);
	for (int i = 0; i < fo.n; i++)
		buf_adds(&o, xcat(", ", fo.v[i]));
	if (o.n) {
		buf_adds(&b, " [");
		buf_add(&b, o.p + 2, o.n - 2);
		buf_addc(&b, ']');
	}
	return buf_view(&b);
}
