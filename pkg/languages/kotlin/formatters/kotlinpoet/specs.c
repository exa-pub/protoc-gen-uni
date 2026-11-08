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

/* A port of the specs of KotlinPoet 2.4.0 and 2.2.0 (kotlinpoet/src/jvmMain/kotlin/com/squareup/
 * kotlinpoet/): TypeName.kt, ClassName.kt, ParameterizedTypeName.kt, LambdaTypeName.kt,
 * TypeVariableName.kt (types), MemberName.kt, AnnotationSpec.kt, ParameterSpec.kt, PropertySpec.kt,
 * FunSpec.kt, TypeSpec.kt and FileSpec.kt: their builders and emit functions for the members
 * generated code uses. */
#include "internal.h"

#include <limits.h>
#include <string.h>

/* ---- TypeName ---- */

KpType *kp_class_name(const char *package, int n, const char *const *simple_names)
{
	if (n == 0)
		kp_illegal_argument("simpleNames must not be empty");
	for (int i = 0; i < n; i++)
		if (!simple_names[i][0]) {
			Vec v = {0};
			for (int j = 0; j < n; j++)
				vec_push(&v, (void *)simple_names[j]);
			kp_illegal_argument(xcat("simpleNames must not contain empty items: [", str_join(&v, ", "), "]"));
		}
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_CLASS;
	t->n = n + 1;
	t->names = xalloc(sizeof(char *) * (size_t)t->n);
	t->names[0] = package;
	for (int i = 0; i < n; i++)
		t->names[i + 1] = simple_names[i];
	return t;
}

KpType *kp_class(const char *package, const char *simple_name) { return kp_class_name(package, 1, &simple_name); }

static KpType *kp_class_of_names(const char *const *names, int n)
{
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_CLASS;
	t->n = n;
	t->names = xalloc(sizeof(char *) * (size_t)n);
	memcpy(t->names, names, sizeof(char *) * (size_t)n);
	return t;
}

KpType *kp_nested_class(const KpType *c, const char *name)
{
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_CLASS;
	t->n = c->n + 1;
	t->names = xalloc(sizeof(char *) * (size_t)t->n);
	memcpy(t->names, c->names, sizeof(char *) * (size_t)c->n);
	t->names[c->n] = name;
	return t;
}

KpType *kp_type_variable(const char *name)
{
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_TYPEVAR;
	t->name = name;
	return t;
}

KpType *kp_top_level_class_name(const KpType *c) { return kp_class_of_names(c->names, 2); }
KpType *kp_enclosing_class_name(const KpType *c) { return c->n != 2 ? kp_class_of_names(c->names, c->n - 1) : NULL; }

const char *kp_canonical_name(const KpType *c)
{
	Buf b = {0};
	for (int i = c->names[0][0] ? 0 : 1; i < c->n; i++) {
		if (b.n)
			buf_addc(&b, '.');
		buf_adds(&b, c->names[i]);
	}
	return xstrdup(buf_str(&b));
}

KpType *kp_parameterized(KpType *raw, int n, KpType *const *args)
{
	if (n == 0)
		kp_illegal_argument(xcat("no type arguments: ", kp_type_to_string(raw)));
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_PARAMETERIZED;
	t->raw = raw;
	for (int i = 0; i < n; i++)
		vec_push(&t->type_args, args[i]);
	return t;
}

KpType *kp_lambda(int n, KpParam *const *params, KpType *ret)
{
	KpType *t = xalloc(sizeof *t);
	t->kind = KT_LAMBDA;
	for (int i = 0; i < n; i++) {
		if (params[i]->default_value)
			kp_illegal_argument("Parameters with default values are not allowed");
		vec_push(&t->params, params[i]);
	}
	t->ret = ret;
	return t;
}

KpType *kp_unit(void) { return kp_class("kotlin", "Unit"); }

/* ---- MemberName ---- */

