// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 35.1: what protobuf v35.1 (https://github.com/protocolbuffers/protobuf, tag v35.1)
 * does differently from the 36.2 of this library, for the plugins built with it
 * (grpc/{cpp,csharp,objc,php,python,ruby} v1.85.1). Its own library (cpp_libprotoc_35.1), linked
 * by those generators.
 *
 *   - compiler/code_generator.h: MaximumKnownEdition() (and ProtocMaximumEdition()) is
 *     EDITION_2024, so the feature defaults of BuildFeatureSetDefaults end at 2024;
 *   - the generated pool of protoc_lib: descriptor.proto of v35.1 differs from 36.2's by
 *     FeatureSet.enforce_proto_limits (ProtoLimitsFeature), the edition default STYLE2026 of
 *     enforce_naming_style at EDITION_UNSTABLE (not 2026), no 2026 default of
 *     default_symbol_visibility, no feature_support of cc_enable_arenas and no declaration of
 *     pb.enumvalue.json in the extension range of EnumValueOptions; protoc_lib links no
 *     c_sharp_features.proto, cpp_file_options.proto and json_enumvalue_options.proto
 *     (cpp_features.proto, java_features.proto and plugin.proto are those of 36.2);
 *   - compiler/php/names.cc: kReservedNames has 80 words, without "mixed", "never" and
 *     "object" (LpVersion.php_unreserved_names);
 *   - descriptor.cc: map fields are validated as before 36 (LpVersion.legacy_map_entry). The
 *     other changes of descriptor.cc up to 36.2 are refactorings, edition 2026 (proto limits,
 *     visibility STRICT), "The file is too large." and the checks of pb.enumvalue.json, which
 *     this version does not link; feature_resolver.cc only adds the enforce_proto_limits check. */
#include "libprotoc.h"

/* Field numbers of descriptor.proto */
enum {
	F_FILE_MESSAGE_TYPE = 4,
	F_MSG_FIELD = 2, F_MSG_NESTED_TYPE = 3, F_MSG_EXTENSION_RANGE = 5,
	F_RANGE_OPTIONS = 3,
	F_FIELD_OPTIONS = 8, F_FOPT_EDITION_DEFAULTS = 20, F_FOPT_FEATURE_SUPPORT = 22,
	F_EDITION_DEFAULT_EDITION = 3,
};

static bool v35_named(const PbMsg *e, const void *name) { return str_eq(pb_str(e, 1), name); }

static bool v35_edition_2026(const PbMsg *e, const void *arg)
{
	(void)arg;
	return pb_int(e, F_EDITION_DEFAULT_EDITION, 0) == LP_EDITION_2026;
}

/* The options of the field `field` of message (NULL if none). */
static PbMsg *v35_field_options(const PbMsg *message, const char *field)
{
	return pb_msg(lp_desc_find(message, F_MSG_FIELD, field), F_FIELD_OPTIONS);
}

static void v35_clear(PbMsg *m, uint32_t num)
{
	PbSlot *s = m ? (PbSlot *)pb_slot(m, num) : NULL;
	if (s)
		s->n = 0;
}

static COLD void v35_patch_descriptor_proto(PbMsg *file)
{
	PbMsg *fs = lp_desc_find(file, F_FILE_MESSAGE_TYPE, "FeatureSet");
	lp_desc_remove_if(fs, F_MSG_FIELD, v35_named, "enforce_proto_limits");
	lp_desc_remove_if(fs, F_MSG_NESTED_TYPE, v35_named, "ProtoLimitsFeature");
	PbMsg *style = v35_field_options(fs, "enforce_naming_style");
	for (int i = 0; i < pb_count(style, F_FOPT_EDITION_DEFAULTS); i++) {
		PbMsg *d = pb_msg_at(style, F_FOPT_EDITION_DEFAULTS, i);
		if (v35_edition_2026(d, NULL))
			((PbSlot *)pb_slot(d, F_EDITION_DEFAULT_EDITION))->v[0].u = (uint64_t)LP_EDITION_UNSTABLE;
	}
	lp_desc_remove_if(v35_field_options(fs, "default_symbol_visibility"), F_FOPT_EDITION_DEFAULTS, v35_edition_2026, NULL);

	v35_clear(v35_field_options(lp_desc_find(file, F_FILE_MESSAGE_TYPE, "FileOptions"), "cc_enable_arenas"),
	      F_FOPT_FEATURE_SUPPORT);

	PbMsg *evo = lp_desc_find(file, F_FILE_MESSAGE_TYPE, "EnumValueOptions");
	for (int i = 0; i < pb_count(evo, F_MSG_EXTENSION_RANGE); i++)
		v35_clear(pb_msg_at(evo, F_MSG_EXTENSION_RANGE, i), F_RANGE_OPTIONS);
}

/* LpVersion.builtin_file */
static COLD bool v35_builtin_file(int i, PbMsg *file)
{
	switch (i) {
	case 0: v35_patch_descriptor_proto(file); return true;
	case 3: /* c_sharp_features.proto */
	case 4: /* cpp_file_options.proto */
	case 5: /* json_enumvalue_options.proto */
		return false;
	default: return true;
	}
}

const LpVersion lp_version_35_1 = {
	.name = "35.1",
	.maximum_known_edition = LP_EDITION_2024,
	.builtin_file = v35_builtin_file,
	.legacy_map_entry = true,
	.php_unreserved_names = (const char *const[]){"mixed", "never", "object", NULL},
};
