/* Port of Params, Param and InvalidParameter of src/lib.rs of protoc-gen-prost 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0).
 *
 * Upstream iterates over the matches of the regex
 *
 *	(?:(?P<param>[^,=]+)(?:=(?P<key>[^,=]+)(?:=(?P<value>(?:[^,\\]|\\,|\\\\)+))?)?)
 *
 * which supports `parameter`, `parameter=key` and `parameter=key=value`: `parameter` ends at the
 * first `=` or `,`, `key` likewise, and `value` ends at a `,` that is not escaped as `\,`. Text
 * that no match covers (separators, stray `=`) is skipped. */
#include "prost.h"
#include "rust_std.h"

#include <string.h>

static bool is_sep(char c) { return c == ',' || c == '='; }

int prost_params_parse(Str s, ProstParam **out)
{
	Vec v = {0};
	size_t i = 0;
	while (i < s.n) {
		if (is_sep(s.p[i])) {
			i++;
			continue;
		}
		size_t ps = i;
		while (i < s.n && !is_sep(s.p[i]))
			i++;
		ProstParam *p = xalloc(sizeof *p);
		Str param = rust_trim((Str){s.p + ps, i - ps});
		p->param = xstrndup(param.p, param.n);
		p->kind = PROST_PARAMETER;
		vec_push(&v, p);
		if (i + 1 >= s.n || s.p[i] != '=' || is_sep(s.p[i + 1]))
			continue;
		size_t ks = ++i;
		while (i < s.n && !is_sep(s.p[i]))
			i++;
		const char *key = xstrndup(s.p + ks, i - ks);
		p->kind = PROST_VALUE;
		p->value = key;
		if (i >= s.n || s.p[i] != '=')
			continue;
		size_t vs = i + 1, ve = vs;
		while (ve < s.n) {
			if (s.p[ve] != ',' && s.p[ve] != '\\')
				ve++;
			else if (s.p[ve] == '\\' && ve + 1 < s.n && (s.p[ve + 1] == ',' || s.p[ve + 1] == '\\'))
				ve += 2;
			else
				break;
		}
		if (ve == vs) /* the optional `=value` does not match: the `=` stays unmatched */
			continue;
		p->kind = PROST_KEY_VALUE;
		p->key = key;
		p->value = str_replace_all(str_replace_all(xstrndup(s.p + vs, ve - vs), "\\,", ","), "\\\\", "\\");
		i = ve;
	}
	ProstParam *arr = xalloc(sizeof *arr * (size_t)(v.n ? v.n : 1));
	for (int k = 0; k < v.n; k++)
		arr[k] = *VEC_AT(v, ProstParam, k);
	*out = arr;
	return v.n;
}

const char *prost_param_value(const ProstParam *p) { return p->kind == PROST_PARAMETER ? NULL : p->value; }

bool prost_param_flag(const ProstParam *p, const char *name, bool *on)
{
	if (!str_eq(p->param, name))
		return false;
	if (p->kind == PROST_PARAMETER || (p->kind == PROST_VALUE && str_eq(p->value, "true"))) {
		*on = true;
		return true;
	}
	return p->kind == PROST_VALUE && str_eq(p->value, "false");
}

char *prost_invalid_parameter(const ProstParam *p)
{
	switch (p->kind) {
	case PROST_PARAMETER:
		return xcat("invalid parameter: ", p->param);
	case PROST_VALUE:
		return xcat("invalid parameter: ", p->param, "=", p->value);
	default:
		return xcat("invalid parameter: ", p->param, "=", p->key, "=",
			    str_replace_all(str_replace_all(p->value, "\\", "\\\\"), ",", "\\,"));
	}
}
