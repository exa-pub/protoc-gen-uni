/* Copyright (c) 2019 Daniel G. Taylor, MIT (license text in plugin.c). */

/* community/danielgtaylor-betterproto v1.2.5: a port of protoc-gen-python_betterproto of
 * betterproto 1.2.5 (buf.build/community/danielgtaylor-betterproto,
 * https://github.com/danielgtaylor/python-betterproto), which generates dataclass-based Python
 * messages and grpclib service stubs, formatted with black 24.3.0
 * (pkg/languages/python/formatters/black). The case conversions of stringcase 1.2.0 live in
 * pkg/languages/python/stringcase, textwrap and str semantics in pkg/languages/python/std.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "black.h"
#include "python.h"
#include "stringcase.h"
#include "uni.h"

#include <string.h>

#include "plugin.c"
#include "template.c"

/* ---- posixpath ---- */

/* os.path.splitext(p)[0] */
static const char *splitext_root(const char *p)
{
	const char *sep = strrchr(p, '/'), *dot = strrchr(p, '.');
	if (dot && (!sep || dot > sep)) {
		for (const char *f = sep ? sep + 1 : p; f < dot; f++)
			if (*f != '.')
				return xstrndup(p, (size_t)(dot - p));
	}
	return p;
}

/* os.path.dirname(p) */
static const char *dirname_of(const char *p)
{
	const char *sep = strrchr(p, '/');
	if (!sep)
		return "";
	size_t i = (size_t)(sep - p) + 1;
	bool all_slash = true;
	for (size_t k = 0; k < i; k++)
		if (p[k] != '/')
			all_slash = false;
	if (!all_slash)
		while (i && p[i - 1] == '/')
			i--;
	return xstrndup(p, i);
}

/* os.path.join(a, b) */
static const char *path_join(const char *a, const char *b)
{
	if (*b == '/')
		return b;
	if (!*a || a[strlen(a) - 1] == '/')
		return xcat(a, b);
	return xcat(a, "/", b);
}

static void add_file(Buf *files, const char *name, const char *content)
{
	Buf file = {0};
	pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(name));
	pb_put_bytes(&file, CGRESP_FILE_CONTENT, str_c(content));
	pb_put_bytes(files, CGRESP_FILE, buf_view(&file));
}

/* generate_code(request, response) */
static COLD void generate_code(PbMsg *req, Buf *files)
{
	Vec outputs = {0};
	Map by_key = {0};
	int n = pb_count(req, CGR_PROTO_FILE);
	for (int i = 0; i < n; i++) {
		PbMsg *proto_file = pb_msg_at(req, CGR_PROTO_FILE, i);
		const char *package = text(pb_bytes(proto_file, FDP_PACKAGE), "package");
		const char *out = package;
		if (!strcmp(out, "google.protobuf"))
			continue;
		dotted(str_c(package), "package", true);
		const char *name = text(pb_bytes(proto_file, FDP_NAME), "file name");
		for (const char *q = name; *q; q++)
			if ((unsigned char)*q < 0x20 || *q == 0x7f)
				fail("unsupported by protoc-gen-uni: control character in file name %s", py_repr_str(str_c(name)));
		if (!*out)
			out = str_replace_all(splitext_root(name), "/", ".");
		Output *o = map_get(&by_key, out);
		if (!o) {
			o = xalloc(sizeof *o);
			o->filename = out;
			o->package = package;
			map_put(&by_key, out, o);
			vec_push(&outputs, o);
		}
		vec_push(&o->files, (void *)name);
		vec_push(&o->protos, proto_file);
	}

	Vec names = {0};
	for (int i = 0; i < outputs.n; i++) {
		Output *o = VEC_AT(outputs, Output, i);
		for (int j = 0; j < o->protos.n; j++)
			process_file(o, VEC_AT(o->protos, PbMsg, j));
		const char *fname = xcat(str_replace_all(o->filename, ".", "/"), ".py");
		add_file(files, fname, render(o));
		vec_push(&names, (void *)fname);
	}

	/* __init__.py of every directory; os.path.exists("__init__.py") is false in the plugin's
	 * empty working directory, os.makedirs succeeds for relative paths */
	Map inits = {0};
	Vec init_order = {0};
	set_add(&inits, &init_order, "", (void *)1);
	for (int i = 0; i < names.n; i++) {
		const char *dirnames = dirname_of(VEC_AT(names, char, i));
		if (!*dirnames)
			continue;
		Vec parts = str_split(dirnames, '/');
		if (*dirnames == '/' || strlen(dirnames) > 4000)
			fail("unsupported by protoc-gen-uni: output directory %s (os.makedirs)", py_repr_str(str_c(dirnames)));
		const char *base = "";
		for (int k = 0; k < parts.n; k++) {
			const char *part = VEC_AT(parts, char, k);
			if (!strcmp(part, "..") || strlen(part) > 255)
				fail("unsupported by protoc-gen-uni: output directory %s (os.makedirs)",
				     py_repr_str(str_c(dirnames)));
			base = path_join(base, part);
			set_add(&inits, &init_order, base, (void *)1);
		}
	}
	for (int i = 0; i < init_order.n; i++)
		add_file(files, path_join(VEC_AT(init_order, char, i), "__init__.py"), "");
}

typedef struct {
	PbMsg *req;
	Buf files;
} RunCtx;

static void run(void *arg)
{
	RunCtx *c = arg;
	generate_code(c->req, &c->files);
}

static const char *generate(PbMsg *req, Str raw, Buf *out)
{
	(void)raw;
	RunCtx c = {req, {0}};
	if (try_run(run, &c)) {
		/* Upstream raises an exception: a traceback on stderr and exit status 1. */
		Buf msg = {0};
		if (!str_has_prefix(fail_message(), STR_LIT("unsupported by protoc-gen-uni")))
			buf_adds(&msg, "protoc-gen-python_betterproto: ");
		buf_add_str(&msg, fail_message());
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&msg));
		return NULL;
	}
	/* supported_features is not set: no proto3 optional, no editions */
	buf_add(out, c.files.p, c.files.n);
	return NULL;
}

const UniGenerator gen_community_danielgtaylor_betterproto_v1_2_5 = {
	.name = "community/danielgtaylor-betterproto",
	.version = "v1.2.5",
	.generate = generate,
};
