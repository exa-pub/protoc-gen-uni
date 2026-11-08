/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic). */

/* Port of cmd/protoc-gen-openapi/generator/utils.go (gnostic v0.7.1). contains and appendUnique
 * work on string lists (Vec of Str *). */

/* contains returns true if an array contains a specified string. */
static bool contains(const Vec *s, Str e)
{
	for (int i = 0; i < s->n; i++)
		if (str_equal(*(Str *)s->v[i], e))
			return true;
	return false;
}

static void vec_push_str(Vec *v, Str s)
{
	Str *p = xalloc(sizeof *p);
	*p = s;
	vec_push(v, p);
}

/* appendUnique appends a string, to a string slice, if the string is not already in the slice */
static void append_unique(Vec *s, Str e)
{
	if (!contains(s, e))
		vec_push_str(s, e);
}

/* singular produces the singular form of a collection name. */
static Str singular(Str plural)
{
	if (str_has_suffix(plural, STR_LIT("ves"))) {
		Buf b = {0};
		buf_add(&b, plural.p, plural.n - 3);
		buf_addc(&b, 'f');
		return buf_view(&b);
	}
	if (str_has_suffix(plural, STR_LIT("ies"))) {
		Buf b = {0};
		buf_add(&b, plural.p, plural.n - 3);
		buf_addc(&b, 'y');
		return buf_view(&b);
	}
	if (str_has_suffix(plural, STR_LIT("s")))
		return (Str){plural.p, plural.n - 1};
	return plural;
}

/* getValueField: message.Fields().ByName("value") */
static const GoField *get_value_field(const GoMessage *message)
{
	for (int i = 0; i < message->n_fields; i++)
		if (str_eq(message->fields[i]->d.name, "value"))
			return message->fields[i];
	return NULL;
}

static const char *get_value_kind(const GoMessage *message)
{
	const GoField *f = get_value_field(message);
	if (!f)
		nil_panic("getValueKind", message);
	return go_kind_string(f->kind);
}
