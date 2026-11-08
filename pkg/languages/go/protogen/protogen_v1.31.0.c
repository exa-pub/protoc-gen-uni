/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* compiler/protogen of google.golang.org/protobuf v1.31.0 where it differs from v1.36.12
 * (protogen.c, generated_file.c): the v1.31.0 behaviour of reflect/protodesc (only proto2 and
 * proto3, its validation and error texts) behind pg_v1_31_0 (PgOptions.Version), and the
 * internal/filedesc methods whose v1.31.0 semantics differ from pkg/languages/go/protodesc. */
#include "internal.h"
#include "protogen_v1.31.0.h"

#include <string.h>

/* ---- internal/filedesc v1.31.0 ---- */

COLD bool pg_v131_field_has_optional_keyword(const GoField *f)
{
	return (f->d.file->syntax == GO_PROTO2 && f->cardinality == GO_OPTIONAL && !f->oneof) || f->is_proto3_optional;
}

COLD bool pg_v131_field_has_presence(const GoField *f)
{
	return f->cardinality != GO_REPEATED && (f->d.file->syntax == GO_PROTO2 || f->message_type || f->oneof);
}

COLD bool pg_v131_field_is_packed(const GoField *f)
{
	const PbMsg *o = f->d.options;
	bool has_packed = o && pb_has(o, GENID_FieldOptions_Packed_field_number);
	if (!has_packed && f->d.file->syntax != GO_PROTO2 && f->cardinality == GO_REPEATED) {
		switch (f->kind) {
		case PT_STRING:
		case PT_BYTES:
		case PT_MESSAGE:
		case PT_GROUP:
			break;
		default:
			return true;
		}
	}
	return has_packed && pb_int(o, GENID_FieldOptions_Packed_field_number, 0) != 0;
}

COLD bool pg_v131_oneof_is_synthetic(const GoOneof *o)
{
	return o->d.file->syntax == GO_PROTO3 && o->n_fields == 1 && pg_v131_field_has_optional_keyword(o->fields[0]);
}

COLD int pg_v131_message_required_numbers(const GoMessage *m, const int32_t **out)
{
	int32_t *nums = xalloc(sizeof *nums * (size_t)(m->n_fields ? m->n_fields : 1));
	int n = 0;
	for (int i = 0; i < m->n_fields; i++)
		if (m->fields[i]->cardinality == GO_REQUIRED)
			nums[n++] = m->fields[i]->number;
	*out = nums;
	return n;
}

/* ---- reflect/protodesc v1.31.0 ---- */

#define FIELD_OPTIONS_WEAK 10 /* FieldOptions.weak */

/* The parts of protodesc.NewFile v1.31.0 that pkg/languages/go/protodesc (v1.36.12) does not
 * check or reports with other texts. Only proto2 and proto3 exist in v1.31.0 (edition-based
 * checks reduce to syntax checks there). */

static COLD const char *v131_check_syntax(PbMsg *p)
{
	Str syn = pb_bytes(p, GENID_FileDescriptorProto_Syntax_field_number);
	if (!syn.n || str_eqc(syn, "proto2") || str_eqc(syn, "proto3"))
		return NULL;
	return xcat("invalid FileDescriptorProto ", quote_go(pb_bytes(p, GENID_FileDescriptorProto_Name_field_number)), ": ",
		    go_err("invalid syntax: %s", quote_go(syn)));
}

static COLD bool v131_is_weak(const GoField *f)
{
	return f->d.options && pb_int(f->d.options, FIELD_OPTIONS_WEAK, 0) != 0;
}

static COLD const char *v131_validate_extensions(GoField *const *xs, int n)
{
	for (int i = 0; i < n; i++)
		if (v131_is_weak(xs[i]))
			return go_err("extension field %s cannot be a weak reference", go_quote(xs[i]->d.full_name));
	return NULL;
}

static COLD const char *v131_validate_message(const GoMessage *m)
{
	if (m->d.file->syntax == GO_PROTO3) {
		/* Verify that field names in proto3 do not conflict if lowercased with all
		 * underscores removed. */
		Map names = {0};
		for (int i = 0; i < m->n_fields; i++) {
			const GoField *f1 = m->fields[i];
			Buf b = {0};
			for (const char *c = f1->d.name; *c; c++)
				if (*c != '_')
					buf_addc(&b, *c >= 'A' && *c <= 'Z' ? (char)(*c + 32) : *c);
			const char *key = buf_str(&b);
			const GoField *f2 = map_get(&names, key);
			if (f2)
				return go_err("message %s using proto3 semantics has conflict: %s with %s", go_quote(m->d.full_name),
					      go_quote(f1->d.name), go_quote(f2->d.name));
			map_put(&names, key, (void *)f1);
		}
	}
	for (int i = 0; i < m->n_fields; i++)
		if (v131_is_weak(m->fields[i]))
			return go_err("message field %s is a weak field, which is a legacy proto1 feature that is no longer supported",
				      go_quote(m->fields[i]->d.full_name));
	const char *err;
	for (int i = 0; i < m->n_messages; i++)
		if ((err = v131_validate_message(m->messages[i])))
			return err;
	return v131_validate_extensions(m->extensions, m->n_extensions);
}

static COLD const char *v131_validate_file(const GoFile *fd)
{
	PbMsg *p = fd->proto;
	const char *err = NULL;
	int nd = pb_count(p, GENID_FileDescriptorProto_Dependency_field_number);
	bool *weak = xalloc((size_t)nd + 1);
	for (int k = 0; k < pb_count(p, GENID_FileDescriptorProto_WeakDependency_field_number) && !err; k++) {
		int64_t i = pb_int_at(p, GENID_FileDescriptorProto_WeakDependency_field_number, k);
		if (!(0 <= i && i < nd) || weak[i])
			err = go_err("invalid or duplicate weak import index: %d", (int)i);
		else
			weak[i] = true;
	}
	for (int i = 0; i < fd->n_messages && !err; i++)
		err = v131_validate_message(fd->messages[i]);
	if (!err)
		err = v131_validate_extensions(fd->extensions, fd->n_extensions);
	if (!err)
		return NULL;
	return xcat("invalid FileDescriptorProto ", quote_go(pb_bytes(p, GENID_FileDescriptorProto_Name_field_number)), ": ", err);
}

/* The v1.36.12 texts of checks that v1.31.0 words differently (for proto2/proto3, "open" and
 * "closed" enums are proto3 and proto2 enums). */
static COLD const char *v131_error_text(const char *err)
{
	static const char *const fix[][2] = {
		{" using open semantics must have zero number", " using proto3 semantics must have zero number"},
		{" using open semantics has conflict: ", " using proto3 semantics has conflict: "},
		{" using proto3 semantics may only depend on open enums", " using proto3 semantics may only depend on a proto3 enum"},
		{"invalid under proto3 semantics", "invalid under proto2 semantics"},
	};
	for (size_t i = 0; i < sizeof fix / sizeof *fix; i++)
		if (strstr(err, fix[i][0]))
			err = str_replace_all(err, fix[i][0], fix[i][1]);
	return err;
}

const PgVersion pg_v1_31_0 = {v131_check_syntax, v131_error_text, v131_validate_file, false};
