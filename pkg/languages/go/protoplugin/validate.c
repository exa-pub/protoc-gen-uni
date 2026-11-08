/* Copyright 2024-2025 Buf Technologies, Inc.
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

/* Ported from github.com/bufbuild/protoplugin@750e09ce93e1 validate.go and errors.go (the
 * request and response validation) and request.go (NewRequest). */
#include "internal.h"

#include <string.h>

/* ---- path/filepath on Unix ---- */

/* filepath.Clean (= path.Clean on Unix; filepath.ToSlash is a no-op there). */
COLD Str gopp_path_clean(Str path)
{
	size_t n = path.n;
	const char *p = path.p;
	if (n == 0)
		return STR_LIT(".");
	bool rooted = p[0] == '/';
	char *out = xalloc(n + 2);
	size_t w = 0, r = 0, dotdot = 0;
	if (rooted) {
		out[w++] = '/';
		r = 1, dotdot = 1;
	}
	while (r < n) {
		if (p[r] == '/') {
			r++; /* empty path element */
		} else if (p[r] == '.' && (r + 1 == n || p[r + 1] == '/')) {
			r++; /* . element */
		} else if (p[r] == '.' && r + 1 < n && p[r + 1] == '.' && (r + 2 == n || p[r + 2] == '/')) {
			/* .. element: remove to last / */
			r += 2;
			if (w > dotdot) {
				w--;
				while (w > dotdot && out[w] != '/')
					w--;
			} else if (!rooted) {
				if (w > 0)
					out[w++] = '/';
				out[w++] = '.';
				out[w++] = '.';
				dotdot = w;
			}
		} else {
			if ((rooted && w != 1) || (!rooted && w != 0))
				out[w++] = '/';
			for (; r < n && p[r] != '/'; r++)
				out[w++] = p[r];
		}
	}
	if (w == 0)
		return STR_LIT(".");
	return (Str){out, w};
}

/* filepath.Ext */
static COLD Str path_ext(Str path)
{
	for (size_t i = path.n; i > 0 && path.p[i - 1] != '/'; i--)
		if (path.p[i - 1] == '.')
			return (Str){path.p + i - 1, path.n - i + 1};
	return (Str){"", 0};
}

/* A key for a Map of Go strings (which may contain NUL): strconv.Quote is injective. */
COLD const char *gopp_key(Str s) { return quote_go(s); }

/* ---- errors.go ---- */

COLD const char *gopp_unnormalized_name_error(Str name, Str normalized, bool is_warning)
{
	return xcat("path ", quote_go(name), " is not equal to ", quote_go(normalized),
		    ", and therefore does not conform to the Protobuf generation specification. The path must be "
		    "non-empty, relative, use \"/\" instead of \"\\\" as the path separator, and not use \".\" or "
		    "\"..\" as part of the path.",
		    is_warning ? " Generation will continue without error here, but please raise an issue with the "
				 "maintainer of the plugin and reference "
				 "https://github.com/protocolbuffers/protobuf/blob/95e6c5b4746dd7474d540ce4fb375e3f79a086f8/"
				 "src/google/protobuf/compiler/plugin.proto#L122"
			       : "");
}

COLD const char *gopp_duplicate_name_error(Str name, bool is_warning)
{
	return xcat("duplicate generated file name ", quote_go(name), ".",
		    is_warning ? " Generation will continue without error here and drop the second occurrence of this "
				 "file, but please raise an issue with the maintainer of the plugin."
			       : "");
}

/* ---- validate.go: paths ---- */

/* validateAndNormalizePath validates that the path is non-empty, relative, and uses '/' as the
 * path separator, and returns filepath.ToSlash(filepath.Clean(path)). */
COLD const char *gopp_validate_and_normalize_path(const char *field, Str path, Str *normalized)
{
	if (path.n == 0)
		return xcat(field, ": path was empty");
	Str np = gopp_path_clean(path);
	if (np.p[0] == '/')
		return xcat(field, ": path ", quote_go(np), " should be relative");
	/* https://github.com/bufbuild/buf/issues/51 */
	if (str_has_prefix(np, STR_LIT("../")))
		return xcat(field, ": path ", quote_go(np), " should not jump context");
	*normalized = np;
	return NULL;
}

/* validateAndCheckPathIsNormalized */
static COLD const char *check_path_is_normalized(const char *field, Str path)
{
	Str np;
	const char *err = gopp_validate_and_normalize_path(field, path, &np);
	if (err)
		return err;
	if (!str_equal(path, np))
		return xcat(field, ": path ", quote_go(path), " to be given as ", quote_go(np));
	return NULL;
}

/* validateAndCheckProtoPathIsNormalized */
static COLD const char *check_proto_path_is_normalized(const char *field, Str path)
{
	const char *err = check_path_is_normalized(field, path);
	if (err)
		return err;
	if (!str_eqc(path_ext(path), ".proto"))
		return xcat(field, ": path ", quote_go(path), " should have the .proto file extension");
	return NULL;
}

/* validateAndCheckProtoPathsAreNormalized validates the repeated string field num of m and
 * ensures that the paths are unique. */
