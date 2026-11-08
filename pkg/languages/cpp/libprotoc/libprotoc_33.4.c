// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 33.4: what protobuf v33.4 (https://github.com/protocolbuffers/protobuf, tag v33.4)
 * does differently from the 36.2 of this library, for the plugins built with it
 * (protocolbuffers/js v4.0.3). Its own library (cpp_libprotoc_33.4), linked by those generators.
 *
 *   - compiler/code_generator.h: MaximumKnownEdition() is EDITION_2024, so the feature defaults
 *     of BuildFeatureSetDefaults end at 2024 and later editions fail to build;
 *   - the generated pool of protoc_lib: descriptor.proto of v33.4 differs from 36.2's by
 *     EDITION_2026, FeatureSet.enforce_proto_limits (ProtoLimitsFeature), STYLE2026, the 2026
 *     edition defaults of enforce_naming_style and default_symbol_visibility, the pb.csharp
 *     declaration of FeatureSet, FeatureSupport.removal_error, the feature_support of
 *     java_multiple_files and cc_enable_arenas, and the `extensions 990 to 998` ranges of the
 *     options messages; it links no c_sharp_features.proto, cpp_file_options.proto and
 *     json_enumvalue_options.proto. builtin_file edits the 36.2 files into these (the feature
 *     protos of cpp and java are kept as in 36.2: the v33.4 plugins read no language features);
 *   - descriptor.cc: `java_multiple_files` in edition 2024 is an error of ValidateOptions, and
 *     only the features take part in the feature lifetime validation, with the texts of
 *     feature_resolver.cc 33.4 (LpVersion.java_multiple_files_error, legacy_feature_lifetimes).
 *     The other changes of descriptor.cc up to 36.2 only concern descriptors that protoc and buf
 *     reject (texts of some errors) or edition 2026. */
#include "libprotoc.h"

#include <string.h>

/* Field numbers of descriptor.proto */
enum {
	F_FILE_MESSAGE_TYPE = 4, F_FILE_ENUM_TYPE = 5,
	F_MSG_NAME = 1, F_MSG_FIELD = 2, F_MSG_NESTED_TYPE = 3, F_MSG_ENUM_TYPE = 4, F_MSG_EXTENSION_RANGE = 5,
	F_RANGE_START = 1, F_RANGE_OPTIONS = 3, F_ERO_DECLARATION = 2, F_DECL_NUMBER = 1,
	F_ENUM_NAME = 1, F_ENUM_VALUE = 2, F_VALUE_NAME = 1,
	F_FIELD_NAME = 1, F_FIELD_OPTIONS = 8, F_FOPT_EDITION_DEFAULTS = 20, F_FOPT_FEATURE_SUPPORT = 22,
	F_EDITION_DEFAULT_EDITION = 3,
};

/* Removes the elements of a repeated message field for which drop returns true. */
static void remove_where(PbMsg *m, uint32_t num, bool (*drop)(const PbMsg *e, const void *arg), const void *arg)
{
	PbSlot *s = (PbSlot *)pb_slot(m, num);
	if (!s)
		return;
	int k = 0;
	for (int i = 0; i < s->n; i++)
		if (!drop(s->v[i].m, arg))
			s->v[k++] = s->v[i];
	s->n = k;
}

static bool named(const PbMsg *e, const void *name) { return strcmp(pb_str(e, 1), name) == 0; }

static bool edition_2026(const PbMsg *e, const void *arg)
{
	(void)arg;
	return pb_int(e, F_EDITION_DEFAULT_EDITION, 0) == LP_EDITION_2026;
}

static bool reserved_range(const PbMsg *e, const void *arg)
{
	(void)arg;
	return pb_int(e, F_RANGE_START, 0) == 990;
}

static bool csharp_declaration(const PbMsg *e, const void *arg)
{
	(void)arg;
	return pb_int(e, F_DECL_NUMBER, 0) == 1004;
}

