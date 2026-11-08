// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The parts of src/google/protobuf/compiler/python (protobuf v36.2) that the Python generator and
 * the Python stub (.pyi) generator share, ported to C:
 *
 *   - helpers.c (helpers.{h,cc}): module names, Python keywords, file names;
 *   - pyi_generator.c (pyi_generator.{h,cc}): PyiGenerator, which the pyi plugin runs and the
 *     Python generator runs in-process for its pyi_out option.
 *
 * Unity build (libprotoc_python.c); memory comes from the pkg/core arena. */
#ifndef LIBPROTOC_PYTHON_H
#define LIBPROTOC_PYTHON_H

#include "libprotoc.h"

/* ---- helpers.h ---- */

/* ModuleName: the Python module of a .proto file ("foo/bar-baz.proto" -> "foo.bar_baz_pb2"). */
char *lpy_module_name(const char *filename);
/* StrippedModuleName (the same as ModuleName in the open source release) */
char *lpy_stripped_module_name(const char *filename);
/* ContainsPythonKeyword: a '.'-separated component of module_name is a Python keyword. */
bool lpy_contains_python_keyword(const char *module_name);
bool lpy_is_python_keyword(const char *name);
/* ResolveKeyword: "globals()['<name>']" for a keyword, else name. */
char *lpy_resolve_keyword(const char *name);
/* GetFileName: the module name as a path with suffix ("foo/bar_pb2.py"). */
char *lpy_get_file_name(const LpFile *file, const char *suffix);
/* HasGenericServices: services and option py_generic_services. */
bool lpy_has_generic_services(const LpFile *file);
/* NamePrefixedWithNestedTypes of a message or an enum (d is a ProtoMessage or ProtoEnum). */
char *lpy_name_prefixed_with_nested_types(const ProtoDesc *d, const char *separator);

/* ---- pyi_generator.h ---- */

/* PyiGenerator::Generate (LpGenerator.generate) */
bool lpy_pyi_generate(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
		      Buf *error);

#endif
