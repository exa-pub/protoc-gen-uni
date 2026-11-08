// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Editions feature resolution of src/google/protobuf/feature_resolver.cc and descriptor.cc
 * (protobuf v36.2): internal::GetEditionFeatureSetDefaults, FeatureResolver::MergeFeatures,
 * InferLegacyProtoFeatures for fields and the parent a field inherits from. The edition defaults
 * are data: a FeatureSetDefaults message the library compiles (protoc's CompileDefaults) or parses
 * from the bytes its upstream embeds. Validation of the defaults and of merged features, and their
 * error texts, stay in the library. */
#include "proto.h"

int proto_feature(const PbMsg *features, uint32_t number) { return (int)pb_int(features, number, 0); }

PbMsg *proto_edition_default_features(const PbMsg *entry, const PbMsgDef *feature_set_def)
{
	PbMsg *features = pb_has(entry, PROTO_FSED_FIXED) ? proto_msg_clone(pb_msg(entry, PROTO_FSED_FIXED))
							  : pb_new(feature_set_def);
	proto_msg_merge(features, pb_msg(entry, PROTO_FSED_OVERRIDABLE));
	return features;
}

const PbMsg *proto_edition_defaults_entry(const PbMsg *defaults, int edition)
{
	const PbMsg *match = NULL;
	for (int i = 0; i < pb_count(defaults, PROTO_FSD_DEFAULTS); i++) {
		const PbMsg *d = pb_msg_at(defaults, PROTO_FSD_DEFAULTS, i);
		if (pb_int(d, PROTO_FSED_EDITION, 0) > edition)
			break;
		match = d;
	}
	return match;
}

PbMsg *proto_edition_features(const PbMsg *defaults, int edition, const PbMsgDef *feature_set_def)
{
	const PbMsg *entry = proto_edition_defaults_entry(defaults, edition);
	return entry ? proto_edition_default_features(entry, feature_set_def) : NULL;
}

PbMsg *proto_merge_features(const PbMsg *edition_features, const PbMsg *parent, const PbMsg *child)
{
	PbMsg *merged = proto_msg_clone(edition_features);
	proto_msg_merge(merged, parent);
	proto_msg_merge(merged, child);
	return merged;
}

void proto_infer_legacy_field_features(PbMsg *features, const PbMsg *field_proto, const PbMsg *options, int edition)
{
	if (edition >= PROTO_EDITION_2023)
		return;
	if (pb_int(field_proto, FD_LABEL, LABEL_OPTIONAL) == LABEL_REQUIRED)
		proto_msg_set_u(features, PROTO_FEATURE_FIELD_PRESENCE, PROTO_PRESENCE_LEGACY_REQUIRED);
	if (pb_int(field_proto, FD_TYPE, PT_DOUBLE) == PT_GROUP)
		proto_msg_set_u(features, PROTO_FEATURE_MESSAGE_ENCODING, PROTO_MESSAGE_DELIMITED);
	if (pb_int(options, PROTO_FOPT_PACKED, 0))
		proto_msg_set_u(features, PROTO_FEATURE_REPEATED_FIELD_ENCODING, PROTO_REPEATED_PACKED);
	if (edition == PROTO_EDITION_PROTO3 && pb_has(options, PROTO_FOPT_PACKED) && !pb_int(options, PROTO_FOPT_PACKED, 0))
		proto_msg_set_u(features, PROTO_FEATURE_REPEATED_FIELD_ENCODING, PROTO_REPEATED_EXPANDED);
}

const PbMsg *proto_field_parent_features(const ProtoField *f)
{
	if (f->containing_oneof)
		return f->containing_oneof->merged_features;
	if (f->is_extension)
		return f->extension_scope ? f->extension_scope->merged_features : f->file->merged_features;
	return f->containing_type->merged_features;
}

/* ---- FeatureSetDefaults data ---- */

typedef struct {
	PbMsgDef entry, defaults;
	PbField entry_fields[3], defaults_fields[3];
} DefaultsDefs;

/* Allocated per call, not cached: the arena may be reset between requests (fuzzer). */
const PbMsgDef *proto_feature_set_defaults_def(const PbMsgDef *feature_set_def)
{
	DefaultsDefs *d = xalloc(sizeof *d);
	d->entry_fields[0] = (PbField){PROTO_FSED_EDITION, PT_ENUM, PL_OPT, NULL, NULL, 0};
	d->entry_fields[1] = (PbField){PROTO_FSED_OVERRIDABLE, PT_MESSAGE, PL_OPT, feature_set_def, NULL, 0};
	d->entry_fields[2] = (PbField){PROTO_FSED_FIXED, PT_MESSAGE, PL_OPT, feature_set_def, NULL, 0};
	d->entry = (PbMsgDef){3, d->entry_fields};
	d->defaults_fields[0] = (PbField){PROTO_FSD_DEFAULTS, PT_MESSAGE, PL_REP, &d->entry, NULL, 0};
	d->defaults_fields[1] = (PbField){PROTO_FSD_MINIMUM_EDITION, PT_ENUM, PL_OPT, NULL, NULL, 0};
	d->defaults_fields[2] = (PbField){PROTO_FSD_MAXIMUM_EDITION, PT_ENUM, PL_OPT, NULL, NULL, 0};
	d->defaults = (PbMsgDef){3, d->defaults_fields};
	return &d->defaults;
}

PbMsg *proto_parse_feature_set_defaults(Str data, const PbMsgDef *feature_set_def)
{
	PbMsg *m = pb_new(proto_feature_set_defaults_def(feature_set_def));
	return pb_decode(m, (const uint8_t *)data.p, data.n) ? m : NULL;
}
