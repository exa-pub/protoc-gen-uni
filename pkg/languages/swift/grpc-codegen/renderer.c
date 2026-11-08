/* Copyright (c) 2023 Apple Inc. and the SwiftOpenAPIGenerator project authors
 * Licensed under Apache License v2.0
 *
 * See LICENSE.txt for license information
 * See CONTRIBUTORS.txt for the list of SwiftOpenAPIGenerator project authors
 *
 * SPDX-License-Identifier: Apache-2.0 */

/* Port of Internal/Renderer/TextBasedRenderer.swift (grpc-swift-2 2.4.4): StringCodeWriter and
 * the rendering of the nodes in structured_swift.h. The writer keeps its lines joined with "\n"
 * in one buffer: only the last line is ever extended (nextLineAppendsToLastLine). */
#include "structured_swift.h"
#include "swift_std.h"

#include <string.h>

typedef struct {
	Buf out;
	int n_lines, level, indentation;
	bool append; /* nextWriteAppendsToLastLine */
} Writer;

static void write_str(Writer *w, Str line)
{
	if (w->append && w->n_lines) {
		buf_add_str(&w->out, line);
	} else {
		if (w->n_lines)
			buf_addc(&w->out, '\n');
		/* Skip indentation to avoid trailing whitespace on blank lines. */
		if (line.n)
			for (int i = 0; i < w->indentation * w->level; i++)
				buf_addc(&w->out, ' ');
		buf_add_str(&w->out, line);
		w->n_lines++;
	}
	w->append = false;
}

static void wl(Writer *w, const char *line) { write_str(w, str_c(line)); }

/* nextLineAppendsToLastLine() followed by writeLine(line) */
static void app(Writer *w, const char *line)
{
	w->append = true;
	wl(w, line);
}

static const char *const access_names[] = {"", "public", "package", "internal", "fileprivate", "private"};

static void render_expr(Writer *w, const GcExpr *e);
static void render_decl(Writer *w, const GcDecl *d);
static void render_type(Writer *w, const GcType *t);
static void render_blocks(Writer *w, const Vec *blocks);

static void render_comment(Writer *w, const GcComment *c)
{
	static const char *const prefixes[] = {"//", "///", "// MARK:", "// MARK: -", ""};
	const char *prefix = prefixes[c->kind];
	Str *lines;
	int n = swift_lines(c->text, &lines);
	for (int i = 0; i < n; i++) {
		/* The last line of a comment that is blank should be dropped. */
		if (!lines[i].n && !*prefix && i == n - 1)
			continue;
		Buf b = {0};
		buf_adds(&b, prefix);
		if (*prefix && lines[i].n)
			buf_addc(&b, ' ');
		buf_add_str(&b, lines[i]);
		write_str(w, buf_view(&b));
	}
}

static void render_import(Writer *w, const GcImport *im)
{
	/* preconcurrency .never, no spi, no item */
	if (im->access)
		wl(w, xcat(access_names[im->access], " import ", im->module_name));
	else
		wl(w, xcat("import ", im->module_name));
}

static void render_call(Writer *w, const GcExpr *e)
{
	render_expr(w, e->left);
	app(w, "(");
	if (e->items.n > 1) {
		w->level++;
		for (int i = 0; i < e->items.n; i++) {
			const GcArg *a = e->items.v[i];
			if (a->label) {
				wl(w, xcat(a->label, ": "));
				w->append = true;
			}
			render_expr(w, a->expr);
			if (i < e->items.n - 1)
				app(w, ",");
		}
		w->level--;
	} else {
		w->append = true;
		if (e->items.n) {
			const GcArg *a = e->items.v[0];
			if (a->label) {
				wl(w, xcat(a->label, ": "));
				w->append = true;
			}
			render_expr(w, a->expr);
		}
		w->append = true;
	}
	wl(w, ")");
}

static void render_expr(Writer *w, const GcExpr *e)
{
	switch (e->kind) {
	case GE_STRING:
		/* Use a raw literal if the string contains a quote/backslash. */
		if (strpbrk(e->s, "\"\\"))
			wl(w, xcat("#\"", e->s, "\"#"));
		else
			wl(w, xcat("\"", e->s, "\""));
		break;
	case GE_ARRAY:
		wl(w, "[");
		if (e->items.n) {
			w->level++;
			for (int i = 0; i < e->items.n; i++) {
				render_expr(w, e->items.v[i]);
				if (i < e->items.n - 1)
					app(w, ",");
			}
			w->level--;
		} else {
			w->append = true;
		}
		wl(w, "]");
		break;
	case GE_DICTIONARY: /* always empty */
		wl(w, "[");
		app(w, ":");
		app(w, "]");
		break;
	case GE_PATTERN:
		wl(w, e->s);
		break;
	case GE_TYPE:
		render_type(w, e->type);
		break;
	case GE_MEMBER:
		if (e->left) {
			render_expr(w, e->left);
			w->append = true;
		}
		wl(w, xcat(".", e->s));
		break;
	case GE_CALL:
		render_call(w, e);
		break;
	case GE_ASSIGN:
		render_expr(w, e->left);
		app(w, " = ");
		w->append = true;
		render_expr(w, e->right);
		break;
	case GE_KEYWORD: {
		static const char *const keywords[] = {"return", "try", "await"};
		wl(w, keywords[e->keyword]);
		if (!e->right)
			break;
		app(w, " ");
		w->append = true;
		render_expr(w, e->right);
		break;
	}
	case GE_CLOSURE:
		wl(w, "{");
		if (e->items.n)
			app(w, xcat(" ", str_join(&e->items, ", "), " in"));
		if (e->body) {
			w->level++;
			render_blocks(w, e->body);
			w->level--;
		}
		wl(w, "}");
		break;
	}
}

