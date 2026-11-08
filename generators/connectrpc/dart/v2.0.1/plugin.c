/* Copyright 2024-2025 The Connect Authors
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

/* Port of packages/connect/bin/src/plugin.dart. */

enum {
	MO_DEPRECATED = 33, MO_IDEMPOTENCY_LEVEL = 34, /* MethodOptions */
	IDEMPOTENT = 2, NO_SIDE_EFFECTS = 1,
};

/* A file to generate with what the extension on GeneratedFile computes from it once:
 * proto.service.indexOf(service), service.method.indexOf(method) (GeneratedMessage ==) and the
 * first source location of each path (findComments). */
struct ProtoInfo {
	PbMsg *proto;
	const char *name;
	int *serviceIndex;
	int **methodIndex;
	Map locations; /* "<path>" -> first SourceCodeInfo.Location */
	bool locationsBuilt;
};

static const char *proto_name(const ProtoInfo *p) { return p->name; }

/* List.indexOf of every element of a list of messages: the first equal element (equal messages
 * have equal hashes; a message with NaN is equal only to itself). */
static int *index_of_all(const PbMsg *list, uint32_t num)
{
	int n = pb_count(list, num);
	int *out = xalloc(sizeof *out * (size_t)(n ? n : 1));
	Map buckets = {0}; /* hash -> Vec* of indices */
	for (int i = 0; i < n; i++) {
		PbMsg *m = pb_msg_at(list, num, i);
		bool self_only = false;
		uint64_t h = dpb_hash(m, &self_only);
		out[i] = i;
		if (self_only)
			continue;
		const char *key = xfmt("%llx", (unsigned long long)h);
		Vec *bucket = map_get(&buckets, key);
		if (!bucket) {
			bucket = xalloc(sizeof *bucket);
			map_put(&buckets, key, bucket);
		}
		bool found = false;
		for (int j = 0; j < bucket->n && !found; j++) {
			int k = (int)(intptr_t)bucket->v[j];
			if (dpb_equals(pb_msg_at(list, num, k), m))
				out[i] = k, found = true;
		}
		if (!found)
			vec_push(bucket, (void *)(intptr_t)i);
	}
	return out;
}

COLD static ProtoInfo *proto_info(PbMsg *proto)
{
	ProtoInfo *p = xalloc(sizeof *p);
	p->proto = proto;
	p->name = cname(pb_bytes(proto, FDP_NAME));
	int n = pb_count(proto, FDP_SERVICE);
	p->serviceIndex = index_of_all(proto, FDP_SERVICE);
	p->methodIndex = xalloc(sizeof *p->methodIndex * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		p->methodIndex[i] = index_of_all(pb_msg_at(proto, FDP_SERVICE, i), SDP_METHOD);
	return p;
}

static const char *path_key(const int64_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, "%lld,", (long long)path[i]);
	return buf_str(&b);
}

/* findComments: the first location whose path equals path (only paths of 2 and 4 elements are
 * looked up). */
static PbMsg *find_comments(ProtoInfo *p, const int64_t *path, int n)
{
	PbMsg *sci = pb_msg(p->proto, FDP_SOURCE_CODE_INFO);
	if (!p->locationsBuilt) {
		p->locationsBuilt = true;
		for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
			PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
			int m = pb_count(loc, LOC_PATH);
			if (m != 2 && m != 4)
				continue;
			int64_t lp[4];
			for (int j = 0; j < m; j++)
				lp[j] = pb_int_at(loc, LOC_PATH, j);
			const char *key = path_key(lp, m);
			if (!map_has(&p->locations, key))
				map_put(&p->locations, key, loc);
		}
	}
	return map_get(&p->locations, path_key(path, n));
}

/* ---- String extensions ---- */

static Str without_ln(Str s) { return s.n && s.p[s.n - 1] == '\n' ? (Str){s.p, s.n - 1} : s; }

static const char *const disallowedIdentifiers[] = {
	"assert", "break", "case", "catch", "class", "const", "continue", "default", "do", "else", "enum",
	"extends", "false", "final", "finally", "for", "if", "in", "is", "new", "null", "rethrow",
	"return", "super", "switch", "this", "throw", "true", "try", "var", "void", "with", "while",
};

