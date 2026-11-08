/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protoc-gen-es/src/valid-types.ts: which fields are required in the
 * generated "Valid" types, from protovalidate rules (buf.validate.field, an unknown field of
 * FieldOptions) or proto2 required fields. Part of generator.c (unity build). */

/* The parts of buf.validate.FieldRules that matter: required (25), ignore (27), and the rules of
 * repeated items (18 → items 4) and map values (19 → values 5). */
typedef struct Rules Rules;
struct Rules {
	bool required;
	int32_t ignore;
	const char *type_case; /* "", "repeated", "map" */
	Rules *child;
};

enum { IGNORE_ALWAYS = 3 };

COLD static void rules_merge(Rules *r, const uint8_t *p, size_t n);

/* Upstream reads the option with getExtension: a known field is read by its type whatever the
 * wire type, and malformed data throws. Both are not supported here. */
NORETURN COLD static void rules_unsupported(void)
{
	fail("unsupported by protoc-gen-uni: malformed option (buf.validate.field)");
}

/* The next field of a FieldRules, RepeatedRules or MapRules. */
COLD static bool rules_next(PbReader *rd, PbRawField *f)
{
	if (rd->p >= rd->end)
		return false;
	if (!pb_next_field(rd, f))
		rules_unsupported();
	return true;
}

COLD static void rules_merge_child(Rules *r, const uint8_t *p, size_t n, uint32_t child_num)
{
	PbReader rd = {p, p + n};
	PbRawField f;
	while (rules_next(&rd, &f)) {
		if (f.num != child_num)
			continue;
		if (f.wt != WT_LEN)
			rules_unsupported();
		if (!r->child)
			r->child = xalloc(sizeof(Rules)), r->child->type_case = "";
		rules_merge(r->child, f.val, f.vlen);
	}
}

/* Merges serialized FieldRules into r, like the protobuf runtime merges a repeated field. */
static void rules_merge(Rules *r, const uint8_t *p, size_t n)
{
	PbReader rd = {p, p + n};
	PbRawField f;
	while (rules_next(&rd, &f)) {
		if ((f.num == 25 || f.num == 27) && f.wt != WT_VARINT)
			rules_unsupported();
		if ((f.num == 18 || f.num == 19) && f.wt != WT_LEN)
			rules_unsupported();
		if (f.num == 25) {
			r->required = f.v != 0;
		} else if (f.num == 27) {
			int32_t v = (int32_t)f.v;
			if (v == 0 || v == 1 || v == 3)
				r->ignore = v; /* closed enum: other values are unknown fields */
		} else if (f.num == 18 || f.num == 19) {
			const char *c = f.num == 18 ? "repeated" : "map";
			if (!str_eq(r->type_case, c)) {
				r->type_case = c;
				r->child = NULL;
			}
			rules_merge_child(r, f.val, f.vlen, f.num == 18 ? 4 : 5);
		}
	}
}

/* The merged buf.validate.field (1159) options of the field; *has: the option is present. */
COLD static Rules *field_rules(const DescField *f, bool *has)
{
	Rules *r = xalloc(sizeof *r);
	r->type_case = "";
	*has = false;
	PbReader rd = pb_unknown_fields(pb_msg(f->h.proto, FD_OPTIONS));
	PbRawField uf;
	while (pb_next_field(&rd, &uf)) {
		if (uf.num != 1159)
			continue;
		*has = true;
		if (uf.wt != WT_LEN)
			rules_unsupported();
		rules_merge(r, uf.val, uf.vlen);
	}
	return r;
}

static bool is_protovalidate_disabled(const DescField *f)
{
	if (!f->message)
		return false;
	bool has;
	Rules *r = field_rules(f, &has);
	if (r->ignore == IGNORE_ALWAYS)
		return true;
	Rules *child = NULL;
	if (f->field_kind == FK_LIST && str_eq(r->type_case, "repeated"))
		child = r->child;
	else if (f->field_kind == FK_MAP && str_eq(r->type_case, "map"))
		child = r->child;
	return child && child->ignore == IGNORE_ALWAYS;
}

static bool is_protovalidate_required(const DescField *f)
{
	bool has;
	Rules *r = field_rules(f, &has);
	if (!has || r->ignore == IGNORE_ALWAYS)
		return false;
	return r->required;
}

static bool is_legacy_required(const DescField *f)
{
	return f->field_kind == FK_MESSAGE && f->presence == PRESENCE_LEGACY_REQUIRED;
}

static bool uses_required(const DescMessage *m, Map *seen, bool protovalidate)
{
	map_put(seen, m->type_name, (void *)1);
	for (int i = 0; i < m->fields.n; i++) {
		const DescField *f = m->fields.v[i];
		if (protovalidate) {
			if (is_protovalidate_disabled(f))
				continue;
			if (is_protovalidate_required(f))
				return true;
		} else if (is_legacy_required(f)) {
			return true;
		}
		if (f->message && !map_has(seen, f->message->type_name) && uses_required(f->message, seen, protovalidate))
			return true;
	}
	return false;
}

static bool needs_custom_valid_type(const DescMessage *m, const Options *o)
{
	Map s1 = {0}, s2 = {0};
	return (o->protovalidate_required && uses_required(m, &s1, true)) ||
	       (o->legacy_required && uses_required(m, &s2, false));
}
