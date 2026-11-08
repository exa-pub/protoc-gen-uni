/* Copyright 2018 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 compiler/protogen/protogen.go: GeneratedFile
 * (P, QualifiedGoIdent, Content, generatedCodeInfo, metaFile) and Plugin.Response;
 * internal/encoding/text (string quoting of the compact prototext of .meta files). */
#include "internal.h"

#include "go_std.h"

#include <stdlib.h>
#include <string.h>

static char map_true;
#define MAP_TRUE ((void *)&map_true)

struct PgGeneratedFile {
	PgPlugin *gen;
	bool skip;
	const char *filename, *goImportPath;
	Buf buf;
	Map packageNames;     /* import path -> package name */
	Vec packageNameKeys;  /* import paths in packageNames */
	Map usedPackageNames; /* package name -> MAP_TRUE */
	Map manualImports;    /* import path -> MAP_TRUE */
	Vec manualImportKeys;
	Map annotations;      /* symbol -> Vec of PgAnnotation* */
	Vec annotationKeys;
	bool stripForEditionsDiff;
};

/* go/types.Universe.Names() of Go 1.26.5: all predeclared identifiers. */
static const char *const universe_names[] = {
	"any", "append", "bool", "byte", "cap", "clear", "close", "comparable", "complex", "complex128", "complex64",
	"copy", "delete", "error", "false", "float32", "float64", "imag", "int", "int16", "int32", "int64", "int8", "iota",
	"len", "make", "max", "min", "new", "nil", "panic", "print", "println", "real", "recover", "rune", "string", "true",
	"uint", "uint16", "uint32", "uint64", "uint8", "uintptr",
};

PgGeneratedFile *pg_new_generated_file(PgPlugin *gen, const char *filename, const char *go_import_path)
{
	PgGeneratedFile *g = xalloc(sizeof *g);
	g->gen = gen;
	g->filename = filename;
	g->goImportPath = go_import_path;
	g->stripForEditionsDiff = pg_plugin_internal_strip_for_editions_diff(gen);
	/* All predeclared identifiers in Go are already used. */
	for (size_t i = 0; i < sizeof universe_names / sizeof *universe_names; i++)
		map_put(&g->usedPackageNames, universe_names[i], MAP_TRUE);
	vec_push(&gen->genFiles, g);
	return g;
}

const char *pg_qualified_go_ident(PgGeneratedFile *g, PgGoIdent ident)
{
	if (str_eq(ident.GoImportPath, g->goImportPath))
		return ident.GoName;
	const char *name = map_get(&g->packageNames, ident.GoImportPath);
	if (name)
		return xcat(name, ".", ident.GoName);
	const char *orig = pg_clean_package_name(pg_path_base(ident.GoImportPath));
	const char *pkg = orig;
	for (int i = 1; map_get(&g->usedPackageNames, pkg); i++)
		pkg = xcat(orig, go_itoa(i));
	map_put(&g->packageNames, ident.GoImportPath, (void *)pkg);
	vec_push(&g->packageNameKeys, (void *)ident.GoImportPath);
	map_put(&g->usedPackageNames, pkg, MAP_TRUE);
	return xcat(pkg, ".", ident.GoName);
}

void pg_import(PgGeneratedFile *g, const char *import_path)
{
	if (!map_get(&g->manualImports, import_path))
		vec_push(&g->manualImportKeys, (void *)import_path);
	map_put(&g->manualImports, import_path, MAP_TRUE);
}

void pg_skip(PgGeneratedFile *g) { g->skip = true; }
void pg_unskip(PgGeneratedFile *g) { g->skip = false; }
bool pg_generated_file_internal_strip_for_editions_diff(const PgGeneratedFile *g) { return g->stripForEditionsDiff; }
void pg_write(PgGeneratedFile *g, Str s) { buf_add_str(&g->buf, s); }

void pg_annotate_symbol(PgGeneratedFile *g, const char *symbol, PgAnnotation info)
{
	Vec *v = map_get(&g->annotations, symbol);
	if (!v) {
		v = xalloc(sizeof *v);
		map_put(&g->annotations, symbol, v);
		vec_push(&g->annotationKeys, (void *)symbol);
	}
	PgAnnotation *a = xalloc(sizeof *a);
	*a = info;
	vec_push(v, a);
}

/* trailingComment.String() */
static Str trailing_comment_string(PgComments c)
{
	Str s = str_trim_suffix_s(pg_comments_string(c), STR_LIT("\n"));
	if (memchr(s.p, '\n', s.n))
		return (Str){"", 0}; /* multi-lined trailing comments are not supported */
	return s;
}