static const char *where_clause(const GcWhere *wc) { return xcat("where ", wc->left, ": ", wc->right); }

static void render_keywords(Writer *w, GcKeywords k)
{
	if (k == GC_KW_ASYNC_THROWS) {
		app(w, " async");
		app(w, " throws");
	}
}

/* renderParameter and renderClosureParameter */
static void render_param(Writer *w, const GcParam *p, bool closure)
{
	const char *label = p->label ? p->label : "_";
	bool rename = p->name && (!p->label || strcmp(p->name, p->label) != 0);
	if (!closure) {
		wl(w, label);
		w->append = true;
		if (rename) {
			/* If the label and name are the same value, don't repeat it. */
			wl(w, " ");
			app(w, p->name);
			w->append = true;
		}
		wl(w, ": ");
		w->append = true;
	} else if (p->name) {
		wl(w, label);
		if (rename) {
			wl(w, " ");
			app(w, p->name);
			w->append = true;
		}
	}
	if (p->is_inout) {
		wl(w, "inout ");
		w->append = true;
	}
	if (p->type)
		render_type(w, p->type);
	if (p->default_value) {
		app(w, " = ");
		w->append = true;
		render_expr(w, p->default_value);
	}
}

/* The parameter list of a function or closure signature, from "(" to ")". */
static void render_params(Writer *w, const Vec *params, bool closure)
{
	if (params->n > 1) {
		w->level++;
		for (int i = 0; i < params->n; i++) {
			render_param(w, params->v[i], closure);
			if (i < params->n - 1)
				app(w, ",");
		}
		w->level--;
	} else {
		w->append = true;
		if (params->n) {
			render_param(w, params->v[0], closure);
			w->append = true;
		}
	}
	wl(w, ")");
}

static void render_generics(Writer *w, const Vec *generics)
{
	app(w, "<");
	for (int i = 0; i < generics->n; i++) {
		w->append = true;
		render_type(w, generics->v[i]);
		if (i < generics->n - 1)
			app(w, ", ");
	}
	app(w, ">");
}

static void render_type(Writer *w, const GcType *t)
{
	switch (t->kind) {
	case GT_ANY:
		wl(w, "any ");
		w->append = true;
		render_type(w, t->inner);
		break;
	case GT_GENERIC:
		render_type(w, t->inner);
		app(w, "<");
		w->append = true;
		for (int i = 0; i < t->wrapped.n; i++) {
			render_type(w, t->wrapped.v[i]);
			w->append = true;
			if (i < t->wrapped.n - 1) {
				wl(w, ", ");
				w->append = true;
			}
		}
		app(w, ">");
		break;
	case GT_MEMBER:
		wl(w, t->member);
		break;
	case GT_ARRAY:
		wl(w, "[");
		w->append = true;
		render_type(w, t->inner);
		app(w, "]");
		break;
	case GT_SOME:
		wl(w, "some ");
		w->append = true;
		render_type(w, t->inner);
		break;
	case GT_CLOSURE: {
		const GcClosureSig *c = t->closure;
		if (c->sendable) {
			wl(w, "@Sendable ");
			w->append = true;
		}
		if (c->escaping) {
			wl(w, "@escaping ");
			w->append = true;
		}
		wl(w, "(");
		render_params(w, &c->parameters, true);
		render_keywords(w, c->keywords);
		if (c->return_type) {
			app(w, " -> ");
			w->append = true;
			render_expr(w, c->return_type);
		}
		break;
	}
	}
}

/* "<access> " followed by an appended line */
static void render_access(Writer *w, GcAccess access)
{
	if (access) {
		wl(w, xcat(access_names[access], " "));
		w->append = true;
	}
}

static void render_conformances(Writer *w, const Vec *conformances)
{
	if (conformances->n) {
		wl(w, xcat(": ", str_join(conformances, ", ")));
		w->append = true;
	}
}

/* " {", the members (with a blank line between them if separated) and "}" */
static void render_members(Writer *w, const Vec *members, bool separated)
{
	wl(w, " {");
	if (members->n) {
		w->level++;
		for (int i = 0; i < members->n; i++) {
			render_decl(w, members->v[i]);
			if (separated && i < members->n - 1)
				wl(w, "");
		}
		w->level--;
	} else {
		w->append = true;
	}
	wl(w, "}");
}

