/* Copyright (c) 2022 Nipunn Koorapati
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Port of mypy_protobuf/main.py of mypy-protobuf v5.1.0
 * (https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/mypy_protobuf/main.py), Apache-2.0:
 * Descriptors, Imports, PkgWriter, generate_mypy_stubs and generate_mypy_grpc_stubs; main() and
 * grpc() are in the generators (community/nipunn1313-mypy, community/nipunn1313-mypy-grpc).
 * PkgWriter.write_services (py_generic_services) is not called by upstream and not ported. Python
 * crashes (KeyError, AssertionError, IndexError) are fail() calls with the exception text; input
 * that upstream accepts but this port cannot reproduce fails with "unsupported by protoc-gen-uni: ...". */
#include "mypy_protobuf.h"
#include "python.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- descriptor access (descriptor_pb2 defaults) ---- */

enum {
	FILEOPT_DEPRECATED = 23, FILEOPT_FEATURES = 50,
	MSGOPT_DEPRECATED = 3, MSGOPT_MAP_ENTRY = 7,
	FIELDOPT_DEPRECATED = 3, FIELDOPT_FEATURES = 21,
	ENUMOPT_DEPRECATED = 3, ENUMVALOPT_DEPRECATED = 1,
	FEATURE_FIELD_PRESENCE = 1, PRESENCE_UNKNOWN = 0, PRESENCE_IMPLICIT = 2,
	TYPE_DOUBLE = 1, TYPE_GROUP = 10, TYPE_MESSAGE = 11, TYPE_ENUM = 14,
	/* the extensions of mypy_protobuf/extensions.proto on google.protobuf.FieldOptions */
	EXT_CASTTYPE = 1151, EXT_KEYTYPE = 1152, EXT_VALUETYPE = 1153, EXT_OPTIONS = 1154,
};

/* Upstream reads string fields with upb, which returns a value that is not valid UTF-8 as bytes
 * (str operations on it raise TypeError or print its repr), and a str may contain NUL, which the
 * C strings of this port cannot. Neither is supported. */
static bool valid_utf8(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xfffd && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

static const char *checked(Str s, const char *what)
{
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: NUL byte in %s %s", what, py_repr_str(s));
	if (!valid_utf8(s))
		fail("unsupported by protoc-gen-uni: invalid UTF-8 in %s", what);
	return s.p;
}

static bool clean(Str s) { return !memchr(s.p, 0, s.n) && valid_utf8(s); }

/* A string field of a descriptor, checked. */
static const char *str_of(const PbMsg *m, uint32_t num, const char *what) { return checked(pb_bytes(m, num), what); }

static const char *name_of(const PbMsg *m) { return str_of(m, 1, "name"); }

static bool is_scalar(const PbMsg *field)
{
	int64_t t = pb_int(field, FD_TYPE, TYPE_DOUBLE);
	return !(t == TYPE_MESSAGE || t == TYPE_GROUP);
}

static bool is_repeated(const PbMsg *field) { return pb_int(field, FD_LABEL, LABEL_OPTIONAL) == LABEL_REPEATED; }

static bool field_deprecated(const PbMsg *field) { return pb_int(pb_msg(field, FD_OPTIONS), FIELDOPT_DEPRECATED, 0) != 0; }

/* The mypy_protobuf extensions of a field: (mypy_protobuf.options).casttype/keytype/valuetype and
 * the legacy (mypy_protobuf.casttype/keytype/valuetype). Parsed from the unknown fields of
 * FieldOptions like upb parses known extensions: the last value of a string wins, occurrences of
 * the message are merged, values with another wire type are ignored. */
typedef struct {
	Str legacy[3], opts[3]; /* casttype, keytype, valuetype */
} FieldExtensions;

static FieldExtensions field_extensions(const PbMsg *field)
{
	FieldExtensions o = {0};
	for (int i = 0; i < 3; i++)
		o.legacy[i] = o.opts[i] = (Str){"", 0};
	PbReader r = pb_unknown_fields(pb_msg(field, FD_OPTIONS));
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.wt != WT_LEN)
			continue;
		if (f.num >= EXT_CASTTYPE && f.num <= EXT_VALUETYPE) {
			o.legacy[f.num - EXT_CASTTYPE] = (Str){(const char *)f.val, f.vlen};
		} else if (f.num == EXT_OPTIONS) {
			PbReader sub = {f.val, f.val + f.vlen};
			PbRawField g;
			while (pb_next_field(&sub, &g))
				if (g.wt == WT_LEN && g.num >= 1 && g.num <= 3)
					o.opts[g.num - 1] = (Str){(const char *)g.val, g.vlen};
		}
	}
	return o;
}

/* Upstream parses the request with mypy_protobuf/extensions.proto (proto3) registered, so upb
 * parses these extensions of every field in the request: a malformed (mypy_protobuf.options) or a
 * string that is not valid UTF-8 fails the whole parse. */
static NORETURN COLD void decode_error(void)
{
	fail("google.protobuf.message.DecodeError: Error parsing message with type "
	     "'google.protobuf.compiler.CodeGeneratorRequest'");
}

static void check_field_extensions(const PbMsg *field)
{
	PbReader r = pb_unknown_fields(pb_msg(field, FD_OPTIONS));
	PbRawField f;
	while (r.p < r.end) {
		if (!pb_next_field(&r, &f))
			decode_error(); /* unreachable: pkg/pb parsed the unknown fields */
		if (f.wt != WT_LEN)
			continue;
		if (f.num >= EXT_CASTTYPE && f.num <= EXT_VALUETYPE) {
			if (!valid_utf8((Str){(const char *)f.val, f.vlen}))
				decode_error();
		} else if (f.num == EXT_OPTIONS) {
			PbReader sub = {f.val, f.val + f.vlen};
			PbRawField g;
			while (sub.p < sub.end) {
				if (!pb_next_field(&sub, &g))
					decode_error();
				if (g.wt == WT_LEN && g.num >= 1 && g.num <= 3 && !valid_utf8((Str){(const char *)g.val, g.vlen}))
					decode_error();
			}
		}
	}
}

static void check_message_extensions(const PbMsg *m)
{
	for (int i = 0; i < pb_count(m, DP_FIELD); i++)
		check_field_extensions(pb_msg_at(m, DP_FIELD, i));
	for (int i = 0; i < pb_count(m, DP_EXTENSION); i++)
		check_field_extensions(pb_msg_at(m, DP_EXTENSION, i));
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++) /* nesting is limited by the parser */
		check_message_extensions(pb_msg_at(m, DP_NESTED_TYPE, i));
}

COLD void mypy_check_extensions(const PbMsg *req)
{
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		const PbMsg *fd = pb_msg_at(req, CGR_PROTO_FILE, i);
		for (int j = 0; j < pb_count(fd, FDP_EXTENSION); j++)
			check_field_extensions(pb_msg_at(fd, FDP_EXTENSION, j));
		for (int j = 0; j < pb_count(fd, FDP_MESSAGE_TYPE); j++)
			check_message_extensions(pb_msg_at(fd, FDP_MESSAGE_TYPE, j));
	}
}

/* The values are valid UTF-8 (check_mypy_extensions); a NUL byte is not supported. */
static const char *cstr(Str s) { return xstrndup(checked(s, "a mypy_protobuf option"), s.n); }

/* ---- Python helpers ---- */

static const char *const PYTHON_RESERVED[] = {
	"False", "None", "True", "and", "as", "async", "await", "assert", "break", "class", "continue",
	"def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import",
	"in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while",
	"with", "yield",
};

static bool python_reserved(const char *s)
{
	for (size_t i = 0; i < sizeof PYTHON_RESERVED / sizeof *PYTHON_RESERVED; i++)
		if (str_eq(s, PYTHON_RESERVED[i]))
			return true;
	return false;
}

static bool proto_enum_reserved(const char *s)
{
	return str_eq(s, "Name") || str_eq(s, "Value") || str_eq(s, "keys") || str_eq(s, "values") || str_eq(s, "items");
}

/* str.upper() of an identifier: only ASCII is supported (protoc and buf only allow ASCII
 * identifiers; Unicode case mapping has no table here). */
static char *upper(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++) {
		if ((unsigned char)*p >= 0x80)
			fail("unsupported by protoc-gen-uni: str.upper() of the non-ASCII name %s", py_repr_str(str_c(s)));
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	}
	return r;
}

/* s[:-6] */
static char *drop6(const char *s)
{
	size_t n = strlen(s);
	return xstrndup(s, n >= 6 ? n - 6 : 0);
}

static char *replace_char(const char *s, char from, char to)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p == from)
			*p = to;
	return r;
}

static char *mangle_global_identifier(const char *name) { return xcat("Global___", name); }

/* repr() of a str key in a KeyError message (approximate: printable ASCII names). */
static NORETURN COLD void key_error(const char *key) { fail("KeyError: %s", py_repr_str(str_c(key))); }

/* ---- Descriptors ---- */

struct MypyDescriptors {
	Map files;           /* name -> FileDescriptorProto */
	Vec to_generate;     /* FileDescriptorProto, in file_to_generate order without duplicates */
	Map messages;        /* full name -> DescriptorProto */
	Map message_to_fd;   /* full name (messages, enums, "<enum>.ValueType") -> FileDescriptorProto */
};
typedef struct MypyDescriptors Descriptors;

