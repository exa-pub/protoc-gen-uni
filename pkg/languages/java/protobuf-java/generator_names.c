/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* A port of java/core/src/main/java/com/google/protobuf/GeneratorNames.java of protobuf-java
 * 4.36.2 (the class is new in 4.x): the Java names protoc's java generator gives to files and
 * messages, from the resolved features (features.c). */
#include "protobuf_java.h"

#include <string.h>

/* getFileJavaPackage: java_package when set (even empty), else the package */
const char *jnames_file_java_package(const JFile *f)
{
	PbMsg *o = pb_msg(f->proto, FDP_OPTIONS);
	return pb_has(o, 1) ? jstr(o, 1) : f->package;
}

/* underscoresToCamelCase(input, capitalizeNextLetter = true) */
static const char *jnames_underscores_to_camel_case(const char *s)
{
	Buf b = {0};
	bool cap = true;
	for (int i = 0; s[i]; i++) {
		char c = s[i];
		if (c >= 'a' && c <= 'z') {
			buf_addc(&b, cap ? (char)(c - 'a' + 'A') : c);
			cap = false;
		} else if (c >= 'A' && c <= 'Z') {
			/* i == 0 && !cap cannot happen: cap starts true */
			buf_addc(&b, c);
			cap = false;
		} else if (c >= '0' && c <= '9') {
			buf_addc(&b, c);
			cap = true;
		} else {
			/* other characters, also the two surrogates of a supplementary character and the
			 * bytes of other UTF-8 sequences: Java skips every char that is not an ASCII letter
			 * or digit */
			cap = true;
		}
	}
	return xstrdup(buf_str(&b));
}

static bool jnames_conflicts_msg(const PbMsg *m, const char *name)
{
	if (str_eq(jstr(m, DP_NAME), name))
		return true;
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		if (str_eq(jstr(pb_msg_at(m, DP_ENUM_TYPE, i), EDP_NAME), name))
			return true;
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		if (jnames_conflicts_msg(pb_msg_at(m, DP_NESTED_TYPE, i), name))
			return true;
	return false;
}

/* hasConflictingClassName */
static bool jnames_conflicts(const PbMsg *file, const char *name)
{
	for (int i = 0; i < pb_count(file, FDP_ENUM_TYPE); i++)
		if (str_eq(jstr(pb_msg_at(file, FDP_ENUM_TYPE, i), EDP_NAME), name))
			return true;
	for (int i = 0; i < pb_count(file, FDP_SERVICE); i++)
		if (str_eq(jstr(pb_msg_at(file, FDP_SERVICE, i), SDP_NAME), name))
			return true;
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++)
		if (jnames_conflicts_msg(pb_msg_at(file, FDP_MESSAGE_TYPE, i), name))
			return true;
	return false;
}

static const char *jnames_file_class_name_uncached(const JFile *f)
{
	PbMsg *o = pb_msg(f->proto, FDP_OPTIONS);
	if (pb_has(o, 8))
		return jstr(o, 8);
	bool old_default = jfile_features(f)->v[JF_JAVA_USE_OLD_OUTER_CLASSNAME_DEFAULT] != 0;
	/* getDefaultFileClassName */
	const char *name = f->name, *slash = strrchr(name, '/');
	if (slash)
		name = slash + 1;
	if (has_suffix(name, ".protodevel"))
		name = xstrndup(name, strlen(name) - strlen(".protodevel"));
	else if (has_suffix(name, ".proto"))
		name = xstrndup(name, strlen(name) - strlen(".proto"));
	const char *cls = jnames_underscores_to_camel_case(name);
	if (!old_default)
		cls = xcat(cls, "Proto");
	if (old_default && jnames_conflicts(f->proto, cls))
		return xcat(cls, "OuterClass");
	return cls;
}

const char *jnames_file_class_name(const JFile *f)
{
	if (!f->file_class_name)
		((JFile *)f)->file_class_name = jnames_file_class_name_uncached(f);
	return f->file_class_name;
}

bool jnames_nest_in_file_class(const JMsg *m)
{
	switch (jmsg_features(m)->v[JF_JAVA_NEST_IN_FILE_CLASS]) {
	case 2: return true;  /* YES */
	case 1: return false; /* NO */
	case 3: return !jpb_bool(pb_msg(m->file->proto, FDP_OPTIONS), 10, false); /* LEGACY: !java_multiple_files */
	default: java_exception("java.lang.IllegalArgumentException", "Java features are not resolved");
	}
}

/* joinPackage */
static const char *jnames_join_package(const char *a, const char *b)
{
	return !a[0] ? b : !b[0] ? a : xcat(a, ".", b);
}

const char *jnames_qualified_class_name(const JMsg *m)
{
	/* getClassNameWithoutPackage: stripPackageName(fullName, file) */
	const char *without = m->full_name;
	if (m->file->package[0])
		without += strlen(m->file->package) + 1;
	/* getClassFullName(nameWithoutPackage, file, isOwnFile), then '$' -> '.' */
	const char *pkg = jnames_file_java_package(m->file);
	Buf b = {0};
	if (!jnames_nest_in_file_class(m)) {
		buf_adds(&b, pkg);
		if (b.n)
			buf_addc(&b, '.');
	} else {
		buf_adds(&b, jnames_join_package(pkg, jnames_file_class_name(m->file)));
		if (b.n)
			buf_addc(&b, '.');
	}
	buf_adds(&b, without);
	return str_replace_all(buf_str(&b), "$", ".");
}
