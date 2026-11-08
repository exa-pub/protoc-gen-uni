// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/python/helpers.{h,cc} (protobuf v36.2). PrintImport is
 * declared there but never defined, GeneratedCodeToBase64 is unused by the generators. */

enum { LPY_FILEOPT_PY_GENERIC_SERVICES = 18 };

char *lpy_module_name(const char *filename)
{
	char *basename = lp_strip_proto(filename);
	for (char *p = basename; *p; p++) {
		if (*p == '-')
			*p = '_';
		else if (*p == '/')
			*p = '.';
	}
	return xcat(basename, "_pb2");
}

char *lpy_stripped_module_name(const char *filename) { return lpy_module_name(filename); }

/* kKeywords */
static const char *const lpy_keywords[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
	"del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
	"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
};

static bool lpy_is_keyword_n(const char *name, size_t n)
{
	for (size_t i = 0; i < sizeof lpy_keywords / sizeof *lpy_keywords; i++)
		if (strlen(lpy_keywords[i]) == n && memcmp(lpy_keywords[i], name, n) == 0)
			return true;
	return false;
}

bool lpy_contains_python_keyword(const char *module_name)
{
	for (const char *p = module_name;;) {
		const char *dot = strchr(p, '.');
		size_t n = dot ? (size_t)(dot - p) : strlen(p);
		if (lpy_is_keyword_n(p, n))
			return true;
		if (!dot)
			return false;
		p = dot + 1;
	}
}

bool lpy_is_python_keyword(const char *name) { return lpy_is_keyword_n(name, strlen(name)); }

char *lpy_resolve_keyword(const char *name)
{
	if (lpy_is_python_keyword(name))
		return xcat("globals()['", name, "']");
	return xstrdup(name);
}

char *lpy_get_file_name(const LpFile *file, const char *suffix)
{
	char *filename = lpy_module_name(file->name);
	for (char *p = filename; *p; p++)
		if (*p == '.')
			*p = '/';
	return xcat(filename, suffix);
}

bool lpy_has_generic_services(const LpFile *file)
{
	return file->n_services > 0 && pb_int(file->options, LPY_FILEOPT_PY_GENERIC_SERVICES, 0);
}

static const LpMessage *lpy_containing_type(const ProtoDesc *d)
{
	return d->kind == PROTO_MESSAGE ? ((const LpMessage *)d)->containing_type : ((const LpEnum *)d)->containing_type;
}

char *lpy_name_prefixed_with_nested_types(const ProtoDesc *d, const char *separator)
{
	const char *name = d->name;
	const LpMessage *parent = lpy_containing_type(d);
	bool dot = str_eq(separator, ".");
	if (parent) {
		char *prefix = lpy_name_prefixed_with_nested_types((const ProtoDesc *)parent, separator);
		if (dot && lpy_is_python_keyword(name))
			return xcat("getattr(", prefix, ", '", name, "')");
		return xcat(prefix, separator, name);
	}
	return dot ? lpy_resolve_keyword(name) : xstrdup(name);
}
