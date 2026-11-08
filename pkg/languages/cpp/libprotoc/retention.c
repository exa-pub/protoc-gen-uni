// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/retention.cc: StripSourceRetentionOptions(file) without
 * source code info. When the file's pool defines the descriptor proto (the request contains
 * google/protobuf/descriptor.proto, e.g. for custom options), the proto goes through a dynamic
 * message of the pool's definition, so custom options with retention = RETENTION_SOURCE are
 * stripped too and the options known to the pool are re-serialized in field number order. */

enum { RETENTION_SOURCE = 2 };

/* IsOptionsProto */
static bool is_options_proto(const PbMsg *m)
{
	const LpMessage *d = ((const LpMsgDef *)m->def)->desc;
	return str_eq(d->file->name, "google/protobuf/descriptor.proto") && has_suffix(d->name, "Options");
}

/* StripMessage (the stripped paths are only needed for source code info) */
static void lp_strip_message(PbMsg *m, Vec *path, Vec *stripped)
{
	(void)path;
	(void)stripped;
	const LpMsgDef *d = (const LpMsgDef *)m->def;
	for (int i = 0; i < d->pb.n; i++) {
		PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		const LpField *field = d->fields[i];
		if (pb_int(field->options, FOPT_RETENTION, 0) == RETENTION_SOURCE) {
			s->n = 0;
		} else if (field->type == PT_MESSAGE && d->pb.f[i].type == PT_MESSAGE) {
			if (field->label == LABEL_REPEATED) {
				for (int j = 0; j < s->n; j++)
					lp_strip_message(s->v[j].m, NULL, NULL);
			} else {
				PbMsg *child = s->v[0].m;
				bool was_nonempty_options_proto = is_options_proto(child) && !proto_msg_is_empty(child);
				lp_strip_message(child, NULL, NULL);
				if (was_nonempty_options_proto && proto_msg_is_empty(child))
					s->n = 0;
			}
		}
	}
}

/* ConvertToDynamicMessageAndStripOptions(m, pool) */
static void convert_to_dynamic_message_and_strip_options(PbMsg *m, LpPool *pool)
{
	const LpMessage *descriptor = lp_find_message(pool, ((const LpMsgDef *)m->def)->desc->full_name);
	if (!descriptor || !pool->builtin) {
		lp_strip_message(m, NULL, NULL);
		return;
	}
	PbMsg *dynamic_message = pb_new(&lp_msgdef(pool, descriptor)->pb);
	Str serialized = lp_serialize_str(m);
	if (!lp_decode(dynamic_message, (const uint8_t *)serialized.p, serialized.n)) {
		/* "Failed to fully strip source-retention options" */
		lp_strip_message(m, NULL, NULL);
		return;
	}
	lp_strip_message(dynamic_message, NULL, NULL);
	serialized = lp_serialize_str(dynamic_message);
	for (int i = 0; i < m->def->n; i++)
		m->slots[i].n = 0;
	m->unknown.n = 0;
	if (!lp_decode(m, (const uint8_t *)serialized.p, serialized.n))
		lp_strip_message(m, NULL, NULL);
}

PbMsg *lp_strip_source_retention_options(const LpFile *file)
{
	PbMsg *proto = lp_file_copy_to(file);
	convert_to_dynamic_message_and_strip_options(proto, lp_file_pool(file));
	return proto;
}