static const char *sanitize_identifier(const char *s)
{
	for (size_t i = 0; i < sizeof disallowedIdentifiers / sizeof *disallowedIdentifiers; i++)
		if (str_eq(s, disallowedIdentifiers[i]))
			return xcat(s, "$");
	return s;
}

/* ---- MethodDescriptorProto extensions ---- */

/* (name.substring(0, 1).toLowerCase() + name.substring(1)).sanitizeIdentifier: the first UTF-16
 * code unit (of a character above U+FFFF, a high surrogate: unchanged). */
COLD static const char *local_name(const PbMsg *method)
{
	const char *name = cname(pb_bytes(method, MDP_NAME));
	if (!*name)
		dart_throw(STR_LIT("RangeError (end): Invalid value: Only valid value is 0: 1"));
	uint32_t c = (unsigned char)*name;
	size_t w = 1;
	if (c == 0xFF) /* NUL */
		c = 0;
	else if (c >= 0x80)
		w = (size_t)utf8_decode(name, strlen(name), &c);
	Buf b = {0};
	uint32_t l = c > 0xFFFF ? c : dart_to_lower_code_unit(c);
	if (l == c) {
		buf_add(&b, name, w);
	} else if (l < 0x80) {
		buf_addc(&b, (char)l);
	} else if (l < 0x800) {
		buf_addc(&b, (char)(0xC0 | l >> 6));
		buf_addc(&b, (char)(0x80 | (l & 0x3F)));
	} else {
		buf_addc(&b, (char)(0xE0 | l >> 12));
		buf_addc(&b, (char)(0x80 | (l >> 6 & 0x3F)));
		buf_addc(&b, (char)(0x80 | (l & 0x3F)));
	}
	buf_adds(&b, name + w);
	return sanitize_identifier(buf_str(&b));
}

static const char *spec_name(const PbMsg *method)
{
	const char *n = local_name(method);
	return str_eq(n, "name") ? "name$" : n;
}

static const char *stream_type(const PbMsg *method)
{
	bool cs = pb_int(method, MDP_CLIENT_STREAMING, 0), ss = pb_int(method, MDP_SERVER_STREAMING, 0);
	if (!cs && !ss)
		return "unary";
	if (cs && ss)
		return "bidi";
	return ss ? "server" : "client";
}

/* ---- importMsg ----
 *
 * importMsg(typeName) takes the first file of the map whose ".<package>" starts typeName and
 * whose messages have the rest (hasMessage). Instead of trying every file, the index has a trie of
 * the packages (so the packages that start typeName are found in one walk of it) and, per
 * package, a tree of the message names hasMessage can reach: the first message of a name among
 * its siblings, whose nested messages it searches, with the first map position of a file that
 * has the path. Names with '.' are never found by hasMessage and are left out. */
typedef struct MsgIndex {
	Map trie;  /* "<node>/<byte>" -> trie node + 1 */
	Map names; /* "<node>/<name>" -> message node + 1 */
	int *pkgRoot, nTrie, capTrie; /* trie node -> message node of its package + 1, or 0 */
	int *minPos, *lastPos, nMsg, capMsg;
} MsgIndex;

static int grow(int **a, int n, int *cap)
{
	if (n == *cap) {
		int c = *cap ? *cap * 2 : 64;
		*a = xrealloc(*a, sizeof **a * (size_t)*cap, sizeof **a * (size_t)c);
		*cap = c;
	}
	return n;
}

static int msg_node(MsgIndex *ix)
{
	int c = ix->capMsg;
	grow(&ix->minPos, ix->nMsg, &c);
	c = ix->capMsg;
	grow(&ix->lastPos, ix->nMsg, &c);
	ix->capMsg = c;
	ix->minPos[ix->nMsg] = -1;
	ix->lastPos[ix->nMsg] = -1;
	return ix->nMsg++;
}

