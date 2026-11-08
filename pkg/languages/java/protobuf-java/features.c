/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* Feature resolution of protobuf-java 4.36.2 (java/core/src/main/java/com/google/protobuf/
 * Descriptors.java: getEditionDefaults, GenericDescriptor.resolveFeatures, the
 * inferLegacyProtoFeatures and validateFeatures of the descriptors, FieldDescriptor.getType; the
 * defaults are JavaEditionDefaults.java). protobuf-java 3.25.8 has no features: only the plugins on
 * 4.x call jfile_resolve_features, after jfile_build. */
#include "protobuf_java.h"

#include <string.h>

NORETURN COLD static void jvalidation_error(const char *symbol, const char *desc)
{
	fail("com.google.protobuf.Descriptors$DescriptorValidationException: %s: %s", symbol, desc);
}

int jfile_edition(const JFile *f)
{
	const char *syntax = jstr(f->proto, FDP_SYNTAX);
	if (str_eq(syntax, "editions"))
		return (int)pb_int(f->proto, FDP_EDITION, JED_UNKNOWN);
	return str_eq(syntax, "proto3") ? JED_PROTO3 : JED_PROTO2;
}

const char *jedition_name(int e)
{
	switch (e) {
	case 0: return "EDITION_UNKNOWN";
	case 900: return "EDITION_LEGACY";
	case 998: return "EDITION_PROTO2";
	case 999: return "EDITION_PROTO3";
	case 1000: return "EDITION_2023";
	case 1001: return "EDITION_2024";
	case 1002: return "EDITION_2026";
	case 9999: return "EDITION_UNSTABLE";
	case 1: return "EDITION_1_TEST_ONLY";
	case 2: return "EDITION_2_TEST_ONLY";
	case 99997: return "EDITION_99997_TEST_ONLY";
	case 99998: return "EDITION_99998_TEST_ONLY";
	case 99999: return "EDITION_99999_TEST_ONLY";
	default: return "EDITION_MAX";
	}
}

/* JavaEditionDefaults.PROTOBUF_INTERNAL_JAVA_EDITION_DEFAULTS: fixed features merged with the
 * overridable ones, per edition (minimum EDITION_PROTO2, maximum EDITION_2026). */
static const struct {
	int edition;
	JFeatures f;
} jedition_defaults[] = {
	{JED_LEGACY, {{1, 2, 2, 3, 1, 2, 2, 1, 1, 1, 1, 0, 1, 3}}},
	{JED_PROTO3, {{2, 1, 1, 2, 1, 1, 2, 1, 1, 0, 1, 0, 1, 3}}},
	{JED_2023, {{1, 1, 1, 2, 1, 1, 2, 1, 1, 0, 1, 0, 1, 3}}},
	{JED_2024, {{1, 1, 1, 2, 1, 1, 1, 2, 1, 0, 1, 0, 0, 1}}},
	{JED_2026, {{1, 1, 1, 2, 1, 1, 3, 4, 2, 0, 1, 0, 0, 1}}},
};

/* Descriptors.getEditionDefaults */
static JFeatures jget_edition_defaults(int e)
{
	if (e < JED_PROTO2)
		java_exception("java.lang.IllegalArgumentException",
			       xfmt("Edition %s is lower than the minimum supported edition EDITION_PROTO2!", jedition_name(e)));
	if (e > JED_2026 && e != JED_UNSTABLE)
		java_exception("java.lang.IllegalArgumentException",
			       xfmt("Edition %s is greater than the maximum supported edition EDITION_2026!", jedition_name(e)));
	int found = 0;
	for (int i = 0; i < (int)(sizeof jedition_defaults / sizeof *jedition_defaults); i++)
		if (jedition_defaults[i].edition <= e)
			found = i;
	return jedition_defaults[found].f;
}

/* Merges the JavaFeatures message bytes p[0:n] (FeatureSet.parseFrom with the java_ registry);
 * false on InvalidProtocolBufferException. Unknown values of its closed enums stay unknown. */
