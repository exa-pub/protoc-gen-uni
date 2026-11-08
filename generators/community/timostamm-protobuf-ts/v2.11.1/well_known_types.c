/* Copyright (c) 2016 Timo Stamm
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

/* Ported from packages/plugin/src/message-type-extensions/well-known-types.ts and
 * google-types.ts with framework/typescript-method-from-text.ts. Upstream writes the methods as
 * text with the names it imports interpolated and parses each with ts.createSourceFile; here the
 * parsed trees are decoded from well_known_types_trees.c (ts_decode_parsed), with the same names
 * as parameters, and turned into methods the same way. */

typedef struct {
	const char *type_name;
	const char *params; /* see well_known_types_trees.c */
	uint16_t code;      /* offset into snippet_code */
	uint8_t n_methods;
} SnippetHandler;

#include "well_known_types_trees.c"

/* The handler's parameters, computed in upstream's order (imports are listed in reverse order of
 * first use). */
static const char **snippet_params(OutFile *f, DescMessage *m, const char *spec, int *n)
{
	Vec parts = str_split(spec, ',');
	const char **params = xalloc(sizeof *params * (size_t)(parts.n + 1));
	for (int i = 0; i < parts.n; i++) {
		const char *p = parts.v[i];
		const char *arg = p[1] == ':' ? p + 2 : "";
		switch (p[0]) {
		case 'S': params[i] = import_type(&G.imports, f, &m->h, "default", false); break;
		case 'T':
		case 'N': params[i] = import_name(f, arg, G.o->runtime_import_path, p[0] == 'T'); break;
		case 'L': params[i] = create_typescript_local_name(arg, G.o); break;
		case 'C':
			params[i] = G.o->normal_long_type == RT_LONG_NUMBER   ? "toNumber"
				    : G.o->normal_long_type == RT_LONG_STRING ? "toString"
									      : "toBigInt";
			break;
		case 'E':
			params[i] = G.o->normal_long_type == RT_LONG_NUMBER   ? "NUMBER"
				    : G.o->normal_long_type == RT_LONG_STRING ? "STRING"
									      : "BIGINT";
			break;
		}
	}
	*n = parts.n;
	return params;
}

/* WellKnownTypes.make / GoogleTypes.make with typescriptMethodFromText */
static TsNodeArray *snippet_make(OutFile *f, DescMessage *m, bool google_type)
{
	/* this[descMessage.typeName] also finds the members of the class and of Object.prototype;
	 * calling those fails (or recurses, for "make") */
	static const char *const fails[] = {"constructor", "make", "imports", "options", "toString", "toLocaleString",
					    "__defineGetter__", "__defineSetter__", "__proto__"};
	for (size_t i = 0; i < sizeof fails / sizeof *fails; i++)
		if (str_eq(m->type_name, fails[i]))
			fail("TypeError: cannot generate methods for message %s (it names a property of %s)", m->type_name,
			     google_type ? "GoogleTypes" : "WellKnownTypes");
	if (has_prefix(m->type_name, "google.type.") != google_type)
		return NULL;
	for (size_t h = 0; h < sizeof snippet_handlers / sizeof *snippet_handlers; h++) {
		const SnippetHandler *sh = &snippet_handlers[h];
		if (!str_eq(sh->type_name, m->type_name))
			continue;
		int n;
		const char **params = snippet_params(f, m, sh->params, &n);
		const uint8_t *code = snippet_code + sh->code;
		TsNodeArray *methods = ts_list_new();
		for (int i = 0; i < sh->n_methods; i++) {
			Vec docs = {0};
			TsNode *fn;
			while ((fn = ts_decode_parsed(&code, snippet_strings, params, n))->kind == TS_JSDocComment)
				vec_push(&docs, fn);
			TsNode *method = ts_create_method(fn->decorators, fn->modifiers, fn->asterisk_token, fn->name, fn->question_token,
							  fn->type_parameters, fn->parameters, fn->type, fn->body);
			for (int j = 0; j < docs.n; j++)
				add_comment_block_as_js_doc(method, ((TsNode *)docs.v[j])->text);
			ts_list_push(methods, method);
		}
		return methods;
	}
	return NULL;
}

static TsNodeArray *wkt_make(OutFile *f, DescMessage *m) { return snippet_make(f, m, false); }
static TsNodeArray *google_types_make(OutFile *f, DescMessage *m) { return snippet_make(f, m, true); }
