/* Copyright (c) 2025-2026 Buf Technologies, Inc.
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
 * limitations under the License. */

/* protobuf/plugin/_run.py and _schema.py of protobuf-py v0.7.0: run() and the Schema. */
#include "internal.h"

#include <string.h>

enum {
	RESP_ERROR = 1, RESP_SUPPORTED_FEATURES = 2, RESP_MINIMUM_EDITION = 3, RESP_MAXIMUM_EDITION = 4,
	RESP_FILE = 15, RESP_FILE_NAME = 1, RESP_FILE_CONTENT = 15,
	FEATURE_PROTO3_OPTIONAL = 1, FEATURE_SUPPORTS_EDITIONS = 2,
};

static const char RUN_FW[] = PBPY_FRAME("_run.py", 162, "run",
	"fw_opts, remaining_parameter = parse_options(_FrameworkOptions, req.parameter)")
	"                                   ~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char RUN_PL[] = PBPY_FRAME("_run.py", 164, "run", "plugin_options = _parse_plugin_options(options, remaining_parameter)");
static const char PL_NONE[] = PBPY_FRAME("_run.py", 222, "_parse_plugin_options", "raise ValueError(msg)");
static const char PL_PARSE[] = PBPY_FRAME("_run.py", 226, "_parse_plugin_options", "result, unknown = parse_options(options, parameter)")
	"                      ~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^\n";
static const char PL_UNKNOWN[] = PBPY_FRAME("_run.py", 229, "_parse_plugin_options", "raise ValueError(msg)");

/* _FrameworkOptions */
static const PbpyOptDef framework_options[] = {
	{.name = "no_fmt_off", .type = PBPY_OPT_BOOL},
	{.name = "escape_module_with_hash", .type = PBPY_OPT_BOOL},
	{.name = "map_imports", .type = PBPY_OPT_DICT_STR},
};

const Vec *pbpy_files_to_generate(const PbpySchema *s) { return &s->files_to_generate; }
const Vec *pbpy_all_files(const PbpySchema *s) { return &s->all_files; }

static PbpyFile *register_file(PbpySchema *s, const char *path, const char *module)
{
	PbpyFile *f = pbpy_file_new(s, path, module);
	if (!map_has(&s->generated, path))
		vec_push(&s->paths, (void *)path);
	map_put(&s->generated, path, f);
	return f;
}

PbpyFile *pbpy_generate_file_desc(PbpySchema *s, const PbpyDescFile *desc, const char *suffix)
{
	/* the suffix may name another extension; the module stays a Python module path */
	const char *dot = strchr(suffix, '.');
	const char *base = dot ? xstrndup(suffix, (size_t)(dot - suffix)) : suffix;
	const char *module = pbpy_module_for_desc(desc, base, s->escape_module_with_hash);
	/* _output_path_from_module */
	const char *mp = module + 1;
	char *path = *mp ? str_replace_all(mp, ".", "/") : xstrdup("__init__");
	if (dot && dot[1])
		path = xcat(path, dot);
	return register_file(s, path, module);
}

PbpyFile *pbpy_generate_file_path(PbpySchema *s, const char *path)
{
	/* _module_from_path: "." + the parts joined with "." after cutting the last one at its first
	 * "." and dropping it if it is __init__ (computed in place: a part list costs a pointer and an
	 * allocation per part, too much for the deep paths of the __init__.py files). */
	const char *slash = strrchr(path, '/');
	const char *last = slash ? slash + 1 : path;
	size_t base = strcspn(last, ".");
	size_t n = base == 8 && !memcmp(last, "__init__", 8) ? (slash ? (size_t)(slash - path) : 0) : (size_t)(last - path) + base;
	char *module = xalloc(n + 2);
	module[0] = '.';
	for (size_t i = 0; i < n; i++)
		module[i + 1] = path[i] == '/' ? '.' : path[i];
	return register_file(s, xstrdup(path), module);
}

bool pbpy_schema_has_file(const PbpySchema *s, const char *path) { return map_has(&s->generated, path); }

/* The length of v as a varint. */
static size_t varint_len(uint64_t v)
{
	size_t n = 1;
	while (v >= 0x80)
		v >>= 7, n++;
	return n;
}