static void add_enums(Descriptors *d, PbMsg *parent, uint32_t num, const char *prefix, PbMsg *fd)
{
	for (int i = 0; i < pb_count(parent, num); i++) {
		const char *name = xcat(prefix, name_of(pb_msg_at(parent, num, i)));
		map_put(&d->message_to_fd, name, fd);
		map_put(&d->message_to_fd, xcat(name, ".ValueType"), fd);
	}
}

static void add_messages(Descriptors *d, PbMsg *parent, uint32_t num, const char *prefix, PbMsg *fd)
{
	for (int i = 0; i < pb_count(parent, num); i++) {
		PbMsg *m = pb_msg_at(parent, num, i);
		const char *name = xcat(prefix, name_of(m));
		map_put(&d->messages, name, m);
		map_put(&d->message_to_fd, name, fd);
		const char *sub = xcat(name, ".");
		add_messages(d, m, DP_NESTED_TYPE, sub, fd);
		add_enums(d, m, DP_ENUM_TYPE, sub, fd);
	}
}

COLD MypyDescriptors *mypy_descriptors(PbMsg *req)
{
	Descriptors *d = xalloc(sizeof *d);
	memset(d, 0, sizeof *d);
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		PbMsg *fd = pb_msg_at(req, CGR_PROTO_FILE, i);
		/* only checked names are looked up, and they never equal another name */
		if (clean(pb_bytes(fd, FDP_NAME)))
			map_put(&d->files, name_of(fd), fd);
	}
	Map seen = {0};
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++) {
		const char *n = checked(pb_bytes_at(req, CGR_FILE_TO_GENERATE, i), "file_to_generate");
		PbMsg *fd = map_get(&d->files, n);
		if (!fd)
			key_error(n);
		if (!map_has(&seen, n)) {
			map_put(&seen, n, fd);
			vec_push(&d->to_generate, fd);
		}
	}
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++) {
		PbMsg *fd = pb_msg_at(req, CGR_PROTO_FILE, i);
		const char *pkg = str_of(fd, FDP_PACKAGE, "package");
		const char *prefix = *pkg ? xcat(".", pkg, ".") : ".";
		add_messages(d, fd, FDP_MESSAGE_TYPE, prefix, fd);
		add_enums(d, fd, FDP_ENUM_TYPE, prefix, fd);
	}
	return d;
}

/* ---- Python frames ---- */

/* The Python frames on the stack, for the RecursionError of upstream (recursion limit 1000, the
 * protoc-gen-mypy script: <module>, main, generate_mypy_stubs, ...). Only a chain of map entries
 * (python_type -> _map_key_value_types -> python_type) gets that deep, so the functions on the way
 * there count their frames: write_messages, write_extensions, python_type, the lambda of its type
 * mapping, _map_key_value_types, _import_casttype, _import_message, _builtin, _import,
 * Imports.add_import and Imports._name_alias. */
static int py_frames;

static void py_enter(void)
{
	if (++py_frames > 1000)
		fail("RecursionError: maximum recursion depth exceeded");
}

static void py_leave(void) { py_frames--; }

/* ---- Imports ---- */

typedef struct {
	const char *frm, *alias, *original; /* frm, alias: NULL for None */
} Import;

typedef struct {
	Map imports;        /* path -> Import */
	Vec paths;          /* the keys of imports */
	Map imported_names; /* name -> int count */
	Map typing;         /* name -> alias */
	Vec typing_names;   /* the keys of typing, in insertion order */
} Imports;

static void put_import(Imports *im, const char *path, const char *frm, const char *alias, const char *original)
{
	Import *imp = xalloc(sizeof *imp);
	*imp = (Import){frm, alias, original};
	if (!map_has(&im->imports, path))
		vec_push(&im->paths, (void *)path);
	map_put(&im->imports, path, imp);
}

static const char *name_alias_(Imports *im, const char *name, bool mangle)
{
	int *count = map_get(&im->imported_names, name);
	if (count) {
		++*count;
		return xfmt("%s%s_%d", mangle ? "_" : "", name, *count);
	}
	count = xalloc(sizeof *count);
	map_put(&im->imported_names, name, count);
	return xcat(mangle ? "_" : "", name);
}

static const char *name_alias(Imports *im, const char *name, bool mangle)
{
	py_enter();
	const char *r = name_alias_(im, name, mangle);
	py_leave();
	return r;
}

/* Imports.add_import; name NULL or "" imports the module itself. */
static const char *add_import_(Imports *im, const char *path, const char *name, bool mangle)
{
	if (!name || !*name) {
		put_import(im, path, NULL, NULL, path);
		return path;
	}
	const char *alias;
	Import *imp = map_get(&im->imports, path);
	if (imp) {
		alias = imp->alias;
	} else {
		const char *dot = strrchr(path, '.');
		if (dot) {
			const char *last = dot + 1;
			alias = name_alias(im, last, mangle);
			put_import(im, path, xstrndup(path, (size_t)(dot - path)), alias, last);
		} else {
			alias = name_alias(im, path, mangle);
			put_import(im, path, NULL, alias, path);
		}
	}
	return xcat(alias ? alias : "None", ".", name);
}

static const char *add_import(Imports *im, const char *path, const char *name, bool mangle)
{
	py_enter();
	const char *r = add_import_(im, path, name, mangle);
	py_leave();
	return r;
}

static const char *add_typing_import(Imports *im, const char *name)
{
	const char *alias = map_get(&im->typing, name);
	if (!alias) {
		alias = name_alias(im, name, true);
		map_put(&im->typing, name, (void *)alias);
		vec_push(&im->typing_names, (void *)name);
	}
	return alias;
}

/* ---- PkgWriter ---- */

typedef struct {
	Str *v;
	int n, cap;
} Lines;

static void lines_push(Lines *l, Str s)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 64;
		l->v = xrealloc(l->v, sizeof(Str) * (size_t)l->cap, sizeof(Str) * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = s;
}

typedef struct {
	int major, minor; /* major 0: None */
} Version;

typedef struct {
	PbMsg *fd;
	Descriptors *descriptors;
	bool readable_stubs, relax_strict_optional_primitives, use_default_deprecation_warnings;
	bool generate_concrete_servicer_stubs, grpc;
	MypyGrpcType grpc_type;
	Lines lines;
	int indent;
	Imports imports;
	Version typing_extensions_min, deprecated_min;
	Map sci; /* path key -> SourceCodeInfo.Location */
} PkgWriter;

/* Source code paths are keys of the form ".4.0.2.1". */
static const char *scl(const char *prefix, int64_t v) { return xfmt("%s.%lld", prefix, (long long)v); }

static void pkg_writer_init(PkgWriter *w, PbMsg *fd, Descriptors *d, const MypyOptions *o, bool grpc,
			    MypyGrpcType grpc_type)
{
	memset(w, 0, sizeof *w);
	w->fd = fd;
	w->descriptors = d;
	w->readable_stubs = o->readable_stubs;
	w->relax_strict_optional_primitives = o->relax_strict_optional_primitives;
	w->use_default_deprecation_warnings = o->use_default_deprecation_warnings;
	w->generate_concrete_servicer_stubs = o->generate_concrete_servicer_stubs;
	w->grpc = grpc;
	w->grpc_type = grpc_type;
	PbMsg *sci = pb_msg(fd, FDP_SOURCE_CODE_INFO);
	for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
		PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
		Buf key = {0};
		for (int j = 0; j < pb_count(loc, LOC_PATH); j++)
			buf_fmt(&key, ".%lld", (long long)(int32_t)pb_int_at(loc, LOC_PATH, j));
		map_put(&w->sci, buf_str(&key), loc);
	}
}

static const char *DEPRECATED_NAME = "_deprecated";

static void version_max(Version *v, int major, int minor)
{
	if (!v->major || v->major < major || (v->major == major && v->minor < minor))
		*v = (Version){major, minor};
}

static const char *import__(PkgWriter *w, const char *path, const char *name)
{
	if (str_eq(path, "typing_extensions")) {
		static const struct {
			const char *name;
			int major, minor;
		} stabilization[] = {{"TypeAlias", 3, 10}, {"TypeVar", 3, 13}, {"type_check_only", 3, 12}, {"Self", 3, 11}, {"Never", 3, 11}};
		for (size_t i = 0; i < sizeof stabilization / sizeof *stabilization; i++)
			if (str_eq(name, stabilization[i].name)) {
				version_max(&w->typing_extensions_min, stabilization[i].major, stabilization[i].minor);
				return add_typing_import(&w->imports, name);
			}
		fail("AssertionError");
	}
	if (str_eq(path, "warnings") && str_eq(name, "deprecated")) {
		if (!w->deprecated_min.major || w->deprecated_min.major < 3 ||
		    (w->deprecated_min.major == 3 && w->deprecated_min.minor < 11))
			w->deprecated_min = (Version){3, 13};
		return DEPRECATED_NAME;
	}
	const char *imp = replace_char(path, '/', '.');
	return add_import(&w->imports, imp, name, !w->readable_stubs);
}

static const char *import_(PkgWriter *w, const char *path, const char *name)
{
	py_enter();
	const char *r = import__(w, path, name);
	py_leave();
	return r;
}

static const char *builtin(PkgWriter *w, const char *name)
{
	py_enter();
	const char *r = import_(w, "builtins", name);
	py_leave();
	return r;
}

static const char *property(PkgWriter *w) { return xcat("@", builtin(w, "property")); }

