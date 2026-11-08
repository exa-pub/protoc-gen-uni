/* Port of pkg/uni of the Go implementation (uni.go). */
#include "uni.h"

#include <string.h>

const char *uni_split_parameter(Str parameter, Str *name, Str *version, Str *rest)
{
	*name = *version = *rest = (Str){"", 0};
	if (!parameter.n)
		return NULL;
	Str *elems;
	int n = str_split_s(parameter, ',', &elems);
	Buf kept = {0};
	bool seen_name = false, seen_version = false, first = true;
	for (int i = 0; i < n; i++) {
		Str e = elems[i];
		const char *eq = memchr(e.p, '=', e.n);
		Str key = {e.p, eq ? (size_t)(eq - e.p) : e.n};
		Str value = eq ? (Str){eq + 1, e.n - key.n - 1} : (Str){"", 0};
		key = go_trim_space(key);
		if (str_eqc(key, UNI_PARAM_NAME)) {
			if (seen_name)
				return "protoc-gen-uni: duplicate parameter \"" UNI_PARAM_NAME "\"";
			seen_name = true, *name = go_trim_space(value);
		} else if (str_eqc(key, UNI_PARAM_VERSION)) {
			if (seen_version)
				return "protoc-gen-uni: duplicate parameter \"" UNI_PARAM_VERSION "\"";
			seen_version = true, *version = go_trim_space(value);
		} else {
			if (!first)
				buf_addc(&kept, ',');
			first = false;
			buf_add_str(&kept, e);
		}
	}
	*rest = buf_view(&kept);
	return NULL;
}

/* Sorts the strings in v (byte order) and removes duplicates. */
static void sort_unique(Vec *v)
{
	for (int i = 1; i < v->n; i++) {
		void *x = v->v[i];
		int j = i - 1;
		for (; j >= 0 && strcmp(v->v[j], x) > 0; j--)
			v->v[j + 1] = v->v[j];
		v->v[j + 1] = x;
	}
	int k = 0;
	for (int i = 0; i < v->n; i++)
		if (!k || !str_eq(v->v[k - 1], v->v[i]))
			v->v[k++] = v->v[i];
	v->n = k;
}

const char *uni_lookup(const UniGenerator *const *gens, int n, Str name, Str version, const UniGenerator **out)
{
	if (!name.n || !version.n)
		return "protoc-gen-uni: parameters " UNI_PARAM_NAME "=<owner>/<plugin> and " UNI_PARAM_VERSION
		       "=<version> are required, e.g. " UNI_PARAM_NAME "=bufbuild/es," UNI_PARAM_VERSION "=v2.17.0";
	Vec versions = {0}, names = {0};
	for (int i = 0; i < n; i++) {
		vec_push(&names, (void *)gens[i]->name);
		if (!str_eqc(name, gens[i]->name))
			continue;
		if (str_eqc(version, gens[i]->version)) {
			*out = gens[i];
			return NULL;
		}
		vec_push(&versions, (void *)gens[i]->version);
	}
	if (!versions.n) {
		sort_unique(&names);
		return xcat("protoc-gen-uni: unknown generator ", quote_go(name), ", available: ", str_join(&names, ", "));
	}
	sort_unique(&versions);
	return xcat("protoc-gen-uni: unknown version ", quote_go(version), " of ", quote_go(name),
		    ", available: ", str_join(&versions, ", "));
}

/* Dispatches req; partial: req holds only the parameter of a request that pb_decode rejected as
 * nested deeper than PB_MAX_DEPTH (see UniGenerator.parses_raw). */
static const char *dispatch(PbMsg *req, Str raw, const UniGenerator *const *gens, int n, Buf *out, bool partial)
{
	Str name, version, rest;
	const UniGenerator *g = NULL;
	const char *err = uni_split_parameter(pb_bytes(req, CGR_PARAMETER), &name, &version, &rest);
	if (!err)
		err = uni_lookup(gens, n, name, version, &g);
	if (err) {
		pb_put_bytes(out, CGRESP_ERROR, str_c(err));
		return NULL;
	}
	if (partial && !g->parses_raw)
		return "messages nested deeper than 100 levels are not supported";
	pb_set_bytes(req, CGR_PARAMETER, rest.n ? rest : (Str){NULL, 0});
	return g->generate(req, raw, out);
}

const char *uni_run(PbMsg *req, Str raw, const UniGenerator *const *gens, int n, Buf *out)
{
	return dispatch(req, raw, gens, n, out, false);
}

/* Skips the rest of the group num at r iteratively: unlike pb_skip_value, without a depth limit
 * (the stack of open groups is on the arena, in proportion to the input). */
static COLD bool skip_group(PbReader *r, uint32_t num)
{
	Vec open = {0};
	vec_push(&open, (void *)(uintptr_t)num);
	while (open.n) {
		uint32_t n2;
		int wt;
		if (!pb_read_tag(r, &n2, &wt))
			return false;
		if (wt == WT_EGROUP) {
			if ((uintptr_t)open.v[--open.n] != n2)
				return false;
		} else if (wt == WT_SGROUP) {
			vec_push(&open, (void *)(uintptr_t)n2);
		} else if (!pb_skip_value(r, n2, wt, NULL, NULL, 0)) {
			return false;
		}
	}
	return true;
}

/* A request with only the parameter of the request in[0:len] (the last length-delimited field 2
 * of the top level, as pb_decode takes it), or NULL if the top level is malformed. */
static COLD PbMsg *parameter_only(const uint8_t *in, size_t len)
{
	PbMsg *req = pb_new(&pbd_CodeGeneratorRequest);
	PbReader r = {in, in + len};
	while (r.p < r.end) {
		uint32_t num;
		int wt;
		const uint8_t *v;
		size_t vn;
		if (!pb_read_tag(&r, &num, &wt) || wt == WT_EGROUP)
			return NULL;
		if (wt == WT_SGROUP) {
			if (!skip_group(&r, num))
				return NULL;
			continue;
		}
		if (!pb_skip_value(&r, num, wt, &v, &vn, 0))
			return NULL;
		if (num == CGR_PARAMETER && wt == WT_LEN)
			pb_set_bytes(req, CGR_PARAMETER, (Str){xstrndup((const char *)v, vn), vn});
	}
	return req;
}

const char *uni_main(const uint8_t *in, size_t len, const UniGenerator *const *gens, int n, Buf *out)
{
	PbMsg *req = pb_new(&pbd_CodeGeneratorRequest);
	bool partial = false;
	if (!pb_decode(req, in, len)) {
		/* too deep for pkg/pb: generators that parse the request themselves may still take it */
		if (!pb_too_deep)
			return "protoc-gen-uni: parse request: proto: cannot parse invalid wire-format data";
		req = parameter_only(in, len);
		if (!req)
			return "protoc-gen-uni: parse request: messages nested deeper than 100 levels are not supported";
		partial = true;
	}
	const char *err = dispatch(req, (Str){(const char *)in, len}, gens, n, out, partial);
	return err ? xcat("protoc-gen-uni: parse request: ", err) : NULL;
}
