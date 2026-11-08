/*
 * Copyright (C) 2015 Square, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A port of kotlinpoet/src/jvmMain/kotlin/com/squareup/kotlinpoet/CodeWriter.kt and
 * kotlinpoet/src/commonMain/kotlin/com/squareup/kotlinpoet/LineWrapper.kt of KotlinPoet 2.4.0 and
 * 2.2.0: emitting code blocks (statements, indentation, KDoc and comments), name lookup of types
 * and members with the imports of a first pass and import aliases, static imports of members after
 * a %T (the "deferred type"), and soft line wrapping at ♢ (100 columns).
 *
 * Not ported: extension members and operators, context receivers, type variable declarations,
 * enums and anonymous classes. */
#include "internal.h"

#include <limits.h>
#include <string.h>

#define KP_ESCAPED_STAR "&#42;"
#define KP_DEFAULT_INDENT "  "

typedef struct {
	const char *qualified, *alias; /* Import */
} KpImport;

struct KpWriter {
	/* LineWrapper */
	Buf *out; /* NULL: NullAppendable */
	int column_limit;
	Buf *segs;
	int nseg, capseg;
	int lw_indent_level;
	const char *line_prefix;
	bool closed;
	/* CodeWriter */
	int indent_level;
	bool kdoc, comment, trailing_newline;
	const char *package; /* NULL: NO_PACKAGE */
	Vec type_stack;      /* KpTypeSpec* */
	Map member_import_names;
	Map importable_idx; /* simple name -> 1 + index in importable_keys */
	Vec importable_keys, importable_vals; /* const char*, Vec* of KpType* (distinct, first-seen order) */
	Map importable_seen;                  /* simple name + names of a class already in importable_vals */
	Map member_idx;                       /* importableMembers: simple name -> 1 + index in member_keys */
	Vec member_keys, member_vals;         /* const char*, Vec* of KpMember* (distinct, first-seen order) */
	Map member_seen;                      /* simple name + identity of a member already in member_vals */
	Map imported_members;                 /* simple name or alias -> Vec* of KpMember* */
	Map referenced_names;
	Map imports; /* canonical -> KpImport* */
	Vec import_order;
	Map imported_types; /* simple name -> KpType* */
	int statement_line;
	int code_depth; /* nesting of kp_emit_code */
};

/* ---- LineWrapper ---- */

static Buf *lw_last(KpWriter *w) { return &w->segs[w->nseg - 1]; }

static void lw_new_segment(KpWriter *w)
{
	if (w->nseg == w->capseg) {
		int nc = w->capseg ? 2 * w->capseg : 8;
		w->segs = xrealloc(w->segs, sizeof(Buf) * (size_t)w->capseg, sizeof(Buf) * (size_t)nc);
		w->capseg = nc;
	}
	w->segs[w->nseg++] = (Buf){0};
}

static void lw_out(KpWriter *w, const char *s, size_t n)
{
	if (w->out)
		buf_add(w->out, s, n);
}

static size_t lw_seg_len(const Buf *b) { return kp_utf16_len(b->p ? b->p : "", b->n); }

static void lw_emit_range(KpWriter *w, int start, int end)
{
	if (start > 0) {
		lw_out(w, "\n", 1);
		for (int i = 0; i < w->lw_indent_level; i++)
			lw_out(w, KP_DEFAULT_INDENT, 2);
		lw_out(w, w->line_prefix, strlen(w->line_prefix));
	}
	lw_out(w, w->segs[start].p, w->segs[start].n);
	for (int i = start + 1; i < end; i++) {
		lw_out(w, " ", 1);
		lw_out(w, w->segs[i].p, w->segs[i].n);
	}
}

static void lw_emit_current_line(KpWriter *w)
{
	int start = 0;
	long long column = (long long)lw_seg_len(&w->segs[0]);
	for (int i = 1; i < w->nseg; i++) {
		long long seg = (long long)lw_seg_len(&w->segs[i]);
		long long next = column + 1 + seg;
		if (next > w->column_limit) {
			lw_emit_range(w, start, i);
			start = i;
			column = seg + 2LL * w->lw_indent_level;
			continue;
		}
		column = next;
	}
	lw_emit_range(w, start, w->nseg);
	w->nseg = 0;
	lw_new_segment(w);
}

static void lw_check_open(KpWriter *w)
{
	if (w->closed)
		kp_illegal_state("closed");
}

static void lw_newline(KpWriter *w);

