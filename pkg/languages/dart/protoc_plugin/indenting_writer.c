/* Port of protoc_plugin 25.1.0 lib/indenting_writer.dart (IndentingWriter, ImportWriter) over
 * dart_style nodes: see Out in internal.h.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static void out_init(Out *o, const char *fileName, bool generateMetadata)
{
	memset(o, 0, sizeof *o);
	o->fileName = fileName;
	o->generateMetadata = generateMetadata;
	o->cur = &o->declarations;
}

/* Comments printed before the next declaration (println of a comment line). sp: the spaces that
 * begin the printed line. */
static void out_comment(Out *o, const char *text, int sp)
{
	Node holder = {0};
	holder.comments = o->pcomments;
	Token *c = ds_comment(&holder, text);
	ds_tnl(c, o->nls, sp);
	o->pcomments = holder.comments;
	o->nls = 1;
}

/* println of a declaration or member; also takes the pending comments. */
static Node *out_add(Out *o, Node *n)
{
	if (o->pcomments) {
		Token *last = o->pcomments;
		while (last->next)
			last = last->next;
		last->next = n->comments;
		n->comments = o->pcomments;
		o->pcomments = NULL;
	}
	ds_nl(n, o->nls, 0);
	ds_push(o->cur, n);
	o->nls = 1;
	return n;
}

/* println() */
static void out_blank(Out *o) { o->nls++; }

/* addBlock: the declaration was printed (its first line ends with the opening bracket); members
 * are added to *members. */
static OutBody out_begin_body(Out *o, NodeList *members)
{
	OutBody saved = {o->cur, o->nls};
	o->cur = members;
	o->nls = 1;
	return saved;
}

static void out_end_body(Out *o, OutBody saved, Token *rb)
{
	if (o->pcomments)
		fail("protoc_plugin: comments at the end of a body");
	ds_tnl(rb, o->nls, 0);
	o->cur = saved.cur;
	o->nls = 1;
}

/* _addAnnotation of a NamedLocation: the name's token. */
static Annotation *out_annotate(Out *o, Token *tok, IntList path)
{
	if (!o->generateMetadata)
		return NULL;
	Annotation *a = xalloc(sizeof *a);
	a->tok = tok;
	a->path = path;
	vec_push(&o->annotations, a);
	return a;
}

/* addSuffix of a ConditionalConstDefinition (the key is its field name). */
static void out_addSuffix(Out *o, const char *envName)
{
	for (int i = 0; i < o->nsuffixes; i++)
		if (strcmp(o->suffixes[i], envName) == 0)
			return;
	o->suffixes[o->nsuffixes++] = envName;
}

static const char *ccd_constFieldName(const char *envName);
static Node *ccd_constDefinition(const char *envName);

/* emitSource: the suffixes, then the unformatted text, formatted if `format`. */
static Str out_emitSource(Out *o, bool format)
{
	if (!o->unit) {
		if (o->nsuffixes) {
			out_blank(o);
			/* SplayTreeMap order of the field names */
			for (int i = 1; i < o->nsuffixes; i++)
				for (int j = i; j > 0 && dart_compare(ccd_constFieldName(o->suffixes[j - 1]),
								      ccd_constFieldName(o->suffixes[j])) > 0; j--) {
					const char *t = o->suffixes[j];
					o->suffixes[j] = o->suffixes[j - 1];
					o->suffixes[j - 1] = t;
				}
			for (int i = 0; i < o->nsuffixes; i++)
				out_add(o, ccd_constDefinition(o->suffixes[i]));
		}
		o->unit = ds_unit(o->directives, o->declarations);
		o->unit->comments = o->pcomments;
		o->pcomments = NULL;
		ds_tnl(o->unit->name, o->nls, 0);
		ds_linearize(o->unit, &o->text);
	}
	if (!format)
		return buf_view(&o->text);
	Buf out = {0};
	const char *err = ds_format(o->unit, buf_view(&o->text), &out);
	if (err)
		fail("%s", err);
	return buf_view(&out);
}

