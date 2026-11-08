// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 29.1: what protobuf v29.1 (https://github.com/protocolbuffers/protobuf, tag v29.1)
 * does differently from the 33.4 of libprotoc_33.4.c (its base), for the plugins built with it
 * (grpc/web v2.1.1). Its own library (cpp_libprotoc_29.1), linked by those generators together
 * with cpp_libprotoc_33.4.
 *
 *   - compiler/code_generator.cc: BuildFeatureSetDefaults compiles the defaults for the
 *     generator's own edition range (GetMinimumEdition()..GetMaximumEdition()), or for
 *     MinimumAllowedEdition()..MaximumAllowedEdition() (PROTO2..2023) for a generator without
 *     FEATURE_SUPPORTS_EDITIONS (LpVersion.generator_edition_defaults); MaximumKnownEdition()
 *     does not exist;
 *   - the generated pool of protoc_lib: descriptor.proto of v29.1 differs from 33.4's by
 *     EDITION_UNSTABLE, FileDescriptorProto.option_dependency, the visibility of
 *     DescriptorProto and EnumDescriptorProto, and the features enforce_naming_style and
 *     default_symbol_visibility, which builtin_file removes (a request that sets them keeps them
 *     as unknown fields; an edition 9999 is an unknown value of the closed enum). What else
 *     differs cannot be observed through the pool: the reserved `stream` of
 *     MethodDescriptorProto, the declarations of FeatureSet's extension range, the deprecation of
 *     FieldOptions.weak and the types left unused by the removed fields. cpp_features.proto has
 *     no CppFeatures.repeated_type (nor in 33.4), which builtin_file removes too; the retention
 *     of pb.cpp.legacy_closed_enum (source) is not ported (the 29.1 plugins strip no options);
 *   - descriptor.cc: no error for `java_multiple_files` in edition 2024 (it has no
 *     feature_support), feature lifetimes as in 33.4 (LpVersion.legacy_feature_lifetimes). The
 *     other changes of descriptor.cc up to 33.4 concern descriptors that protoc and buf reject
 *     ("option imports are not supported before edition 2024", weak imports in edition 2024,
 *     "Next available field number is ..." after a duplicate field number, "Name too long.",
 *     more fields than kMaxFieldsPerMessage, ctype together with features.(pb.cpp).string_type)
 *     or visibility and naming style, which a plugin's pool does not check;
 *   - plugin.cc, feature_resolver.cc and the legacy Print of io/printer.cc are those of 33.4. */
#include "libprotoc.h"

/* Field numbers of descriptor.proto (prefixed: a test includes the file of the base too) */
enum { V29_FILE_MESSAGE_TYPE = 4, V29_FILE_ENUM_TYPE = 5, V29_MSG_FIELD = 2, V29_ENUM_VALUE = 2 };

static bool v29_named(const PbMsg *e, const void *name) { return str_eq(pb_str(e, 1), name); }

/* Removes the field named field of the message named message of file. */
static COLD void v29_remove_field(PbMsg *file, const char *message, const char *field)
{
	lp_desc_remove_if(lp_desc_find(file, V29_FILE_MESSAGE_TYPE, message), V29_MSG_FIELD, v29_named, field);
}

/* LpVersion.builtin_file */
static COLD bool v29_builtin_file(int i, PbMsg *file)
{
	if (i == 0) {
		lp_desc_remove_if(lp_desc_find(file, V29_FILE_ENUM_TYPE, "Edition"), V29_ENUM_VALUE, v29_named,
				  "EDITION_UNSTABLE");
		v29_remove_field(file, "FileDescriptorProto", "option_dependency");
		v29_remove_field(file, "DescriptorProto", "visibility");
		v29_remove_field(file, "EnumDescriptorProto", "visibility");
		v29_remove_field(file, "FeatureSet", "enforce_naming_style");
		v29_remove_field(file, "FeatureSet", "default_symbol_visibility");
	} else if (i == 1) { /* cpp_features.proto */
		v29_remove_field(file, "CppFeatures", "repeated_type");
	}
	return true;
}

const LpVersion lp_version_29_1 = {
	.name = "29.1",
	.maximum_known_edition = LP_EDITION_2023, /* MaximumAllowedEdition() */
	.base = &lp_version_33_4,
	.builtin_file = v29_builtin_file,
	.legacy_feature_lifetimes = true,
	.generator_edition_defaults = true,
};