static KpMember *kp_member_new(const char *package, KpType *enclosing, const char *simple_name)
{
	KpMember *m = xalloc(sizeof *m);
	m->package = package, m->enclosing = enclosing, m->simple_name = simple_name;
	/* canonicalName: the enclosing class, else the package unless blank */
	bool blank = true;
	for (const char *c = package; *c && blank; c++)
		blank = *c == ' ' || *c == '\t' || *c == '\n' || *c == '\r' || *c == '\f' || *c == '\v';
	m->canonical = enclosing ? xcat(kp_canonical_name(enclosing), ".", simple_name)
				 : blank ? simple_name : xcat(package, ".", simple_name);
	return m;
}

KpMember *kp_member(KpType *enclosing, const char *simple_name) { return kp_member_new(enclosing->names[0], enclosing, simple_name); }
KpMember *kp_top_level_member(const char *package, const char *simple_name) { return kp_member_new(package, NULL, simple_name); }

bool kp_member_equals(const KpMember *a, const KpMember *b)
{
	if (a == b)
		return true;
	if (!str_eq(a->package, b->package) || !str_eq(a->simple_name, b->simple_name) || !a->enclosing != !b->enclosing)
		return false;
	return !a->enclosing || kp_type_equals(a->enclosing, b->enclosing);
}

bool kp_type_equals(const KpType *a, const KpType *b)
{
	if (a == b)
		return true;
	if (a->kind != b->kind || a->nullable != b->nullable)
		return false;
	switch (a->kind) {
	case KT_CLASS:
		if (a->n != b->n)
			return false;
		for (int i = 0; i < a->n; i++)
			if (!str_eq(a->names[i], b->names[i]))
				return false;
		return true;
	case KT_TYPEVAR:
		return str_eq(a->name, b->name);
	case KT_PARAMETERIZED:
		if (!kp_type_equals(a->raw, b->raw) || a->type_args.n != b->type_args.n)
			return false;
		for (int i = 0; i < a->type_args.n; i++)
			if (!kp_type_equals(a->type_args.v[i], b->type_args.v[i]))
				return false;
		return true;
	default:
		if (!kp_type_equals(a->ret, b->ret) || a->params.n != b->params.n)
			return false;
		for (int i = 0; i < a->params.n; i++) {
			KpParam *x = a->params.v[i], *y = b->params.v[i];
			if (!str_eq(x->name, y->name) || !kp_type_equals(x->type, y->type))
				return false;
		}
		return true;
	}
}

void kp_type_emit_nullable(const KpType *t, KpWriter *w)
{
	if (t->nullable)
		kp_emit(w, "?");
}

static void kp_param_emit_default(KpParam *p, KpWriter *w, void *arg)
{
	(void)arg;
	kp_param_emit(p, w, true);
}

void kp_type_emit(KpType *t, KpWriter *w)
{
	switch (t->kind) {
	case KT_CLASS:
		kp_emit(w, kp_escape_segments_if_necessary(kp_lookup_name(w, t)));
		break;
	case KT_TYPEVAR:
		kp_emit(w, t->name);
		break;
	case KT_PARAMETERIZED:
		kp_type_emit(t->raw, w);
		kp_emit(w, "<");
		for (int i = 0; i < t->type_args.n; i++) {
			if (i > 0)
				kp_emit(w, ", ");
			kp_type_emit(t->type_args.v[i], w);
			kp_type_emit_nullable(t->type_args.v[i], w);
		}
		kp_emit(w, ">");
		break;
	default:
		if (t->nullable)
			kp_emit(w, "(");
		kp_params_emit(&t->params, w, false, kp_param_emit_default, NULL);
		kp_emit_codef(w, t->ret->kind == KT_LAMBDA ? " -> (%T)" : " -> %T", 1, (KpArg[]){kp_arg_type(t->ret)});
		if (t->nullable)
			kp_emit(w, ")");
	}
}

