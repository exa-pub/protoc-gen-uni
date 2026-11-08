// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 36.2: src/google/protobuf/compiler/csharp/names.h, the C# names shared by the csharp
 * generator of protoc and grpc's csharp plugin (csharp_names.c). Strings are arena allocated.
 *
 * A csharp_namespace with a NUL byte fails (fail()) as unsupported: the names are C strings. The
 * csharp generator rejects it for the files it generates before it asks for a name, like
 * upstream. */
#ifndef LIBPROTOC_CSHARP_NAMES_H
#define LIBPROTOC_CSHARP_NAMES_H

#include "libprotoc.h"

/* GetFileNamespace: csharp_namespace, else the package in PascalCase with its periods. */
char *lp_csharp_file_namespace(const LpFile *file);

/* GetClassName: "global::<namespace>.<Outer>.Types.<Name>". */
char *lp_csharp_class_name_message(const LpMessage *descriptor);
char *lp_csharp_class_name_enum(const LpEnum *descriptor);

/* GetReflectionClassUnqualifiedName ("<FileBase>Reflection"),
 * GetExtensionClassUnqualifiedName ("<FileBase>Extensions") and GetReflectionClassName
 * ("global::<namespace>.<FileBase>Reflection"). */
char *lp_csharp_reflection_class_unqualified_name(const LpFile *file);
char *lp_csharp_extension_class_unqualified_name(const LpFile *file);
char *lp_csharp_reflection_class_name(const LpFile *file);

/* GetOutputFile: "<FileBase><file_extension>", under the namespace directories (relative to
 * base_namespace) if generate_directories. On a namespace that does not start with base_namespace
 * returns "" and sets *error. */
char *lp_csharp_output_file(const LpFile *file, const char *file_extension, bool generate_directories,
			    const char *base_namespace, char **error);

/* UnderscoresToPascalCase and UnderscoresToCamelCase. */
char *lp_csharp_underscores_to_pascal_case(const char *input);
char *lp_csharp_underscores_to_camel_case(const char *input, bool cap_next_letter, bool preserve_period);

#endif
