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

/* Port of packages/connect/bin/src/run.dart: Schema.fromProto and run. */

/* Thrown values of the Dart program: their toString() becomes CodeGeneratorResponse.error. */
NORETURN COLD static void dart_throw(Str text) { fail_str(text); }

/* Names as C strings (paths, map keys, identifiers): strings are well-formed UTF-8 after decoding,
 * so a NUL of a Dart string is kept as the byte 0xFF, which UTF-8 never has, and turned back into
 * NUL in the response (unescape). */
static const char *cname(Str s)
{
	if (!s.p)
		return "";
	if (!memchr(s.p, 0, s.n))
		return s.p;
	char *c = xstrndup(s.p, s.n);
	for (size_t i = 0; i < s.n; i++)
		if (!c[i])
			c[i] = (char)0xFF;
	return c;
}

static Str unescape(Str s)
{
	if (!s.n || !memchr(s.p, 0xFF, s.n))
		return s;
	char *c = xstrndup(s.p, s.n);
	for (size_t i = 0; i < s.n; i++)
		if ((unsigned char)c[i] == 0xFF)
			c[i] = 0;
	return (Str){c, s.n};
}

/* Schema: the files to generate (CodeGeneratorRequest.source_file_descriptors) and all files by
 * their path (proto_file; a later file of the same path replaces the value, the map keeps the
 * position of the first). */
struct Schema {
	PbMsg *req;
	Vec file;         /* PbMsg*: the values of the map, in its order */
	bool keepEmptyFiles;
	struct MsgIndex *index; /* importMsg lookups (plugin.c), built on first use */
};

/* A map key for any string (escapes NUL and \x01). */
static const char *any_key(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] == 0 || s.p[i] == 1) {
			buf_addc(&b, 1);
			buf_addc(&b, (char)(s.p[i] + 2));
		} else {
			buf_addc(&b, s.p[i]);
		}
	}
	return buf_str(&b);
}

COLD static Schema *schema_from_proto(PbMsg *proto, Str parameter)
{
	Schema *s = xalloc(sizeof *s);
	s->req = proto;
	Map pos = {0};
	for (int i = 0; i < pb_count(proto, CGR_PROTO_FILE); i++) {
		PbMsg *f = pb_msg_at(proto, CGR_PROTO_FILE, i);
		const char *key = any_key(pb_bytes(f, FDP_NAME));
		intptr_t at = (intptr_t)map_get(&pos, key);
		if (at) {
			s->file.v[at - 1] = f;
		} else {
			vec_push(&s->file, f);
			map_put(&pos, key, (void *)(intptr_t)s->file.n);
		}
	}
	bool keepEmptyFiles = false;
	if (parameter.n) {
		Str *raws;
		int n = str_split_s(parameter, ',', &raws);
		for (int i = 0; i < n; i++) {
			Str *parts;
			int m = str_split_s(raws[i], '=', &parts);
			Str key = parts[0], value = m == 1 ? STR_LIT("") : parts[m - 1];
			if (str_eqc(key, "keep_empty_files")) {
				if (!value.n || str_eqc(value, "true")) {
					keepEmptyFiles = true;
				} else if (str_eqc(value, "false")) {
					keepEmptyFiles = false;
				} else {
					Buf b = {0};
					buf_adds(&b, "invalid plugin option 'keep_empty_files' must be a bool, got ");
					buf_add_str(&b, value);
					dart_throw(buf_view(&b));
				}
			} else {
				Buf b = {0};
				buf_adds(&b, "Unsupported plugin option ");
				buf_add_str(&b, raws[i]);
				dart_throw(buf_view(&b));
			}
		}
	}
	s->keepEmptyFiles = keepEmptyFiles;
	return s;
}

typedef struct {
	PbMsg *req;
	Str raw, parameter;
	Vec files; /* GeneratedFile* yielded so far, with their content */
	Vec contents;
} RunJob;

static void generate(Schema *schema, RunJob *job);

static void run_job(void *arg)
{
	RunJob *job = arg;
	const char *err = dpb_merge_from_buffer(job->req, job->raw);
	if (err)
		dart_throw(str_c(err));
	generate(schema_from_proto(job->req, job->parameter), job);
}

/* run: the response has the supported features and editions, and the files or the error.
 * Upstream also keeps the files added before the error (res.file.addAll of a lazy iterable),
 * which protoc and buf ignore; the port leaves them out. */
static const char *run(PbMsg *req, Str raw, Buf *out)
{
	RunJob job = {.req = pb_new(&pbd_CodeGeneratorRequest), .raw = raw};
	job.parameter = dart_utf8_decode_allow_malformed(pb_bytes(req, CGR_PARAMETER));
	const char *err = try_run(run_job, &job);
	if (err)
		pb_put_bytes(out, CGRESP_ERROR, unescape(fail_message()));
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3); /* FEATURE_PROTO3_OPTIONAL + FEATURE_SUPPORTS_EDITIONS */
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 998); /* EDITION_PROTO2 */
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 1001); /* EDITION_2024 */
	for (int i = 0; !err && i < job.files.n; i++) {
		GeneratedFile *f = job.files.v[i];
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, unescape(str_c(f->path)));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, unescape(*(Str *)job.contents.v[i]));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
	return NULL;
}

/* res.file.addAll(files.map(...)): files are added as the generator yields them. */
static void yield_file(RunJob *job, GeneratedFile *f)
{
	Str *content = xalloc(sizeof *content);
	*content = gf_content(f);
	vec_push(&job->files, f);
	vec_push(&job->contents, content);
}
