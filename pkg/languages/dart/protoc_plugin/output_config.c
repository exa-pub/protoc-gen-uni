/* Port of protoc_plugin 25.1.0 lib/src/output_config.dart (DefaultOutputConfiguration).
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static const char *const wellKnownTypeProtoPaths[] = {
	"google/protobuf/any.proto", "google/protobuf/api.proto", "google/protobuf/duration.proto",
	"google/protobuf/empty.proto", "google/protobuf/field_mask.proto", "google/protobuf/source_context.proto",
	"google/protobuf/struct.proto", "google/protobuf/timestamp.proto", "google/protobuf/type.proto",
	"google/protobuf/wrappers.proto", NULL,
};

/* outputPathFor(inputPath, extension).path */
static const char *outputPathFor(const char *inputUri, const char *extension)
{
	return xcat(dpath_without_extension(inputUri), extension);
}

/* resolveImport(target, source, extension).toString() */
static const char *resolveImport(const char *target, const char *source, const char *extension)
{
	for (const char *const *p = wellKnownTypeProtoPaths; *p; p++)
		if (strcmp(*p, target) == 0)
			return xcat("package:protobuf/well_known_types/", dpath_without_extension(target), extension);
	const char *sourceDir = dpath_dirname(source);
	const char *rel = dpath_relative(target, sourceDir);
	if (!rel)
		dart_throw(xfmt("PathException: Unable to find a path to \"%s\" from \"%s\".", target, sourceDir));
	return xcat(dpath_without_extension(rel), extension);
}