static const char *import_message_(PkgWriter *w, const char *name)
{
	PbMsg *message_fd = map_get(&w->descriptors->message_to_fd, name);
	if (!message_fd)
		key_error(name);
	const char *fd_name = name_of(message_fd);
	if (!has_suffix(fd_name, ".proto"))
		fail("AssertionError");
	const char *pkg = str_of(message_fd, FDP_PACKAGE, "package");
	if (*pkg) {
		const char *prefix = xcat(".", pkg, ".");
		if (!has_prefix(name, prefix))
			fail("AssertionError");
		name += strlen(prefix);
	} else {
		if (name[0] != '.')
			fail("AssertionError");
		name++;
	}
	Vec split = str_split(name, '.');
	for (int i = 0; i < split.n; i++)
		if (python_reserved(split.v[i]))
			split.v[i] = xcat("_r_", split.v[i]);
	name = str_join(&split, ".");

	/* Message defined in this file. Note: GRPC stubs in same .proto are generated into separate files */
	if (!w->grpc && str_eq(fd_name, name_of(w->fd)))
		return w->readable_stubs ? name : mangle_global_identifier(name);

	const char *import_name = import_(w, xcat(replace_char(drop6(fd_name), '-', '_'), "_pb2"), split.v[0]);
	Vec rest = {0};
	for (int i = 1; i < split.n; i++)
		vec_push(&rest, split.v[i]);
	const char *remains = str_join(&rest, ".");
	if (!*remains)
		return import_name;
	return xcat(import_name, ".", remains);
}

static const char *import_message(PkgWriter *w, const char *name)
{
	py_enter();
	const char *r = import_message_(w, name);
	py_leave();
	return r;
}

static void write_line(PkgWriter *w, Str line)
{
	if (!line.n) {
		lines_push(&w->lines, line);
		return;
	}
	Buf b = {0};
	for (int i = 0; i < w->indent; i++)
		buf_addc(&b, ' ');
	buf_add_str(&b, line);
	lines_push(&w->lines, buf_view(&b));
}

/* _write_line of the concatenation of NUL-terminated strings. */
static void wl_(PkgWriter *w, const char *first, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, first);
	for (const char *s = first; s; s = va_arg(ap, const char *))
		buf_adds(&b, s);
	va_end(ap);
	write_line(w, buf_view(&b));
}
#define wl(w, ...) wl_(w, __VA_ARGS__, (const char *)0)

/* ---- comments ---- */

static void break_text(Str text, Lines *out)
{
	out->n = 0;
	if (!text.n)
		return;
	/* upb returns the comment as bytes: text_block.rstrip().split("\n") */
	if (!valid_utf8(text))
		fail("TypeError: a bytes-like object is required, not 'str'");
	Str *parts;
	int n = str_split_s(py_rstrip(text), '\n', &parts);
	for (int i = 0; i < n; i++) {
		Str line = parts[i];
		if (line.n && line.p[0] == ' ')
			line.p++, line.n--;
		lines_push(out, line);
	}
}

static PbMsg *location(PkgWriter *w, const char *key) { return map_get(&w->sci, key); }

static bool has_comments(PkgWriter *w, const char *key)
{
	PbMsg *loc = location(w, key);
	return loc && (pb_count(loc, LOC_LEADING_DETACHED) || pb_bytes(loc, LOC_LEADING).n || pb_bytes(loc, LOC_TRAILING).n);
}

/* _get_comments: false if there are no comments. */
static bool get_comments(PkgWriter *w, const char *key, Lines *lines)
{
	memset(lines, 0, sizeof *lines);
	if (!has_comments(w, key))
		return false;
	PbMsg *loc = location(w, key);
	Lines leading = {0}, trailing = {0};
	for (int i = 0; i < pb_count(loc, LOC_LEADING_DETACHED); i++)
		break_text(pb_bytes_at(loc, LOC_LEADING_DETACHED, i), lines);
	break_text(pb_bytes(loc, LOC_LEADING), &leading);
	break_text(pb_bytes(loc, LOC_TRAILING), &trailing);
	if (lines->n && (leading.n || trailing.n))
		lines_push(lines, (Str){"", 0});
	for (int i = 0; i < leading.n; i++)
		lines_push(lines, leading.v[i]);
	for (int i = 0; i < trailing.n; i++)
		lines_push(lines, trailing.v[i]);
	return true;
}

static const char *deprecation_message(PkgWriter *w, const char *key, const char *default_message)
{
	Lines comments;
	if (w->use_default_deprecation_warnings || !get_comments(w, key, &comments) || !comments.n)
		return default_message;
	Buf joined = {0};
	for (int i = 0; i < comments.n; i++) {
		if (i)
			buf_adds(&joined, "\\n");
		buf_add_str(&joined, comments.v[i]);
	}
	/* ast.parse(f'"""{joined}"""'); on a SyntaxError upstream prints a warning to stderr */
	Buf src = {0};
	buf_adds(&src, "\"\"\"");
	buf_add_str(&src, buf_view(&joined));
	buf_adds(&src, "\"\"\"");
	switch (py_parse_string_literals(buf_view(&src))) {
	case PY_PARSE_OK:
		break;
	case PY_PARSE_ERROR:
		return default_message;
	case PY_PARSE_UNKNOWN:
		fail("unsupported by protoc-gen-uni: whether ast.parse accepts the deprecation comment %s (in %s) is not "
		     "known: only string literals are recognized",
		     py_repr_str(buf_view(&src)), name_of(w->fd));
	}
	if (memchr(joined.p, 0, joined.n))
		return default_message; /* unreachable: ast.parse rejects NUL */
	return buf_str(&joined);
}

static void write_deprecation_warning(PkgWriter *w, const char *key, const char *default_message)
{
	const char *msg = deprecation_message(w, key, default_message);
	wl(w, "@", import_(w, "warnings", "deprecated"), "(\"\"\"", msg, "\"\"\")");
}

/* line.replace("\\", "\\\\").replace('"""', r'\"\"\"') */
static Str escape_comment(Str line)
{
	Buf b = {0};
	for (size_t i = 0; i < line.n; i++) {
		if (line.p[i] == '\\')
			buf_addc(&b, '\\');
		buf_addc(&b, line.p[i]);
	}
	Str s = buf_view(&b);
	Buf r = {0};
	for (size_t i = 0; i < s.n;) {
		if (i + 3 <= s.n && !memcmp(s.p + i, "\"\"\"", 3)) {
			buf_adds(&r, "\\\"\\\"\\\"");
			i += 3;
		} else {
			buf_addc(&r, s.p[i++]);
		}
	}
	return (Str){buf_str(&r), r.n};
}

static bool write_comments(PkgWriter *w, const char *key)
{
	Lines lines;
	if (!get_comments(w, key, &lines))
		return false;
	for (int i = 0; i < lines.n; i++)
		lines.v[i] = escape_comment(lines.v[i]);
	Buf b = {0};
	if (lines.n == 1) {
		Str line = lines.v[0];
		buf_adds(&b, "\"\"\"");
		buf_add_str(&b, line);
		if (line.n && (line.p[line.n - 1] == '"' || line.p[line.n - 1] == '\\'))
			buf_addc(&b, ' ');
		buf_adds(&b, "\"\"\"");
		write_line(w, buf_view(&b));
	} else {
		for (int i = 0; i < lines.n; i++) {
			if (i == 0) {
				Buf f = {0};
				buf_adds(&f, "\"\"\"");
				buf_add_str(&f, lines.v[i]);
				write_line(w, buf_view(&f));
			} else {
				write_line(w, lines.v[i]);
			}
		}
		write_line(w, STR_LIT("\"\"\""));
	}
	return true;
}

/* ---- types ---- */

static void python_type_to(PkgWriter *w, PbMsg *field, bool generic_container, Buf *out);

static const char *import_casttype(PkgWriter *w, Str casttype)
{
	py_enter();
	Vec split = str_split(cstr(casttype), '.');
	if (split.n != 2)
		fail("AssertionError: mypy_protobuf.[casttype,keytype,valuetype] is expected to be of format path/to/file.TypeInFile");
	const char *r = import_(w, replace_char(split.v[0], '/', '.'), split.v[1]);
	py_leave();
	return r;
}

/* Appends f"{ktype}, {vtype}" to out. */
static void map_key_value_types(PkgWriter *w, PbMsg *map_field, PbMsg *key_field, PbMsg *value_field, Buf *out)
{
	py_enter();
	FieldExtensions o = field_extensions(map_field);
	/* a warning on stderr for the legacy (mypy_protobuf.keytype) */
	Str key_casttype = o.opts[1].n ? o.opts[1] : o.legacy[1];
	if (key_casttype.n)
		buf_adds(out, import_casttype(w, key_casttype));
	else
		python_type_to(w, key_field, false, out);
	buf_adds(out, ", ");
	Str value_casttype = o.opts[2].n ? o.opts[2] : o.legacy[2];
	if (value_casttype.n)
		buf_adds(out, import_casttype(w, value_casttype));
	else
		python_type_to(w, value_field, false, out);
	py_leave();
}