const char *kp_type_to_string(KpType *t)
{
	if (!t->cached) {
		Buf b = {0};
		KpWriter *w = kp_writer_new(&b, INT_MAX);
		kp_type_emit(t, w);
		kp_type_emit_nullable(t, w);
		kp_writer_close(w);
		t->cached = xstrdup(buf_str(&b));
	}
	return t->cached;
}

const char *kp_code_to_string(const KpCode *c)
{
	Buf b = {0};
	KpWriter *w = kp_writer_new(&b, INT_MAX);
	kp_emit_code(w, c, false, false);
	kp_writer_close(w);
	return xstrdup(buf_str(&b));
}

/* ---- AnnotationSpec ---- */

KpAnnotation *kp_annotation(KpType *type)
{
	KpAnnotation *a = xalloc(sizeof *a);
	a->type = type;
	return a;
}

void kp_annotation_add_member(KpAnnotation *a, const char *format, int nargs, const KpArg *args)
{
	vec_push(&a->members, kp_code_of(format, nargs, args));
}

void kp_annotation_emit(KpAnnotation *a, KpWriter *w, bool inline_)
{
	kp_emit(w, "@");
	if (a->use_site_target)
		kp_emit(w, xcat(a->use_site_target, ":"));
	kp_emit_codef(w, "%T", 1, (KpArg[]){kp_arg_type(a->type)});
	if (!a->members.n)
		return;
	const char *whitespace = inline_ ? "" : "\n";
	kp_emit(w, "(");
	if (a->members.n > 1) {
		kp_emit(w, whitespace);
		kp_writer_indent(w, 1);
	}
	/* members.map { if (inline) it.replaceAll("[⇥|⇤]", "") else it }: replaceAll replaces that
	 * literal text, which a member does not contain */
	kp_emit_code(w, kp_join_to_code(&a->members, inline_ ? ", " : ",\n", "", !inline_ && a->members.n > 1 ? "," : ""), true,
		     false);
	if (a->members.n > 1) {
		kp_writer_unindent(w, 1);
		kp_emit(w, whitespace);
	}
	kp_emit(w, ")");
}

/* ---- ParameterSpec ---- */

KpParam *kp_param(const char *name, KpType *type)
{
	KpParam *p = xalloc(sizeof *p);
	p->name = name, p->type = type;
	return p;
}

static void kp_param_emit_default_value(KpParam *p, KpWriter *w)
{
	if (p->default_value)
		kp_emit_codef(w, kp_code_has_statements(p->default_value) ? " = %L" : " = " KP_OPEN "%L" KP_CLOSE, 1,
			      (KpArg[]){kp_arg_code(p->default_value)});
}

void kp_param_emit(KpParam *p, KpWriter *w, bool include_type)
{
	kp_emit_modifiers(w, 0, 0);
	if (p->name[0])
		kp_emit_codef(w, "%N", 1, (KpArg[]){kp_arg_name(p->name)});
	if (p->name[0] && include_type)
		kp_emit_codef(w, ": ", 0, NULL);
	if (include_type)
		kp_emit_codef(w, "%T", 1, (KpArg[]){kp_arg_type(p->type)});
	kp_param_emit_default_value(p, w);
}

void kp_params_emit(const Vec *params, KpWriter *w, bool force_new_lines, void (*emit_block)(KpParam *, KpWriter *, void *),
		    void *arg)
{
	kp_emit(w, "(");
	if (params->n) {
		bool new_lines = params->n > 2 || force_new_lines;
		if (new_lines) {
			kp_emit(w, "\n");
			kp_writer_indent(w, 1);
		}
		for (int i = 0; i < params->n; i++) {
			if (i > 0)
				kp_emit(w, new_lines ? "\n" : ", ");
			emit_block(params->v[i], w, arg);
			if (new_lines)
				kp_emit(w, ",");
		}
		if (new_lines) {
			kp_writer_unindent(w, 1);
			kp_emit(w, "\n");
		}
	}
	kp_emit(w, ")");
}

/* ---- PropertySpec ---- */

