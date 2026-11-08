/* Port of protoc_plugin 25.1.0 lib/src/code_generator.dart (CodeGenerator.generate).
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* Well-formed UTF-8 (no overlong forms, surrogates or code points above U+10FFFF). */
static bool valid_utf8(Str s)
{
	const unsigned char *p = (const unsigned char *)s.p, *end = p + s.n;
	while (p < end) {
		unsigned c = *p++;
		if (c < 0x80)
			continue;
		int n;
		unsigned lo = 0x80, hi = 0xBF;
		if (c >= 0xC2 && c <= 0xDF)
			n = 1;
		else if (c >= 0xE0 && c <= 0xEF)
			n = 2, lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
		else if (c >= 0xF0 && c <= 0xF4)
			n = 3, lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
		else
			return false;
		if (end - p < n || *p < lo || *p > hi)
			return false;
		p++;
		for (int i = 1; i < n; i++, p++)
			if ((*p & 0xC0) != 0x80)
				return false;
	}
	return true;
}

/* The Dart runtime decodes string fields with replacement characters for malformed UTF-8, which
 * changes the output; such requests are not supported (see README). */
static bool strings_valid(const PbMsg *m)
{
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		for (int j = 0; j < s->n; j++) {
			if (f->type == PT_STRING && !valid_utf8(s->v[j].s))
				return false;
			if (f->type == PT_MESSAGE && !strings_valid(s->v[j].m))
				return false;
		}
	}
	return true;
}

typedef struct {
	PbMsg *request;
	Str parameter;
	Vec files; /* OutFile* */
	const char *error;
	bool optionsFailed;
} GenerateJob;

static void generate_job(void *arg)
{
	GenerateJob *job = arg;
	PbMsg *request = job->request;
	/* request.protoFile.sortBy((desc) => desc.name): a stable sort */
	PbSlot *protoFiles = (PbSlot *)pb_slot(request, CGR_PROTO_FILE);
	int n = protoFiles ? protoFiles->n : 0;
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && dart_compare(pb_str(protoFiles->v[j - 1].m, FDP_NAME), pb_str(protoFiles->v[j].m, FDP_NAME)) > 0; j--) {
			PbVal t = protoFiles->v[j];
			protoFiles->v[j] = protoFiles->v[j - 1];
			protoFiles->v[j - 1] = t;
		}
	const Options *options = parseGenerationOptions(job->parameter, &job->error);
	if (!options) {
		job->optionsFailed = true;
		return;
	}
	Vec generators = {0};
	for (int i = 0; i < n; i++)
		vec_push(&generators, file_new(protoFiles->v[i].m, options));
	link_files(options, &generators);
	Map toGenerate = {0};
	for (int i = 0; i < pb_count(request, CGR_FILE_TO_GENERATE); i++)
		map_put(&toGenerate, pb_str_at(request, CGR_FILE_TO_GENERATE, i), (void *)1);
	for (int i = 0; i < generators.n; i++) {
		FileGen *f = generators.v[i];
		if (map_has(&toGenerate, pb_str(f->descriptor, FDP_NAME)))
			file_generateFiles(f, &job->files);
	}
}

const char *protoc_plugin_generate(Str raw, Str parameter, Buf *out)
{
	GenerateJob job = {.parameter = parameter};
	job.request = pb_new(&dpb_CodeGeneratorRequest);
	if (!pb_decode(job.request, (const uint8_t *)raw.p, raw.n))
		return "protoc_plugin: the request could not be parsed";
	if (!strings_valid(job.request)) {
		pb_put_bytes(out, CGRESP_ERROR,
			     STR_LIT("unsupported by protoc-gen-uni: invalid UTF-8 in a string field of the request (protoc_plugin "
				     "decodes it with replacement characters)"));
		return NULL;
	}
	const char *err = try_run(generate_job, &job);
	if (err) {
		/* An uncaught exception of the Dart program (see dart_throw) or a failure of the port. */
		pb_put_bytes(out, CGRESP_ERROR, fail_message());
		return NULL;
	}
	if (job.optionsFailed) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(job.error));
		return NULL;
	}
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3); /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 998);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, 1001);
	for (int i = 0; i < job.files.n; i++) {
		OutFile *f = job.files.v[i];
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, f->content);
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
	return NULL;
}