/* python_type, appended to out: the types of nested maps are not copied at every level. */
static void python_type_buf(PkgWriter *w, PbMsg *field, bool generic_container, Buf *out)
{
	FieldExtensions o = field_extensions(field);
	Str casttype = o.opts[0].n ? o.opts[0] : o.legacy[0];
	if (casttype.n) {
		buf_adds(out, import_casttype(w, casttype));
		return;
	}

	const char *field_type;
	py_enter(); /* the lambda of the mapping */
	switch (pb_int(field, FD_TYPE, TYPE_DOUBLE)) {
	case PT_DOUBLE: case PT_FLOAT:
		field_type = builtin(w, "float");
		break;
	case PT_INT64: case PT_UINT64: case PT_FIXED64: case PT_SFIXED64: case PT_SINT64: case PT_INT32:
	case PT_UINT32: case PT_FIXED32: case PT_SFIXED32: case PT_SINT32:
		field_type = builtin(w, "int");
		break;
	case PT_BOOL:
		field_type = builtin(w, "bool");
		break;
	case PT_STRING:
		field_type = builtin(w, "str");
		break;
	case PT_BYTES:
		field_type = builtin(w, "bytes");
		break;
	case PT_ENUM:
		field_type = import_message(w, xcat(str_of(field, FD_TYPE_NAME, "type_name"), ".ValueType"));
		break;
	case PT_MESSAGE: case PT_GROUP:
		field_type = import_message(w, str_of(field, FD_TYPE_NAME, "type_name"));
		break;
	default:
		fail("AssertionError: Unrecognized type: %lld", (long long)pb_int(field, FD_TYPE, TYPE_DOUBLE));
	}
	py_leave();

	if (!is_repeated(field)) {
		buf_adds(out, field_type);
		return;
	}

	if (is_scalar(field)) {
		const char *container = generic_container ? import_(w, "collections.abc", "Iterable")
							  : import_(w, "google.protobuf.internal.containers", "RepeatedScalarFieldContainer");
		buf_adds(out, xcat(container, "[", field_type, "]"));
		return;
	}

	const char *type_name = str_of(field, FD_TYPE_NAME, "type_name");
	PbMsg *msg = map_get(&w->descriptors->messages, type_name);
	if (!msg)
		key_error(type_name);
	if (pb_int(pb_msg(msg, DP_OPTIONS), MSGOPT_MAP_ENTRY, 0)) {
		if (pb_count(msg, DP_FIELD) < 2)
			fail("IndexError: list index out of range");
		const char *container;
		if (generic_container)
			container = import_(w, "collections.abc", "Mapping");
		else if (is_scalar(pb_msg_at(msg, DP_FIELD, 1)))
			container = import_(w, "google.protobuf.internal.containers", "ScalarMap");
		else
			container = import_(w, "google.protobuf.internal.containers", "MessageMap");
		/* a map entry whose value field is a repeated map entry recurses (py_frames) */
		buf_adds(out, container);
		buf_addc(out, '[');
		map_key_value_types(w, field, pb_msg_at(msg, DP_FIELD, 0), pb_msg_at(msg, DP_FIELD, 1), out);
		buf_addc(out, ']');
		return;
	}

	const char *container = generic_container ? import_(w, "collections.abc", "Iterable")
						  : import_(w, "google.protobuf.internal.containers", "RepeatedCompositeFieldContainer");
	buf_adds(out, xcat(container, "[", field_type, "]"));
}

static void python_type_to(PkgWriter *w, PbMsg *field, bool generic_container, Buf *out)
{
	py_enter();
	python_type_buf(w, field, generic_container, out);
	py_leave();
}

static const char *python_type(PkgWriter *w, PbMsg *field, bool generic_container)
{
	Buf b = {0};
	python_type_to(w, field, generic_container, &b);
	return buf_str(&b);
}

/* ---- writers ---- */

static void write_enum_values(PkgWriter *w, PbMsg *enum_proto, bool skip_enum_reserved, const char *value_type,
			      const char *scl_prefix, bool file_deprecated, bool class_attributes)
{
	for (int i = 0; i < pb_count(enum_proto, EDP_VALUE); i++) {
		PbMsg *val = pb_msg_at(enum_proto, EDP_VALUE, i);
		const char *name = name_of(val);
		if (skip_enum_reserved && proto_enum_reserved(name))
			continue;
		if (python_reserved(name))
			continue;
		const char *key = scl(scl_prefix, i);
		const char *number = xfmt("%lld", (long long)(int32_t)pb_int(val, EVDP_NUMBER, 0));
		bool deprecated = pb_int(pb_msg(val, EVDP_OPTIONS), ENUMVALOPT_DEPRECATED, 0) != 0;
		if (class_attributes && (deprecated || file_deprecated)) {
			wl(w, property(w));
			write_deprecation_warning(w, scl(scl(key, EVDP_OPTIONS), ENUMOPT_DEPRECATED),
						  deprecated ? "This enum value has been marked as deprecated using proto enum value options."
							     : "This enum value is within a file that has been marked as deprecated using proto file options.");
			wl(w, "def ", name, "(self) -> ", value_type, ": ", has_comments(w, key) ? "" : "...", "  # ", number);
			w->indent += 4;
			write_comments(w, key);
			w->indent -= 4;
		} else {
			wl(w, name, ": ", value_type, "  # ", number);
			write_comments(w, key);
		}
	}
}

static void write_module_attributes(PkgWriter *w)
{
	const char *fd_type = import_(w, "google.protobuf.descriptor", "FileDescriptor");
	wl(w, "DESCRIPTOR: ", fd_type);
	wl(w, "");
}

static void write_enums(PkgWriter *w, PbMsg *parent, uint32_t num, const char *prefix, const char *scl_prefix, bool file_deprecated)
{
	for (int i = 0; i < pb_count(parent, num); i++) {
		PbMsg *enum_proto = pb_msg_at(parent, num, i);
		const char *name = name_of(enum_proto);
		const char *class_name = python_reserved(name) ? xcat("_r_", name) : name;
		const char *value_type_fq = xcat(prefix, class_name, ".ValueType");
		const char *enum_helper_class = xcat("_", name);
		const char *value_type_helper_fq = xcat(prefix, enum_helper_class, ".ValueType");
		const char *etw_helper_class = xcat("_", name, "EnumTypeWrapper");
		const char *key = scl(scl_prefix, i);

		wl(w, "class ", enum_helper_class, ":");
		w->indent += 4;
		{
			const char *newtype = import_(w, "typing", "NewType");
			const char *int_ = builtin(w, "int");
			wl(w, "ValueType = ", newtype, "(\"ValueType\", ", int_, ")");
			wl(w, "V: ", import_(w, "typing_extensions", "TypeAlias"), " = ValueType  # noqa: Y015");
		}
		w->indent -= 4;
		wl(w, "");
		{
			const char *etw = import_(w, "google.protobuf.internal.enum_type_wrapper", "_EnumTypeWrapper");
			const char *type = builtin(w, "type");
			wl(w, "class ", etw_helper_class, "(", etw, "[", value_type_helper_fq, "], ", type, "):");
		}
		w->indent += 4;
		wl(w, "DESCRIPTOR: ", import_(w, "google.protobuf.descriptor", "EnumDescriptor"));
		write_enum_values(w, enum_proto, true, value_type_helper_fq, scl(key, EDP_VALUE), file_deprecated, true);
		w->indent -= 4;
		wl(w, "");

		bool deprecated = pb_int(pb_msg(enum_proto, EDP_OPTIONS), ENUMOPT_DEPRECATED, 0) != 0;
		if (deprecated || file_deprecated)
			write_deprecation_warning(w, scl(scl(key, EDP_OPTIONS), ENUMOPT_DEPRECATED),
						  deprecated ? "This enum has been marked as deprecated using proto enum options."
							     : "This enum is within a file that has been marked as deprecated using proto file options.");
		if (has_comments(w, key)) {
			wl(w, "class ", class_name, "(", enum_helper_class, ", metaclass=", etw_helper_class, "):");
			w->indent += 4;
			write_comments(w, key);
			w->indent -= 4;
			wl(w, "");
		} else {
			wl(w, "class ", class_name, "(", enum_helper_class, ", metaclass=", etw_helper_class, "): ...");
			if (!*prefix)
				wl(w, "");
		}

		write_enum_values(w, enum_proto, false, value_type_fq, scl(key, EDP_VALUE), file_deprecated, false);
		if (!*prefix && !w->readable_stubs)
			wl(w, mangle_global_identifier(class_name), ": ", import_(w, "typing_extensions", "TypeAlias"), " = ",
			   class_name, "  # noqa: Y015");
		wl(w, "");
	}
}

static int64_t get_field_presence(int64_t file_field_presence, PbMsg *field)
{
	PbMsg *features = pb_msg(pb_msg(field, FD_OPTIONS), FIELDOPT_FEATURES);
	if (pb_has(features, FEATURE_FIELD_PRESENCE))
		return pb_int(features, FEATURE_FIELD_PRESENCE, 0);
	return file_field_presence;
}

static const char *syntax(PkgWriter *w) { return str_of(w->fd, FDP_SYNTAX, "syntax"); }

static void write_extensions(PkgWriter *w, PbMsg *parent, uint32_t num, const char *scl_prefix)
{
	py_enter();
	for (int i = 0; i < pb_count(parent, num); i++)
		wl(w, upper(name_of(pb_msg_at(parent, num, i))), "_FIELD_NUMBER: ", builtin(w, "int"));
	for (int i = 0; i < pb_count(parent, num); i++) {
		PbMsg *ext = pb_msg_at(parent, num, i);
		const char *key = scl(scl_prefix, i);
		const char *efd = import_(w, "google.protobuf.internal.extension_dict", "_ExtensionFieldDescriptor");
		const char *extendee = import_message(w, str_of(ext, FD_EXTENDEE, "extendee"));
		const char *type = python_type(w, ext, false);
		wl(w, name_of(ext), ": ", efd, "[", extendee, ", ", type, "]");
		write_comments(w, key);
	}
	py_leave();
}

