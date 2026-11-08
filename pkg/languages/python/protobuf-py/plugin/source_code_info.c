/* Copyright (c) 2025-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* protobuf/plugin/_source_code_info.py (get_comments) and DescComments.lines() of
 * _descriptors.py, protobuf-py v0.7.0. The first location with the path is found through a map
 * built once per file (upstream scans the locations for every lookup). */
#include "internal.h"

static void path_add(Buf *b, int64_t v) { buf_fmt(b, "%lld,", (long long)v); }

/* The source path of desc as text ("4,0,2,1,"); file receives its file. */
static void source_path(const void *desc, Buf *b, const PbpyDescFile **file)
{
	const PbpyDesc *d = desc;
	switch (d->kind) {
	case PBPY_ENUM: {
		const PbpyDescEnum *e = desc;
		*file = e->file;
		if (e->parent) {
			source_path(e->parent, b, file);
			path_add(b, DP_ENUM_TYPE);
		} else {
			path_add(b, FDP_ENUM_TYPE);
		}
		path_add(b, e->index);
		return;
	}
	case PBPY_ONEOF: {
		const PbpyDescOneof *o = desc;
		source_path(o->parent, b, file);
		path_add(b, DP_ONEOF_DECL);
		path_add(b, o->index);
		return;
	}
	case PBPY_MESSAGE: {
		const PbpyDescMessage *m = desc;
		*file = m->file;
		if (m->parent) {
			source_path(m->parent, b, file);
			path_add(b, DP_NESTED_TYPE);
		} else {
			path_add(b, FDP_MESSAGE_TYPE);
		}
		path_add(b, m->index);
		return;
	}
	case PBPY_ENUM_VALUE: {
		const PbpyDescEnumValue *v = desc;
		source_path(v->parent, b, file);
		path_add(b, EDP_VALUE);
		path_add(b, v->index);
		return;
	}
	case PBPY_FIELD: {
		const PbpyDescField *f = desc;
		source_path(f->parent, b, file);
		path_add(b, DP_FIELD);
		path_add(b, f->index);
		return;
	}
	case PBPY_EXTENSION: {
		const PbpyDescExtension *x = desc;
		*file = x->file;
		if (x->parent) {
			source_path(x->parent, b, file);
			path_add(b, DP_EXTENSION);
		} else {
			path_add(b, FDP_EXTENSION);
		}
		path_add(b, x->index);
		return;
	}
	case PBPY_SERVICE: {
		const PbpyDescService *s = desc;
		*file = s->file;
		path_add(b, FDP_SERVICE);
		path_add(b, s->index);
		return;
	}
	case PBPY_METHOD: {
		const PbpyDescMethod *m = desc;
		source_path(m->parent, b, file);
		path_add(b, SDP_METHOD);
		path_add(b, m->index);
		return;
	}
	default:
		fail("TypeError: unexpected descriptor type");
	}
}

/* path text -> the first Location with it */
static Map *locations(PbpyDescFile *f)
{
	if (f->loc)
		return f->loc;
	Map *m = xalloc(sizeof *m);
	const PbMsg *sci = pb_msg(f->proto, FDP_SOURCE_CODE_INFO);
	for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
		PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		Buf b = {0};
		for (int k = 0; k < pb_count(loc, LOC_PATH); k++)
			path_add(&b, (int32_t)pb_int_at(loc, LOC_PATH, k));
		const char *key = xstrdup(buf_str(&b));
		if (!map_has(m, key))
			map_put(m, key, loc);
	}
	f->loc = m;
	return m;
}

/* line.removeprefix(" ") for the lines of text.rstrip() */
static void add_lines(Vec *out, Str text)
{
	Str *lines;
	int n = py_splitlines(py_rstrip(text), &lines);
	for (int i = 0; i < n; i++) {
		Str *l = xalloc(sizeof *l);
		*l = lines[i];
		if (l->n && l->p[0] == ' ')
			l->p++, l->n--;
		vec_push(out, l);
	}
}

int pbpy_comment_lines(const void *desc, Str **out)
{
	Buf path = {0};
	const PbpyDescFile *file = NULL;
	source_path(desc, &path, &file);
	const PbMsg *loc = map_get(locations((PbpyDescFile *)file), buf_str(&path));
	Vec lines = {0};
	if (pb_has(loc, LOC_LEADING) && pb_bytes(loc, LOC_LEADING).n)
		add_lines(&lines, pb_bytes(loc, LOC_LEADING));
	if (pb_has(loc, LOC_TRAILING) && pb_bytes(loc, LOC_TRAILING).n) {
		if (lines.n) {
			Str *empty = xalloc(sizeof *empty);
			*empty = (Str){"", 0};
			vec_push(&lines, empty);
		}
		add_lines(&lines, pb_bytes(loc, LOC_TRAILING));
	}
	Str *v = xalloc(sizeof *v * (size_t)(lines.n ? lines.n : 1));
	for (int i = 0; i < lines.n; i++)
		v[i] = *(Str *)lines.v[i];
	*out = v;
	return lines.n;
}