/* LineWrapper.append(s, indentLevel, linePrefix) */
static void lw_append(KpWriter *w, const char *s, int indent_level, const char *line_prefix)
{
	lw_check_open(w);
	while (*s) {
		if (!strncmp(s, KP_WRAP, 3)) {
			w->lw_indent_level = indent_level;
			w->line_prefix = line_prefix;
			lw_new_segment(w);
			s += 3;
		} else if (*s == '\n') {
			lw_newline(w);
			s++;
		} else if (*s == ' ' || !strncmp(s, KP_NBSP, 2)) {
			buf_addc(lw_last(w), ' ');
			s += *s == ' ' ? 1 : 2;
		} else {
			const char *e = s;
			while (*e && *e != ' ' && *e != '\n' && strncmp(e, KP_NBSP, 2) && strncmp(e, KP_WRAP, 3))
				e += kp_utf8_width(e);
			buf_add(lw_last(w), s, (size_t)(e - s));
			s = e;
		}
	}
}

static void lw_append_non_wrapping(KpWriter *w, const char *s)
{
	lw_check_open(w);
	buf_adds(lw_last(w), s);
}

static void lw_newline(KpWriter *w)
{
	lw_check_open(w);
	lw_emit_current_line(w);
	lw_out(w, "\n", 1);
	w->lw_indent_level = -1;
}

static bool lw_has_pending(KpWriter *w) { return w->nseg != 1 || w->segs[0].n != 0; }

/* ---- CodeWriter ---- */

static void kp_writer_set_imports(KpWriter *w, Vec *imports)
{
	for (int i = 0; i < imports->n; i++) {
		KpImport *im = imports->v[i];
		map_put(&w->imports, im->qualified, im);
		vec_push(&w->import_order, im);
		const char *dot = strrchr(im->qualified, '.');
		if (dot)
			map_put(&w->member_import_names, xstrndup(im->qualified, (size_t)(dot - im->qualified)), (void *)1);
	}
}

KpWriter *kp_writer_new(Buf *out, int column_limit)
{
	KpWriter *w = xalloc(sizeof *w);
	w->out = out;
	w->column_limit = column_limit;
	w->lw_indent_level = -1;
	w->line_prefix = "";
	lw_new_segment(w);
	w->statement_line = -1;
	return w;
}

void kp_writer_close(KpWriter *w)
{
	lw_emit_current_line(w);
	w->closed = true;
}

void kp_writer_indent(KpWriter *w, int levels) { w->indent_level += levels; }

void kp_writer_unindent(KpWriter *w, int levels)
{
	if (w->indent_level - levels < 0)
		kp_illegal_argument(xfmt("cannot unindent %d from %d", levels, w->indent_level));
	w->indent_level -= levels;
}

void kp_writer_push_package(KpWriter *w, const char *package)
{
	if (w->package)
		kp_illegal_state(xfmt("package already set: %s", w->package));
	w->package = package;
}

void kp_writer_pop_package(KpWriter *w) { w->package = NULL; }
void kp_writer_push_type(KpWriter *w, KpTypeSpec *t) { vec_push(&w->type_stack, t); }
void kp_writer_pop_type(KpWriter *w) { w->type_stack.n--; }
int kp_writer_statement_line(KpWriter *w) { return w->statement_line; }
void kp_writer_set_statement_line(KpWriter *w, int line) { w->statement_line = line; }

static void kp_emit_indentation(KpWriter *w)
{
	for (int j = 0; j < w->indent_level; j++)
		lw_append(w, KP_DEFAULT_INDENT, -1, "");
}