/* sorted(f'"{name}", b"{name}"' for name in names) joined with ", " */
static const char *literal_args(Vec *names)
{
	const char **v = xalloc(sizeof(char *) * (size_t)(names->n + 1));
	for (int i = 0; i < names->n; i++)
		v[i] = xcat("\"", names->v[i], "\", b\"", names->v[i], "\"");
	py_sort_cstrs(v, names->n);
	Buf b = {0};
	for (int i = 0; i < names->n; i++) {
		if (i)
			buf_adds(&b, ", ");
		buf_adds(&b, v[i]);
	}
	return buf_str(&b);
}

typedef struct {
	const char *name;
	Vec members;
} Oneof;

static int cmp_oneof(const void *a, const void *b)
{
	return strcmp((*(Oneof *const *)a)->name, (*(Oneof *const *)b)->name);
}

static void write_stringly_typed_fields(PkgWriter *w, PbMsg *desc, int64_t file_field_presence)
{
	const char *syn = syntax(w);
	bool editions = str_eq(syn, "editions");
	bool proto2 = str_eq(syn, "proto2") || !*syn;
	Vec hf = {0}, cf = {0};
	for (int i = 0; i < pb_count(desc, DP_FIELD); i++) {
		PbMsg *f = pb_msg_at(desc, DP_FIELD, i);
		if (pb_has(f, FD_ONEOF_INDEX) ||
		    (editions && get_field_presence(file_field_presence, f) != PRESENCE_IMPLICIT) ||
		    (!is_repeated(f) && (proto2 || pb_int(f, FD_TYPE, TYPE_DOUBLE) == TYPE_MESSAGE || pb_int(f, FD_PROTO3_OPTIONAL, 0))))
			vec_push(&hf, (void *)name_of(f));
		vec_push(&cf, (void *)name_of(f));
	}
	/* wo_fields: {oneof.name: [members]} (a repeated name keeps its first position, last members) */
	int noneofs = pb_count(desc, DP_ONEOF_DECL);
	Vec *members = xalloc(sizeof(Vec) * (size_t)(noneofs + 1));
	memset(members, 0, sizeof(Vec) * (size_t)(noneofs + 1));
	for (int i = 0; i < pb_count(desc, DP_FIELD); i++) {
		PbMsg *f = pb_msg_at(desc, DP_FIELD, i);
		int64_t idx = pb_int(f, FD_ONEOF_INDEX, 0);
		if (pb_has(f, FD_ONEOF_INDEX) && idx >= 0 && idx < noneofs)
			vec_push(&members[idx], (void *)name_of(f));
	}
	Map wo_map = {0};
	Vec wo = {0};
	for (int idx = 0; idx < noneofs; idx++) {
		const char *name = name_of(pb_msg_at(desc, DP_ONEOF_DECL, idx));
		Oneof *o = map_get(&wo_map, name);
		if (!o) {
			o = xalloc(sizeof *o);
			o->name = name;
			map_put(&wo_map, name, o);
			vec_push(&wo, o);
		}
		o->members = members[idx];
	}
	for (int i = 0; i < wo.n; i++) {
		vec_push(&hf, (void *)VEC_AT(wo, Oneof, i)->name);
		vec_push(&cf, (void *)VEC_AT(wo, Oneof, i)->name);
	}
	const char *hf_text = literal_args(&hf), *cf_text = literal_args(&cf);

	if (hf.n) {
		const char *ta = import_(w, "typing_extensions", "TypeAlias");
		const char *lit = import_(w, "typing", "Literal");
		wl(w, "_HasFieldArgType: ", ta, " = ", lit, "[", hf_text, "]  # noqa: Y015");
	} else {
		const char *ta = import_(w, "typing_extensions", "TypeAlias");
		const char *never = import_(w, "typing_extensions", "Never");
		wl(w, "_HasFieldArgType: ", ta, " = ", never, "  # noqa: Y015");
	}
	wl(w, "def HasField(self, field_name: _HasFieldArgType) -> ", builtin(w, "bool"), ": ...");
	if (cf.n) {
		const char *ta = import_(w, "typing_extensions", "TypeAlias");
		const char *lit = import_(w, "typing", "Literal");
		wl(w, "_ClearFieldArgType: ", ta, " = ", lit, "[", cf_text, "]  # noqa: Y015");
	} else {
		const char *ta = import_(w, "typing_extensions", "TypeAlias");
		const char *never = import_(w, "typing_extensions", "Never");
		wl(w, "_ClearFieldArgType: ", ta, " = ", never, "  # noqa: Y015");
	}
	wl(w, "def ClearField(self, field_name: _ClearFieldArgType) -> None: ...");

	if (wo.n) {
		Oneof **sorted = xalloc(sizeof(Oneof *) * (size_t)wo.n);
		for (int i = 0; i < wo.n; i++)
			sorted[i] = wo.v[i];
		qsort(sorted, (size_t)wo.n, sizeof *sorted, cmp_oneof);
		for (int i = 0; i < wo.n; i++) {
			Oneof *o = sorted[i];
			Buf members = {0};
			for (int j = 0; j < o->members.n; j++) {
				if (j)
					buf_adds(&members, ", ");
				buf_adds(&members, xcat("\"", (const char *)o->members.v[j], "\""));
			}
			const char *ta = import_(w, "typing_extensions", "TypeAlias");
			const char *lit = import_(w, "typing", "Literal");
			wl(w, "_WhichOneofReturnType_", o->name, ": ", ta, " = ", lit, "[", buf_str(&members), "]  # noqa: Y015");
			ta = import_(w, "typing_extensions", "TypeAlias");
			lit = import_(w, "typing", "Literal");
			wl(w, "_WhichOneofArgType_", o->name, ": ", ta, " = ", lit, "[\"", o->name, "\", b\"", o->name, "\"]  # noqa: Y015");
		}
		for (int i = 0; i < wo.n; i++) {
			Oneof *o = sorted[i];
			if (wo.n > 1)
				wl(w, "@", import_(w, "typing", "overload"));
			wl(w, "def WhichOneof(self, oneof_group: _WhichOneofArgType_", o->name, ") -> _WhichOneofReturnType_", o->name,
			   " | None: ...");
		}
	} else {
		wl(w, "def WhichOneof(self, oneof_group: ", import_(w, "typing_extensions", "Never"), ") -> None: ...");
	}
}

/* WKTBASES of google.protobuf.internal.well_known_types (protobuf 6.33.1) */
static const char *wkt_base(const char *full_name)
{
	static const char *const wkt[] = {"Any", "Duration", "FieldMask", "ListValue", "Struct", "Timestamp"};
	if (!has_prefix(full_name, "google.protobuf."))
		return NULL;
	for (size_t i = 0; i < sizeof wkt / sizeof *wkt; i++)
		if (str_eq(full_name + 16, wkt[i]))
			return wkt[i];
	return NULL;
}

