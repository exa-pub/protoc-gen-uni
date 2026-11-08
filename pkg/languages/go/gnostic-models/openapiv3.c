/* Copyright 2020 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (see LICENSE of github.com/google/gnostic-models). */

/* Port of github.com/google/gnostic-models v0.7.0 openapiv3/OpenAPIv3.pb.go (the messages),
 * openapiv3/OpenAPIv3.go (ToRawInfo), openapiv3/document.go (YAMLValue) and compiler/helpers.go
 * (the yaml.Node constructors). The ToRawInfo methods are generated code: openapiv3_tables.inc
 * describes each one as a list of operations (mkopenapiv3.py), interpreted by oa_to_raw_info. */
#include "openapiv3.h"

#include "go_std.h"

#include <string.h>

typedef struct {
	uint8_t num, type, label, oneof;
	int8_t msg; /* OA_* of a message field, -1 */
} OaField;

/* The statement shapes of the generated ToRawInfo methods. */
enum {
	OP_REQ_STR,  /* always include this required field: key, NewScalarNodeForString(m.F) */
	OP_REQ_MSG,  /* always include this required field: key, m.F.ToRawInfo() */
	OP_STR,      /* if m.F != "" */
	OP_BOOL,     /* if m.F != false */
	OP_FLOAT,    /* if m.F != 0.0 */
	OP_INT,      /* if m.F != 0 */
	OP_MSG,      /* if m.F != nil: key, m.F.ToRawInfo() */
	OP_REP_MSG,  /* if len(m.F) != 0: key, a sequence of item.ToRawInfo() */
	OP_REP_STR,  /* if len(m.F) != 0: key, NewSequenceNodeForStringArray(m.F) */
	OP_INLINE,   /* for _, item := range m.F: item.Name, item.Value.ToRawInfo() */
	OP_ITEMS,    /* Schema.items: the sequence of m.F.SchemaOrReference, or its only element */
	OP_ONEOF_MSG,    /* one of wrapper: if v := m.GetF(); v != nil { return v.ToRawInfo() } */
	OP_ONEOF_FLOAT,  /* one of wrapper: the scalar member if set */
	OP_ONEOF_BOOL,
	OP_ONEOF_STRING,
};

typedef struct {
	uint8_t op, field;
	const char *key;
} OaOp;

enum { K_MAPPING, K_ONEOF, K_ANY, K_STRINGARRAY };

typedef struct {
	const char *name;
	const char *field_names; /* NUL-separated, in field order */
	const OaField *f;
	uint8_t nf;
	const OaOp *ops;
	uint8_t nops;
	uint8_t kind;
} OaMsg;

#include "openapiv3_tables.inc"

static GoMsgDef *oa_defs;

COLD void oa_init(void)
{
	oa_defs = xalloc(sizeof *oa_defs * OA_COUNT);
	for (int i = 0; i < OA_COUNT; i++) {
		const OaMsg *d = &oa_msgs[i];
		GoMsgDef *def = &oa_defs[i];
		int n = d->nf;
		PbField *f = xalloc(sizeof *f * (size_t)(n ? n : 1));
		uint8_t *flags = xalloc((size_t)(n ? n : 1));
		int *oneof = xalloc(sizeof *oneof * (size_t)(n ? n : 1));
		const char **names = xalloc(sizeof *names * (size_t)(n ? n : 1));
		int *order = xalloc(sizeof *order * (size_t)(n ? n : 1));
		const char *pkg = i == OA_GoogleProtobufAny ? "" : "openapi.v3.";
		const char *fname = d->field_names;
		for (int k = 0; k < n; k++) {
			const OaField *of = &d->f[k];
			f[k] = (PbField){of->num, of->type, of->label, of->msg >= 0 ? &oa_defs[of->msg].pb : NULL, NULL, 0};
			if (of->type == PT_STRING)
				flags[k] |= GOF_UTF8; /* proto3 */
			if (of->label == PL_OPT && !of->oneof && of->type != PT_MESSAGE)
				flags[k] |= GOF_IMPLICIT;
			oneof[k] = of->oneof;
			names[k] = xcat(pkg, d->name, ".", fname);
			fname += strlen(fname) + 1;
			order[k] = k;
		}
		def->pb = (PbMsgDef){n, f};
		def->full_name = xcat(pkg, d->name);
		def->flags = flags;
		def->oneof = oneof;
		def->names = names;
		def->order = order;
	}
}