KpProperty *kp_property(const char *name, KpType *type, KpModifiers modifiers)
{
	KpProperty *p = xalloc(sizeof *p);
	p->name = name, p->type = type, p->modifiers = modifiers;
	return p;
}

static void kp_fun_emit(KpFun *f, KpWriter *w, const char *enclosing_name, KpModifiers implicit);

/* emit(codeWriter, implicitModifiers, withInitializer, emitKdoc, inline, inlineAnnotations = false):
 * the properties have no KDoc. */
static void kp_property_emit(KpProperty *p, KpWriter *w, KpModifiers implicit, bool with_initializer, bool inline_)
{
	kp_emit_annotations(w, &p->annotations, false);
	kp_emit_modifiers(w, p->modifiers, implicit);
	kp_emit_codef(w, "val ", 0, NULL);
	kp_emit_codef(w, "%N:" KP_WRAP "%T", 2, (KpArg[]){kp_arg_name(p->name), kp_arg_type(p->type)});
	if (with_initializer && p->initializer) {
		kp_emit_codef(w, KP_WRAP "=" KP_WRAP, 0, NULL);
		KpCode *init = kp_code_trim_trailing_newline(p->initializer, 0);
		kp_emit_code(w, kp_code_of(kp_code_has_statements(p->initializer) ? "%L" : KP_OPEN "%L" KP_CLOSE, 1,
					   (KpArg[]){kp_arg_code(init)}),
			     (p->modifiers & KM(KM_CONST)) != 0, false);
	}
	if (!inline_)
		kp_emit(w, "\n");
	if (p->getter) {
		/* the implicit modifiers without visibility (no inline getter) */
		KpModifiers accessor = implicit & ~(KM(KM_PUBLIC) | KM(KM_PROTECTED) | KM(KM_PRIVATE) | KM(KM_INTERNAL));
		kp_writer_indent(w, 1);
		kp_fun_emit(p->getter, w, NULL, accessor);
		kp_writer_unindent(w, 1);
	}
}

/* ---- FunSpec ---- */

/* RETURN_EXPRESSION_BODY_PREFIX_SPACE / _NBSP: CodeBlock.of("return ") and CodeBlock.of("return·") */
static const KpCode *kp_return_prefix(bool nbsp) { return kp_code_of(nbsp ? "return" KP_NBSP : "return ", 0, NULL); }

KpFun *kp_fun(const char *name)
{
	KpFun *f = xalloc(sizeof *f);
	f->name = name;
	f->kdoc = kp_code();
	f->body = kp_code();
	return f;
}

KpFun *kp_constructor(void)
{
	KpFun *f = kp_fun("constructor()");
	f->is_constructor = true;
	return f;
}

KpFun *kp_getter(void)
{
	KpFun *f = kp_fun("get()");
	f->is_getter = true;
	return f;
}

void kp_fun_add_kdoc(KpFun *f, const char *format) { kp_add0(f->kdoc, format); }

void kp_fun_build(KpFun *f)
{
	if (!kp_code_is_empty(f->body) && (f->modifiers & (KM(KM_ABSTRACT) | KM(KM_EXPECT))))
		kp_illegal_argument(xfmt("abstract or expect function %s cannot have code", f->name));
}

/* asExpressionBody(): KotlinPoet 2.2.0 trims the body without keeping it balanced. */
static KpCode *kp_as_expression_body(const KpCode *body)
{
	KpCode *c = kp_code_trim(body, !KP_V22);
	if (!KP_V22 && kp_code_has_unmatched_closing_statement(c))
		return NULL;
	KpCode *r = kp_code_without_prefix(c, kp_return_prefix(false));
	if (!r)
		r = kp_code_without_prefix(c, kp_return_prefix(true));
	if (r)
		return r;
	if (kp_code_without_prefix(c, kp_code_of("throw ", 0, NULL)) ||
	    kp_code_without_prefix(c, kp_code_of("throw" KP_NBSP, 0, NULL)))
		return c;
	return NULL;
}