typedef struct {
	const PbpyPlugin *p;
	PbMsg *req;
	Str param;
	Buf files;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	const PbpyPlugin *p = c->p;
	PbpyOptValue fw[3] = {{0}};
	const char *remaining = pbpy_parse_options(framework_options, 3, c->param, fw, RUN_FW);
	/* _parse_plugin_options */
	PbpyOptValue *opts = xalloc(sizeof *opts * (size_t)(p->n_options > 0 ? p->n_options : 1));
	if (p->n_options < 0) {
		if (*remaining)
			pbpy_raise(xcat(RUN_PL, PL_NONE), "plugin does not accept options but got parameter: %s", py_repr_str(str_c(remaining)));
	} else {
		const char *unknown = pbpy_parse_options(p->options, p->n_options, str_c(remaining), opts, xcat(RUN_PL, PL_PARSE));
		if (*unknown)
			pbpy_raise(xcat(RUN_PL, PL_UNKNOWN), "unknown option(s): %s", unknown);
	}

	PbpySchema *s = xalloc(sizeof *s);
	s->plugin = p;
	s->parameter = xstrndup(c->param.p, c->param.n);
	s->escape_module_with_hash = fw[1].b;
	s->map_imports = pbpy_compile_map_imports(&fw[2].keys, &fw[2].values);

	PbMsg *req = c->req;
	for (int i = 0; i < pb_count(req, CGR_FILE_TO_GENERATE); i++)
		map_put(&s->file_to_generate, pb_str_at(req, CGR_FILE_TO_GENERATE, i), (void *)1);
	Map source_by_name = {0};
	for (int i = 0; i < pb_count(req, CGR_SOURCE_FILE_DESCRIPTORS); i++) {
		PbMsg *sf = pb_msg_at(req, CGR_SOURCE_FILE_DESCRIPTORS, i);
		map_put(&source_by_name, pb_str(sf, FDP_NAME), sf);
	}
	int min = p->minimum_edition ? p->minimum_edition : PBPY_MINIMUM_EDITION;
	int max = p->maximum_edition ? p->maximum_edition : PBPY_MAXIMUM_EDITION;
	int n = pb_count(req, CGR_PROTO_FILE);
	PbMsg **protos = xalloc(sizeof *protos * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		PbMsg *file = pb_msg_at(req, CGR_PROTO_FILE, i);
		PbMsg *src = map_get(&source_by_name, pb_str(file, FDP_NAME));
		protos[i] = src ? src : file;
		if (!map_has(&s->file_to_generate, pb_str(file, FDP_NAME)))
			continue;
		int edition = pbpy_file_edition(protos[i]);
		if (edition < min || edition > max)
			fail("ValueError: %s has edition %d, which is outside the supported range %d to %d", pb_str(file, FDP_NAME),
				edition, min, max);
	}
	PbpyRegistry reg = {0};
	for (int i = 0; i < n; i++)
		pbpy_add_file(&reg, protos[i]);
	for (int i = 0; i < n; i++) {
		PbpyDescFile *d = pbpy_registry_file(&reg, pb_str(pb_msg_at(req, CGR_PROTO_FILE, i), FDP_NAME));
		if (!d)
			fail("AssertionError: registry missing file %s", pb_str(pb_msg_at(req, CGR_PROTO_FILE, i), FDP_NAME));
		vec_push(&s->all_files, d);
		if (map_has(&s->file_to_generate, d->name))
			vec_push(&s->files_to_generate, d);
	}

	p->generate(s, opts);

	/* the response of a success, its fields in number order (files are written in place: a copy
	 * per file would double the memory of a large response) */
	Buf *o = &c->files;
	pb_put_tag(o, RESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(o, FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS);
	pb_put_tag(o, RESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(o, (uint64_t)min);
	pb_put_tag(o, RESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(o, (uint64_t)max);
	for (int i = 0; i < s->paths.n; i++) {
		const char *path = s->paths.v[i];
		Str name = str_c(path);
		Str content = pbpy_file_write(map_get(&s->generated, path), fw[0].b);
		pb_put_tag(o, RESP_FILE, WT_LEN);
		pb_put_varint(o, 2 + varint_len(name.n) + name.n + varint_len(content.n) + content.n);
		pb_put_bytes(o, RESP_FILE_NAME, name);
		pb_put_bytes(o, RESP_FILE_CONTENT, content);
	}
}

static void parse(void *arg)
{
	RunCtx *c = arg;
	c->req = pbpy_from_binary(&pbpy_CodeGeneratorRequest, (Str){c->param.p, c->param.n});
}

const char *pbpy_run(const PbpyPlugin *p, Str raw, Str param, Buf *out)
{
	/* CodeGeneratorRequest.from_binary runs outside the try: an exception ends the plugin */
	RunCtx c = {p, NULL, raw, {0}};
	const char *err = try_run(parse, &c);
	if (err)
		return err;
	c.param = param;
	if (memchr(param.p, 0, param.n)) {
		pb_put_bytes(out, RESP_ERROR, STR_LIT("unsupported by protoc-gen-uni: a parameter with a NUL character\n"));
		return NULL;
	}
	if (try_run(run, &c)) {
		/* traceback.format_exc(): exact for the option errors, else only its last line */
		Buf msg = {0};
		buf_add_str(&msg, fail_message());
		buf_addc(&msg, '\n');
		pb_put_bytes(out, RESP_ERROR, buf_view(&msg));
		return NULL;
	}
	if (out->n)
		buf_add(out, c.files.p, c.files.n);
	else
		*out = c.files;
	return NULL;
}