const GoMsgDef *oa_def(int msg) { return &oa_defs[msg]; }

PbMsg *oa_new(int msg) { return pb_new(&oa_defs[msg].pb); }

static int field_index(const PbMsg *m, uint32_t num)
{
	const PbMsgDef *def = m->def;
	int lo = 0, hi = def->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (def->f[mid].num < num)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < def->n && def->f[lo].num == num)
		return lo;
	fail("openapiv3: no field %u in %s", num, ((const GoMsgDef *)def)->full_name);
}

static void slot_push(PbSlot *s, PbVal v)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 2;
		s->v = xrealloc(s->v, sizeof *s->v * (size_t)s->cap, sizeof *s->v * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = v;
}

/* Setting a singular field: the other members of its oneof are cleared. */
static PbSlot *set_slot(PbMsg *m, uint32_t num)
{
	int fi = field_index(m, num);
	const GoMsgDef *def = (const GoMsgDef *)m->def;
	if (def->oneof[fi])
		for (int i = 0; i < def->pb.n; i++)
			if (i != fi && def->oneof[i] == def->oneof[fi])
				m->slots[i].n = 0;
	PbSlot *s = &m->slots[fi];
	s->n = 0;
	return s;
}

void oa_set_str(PbMsg *m, uint32_t num, Str v)
{
	PbSlot *s = set_slot(m, num);
	slot_push(s, (PbVal){.s = {xstrndup(v.p ? v.p : "", v.n), v.n}});
}

void oa_set_cstr(PbMsg *m, uint32_t num, const char *v) { oa_set_str(m, num, str_c(v)); }

void oa_set_bool(PbMsg *m, uint32_t num, bool v) { slot_push(set_slot(m, num), (PbVal){.u = v}); }

void oa_set_double(PbMsg *m, uint32_t num, double v)
{
	PbVal x;
	memcpy(&x.u, &v, sizeof v);
	slot_push(set_slot(m, num), x);
}

void oa_set_int(PbMsg *m, uint32_t num, int64_t v) { slot_push(set_slot(m, num), (PbVal){.u = (uint64_t)v}); }

void oa_set_msg(PbMsg *m, uint32_t num, PbMsg *sub)
{
	PbSlot *s = set_slot(m, num);
	if (sub)
		slot_push(s, (PbVal){.m = sub});
}

void oa_add_msg(PbMsg *m, uint32_t num, PbMsg *sub) { slot_push(&m->slots[field_index(m, num)], (PbVal){.m = sub}); }

void oa_add_str(PbMsg *m, uint32_t num, Str v)
{
	slot_push(&m->slots[field_index(m, num)], (PbVal){.s = {xstrndup(v.p ? v.p : "", v.n), v.n}});
}

void oa_clear(PbMsg *m, uint32_t num) { m->slots[field_index(m, num)].n = 0; }

static const PbVal *get(const PbMsg *m, uint32_t num)
{
	if (!m)
		return NULL;
	const PbSlot *s = &m->slots[field_index(m, num)];
	return s->n ? &s->v[s->n - 1] : NULL;
}

Str oa_str(const PbMsg *m, uint32_t num)
{
	const PbVal *v = get(m, num);
	return v ? v->s : (Str){"", 0};
}

bool oa_bool(const PbMsg *m, uint32_t num)
{
	const PbVal *v = get(m, num);
	return v && v->u;
}

PbMsg *oa_msg(const PbMsg *m, uint32_t num)
{
	const PbVal *v = get(m, num);
	return v ? v->m : NULL;
}

/* ---- proto.Merge ---- */

/* Whether proto.Merge copies a set slot: the generated messages merge implicit-presence scalars
 * only when they are not zero (mergeFloat64NoZero etc.: -0 is not copied either). */
static bool populated(const GoMsgDef *def, int fi, const PbSlot *s)
{
	if (!s->n)
		return false;
	if (!(def->flags[fi] & GOF_IMPLICIT))
		return true;
	const PbVal *v = &s->v[s->n - 1];
	switch (def->pb.f[fi].type) {
	case PT_STRING:
	case PT_BYTES:
		return v->s.n != 0;
	case PT_DOUBLE: {
		double d;
		memcpy(&d, &v->u, sizeof d);
		return d != 0;
	}
	default:
		return v->u != 0;
	}
}