static void index_messages(MsgIndex *ix, int parent, const PbMsg *m, uint32_t field, int pos)
{
	for (int i = 0; i < pb_count(m, field); i++) {
		PbMsg *msg = pb_msg_at(m, field, i);
		Str name = pb_bytes(msg, DP_NAME);
		if (name.n && memchr(name.p, '.', name.n))
			continue;
		const char *key = xfmt("%d/%s", parent, cname(name));
		int node = (int)(intptr_t)map_get(&ix->names, key) - 1;
		if (node < 0) {
			node = msg_node(ix);
			map_put(&ix->names, key, (void *)(intptr_t)(node + 1));
		}
		if (ix->lastPos[node] == pos)
			continue; /* a later sibling of the same name */
		ix->lastPos[node] = pos;
		if (ix->minPos[node] < 0)
			ix->minPos[node] = pos;
		index_messages(ix, node, msg, DP_NESTED_TYPE, pos);
	}
}

COLD static MsgIndex *msg_index(const Schema *schema)
{
	MsgIndex *ix = xalloc(sizeof *ix);
	grow(&ix->pkgRoot, ix->nTrie, &ix->capTrie);
	ix->pkgRoot[ix->nTrie++] = 0;
	for (int pos = 0; pos < schema->file.n; pos++) {
		PbMsg *f = schema->file.v[pos];
		const char *pkg = cname(pb_bytes(f, FDP_PACKAGE));
		int node = 0;
		for (size_t i = 0; pkg[i]; i++) {
			const char *key = xfmt("%d/%c", node, pkg[i]);
			int next = (int)(intptr_t)map_get(&ix->trie, key) - 1;
			if (next < 0) {
				next = ix->nTrie;
				grow(&ix->pkgRoot, ix->nTrie, &ix->capTrie);
				ix->pkgRoot[ix->nTrie++] = 0;
				map_put(&ix->trie, key, (void *)(intptr_t)(next + 1));
			}
			node = next;
		}
		if (!ix->pkgRoot[node])
			ix->pkgRoot[node] = msg_node(ix) + 1;
		index_messages(ix, ix->pkgRoot[node] - 1, f, FDP_MESSAGE_TYPE, pos);
	}
	return ix;
}

/* The first map position of a file of the package at message node root with hasMessage(local). */
static int has_message(const MsgIndex *ix, int root, const char *local)
{
	if (*local == '.')
		local++;
	int node = root;
	for (;;) {
		const char *dot = strchr(local, '.');
		size_t n = dot ? (size_t)(dot - local) : strlen(local);
		const char *key = xfmt("%d/%s", node, xstrndup(local, n));
		node = (int)(intptr_t)map_get(&ix->names, key) - 1;
		if (node < 0)
			return -1;
		if (!dot)
			return ix->minPos[node];
		local = dot + 1;
	}
}

static const char *const wellKnownTypeProtoPaths[] = {
	"google/protobuf/any.proto", "google/protobuf/api.proto", "google/protobuf/duration.proto",
	"google/protobuf/empty.proto", "google/protobuf/field_mask.proto", "google/protobuf/source_context.proto",
	"google/protobuf/struct.proto", "google/protobuf/timestamp.proto", "google/protobuf/type.proto",
	"google/protobuf/wrappers.proto",
};

COLD static Printable import_msg(GeneratedFile *g, Str type)
{
	const char *typeName = cname(type);
	if (!g->schema->index)
		g->schema->index = msg_index(g->schema);
	const MsgIndex *ix = g->schema->index;
	int best = -1;
	size_t bestLen = 0;
	if (*typeName == '.') {
		int node = 0;
		for (size_t i = 1;; i++) {
			if (ix->pkgRoot[node]) {
				int pos = has_message(ix, ix->pkgRoot[node] - 1, typeName + i);
				if (pos >= 0 && (best < 0 || pos < best))
					best = pos, bestLen = i - 1;
			}
			if (!typeName[i])
				break;
			node = (int)(intptr_t)map_get(&ix->trie, xfmt("%d/%c", node, typeName[i])) - 1;
			if (node < 0)
				break;
		}
	}
	if (best < 0) {
		Buf b = {0};
		buf_adds(&b, "Message: ");
		buf_adds(&b, typeName);
		buf_adds(&b, " not found");
		dart_throw(buf_view(&b));
	}
	PbMsg *file = g->schema->file.v[best];
	const char *fileName = cname(pb_bytes(file, FDP_NAME));
	const char *localName = typeName + bestLen + 1;
	const char *libraryPath = NULL;
	for (size_t i = 0; i < sizeof wellKnownTypeProtoPaths / sizeof *wellKnownTypeProtoPaths; i++)
		if (str_eq(fileName, wellKnownTypeProtoPaths[i]))
			libraryPath = xcat("package:protobuf/well_known_types/", dpath_posix_without_extension(fileName), ".pb.dart");
	if (!libraryPath) {
		const char *err = NULL;
		const char *rel = dpath_posix_relative(fileName, dpath_posix_dirname(g->proto->name), &err);
		if (!rel)
			dart_throw(str_c(err));
		libraryPath = xcat(dpath_posix_without_extension(rel), ".pb.dart");
	}
	DartLibrary *lib = dart_library(libraryPath, str_replace_all(dpath_posix_without_extension(fileName), "/", ""));
	const char *ident = str_replace_all(*localName == '.' ? localName + 1 : localName, ".", "_");
	return (Printable){lib, str_c(ident)};
}

