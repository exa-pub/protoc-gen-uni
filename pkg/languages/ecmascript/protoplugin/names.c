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

/* Ported from packages/protoplugin/src/names.ts: the names of generated elements with name
 * clashes resolved, and import paths of generated files. */
#include "internal.h"

#include <string.h>

/* Well-known types are published with the runtime package. We usually want the generated code to
 * import them from the runtime package, with the following exceptions:
 * 1. We are bootstrapping the runtime package via the plugin option "bootstrap_wkt". In that
 *    case, we cannot refer to the runtime package itself.
 * 2. We were explicitly asked to generate the well-known type. */
const char *generate_file_path(const Schema *s, const DescFile *file)
{
	const char *pname = pb_str(file->h.proto, FDP_NAME);
	const char *wkt = V2_13(s) ? wkt_public_import_path_v2_13_0(pname) : wkt_public_import_path(pname);
	if (wkt && !s->opts.bootstrap_wkt && !map_has(&s->generating, file->name))
		return wkt;
	for (int i = 0; i < s->opts.map_imports.n; i++) {
		const PatternTarget *mi = s->opts.map_imports.v[i];
		if (import_pattern_match(mi->pattern, pname, true))
			return xcat(str_trim_suffix(mi->target, "/"), "/", file->name, "_pb.js");
	}
	return xcat("./", file->name, "_pb.js");
}

static const char *salt(int i)
{
	if (i == 0)
		return "";
	if (i == 1)
		return "$";
	return xfmt("$%d", i - 1);
}

/* An identifier based on the type name: the type name without the package, with dots replaced by
 * underscores. For example: ParentMsg_MyEnum. */
static const char *identifier(const Desc *d)
{
	const char *tn = "";
	switch (d->kind) {
	case DK_MESSAGE: tn = ((const DescMessage *)d)->type_name; break;
	case DK_ENUM: tn = ((const DescEnum *)d)->type_name; break;
	case DK_FIELD: tn = ((const DescField *)d)->type_name; break;
	case DK_SERVICE: tn = ((const DescService *)d)->type_name; break;
	}
	const char *pkg = pb_str(desc_file_of(d)->h.proto, FDP_PACKAGE);
	size_t off = *pkg ? strlen(pkg) + 1 : 0;
	if (off > strlen(tn))
		off = strlen(tn);
	char *r = xstrdup(tn + off);
	for (char *p = r; *p; p++)
		if (*p == '.')
			*p = '_';
	return r;
}

/* "file_" + the file name with runs of /[^a-zA-Z0-9_]+/ replaced by "_" */
static const char *file_ident(const DescFile *f)
{
	Buf b = {0};
	buf_adds(&b, "file_");
	bool run = false;
	for (const char *p = f->name; *p; p++) {
		char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			buf_addc(&b, c);
			run = false;
		} else if (!run) {
			buf_addc(&b, '_');
			run = true;
		}
	}
	return buf_str(&b);
}

static const char *register_name(Map *taken, const char *base, const char *suffix)
{
	for (int i = 0;; i++) {
		const char *name = safe_identifier(xcat(base, suffix, salt(i)));
		if (!map_has(taken, name)) {
			map_put(taken, name, (void *)1);
			return name;
		}
	}
}

static bool is_shape(const Desc *d) { return d->kind == DK_ENUM || d->kind == DK_MESSAGE; }

/* Assigns the names of all elements generated for a file (once per file). */
void all_names(DescFile *file)
{
	if (file->names_done)
		return;
	file->names_done = true;
	Map taken = {0};
	Vec nested = nested_types(&file->h);
	/* In the first pass, register shape names */
	for (int i = 0; i < nested.n; i++) {
		Desc *d = nested.v[i];
		if (is_shape(d))
			d->n_shape = register_name(&taken, identifier(d), "");
	}
	/* In the second pass, register desc names */
	file->h.n_desc = register_name(&taken, file_ident(file), "");
	for (int i = 0; i < nested.n; i++) {
		Desc *d = nested.v[i];
		d->n_desc = register_name(&taken, identifier(d), is_shape(d) ? "Schema" : "");
	}
	/* In the third pass, register json type names */
	for (int i = 0; i < nested.n; i++) {
		Desc *d = nested.v[i];
		if (is_shape(d))
			d->n_json = register_name(&taken, identifier(d), "Json");
	}
	for (int i = 0; i < nested.n; i++) {
		Desc *d = nested.v[i];
		if (is_shape(d))
			d->n_valid = register_name(&taken, identifier(d), "Valid");
	}
}