void oa_merge(PbMsg *dst, const PbMsg *src)
{
	if (!src)
		return;
	const GoMsgDef *def = (const GoMsgDef *)src->def;
	for (int fi = 0; fi < def->pb.n; fi++) {
		const PbSlot *s = &src->slots[fi];
		if (!populated(def, fi, s))
			continue;
		const PbField *f = &def->pb.f[fi];
		PbSlot *d = &dst->slots[fi];
		if (f->label != PL_OPT) {
			for (int i = 0; i < s->n; i++) {
				PbVal v = s->v[i];
				if (f->type == PT_MESSAGE)
					v.m = gopb_clone(v.m);
				else if (f->type == PT_STRING || f->type == PT_BYTES)
					v.s = (Str){xstrndup(v.s.p, v.s.n), v.s.n};
				slot_push(d, v);
			}
		} else if (f->type == PT_MESSAGE) {
			/* dst.Mutable(fd): a new message unless this oneof member is already set */
			if (!d->n)
				slot_push(set_slot(dst, f->num), (PbVal){.m = pb_new(f->msg)});
			oa_merge(d->v[d->n - 1].m, s->v[s->n - 1].m);
		} else {
			PbVal v = s->v[s->n - 1];
			if (f->type == PT_STRING || f->type == PT_BYTES)
				v.s = (Str){xstrndup(v.s.p, v.s.n), v.s.n};
			slot_push(set_slot(dst, f->num), v);
		}
	}
	if (src->unknown.n)
		buf_add(&dst->unknown, src->unknown.p, src->unknown.n);
}

/* ---- compiler/helpers.go ---- */

YamlNode *compiler_new_null_node(void)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!null");
	return n;
}

YamlNode *compiler_new_mapping_node(void)
{
	YamlNode *n = yaml_node_new(YAML_MAPPING_NODE);
	n->content_set = true; /* make([]*yaml.Node, 0) */
	return n;
}

YamlNode *compiler_new_sequence_node(void)
{
	YamlNode *n = yaml_node_new(YAML_SEQUENCE_NODE);
	n->content_set = true;
	return n;
}

YamlNode *compiler_new_scalar_node_for_string(Str s)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!str");
	n->value = s;
	return n;
}

YamlNode *compiler_new_scalar_node_for_bool(bool b)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!bool");
	n->value = b ? STR_LIT("true") : STR_LIT("false");
	return n;
}

YamlNode *compiler_new_scalar_node_for_float(double f)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!float");
	/* fmt.Sprintf("%g", f): strconv's shortest 'g' form ("+Inf", "-Inf", "NaN" included) */
	n->value = str_c(go_std_format_float(f, 'g', -1, 64));
	return n;
}

YamlNode *compiler_new_scalar_node_for_int(int64_t i)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->tag = STR_LIT("!!int");
	n->value = str_c(xfmt("%lld", (long long)i));
	return n;
}

/* NewSequenceNodeForStringArray */
static YamlNode *new_sequence_node_for_string_array(const PbSlot *s)
{
	YamlNode *n = compiler_new_sequence_node();
	for (int i = 0; i < s->n; i++)
		yaml_node_append(n, compiler_new_scalar_node_for_string(s->v[i].s));
	return n;
}

/* ---- ToRawInfo ---- */

static NORETURN COLD void nil_deref(int msg)
{
	fail("unsupported by protoc-gen-uni: protoc-gen-openapi panics (nil pointer dereference in "
	     "gnostic-models openapi_v3.%s.ToRawInfo) on an openapi.v3 annotation without that value",
	     oa_msgs[msg].name);
}

static double val_double(PbVal v)
{
	double d;
	memcpy(&d, &v.u, sizeof d);
	return d;
}