/* emit(s, nonWrapping) */
static void kp_emit_impl(KpWriter *w, const char *s, bool non_wrapping)
{
	if (w->kdoc && !KP_V22) {
		s = str_replace_all(s, "/*", "/" KP_ESCAPED_STAR);
		s = str_replace_all(s, "*/", KP_ESCAPED_STAR "/");
	}
	bool first = true;
	for (const char *line = s;;) {
		const char *nl = strchr(line, '\n');
		size_t n = nl ? (size_t)(nl - line) : strlen(line);
		if (!first) {
			if ((w->kdoc || w->comment) && w->trailing_newline) {
				kp_emit_indentation(w);
				lw_append(w, w->kdoc ? " *" : "//", -1, "");
			}
			lw_newline(w);
			w->trailing_newline = true;
			if (w->statement_line != -1) {
				if (w->statement_line == 0)
					kp_writer_indent(w, 2);
				w->statement_line++;
			}
		}
		first = false;
		if (n) {
			if (w->trailing_newline) {
				kp_emit_indentation(w);
				if (w->kdoc)
					lw_append(w, " * ", -1, "");
				else if (w->comment)
					lw_append(w, "// ", -1, "");
			}
			const char *text = xstrndup(line, n);
			if (non_wrapping)
				lw_append_non_wrapping(w, text);
			else if (KP_V22)
				lw_append(w, text, w->kdoc ? w->indent_level : w->indent_level + 2, w->kdoc ? " * " : "");
			else
				lw_append(w, text, w->kdoc || w->comment ? w->indent_level : w->indent_level + 2,
					  w->kdoc ? " * " : w->comment ? "// " : "");
			w->trailing_newline = false;
		}
		if (!nl)
			break;
		line = nl + 1;
	}
}

void kp_emit(KpWriter *w, const char *s) { kp_emit_impl(w, s, false); }

void kp_emit_comment(KpWriter *w, const KpCode *c)
{
	w->trailing_newline = true;
	w->comment = true;
	kp_emit_code(w, c, false, false);
	kp_emit(w, "\n");
	w->comment = false;
}

void kp_emit_kdoc(KpWriter *w, const KpCode *c)
{
	if (kp_code_is_empty(c))
		return;
	kp_emit(w, "/**\n");
	w->kdoc = true;
	kp_emit_code(w, c, false, true);
	w->kdoc = false;
	kp_emit(w, " */\n");
}

void kp_emit_annotations(KpWriter *w, const Vec *annotations, bool inline_)
{
	for (int i = 0; i < annotations->n; i++) {
		kp_annotation_emit(annotations->v[i], w, inline_);
		kp_emit(w, inline_ ? " " : "\n");
	}
}

static const char *const kp_modifier_keywords[] = {
	"public", "protected", "private", "internal", "expect", "actual", "final", "open", "abstract",
	"sealed", "const", "external", "override", "lateinit", "tailrec", "vararg", "suspend", "inner",
	"enum", "annotation", "value", "fun", "companion", "inline", "noinline", "crossinline", "reified",
	"infix", "operator", "data", "in", "out",
};

void kp_emit_modifiers(KpWriter *w, KpModifiers modifiers, KpModifiers implicit)
{
	bool emit_public;
	if (modifiers & KM(KM_PUBLIC))
		emit_public = true;
	else if ((implicit & KM(KM_PUBLIC)) && (modifiers & KM(KM_OVERRIDE)))
		emit_public = false;
	else if (!(implicit & KM(KM_PUBLIC)))
		emit_public = false;
	else
		emit_public = !(modifiers & (KM(KM_PRIVATE) | KM(KM_INTERNAL) | KM(KM_PROTECTED)));
	if (emit_public) {
		kp_emit(w, "public");
		kp_emit(w, " ");
	}
	for (int m = KM_PROTECTED; m <= KM_OUT; m++)
		if ((modifiers & KM(m)) && !(implicit & KM(m))) {
			kp_emit(w, kp_modifier_keywords[m]);
			kp_emit(w, " ");
		}
}

static const char *kp_parts_list(const KpCode *c)
{
	Vec v = {0};
	for (int i = 0; i < c->parts.n; i++)
		vec_push(&v, (void *)kp_escape_character_literals(c->parts.v[i]));
	return xcat("[", str_join(&v, ", "), "]");
}

static const char *kp_args_list(const KpCode *c)
{
	Vec v = {0};
	for (int i = 0; i < c->args.n; i++)
		vec_push(&v, (void *)kp_arg_to_string(c->args.v[i]));
	return xcat("[", str_join(&v, ", "), "]");
}

static bool kp_emit_static_import_member(KpWriter *w, const char *canonical, const char *part);