/* GeneratedCodeInfo.writeToJson().toString() of the annotations. */
static Str out_metadataJson(Out *o)
{
	Buf b = {0};
	buf_addc(&b, '{');
	if (o->annotations.n) {
		buf_adds(&b, "\"1\":[");
		for (int i = 0; i < o->annotations.n; i++) {
			Annotation *a = VEC_AT(o->annotations, Annotation, i);
			if (i)
				buf_addc(&b, ',');
			buf_adds(&b, "{\"1\":[");
			for (int j = 0; j < a->path.n; j++)
				buf_fmt(&b, j ? ",%d" : "%d", a->path.v[j]);
			buf_adds(&b, "],\"2\":");
			buf_adds(&b, dart_json_string(str_c(o->fileName)));
			int begin = a->tok->off16;
			buf_fmt(&b, ",\"3\":%d,\"4\":%d}", begin, begin + dart_utf16_len(a->tok->lexeme, (size_t)a->tok->len));
		}
		buf_addc(&b, ']');
	}
	buf_addc(&b, '}');
	return buf_view(&b);
}

/* ---- ImportWriter ---- */

typedef struct {
	const char *text; /* the directive, the sort key */
	const char *url, *prefix;
	NodeList show;
	bool isExport;
} Directive;

typedef struct {
	Vec groups[5]; /* dart imports, package imports, file imports, package exports, file exports */
} ImportWriter;

static void iw_insert(Vec *set, Directive *d)
{
	for (int i = 0; i < set->n; i++) {
		int c = dart_compare(VEC_AT(*set, Directive, i)->text, d->text);
		if (c == 0)
			return;
		if (c > 0) {
			vec_push(set, NULL);
			memmove(&set->v[i + 1], &set->v[i], (size_t)(set->n - 1 - i) * sizeof *set->v);
			set->v[i] = d;
			return;
		}
	}
	vec_push(set, d);
}

static void iw_addImport(ImportWriter *w, const char *url, const char *prefix)
{
	Directive *d = xalloc(sizeof *d);
	d->url = url;
	d->prefix = prefix;
	d->text = prefix ? xfmt("import '%s' as %s;", url, prefix) : xfmt("import '%s';", url);
	iw_insert(&w->groups[has_prefix(url, "dart:") ? 0 : has_prefix(url, "package:") ? 1 : 2], d);
}

static void iw_addExport(ImportWriter *w, const char *url, const char *member)
{
	Directive *d = xalloc(sizeof *d);
	d->url = url;
	d->isExport = true;
	if (member) {
		ds_push(&d->show, ds_id(member));
		d->text = xfmt("export '%s' show %s;", url, member);
	} else {
		d->text = xfmt("export '%s';", url);
	}
	iw_insert(&w->groups[has_prefix(url, "package:") ? 3 : 4], d);
}

static bool iw_hasImports(const ImportWriter *w)
{
	for (int i = 0; i < 5; i++)
		if (w->groups[i].n)
			return true;
	return false;
}

static bool iw_hasSrcImport(const ImportWriter *w)
{
	for (int i = 0; i < w->groups[1].n; i++)
		if (strstr(VEC_AT(w->groups[1], Directive, i)->url, "/src/"))
			return true;
	return false;
}

/* out.println(importWriter.emit()) */
static void iw_emit(ImportWriter *w, Out *o)
{
	NodeList *saved = o->cur;
	o->cur = &o->directives;
	bool any = false;
	for (int g = 0; g < 5; g++) {
		if (!w->groups[g].n)
			continue;
		if (any)
			out_blank(o);
		any = true;
		for (int i = 0; i < w->groups[g].n; i++) {
			Directive *d = VEC_AT(w->groups[g], Directive, i);
			const char *uri = xfmt("'%s'", d->url);
			out_add(o, d->isExport ? ds_export(uri, d->show) : ds_import(uri, d->prefix));
		}
	}
	o->cur = saved;
	out_blank(o);
}