static COLD const char *check_proto_paths_are_normalized(const char *field, const PbMsg *m, uint32_t num)
{
	Map seen = {0};
	int n = pb_count(m, num);
	for (int i = 0; i < n; i++) {
		Str path = pb_bytes_at(m, num, i);
		const char *err = check_proto_path_is_normalized(field, path);
		if (err)
			return err;
		const char *key = gopp_key(path);
		if (map_has(&seen, key))
			return xcat(field, ": duplicate path ", quote_go(path));
		map_put(&seen, key, (void *)key);
	}
	return NULL;
}

/* ---- validate.go: request ---- */

/* CodeGeneratorRequest, FileDescriptorProto and Version field numbers. */
enum { REQ_FILE_TO_GENERATE = 1, REQ_COMPILER_VERSION = 3, REQ_PROTO_FILE = 15, REQ_SOURCE_FILE_DESCRIPTORS = 17 };
enum { FILE_NAME = 1, FILE_DEPENDENCY = 3 };
enum { VERSION_MAJOR = 1, VERSION_MINOR = 2, VERSION_PATCH = 3 };

/* validateFileDescriptorProto */
static COLD const char *validate_file_descriptor_proto(const char *field, const PbMsg *fd)
{
	const char *err = check_proto_path_is_normalized(xcat(field, ".name"), pb_bytes(fd, FILE_NAME));
	if (err)
		return err;
	return check_proto_paths_are_normalized(xcat(field, ".dependency"), fd, FILE_DEPENDENCY);
}

/* validateCodeGeneratorRequestFileDescriptorProtos: equal is true for source_file_descriptors
 * (the names must equal file_to_generate), false for proto_file (a superset). */
static COLD const char *validate_file_descriptor_protos(const char *field, const PbMsg *req, uint32_t num, bool equal)
{
	Map names = {0};
	int n = pb_count(req, num);
	for (int i = 0; i < n; i++) {
		const PbMsg *fd = pb_msg_at(req, num, i);
		const char *err = validate_file_descriptor_proto(field, fd);
		if (err)
			return err;
		Str name = pb_bytes(fd, FILE_NAME);
		const char *key = gopp_key(name);
		if (map_has(&names, key))
			return xcat(field, ": duplicate path ", quote_go(name));
		map_put(&names, key, (void *)key);
	}
	Map to_generate = {0};
	int ng = pb_count(req, REQ_FILE_TO_GENERATE);
	for (int i = 0; i < ng; i++) {
		Str f = pb_bytes_at(req, REQ_FILE_TO_GENERATE, i);
		const char *key = gopp_key(f);
		if (!map_has(&names, key))
			return xcat("file_to_generate: path ", quote_go(f), " is not contained within ", field);
		map_put(&to_generate, key, (void *)key);
	}
	if (equal) {
		/* Upstream ranges over a Go map here (random order); the first name in request order
		 * is reported. */
		for (int i = 0; i < n; i++) {
			Str name = pb_bytes(pb_msg_at(req, num, i), FILE_NAME);
			if (!map_has(&to_generate, gopp_key(name)))
				return xcat(field, ": path ", quote_go(name), " is not contained within file_to_generate");
		}
	}
	return NULL;
}

/* validateCompilerVersion */
static COLD const char *validate_compiler_version(const PbMsg *v)
{
	static const struct {
		uint32_t num;
		const char *name;
	} parts[] = {{VERSION_MAJOR, "major"}, {VERSION_MINOR, "minor"}, {VERSION_PATCH, "patch"}};
	for (int i = 0; i < 3; i++) {
		int32_t x = (int32_t)pb_int(v, parts[i].num, 0);
		if (x < 0)
			return xcat(parts[i].name, ": negative: ", xfmt("%d", (int)x));
	}
	return NULL;
}

static COLD const char *validate_request(const PbMsg *req)
{
	if (pb_count(req, REQ_PROTO_FILE) == 0)
		return "proto_file: empty";
	if (pb_count(req, REQ_FILE_TO_GENERATE) == 0)
		return "file_to_generate: empty";
	const char *err = check_proto_paths_are_normalized("file_to_generate", req, REQ_FILE_TO_GENERATE);
	if (!err)
		err = validate_file_descriptor_protos("proto_file", req, REQ_PROTO_FILE, false);
	if (!err && pb_count(req, REQ_SOURCE_FILE_DESCRIPTORS) > 0)
		err = validate_file_descriptor_protos("source_file_descriptors", req, REQ_SOURCE_FILE_DESCRIPTORS, true);
	if (!err && pb_has(req, REQ_COMPILER_VERSION)) {
		err = validate_compiler_version(pb_msg(req, REQ_COMPILER_VERSION));
		if (err)
			err = xcat("compiler_version: ", err);
	}
	return err;
}

/* validateCodeGeneratorRequest, as called by NewRequest. */
COLD const char *gopp_validate_request(const PbMsg *req)
{
	const char *err = validate_request(req);
	return err ? xcat("CodeGeneratorRequest: ", err) : NULL;
}
