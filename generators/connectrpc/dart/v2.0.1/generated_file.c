/* Copyright 2024-2025 The Connect Authors
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

/* Port of packages/connect/bin/src/symbol.dart and generated_file.dart. */

/* ---- symbol.dart ---- */

/* DartLibrary: a library that can be imported, `import "<path>" as <ident>;`. */
typedef struct {
	const char *path;
	const char *ident;
} DartLibrary;

static DartLibrary *dart_library(const char *path, const char *ident)
{
	DartLibrary *l = xalloc(sizeof *l);
	l->path = path;
	l->ident = ident;
	return l;
}

/* ---- generated_file.dart ---- */

/* A printable of GeneratedFile.p: text, or a DartIdentifier (library != NULL) that prints as
 * `<library.ident>.<text>`. */
typedef struct {
	DartLibrary *library;
	Str text;
} Printable;

typedef struct {
	int n, cap;
	Printable *v;
} Line;

typedef struct Schema Schema;
typedef struct ProtoInfo ProtoInfo;

typedef struct {
	/* Path to the generated file, relative to the output directory. */
	const char *path;
	/* The proto file against which this is generated, and what plugin.dart derives from it. */
	ProtoInfo *proto;
	Schema *schema;
	Map libs_by_path; /* DartLibrary* */
	Vec libs;         /* DartLibrary*, in insertion order */
	Vec lines;        /* Line* */
} GeneratedFile;

static GeneratedFile *generated_file(const char *path, Schema *schema, ProtoInfo *proto)
{
	GeneratedFile *f = xalloc(sizeof *f);
	f->path = path;
	f->schema = schema;
	f->proto = proto;
	return f;
}

static void ln_push(Line *l, Printable p)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, sizeof *l->v * (size_t)l->cap, sizeof *l->v * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = p;
}

/* The DartLibrary of package:connectrpc/connect.dart, `connect`. */
static DartLibrary *connect_lib(void) { return dart_library("package:connectrpc/connect.dart", "connect"); }

/* The connect identifier that follows in a gf_pf format, e.g. CONNECT "Headers". */
#define CONNECT "\x01"

/* GeneratedFile.p of the printables of fmt: text, CONNECT <name> (connect.import(name)), %s (a
 * C string), %S (a Str) and %I (a Printable: an identifier). */
static void gf_p(GeneratedFile *f, Line *l);
COLD static void gf_pf(GeneratedFile *f, const char *fmt, ...)
{
	Line *l = xalloc(sizeof *l);
	va_list ap;
	va_start(ap, fmt);
	while (*fmt) {
		const char *t = fmt;
		while (*fmt && *fmt != '%' && *fmt != *CONNECT)
			fmt++;
		if (fmt > t)
			ln_push(l, (Printable){NULL, {t, (size_t)(fmt - t)}});
		if (*fmt == *CONNECT) {
			t = ++fmt;
			while ((*fmt | 0x20) >= 'a' && (*fmt | 0x20) <= 'z')
				fmt++;
			ln_push(l, (Printable){connect_lib(), {t, (size_t)(fmt - t)}});
		} else if (*fmt == '%') {
			char c = fmt[1];
			fmt += 2;
			if (c == 's')
				ln_push(l, (Printable){NULL, str_c(va_arg(ap, const char *))});
			else if (c == 'S')
				ln_push(l, (Printable){NULL, va_arg(ap, Str)});
			else
				ln_push(l, va_arg(ap, Printable));
		}
	}
	va_end(ap);
	gf_p(f, l);
}

/* GeneratedFile.p: identifiers of a library path already imported use its first DartLibrary. */
static void gf_p(GeneratedFile *f, Line *l)
{
	for (int i = 0; i < l->n; i++) {
		DartLibrary *lib = l->v[i].library;
		if (!lib)
			continue;
		DartLibrary *first = map_get(&f->libs_by_path, lib->path);
		if (first) {
			l->v[i].library = first;
		} else {
			map_put(&f->libs_by_path, lib->path, lib);
			vec_push(&f->libs, lib);
		}
	}
	vec_push(&f->lines, l);
}

static const char *proto_name(const ProtoInfo *p);

/* GeneratedFile.content: an import identifier taken by an earlier import gets '$' appended until
 * it is free. Appended '$' are counted per identifier, so each candidate is tried once. */
static Str gf_content(GeneratedFile *f)
{
	Buf b = {0};
	buf_adds(&b, "//\n//  Generated code. Do not modify.\n//  source: ");
	buf_adds(&b, proto_name(f->proto));
	buf_adds(&b, "\n//\n\n");
	Map taken = {0}, dollars = {0};
	for (int i = 0; i < f->libs.n; i++) {
		DartLibrary *lib = f->libs.v[i];
		const char *base = lib->ident;
		intptr_t k = (intptr_t)map_get(&dollars, base);
		for (;; k++) {
			Buf c = {0};
			buf_adds(&c, base);
			for (intptr_t j = 0; j < k; j++)
				buf_addc(&c, '$');
			if (!map_has(&taken, buf_str(&c))) {
				lib->ident = buf_str(&c);
				break;
			}
		}
		map_put(&dollars, base, (void *)(k + 1));
		map_put(&taken, lib->ident, (void *)1);
		buf_fmt(&b, "import \"%s\" as %s;\n", lib->path, lib->ident);
	}
	buf_addc(&b, '\n');
	for (int i = 0; i < f->lines.n; i++) {
		Line *l = f->lines.v[i];
		for (int j = 0; j < l->n; j++) {
			if (l->v[j].library) {
				buf_adds(&b, l->v[j].library->ident);
				buf_addc(&b, '.');
			}
			buf_add_str(&b, l->v[j].text);
		}
		buf_addc(&b, '\n');
	}
	return buf_view(&b);
}