YamlNode *oa_to_raw_info(const PbMsg *m, int msg)
{
	const OaMsg *d = &oa_msgs[msg];
	switch (d->kind) {
	case K_ANY: {
		if (!m)
			nil_deref(msg);
		YamlNode *node = yaml_node_new(0);
		const char *err = yaml_unmarshal(oa_str(m, OA_Any_yaml), node);
		if (err && has_prefix(err, "unsupported by protoc-gen-uni"))
			fail("%s", err);
		if (err == NULL) {
			if (node->kind == YAML_DOCUMENT_NODE)
				return node->content[0];
			return node;
		}
		return compiler_new_null_node();
	}
	case K_STRINGARRAY:
		if (!m)
			nil_deref(msg);
		return new_sequence_node_for_string_array(&m->slots[field_index(m, OA_StringArray_value)]);
	case K_ONEOF:
		for (int i = 0; m && i < d->nops; i++) {
			const OaOp *op = &d->ops[i];
			const PbSlot *s = &m->slots[op->field];
			if (!s->n)
				continue;
			PbVal v = s->v[s->n - 1];
			switch (op->op) {
			case OP_ONEOF_MSG:
				return oa_to_raw_info(v.m, d->f[op->field].msg);
			case OP_ONEOF_FLOAT:
				return compiler_new_scalar_node_for_float(val_double(v));
			case OP_ONEOF_BOOL:
				return compiler_new_scalar_node_for_bool(v.u != 0);
			case OP_ONEOF_STRING:
				return compiler_new_scalar_node_for_string(v.s);
			}
		}
		return compiler_new_null_node();
	}

	YamlNode *info = compiler_new_mapping_node();
	if (!m)
		return info;
	for (int i = 0; i < d->nops; i++) {
		const OaOp *op = &d->ops[i];
		const OaField *of = &d->f[op->field];
		const PbSlot *s = &m->slots[op->field];
		PbVal v = s->n ? s->v[s->n - 1] : (PbVal){0};
		Str key = op->key ? str_c(op->key) : (Str){0};
		YamlNode *value = NULL;
		switch (op->op) {
		case OP_REQ_STR:
			value = compiler_new_scalar_node_for_string(s->n ? v.s : (Str){"", 0});
			break;
		case OP_REQ_MSG:
			value = oa_to_raw_info(s->n ? v.m : NULL, of->msg);
			break;
		case OP_STR:
			if (s->n && v.s.n)
				value = compiler_new_scalar_node_for_string(v.s);
			break;
		case OP_BOOL:
			if (s->n && v.u)
				value = compiler_new_scalar_node_for_bool(true);
			break;
		case OP_FLOAT:
			if (s->n && val_double(v) != 0.0)
				value = compiler_new_scalar_node_for_float(val_double(v));
			break;
		case OP_INT:
			if (s->n && v.u)
				value = compiler_new_scalar_node_for_int((int64_t)v.u);
			break;
		case OP_MSG:
			if (s->n)
				value = oa_to_raw_info(v.m, of->msg);
			break;
		case OP_REP_MSG:
			if (s->n) {
				value = compiler_new_sequence_node();
				for (int k = 0; k < s->n; k++)
					yaml_node_append(value, oa_to_raw_info(s->v[k].m, of->msg));
			}
			break;
		case OP_REP_STR:
			if (s->n)
				value = new_sequence_node_for_string_array(s);
			break;
		case OP_INLINE: {
			/* Named<T>: name = 1, value = 2 */
			int item_msg = of->msg;
			for (int k = 0; k < s->n; k++) {
				const PbMsg *item = s->v[k].m;
				const OaMsg *im = &oa_msgs[item_msg];
				yaml_node_append(info, compiler_new_scalar_node_for_string(oa_str(item, 1)));
				yaml_node_append(info, oa_to_raw_info(oa_msg(item, 2), im->f[1].msg));
			}
			break;
		}
		case OP_ITEMS:
			if (s->n) {
				value = compiler_new_sequence_node();
				const PbMsg *items = v.m;
				const PbSlot *is = &items->slots[field_index(items, OA_ItemsItem_schema_or_reference)];
				for (int k = 0; k < is->n; k++)
					yaml_node_append(value, oa_to_raw_info(is->v[k].m, OA_SchemaOrReference));
				if (value->n_content == 1)
					value = value->content[0];
			}
			break;
		}
		if (value) {
			yaml_node_append(info, compiler_new_scalar_node_for_string(key));
			yaml_node_append(info, value);
		}
	}
	return info;
}

const char *oa_document_yaml_value(const PbMsg *d, Str comment, Buf *out)
{
	YamlNode *raw = oa_to_raw_info(d, OA_Document);
	YamlNode *doc = yaml_node_new(YAML_DOCUMENT_NODE);
	yaml_node_append(doc, raw);
	doc->head_comment = comment;
	return yaml_marshal(doc, out);
}
