/* Copyright (c) ScalaPB authors (Nadav Samet and contributors).
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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/FunctionalPrinter.scala and
 * ExpressionBuilder.scala. The printer is mutable here: the Scala code uses each printer value
 * once. */

/* ---- FunctionalPrinter ---- */

typedef FunctionalPrinter Printer;

#define INDENT_SIZE 2

/* add(s): splits s at every newline (keeping empty lines) and indents each line. */
static void p_add(Printer *p, const char *s)
{
	Vec parts = str_split(s, '\n');
	for (int i = 0; i < parts.n; i++) {
		Buf b = {0};
		for (int k = 0; k < p->indent * INDENT_SIZE; k++)
			buf_addc(&b, ' ');
		buf_adds(&b, parts.v[i]);
		vec_push(&p->lines, (void *)buf_str(&b));
	}
}

static void p_addf(Printer *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void p_addf(Printer *p, const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	p_add(p, buf_str(&b));
}

static void p_seq(Printer *p, const Vec *v)
{
	for (int i = 0; i < v->n; i++)
		p_add(p, v->v[i]);
}

static void p_indent(Printer *p) { p->indent++; }

static void p_outdent(Printer *p)
{
	if (p->indent < 1)
		java_exception("java.lang.AssertionError", "assertion failed");
	p->indent--;
}

static void p_newline(Printer *p) { p_add(p, ""); }

/* addWithDelimiter: the delimiter after every line but the last. */
static void p_add_with_delimiter(Printer *p, const char *delim, const Vec *s)
{
	for (int i = 0; i < s->n; i++)
		p_add(p, i == s->n - 1 ? s->v[i] : xcat(s->v[i], delim));
}

/* addGroupsWithDelimiter: groups are Vec* of lines; the delimiter after the last line of every
 * group but the last. */
static void p_add_groups_with_delimiter(Printer *p, const char *delim, const Vec *groups)
{
	for (int g = 0; g < groups->n; g++) {
		const Vec *group = groups->v[g];
		for (int i = 0; i < group->n; i++)
			p_add(p, g < groups->n - 1 && i == group->n - 1 ? xcat(group->v[i], delim) : group->v[i]);
	}
}

static const char *p_result(Printer *p) { return str_join(&p->lines, "\n"); }

/* ---- ExpressionBuilder.scala ---- */

enum { LIT_FUNCTION, LIT_METHOD, LIT_OPERATOR };

typedef struct {
	int kind;
	const char *name;
} Lit;

/* An expression as the list of its non-identity literals, outermost first (Identity drops out
 * of every combination in ExpressionBuilder). */
typedef struct {
	int n;
	Lit l[8];
} Expr;

static const Expr IDENTITY = {0};

static Expr e_lit(int kind, const char *name)
{
	Expr e = {1, {{kind, name}}};
	return e;
}
static Expr e_func(const char *name) { return e_lit(LIT_FUNCTION, name); }
static Expr e_method(const char *name) { return e_lit(LIT_METHOD, name); }
static Expr e_op(const char *name) { return e_lit(LIT_OPERATOR, name); }

/* a andThen b: b applies to the result of a. */
static Expr e_then(Expr a, Expr b)
{
	Expr r = {0};
	if (a.n + b.n > 8)
		java_exception("java.lang.IllegalStateException", "expression too long");
	for (int i = 0; i < b.n; i++)
		r.l[r.n++] = b.l[i];
	for (int i = 0; i < a.n; i++)
		r.l[r.n++] = a.l[i];
	return r;
}

static const char *run_singleton(const Lit *l, int n, const char *e)
{
	if (n == 0)
		return e;
	const char *inner = run_singleton(l + 1, n - 1, e);
	switch (l[0].kind) {
	case LIT_FUNCTION: return xcat(l[0].name, "(", inner, ")");
	case LIT_METHOD: return xcat(inner, ".", l[0].name);
	default: return xcat(inner, " ", l[0].name);
	}
}

static const char *e_run_singleton(Expr es, const char *e) { return run_singleton(es.l, es.n, e); }

static const char *convert_collection(const char *expr, Enclosing target)
{
	Expr c = IDENTITY;
	if (target.kind == ENC_COLLECTION) {
		if (target.tc)
			c = e_func(xcat(target.tc, ".fromIteratorUnsafe"));
		else if (str_eq(target.cc, ScalaVector))
			c = e_method("toVector");
		else if (str_eq(target.cc, ScalaSeq))
			c = e_method("toSeq");
		else if (str_eq(target.cc, ScalaMap))
			c = e_method("toMap");
		else if (str_eq(target.cc, ScalaIterable))
			c = e_func("_root_.scalapb.internal.compat.toIterable");
		else
			c = e_func("_root_.scalapb.internal.compat.convertTo");
	}
	return e_run_singleton(c, expr);
}

static const char *run_collection(Expr es, const char *e0, Enclosing source, Enclosing target, bool must_copy)
{
	bool need_variable = false;
	for (int i = 0; i + 1 < es.n; i++)
		if (es.l[i].kind == LIT_FUNCTION)
			need_variable = true;
	const char *e = e0;
	if (source.kind == ENC_COLLECTION) {
		if (source.tc)
			e = xcat(source.tc, ".toIterator(", e0, ")");
		else if (str_eq(source.cc, ScalaIterator))
			e = e0;
		else
			e = xcat(e0, ".iterator");
	}
	bool force = source.kind == ENC_COLLECTION && source.tc && !enc_eq(source, target);
	if (need_variable)
		return convert_collection(xcat(e, ".map(__e => ", e_run_singleton(es, "__e"), ")"), target);
	if (es.n) {
		const char *f = es.n == 1 && es.l[0].kind == LIT_FUNCTION ? xcat(es.l[0].name, "(_)") : e_run_singleton(es, "_");
		return convert_collection(xcat(e, ".map(", f, ")"), target);
	}
	if (must_copy)
		return convert_collection(xcat(e, ".map(_root_.scala.Predef.identity)"), target);
	if (force)
		return convert_collection(e, target);
	return e0;
}

static const char *e_apply4(Expr es, const char *e, Enclosing source, Enclosing target, bool must_copy)
{
	if (source.kind == ENC_NONE)
		return e_run_singleton(es, e);
	return run_collection(es, e, source, target, must_copy);
}

static const char *e_apply(Expr es, const char *e, Enclosing source) { return e_apply4(es, e, source, source, false); }