/* returnsWithoutLinebreak() */
static KpCode *kp_returns_without_linebreak(const KpCode *body)
{
	KpCode *b = NULL;
	for (int i = 0; i < body->parts.n; i++) {
		const char *part = body->parts.v[i];
		if (has_prefix(part, "return ")) {
			if (!b) {
				b = kp_code();
				kp_add_code(b, body);
			}
			b->parts.v[i] = (void *)xcat("return" KP_NBSP, part + 7);
		}
	}
	return b ? b : (KpCode *)body;
}

static void kp_fun_emit(KpFun *f, KpWriter *w, const char *enclosing_name, KpModifiers implicit)
{
	(void)enclosing_name;
	/* kdocWithTags(): no receiver, parameter or return KDoc */
	kp_emit_kdoc(w, kp_code_ensure_ends_with_newline(f->kdoc));
	kp_emit_annotations(w, &f->annotations, false);
	kp_emit_modifiers(w, f->modifiers, implicit);
	if (!f->is_constructor && !f->is_getter)
		kp_emit_codef(w, "fun ", 0, NULL);
	/* emitSignature */
	if (f->is_constructor)
		kp_emit_codef(w, "constructor", 0, NULL);
	else if (f->is_getter)
		kp_emit_codef(w, "get", 0, NULL);
	else
		kp_emit_codef(w, "%N", 1, (KpArg[]){kp_arg_name(f->name)});
	kp_params_emit(&f->params, w, false, kp_param_emit_default, NULL);
	KpCode *expr = kp_as_expression_body(f->body);
	KpType *unit = kp_unit();
	KpType *ret = f->ret ? f->ret : unit;
	if (!kp_type_equals(ret, unit) || (!f->is_constructor && !f->is_getter && expr))
		kp_emit_codef(w, ": %T", 1, (KpArg[]){kp_arg_type(ret)});
	/* shouldOmitBody */
	if ((f->modifiers & (KM(KM_ABSTRACT) | KM(KM_EXPECT))) || (implicit & KM(KM_EXPECT))) {
		if (!kp_code_is_empty(f->body))
			kp_illegal_state(xfmt("function %s cannot have code", f->name));
		kp_emit(w, "\n");
		return;
	}
	if ((f->is_constructor || ((f->modifiers | implicit) & KM(KM_EXTERNAL))) && kp_code_is_empty(f->body)) {
		kp_emit(w, "\n");
		return;
	}
	if (expr) {
		kp_emit_code(w, kp_code_of(" = %L", 1, (KpArg[]){kp_arg_code(expr)}), false, true);
	} else {
		kp_emit_codef(w, " {\n", 0, NULL);
		kp_writer_indent(w, 1);
		kp_emit_code(w, kp_returns_without_linebreak(f->body), false, true);
		kp_writer_unindent(w, 1);
		kp_emit(w, "}\n");
	}
}

/* ---- TypeSpec ---- */

static KpTypeSpec *kp_type_new(int kind, const char *name)
{
	KpTypeSpec *t = xalloc(sizeof *t);
	t->kind = kind, t->name = name;
	t->kdoc = kp_code();
	return t;
}

static const char *const kp_kind_names[] = {"CLASS", "OBJECT", "INTERFACE"};

KpTypeSpec *kp_class_builder(const char *name) { return kp_type_new(KK_CLASS, name); }
KpTypeSpec *kp_object_builder(const char *name) { return kp_type_new(KK_OBJECT, name); }

/* checkCanHaveSuperclass() (no enums, annotations or value classes) */
static void kp_check_can_have_superclass(KpTypeSpec *t)
{
	if (t->kind == KK_INTERFACE)
		kp_illegal_state(xcat("only classes can have super classes, not ", kp_kind_names[t->kind]));
}

void kp_type_superclass(KpTypeSpec *t, KpType *superclass)
{
	kp_check_can_have_superclass(t);
	if (t->superclass)
		kp_illegal_state(xcat("superclass already set to ", kp_type_to_string(t->superclass)));
	t->superclass = superclass;
}

