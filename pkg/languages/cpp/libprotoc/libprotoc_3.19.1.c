// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 3.19.1: what protobuf v3.19.1 (https://github.com/protocolbuffers/protobuf, tag
 * v3.19.1) does differently from 3.21.12 (libprotoc_3.21.12.c, its base), for the plugins built
 * with it (grpc/node v1.13.1: grpc-tools 1.13.1, whose protobuf submodule is v3.19.1). Its own
 * library (cpp_libprotoc_3.19.1), linked by those generators on top of cpp_libprotoc_3.21.12.
 * plugin.cc, code_generator.cc, plugin.proto and io/printer.cc are those of 3.21.12 (no change
 * but nullptr and the flush of BackUp(0)), so the plugins use the io::Printer of
 * printer_3.21.12.h.
 *
 *   - the generated pool: descriptor.proto of v3.19.1 lacks FieldOptions.unverified_lazy (15),
 *     which stays an unknown field (no "[lazy = true] can only be specified for submessage
 *     fields." nor "Extensions cannot be marked as unverified_lazy." for it);
 *   - descriptor.cc (LpVersion.legacy_descriptor_builder): no limits on the package length and
 *     depth (the port fails with an "unsupported" error over 1 MiB of package prefixes, where
 *     AddPackage turns quadratic) or on message nesting, nested types built before the enums, ranges and extensions of
 *     their message (the order of errors and of duplicate symbols), no "Suggested field numbers"
 *     error, and DetectMapConflicts reports every conflict;
 *   - the log prefixes of the default error collector: GOOGLE_LOG(ERROR) and GOOGLE_LOG(WARNING)
 *     with the __FILE__ of the grpc-tools release build (the CMake build of packages/grpc-tools
 *     on GitHub Actions) and the lines of v3.19.1; the warnings (an enum value name that
 *     conflicts in proto2, a used extension number) are logged before the errors of a file that
 *     fails.
 *
 * Everything else that 3.21.12 does differently from 36.2 (no editions, the JSON name and enum
 * value checks, map entries, field number conflicts) is the same in 3.19.1. */
#include "libprotoc.h"

/* Field numbers of descriptor.proto */
enum {
	F_FILE_MESSAGE_TYPE = 4,
	F_MSG_FIELD = 2,
	F_FIELD_NUMBER = 3,
	F_FOPT_UNVERIFIED_LAZY = 15,
};

static bool v3_19_unverified_lazy(const PbMsg *e, const void *arg)
{
	(void)arg;
	return pb_int(e, F_FIELD_NUMBER, 0) == F_FOPT_UNVERIFIED_LAZY;
}

/* LpVersion.builtin_file (on top of 3.21.12's) */
static COLD bool builtin_file(int i, PbMsg *file)
{
	if (i == 0)
		lp_desc_remove_if(lp_desc_find(file, F_FILE_MESSAGE_TYPE, "FieldOptions"), F_MSG_FIELD,
				  v3_19_unverified_lazy, NULL);
	return true;
}

#define LOG_PREFIX \
	"[libprotobuf ERROR " \
	"/home/runner/work/grpc-node/grpc-node/packages/grpc-tools/deps/protobuf/src/google/protobuf/descriptor.cc:"

const LpVersion lp_version_3_19_1 = {
	.name = "3.19.1",
	.maximum_known_edition = LP_EDITION_PROTO3,
	.base = &lp_version_3_21_12,
	.builtin_file = builtin_file,
	.log_file_prefix = LOG_PREFIX "4202] ",
	.log_error_prefix = LOG_PREFIX "4205] ",
	.log_warning_prefix = "[libprotobuf WARNING "
			      "/home/runner/work/grpc-node/grpc-node/packages/grpc-tools/deps/protobuf/src/google/"
			      "protobuf/descriptor.cc:4256] ",
	.no_editions = true,
	.field_json_name_errors = lp_3_21_12_field_json_name_errors,
	.enum_value_name_error = lp_3_21_12_enum_value_name_error,
	.legacy_map_entry = true,
	.legacy_field_number_error = true,
	.legacy_descriptor_builder = true,
};
