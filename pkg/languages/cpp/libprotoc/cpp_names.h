// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 36.2: src/google/protobuf/compiler/cpp/names.h, the C++ names shared by the cpp
 * generator of protoc and the plugins built on it (grpc's cpp plugin), with the parts of
 * compiler/cpp/helpers.cc they use (keywords, known name collisions, namespaces). Strings are
 * arena allocated. */
#ifndef LIBPROTOC_CPP_NAMES_H
#define LIBPROTOC_CPP_NAMES_H

#include "libprotoc.h"

/* NameContext and NameKind of ResolveKnownNameCollisions */
enum { LP_CPP_CTX_FILE, LP_CPP_CTX_MESSAGE };
enum { LP_CPP_KIND_TYPE, LP_CPP_KIND_FUNCTION, LP_CPP_KIND_VALUE };

/* Whether name is a C++ keyword (helpers.cc kKeywordList). */
bool lp_cpp_is_keyword(const char *name);
/* ResolveKeyword: name + "_" for keywords. */
char *lp_cpp_resolve_keyword(const char *name);
char *lp_cpp_resolve_known_name_collisions(const char *name, int name_context, int name_kind);
/* UnderscoresToCamelCase */
char *lp_cpp_underscores_to_camel_case(const char *input, bool cap_next_letter);

/* Namespace(package): "::a::b" with keywords resolved, "" for no package. */
char *lp_cpp_package_namespace(const char *package);
/* Namespace(FileDescriptor): pb.file.cpp namespace if set, else of the package. */
char *lp_cpp_namespace(const LpFile *file);
/* ValidateCcNamespace: NULL or the error. */
char *lp_cpp_validate_cc_namespace(const LpFile *file);
char *lp_cpp_class_name(const LpMessage *descriptor);
char *lp_cpp_enum_class_name(const LpEnum *enum_descriptor);
/* QualifiedFileLevelSymbol */
char *lp_cpp_qualified_file_level_symbol(const LpFile *file, const char *name);
char *lp_cpp_qualified_class_name(const LpMessage *d);
char *lp_cpp_qualified_enum_class_name(const LpEnum *d);
char *lp_cpp_extension_name(const LpField *d);
char *lp_cpp_qualified_extension_name(const LpField *d);
char *lp_cpp_field_name(const LpField *field);
char *lp_cpp_oneof_case_constant_name(const LpField *field);
char *lp_cpp_qualified_oneof_case_constant_name(const LpField *field);
char *lp_cpp_enum_value_name(const LpEnumValue *enum_value);

#endif
