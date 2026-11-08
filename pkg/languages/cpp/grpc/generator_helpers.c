/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Port of src/compiler/generator_helpers.h (grpc v1.85.1, namespace grpc_generator). */

static char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static char ascii_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

char *grpc_to_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		*p = ascii_lower(*p);
	return r;
}

bool grpc_strip_suffix(char **filename, const char *suffix)
{
	size_t n = strlen(*filename), m = strlen(suffix);
	if (n < m)
		return false;
	if (!str_eq(grpc_to_lower(*filename + n - m), grpc_to_lower(suffix)))
		return false;
	*filename = xstrndup(*filename, n - m);
	return true;
}

bool grpc_strip_prefix(char **name, const char *prefix)
{
	if (!has_prefix(*name, prefix))
		return false;
	*name = xstrdup(*name + strlen(prefix));
	return true;
}

char *grpc_strip_proto(const char *filename)
{
	char *s = (char *)filename;
	if (!grpc_strip_suffix(&s, ".protodevel"))
		grpc_strip_suffix(&s, ".proto");
	return s;
}

char *grpc_string_replace(const char *str, const char *from, const char *to, bool replace_all)
{
	size_t nf = strlen(from);
	if (!nf) /* std::string::find("") matches everywhere; no caller passes it */
		return xstrdup(str);
	Buf b = {0};
	const char *p = str;
	do {
		const char *hit = strstr(p, from);
		if (!hit)
			break;
		buf_add(&b, p, (size_t)(hit - p));
		buf_adds(&b, to);
		p = hit + nf;
	} while (replace_all);
	buf_adds(&b, p);
	return xstrdup(buf_str(&b));
}

Vec grpc_tokenize(const char *input, const char *delimiters)
{
	Vec tokens = {0};
	const char *p = input;
	for (;;) {
		size_t n = strcspn(p, delimiters);
		vec_push(&tokens, xstrndup(p, n));
		if (!p[n])
			return tokens;
		p += n + 1;
	}
}

char *grpc_capitalize_first_letter(const char *s)
{
	char *r = xstrdup(s);
	r[0] = ascii_upper(r[0]);
	return r;
}

char *grpc_lowercase_first_letter(const char *s)
{
	char *r = xstrdup(s);
	r[0] = ascii_lower(r[0]);
	return r;
}

char *grpc_lower_underscore_to_upper_camel(const char *s)
{
	Vec tokens = grpc_tokenize(s, "_");
	Buf b = {0};
	for (int i = 0; i < tokens.n; i++)
		buf_adds(&b, grpc_capitalize_first_letter(tokens.v[i]));
	return xstrdup(buf_str(&b));
}

char *grpc_file_name_in_upper_camel(const LpFile *file, bool include_package_path)
{
	Vec tokens = grpc_tokenize(grpc_strip_proto(file->name), "/");
	Buf b = {0};
	if (include_package_path)
		for (int i = 0; i < tokens.n - 1; i++) {
			buf_adds(&b, tokens.v[i]);
			buf_addc(&b, '/');
		}
	buf_adds(&b, grpc_lower_underscore_to_upper_camel(tokens.v[tokens.n - 1]));
	return xstrdup(buf_str(&b));
}

GrpcMethodType grpc_get_method_type(const LpMethod *method)
{
	if (method->client_streaming)
		return method->server_streaming ? GRPC_METHODTYPE_BIDI_STREAMING : GRPC_METHODTYPE_CLIENT_STREAMING;
	return method->server_streaming ? GRPC_METHODTYPE_SERVER_STREAMING : GRPC_METHODTYPE_NO_STREAMING;
}

static void push_line(Vec *out, const char *p, size_t n)
{
	Str *line = xalloc(sizeof *line);
	*line = (Str){p, n};
	vec_push(out, line);
}

void grpc_split(Str s, Vec *append_to)
{
	size_t pos = 0;
	while (pos < s.n) {
		const char *nl = memchr(s.p + pos, '\n', s.n - pos);
		size_t end = nl ? (size_t)(nl - s.p) : s.n;
		push_line(append_to, s.p + pos, end - pos);
		pos = nl ? end + 1 : end;
	}
}

static void comment_lines(const LpSourceLocation *location, GrpcCommentType type, Vec *out)
{
	if (type == GRPC_COMMENTTYPE_LEADING_DETACHED) {
		for (int i = 0; i < location->n_leading_detached_comments; i++) {
			grpc_split(location->leading_detached_comments[i], out);
			push_line(out, "", 0);
		}
		return;
	}
	grpc_split(type == GRPC_COMMENTTYPE_LEADING ? location->leading_comments : location->trailing_comments, out);
}

void grpc_get_comment(const LpSourceLocations *locs, const ProtoDesc *desc, GrpcCommentType type, Vec *out)
{
	LpSourceLocation location;
	if (desc->kind == PROTO_FILE) {
		/* the file-level comments are those above the syntax line; none trailing */
		if (type == GRPC_COMMENTTYPE_TRAILING)
			return;
		static const int32_t path[] = {12}; /* FileDescriptorProto::kSyntaxFieldNumber */
		if (!lp_get_source_location_path(locs, path, 1, &location))
			return;
	} else if (!lp_get_source_location(locs, desc, &location)) {
		return;
	}
	comment_lines(&location, type, out);
}

Str grpc_generate_comments_with_prefix(const Vec *in, const char *prefix)
{
	Buf b = {0};
	for (int i = 0; i < in->n; i++) {
		const Str *elem = in->v[i];
		buf_adds(&b, prefix);
		if (elem->n && elem->p[0] != ' ')
			buf_addc(&b, ' ');
		if (elem->n)
			buf_add_str(&b, grpc_escape_variable_delimiters(*elem));
		buf_addc(&b, '\n');
	}
	buf_str(&b);
	return (Str){b.p ? b.p : "", b.n};
}

Str grpc_get_prefixed_comments(const LpSourceLocations *locs, const ProtoDesc *desc, bool leading, const char *prefix)
{
	Vec out = {0};
	if (leading) {
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING_DETACHED, &out);
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_LEADING, &out);
	} else {
		grpc_get_comment(locs, desc, GRPC_COMMENTTYPE_TRAILING, &out);
	}
	return grpc_generate_comments_with_prefix(&out, prefix);
}
