/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protoplugin/src/parameter.ts and error.ts. The parameter is handled as
 * bytes: it is echoed into the file preamble. */
#include "internal.h"

#include <string.h>

void plugin_option_error(Str option, const char *detail)
{
	Buf b = {0};
	buf_adds(&b, "invalid option \"");
	buf_add_str(&b, option);
	buf_addc(&b, '"');
	if (detail && *detail)
		buf_adds(&b, xcat(": ", detail));
	fail_str(buf_view(&b));
}

bool has_target(const EsOptions *o, int t)
{
	for (int i = 0; i < o->n_targets; i++)
		if (o->targets[i] == t)
			return true;
	return false;
}

static void parse_bool(Str raw, Str value, bool *dst)
{
	if (str_eqc(value, "true") || str_eqc(value, "1"))
		*dst = true;
	else if (str_eqc(value, "false") || str_eqc(value, "0"))
		*dst = false;
	else
		plugin_option_error(raw, NULL);
}

/* "<pattern>:<target>" of rewrite_imports and map_imports. */
static PatternTarget *pattern_target(Str raw, Str value, bool rewrite)
{
	const char *c = memchr(value.p, ':', value.n);
	size_t i = c ? (size_t)(c - value.p) : 0;
	if (!c || (!rewrite && (i < 1 || i == value.n - 1)))
		plugin_option_error(raw, "must be in the form of <pattern>:<target>");
	PatternTarget *pt = xalloc(sizeof *pt);
	pt->pattern = xstrndup(value.p, i);
	pt->target = xstrndup(c + 1, value.n - i - 1);
	return pt;
}

void parse_parameter(Schema *s, const EsPlugin *p, Str parameter)
{
	EsOptions *o = &s->opts;
	o->targets[0] = TARGET_JS;
	o->targets[1] = TARGET_DTS;
	o->n_targets = 2;
	o->import_extension = "none";
	int n_extra = 0, n_sanitized = 0;
	Buf sanitized = {0}, extra_raw = {0};
	Str *parts = NULL;
	int n = parameter.n ? str_split_s(parameter, ',', &parts) : 0;
	RawOption *extra = xalloc(sizeof(RawOption) * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		Str raw = parts[i];
		const char *eq = memchr(raw.p, '=', raw.n);
		Str key = {raw.p, eq ? (size_t)(eq - raw.p) : raw.n};
		Str value = eq ? (Str){eq + 1, raw.n - key.n - 1} : (Str){"", 0};
		bool sanitize = false;
		if (str_eqc(key, "target")) {
			o->n_targets = 0;
			Str *ts;
			int nt = str_split_s(value, '+', &ts);
			for (int j = 0; j < nt; j++) {
				int t = str_eqc(ts[j], "js") ? TARGET_JS : str_eqc(ts[j], "ts") ? TARGET_TS
					: str_eqc(ts[j], "dts") ? TARGET_DTS : 0;
				if (!t)
					plugin_option_error(raw, NULL);
				if (!has_target(o, t))
					o->targets[o->n_targets++] = t;
			}
		} else if (str_eqc(key, "ts_nocheck")) {
			parse_bool(raw, value, &o->ts_nocheck);
		} else if (str_eqc(key, "elide_plugin_version")) {
			parse_bool(raw, value, &o->elide_plugin_version);
		} else if (str_eqc(key, "bootstrap_wkt")) {
			parse_bool(raw, value, &o->bootstrap_wkt);
		} else if (str_eqc(key, "rewrite_imports")) {
			vec_push(&o->rewrite_imports, pattern_target(raw, value, true));
			/* rewrite_imports can be noisy and is more of an implementation detail
			 * so we strip it out of the preamble */
			sanitize = true;
		} else if (str_eqc(key, "map_imports")) {
			vec_push(&o->map_imports, pattern_target(raw, value, false));
		} else if (str_eqc(key, "import_extension")) {
			if (str_eqc(value, "none") || !value.n)
				o->import_extension = "none";
			else if (str_eqc(value, "js") || str_eqc(value, ".js"))
				o->import_extension = "js";
			else if (str_eqc(value, "ts") || str_eqc(value, ".ts"))
				o->import_extension = "ts";
			else
				plugin_option_error(raw, NULL);
		} else if (str_eqc(key, "js_import_style")) {
			if (str_eqc(value, "module"))
				o->legacy_commonjs = false;
			else if (str_eqc(value, "legacy_commonjs"))
				o->legacy_commonjs = true;
			else
				plugin_option_error(raw, NULL);
		} else if (str_eqc(key, "keep_empty_files")) {
			parse_bool(raw, value, &o->keep_empty_files);
		} else {
			extra[n_extra++] = (RawOption){key, value};
			if (n_extra > 1)
				buf_addc(&extra_raw, ',');
			buf_add_str(&extra_raw, raw);
		}
		if (!sanitize) {
			if (n_sanitized++)
				buf_addc(&sanitized, ',');
			buf_add_str(&sanitized, raw);
		}
	}
	s->sanitized_parameter = buf_view(&sanitized);
	const char *err = p->parse_options(extra, n_extra, &s->plugin_options);
	if (err)
		plugin_option_error(buf_view(&extra_raw), err);
}