void kp_emit_code(KpWriter *w, const KpCode *c, bool is_constant_context, bool ensure_trailing_newline)
{
	int a = 0;
	const KpType *deferred = NULL; /* used by the "import static" logic */
	if (++w->code_depth > KP_MAX_CODE_DEPTH)
		java_exception("java.lang.StackOverflowError", NULL);
	for (int i = 0; i < c->parts.n; i++) {
		const char *part = c->parts.v[i];
		if (part[0] == '%' && part[1] && !part[2]) {
			KpArg *arg = part[1] == '%' ? NULL : c->args.v[a++];
			switch (part[1]) {
			case 'L':
				if (arg->code)
					kp_emit_code(w, arg->code, is_constant_context, false);
				else
					kp_emit(w, arg->s ? arg->s : "null");
				break;
			case 'N':
				kp_emit(w, arg->s);
				break;
			case 'S':
				kp_emit_impl(w, arg->s ? kp_string_literal_with_quotes(arg->s, is_constant_context) : "null", true);
				break;
			case 'T': {
				/* defer the type if the next part is literal text after a class whose members
				 * are imported */
				bool defer = false;
				if (arg->type->kind == KT_CLASS && i + 1 < c->parts.n && ((const char *)c->parts.v[i + 1])[0] != '%' &&
				    map_has(&w->member_import_names, kp_canonical_name(arg->type))) {
					if (deferred)
						kp_illegal_state("pending type for static import?!");
					deferred = arg->type;
					defer = true;
				}
				if (!defer)
					kp_type_emit(arg->type, w);
				if (!defer || KP_V22)
					kp_type_emit_nullable(arg->type, w);
				break;
			}
			case 'M':
				kp_member_emit(arg->member, w);
				break;
			case '%':
				kp_emit(w, "%");
				break;
			}
		} else if (!strcmp(part, KP_INDENT)) {
			kp_writer_indent(w, 1);
		} else if (!strcmp(part, KP_UNINDENT)) {
			kp_writer_unindent(w, 1);
		} else if (!strcmp(part, KP_OPEN)) {
			if (w->statement_line != -1)
				kp_illegal_state(xcat("Can't open a new statement until the current statement is closed (opening " KP_OPEN
						      " followed\nby another " KP_OPEN " without a closing " KP_CLOSE ").\nCurrent code block:\n- Format parts: ",
						      kp_parts_list(c), "\n- Arguments: ", kp_args_list(c), "\n"));
			w->statement_line = 0;
		} else if (!strcmp(part, KP_CLOSE)) {
			if (w->statement_line == -1)
				kp_illegal_state(xcat("Can't close a statement that hasn't been opened (closing " KP_CLOSE
						      " is not preceded by an\nopening " KP_OPEN ").\nCurrent code block:\n- Format parts: ",
						      kp_parts_list(c), "\n- Arguments: ", kp_args_list(c), "\n"));
			if (w->statement_line > 0)
				kp_writer_unindent(w, 2);
			w->statement_line = -1;
		} else {
			bool done = false;
			if (deferred) {
				if (part[0] == '.' && kp_emit_static_import_member(w, kp_canonical_name(deferred), part))
					done = true;
				else {
					kp_type_emit((KpType *)deferred, w);
					if (!KP_V22)
						kp_type_emit_nullable(deferred, w);
				}
				deferred = NULL;
			}
			if (!done)
				kp_emit(w, part);
		}
	}
	if (ensure_trailing_newline && lw_has_pending(w))
		kp_emit(w, "\n");
	w->code_depth--;
}

void kp_emit_codef(KpWriter *w, const char *format, int nargs, const KpArg *args)
{
	kp_emit_code(w, kp_code_of(format, nargs, args), false, false);
}

/* ---- names and imports ---- */

static const char *kp_alias_of(KpWriter *w, const char *canonical)
{
	KpImport *im = map_get(&w->imports, canonical);
	return im ? im->alias : NULL;
}

/* resolve(simpleName): a type nested in a type of the stack, the top-level type, an import. */
static KpType *kp_resolve(KpWriter *w, const char *simple_name)
{
	for (int i = w->type_stack.n - 1; i >= 0; i--) {
		KpTypeSpec *t = w->type_stack.v[i];
		for (int j = 0; j < t->types.n; j++) {
			const char *nested = ((KpTypeSpec *)t->types.v[j])->name;
			if (nested && str_eq(nested, simple_name)) {
				/* stackClassName(i, simpleName) */
				KpType *c = kp_class(w->package ? w->package : "", ((KpTypeSpec *)w->type_stack.v[0])->name);
				for (int k = 1; k <= i; k++)
					c = kp_nested_class(c, ((KpTypeSpec *)w->type_stack.v[k])->name);
				return kp_nested_class(c, simple_name);
			}
		}
	}
	if (w->type_stack.n) {
		KpTypeSpec *t = w->type_stack.v[0];
		if (t->name && str_eq(t->name, simple_name))
			return kp_class(w->package ? w->package : "", simple_name);
	}
	return map_get(&w->imported_types, simple_name);
}