static void write_messages(PkgWriter *w, PbMsg *parent, uint32_t num, const char *prefix, const char *scl_prefix,
			   bool file_deprecated, int64_t file_field_presence)
{
	py_enter();
	for (int i = 0; i < pb_count(parent, num); i++) {
		PbMsg *desc = pb_msg_at(parent, num, i);
		const char *name = name_of(desc);
		const char *qualified_name = xcat(prefix, name);

		const char *addl_base = "";
		const char *wkt = wkt_base(xcat(str_of(w->fd, FDP_PACKAGE, "package"), ".", name));
		if (wkt)
			addl_base = xcat(", ", import_(w, "google.protobuf.internal.well_known_types", wkt));

		const char *class_name = python_reserved(name) ? xcat("_r_", name) : name;
		const char *message_class = import_(w, "google.protobuf.message", "Message");
		bool deprecated = pb_int(pb_msg(desc, DP_OPTIONS), MSGOPT_DEPRECATED, 0) != 0;
		if (deprecated || file_deprecated)
			write_deprecation_warning(w, scl(scl(scl(scl_prefix, i), DP_OPTIONS), MSGOPT_DEPRECATED),
						  deprecated ? "This message has been marked as deprecated using proto message options."
							     : "This message is within a file that has been marked as deprecated using proto file options.");
		wl(w, "@", import_(w, "typing", "final"));
		wl(w, "class ", class_name, "(", message_class, addl_base, "):");
		w->indent += 4;
		const char *key = scl(scl_prefix, i);
		if (write_comments(w, key))
			wl(w, "");

		wl(w, "DESCRIPTOR: ", import_(w, "google.protobuf.descriptor", "Descriptor"));
		wl(w, "");

		const char *nested_prefix = xcat(qualified_name, ".");
		write_enums(w, desc, DP_ENUM_TYPE, nested_prefix, scl(key, DP_ENUM_TYPE), file_deprecated);
		write_messages(w, desc, DP_NESTED_TYPE, nested_prefix, scl(key, DP_NESTED_TYPE), file_deprecated, file_field_presence);

		int nfields = pb_count(desc, DP_FIELD);
		for (int j = 0; j < nfields; j++)
			wl(w, upper(name_of(pb_msg_at(desc, DP_FIELD, j))), "_FIELD_NUMBER: ", builtin(w, "int"));

		for (int idx = 0; idx < nfields; idx++) {
			PbMsg *field = pb_msg_at(desc, DP_FIELD, idx);
			const char *fname = name_of(field);
			if (python_reserved(fname))
				continue;
			const char *field_type = python_type(w, field, false);
			if (!(is_scalar(field) && !is_repeated(field)))
				continue;
			const char *scl_field = scl(scl(key, DP_FIELD), idx);
			const char *deprecation_scl_field = scl(scl(scl_field, FD_OPTIONS), FIELDOPT_DEPRECATED);
			bool fdep = field_deprecated(field);
			if (fdep || file_deprecated) {
				const char *fallback = fdep ? "This field has been marked as deprecated using proto field options."
							    : "This field is within a file that has been marked as deprecated using proto file options.";
				wl(w, property(w));
				write_deprecation_warning(w, deprecation_scl_field, fallback);
				wl(w, "def ", fname, "(self) -> ", field_type, ":", has_comments(w, scl_field) ? "" : " ...");
				if (has_comments(w, scl_field)) {
					w->indent += 4;
					write_comments(w, scl_field);
					w->indent -= 4;
					wl(w, "");
				}
				wl(w, "@", fname, ".setter");
				write_deprecation_warning(w, deprecation_scl_field, fallback);
				wl(w, "def ", fname, "(self, value: ", field_type, ") -> None:", has_comments(w, scl_field) ? "" : " ...");
				if (has_comments(w, scl_field)) {
					w->indent += 4;
					write_comments(w, scl_field);
					w->indent -= 4;
					wl(w, "");
				}
			} else {
				wl(w, fname, ": ", field_type);
				write_comments(w, scl_field);
			}
		}

		for (int idx = 0; idx < nfields; idx++) {
			PbMsg *field = pb_msg_at(desc, DP_FIELD, idx);
			const char *fname = name_of(field);
			if (python_reserved(fname))
				continue;
			const char *field_type = python_type(w, field, false);
			if (is_scalar(field) && !is_repeated(field))
				continue;
			const char *scl_field = scl(scl(key, DP_FIELD), idx);
			wl(w, property(w));
			bool fdep = field_deprecated(field);
			if (fdep || file_deprecated)
				write_deprecation_warning(w, scl(scl(scl_field, FD_OPTIONS), FIELDOPT_DEPRECATED),
							  fdep ? "This field has been marked as deprecated using proto field options."
							       : "This field is within a file that has been marked as deprecated using proto file options.");
			wl(w, "def ", fname, "(self) -> ", field_type, ":", has_comments(w, scl_field) ? "" : " ...");
			if (has_comments(w, scl_field)) {
				w->indent += 4;
				write_comments(w, scl_field);
				w->indent -= 4;
				wl(w, "");
			}
		}

		write_extensions(w, desc, DP_EXTENSION, scl(key, DP_EXTENSION));

		/* constructor */
		wl(w, "def __init__(");
		w->indent += 4;
		bool has_self = false;
		for (int j = 0; j < nfields; j++)
			if (str_eq(name_of(pb_msg_at(desc, DP_FIELD, j)), "self"))
				has_self = true;
		wl(w, has_self ? "self_,  # pyright: ignore[reportSelfClsParameterName]" : "self,");
		w->indent -= 4;
		w->indent += 4;
		int nconstructor = 0;
		for (int j = 0; j < nfields; j++)
			nconstructor += !python_reserved(name_of(pb_msg_at(desc, DP_FIELD, j)));
		if (nconstructor > 0)
			wl(w, "*,");
		const char *syn = syntax(w);
		for (int j = 0; j < nfields; j++) {
			PbMsg *field = pb_msg_at(desc, DP_FIELD, j);
			const char *fname = name_of(field);
			if (python_reserved(fname))
				continue;
			bool implicit_presence = get_field_presence(file_field_presence, field) == PRESENCE_IMPLICIT;
			const char *field_type = python_type(w, field, true);
			if ((implicit_presence && str_eq(syn, "editions")) ||
			    (str_eq(syn, "proto3") && is_scalar(field) && !is_repeated(field) && !w->relax_strict_optional_primitives &&
			     !pb_int(field, FD_PROTO3_OPTIONAL, 0)))
				wl(w, fname, ": ", field_type, " = ...,");
			else
				wl(w, fname, ": ", field_type, " | None = ...,");
		}
		w->indent -= 4;
		wl(w, ") -> None: ...");

		write_stringly_typed_fields(w, desc, file_field_presence);
		w->indent -= 4;

		if (!*prefix && !w->readable_stubs) {
			wl(w, "");
			wl(w, mangle_global_identifier(class_name), ": ", import_(w, "typing_extensions", "TypeAlias"), " = ", class_name,
			   "  # noqa: Y015");
		}
		wl(w, "");
	}
	py_leave();
}

/* ---- gRPC (protoc-gen-mypy_grpc) ---- */

enum { SDP_OPTIONS = 3, SERVICEOPT_DEPRECATED = 33, METHODOPT_DEPRECATED = 33 };

static bool supports_sync(MypyGrpcType t) { return t == MYPY_GRPC_SYNC || t == MYPY_GRPC_BOTH; }

static bool supports_async(MypyGrpcType t) { return t == MYPY_GRPC_ASYNC || t == MYPY_GRPC_BOTH; }

static bool client_streaming(const PbMsg *m) { return pb_int(m, MDP_CLIENT_STREAMING, 0) != 0; }

static bool server_streaming(const PbMsg *m) { return pb_int(m, MDP_SERVER_STREAMING, 0) != 0; }

static bool method_deprecated(const PbMsg *m) { return pb_int(pb_msg(m, MDP_OPTIONS), METHODOPT_DEPRECATED, 0) != 0; }

static bool service_deprecated(const PbMsg *s) { return pb_int(pb_msg(s, SDP_OPTIONS), SERVICEOPT_DEPRECATED, 0) != 0; }

/* The methods of a service whose names are not Python keywords: whether there is one. */
static bool has_methods(PbMsg *service)
{
	for (int i = 0; i < pb_count(service, SDP_METHOD); i++)
		if (!python_reserved(name_of(pb_msg_at(service, SDP_METHOD, i))))
			return true;
	return false;
}

static const char *callable_type(PkgWriter *w, PbMsg *method, bool is_async)
{
	const char *module = is_async ? "grpc.aio" : "grpc";
	if (client_streaming(method))
		return import_(w, module, server_streaming(method) ? "StreamStreamMultiCallable" : "StreamUnaryMultiCallable");
	return import_(w, module, server_streaming(method) ? "UnaryStreamMultiCallable" : "UnaryUnaryMultiCallable");
}

static const char *input_type(PkgWriter *w, PbMsg *method) { return import_message(w, str_of(method, MDP_INPUT_TYPE, "input_type")); }

static const char *output_type(PkgWriter *w, PbMsg *method) { return import_message(w, str_of(method, MDP_OUTPUT_TYPE, "output_type")); }

/* f"{import}[{inner}]" */
static const char *generic(PkgWriter *w, const char *path, const char *name, const char *inner)
{
	const char *t = import_(w, path, name);
	return xcat(t, "[", inner, "]");
}

static const char *servicer_input_type(PkgWriter *w, PbMsg *method)
{
	const char *result = input_type(w, method);
	if (client_streaming(method)) {
		/* See write_grpc_iterator_type(). */
		if (w->grpc_type == MYPY_GRPC_SYNC)
			result = generic(w, "collections.abc", "Iterator", result);
		else if (w->grpc_type == MYPY_GRPC_ASYNC)
			result = generic(w, "collections.abc", "AsyncIterator", result);
		else
			result = xcat("_MaybeAsyncIterator[", result, "]");
	}
	return result;
}

static const char *servicer_output_type(PkgWriter *w, PbMsg *method)
{
	const char *msg = output_type(w, method);
	/* the imports happen in the order of the f-strings: Union, the sync type, the async type */
	bool ss = server_streaming(method);
	const char *u = NULL;
	if (w->grpc_type == MYPY_GRPC_BOTH)
		u = import_(w, "typing", "Union");
	const char *sync = NULL, *a_sync = NULL;
	if (w->grpc_type != MYPY_GRPC_ASYNC)
		sync = ss ? generic(w, "collections.abc", "Iterator", msg) : msg;
	if (w->grpc_type != MYPY_GRPC_SYNC)
		a_sync = generic(w, "collections.abc", ss ? "AsyncIterator" : "Awaitable", msg);
	if (w->grpc_type == MYPY_GRPC_SYNC)
		return sync;
	if (w->grpc_type == MYPY_GRPC_ASYNC)
		return a_sync;
	return xcat(u, "[", sync, ", ", a_sync, "]");
}

static void write_grpc_iterator_type(PkgWriter *w)
{
	if (w->grpc_type == MYPY_GRPC_SYNC) {
		import_(w, "collections.abc", "Iterator");
	} else if (w->grpc_type == MYPY_GRPC_ASYNC) {
		import_(w, "collections.abc", "AsyncIterator");
	} else {
		/* _MaybeAsyncIterator[Req] is supertyped by Iterator[Req] and AsyncIterator[Req]. */
		wl(w, "_T = ", import_(w, "typing", "TypeVar"), "(\"_T\")");
		wl(w, "");
		const char *ait = import_(w, "collections.abc", "AsyncIterator");
		const char *it = import_(w, "collections.abc", "Iterator");
		const char *meta = import_(w, "abc", "ABCMeta");
		wl(w, "class _MaybeAsyncIterator(", ait, "[_T], ", it, "[_T], metaclass=", meta, "): ...");
	}
	wl(w, "");
}

