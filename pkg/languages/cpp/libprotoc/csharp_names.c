// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

/* Port of src/google/protobuf/compiler/csharp/names.cc (protobuf v36.2), see csharp_names.h. */
#include "csharp_names.h"

enum { FILEOPT_CSHARP_NAMESPACE = 37 };

/* StripDotProto: everything before the last '.' (all of it without one). */
static char *csharp_strip_dot_proto(const char *proto_file)
{
	const char *dot = strrchr(proto_file, '.');
	return dot ? xstrndup(proto_file, (size_t)(dot - proto_file)) : xstrdup(proto_file);
}

/* GetFileNameBase: the Pascal-cased last part of the proto file ("google/protobuf/foo_bar.proto"
 * gives "FooBar"). */
static char *csharp_file_name_base(const LpFile *descriptor)
{
	const char *slash = strrchr(descriptor->name, '/');
	const char *base = slash ? slash + 1 : descriptor->name;
	return lp_csharp_underscores_to_pascal_case(csharp_strip_dot_proto(base));
}

/* ToCSharpName */
static char *csharp_to_csharp_name(const char *name, const LpFile *file)
{
	char *result = lp_csharp_file_namespace(file);
	if (*result)
		result = xcat(result, ".");
	const char *classname = name;
	if (*file->package) {
		/* Strip the proto package from full_name since we've replaced it with the C#
		 * namespace. */
		classname = name + strlen(file->package) + 1;
	}
	return xcat("global::", result, str_replace_all(classname, ".", ".Types."));
}

char *lp_csharp_file_namespace(const LpFile *file)
{
	if (pb_has(file->options, FILEOPT_CSHARP_NAMESPACE)) {
		Str ns = pb_bytes(file->options, FILEOPT_CSHARP_NAMESPACE);
		if (memchr(ns.p, 0, ns.n))
			fail("unsupported by protoc-gen-uni: csharp_namespace with a NUL byte in \"%s\"", file->name);
		return xstrndup(ns.p, ns.n);
	}
	return lp_csharp_underscores_to_camel_case(file->package, true, true);
}

char *lp_csharp_class_name_message(const LpMessage *descriptor)
{
	return csharp_to_csharp_name(descriptor->full_name, descriptor->file);
}

char *lp_csharp_class_name_enum(const LpEnum *descriptor)
{
	return csharp_to_csharp_name(descriptor->full_name, descriptor->file);
}

char *lp_csharp_reflection_class_unqualified_name(const LpFile *file)
{
	return xcat(csharp_file_name_base(file), "Reflection");
}

char *lp_csharp_reflection_class_name(const LpFile *file)
{
	char *result = lp_csharp_file_namespace(file);
	if (*result)
		result = xcat(result, ".");
	return xcat("global::", result, lp_csharp_reflection_class_unqualified_name(file));
}

char *lp_csharp_extension_class_unqualified_name(const LpFile *file)
{
	return xcat(csharp_file_name_base(file), "Extensions");
}

char *lp_csharp_output_file(const LpFile *file, const char *file_extension, bool generate_directories,
			    const char *base_namespace, char **error)
{
	char *relative_filename = xcat(csharp_file_name_base(file), file_extension);
	if (!generate_directories)
		return relative_filename;
	char *ns = lp_csharp_file_namespace(file);
	const char *namespace_suffix = ns;
	if (*base_namespace) {
		/* Check that the base_namespace is either equal to or a leading part of the file
		 * namespace. This isn't just a simple prefix; "Foo.B" shouldn't be regarded as a
		 * prefix of "Foo.Bar". */
		size_t n = strlen(base_namespace);
		bool ok = strncmp(namespace_suffix, base_namespace, n) == 0;
		if (ok) {
			namespace_suffix += n;
			if (*namespace_suffix) {
				if (*namespace_suffix == '.')
					namespace_suffix++;
				else
					ok = false;
			}
		}
		if (!ok) {
			*error = xcat("Namespace ", ns, " is not a prefix namespace of base namespace ", base_namespace);
			return xstrdup(""); /* This will be ignored, because we've set an error. */
		}
	}
	return xcat(str_replace_all(namespace_suffix, ".", "/"), *namespace_suffix ? "/" : "", relative_filename);
}

char *lp_csharp_underscores_to_pascal_case(const char *input)
{
	return lp_csharp_underscores_to_camel_case(input, true, false);
}

char *lp_csharp_underscores_to_camel_case(const char *input, bool cap_next_letter, bool preserve_period)
{
	Buf result = {0};
	size_t n = strlen(input);
	for (size_t i = 0; i < n; i++) {
		char c = input[i];
		if ('a' <= c && c <= 'z') {
			buf_addc(&result, cap_next_letter ? (char)(c + ('A' - 'a')) : c);
			cap_next_letter = false;
		} else if ('A' <= c && c <= 'Z') {
			/* Force first letter to lower-case unless explicitly told to capitalize it;
			 * capital letters after the first are left as-is. */
			buf_addc(&result, i == 0 && !cap_next_letter ? (char)(c + ('a' - 'A')) : c);
			cap_next_letter = false;
		} else if ('0' <= c && c <= '9') {
			buf_addc(&result, c);
			cap_next_letter = true;
		} else {
			cap_next_letter = true;
			if (c == '.' && preserve_period)
				buf_addc(&result, '.');
		}
	}
	/* Add a trailing "_" if the name should be altered. */
	if (n && input[n - 1] == '#')
		buf_addc(&result, '_');
	/* https://github.com/protocolbuffers/protobuf/issues/8101: keep one leading underscore when
	 * the result would start with a digit. */
	if (result.n && '0' <= result.p[0] && result.p[0] <= '9' && n && input[0] == '_')
		return xcat("_", buf_str(&result));
	return (char *)buf_str(&result);
}