static void kp_importable_type(KpWriter *w, const KpType *c)
{
	KpType *top = kp_top_level_class_name(c);
	const char *alias = kp_alias_of(w, kp_canonical_name(c));
	const char *simple = alias ? alias : top->names[1];
	/* name clashes with members */
	if (map_has(&w->member_idx, simple))
		return;
	intptr_t at = (intptr_t)map_get(&w->importable_idx, simple);
	if (!at) {
		Vec *v = xalloc(sizeof *v);
		vec_push(&w->importable_keys, (void *)simple);
		vec_push(&w->importable_vals, v);
		at = w->importable_keys.n;
		map_put(&w->importable_idx, simple, (void *)at);
	}
	KpType *t = top;
	if (alias) {
		t = xalloc(sizeof *t);
		*t = *c;
		t->nullable = false;
		t->cached = NULL;
	}
	/* importableTypes keeps every reference; suggestedTypeImports makes a set of them (ClassName
	 * equality: the names), kept here from the first reference */
	Buf key = {0};
	buf_adds(&key, simple);
	for (int i = 0; i < t->n; i++) {
		buf_addc(&key, '\x01');
		buf_adds(&key, t->names[i]);
	}
	if (map_has(&w->importable_seen, buf_str(&key)))
		return;
	map_put(&w->importable_seen, xstrdup(buf_str(&key)), (void *)1);
	vec_push(w->importable_vals.v[at - 1], t);
}

static const char *kp_join_names(const char *const *names, int from, int to)
{
	Buf b = {0};
	for (int i = from; i < to; i++) {
		if (i > from)
			buf_addc(&b, '.');
		buf_adds(&b, names[i]);
	}
	return xstrdup(buf_str(&b));
}

const char *kp_lookup_name(KpWriter *w, const KpType *class_name)
{
	bool name_resolved = false;
	for (const KpType *c = class_name; c; c = kp_enclosing_class_name(c)) {
		const char *alias = kp_alias_of(w, kp_canonical_name(c));
		const char *simple = alias ? alias : c->names[c->n - 1];
		KpType *resolved = kp_resolve(w, simple);
		name_resolved = resolved != NULL;
		KpType plain = *c;
		plain.nullable = false;
		if (resolved && kp_type_equals(resolved, &plain)) {
			if (!alias)
				map_put(&w->referenced_names, class_name->names[1], (void *)1);
			return xcat(simple, ".", kp_join_names(class_name->names, c->n, class_name->n));
		}
	}
	if (name_resolved)
		return kp_canonical_name(class_name);
	if (str_eq(w->package ? w->package : "", class_name->names[0]) && !kp_alias_of(w, kp_canonical_name(class_name))) {
		map_put(&w->referenced_names, class_name->names[1], (void *)1);
		return kp_join_names(class_name->names, 1, class_name->n);
	}
	if (!w->kdoc)
		kp_importable_type(w, class_name);
	return kp_canonical_name(class_name);
}

/* isMethodNameUsedInCurrentContext(simpleName) */
static bool kp_method_name_used(KpWriter *w, const char *simple_name)
{
	for (int i = w->type_stack.n - 1; i >= 0; i--) {
		KpTypeSpec *t = w->type_stack.v[i];
		if (!t->fun_names) {
			t->fun_names = xalloc(sizeof(Map));
			for (int j = 0; j < t->funs.n; j++)
				map_put(t->fun_names, ((KpFun *)t->funs.v[j])->name, (void *)1);
		}
		if (map_has(t->fun_names, simple_name))
			return true;
		if (!(t->modifiers & KM(KM_INNER)))
			break;
	}
	return false;
}

static void kp_importable_member(KpWriter *w, const KpMember *m)
{
	const char *alias = kp_alias_of(w, m->canonical);
	const char *simple = alias ? alias : m->simple_name;
	/* name clashes with types */
	if (map_has(&w->importable_idx, simple))
		return;
	intptr_t at = (intptr_t)map_get(&w->member_idx, simple);
	if (!at) {
		vec_push(&w->member_keys, (void *)simple);
		vec_push(&w->member_vals, xalloc(sizeof(Vec)));
		at = w->member_keys.n;
		map_put(&w->member_idx, simple, (void *)at);
	}
	/* importableMembers keeps every reference; suggestedMemberImports makes a set of them (MemberName
	 * equality: package, enclosing class names, simple name) */
	Buf b = {0};
	buf_adds(&b, simple);
	buf_addc(&b, '\x01');
	buf_adds(&b, m->package);
	buf_addc(&b, '\x01');
	for (int i = 0; m->enclosing && i < m->enclosing->n; i++) {
		buf_adds(&b, m->enclosing->names[i]);
		buf_addc(&b, '\x02');
	}
	buf_addc(&b, '\x01');
	buf_adds(&b, m->simple_name);
	const char *key = buf_str(&b);
	if (map_has(&w->member_seen, key))
		return;
	map_put(&w->member_seen, key, (void *)1);
	vec_push(w->member_vals.v[at - 1], (void *)m);
}

