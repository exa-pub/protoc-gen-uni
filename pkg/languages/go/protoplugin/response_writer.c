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

/* Ported from github.com/bufbuild/protoplugin@750e09ce93e1 response_writer.go and the response
 * part of validate.go (validateAndNormalizeCodeGeneratorResponse), plus the proto.Marshal of
 * protoplugin.run. */
#include "internal.h"

#include <string.h>

/* CodeGeneratorResponse and CodeGeneratorResponse.File field numbers. */
enum { RESP_ERROR = 1, RESP_SUPPORTED_FEATURES = 2, RESP_MINIMUM_EDITION = 3, RESP_MAXIMUM_EDITION = 4, RESP_FILE = 15 };
enum { FILE_NAME = 1, FILE_INSERTION_POINT = 2, FILE_CONTENT = 15, FILE_GENERATED_CODE_INFO = 16 };

#define ALL_SUPPORTED_FEATURES_MASK ((uint64_t)(GOPP_FEATURE_PROTO3_OPTIONAL | GOPP_FEATURE_SUPPORTS_EDITIONS))

struct GoppResponseWriter {
	Vec files; /* GoppFile* */
	Str error;
	bool has_error;
	uint64_t supported_features;
	bool has_supported_features;
	int32_t minimum_edition, maximum_edition; /* 0: not set */
	bool written;
	void (*lenient)(void *ctx, Str warning);
	void *lenient_ctx;
};

GoppResponseWriter *gopp_response_writer_new(void (*lenient)(void *ctx, Str warning), void *ctx)
{
	GoppResponseWriter *w = xalloc(sizeof *w);
	w->lenient = lenient;
	w->lenient_ctx = ctx;
	return w;
}

void gopp_add_response_files(GoppResponseWriter *w, const GoppFile *files, int n)
{
	for (int i = 0; i < n; i++) {
		GoppFile *f = xalloc(sizeof *f);
		*f = files[i];
		vec_push(&w->files, f);
	}
}

void gopp_add_file(GoppResponseWriter *w, Str name, Str content)
{
	GoppFile f = {0};
	f.name = name, f.has_name = true;
	f.content = content, f.has_content = true;
	gopp_add_response_files(w, &f, 1);
}

void gopp_add_error(GoppResponseWriter *w, Str message)
{
	/* plugin.proto specifies that only non-empty errors are considered errors. */
	if (message.n == 0)
		return;
	if (w->has_error && w->error.n) {
		Buf b = {0};
		buf_add_str(&b, w->error);
		buf_adds(&b, "; ");
		buf_add_str(&b, message);
		message = buf_view(&b);
	}
	w->error = message;
	w->has_error = true;
}

/* addSupportedFeatures */
static void add_supported_features(GoppResponseWriter *w, uint64_t features)
{
	w->supported_features |= features;
	w->has_supported_features = true;
}

void gopp_set_feature_proto3_optional(GoppResponseWriter *w) { add_supported_features(w, GOPP_FEATURE_PROTO3_OPTIONAL); }

void gopp_set_feature_supports_editions(GoppResponseWriter *w, int32_t minimum, int32_t maximum)
{
	add_supported_features(w, GOPP_FEATURE_SUPPORTS_EDITIONS);
	gopp_set_minimum_edition(w, minimum);
	gopp_set_maximum_edition(w, maximum);
}

void gopp_set_supported_features(GoppResponseWriter *w, uint64_t features)
{
	w->supported_features = features;
	w->has_supported_features = features != 0;
}

void gopp_set_minimum_edition(GoppResponseWriter *w, int32_t edition) { w->minimum_edition = edition; }
void gopp_set_maximum_edition(GoppResponseWriter *w, int32_t edition) { w->maximum_edition = edition; }

/* ---- validate.go: response ---- */

static Str file_name(const GoppFile *f) { return f->has_name ? f->name : (Str){"", 0}; }

/* validateAndNormalizeCodeGeneratorResponseFilesWithPotentialEmptyNames: files without a name are
 * appended to the previous file. */