static const char *get_servicer_context_type(PkgWriter *w, const char *input, const char *output)
{
	if (w->grpc_type == MYPY_GRPC_ASYNC)
		return xcat(import_(w, "grpc.aio", "ServicerContext"), "[", input, ", ", output, "]");
	if (w->grpc_type == MYPY_GRPC_SYNC)
		return import_(w, "grpc", "ServicerContext");
	return "_ServicerContext"; /* BOTH mode uses _ServicerContext union class */
}

static void write_grpc_servicer_context(PkgWriter *w)
{
	if (w->grpc_type != MYPY_GRPC_BOTH)
		return;
	const char *sync = import_(w, "grpc", "ServicerContext");
	const char *a_sync = import_(w, "grpc.aio", "ServicerContext");
	wl(w, "class _ServicerContext(", sync, ", ", a_sync, "):  # type: ignore[misc, type-arg]");
	w->indent += 4;
	wl(w, "...");
	w->indent -= 4;
	wl(w, "");
}

static const char *grpc_type_str(PkgWriter *w, PbMsg *method, bool is_async)
{
	const char *c = callable_type(w, method, is_async);
	const char *in = input_type(w, method);
	const char *out = output_type(w, method);
	return xcat(c, "[", in, ", ", out, "]");
}

static void write_grpc_stub_methods(PkgWriter *w, PbMsg *service, const char *scl_prefix, bool is_async, bool file_deprecated,
				    bool both, bool ignore_type_error)
{
	if (!has_methods(service))
		return;
	for (int i = 0; i < pb_count(service, SDP_METHOD); i++) {
		PbMsg *method = pb_msg_at(service, SDP_METHOD, i);
		const char *name = name_of(method);
		if (python_reserved(name))
			continue;
		const char *key = scl(scl(scl_prefix, SDP_METHOD), i);
		bool is_deprecated = method_deprecated(method);
		bool comments = has_comments(w, key);

		const char *type_annotation;
		if (both) {
			const char *u = import_(w, "typing", "Union");
			const char *sync = grpc_type_str(w, method, false);
			const char *a_sync = grpc_type_str(w, method, true);
			type_annotation = xcat(u, "[", sync, ", ", a_sync, "]");
		} else {
			type_annotation = grpc_type_str(w, method, is_async);
		}

		if (is_deprecated || file_deprecated) {
			wl(w, property(w));
			write_deprecation_warning(w, scl(scl(key, MDP_OPTIONS), METHODOPT_DEPRECATED),
						  is_deprecated ? "This method has been marked as deprecated using proto method options."
								: "This method is within a file that has been marked as deprecated using proto file options.");
			wl(w, "def ", name, "(self) -> ", type_annotation, ":", comments ? "" : " ...",
			   ignore_type_error ? "  # type: ignore[override]" : "");
			if (comments) {
				w->indent += 4;
				if (!write_comments(w, key))
					wl(w, "...");
				w->indent -= 4;
			}
		} else {
			wl(w, name, ": ", type_annotation, ignore_type_error ? "  # type: ignore[assignment]" : "");
			write_comments(w, key);
		}
	}
}

static void write_grpc_methods(PkgWriter *w, PbMsg *service, const char *scl_prefix, bool file_deprecated)
{
	if (!has_methods(service)) {
		wl(w, "...");
		wl(w, "");
	}
	for (int i = 0; i < pb_count(service, SDP_METHOD); i++) {
		PbMsg *method = pb_msg_at(service, SDP_METHOD, i);
		const char *name = name_of(method);
		if (python_reserved(name))
			continue;
		const char *key = scl(scl(scl_prefix, SDP_METHOD), i);
		const char *in = servicer_input_type(w, method);
		const char *out = servicer_output_type(w, method);

		bool deprecated = method_deprecated(method);
		if (deprecated || file_deprecated)
			write_deprecation_warning(w, key,
						  deprecated ? "This method has been marked as deprecated using proto method options."
							     : "This method is within a file that has been marked as deprecated using proto file options.");
		if (!w->generate_concrete_servicer_stubs)
			wl(w, "@", import_(w, "abc", "abstractmethod"));
		wl(w, "def ", name, "(");
		w->indent += 4;
		wl(w, "self,");
		wl(w, client_streaming(method) ? "request_iterator" : "request", ": ", in, ",");
		wl(w, "context: ", get_servicer_context_type(w, in, out), ",");
		w->indent -= 4;
		wl(w, ") -> ", out, ":", has_comments(w, key) ? "" : " ...");
		if (has_comments(w, key)) {
			w->indent += 4;
			if (!write_comments(w, key))
				wl(w, "...");
			w->indent -= 4;
		}
		wl(w, "");
	}
}

static const char *make_server_type(PkgWriter *w)
{
	if (w->grpc_type == MYPY_GRPC_BOTH) {
		const char *u = import_(w, "typing", "Union");
		const char *server = import_(w, "grpc", "Server");
		const char *aserver = import_(w, "grpc.aio", "Server");
		return xcat(u, "[", server, ", ", aserver, "]");
	}
	if (w->grpc_type == MYPY_GRPC_ASYNC)
		return import_(w, "grpc.aio", "Server");
	return import_(w, "grpc", "Server");
}

static void write_grpc_services(PkgWriter *w, PbMsg *fd, const char *scl_prefix, bool file_deprecated)
{
	wl(w, "GRPC_GENERATED_VERSION: str");
	wl(w, "GRPC_VERSION: str");
	wl(w, "");
	for (int i = 0; i < pb_count(fd, FDP_SERVICE); i++) {
		PbMsg *service = pb_msg_at(fd, FDP_SERVICE, i);
		const char *name = name_of(service);
		if (python_reserved(name))
			continue;

		const char *key = scl(scl_prefix, i);
		const char *class_name = xcat(name, "Stub");
		const char *async_class_alias = xcat(name, "AsyncStub");
		bool deprecated = service_deprecated(service);
		const char *deprecated_key = scl(scl(key, SDP_OPTIONS), SERVICEOPT_DEPRECATED);

		/* The stub client */
		if (supports_sync(w->grpc_type)) {
			if (deprecated || file_deprecated)
				write_deprecation_warning(w, deprecated_key,
							  deprecated ? "This stub has been marked as deprecated using proto service options."
								     : "This stub is within a file that has been marked as deprecated using proto file options.");
			wl(w, "class ", class_name, ":");
			w->indent += 4;
			if (write_comments(w, key))
				wl(w, "");
			if (w->grpc_type == MYPY_GRPC_BOTH) {
				/* sync overload */
				wl(w, "@", import_(w, "typing", "overload"));
				const char *channel = import_(w, "grpc", "Channel");
				const char *self = import_(w, "typing_extensions", "Self");
				wl(w, "def __new__(cls, channel: ", channel, ") -> ", self, ": ...");
				/* async overload */
				wl(w, "@", import_(w, "typing", "overload"));
				wl(w, "def __new__(cls, channel: ", import_(w, "grpc.aio", "Channel"), ") -> ", async_class_alias, ": ...");
			} else {
				wl(w, "def __init__(self, channel: ", import_(w, "grpc", "Channel"), ") -> None: ...");
			}
			write_grpc_stub_methods(w, service, key, false, file_deprecated, false, false);
			wl(w, "");
			w->indent -= 4;
		}

		/* AsyncStub */
		if (supports_async(w->grpc_type)) {
			if (deprecated)
				write_deprecation_warning(w, deprecated_key, "This stub has been marked as deprecated using proto service options.");
			if (w->grpc_type == MYPY_GRPC_BOTH) {
				/* AsyncStub inherits from Stub */
				wl(w, "@", import_(w, "typing", "type_check_only"));
				wl(w, "class ", async_class_alias, "(", class_name, "):");
				w->indent += 4;
				if (write_comments(w, key))
					wl(w, "");
				wl(w, "def __init__(self, channel: ", import_(w, "grpc.aio", "Channel"), ") -> None: ...");
				write_grpc_stub_methods(w, service, key, true, file_deprecated, false, true);
				w->indent -= 4;
			} else {
				/* ASYNC only: the Stub name, since there's only one type */
				wl(w, "class ", class_name, ":");
				w->indent += 4;
				if (write_comments(w, key))
					wl(w, "");
				wl(w, "def __init__(self, channel: ", import_(w, "grpc.aio", "Channel"), ") -> None: ...");
				write_grpc_stub_methods(w, service, key, true, file_deprecated, false, false);
				w->indent -= 4;
			}
			wl(w, "");
		}

		/* The service definition interface */
		const char *fallback = deprecated ? "This servicer has been marked as deprecated using proto service options."
						  : "This servicer is within a file that has been marked as deprecated using proto file options.";
		if (deprecated || file_deprecated)
			write_deprecation_warning(w, deprecated_key, fallback);
		if (!w->generate_concrete_servicer_stubs)
			wl(w, "class ", name, "Servicer(metaclass=", import_(w, "abc", "ABCMeta"), "):");
		else
			wl(w, "class ", name, "Servicer:");
		w->indent += 4;
		if (write_comments(w, key))
			wl(w, "");
		write_grpc_methods(w, service, key, file_deprecated);
		w->indent -= 4;
		if (deprecated || file_deprecated)
			write_deprecation_warning(w, deprecated_key, fallback);

		const char *server = make_server_type(w);
		wl(w, "def add_", name, "Servicer_to_server(servicer: ", name, "Servicer, server: ", server, ") -> None: ...");
		wl(w, "");
	}
}

static const char HEADER[] = "\n@generated by mypy-protobuf.  Do not edit manually!\nisort:skip_file\n";

static const char *version_repr(Version v) { return xfmt("(%d, %d)", v.major, v.minor); }