/* lookupName(memberName) */
static const char *kp_lookup_member(KpWriter *w, const KpMember *m)
{
	const char *alias = kp_alias_of(w, m->canonical);
	const char *simple = alias ? alias : m->simple_name;
	Vec *imported = map_get(&w->imported_members, simple);
	bool found = false;
	for (int i = 0; imported && i < imported->n && !found; i++)
		found = kp_member_equals(imported->v[i], m);
	if (found && !kp_method_name_used(w, simple))
		return simple;
	if (imported && imported->n && m->enclosing)
		return xcat(kp_lookup_name(w, m->enclosing), ".", simple);
	if (found)
		return simple;
	if (str_eq(w->package ? w->package : "", m->package) && !m->enclosing) {
		map_put(&w->referenced_names, m->simple_name, (void *)1);
		return m->simple_name;
	}
	/* KotlinPoet 2.2.0 imports no member referenced in KDoc */
	if (!(KP_V22 && w->kdoc) && !kp_method_name_used(w, m->simple_name))
		kp_importable_member(w, m);
	return m->canonical;
}

void kp_member_emit(const KpMember *m, KpWriter *w) { kp_emit(w, kp_escape_segments_if_necessary(kp_lookup_member(w, m))); }

/* emitStaticImportMember(canonical, part): part starts with '.' */
static bool kp_emit_static_import_member(KpWriter *w, const char *canonical, const char *part)
{
	const char *rest = part + 1;
	if (!*rest)
		return false;
	unsigned char first = (unsigned char)*rest;
	if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '$' || first == '_')) {
		if (first >= 0x80)
			fail("protoc-gen-uni does not support characters beyond ASCII after a type in KotlinPoet code: %s", part);
		return false;
	}
	const char *member = kp_extract_member_name(rest);
	KpImport *explicit_ = map_get(&w->imports, xcat(canonical, ".", member));
	if (!explicit_)
		return false;
	if (explicit_->alias) {
		/* replaceFirst(memberName, alias): the member name, an identifier, is its own regex */
		const char *at = strstr(rest, member);
		kp_emit(w, xcat(xstrndup(rest, (size_t)(at - rest)), explicit_->alias, at + strlen(member)));
	} else {
		kp_emit(w, rest);
	}
	return true;
}

int kp_writer_imports(KpWriter *w, const char ***names, const char ***aliases)
{
	*names = xalloc(sizeof(char *) * (size_t)(w->import_order.n + 1));
	*aliases = xalloc(sizeof(char *) * (size_t)(w->import_order.n + 1));
	for (int i = 0; i < w->import_order.n; i++) {
		KpImport *im = w->import_order.v[i];
		(*names)[i] = im->qualified;
		(*aliases)[i] = im->alias;
	}
	return w->import_order.n;
}

/* generateImportAliases(simpleName, canonicalNamesToQualifiedNames, capitalizeAliases, imported):
 * the aliases (const char*) of the entries (canonical names and the classes or members, distinct
 * canonical names) in order; the entries are reordered like the alias map. Upstream loops forever
 * when two canonical names give the same alias for every number of segments; this fails instead. */