/* ---- the extension on GeneratedFile ---- */

COLD static void p_dart_doc(GeneratedFile *g, const int64_t *path, int n, const char *indentation)
{
	PbMsg *comments = find_comments(g->proto, path, n);
	if (!comments)
		return;
	Buf text = {0};
	if (pb_has(comments, LOC_LEADING))
		buf_add_str(&text, without_ln(pb_bytes(comments, LOC_LEADING)));
	if (pb_has(comments, LOC_TRAILING)) {
		if (text.n)
			buf_adds(&text, "\n\n");
		buf_add_str(&text, without_ln(pb_bytes(comments, LOC_TRAILING)));
	}
	Str *lines;
	int m = str_split_s(buf_view(&text), '\n', &lines);
	for (int i = 0; i < m; i++) {
		Str l = lines[i];
		if (str_has_prefix(l, STR_LIT("/*")))
			l.p += 2, l.n -= 2;
		if (str_has_prefix(l, STR_LIT("//")))
			l.p += 2, l.n -= 2;
		if (str_has_prefix(l, STR_LIT(" ")))
			l.p += 1, l.n -= 1;
		if (!dart_trim(l).n)
			continue;
		Buf b = {0};
		buf_adds(&b, indentation);
		buf_adds(&b, "/// ");
		buf_add_str(&b, l);
		gf_pf(g, "%S", dart_trim_right(buf_view(&b)));
	}
}

static void p_service_doc(GeneratedFile *g, int s)
{
	int64_t path[] = {FDP_SERVICE, g->proto->serviceIndex[s]};
	p_dart_doc(g, path, 2, "");
}

static void p_method_doc(GeneratedFile *g, int s, int m)
{
	int64_t path[] = {FDP_SERVICE, g->proto->serviceIndex[s], SDP_METHOD, g->proto->methodIndex[s][m]};
	p_dart_doc(g, path, 4, "  ");
}

static void p_method_options(GeneratedFile *g)
{
	gf_pf(g, "    " CONNECT "Headers? headers,");
	gf_pf(g, "    " CONNECT "AbortSignal? signal,");
	gf_pf(g, "    Function(" CONNECT "Headers)? onHeader,");
	gf_pf(g, "    Function(" CONNECT "Headers)? onTrailer,");
}

COLD static void p_method_spec(GeneratedFile *g, const PbMsg *method)
{
	gf_pf(g, "  static const %s = " CONNECT "Spec(", spec_name(method));
	gf_pf(g, "    '/$name/%S',", pb_bytes(method, MDP_NAME));
	gf_pf(g, "    " CONNECT "StreamType.%s,", stream_type(method));
	gf_pf(g, "    %I.new,", import_msg(g, pb_bytes(method, MDP_INPUT_TYPE)));
	gf_pf(g, "    %I.new,", import_msg(g, pb_bytes(method, MDP_OUTPUT_TYPE)));
	int64_t level = pb_int(pb_msg(method, MDP_OPTIONS), MO_IDEMPOTENCY_LEVEL, 0);
	if (level == IDEMPOTENT)
		gf_pf(g, "    idempotency: " CONNECT "Idempotency.idempotent,");
	if (level == NO_SIDE_EFFECTS)
		gf_pf(g, "    idempotency: " CONNECT "Idempotency.noSideEffects,");
	gf_pf(g, "  );");
}

