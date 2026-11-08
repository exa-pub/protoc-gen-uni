/* Copyright 2024 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 reflect/protodesc/editions.go and
 * internal/filedesc/presence.go; the defaults are internal/editiondefaults/editions_defaults.binpb
 * decoded into a table (tests/unit/go_features_test.c checks the table against the bytes). */
#include "gofeatures.h"

#include "genid.h"

/* FeatureSetDefaults.defaults: fixed_features merged with overridable_features. */
static const struct {
	int edition;
	GoEditionFeatures f;
} features_defaults[] = {
	{GENID_Edition_EDITION_LEGACY, {true, false, false, false, false, false, false, true, 0, 1}},
	{GENID_Edition_EDITION_PROTO3, {false, false, true, true, true, false, true, false, 0, 1}},
	{GENID_Edition_EDITION_2023, {true, false, true, true, true, false, true, false, 0, 1}},
	{GENID_Edition_EDITION_2024, {true, false, true, true, true, false, true, false, 3, 1}},
	{GENID_Edition_EDITION_UNSTABLE, {true, false, true, true, true, false, true, false, 3, 1}},
};

GoEditionFeatures features_for_edition(int edition)
{
	int k = 0;
	for (int i = 0; i < (int)(sizeof features_defaults / sizeof *features_defaults); i++) {
		if (features_defaults[i].edition > edition)
			break;
		k = i;
	}
	return features_defaults[k].f;
}

GoEditionFeatures features_merge(GoEditionFeatures p, const PbMsg *child)
{
	if (!child)
		return p;
	if (pb_has(child, GENID_FeatureSet_FieldPresence_field_number)) {
		int64_t fp = pb_int(child, GENID_FeatureSet_FieldPresence_field_number, 0);
		p.IsFieldPresence = fp == GENID_FeatureSet_LEGACY_REQUIRED_enum_value || fp == GENID_FeatureSet_EXPLICIT_enum_value;
		p.IsLegacyRequired = fp == GENID_FeatureSet_LEGACY_REQUIRED_enum_value;
	}
	if (pb_has(child, GENID_FeatureSet_EnumType_field_number))
		p.IsOpenEnum = pb_int(child, GENID_FeatureSet_EnumType_field_number, 0) == GENID_FeatureSet_OPEN_enum_value;
	if (pb_has(child, GENID_FeatureSet_RepeatedFieldEncoding_field_number))
		p.IsPacked = pb_int(child, GENID_FeatureSet_RepeatedFieldEncoding_field_number, 0) ==
			     GENID_FeatureSet_PACKED_enum_value;
	if (pb_has(child, GENID_FeatureSet_Utf8Validation_field_number))
		p.IsUTF8Validated = pb_int(child, GENID_FeatureSet_Utf8Validation_field_number, 0) ==
				    GENID_FeatureSet_VERIFY_enum_value;
	if (pb_has(child, GENID_FeatureSet_MessageEncoding_field_number))
		p.IsDelimitedEncoded = pb_int(child, GENID_FeatureSet_MessageEncoding_field_number, 0) ==
				       GENID_FeatureSet_DELIMITED_enum_value;
	if (pb_has(child, GENID_FeatureSet_JsonFormat_field_number))
		p.IsJSONCompliant = pb_int(child, GENID_FeatureSet_JsonFormat_field_number, 0) == GENID_FeatureSet_ALLOW_enum_value;

	/* The pb.go extension (a known message field of FeatureSet in the Go schema); absent or
	 * malformed (kept as an unknown field) means no Go features. */
	const PbMsg *gf = pb_msg(child, GENID_FeatureSet_Go_ext_number);
	if (!gf)
		return p;
	if (pb_has(gf, GENID_GoFeatures_LegacyUnmarshalJsonEnum_field_number))
		p.GenerateLegacyUnmarshalJSON = pb_int(gf, GENID_GoFeatures_LegacyUnmarshalJsonEnum_field_number, 0) != 0;
	if (pb_has(gf, GENID_GoFeatures_StripEnumPrefix_field_number))
		p.StripEnumPrefix = (int)pb_int(gf, GENID_GoFeatures_StripEnumPrefix_field_number, 0);
	if (pb_has(gf, GENID_GoFeatures_ApiLevel_field_number))
		p.APILevel = (int)pb_int(gf, GENID_GoFeatures_ApiLevel_field_number, 0);
	return p;
}

bool features_use_presence_for_field(bool in_real_oneof, bool is_map, bool is_message, bool is_lazy, bool has_presence,
				     bool *can_be_lazy)
{
	*can_be_lazy = false;
	if (in_real_oneof || is_map)
		return false; /* oneof fields and maps never use the presence bitmap */
	if (is_message) {
		*can_be_lazy = is_lazy; /* lazy fields always use the presence bitmap */
		return is_lazy;
	}
	return has_presence;
}
