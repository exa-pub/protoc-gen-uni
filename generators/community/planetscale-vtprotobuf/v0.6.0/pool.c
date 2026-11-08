/* Copyright (c) 2021 PlanetScale Inc. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Port of features/pool/pool.go (vtprotobuf v0.6.0). */

typedef struct {
	VtFile *f;
	PgGeneratedFile *g;
	bool once;
} Pool;

static void pool_message(Pool *p, const PgMessage *message)
{
	for (int i = 0; i < message->nMessages; i++)
		pool_message(p, message->Messages[i]);

	if (message->Desc->is_map_entry || !vt_should_pool(p->f, message))
		return;

	p->once = true;
	PgGoIdent cc_type_name = message->GoIdent;

	const char *sync_pool = vt_ident(p->f, "sync", "Pool");
	P(p->g, "var vtprotoPool_", cc_type_name, " = ", sync_pool, "{");
	P(p->g, "New: func() interface{} {");
	P(p->g, "return &", cc_type_name, "{}");
	P(p->g, "},");
	P(p->g, "}");

	P(p->g, "func (m *", cc_type_name, ") ResetVT() {");
	P(p->g, "if m != nil {");
	Vec saved = {0};
	for (int i = 0; i < message->nFields; i++) {
		const PgField *field = message->Fields[i];
		const char *fieldname = field->GoName;

		if (go_field_is_list(field->Desc)) {
			switch (field->Desc->kind) {
			case PT_MESSAGE:
			case PT_GROUP:
				P(p->g, "for _, mm := range m.", fieldname, "{");
				if (vt_should_pool(p->f, field->Message))
					P(p->g, "mm.ResetVT()");
				else
					P(p->g, "mm.Reset()");
				P(p->g, "}");
			}
			P(p->g, xfmt("f%d", saved.n), " := m.", fieldname, "[:0]");
			vec_push(&saved, (void *)field);
		} else if (is_real_oneof(field)) {
			if (vt_should_pool(p->f, field->Message)) {
				P(p->g, "if oneof, ok := m.", field->Oneof->GoName, ".(*", field->GoIdent, "); ok {");
				P(p->g, "oneof.", fieldname, ".ReturnToVTPool()");
				P(p->g, "}");
			}
		} else {
			switch (field->Desc->kind) {
			case PT_MESSAGE:
			case PT_GROUP:
				if (!go_field_is_map(field->Desc) && vt_should_pool(p->f, field->Message))
					P(p->g, "m.", fieldname, ".ReturnToVTPool()");
				break;
			case PT_BYTES:
				P(p->g, xfmt("f%d", saved.n), " := m.", fieldname, "[:0]");
				vec_push(&saved, (void *)field);
				break;
			}
		}
	}

	P(p->g, "m.Reset()");
	for (int i = 0; i < saved.n; i++)
		P(p->g, "m.", ((const PgField *)saved.v[i])->GoName, " = ", xfmt("f%d", i));
	P(p->g, "}");
	P(p->g, "}");

	P(p->g, "func (m *", cc_type_name, ") ReturnToVTPool() {");
	P(p->g, "if m != nil {");
	P(p->g, "m.ResetVT()");
	P(p->g, "vtprotoPool_", cc_type_name, ".Put(m)");
	P(p->g, "}");
	P(p->g, "}");

	P(p->g, "func ", cc_type_name, "FromVTPool() *", cc_type_name, "{");
	P(p->g, "return vtprotoPool_", cc_type_name, ".Get().(*", cc_type_name, ")");
	P(p->g, "}");
}

static bool pool_generate_file(VtFile *gen, PgFile *file)
{
	Pool p = {gen, gen->g, false};
	for (int i = 0; i < file->nMessages; i++)
		pool_message(&p, file->Messages[i]);
	return p.once;
}
