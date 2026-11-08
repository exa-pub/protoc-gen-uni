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

/* Ported from packages/protobuf/src/reflect/nested-types.ts (nestedTypes, parentTypes) and
 * isDeprecated of codegenv2. */
#include "protobuf.h"

static void append_nested(Vec *out, const Desc *d)
{
	if (d->kind == DK_FILE) {
		const DescFile *f = (const DescFile *)d;
		for (int i = 0; i < f->messages.n; i++) {
			vec_push(out, f->messages.v[i]);
			append_nested(out, f->messages.v[i]);
		}
		for (int i = 0; i < f->enums.n; i++)
			vec_push(out, f->enums.v[i]);
		for (int i = 0; i < f->services.n; i++)
			vec_push(out, f->services.v[i]);
		for (int i = 0; i < f->extensions.n; i++)
			vec_push(out, f->extensions.v[i]);
	} else if (d->kind == DK_MESSAGE) {
		const DescMessage *m = (const DescMessage *)d;
		for (int i = 0; i < m->nested_messages.n; i++) {
			vec_push(out, m->nested_messages.v[i]);
			append_nested(out, m->nested_messages.v[i]);
		}
		for (int i = 0; i < m->nested_enums.n; i++)
			vec_push(out, m->nested_enums.v[i]);
		for (int i = 0; i < m->nested_extensions.n; i++)
			vec_push(out, m->nested_extensions.v[i]);
	}
}

/* Messages, enums, services and extensions declared in the file or message, recursively. */
Vec nested_types(const Desc *d)
{
	Vec v = {0};
	append_nested(&v, d);
	return v;
}

/* The parent of a descriptor, NULL for a file. */
Desc *desc_parent(const Desc *d)
{
	switch (d->kind) {
	case DK_ENUM_VALUE: return &((const DescEnumValue *)d)->parent->h;
	case DK_FIELD: {
		const DescField *f = (const DescField *)d;
		if (f->is_extension)
			return f->parent ? &f->parent->h : &f->file->h;
		return &f->parent->h;
	}
	case DK_ONEOF: return &((const DescOneof *)d)->parent->h;
	case DK_METHOD: return &((const DescMethod *)d)->parent->h;
	case DK_SERVICE: return &((const DescService *)d)->file->h;
	case DK_ENUM: {
		const DescEnum *e = (const DescEnum *)d;
		return e->parent ? &e->parent->h : &e->file->h;
	}
	case DK_MESSAGE: {
		const DescMessage *m = (const DescMessage *)d;
		return m->parent ? &m->parent->h : &m->file->h;
	}
	}
	return NULL;
}

/* The descriptor or one of its parents is deprecated (oneofs never are). */
bool desc_deprecated_with_parents(const Desc *d)
{
	for (; d; d = desc_parent(d))
		if (d->kind != DK_ONEOF && d->deprecated)
			return true;
	return false;
}