static bool jmerge_java_features(JFeatures *out, const uint8_t *p, size_t n)
{
	PbReader r = {p, p + n};
	PbRawField f;
	while (r.p < r.end) {
		if (!pb_next_field(&r, &f))
			return false;
		if (f.wt != WT_VARINT || f.num < 1 || f.num > 5)
			continue;
		if (f.num == 1 || f.num == 3 || f.num == 4)
			out->v[JF_JAVA_LEGACY_CLOSED_ENUM + (int)f.num - 1] = f.v != 0;
		else if (f.num == 2 && f.v <= 2)
			out->v[JF_JAVA_UTF8_VALIDATION] = (int32_t)f.v;
		else if (f.num == 5 && f.v <= 3)
			out->v[JF_JAVA_NEST_IN_FILE_CLASS] = (int32_t)f.v;
	}
	return true;
}

/* features.mergeFrom(unresolvedFeatures) of a FeatureSet message (may be NULL); false when the java
 * extension in its unknown fields does not parse. */
static bool jmerge_features(JFeatures *out, const PbMsg *fs)
{
	if (!fs)
		return true;
	for (uint32_t num = 1; num <= 9; num++)
		if (pb_has(fs, num))
			out->v[num - 1] = (int32_t)pb_int(fs, num, 0);
	PbReader r = pb_unknown_fields(fs);
	PbRawField f;
	while (r.p < r.end && pb_next_field(&r, &f))
		if (f.num == 1001 && f.wt == WT_LEN && !jmerge_java_features(out, f.val, f.vlen))
			return false;
	return true;
}

static JFeatures *jresolve(const char *full_name, const JFeatures *parent, int edition, const PbMsg *unresolved,
			   const JFeatures *inferred)
{
	JFeatures *f = xalloc(sizeof *f);
	*f = parent ? *parent : jget_edition_defaults(edition);
	if (inferred)
		for (int i = 0; i < JF_N; i++)
			if (inferred->v[i] >= 0)
				f->v[i] = inferred->v[i];
	if (!jmerge_features(f, unresolved))
		jvalidation_error(full_name, "Failed to parse features with Java feature extension registry.");
	return f;
}

static JFeatures jno_features(void)
{
	JFeatures f;
	for (int i = 0; i < JF_N; i++)
		f.v[i] = -1;
	return f;
}

static PbMsg *jfeatures_of(const PbMsg *proto, uint32_t options_num, uint32_t features_num)
{
	return pb_msg(pb_msg(proto, options_num), features_num);
}

int jfield_type_v4(const JField *f)
{
	if (f->type == PT_MESSAGE && !(f->msg_type && jmsg_is_map_entry(f->msg_type)) &&
	    !(f->containing_type && jmsg_is_map_entry(f->containing_type)) && f->features &&
	    ((const JFeatures *)f->features)->v[JF_MESSAGE_ENCODING] == 2 /* DELIMITED */)
		return PT_GROUP;
	return f->type;
}

static void jfield_resolve(JField *f, const JFeatures *parent, int edition)
{
	/* FieldDescriptor.inferLegacyProtoFeatures */
	JFeatures inferred = jno_features();
	if (edition < JED_2023) {
		if (f->label == LABEL_REQUIRED)
			inferred.v[JF_FIELD_PRESENCE] = 3; /* LEGACY_REQUIRED */
		if ((int)pb_int(f->proto, FD_TYPE, 0) == PT_GROUP)
			inferred.v[JF_MESSAGE_ENCODING] = 2; /* DELIMITED */
		PbMsg *o = jfield_options(f);
		if (edition == JED_PROTO2 && jpb_bool(o, FO_PACKED, false))
			inferred.v[JF_REPEATED_FIELD_ENCODING] = 1; /* PACKED */
		if (edition == JED_PROTO3 && pb_has(o, FO_PACKED) && !jpb_bool(o, FO_PACKED, false))
			inferred.v[JF_REPEATED_FIELD_ENCODING] = 2; /* EXPANDED */
	}
	f->features = jresolve(f->full_name, parent, edition, jfeatures_of(f->proto, FD_OPTIONS, 21), &inferred);
	/* validateFeatures */
	if (f->is_ext && f->containing_type && jpb_bool(pb_msg(f->containing_type->proto, DP_OPTIONS), 1, false)) {
		bool required = ((JFeatures *)f->features)->v[JF_FIELD_PRESENCE] == 3;
		if (required || jfield_is_repeated(f) || jfield_type_v4(f) != PT_MESSAGE)
			jvalidation_error(f->full_name, "Extensions of MessageSets may not be required or repeated messages.");
	}
}