void pg_p(PgGeneratedFile *g, const PgArg *args, int n)
{
	for (int i = 0; i < n; i++) {
		const PgArg *a = &args[i];
		switch (a->kind) {
		case PGA_S: buf_adds(&g->buf, a->u.s); break;
		case PGA_STR: buf_add_str(&g->buf, a->u.str); break;
		case PGA_ID: buf_adds(&g->buf, pg_qualified_go_ident(g, a->u.id)); break;
		case PGA_I: buf_adds(&g->buf, go_itoa(a->u.i)); break;
		case PGA_COMMENTS: buf_add_str(&g->buf, pg_comments_string((PgComments){a->u.str})); break;
		case PGA_TRAILING: buf_add_str(&g->buf, trailing_comment_string((PgComments){a->u.str})); break;
		}
	}
	buf_addc(&g->buf, '\n');
}

static int cmp_import(const void *a, const void *b)
{
	return strcmp(((const go_std_import *)a)->path, ((const go_std_import *)b)->path);
}

/* The bad code with line numbers (bufio.Scanner lines, "%5d\t%s\n"); a debugging aid upstream. */
static void numbered_source(Buf *out, Str src)
{
	size_t start = 0;
	for (int line = 1; start < src.n; line++) {
		const char *nl = memchr(src.p + start, '\n', src.n - start);
		size_t end = nl ? (size_t)(nl - src.p) : src.n;
		if (end - start >= 64 * 1024)
			return; /* bufio.ErrTooLong ends the scan */
		size_t e = end;
		if (e > start && src.p[e - 1] == '\r')
			e--; /* dropCR */
		const char *num = go_itoa(line);
		for (size_t k = strlen(num); k < 5; k++)
			buf_addc(out, ' ');
		buf_adds(out, num);
		buf_addc(out, '\t');
		buf_add(out, src.p + start, e - start);
		buf_addc(out, '\n');
		start = nl ? end + 1 : src.n;
	}
}

bool pg_content(PgGeneratedFile *g, Str *out, Str *err)
{
	if (!has_suffix(g->filename, ".go")) {
		*out = buf_view(&g->buf);
		if (!out->p)
			*out = (Str){"", 0};
		return true;
	}
	/* Collect a sorted list of all imports. */
	int ni = g->packageNameKeys.n + g->manualImportKeys.n;
	go_std_import *imports = xalloc(sizeof *imports * (size_t)(ni ? ni : 1));
	int k = 0;
	for (int i = 0; i < g->packageNameKeys.n; i++) {
		const char *path = g->packageNameKeys.v[i];
		const char *rewritten = g->gen->opts.ImportRewriteFunc ? g->gen->opts.ImportRewriteFunc(g->gen->opts.ImportRewriteCtx, path) : path;
		imports[k++] = (go_std_import){map_get(&g->packageNames, path), rewritten};
	}
	for (int i = 0; i < g->manualImportKeys.n; i++) {
		const char *path = g->manualImportKeys.v[i];
		if (map_get(&g->packageNames, path))
			continue;
		const char *rewritten = g->gen->opts.ImportRewriteFunc ? g->gen->opts.ImportRewriteFunc(g->gen->opts.ImportRewriteCtx, path) : path;
		imports[k++] = (go_std_import){"_", rewritten};
	}
	qsort(imports, (size_t)k, sizeof *imports, cmp_import);

	Str original = buf_view(&g->buf);
	if (!original.p)
		original = (Str){"", 0};
	Buf formatted = {0};
	const char *perr = go_std_format_content(original.p, original.n, imports, (size_t)k, &formatted);
	if (perr) {
		Buf e = {0};
		buf_adds(&e, xcat(g->filename, ": unparsable Go source: ", perr, "\n"));
		numbered_source(&e, original);
		*err = buf_view(&e);
		return false;
	}
	*out = buf_view(&formatted);
	if (!out->p)
		*out = (Str){"", 0};
	return true;
}

/* internal/encoding/text appendString (outputASCII false) */
static void text_append_string(Buf *out, const char *in)
{
	static const char hex[] = "0123456789abcdef";
	buf_addc(out, '"');
	size_t n = strlen(in);
	for (size_t i = 0; i < n;) {
		uint32_t r;
		int w = utf8_decode(in + i, n - i, &r);
		if (r == 0xfffd && w == 1)
			r = (unsigned char)in[i];
		if ((r == (unsigned char)in[i] && w == 1 && r >= 0x80) || r < ' ' || r == '"' || r == '\\' || r == 0x7f) {
			buf_addc(out, '\\');
			if (r == '"' || r == '\\') {
				buf_addc(out, (char)r);
			} else if (r == '\n') {
				buf_addc(out, 'n');
			} else if (r == '\r') {
				buf_addc(out, 'r');
			} else if (r == '\t') {
				buf_addc(out, 't');
			} else {
				buf_addc(out, 'x');
				if (r < 16)
					buf_addc(out, '0');
				if (r >= 16)
					buf_addc(out, hex[r >> 4]);
				buf_addc(out, hex[r & 15]);
			}
		} else if (r >= 0x80 && r <= 0x9f) {
			buf_adds(out, "\\u00");
			buf_addc(out, hex[r >> 4]);
			buf_addc(out, hex[r & 15]);
		} else {
			buf_add(out, in + i, (size_t)w);
		}
		i += (size_t)w;
	}
	buf_addc(out, '"');
}