COLD static void print_specs(GeneratedFile *g, int s)
{
	const PbMsg *service = pb_msg_at(g->proto->proto, FDP_SERVICE, s);
	Str name = pb_bytes(service, SDP_NAME);
	p_service_doc(g, s);
	gf_pf(g, "abstract final class %S {", name);
	gf_pf(g, "  /// Fully-qualified name of the %S service.", name);
	gf_pf(g, "  static const name = '%S.%S';", pb_bytes(g->proto->proto, FDP_PACKAGE), name);
	for (int m = 0; m < pb_count(service, SDP_METHOD); m++) {
		gf_pf(g, "");
		p_method_doc(g, s, m);
		p_method_spec(g, pb_msg_at(service, SDP_METHOD, m));
	}
	gf_pf(g, "}");
}

COLD static void p_method(GeneratedFile *g, int s, int m)
{
	const PbMsg *service = pb_msg_at(g->proto->proto, FDP_SERVICE, s);
	const PbMsg *method = pb_msg_at(service, SDP_METHOD, m);
	DartLibrary *specsLib = dart_library(xcat(dpath_posix_basename_without_extension(g->proto->name), ".connect.spec.dart"), "specs");
	p_method_doc(g, s, m);
	if (pb_int(pb_msg(method, MDP_OPTIONS), MO_DEPRECATED, 0))
		gf_pf(g, "@deprecated");
	/* in upstream's order: the output type, the input type, then the name */
	Printable output = import_msg(g, pb_bytes(method, MDP_OUTPUT_TYPE));
	Printable input = import_msg(g, pb_bytes(method, MDP_INPUT_TYPE));
	const char *localName = local_name(method);
	gf_pf(g, pb_int(method, MDP_SERVER_STREAMING, 0) ? "  Stream<%I> %s(" : "  Future<%I> %s(", output, localName);
	gf_pf(g, pb_int(method, MDP_CLIENT_STREAMING, 0) ? "    Stream<%I> input, {" : "    %I input, {", input);
	p_method_options(g);
	gf_pf(g, "  }) {");
	gf_pf(g, "    return " CONNECT "Client(_transport).%s(", stream_type(method));
	gf_pf(g, "      %I.%s,", (Printable){specsLib, pb_bytes(service, SDP_NAME)}, spec_name(method));
	gf_pf(g, "      input,\n"
		 "      signal: signal,\n"
		 "      headers: headers,\n"
		 "      onHeader: onHeader,\n"
		 "      onTrailer: onTrailer,\n"
		 "    );\n"
		 "  }");
}

COLD static void print_client(GeneratedFile *g, int s)
{
	const PbMsg *service = pb_msg_at(g->proto->proto, FDP_SERVICE, s);
	p_service_doc(g, s);
	gf_pf(g, "extension type %SClient (" CONNECT "Transport _transport) {", pb_bytes(service, SDP_NAME));
	for (int m = 0; m < pb_count(service, SDP_METHOD); m++) {
		if (m)
			gf_pf(g, "");
		p_method(g, s, m);
	}
	gf_pf(g, "}");
}

/* generate: per file to generate (with services, unless keep_empty_files) <dir>/<base>.connect
 * .spec.dart and .client.dart. */
COLD static void generate(Schema *schema, RunJob *job)
{
	for (int i = 0; i < pb_count(schema->req, CGR_SOURCE_FILE_DESCRIPTORS); i++) {
		PbMsg *file = pb_msg_at(schema->req, CGR_SOURCE_FILE_DESCRIPTORS, i);
		if (!schema->keepEmptyFiles && !pb_count(file, FDP_SERVICE))
			continue;
		ProtoInfo *proto = proto_info(file);
		const char *baseName = dpath_posix_normalize(dpath_posix_join(
			dpath_posix_dirname(proto->name), xcat(dpath_posix_basename_without_extension(proto->name), ".connect")));
		GeneratedFile *specsFile = generated_file(xcat(baseName, ".spec.dart"), schema, proto);
		GeneratedFile *clientFile = generated_file(xcat(baseName, ".client.dart"), schema, proto);
		for (int s = 0; s < pb_count(file, FDP_SERVICE); s++) {
			print_specs(specsFile, s);
			print_client(clientFile, s);
		}
		yield_file(job, specsFile);
		yield_file(job, clientFile);
	}
}
