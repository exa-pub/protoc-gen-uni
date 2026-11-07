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

/* Ported from packages/protoc-gen-es/src/util.ts: TypeScript and JSON types of fields. Part of
 * generator.c (unity build). */

static bool is_map_key_number(int k)
{
	return k == PT_INT32 || k == PT_FIXED32 || k == PT_UINT32 || k == PT_SFIXED32 || k == PT_SINT32;
}

static void message_gen_type(GeneratedFile *f, const DescMessage *m, const Options *o)
{
	gf_sym(f, f->rt->gen_message);
	gf_s(f, "<");
	gf_sym(f, gf_import_shape(f, &m->h));
	if (o->json_types || has_valid_types(o)) {
		gf_s(f, ", {");
		if (o->json_types) {
			gf_s(f, "jsonType: ");
			gf_sym(f, gf_import_json(f, &m->h));
		}
		if (has_valid_types(o)) {
			if (o->json_types)
				gf_s(f, ", ");
			gf_s(f, "validType: ");
			gf_sym(f, gf_import_valid(f, &m->h));
		}
		gf_s(f, "}");
	}
	gf_s(f, ">");
}

static void message_field_ts_type(GeneratedFile *f, const DescField *field, bool valid)
{
	const DescMessage *m = field->message;
	if (is_wrapper_desc(m) && !field->oneof && field->field_kind == FK_MESSAGE) {
		gf_s(f, scalar_ts_type(((DescField *)m->fields.v[0])->scalar, false));
		return;
	}
	if (str_eq(m->type_name, "google.protobuf.Struct") &&
	    (!field->parent || !str_eq(field->parent->type_name, "google.protobuf.Value"))) {
		gf_sym(f, f->rt->json_object);
		return;
	}
	if (valid && !wkt_public_import_path(pb_str(m->file->h.proto, FDP_NAME))) {
		gf_sym(f, gf_import_valid(f, &m->h));
		return;
	}
	gf_sym(f, gf_import_shape(f, &m->h));
}

static bool field_optional(const DescField *field)
{
	switch (field->field_kind) {
	case FK_SCALAR: case FK_ENUM: return pb_int(field->h.proto, FD_PROTO3_OPTIONAL, 0) != 0;
	case FK_MESSAGE: return true;
	}
	return false;
}

static void field_ts_type(GeneratedFile *f, const DescField *field, bool valid)
{
	switch (field->field_kind) {
	case FK_SCALAR:
		gf_s(f, scalar_ts_type(field->scalar, field->long_as_string));
		break;
	case FK_MESSAGE:
		message_field_ts_type(f, field, valid);
		break;
	case FK_ENUM:
		gf_sym(f, gf_import_shape(f, &field->enm->h));
		break;
	case FK_LIST:
		if (field->list_kind == FK_ENUM)
			gf_sym(f, gf_import_shape(f, &field->enm->h));
		else if (field->list_kind == FK_SCALAR)
			gf_s(f, scalar_ts_type(field->scalar, field->long_as_string));
		else
			message_field_ts_type(f, field, valid);
		gf_s(f, "[]");
		break;
	case FK_MAP:
		gf_s(f, "{ [key: ");
		gf_s(f, is_map_key_number(field->map_key) ? "number" : "string");
		gf_s(f, "]: ");
		if (field->map_kind == FK_SCALAR)
			gf_s(f, scalar_ts_type(field->scalar, false));
		else if (field->map_kind == FK_MESSAGE)
			message_field_ts_type(f, field, valid);
		else
			gf_sym(f, gf_import_shape(f, &field->enm->h));
		gf_s(f, " }");
		break;
	}
}

static void field_json_type(GeneratedFile *f, const DescField *field)
{
	switch (field->field_kind) {
	case FK_SCALAR:
		gf_s(f, scalar_json_type(field->scalar));
		break;
	case FK_MESSAGE:
		gf_sym(f, gf_import_json(f, &field->message->h));
		break;
	case FK_ENUM:
		gf_sym(f, gf_import_json(f, &field->enm->h));
		break;
	case FK_LIST:
		if (field->list_kind == FK_ENUM) {
			gf_sym(f, gf_import_json(f, &field->enm->h));
		} else if (field->list_kind == FK_SCALAR) {
			const char *t = scalar_json_type(field->scalar);
			gf_s(f, strchr(t, '|') ? xcat("(", t, ")") : t);
		} else {
			gf_sym(f, gf_import_json(f, &field->message->h));
		}
		gf_s(f, "[]");
		break;
	case FK_MAP:
		gf_s(f, "{ [key: ");
		gf_s(f, is_map_key_number(field->map_key) ? "number" : "string");
		gf_s(f, "]: ");
		if (field->map_kind == FK_SCALAR)
			gf_s(f, scalar_json_type(field->scalar));
		else if (field->map_kind == FK_MESSAGE)
			gf_sym(f, gf_import_json(f, &field->message->h));
		else
			gf_sym(f, gf_import_json(f, &field->enm->h));
		gf_s(f, " }");
		break;
	}
}