static COLD const char *normalize_empty_names(Vec *files)
{
	if (files->n == 0)
		return NULL;
	GoppFile *prev = files->v[0];
	if (file_name(prev).n == 0)
		return "file: first value had no name set";
	if (files->n == 1)
		return NULL;
	Vec result = {0};
	for (int i = 1; i < files->n; i++) {
		GoppFile *cur = files->v[i];
		if (file_name(cur).n) {
			vec_push(&result, prev);
			prev = cur;
		} else {
			if (cur->has_insertion_point && cur->insertion_point.n)
				return "file: empty name with non-empty insertion point";
			if (cur->has_content) {
				if (!prev->has_content) {
					prev->content = cur->content;
					prev->has_content = true;
				} else {
					Buf b = {0};
					buf_add_str(&b, prev->content);
					buf_add_str(&b, cur->content);
					prev->content = buf_view(&b);
				}
			}
		}
		if (i == files->n - 1)
			vec_push(&result, prev);
	}
	*files = result;
	return NULL;
}

/* validateAndNormalizeCodeGeneratorResponseFilesWithPotentialDuplicates */
static COLD const char *normalize_duplicates(GoppResponseWriter *w)
{
	Map names = {0};
	Vec result = {0};
	for (int i = 0; i < w->files.n; i++) {
		GoppFile *f = w->files.v[i];
		Str name = file_name(f), normalized;
		bool has_insertion_point = f->has_insertion_point && f->insertion_point.n;
		const char *err = gopp_validate_and_normalize_path("file", name, &normalized);
		if (err)
			return err;
		if (!str_equal(name, normalized)) {
			if (!w->lenient)
				return xcat("file: ", gopp_unnormalized_name_error(name, normalized, false));
			w->lenient(w->lenient_ctx, str_c(gopp_unnormalized_name_error(name, normalized, true)));
			/* We will coerce this into a normalized name if it is otherwise valid. */
			name = normalized;
			f->name = normalized;
			f->has_name = true;
		}
		const char *key = gopp_key(name);
		/* If insertionPoint is set, it is valid and correct to have a duplicate file. */
		if (map_has(&names, key) && !has_insertion_point) {
			if (!w->lenient)
				return xcat("file: ", gopp_duplicate_name_error(name, false));
			w->lenient(w->lenient_ctx, str_c(gopp_duplicate_name_error(name, true)));
		} else {
			vec_push(&result, f);
			map_put(&names, key, (void *)key);
		}
	}
	w->files = result;
	return NULL;
}

/* strconv.FormatUint(v, 2) */
static COLD const char *format_binary(uint64_t v)
{
	char t[65];
	int i = 64;
	t[i] = 0;
	do {
		t[--i] = (char)('0' + (v & 1));
		v >>= 1;
	} while (v);
	return xstrdup(t + i);
}

/* validateAndNormalizeCodeGeneratorResponse */
static COLD const char *validate_response(GoppResponseWriter *w)
{
	const char *err = normalize_empty_names(&w->files);
	if (!err)
		err = normalize_duplicates(w);
	if (err)
		return err;
	uint64_t features = w->has_supported_features ? w->supported_features : 0;
	if ((features | ALL_SUPPORTED_FEATURES_MASK) != ALL_SUPPORTED_FEATURES_MASK)
		return xcat("supported_features: unknown CodeGeneratorResponse.Features: ", format_binary(features));
	if (features & GOPP_FEATURE_SUPPORTS_EDITIONS) {
		if (w->minimum_edition == 0)
			return "supported_features: FEATURE_SUPPORTS_EDITIONS specified but no minimum_edition set";
		if (w->maximum_edition == 0)
			return "supported_features: FEATURE_SUPPORTS_EDITIONS specified but no maximum_edition set";
		if (w->minimum_edition > w->maximum_edition)
			return xfmt("minimum_edition %d is greater than maximum_edition %d", (int)w->minimum_edition,
				    (int)w->maximum_edition);
	}
	return NULL;
}