static Vec kp_import_aliases(const char *simple_name, Vec *canonicals, Vec *items, bool capitalize, Map *imported)
{
	int n = items->n, max_segments = 0;
	Vec *segments = xalloc(sizeof(Vec) * (size_t)n);
	for (int i = 0; i < n; i++) {
		Vec parts = str_split(canonicals->v[i], '.');
		for (int j = 0; j + 1 < parts.n; j++)
			if (!str_eq(parts.v[j], "Companion"))
				vec_push(&segments[i], (void *)kp_upper_first(parts.v[j]));
		if (segments[i].n > max_segments)
			max_segments = segments[i].n;
	}
	/* canonicalNameSegmentsToQualifiedNames: equal segment lists keep the first position and the
	 * last entry */
	Vec keys = {0}, vals = {0}, cvals = {0};
	Map key_idx = {0}; /* the segments joined by \x01 -> 1 + index in keys */
	for (int i = 0; i < n; i++) {
		const char *k = str_join(&segments[i], "\x01");
		intptr_t at = (intptr_t)map_get(&key_idx, k);
		if (at) {
			vals.v[at - 1] = items->v[i];
			cvals.v[at - 1] = canonicals->v[i];
		} else {
			vec_push(&keys, &segments[i]);
			vec_push(&vals, items->v[i]);
			vec_push(&cvals, canonicals->v[i]);
			map_put(&key_idx, k, (void *)(intptr_t)keys.n);
		}
	}
	const char *upper_simple = kp_upper_first(simple_name);
	for (int use = 1;; use++) {
		if (keys.n != n || use > max_segments + 2 + (int)imported->n)
			fail("protoc-gen-uni does not support classes or members named %s whose packages give the same import "
			     "alias (KotlinPoet loops forever)",
			     simple_name);
		Map seen = {0};
		Vec names = {0};
		bool unique = true;
		for (int k = 0; k < keys.n && unique; k++) {
			Vec *s = keys.v[k];
			int take = use < s->n ? use : s->n;
			Buf b = {0};
			for (int j = s->n - take; j < s->n; j++)
				buf_adds(&b, s->v[j]);
			const char *prefix = buf_str(&b);
			if (!capitalize)
				prefix = kp_lower_first(prefix);
			Buf a = {0};
			buf_adds(&a, prefix);
			buf_adds(&a, upper_simple);
			for (int j = 0; j < use - s->n; j++)
				buf_addc(&a, '_');
			const char *alias = xstrdup(buf_str(&a));
			if (map_has(imported, alias))
				unique = false; /* restart with more segments */
			else if (map_has(&seen, alias))
				unique = false; /* the map of aliases is smaller than the entries */
			map_put(&seen, alias, (void *)1);
			vec_push(&names, (void *)alias);
		}
		if (unique) {
			/* the entries in the order of the alias map (keys) */
			for (int k = 0; k < keys.n; k++) {
				items->v[k] = vals.v[k];
				canonicals->v[k] = cvals.v[k];
			}
			items->n = canonicals->n = keys.n;
			return names;
		}
	}
}

typedef struct {
	const char *simple;
	Vec *classes;
	int index;
} KpImportable;

static int kp_cmp_importable(const void *a, const void *b)
{
	const KpImportable *x = a, *y = b;
	return x->classes->n != y->classes->n ? (x->classes->n < y->classes->n ? -1 : 1) : 0;
}

