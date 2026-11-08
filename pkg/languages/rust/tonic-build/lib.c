/* Port of tonic-build 0.14.2 src/lib.rs and src/code_gen.rs (https://github.com/hyperium/tonic, MIT,
 * Copyright (c) 2025 Lucio Franco): Attributes, the name formatting, doc comments, match_name and
 * naive_snake_case; CodeGenBuilder with the defaults. */

static void push_pair(Vec *v, const char *pattern, const char *attr)
{
	vec_push(v, (void *)pattern);
	vec_push(v, (void *)attr);
}

void tonic_attributes_push_mod(TonicAttributes *a, const char *pattern, const char *attr) { push_pair(&a->module, pattern, attr); }
void tonic_attributes_push_struct(TonicAttributes *a, const char *pattern, const char *attr) { push_pair(&a->structure, pattern, attr); }
void tonic_attributes_push_trait(TonicAttributes *a, const char *pattern, const char *attr) { push_pair(&a->trait_attributes, pattern, attr); }

TonicCodeGenBuilder tonic_code_gen_builder(void)
{
	return (TonicCodeGenBuilder){.emit_package = true, .build_transport = true};
}

/* quote::format_ident!: Ident::new, or Ident::new_raw for "r#..." (proc-macro2 validate_ident). */
static const char *format_ident(const char *s)
{
	const char *id = has_prefix(s, "r#") ? s + 2 : s;
	if (!*id)
		fail("Ident is not allowed to be empty; use Option<Ident>");
	if (strspn(id, "0123456789") == strlen(id))
		fail("Ident cannot be a number; use Literal instead");
	bool ok = *id == '_' || ((*id | 0x20) >= 'a' && (*id | 0x20) <= 'z');
	for (const char *c = id + 1; *c && ok; c++)
		ok = *c == '_' || ((*c | 0x20) >= 'a' && (*c | 0x20) <= 'z') || (*c >= '0' && *c <= '9');
	if (!ok) {
		Buf b = {0};
		rust_str_debug(&b, str_c(id));
		fail("%s is not a valid Ident", buf_str(&b));
	}
	if (id != s && (str_eq(id, "_") || str_eq(id, "super") || str_eq(id, "self") || str_eq(id, "Self") ||
			str_eq(id, "crate")))
		fail("`r#%s` cannot be a raw identifier", id);
	return s;
}

static char *format_service_name(const TonicService *s, bool emit_package)
{
	const char *package = emit_package ? s->package : "";
	return xcat(package, *package ? "." : "", s->identifier);
}

static char *format_method_path(const TonicService *s, const TonicMethod *m, bool emit_package)
{
	return xcat("/", format_service_name(s, emit_package), "/", m->identifier);
}

/* The '.'-separated parts of s. */
static int segments(const char *s, Str **out)
{
	return str_split_s(str_c(s), '.', out);
}

static bool segments_eq(const Str *a, const Str *b, int n)
{
	for (int i = 0; i < n; i++)
		if (!str_equal(a[i], b[i]))
			return false;
	return true;
}

bool tonic_match_name(const char *pattern, const char *path)
{
	if (!*pattern)
		return false;
	if (str_eq(pattern, ".") || str_eq(pattern, path))
		return true;
	Str *ps, *qs;
	int np = segments(pattern, &ps), nq = segments(path, &qs);
	if ((unsigned char)pattern[0] >= 0x80) /* &pattern[..1] */
		rust_str_slice_end_fail(str_c(pattern), 1);
	if (np > nq)
		return false;
	if (pattern[0] == '.') /* prefix match */
		return segments_eq(ps, qs, np);
	return segments_eq(ps, qs + nq - np, np); /* suffix match */
}

/* generate_attributes: the attributes whose pattern matches name. */
static Vec attributes_for(const Vec *pairs, const char *name)
{
	Vec attrs = {0};
	for (int i = 0; i + 1 < pairs->n; i += 2)
		if (tonic_match_name(pairs->v[i], name))
			/* attributes cannot be parsed directly, so we pretend they're on a struct */
			syn_parse_attrs_of_fake_struct(str_c(pairs->v[i + 1]), &attrs);
	return attrs;
}

static void push_attrs(PpNode *node, const Vec *attrs)
{
	for (int i = 0; i < attrs->n; i++)
		pp_push_attr(node, attrs->v[i]);
}

/* Generate a singular line of a doc comment */
static PpNode *generate_doc_comment(Str comment)
{
	if (!(comment.n && comment.p[0] == ' ')) {
		Buf b = {0};
		buf_addc(&b, ' ');
		buf_add_str(&b, comment);
		comment = buf_view(&b);
	}
	return pp_attr_name_value(pp_path("doc"), pp_e_lit(pp_lit_str_repr(pm2_string(comment), comment)), false);
}

/* Generate a larger doc comment composed of many lines of doc comments */
static void push_doc_comments(PpNode *node, const Str *comments, int n)
{
	for (int i = 0; i < n; i++)
		pp_push_attr(node, generate_doc_comment(comments[i]));
}

char *tonic_naive_snake_case(const char *name)
{
	Buf b = {0};
	for (const char *c = name; *c; c++) {
		buf_addc(&b, *c >= 'A' && *c <= 'Z' ? (char)(*c | 0x20) : *c);
		if (c[1] >= 'A' && c[1] <= 'Z')
			buf_addc(&b, '_');
	}
	return xstrdup(buf_str(&b));
}
