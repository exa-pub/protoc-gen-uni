/* Internal declarations of the tonic-build 0.14.2 port (unity build tonic_build.c): the lib.rs
 * helpers that client.c and server.c share, and shorthands for the syn nodes that the quoted
 * tokens parse to. Everything is static. */
#ifndef TONIC_BUILD_INTERNAL_H
#define TONIC_BUILD_INTERNAL_H

#include "tonic_build.h"

#include "proc_macro2.h"
#include "rust_std.h"
#include "syn.h"

#include <stdarg.h>
#include <string.h>

/* ---- lib.rs ---- */

static const char *format_ident(const char *s);
static char *format_service_name(const TonicService *s, bool emit_package);
static char *format_method_path(const TonicService *s, const TonicMethod *m, bool emit_package);
static void push_attrs(PpNode *node, const Vec *attrs);
static Vec attributes_for(const Vec *pairs, const char *name);
static PpNode *generate_doc_comment(Str comment);
static void push_doc_comments(PpNode *node, const Str *comments, int n);

/* ---- quote shorthands (not upstream) ---- */

/* Literal::string(s) as an expression. */
static PpNode *lit_str(const char *s) { return pp_e_lit(pp_lit_str_repr(pm2_string(str_c(s)), str_c(s))); }
/* A doc comment of quoted source (`/// text`): #[doc = r"text"]. */
static PpNode *qdoc(const char *text) { return pp_attr_name_value(pp_path("doc"), lit_str(text), false); }

/* #[path(tokens)] */
static PpNode *attr_src(const char *path, const char *tokens, bool inner)
{
	PpNode *a = pp_attr_list(pp_path(path), PP_DELIM_PAREN, inner);
	pp_tt_push_src(a, tokens);
	return a;
}

static PpNode *attr_word(const char *path) { return pp_attr_path(pp_path(path), false); }

static PpNode *ty1(const char *path, PpNode *a) { return pp_ty_gen(path, a, NULL); }
static PpNode *ty2(const char *path, PpNode *a, PpNode *b) { return pp_ty_gen(path, a, b, NULL); }
static PpNode *path1(const char *path, PpNode *a) { return pp_path_args(pp_path(path), pp_angle(a, NULL)); }
static PpNode *bound(PpNode *path) { return pp_trait_bound(path, false); }
static PpNode *bound_s(const char *path) { return bound(pp_path(path)); }
static PpNode *static_lt(void) { return pp_lifetime("'static"); }

/* Name = ty, a generic argument. */
static PpNode *assoc(const char *name, PpNode *ty)
{
	PpNode *n = pp_node(PP_ASSOC_TYPE);
	n->text = name;
	n->b = ty;
	return n;
}

/* std::result::Result<ok, tonic::Status> */
static PpNode *result_status(PpNode *ok) { return ty2("std::result::Result", ok, pp_ty("tonic::Status")); }

static PpNode *generics1(PpNode *param)
{
	PpNode *g = pp_generics();
	pp_push(g, param);
	return g;
}

static PpNode *tparam(const char *ident, PpNode *bounds) { return pp_type_param(ident, bounds, NULL); }

static PpNode *where_add(PpNode *generics, PpNode *ty, PpNode *bounds)
{
	if (!generics->a)
		generics->a = pp_where();
	pp_push(generics->a, pp_pred_type(ty, bounds));
	return generics;
}

static PpNode *arg(const char *name, PpNode *ty) { return pp_p_type(pp_p_ident(name, false, false), ty); }
static PpNode *s_semi(PpNode *e) { return pp_s_expr(e, true); }
static PpNode *s_expr(PpNode *e) { return pp_s_expr(e, false); }
static PpNode *s_let(const char *name, bool mut, PpNode *init) { return pp_s_local(pp_p_ident(name, false, mut), init); }
static PpNode *self_f(const char *field) { return pp_e_field(pp_e("self"), field); }
static PpNode *call1(const char *f, PpNode *a) { return pp_e_call(pp_e(f), a, NULL); }

/* fn name(<receiver>, args...) -> ret block; receiver: 0 none, 1 &self, 2 &mut self, 3 mut self. */
static PpNode *sig_recv(const char *name, int receiver, PpNode *ret)
{
	PpNode *sig = pp_sig(name, ret);
	if (receiver)
		pp_push(sig, pp_receiver(receiver != 3, receiver >= 2, NULL));
	return sig;
}

/* T<A> as a type path with the type parameter T. */
static PpNode *ty_of(const char *ident, const char *param) { return ty1(ident, pp_ty(param)); }

/* Self { field } */
static PpNode *self_shorthand(const char *field)
{
	PpNode *e = pp_e_struct(pp_path("Self"), NULL);
	pp_push(e, pp_field_value(field, NULL));
	return e;
}

/* /// docs... #[must_use] pub fn name(mut self, arg: ty) -> Self { stmt; self } (docs: NULL-terminated) */
static PpNode *builder_fn(const char *name, const char *arg_name, PpNode *arg_ty, PpNode *stmt, ...)
{
	PpNode *sig = sig_recv(name, 3, pp_ty("Self"));
	pp_push(sig, arg(arg_name, arg_ty));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(stmt, s_expr(pp_e("self")), NULL));
	va_list ap;
	va_start(ap, stmt);
	for (const char *d; (d = va_arg(ap, const char *));)
		pp_push_attr(fn, qdoc(d));
	va_end(ap);
	pp_push_attr(fn, attr_word("must_use"));
	return fn;
}

/* Box::pin(e) */
static PpNode *box_pin(PpNode *e) { return call1("Box::pin", e); }

/* #codec_name::default() */
static PpNode *codec_default(const TonicMethod *m) { return pp_e_call(pp_e(xcat(m->codec_path, "::default")), NULL); }

#endif