KpWriter *kp_writer_with_collected_imports(Buf *out, const Vec *member_imports, void (*emit_step)(KpWriter *, void *),
					   void *arg)
{
	Vec member = {0};
	for (int i = 0; i < member_imports->n; i++) {
		KpImport *im = xalloc(sizeof *im);
		im->qualified = member_imports->v[i];
		vec_push(&member, im);
	}
	KpWriter *collector = kp_writer_new(NULL, INT_MAX);
	kp_writer_set_imports(collector, &member);
	emit_step(collector, arg);
	/* suggestedTypeImports: importable types not referenced, as sets */
	Vec entries = {0};
	for (int i = 0; i < collector->importable_keys.n; i++) {
		const char *simple = collector->importable_keys.v[i];
		if (map_has(&collector->referenced_names, simple))
			continue;
		Vec *set = collector->importable_vals.v[i];
		KpImportable *e = xalloc(sizeof *e);
		e->simple = simple, e->classes = set, e->index = i;
		vec_push(&entries, e);
	}
	java_sort(entries.v, entries.n, kp_cmp_importable);
	Map generated = {0}; /* canonical -> KpImport* */
	Vec generated_order = {0};
	Map imported = {0}; /* imported simple names / aliases -> KpType* */
	for (int i = 0; i < entries.n; i++) {
		KpImportable *e = entries.v[i];
		/* associateBy canonical name */
		Vec by_canonical = {0}, canonicals = {0};
		Map canon_idx = {0};
		for (int j = 0; j < e->classes->n; j++) {
			const char *cn = kp_canonical_name(e->classes->v[j]);
			intptr_t at = (intptr_t)map_get(&canon_idx, cn);
			if (at) {
				by_canonical.v[at - 1] = e->classes->v[j];
			} else {
				vec_push(&by_canonical, e->classes->v[j]);
				vec_push(&canonicals, (void *)cn);
				map_put(&canon_idx, cn, (void *)(intptr_t)by_canonical.n);
			}
		}
		if (by_canonical.n == 1 && !map_has(&collector->referenced_names, e->simple)) {
			const char *cn = kp_canonical_name(by_canonical.v[0]);
			KpImport *im = map_get(&generated, cn);
			if (!im) {
				im = xalloc(sizeof *im);
				vec_push(&generated_order, im);
				map_put(&generated, cn, im);
			}
			im->qualified = cn, im->alias = NULL;
			map_put(&imported, e->simple, e->classes->v[0]);
		} else {
			Vec aliases = kp_import_aliases(e->simple, &canonicals, &by_canonical, true, &imported);
			for (int j = 0; j < aliases.n; j++) {
				const char *alias = kp_escape_as_alias(aliases.v[j]);
				const char *cn = canonicals.v[j];
				KpImport *im = map_get(&generated, cn);
				if (!im) {
					im = xalloc(sizeof *im);
					vec_push(&generated_order, im);
					map_put(&generated, cn, im);
				}
				im->qualified = cn, im->alias = alias;
				map_put(&imported, alias, by_canonical.v[j]);
			}
		}
	}
	/* suggestedMemberImports: all importable members, as sets */
	entries.n = 0;
	for (int i = 0; i < collector->member_keys.n; i++) {
		KpImportable *e = xalloc(sizeof *e);
		e->simple = collector->member_keys.v[i], e->classes = collector->member_vals.v[i], e->index = i;
		vec_push(&entries, e);
	}
	java_sort(entries.v, entries.n, kp_cmp_importable);
	Map imported_members = {0}; /* imported simple names / aliases -> Vec* of KpMember* */
	for (int i = 0; i < entries.n; i++) {
		KpImportable *e = entries.v[i];
		Vec by_canonical = {0}, canonicals = {0};
		Map canon_idx = {0};
		for (int j = 0; j < e->classes->n; j++) {
			const char *cn = ((KpMember *)e->classes->v[j])->canonical;
			intptr_t at = (intptr_t)map_get(&canon_idx, cn);
			if (at) {
				by_canonical.v[at - 1] = e->classes->v[j];
			} else {
				vec_push(&by_canonical, e->classes->v[j]);
				vec_push(&canonicals, (void *)cn);
				map_put(&canon_idx, cn, (void *)(intptr_t)by_canonical.n);
			}
		}
		if (by_canonical.n == 1 && !map_has(&collector->referenced_names, e->simple)) {
			const char *cn = canonicals.v[0];
			KpImport *im = map_get(&generated, cn);
			if (!im) {
				im = xalloc(sizeof *im);
				vec_push(&generated_order, im);
				map_put(&generated, cn, im);
			}
			im->qualified = cn, im->alias = NULL;
			/* overloads of one canonical name: one import for the whole set */
			map_put(&imported_members, e->simple, e->classes);
		} else {
			Vec aliases = kp_import_aliases(e->simple, &canonicals, &by_canonical, false, &imported_members);
			for (int j = 0; j < aliases.n; j++) {
				const char *alias = kp_escape_as_alias(aliases.v[j]);
				const char *cn = canonicals.v[j];
				KpImport *im = map_get(&generated, cn);
				if (!im) {
					im = xalloc(sizeof *im);
					vec_push(&generated_order, im);
					map_put(&generated, cn, im);
				}
				im->qualified = cn, im->alias = alias;
				Vec *one = xalloc(sizeof *one);
				vec_push(one, by_canonical.v[j]);
				map_put(&imported_members, alias, one);
			}
		}
	}
	kp_writer_close(collector);
	KpWriter *w = kp_writer_new(out, 100);
	Map member_set = {0};
	for (int i = 0; i < member.n; i++)
		map_put(&member_set, ((KpImport *)member.v[i])->qualified, (void *)1);
	for (int i = 0; i < generated_order.n; i++) {
		KpImport *im = generated_order.v[i];
		if (!map_has(&member_set, im->qualified))
			vec_push(&member, im);
	}
	kp_writer_set_imports(w, &member);
	w->imported_types = imported;
	w->imported_members = imported_members;
	return w;
}