void kp_type_add_superclass_ctor_param(KpTypeSpec *t, const char *format, int nargs, const KpArg *args)
{
	KpCode *c = kp_code_of(format, nargs, args);
	kp_check_can_have_superclass(t);
	vec_push(&t->super_ctor_params, c);
}
KpTypeSpec *kp_interface_builder(const char *name) { return kp_type_new(KK_INTERFACE, name); }
void kp_type_add_kdoc(KpTypeSpec *t, const char *format) { kp_add0(t->kdoc, format); }

void kp_type_add_superinterface(KpTypeSpec *t, KpType *superinterface)
{
	for (int i = 0; i < t->superinterfaces.n; i++)
		if (kp_type_equals(t->superinterfaces.v[i], superinterface))
			return;
	vec_push(&t->superinterfaces, superinterface);
}

void kp_type_primary_constructor(KpTypeSpec *t, KpFun *constructor)
{
	if (t->kind != KK_CLASS)
		kp_illegal_state(xcat(kp_kind_names[t->kind], " can't have a primary constructor"));
	t->primary_constructor = constructor;
}

void kp_type_build(KpTypeSpec *t)
{
	bool is_abstract = (t->modifiers & (KM(KM_ABSTRACT) | KM(KM_SEALED))) || t->kind == KK_INTERFACE;
	for (int i = 0; i < t->funs.n; i++) {
		KpFun *f = t->funs.v[i];
		if (!is_abstract && (f->modifiers & KM(KM_ABSTRACT)))
			kp_illegal_argument(xfmt("non-abstract type %s cannot declare abstract function %s", t->name, f->name));
	}
}

/* constructorProperties(): the properties declared by parameters of the primary constructor. */
static KpProperty *kp_constructor_property(KpTypeSpec *t, const char *name)
{
	if (!t->primary_constructor)
		return NULL;
	for (int i = 0; i < t->properties.n; i++) {
		KpProperty *p = t->properties.v[i];
		if (!str_eq(p->name, name) || p->getter)
			continue;
		KpParam *param = NULL;
		for (int j = 0; j < t->primary_constructor->params.n && !param; j++)
			if (str_eq(((KpParam *)t->primary_constructor->params.v[j])->name, p->name))
				param = t->primary_constructor->params.v[j];
		if (!param || !kp_type_equals(param->type, p->type))
			continue;
		const char *pname = kp_code_to_string(kp_code_of("%N", 1, (KpArg[]){kp_arg_name(param->name)}));
		const char *init = p->initializer ? kp_code_to_string(p->initializer) : "null";
		if (!str_eq(pname, kp_escape_unvalidated(init)))
			continue;
		return p;
	}
	return NULL;
}

typedef struct {
	KpTypeSpec *type;
	bool *wrap_supertypes;
} KpCtorParamCtx;

static void kp_ctor_param_emit(KpParam *param, KpWriter *w, void *arg)
{
	KpCtorParamCtx *c = arg;
	*c->wrap_supertypes = true;
	KpProperty *p = kp_constructor_property(c->type, param->name);
	if (p) {
		kp_property_emit(p, w, KM(KM_PUBLIC), false, true);
		kp_param_emit_default_value(param, w);
	} else {
		kp_param_emit(param, w, true);
	}
}