static void render_function(Writer *w, const GcDecl *d)
{
	const GcSignature *s = &d->sig;
	render_access(w, s->access);
	wl(w, s->name ? xcat("func ", s->name) : "init");
	if (s->generics.n)
		render_generics(w, &s->generics);
	app(w, "(");
	render_params(w, &s->parameters, false);
	render_keywords(w, s->keywords);
	if (s->return_type) {
		app(w, " -> ");
		w->append = true;
		render_expr(w, s->return_type);
	}
	if (s->where)
		app(w, xcat(" ", where_clause(s->where)));
	if (!d->body)
		return;
	app(w, " {");
	if (d->body->n) {
		w->level++;
		render_blocks(w, d->body);
		w->level--;
	} else {
		w->append = true;
	}
	wl(w, "}");
}

static void render_decl(Writer *w, const GcDecl *d)
{
	switch (d->kind) {
	case GD_COMMENTABLE:
		if (d->comment)
			render_comment(w, d->comment);
		render_decl(w, d->inner);
		break;
	case GD_GUARDED: {
		Buf b = {0};
		buf_adds(&b, "@available(");
		for (int i = 0; i < d->availability->n; i++)
			buf_fmt(&b, "%s %s, ", d->availability->versions[i].os, d->availability->versions[i].version);
		buf_adds(&b, "*)");
		write_str(w, buf_view(&b));
		render_decl(w, d->inner);
		break;
	}
	case GD_VARIABLE:
		render_access(w, d->access);
		if (d->is_static) {
			wl(w, "static ");
			w->append = true;
		}
		wl(w, d->is_let ? "let " : "var ");
		w->append = true;
		render_expr(w, d->left);
		if (d->type) {
			app(w, ": ");
			w->append = true;
			render_type(w, d->type);
		}
		if (d->right) {
			app(w, " = ");
			w->append = true;
			render_expr(w, d->right);
		}
		break;
	case GD_EXTENSION:
		render_access(w, d->access);
		wl(w, xcat("extension ", d->name));
		w->append = true;
		render_conformances(w, &d->conformances);
		if (d->where) {
			wl(w, xcat(" ", where_clause(d->where)));
			w->append = true;
		}
		wl(w, " {");
		for (int i = 0; i < d->members.n; i++) {
			w->level++;
			render_decl(w, d->members.v[i]);
			if (i < d->members.n - 1)
				wl(w, "");
			w->level--;
		}
		wl(w, "}");
		break;
	case GD_STRUCT:
		render_access(w, d->access);
		wl(w, xcat("struct ", d->name));
		w->append = true;
		if (d->generics.n) {
			render_generics(w, &d->generics);
			w->append = true;
		}
		render_conformances(w, &d->conformances);
		if (d->where) {
			app(w, xcat(" ", where_clause(d->where)));
			w->append = true;
		}
		render_members(w, &d->members, true);
		break;
	case GD_PROTOCOL:
		render_access(w, d->access);
		wl(w, xcat("protocol ", d->name));
		w->append = true;
		render_conformances(w, &d->conformances);
		render_members(w, &d->members, true);
		break;
	case GD_ENUM:
		render_access(w, d->access);
		wl(w, xcat("enum ", d->name));
		w->append = true;
		render_conformances(w, &d->conformances);
		render_members(w, &d->members, false);
		break;
	case GD_TYPEALIAS:
		if (d->access)
			wl(w, xcat(access_names[d->access], " typealias ", d->name, " = "));
		else
			wl(w, xcat("typealias ", d->name, " = "));
		w->append = true;
		render_type(w, d->type);
		break;
	case GD_FUNCTION:
		render_function(w, d);
		break;
	}
}

static void render_block(Writer *w, const GcCodeBlock *b)
{
	if (b->comment)
		render_comment(w, b->comment);
	if (b->decl)
		render_decl(w, b->decl);
	else if (b->expr)
		render_expr(w, b->expr);
}

static void render_blocks(Writer *w, const Vec *blocks)
{
	for (int i = 0; i < blocks->n; i++)
		render_block(w, blocks->v[i]);
}

COLD Str gc_render(const GcFile *file, int indentation)
{
	Writer w = {0};
	w.indentation = indentation;
	if (file->top_comment) {
		render_comment(&w, file->top_comment);
		wl(&w, "");
	}
	if (file->imports) {
		for (int i = 0; i < file->n_imports; i++)
			render_import(&w, &file->imports[i]);
		wl(&w, "");
	}
	for (int i = 0; i < file->code_blocks.n; i++) {
		render_block(&w, file->code_blocks.v[i]);
		if (i < file->code_blocks.n - 1)
			wl(&w, "");
	}
	buf_str(&w.out);
	return buf_view(&w.out);
}