static Str pkg_writer_write(PkgWriter *w)
{
	Lines saved = w->lines;
	memset(&w->lines, 0, sizeof w->lines);

	/* module docstring: comments before the package or syntax statement */
	if (!write_comments(w, scl("", FDP_PACKAGE)))
		write_comments(w, scl("", FDP_SYNTAX));

	if (w->lines.n) {
		Str first = w->lines.v[0];
		if (first.n < 3 || memcmp(first.p, "\"\"\"", 3))
			fail("AssertionError");
		Buf b = {0};
		buf_adds(&b, "\"\"\"");
		buf_adds(&b, HEADER);
		buf_add(&b, first.p + 3, first.n - 3);
		w->lines.v[0] = buf_view(&b);
		wl(w, "");
	} else {
		wl(w, "\"\"\"", HEADER, "\"\"\"\n");
	}

	for (int i = 0; i < pb_count(w->fd, FDP_PUBLIC_DEPENDENCY); i++) {
		int64_t idx = (int32_t)pb_int_at(w->fd, FDP_PUBLIC_DEPENDENCY, i);
		int ndeps = pb_count(w->fd, FDP_DEPENDENCY);
		if (idx < 0)
			idx += ndeps; /* Python indexing */
		if (idx < 0 || idx >= ndeps)
			fail("IndexError: list index out of range");
		const char *reexport_file = checked(pb_bytes_at(w->fd, FDP_DEPENDENCY, (int)idx), "dependency");
		PbMsg *reexport_fd = map_get(&w->descriptors->files, reexport_file);
		if (!reexport_fd)
			key_error(reexport_file);
		const char *reexport_imp = xcat(replace_char(replace_char(drop6(reexport_file), '-', '_'), '/', '.'), "_pb2");
		Vec names = {0};
		for (int j = 0; j < pb_count(reexport_fd, FDP_MESSAGE_TYPE); j++)
			vec_push(&names, (void *)name_of(pb_msg_at(reexport_fd, FDP_MESSAGE_TYPE, j)));
		for (int j = 0; j < pb_count(reexport_fd, FDP_ENUM_TYPE); j++)
			vec_push(&names, (void *)name_of(pb_msg_at(reexport_fd, FDP_ENUM_TYPE, j)));
		for (int j = 0; j < pb_count(reexport_fd, FDP_ENUM_TYPE); j++) {
			PbMsg *e = pb_msg_at(reexport_fd, FDP_ENUM_TYPE, j);
			for (int k = 0; k < pb_count(e, EDP_VALUE); k++)
				vec_push(&names, (void *)name_of(pb_msg_at(e, EDP_VALUE, k)));
		}
		for (int j = 0; j < pb_count(reexport_fd, FDP_EXTENSION); j++)
			vec_push(&names, (void *)name_of(pb_msg_at(reexport_fd, FDP_EXTENSION, j)));
		for (int j = 0; j < names.n; j++)
			add_import(&w->imports, xcat(reexport_imp, ".", (const char *)names.v[j]), names.v[j], false);
	}

	if (w->typing_extensions_min.major || w->deprecated_min.major)
		add_import(&w->imports, "sys", NULL, true);

	Imports *im = &w->imports;
	const char **statements = xalloc(sizeof(char *) * (size_t)(im->paths.n + 1));
	for (int i = 0; i < im->paths.n; i++) {
		const Import *imp = map_get(&im->imports, im->paths.v[i]);
		if (!imp->frm)
			statements[i] = imp->alias ? xcat("import ", imp->original, " as ", imp->alias) : xcat("import ", imp->original);
		else
			statements[i] = xcat("from ", imp->frm, " import ", imp->original, " as ", imp->alias ? imp->alias : "None");
	}
	py_sort_cstrs(statements, im->paths.n);
	for (int i = 0; i < im->paths.n; i++)
		wl(w, statements[i]);

	Buf typing = {0};
	for (int i = 0; i < im->typing_names.n; i++) {
		const char *name = im->typing_names.v[i];
		if (i)
			buf_adds(&typing, ", ");
		buf_adds(&typing, xcat(name, " as ", (const char *)map_get(&im->typing, name)));
	}
	if (w->typing_extensions_min.major) {
		wl(w, "");
		wl(w, "if sys.version_info >= ", version_repr(w->typing_extensions_min), ":");
		wl(w, "    from typing import ", buf_str(&typing));
		wl(w, "else:");
		wl(w, "    from typing_extensions import ", buf_str(&typing));
	}
	if (w->deprecated_min.major) {
		wl(w, "");
		wl(w, "if sys.version_info >= ", version_repr(w->deprecated_min), ":");
		wl(w, "    from warnings import deprecated as ", DEPRECATED_NAME);
		wl(w, "else:");
		wl(w, "    from typing_extensions import deprecated as ", DEPRECATED_NAME);
	}
	wl(w, "");

	for (int i = 0; i < saved.n; i++)
		lines_push(&w->lines, saved.v[i]);

	Buf content = {0};
	for (int i = 0; i < w->lines.n; i++) {
		if (i)
			buf_addc(&content, '\n');
		buf_add_str(&content, w->lines.v[i]);
	}
	if (!content.n || content.p[content.n - 1] != '\n')
		buf_addc(&content, '\n');
	return buf_view(&content);
}

/* fd.name[:-6].replace("-", "_").replace(".", "/") + suffix, after the asserts */
static const char *output_name(PbMsg *fd, const char *suffix)
{
	const char *name = name_of(fd);
	if (!has_suffix(name, ".proto"))
		fail("AssertionError");
	return xcat(replace_char(replace_char(drop6(name), '-', '_'), '.', '/'), suffix);
}

static void add_file(const char *out_name, Str content, Buf *files)
{
	Buf file = {0};
	pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(out_name));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, content);
	pb_put_bytes(files, CGRESP_FILE, buf_view(&file));
}

/* generate_mypy_stubs: appends the files to the response. */
void mypy_generate_stubs(Descriptors *d, const MypyOptions *o, Buf *files)
{
	for (int i = 0; i < d->to_generate.n; i++) {
		PbMsg *fd = d->to_generate.v[i];
		py_frames = 3; /* <module>, main, generate_mypy_stubs */
		PkgWriter w;
		pkg_writer_init(&w, fd, d, o, false, MYPY_GRPC_BOTH);
		PbMsg *file_options = pb_msg(fd, FDP_OPTIONS);
		bool file_deprecated = pb_int(file_options, FILEOPT_DEPRECATED, 0) != 0;
		int64_t file_presence = pb_int(pb_msg(file_options, FILEOPT_FEATURES), FEATURE_FIELD_PRESENCE, PRESENCE_UNKNOWN);

		write_module_attributes(&w);
		write_enums(&w, fd, FDP_ENUM_TYPE, "", scl("", FDP_ENUM_TYPE), file_deprecated);
		write_messages(&w, fd, FDP_MESSAGE_TYPE, "", scl("", FDP_MESSAGE_TYPE), file_deprecated, file_presence);
		write_extensions(&w, fd, FDP_EXTENSION, scl("", FDP_EXTENSION));
		const char *name = output_name(fd, "_pb2.pyi");
		add_file(name, pkg_writer_write(&w), files);
	}
}

/* generate_mypy_grpc_stubs: appends the files to the response. */
void mypy_generate_grpc_stubs(Descriptors *d, const MypyOptions *o, MypyGrpcType type, Buf *files)
{
	for (int i = 0; i < d->to_generate.n; i++) {
		PbMsg *fd = d->to_generate.v[i];
		py_frames = 3; /* <module>, grpc, generate_mypy_grpc_stubs */
		PkgWriter w;
		pkg_writer_init(&w, fd, d, o, true, type);
		write_grpc_iterator_type(&w);
		write_grpc_servicer_context(&w);
		bool file_deprecated = pb_int(pb_msg(fd, FDP_OPTIONS), FILEOPT_DEPRECATED, 0) != 0;
		write_grpc_services(&w, fd, scl("", FDP_SERVICE), file_deprecated);
		const char *name = output_name(fd, "_pb2_grpc.pyi");
		add_file(name, pkg_writer_write(&w), files);
	}
}

/* ---- main() and grpc() ---- */

static bool contains(Str s, const char *sub)
{
	size_t n = strlen(sub);
	for (size_t i = 0; i + n <= s.n; i++)
		if (!memcmp(s.p + i, sub, n))
			return true;
	return false;
}

COLD MypyOptions mypy_options(Str parameter)
{
	/* upb returns a parameter that is not valid UTF-8 as bytes */
	if (!valid_utf8(parameter))
		fail("TypeError: a bytes-like object is required, not 'str'");
	return (MypyOptions){
		contains(parameter, "readable_stubs"),
		contains(parameter, "relax_strict_optional_primitives"),
		contains(parameter, "use_default_deprecation_warnings"),
		contains(parameter, "generate_concrete_servicer_stubs"),
	};
}

COLD bool mypy_grpc_type_from_parameter(Str parameter, MypyGrpcType *type)
{
	bool has_sync = contains(parameter, "only_sync"), has_async = contains(parameter, "only_async");
	if (has_sync && has_async)
		return false;
	*type = has_sync ? MYPY_GRPC_SYNC : has_async ? MYPY_GRPC_ASYNC : MYPY_GRPC_BOTH;
	return true;
}

COLD void mypy_response_features(Buf *out)
{
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3); /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 900); /* EDITION_LEGACY */
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 1001); /* EDITION_2024 */
}