static void kp_type_spec_emit(KpTypeSpec *t, KpWriter *w)
{
	int previous_statement_line = kp_writer_statement_line(w);
	kp_writer_set_statement_line(w, -1);
	/* kdocWithConstructorDocs(): the constructor and its parameters have no KDoc */
	KpCode *kdoc = kp_code_ensure_ends_with_newline(t->kdoc);
	Vec kdocs = {0};
	if (!kp_code_is_empty(kdoc))
		vec_push(&kdocs, kdoc);
	kp_emit_kdoc(w, kp_join_to_code(&kdocs, "\n", "", ""));
	kp_emit_annotations(w, &t->annotations, false);
	kp_emit_modifiers(w, t->modifiers, KM(KM_PUBLIC));
	kp_emit(w, t->kind == KK_CLASS ? "class" : t->kind == KK_OBJECT ? "object" : "interface");
	if (t->name)
		kp_emit_codef(w, " %N", 1, (KpArg[]){kp_arg_name(t->name)});
	bool wrap_supertypes = false;
	KpFun *ctor = t->primary_constructor;
	if (ctor) {
		kp_writer_push_type(w, t);
		if (ctor->annotations.n) {
			kp_emit(w, " ");
			kp_emit_annotations(w, &ctor->annotations, true);
		}
		if (ctor->modifiers) {
			if (!ctor->annotations.n)
				kp_emit(w, " ");
			kp_emit_modifiers(w, ctor->modifiers, 0);
		}
		if (ctor->annotations.n || ctor->modifiers)
			kp_emit(w, "constructor");
		KpCtorParamCtx ctx = {t, &wrap_supertypes};
		kp_params_emit(&ctor->params, w, true, kp_ctor_param_emit, &ctx);
		kp_writer_pop_type(w);
	}
	/* the superclass with its constructor parameters (the only constructor is the primary one),
	 * then the superinterfaces */
	Vec super_types = {0};
	if (t->superclass)
		vec_push(&super_types, kp_code_of("%T(%L)", 2,
						  (KpArg[]){kp_arg_type(t->superclass),
							    kp_arg_code(kp_join_to_code(&t->super_ctor_params, ", ", "", ""))}));
	for (int i = 0; i < t->superinterfaces.n; i++)
		vec_push(&super_types, kp_code_of("%T", 1, (KpArg[]){kp_arg_type(t->superinterfaces.v[i])}));
	if (super_types.n)
		kp_emit_code(w, kp_join_to_code(&super_types, wrap_supertypes ? ",\n    " : "," KP_WRAP, " : ", ""), false, false);
	bool has_no_body = !t->funs.n && !t->types.n;
	for (int i = 0; i < t->properties.n && has_no_body; i++)
		if (!kp_constructor_property(t, ((KpProperty *)t->properties.v[i])->name))
			has_no_body = false;
	if (has_no_body) {
		kp_emit(w, "\n");
		kp_writer_set_statement_line(w, previous_statement_line);
		return;
	}
	kp_emit(w, " {\n");
	kp_writer_push_type(w, t);
	kp_writer_indent(w, 1);
	bool first_member = true;
	KpModifiers implicit = KM(KM_PUBLIC) | (t->kind == KK_INTERFACE ? KM(KM_ABSTRACT) : 0);
	for (int i = 0; i < t->properties.n; i++) {
		KpProperty *p = t->properties.v[i];
		if (kp_constructor_property(t, p->name))
			continue;
		if (!first_member)
			kp_emit(w, "\n");
		kp_property_emit(p, w, implicit, true, false);
		first_member = false;
	}
	for (int i = 0; i < t->funs.n; i++) {
		if (!first_member)
			kp_emit(w, "\n");
		kp_fun_emit(t->funs.v[i], w, t->name, implicit);
		first_member = false;
	}
	for (int i = 0; i < t->types.n; i++) {
		if (!first_member)
			kp_emit(w, "\n");
		kp_type_spec_emit(t->types.v[i], w);
		first_member = false;
	}
	kp_writer_unindent(w, 1);
	kp_writer_pop_type(w);
	kp_emit(w, "}");
	kp_emit(w, "\n");
	kp_writer_set_statement_line(w, previous_statement_line);
}

/* ---- FileSpec ---- */

KpFile *kp_file(const char *package, const char *name)
{
	KpFile *f = xalloc(sizeof *f);
	f->package = package, f->name = name;
	f->comment = kp_code();
	return f;
}

void kp_file_add_comment(KpFile *f, const char *format) { kp_add0(f->comment, format); }