/* metaFile: the compact text format of the GeneratedCodeInfo (prototext.Marshal; protobuf-go
 * randomly doubles some spaces per binary, this writes single spaces). */
static bool meta_file(PgGeneratedFile *g, Str content, Str *out, Str *err)
{
	go_std_ident *ids;
	size_t nids;
	const char *perr = go_std_decl_idents(content.p, content.n, &ids, &nids);
	if (perr) {
		*err = str_c(perr);
		return false;
	}
	Map seen = {0};
	Buf b = {0};
	/* prototext.Marshal (single line): the space between fields, doubled by detrand.Bool() */
	const char *sp = go_detrand ? "  " : " ";
	for (size_t i = 0; i < nids; i++) {
		map_put(&seen, ids[i].symbol, MAP_TRUE);
		Vec *as = map_get(&g->annotations, ids[i].symbol);
		for (int j = 0; as && j < as->n; j++) {
			const PgAnnotation *a = as->v[j];
			if (b.n)
				buf_adds(&b, sp);
			buf_adds(&b, "annotation:{");
			for (int k = 0; k < a->Location.nPath; k++)
				buf_adds(&b, xcat("path:", go_itoa(a->Location.Path[k]), sp));
			buf_adds(&b, "source_file:");
			text_append_string(&b, a->Location.SourceFile);
			buf_adds(&b, xcat(sp, "begin:", go_itoa(ids[i].begin), sp, "end:", go_itoa(ids[i].end)));
			if (a->Semantic) {
				static const char *const sem[] = {"NONE", "SET", "ALIAS"};
				int s = a->Semantic - 1;
				buf_adds(&b, xcat(sp, "semantic:", s >= 0 && s < 3 ? sem[s] : go_itoa(s)));
			}
			buf_addc(&b, '}');
		}
	}
	for (int i = 0; i < g->annotationKeys.n; i++)
		if (!map_get(&seen, g->annotationKeys.v[i])) {
			*err = str_c(xcat(g->filename, ": no symbol matching annotation ", quote_go(str_c(g->annotationKeys.v[i]))));
			return false;
		}
	*out = buf_view(&b);
	if (!out->p)
		*out = (Str){"", 0};
	return true;
}

static void put_file(Buf *resp, const char *name, Str content)
{
	Buf f = {0};
	pb_put_bytes(&f, CGRESP_FILE_NAME, str_c(name));
	pb_put_bytes(&f, CGRESP_FILE_CONTENT, content);
	pb_put_bytes(resp, CGRESP_FILE, buf_view(&f));
}

static void error_only(Buf *out, Str err)
{
	out->n = 0;
	pb_put_bytes(out, CGRESP_ERROR, err);
}

void pg_plugin_response(PgPlugin *gen, Buf *out)
{
	Buf head = {0}, files = {0};
	/* Always report the support for editions. */
	if (gen->SupportedFeatures > 0) {
		pb_put_tag(&head, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
		pb_put_varint(&head, gen->SupportedFeatures);
	}
	if (gen->SupportedEditionsMinimum != 0 && gen->SupportedEditionsMaximum != 0) {
		pb_put_tag(&head, CGRESP_MINIMUM_EDITION, WT_VARINT);
		pb_put_varint(&head, (uint64_t)(int64_t)gen->SupportedEditionsMinimum);
		pb_put_tag(&head, CGRESP_MAXIMUM_EDITION, WT_VARINT);
		pb_put_varint(&head, (uint64_t)(int64_t)gen->SupportedEditionsMaximum);
	}
	if (gen->has_err) {
		pb_put_bytes(out, CGRESP_ERROR, gen->err);
		if (PG_V1_36(gen->opts.Version)) /* v1.31.0: without supported_features */
			buf_add(out, head.p, head.n);
		return;
	}
	for (int i = 0; i < gen->genFiles.n; i++) {
		PgGeneratedFile *g = gen->genFiles.v[i];
		if (g->skip)
			continue;
		Str content, err;
		if (!pg_content(g, &content, &err)) {
			error_only(out, err);
			return;
		}
		const char *filename = g->filename;
		if (gen->module.n) {
			Buf trim = {0};
			buf_add_str(&trim, gen->module);
			buf_addc(&trim, '/');
			if (!str_has_prefix(str_c(filename), buf_view(&trim))) {
				error_only(out, str_c(xcat(filename, ": generated file does not match prefix ", quote_go(gen->module))));
				return;
			}
			filename += trim.n;
		}
		put_file(&files, filename, content);
		if (gen->annotateCode && has_suffix(g->filename, ".go")) {
			Str meta;
			if (!meta_file(g, content, &meta, &err)) {
				error_only(out, err);
				return;
			}
			put_file(&files, xcat(filename, ".meta"), meta);
		}
	}
	buf_add(out, head.p, head.n);
	buf_add(out, files.p, files.n);
}