/* ---- proto.Marshal ---- */

static void put_varint_field(Buf *b, uint32_t num, uint64_t v)
{
	pb_put_tag(b, num, WT_VARINT);
	pb_put_varint(b, v);
}

static void marshal_file(Buf *out, const GoppFile *f)
{
	Buf b = {0};
	if (f->has_name)
		pb_put_bytes(&b, FILE_NAME, f->name);
	if (f->has_insertion_point)
		pb_put_bytes(&b, FILE_INSERTION_POINT, f->insertion_point);
	if (f->has_content)
		pb_put_bytes(&b, FILE_CONTENT, f->content);
	if (f->has_generated_code_info)
		pb_put_bytes(&b, FILE_GENERATED_CODE_INFO, f->generated_code_info);
	pb_put_bytes(out, RESP_FILE, buf_view(&b));
}

const char *gopp_to_response(GoppResponseWriter *w, Buf *out)
{
	if (w->written)
		return "ResponseWriter cannot be reused";
	w->written = true;
	const char *err = validate_response(w);
	if (err)
		return xcat("CodeGeneratorResponse: ", err);
	if (w->has_error)
		pb_put_bytes(out, RESP_ERROR, w->error);
	if (w->has_supported_features)
		put_varint_field(out, RESP_SUPPORTED_FEATURES, w->supported_features);
	if (w->minimum_edition)
		put_varint_field(out, RESP_MINIMUM_EDITION, (uint64_t)(int64_t)w->minimum_edition);
	if (w->maximum_edition)
		put_varint_field(out, RESP_MAXIMUM_EDITION, (uint64_t)(int64_t)w->maximum_edition);
	for (int i = 0; i < w->files.n; i++)
		marshal_file(out, w->files.v[i]);
	return NULL;
}

/* ---- reading a serialized response ---- */

static bool parse_file(const uint8_t *p, size_t n, GoppFile *f)
{
	PbReader r = {p, p + n};
	PbRawField fl;
	while (r.p < r.end) {
		if (!pb_next_field(&r, &fl))
			return false;
		if (fl.wt != WT_LEN)
			continue;
		Str v = {(const char *)fl.val, fl.vlen};
		switch (fl.num) {
		case FILE_NAME:
			f->name = v, f->has_name = true;
			break;
		case FILE_INSERTION_POINT:
			f->insertion_point = v, f->has_insertion_point = true;
			break;
		case FILE_CONTENT:
			f->content = v, f->has_content = true;
			break;
		case FILE_GENERATED_CODE_INFO:
			if (f->has_generated_code_info) {
				/* repeated occurrences of a message field merge: concatenation */
				Buf b = {0};
				buf_add_str(&b, f->generated_code_info);
				buf_add_str(&b, v);
				v = buf_view(&b);
			}
			f->generated_code_info = v, f->has_generated_code_info = true;
			break;
		}
	}
	return true;
}

bool gopp_parse_response(Str raw, GoppFile **files, int *n, Str *error)
{
	PbReader r = {(const uint8_t *)raw.p, (const uint8_t *)raw.p + raw.n};
	PbRawField fl;
	Vec v = {0};
	*error = (Str){"", 0};
	while (r.p < r.end) {
		if (!pb_next_field(&r, &fl))
			return false;
		if (fl.wt != WT_LEN)
			continue;
		if (fl.num == RESP_ERROR) {
			*error = (Str){(const char *)fl.val, fl.vlen};
		} else if (fl.num == RESP_FILE) {
			GoppFile *f = xalloc(sizeof *f);
			if (!parse_file(fl.val, fl.vlen, f))
				return false;
			vec_push(&v, f);
		}
	}
	GoppFile *out = xalloc(sizeof *out * (size_t)(v.n ? v.n : 1));
	for (int i = 0; i < v.n; i++)
		out[i] = *(GoppFile *)v.v[i];
	*files = out;
	*n = v.n;
	return true;
}