static void jenum_resolve(JEnum *e, const JFeatures *parent, int edition)
{
	JFeatures *f = jresolve(e->full_name, parent, edition, jfeatures_of(e->proto, EDP_OPTIONS, 7), NULL);
	for (int i = 0; i < e->values.n; i++) {
		JEnumValue *v = VEC_AT(e->values, JEnumValue, i);
		jresolve(v->full_name, f, edition, jfeatures_of(v->proto, EVDP_OPTIONS, 2), NULL);
	}
}

static void jmsg_resolve(JMsg *m, const JFeatures *parent, int edition)
{
	JFeatures *f = jresolve(m->full_name, parent, edition, jfeatures_of(m->proto, DP_OPTIONS, 12), NULL);
	m->features = f;
	for (int i = 0; i < m->nested.n; i++)
		jmsg_resolve(VEC_AT(m->nested, JMsg, i), f, edition);
	for (int i = 0; i < m->enums.n; i++)
		jenum_resolve(VEC_AT(m->enums, JEnum, i), f, edition);
	JFeatures **oneofs = xalloc(sizeof(JFeatures *) * (size_t)(m->oneofs.n + 1));
	for (int i = 0; i < m->oneofs.n; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		oneofs[i] = jresolve(o->full_name, f, edition, jfeatures_of(o->proto, 2, 1), NULL);
	}
	for (int i = 0; i < m->fields.n; i++) {
		JField *fd = VEC_AT(m->fields, JField, i);
		jfield_resolve(fd, fd->oneof ? oneofs[fd->oneof->index] : f, edition);
	}
	for (int i = 0; i < m->exts.n; i++)
		jfield_resolve(VEC_AT(m->exts, JField, i), f, edition);
}

void jfile_resolve_features(JFile *file)
{
	int edition = jfile_edition(file);
	/* FileDescriptor.inferLegacyProtoFeatures: java_string_check_utf8 in proto2 */
	JFeatures inferred = jno_features();
	if (edition == JED_PROTO2 && jpb_bool(pb_msg(file->proto, FDP_OPTIONS), 27, false))
		inferred.v[JF_JAVA_UTF8_VALIDATION] = 2; /* VERIFY */
	JFeatures *f = jresolve(file->name, NULL, edition, jfeatures_of(file->proto, FDP_OPTIONS, 50), &inferred);
	file->features = f;
	for (int i = 0; i < file->msgs.n; i++)
		jmsg_resolve(VEC_AT(file->msgs, JMsg, i), f, edition);
	for (int i = 0; i < file->enums.n; i++)
		jenum_resolve(VEC_AT(file->enums, JEnum, i), f, edition);
	for (int i = 0; i < file->services.n; i++) {
		JService *s = VEC_AT(file->services, JService, i);
		JFeatures *sf = jresolve(s->full_name, f, edition, jfeatures_of(s->proto, 3, 34), NULL);
		for (int j = 0; j < s->methods.n; j++) {
			JMethod *m = VEC_AT(s->methods, JMethod, j);
			jresolve(m->full_name, sf, edition, jfeatures_of(m->proto, MDP_OPTIONS, 35), NULL);
		}
	}
	for (int i = 0; i < file->exts.n; i++)
		jfield_resolve(VEC_AT(file->exts, JField, i), f, edition);
}