/* The element of a repeated message field whose name (field 1) is name, or NULL. */
static PbMsg *find(const PbMsg *m, uint32_t num, const char *name)
{
	for (int i = 0; i < pb_count(m, num); i++)
		if (strcmp(pb_str(pb_msg_at(m, num, i), 1), name) == 0)
			return pb_msg_at(m, num, i);
	return NULL;
}

static void drop_field_option(PbMsg *message, const char *field, uint32_t option)
{
	PbMsg *f = find(message, F_MSG_FIELD, field);
	if (f && pb_msg(f, F_FIELD_OPTIONS))
		((PbSlot *)pb_slot(pb_msg(f, F_FIELD_OPTIONS), option))->n = 0;
}

static void drop_edition_2026_default(PbMsg *message, const char *field)
{
	PbMsg *f = find(message, F_MSG_FIELD, field);
	if (f && pb_msg(f, F_FIELD_OPTIONS))
		remove_where(pb_msg(f, F_FIELD_OPTIONS), F_FOPT_EDITION_DEFAULTS, edition_2026, NULL);
}

static COLD void patch_descriptor_proto(PbMsg *file)
{
	PbMsg *edition = find(file, F_FILE_ENUM_TYPE, "Edition");
	if (edition)
		remove_where(edition, F_ENUM_VALUE, named, "EDITION_2026");
	for (int i = 0; i < pb_count(file, F_FILE_MESSAGE_TYPE); i++)
		remove_where(pb_msg_at(file, F_FILE_MESSAGE_TYPE, i), F_MSG_EXTENSION_RANGE, reserved_range, NULL);

	PbMsg *fs = find(file, F_FILE_MESSAGE_TYPE, "FeatureSet");
	if (fs) {
		remove_where(fs, F_MSG_FIELD, named, "enforce_proto_limits");
		remove_where(fs, F_MSG_NESTED_TYPE, named, "ProtoLimitsFeature");
		PbMsg *style = find(fs, F_MSG_ENUM_TYPE, "EnforceNamingStyle");
		if (style)
			remove_where(style, F_ENUM_VALUE, named, "STYLE2026");
		drop_edition_2026_default(fs, "enforce_naming_style");
		PbMsg *vis = find(fs, F_MSG_NESTED_TYPE, "VisibilityFeature");
		drop_edition_2026_default(vis, "default_symbol_visibility");
		drop_edition_2026_default(fs, "default_symbol_visibility");
		for (int i = 0; i < pb_count(fs, F_MSG_EXTENSION_RANGE); i++) {
			PbMsg *opts = pb_msg(pb_msg_at(fs, F_MSG_EXTENSION_RANGE, i), F_RANGE_OPTIONS);
			if (opts)
				remove_where(opts, F_ERO_DECLARATION, csharp_declaration, NULL);
		}
	}

	PbMsg *fo = find(file, F_FILE_MESSAGE_TYPE, "FieldOptions");
	PbMsg *support = fo ? find(fo, F_MSG_NESTED_TYPE, "FeatureSupport") : NULL;
	if (support)
		remove_where(support, F_MSG_FIELD, named, "removal_error");

	PbMsg *fileopts = find(file, F_FILE_MESSAGE_TYPE, "FileOptions");
	if (fileopts) {
		drop_field_option(fileopts, "java_multiple_files", F_FOPT_FEATURE_SUPPORT);
		drop_field_option(fileopts, "cc_enable_arenas", F_FOPT_FEATURE_SUPPORT);
	}
}

/* LpVersion.builtin_file */
static COLD bool builtin_file(int i, PbMsg *file)
{
	switch (i) {
	case 0: patch_descriptor_proto(file); return true;
	case 3: /* c_sharp_features.proto */
	case 4: /* cpp_file_options.proto */
	case 5: /* json_enumvalue_options.proto */
		return false;
	default: return true;
	}
}

const LpVersion lp_version_33_4 = {
	.name = "33.4",
	.maximum_known_edition = LP_EDITION_2024,
	.builtin_file = builtin_file,
	.java_multiple_files_error = true,
	.legacy_feature_lifetimes = true,
};