void kp_file_add_annotation(KpFile *f, KpAnnotation *a)
{
	if (a->use_site_target && !str_eq(a->use_site_target, "file"))
		kp_illegal_state(xfmt("Use-site target %s not supported for file annotations.", a->use_site_target));
	KpAnnotation *c = xalloc(sizeof *c);
	*c = *a;
	c->use_site_target = "file";
	vec_push(&f->annotations, c);
}

/* Import.toString() */
static const char *kp_import_string(const char *qualified, const char *alias)
{
	const char *s = kp_escape_segments_if_necessary(qualified);
	return alias ? xcat(s, " as ", kp_escape(alias)) : s;
}

void kp_file_add_import(KpFile *f, const char *package, const char *name)
{
	if (str_eq(name, "*"))
		kp_illegal_argument("Wildcard imports are not allowed");
	const char *q = package[0] ? xcat(package, ".", name) : name;
	/* memberImports: a sorted set of Import (compared by their strings) */
	const char *s = kp_import_string(q, NULL);
	int i = 0;
	for (; i < f->member_imports.n; i++) {
		int c = java_strcmp(kp_import_string(f->member_imports.v[i], NULL), s);
		if (c == 0)
			return;
		if (c > 0)
			break;
	}
	vec_push(&f->member_imports, NULL);
	memmove(&f->member_imports.v[i + 1], &f->member_imports.v[i], sizeof(void *) * (size_t)(f->member_imports.n - 1 - i));
	f->member_imports.v[i] = (void *)q;
}

void kp_file_add_type(KpFile *f, KpTypeSpec *t) { vec_push(&f->members, t); }

static int kp_cmp_java(const void *a, const void *b) { return java_strcmp(a, b); }

/* the strings of imports, sorted and without duplicates (toSortedSet) */
static Vec kp_sorted_set(Vec v)
{
	java_sort(v.v, v.n, kp_cmp_java);
	Vec r = {0};
	for (int i = 0; i < v.n; i++)
		if (!r.n || !str_eq(r.v[r.n - 1], v.v[i]))
			vec_push(&r, v.v[i]);
	return r;
}

static void kp_file_emit(KpWriter *w, void *arg)
{
	KpFile *f = arg;
	if (!kp_code_is_empty(f->comment))
		kp_emit_comment(w, f->comment);
	if (f->annotations.n) {
		kp_emit_annotations(w, &f->annotations, false);
		kp_emit(w, "\n");
	}
	kp_writer_push_package(w, f->package);
	const char *escaped = kp_escape_segments_if_necessary(f->package);
	if (escaped[0]) {
		kp_emit_codef(w, "package %L\n", 1, (KpArg[]){kp_arg_s(escaped)});
		kp_emit(w, "\n");
	}
	const char **names, **aliases;
	int n = kp_writer_imports(w, &names, &aliases);
	Vec plain = {0}, aliased = {0};
	for (int i = 0; i < n; i++)
		vec_push(aliases[i] ? &aliased : &plain, (void *)kp_import_string(names[i], aliases[i]));
	plain = kp_sorted_set(plain);
	aliased = kp_sorted_set(aliased);
	for (int i = 0; i < aliased.n; i++)
		vec_push(&plain, aliased.v[i]);
	if (plain.n) {
		for (int i = 0; i < plain.n; i++) {
			kp_emit_codef(w, "import %L", 1, (KpArg[]){kp_arg_s(plain.v[i])});
			kp_emit(w, "\n");
		}
		kp_emit(w, "\n");
	}
	for (int i = 0; i < f->members.n; i++) {
		if (i > 0)
			kp_emit(w, "\n");
		kp_type_spec_emit(f->members.v[i], w);
	}
	kp_writer_pop_package(w);
}

const char *kp_file_to_string(KpFile *f)
{
	Buf out = {0};
	KpWriter *w = kp_writer_with_collected_imports(&out, &f->member_imports, kp_file_emit, f);
	kp_file_emit(w, f);
	kp_writer_close(w);
	return xstrdup(buf_str(&out));
}
