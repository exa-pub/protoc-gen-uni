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


/* Ported from packages/protoc-gen-es/src/protoc-gen-es-plugin.ts. Part of generator.c (unity
 * build). */

static const char *parse_bool_opt(Str v, bool *dst)
{
	if (str_eqc(v, "true") || str_eqc(v, "1"))
		*dst = true;
	else if (str_eqc(v, "false") || str_eqc(v, "0"))
		*dst = false;
	else
		return "please provide true or false";
	return NULL;
}

/* Returns NULL, or the reason of an invalid option ("" for none). */
static const char *parse_options(RawOption *raw, int n, void **out)
{
	Options *o = xalloc(sizeof *o);
	*out = o;
	for (int i = 0; i < n; i++) {
		const char *err = NULL;
		if (str_eqc(raw[i].key, "json_types")) {
			err = parse_bool_opt(raw[i].value, &o->json_types);
		} else if (str_eqc(raw[i].key, "valid_types")) {
			Str *parts;
			int np = str_split_s(raw[i].value, '+', &parts);
			for (int j = 0; j < np && !err; j++) {
				if (str_eqc(parts[j], "protovalidate_required"))
					o->protovalidate_required = true;
				else if (str_eqc(parts[j], "legacy_required"))
					o->legacy_required = true;
				else
					err = "";
			}
		} else if (str_eqc(raw[i].key, "erasable_syntax")) {
			err = parse_bool_opt(raw[i].value, &o->erasable_syntax);
		} else {
			err = "";
		}
		if (err)
			return err;
	}
	return NULL;
}

#define PURE "/*@__PURE__*/"

/* functionCall(fn, [fileDesc, ...pathInFileDesc(desc)]) */
static void desc_call(GeneratedFile *f, ImportSymbol *fn, ImportSymbol *file_desc, const Desc *d)
{
	gf_sym(f, fn);
	gf_s(f, "(");
	gf_sym(f, file_desc);
	Vec path = path_in_file_desc(d);
	for (int i = 0; i < path.n; i++)
		gf_sf(f, ", %d", (int)(intptr_t)path.v[i]);
	gf_s(f, ")");
}

static void generate_desc_doc(GeneratedFile *f, const Desc *d)
{
	Buf b = {0};
	buf_str(&b);
	if (d->kind == DK_FILE || d->kind == DK_ENUM) {
		buf_adds(&b, xcat("Describes the ", desc_string(d), "."));
	} else if (d->kind == DK_MESSAGE) {
		buf_adds(&b, xcat("Describes the ", desc_string(d), ".\nUse `create(",
				  gf_import_schema(f, d, false)->name, ")` to create a new message."));
	}
	if (desc_deprecated_with_parents(d))
		buf_adds(&b, b.n ? "\n@deprecated" : "@deprecated");
	gf_jsdoc_text(f, buf_view(&b), "");
	gf_nl(f);
}

static void file_desc_call(GeneratedFile *f, DescFile *file, Schema *s)
{
	/* Schema provides files with source retention options. Since we do not want to
	 * embed source retention options in generated code, we use FileDescriptorProto
	 * messages from CodeGeneratorRequest.proto_file instead. */
	PbMsg *runtime = map_get(&s->proto_files, pb_str(file->h.proto, FDP_NAME));
	if (!runtime)
		runtime = file->h.proto;
	if (embed_bootable(runtime) && !has_prefix(f->rt->create->from, "@bufbuild/protobuf")) {
		/* google/protobuf/descriptor.proto is embedded as a plain object when
		 * bootstrapping to avoid recursion */
		gf_sym(f, f->rt->boot);
		gf_s(f, "(");
		gf_s(f, embed_boot(runtime));
		gf_s(f, ")");
		return;
	}
	gf_sym(f, f->rt->file_desc);
	gf_s(f, "(");
	gf_lit(f, str_c(embed_base64(runtime)));
	if (file->deps.n) {
		gf_s(f, ", [");
		for (int i = 0; i < file->deps.n; i++) {
			if (i)
				gf_s(f, ", ");
			gf_sym(f, gf_import_schema(f, file->deps.v[i], false));
		}
		gf_s(f, "]");
	}
	gf_s(f, ")");
}

static void service_shape_expr(GeneratedFile *f, const DescService *s)
{
	gf_s(f, "{\n");
	for (int i = 0; i < s->methods.n; i++) {
		const DescMethod *m = s->methods.v[i];
		gf_jsdoc(f, &m->h, "  ");
		gf_s(f, "\n");
		gf_s(f, xcat("  ", m->local_name, ": {\n"));
		gf_s(f, "    methodKind: ");
		gf_lit(f, str_c(m->method_kind));
		gf_s(f, ";\n");
		gf_s(f, "    input: typeof ");
		gf_sym(f, gf_import_schema(f, &m->input->h, true));
		gf_s(f, ";\n");
		gf_s(f, "    output: typeof ");
		gf_sym(f, gf_import_schema(f, &m->output->h, true));
		gf_s(f, ";\n");
		gf_s(f, "  },\n");
	}
	gf_s(f, "}");
}

static void generate_enum_shape(GeneratedFile *f, const DescEnum *e)
{
	gf_jsdoc(f, &e->h, "");
	gf_nl(f);
	gf_export(f, "enum", gf_import_shape(f, &e->h)->name);
	gf_s(f, " {\n");
	for (int i = 0; i < e->values.n; i++) {
		const DescEnumValue *v = e->values.v[i];
		if (i > 0)
			gf_nl(f);
		gf_jsdoc(f, &v->h, "  ");
		gf_nl(f);
		gf_sf(f, "  %s = %d,\n", v->local_name, v->number);
	}
	gf_s(f, "}\n\n");
}

static void generate_obj_enum(GeneratedFile *f, const DescEnum *e, int target)
{
	const char *name = gf_import_shape(f, &e->h)->name;
	gf_jsdoc(f, &e->h, "");
	gf_nl(f);
	if (target == TARGET_TS) {
		gf_export(f, "const", name);
		gf_s(f, " = {\n");
	} else {
		gf_export(f, "declare const", name);
		gf_s(f, ": {\n");
	}
	for (int i = 0; i < e->values.n; i++) {
		const DescEnumValue *v = e->values.v[i];
		if (i > 0)
			gf_nl(f);
		gf_jsdoc(f, &v->h, "  ");
		gf_nl(f);
		gf_sf(f, "  %s%s: %d,\n", target == TARGET_TS ? "" : "readonly ", v->local_name, v->number);
	}
	gf_s(f, target == TARGET_TS ? "} as const;\n\n" : "};\n\n");
	/* generateObjEnumType */
	gf_jsdoc(f, &e->h, "");
	gf_nl(f);
	gf_export(f, target == TARGET_TS ? "type" : "declare type", name);
	gf_s(f, xcat(" = (typeof ", name, ")[keyof typeof ", name, "]"));
	if (e->open) {
		gf_s(f, " | ");
		gf_sym(f, f->rt->unknown_enum);
	}
	gf_s(f, ";\n\n");
}

static const char *type_decl(int target) { return target == TARGET_TS ? "type" : "declare type"; }

/* A Map key for a string that may contain NUL: NUL becomes \1\2, \1 becomes \1\1. */
static const char *set_key(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] == 0 || s.p[i] == 1)
			buf_addc(&b, 1);
		buf_addc(&b, s.p[i] == 0 ? 2 : s.p[i]);
	}
	return buf_str(&b);
}

static void generate_enum_json_shape(GeneratedFile *f, const DescEnum *e, int target)
{
	gf_jsdoc(f, &e->h, "");
	gf_nl(f);
	gf_export(f, type_decl(target), gf_import_json(f, &e->h)->name);
	gf_s(f, " = ");
	if (str_eq(e->type_name, "google.protobuf.NullValue")) {
		gf_s(f, "null");
	} else {
		/* The JSON type matches the output of toJson(), which emits the custom JSON
		 * name if one is set. We omit the Protobuf name from the type, even though
		 * fromJson() accepts it. */
		Map names = {0}; /* new Set<string>() */
		for (int i = 0, first = 1; i < e->values.n; i++) {
			const DescEnumValue *v = e->values.v[i];
			const char *key = set_key(v->json_name);
			if (map_has(&names, key))
				continue;
			map_put(&names, key, (void *)1);
			if (!first)
				gf_s(f, " | ");
			first = 0;
			gf_lit(f, v->json_name);
		}
	}
	gf_s(f, ";\n\n");
}

static void generate_member(GeneratedFile *f, const Desc *member, const Options *valid)
{
	if (member->kind == DK_ONEOF) {
		const DescOneof *o = (const DescOneof *)member;
		gf_jsdoc(f, member, "  ");
		gf_nl(f);
		gf_s(f, xcat("  ", o->local_name, ": {\n"));
		for (int i = 0; i < o->fields.n; i++) {
			const DescField *field = o->fields.v[i];
			if (i > 0)
				gf_s(f, "  } | {\n");
			gf_jsdoc(f, &field->h, "    ");
			gf_nl(f);
			gf_s(f, "    value: ");
			field_ts_type(f, field, valid && !is_protovalidate_disabled(field));
			gf_s(f, ";\n");
			gf_s(f, xcat("    case: \"", field->local_name, "\";\n"));
		}
		gf_s(f, "  } | { case: undefined; value?: undefined };\n");
		return;
	}
	const DescField *field = (const DescField *)member;
	gf_jsdoc(f, member, "  ");
	gf_nl(f);
	bool optional = field_optional(field);
	if (optional && valid) {
		bool required = (valid->legacy_required && is_legacy_required(field)) ||
				(valid->protovalidate_required && is_protovalidate_required(field));
		if (required)
			optional = false;
	}
	gf_s(f, xcat("  ", field->local_name, optional ? "?: " : ": "));
	field_ts_type(f, field, valid && !is_protovalidate_disabled(field));
	gf_s(f, optional ? " | undefined;\n" : ";\n");
}

static void generate_message_shape(GeneratedFile *f, const DescMessage *m, int target)
{
	gf_jsdoc(f, &m->h, "");
	gf_nl(f);
	gf_export(f, type_decl(target), gf_import_shape(f, &m->h)->name);
	gf_s(f, " = ");
	gf_sym(f, f->rt->message);
	gf_s(f, "<");
	gf_lit(f, str_c(m->type_name));
	gf_s(f, "> & {\n");
	for (int i = 0; i < m->members.n; i++) {
		generate_member(f, m->members.v[i], NULL);
		if (i < m->members.n - 1)
			gf_nl(f);
	}
	gf_s(f, "};\n\n");
}

static void generate_message_valid_shape(GeneratedFile *f, const DescMessage *m, const Options *o, int target)
{
	if (!needs_custom_valid_type(m, o)) {
		gf_export(f, type_decl(target), gf_import_valid(f, &m->h)->name);
		gf_s(f, " = ");
		gf_sym(f, gf_import_shape(f, &m->h));
		gf_s(f, ";\n\n");
		return;
	}
	gf_jsdoc(f, &m->h, "");
	gf_nl(f);
	gf_export(f, type_decl(target), gf_import_valid(f, &m->h)->name);
	gf_s(f, " = ");
	gf_sym(f, f->rt->message);
	gf_s(f, "<");
	gf_lit(f, str_c(m->type_name));
	gf_s(f, "> & {\n");
	for (int i = 0; i < m->members.n; i++) {
		generate_member(f, m->members.v[i], o);
		if (i < m->members.n - 1)
			gf_nl(f);
	}
	gf_s(f, "};\n\n");
}

/* The JSON name is not a valid identifier: /^[0-9]|[^a-zA-Z0-9_$]/ */
static bool needs_quotes(Str json_name)
{
	if (!json_name.n || (json_name.p[0] >= '0' && json_name.p[0] <= '9'))
		return true;
	for (size_t i = 0; i < json_name.n; i++) {
		char c = json_name.p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'))
			return true;
	}
	return false;
}

static void generate_message_json_shape(GeneratedFile *f, const DescMessage *m, int target)
{
	const char *tn = m->type_name;
	gf_jsdoc(f, &m->h, "");
	gf_nl(f);
	gf_export(f, type_decl(target), gf_import_json(f, &m->h)->name);
	if (str_eq(tn, "google.protobuf.Any")) {
		gf_s(f, " = {\n  \"@type\"?: string;\n};\n");
	} else if (str_eq(tn, "google.protobuf.Timestamp") || str_eq(tn, "google.protobuf.Duration") ||
		   str_eq(tn, "google.protobuf.FieldMask")) {
		gf_s(f, " = string;\n");
	} else if (str_eq(tn, "google.protobuf.Struct")) {
		gf_s(f, " = ");
		gf_sym(f, f->rt->json_object);
		gf_s(f, ";\n");
	} else if (str_eq(tn, "google.protobuf.Value")) {
		gf_s(f, " = ");
		gf_sym(f, f->rt->json_value);
		gf_s(f, ";\n");
	} else if (str_eq(tn, "google.protobuf.ListValue")) {
		gf_s(f, " = ");
		gf_sym(f, f->rt->json_value);
		gf_s(f, "[];\n");
	} else if (str_eq(tn, "google.protobuf.Empty")) {
		gf_s(f, " = Record<string, never>;\n");
	} else if (is_wrapper_desc(m)) {
		gf_s(f, " = ");
		field_json_type(f, m->fields.v[0]);
		gf_s(f, ";\n");
	} else {
		gf_s(f, " = {\n");
		for (int i = 0; i < m->fields.n; i++) {
			const DescField *field = m->fields.v[i];
			gf_jsdoc(f, &field->h, "  ");
			gf_nl(f);
			gf_s(f, "  ");
			if (needs_quotes(field->json_name))
				gf_lit(f, field->json_name);
			else
				gf_str(f, field->json_name);
			gf_s(f, "?: ");
			field_json_type(f, field);
			gf_s(f, ";\n");
			if (i < m->fields.n - 1)
				gf_nl(f);
		}
		gf_s(f, "};\n");
	}
	gf_nl(f);
}

static void generate_enum_decl(GeneratedFile *f, const DescEnum *e, const Options *o, int target)
{
	if (o->erasable_syntax)
		generate_obj_enum(f, e, target);
	else
		generate_enum_shape(f, e);
	if (o->json_types)
		generate_enum_json_shape(f, e, target);
	generate_desc_doc(f, &e->h);
}

static void generate_enum_gen_type(GeneratedFile *f, const DescEnum *e, const Options *o)
{
	ImportSymbol *shape = gf_import_shape(f, &e->h);
	gf_sym(f, f->rt->gen_enum);
	gf_s(f, "<");
	gf_sym(f, shape);
	if (o->json_types) {
		gf_s(f, ", ");
		gf_sym(f, gf_import_json(f, &e->h));
	}
	gf_s(f, ">");
}

static void extension_type(GeneratedFile *f, const DescField *x)
{
	gf_sym(f, f->rt->gen_extension);
	gf_s(f, "<");
	gf_sym(f, gf_import_shape(f, &x->extendee->h));
	gf_s(f, ", ");
	field_ts_type(f, x, false);
	gf_s(f, ">");
}

static void generate_target(Schema *s, int target)
{
	const Options *o = OPTS(s);
	const char *ext = target == TARGET_TS ? "_pb.ts" : target == TARGET_JS ? "_pb.js" : "_pb.d.ts";
	for (int fi = 0; fi < s->files.n; fi++) {
		DescFile *file = s->files.v[fi];
		GeneratedFile *f = gf_new(s, xcat(file->name, ext));
		gf_preamble(f, file);
		ImportSymbol *file_desc = gf_import_schema(f, &file->h, false);
		generate_desc_doc(f, &file->h);
		if (target == TARGET_DTS) {
			gf_export(f, "declare const", file_desc->name);
			gf_s(f, ": ");
			gf_sym(f, f->rt->gen_file);
			gf_s(f, ";\n\n");
		} else {
			gf_export(f, "const", file_desc->name);
			if (target == TARGET_TS) {
				gf_s(f, ": ");
				gf_sym(f, f->rt->gen_file);
			}
			gf_s(f, " = " PURE "\n  ");
			file_desc_call(f, file, s);
			gf_s(f, ";\n\n");
		}
		Vec types = nested_types(&file->h);
		for (int i = 0; i < types.n; i++) {
			Desc *d = types.v[i];
			switch (d->kind) {
			case DK_MESSAGE: {
				DescMessage *m = (DescMessage *)d;
				if (target == TARGET_JS) {
					const char *n = gf_import_schema(f, d, false)->name;
					generate_desc_doc(f, d);
					gf_export(f, "const", n);
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->message_desc, file_desc, d);
					gf_s(f, ";\n\n");
					break;
				}
				generate_message_shape(f, m, target);
				if (o->json_types)
					generate_message_json_shape(f, m, target);
				if (has_valid_types(o))
					generate_message_valid_shape(f, m, o, target);
				if (target == TARGET_TS) {
					generate_desc_doc(f, d);
					gf_export(f, "const", gf_import_schema(f, d, false)->name);
					gf_s(f, ": ");
					message_gen_type(f, m, o);
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->message_desc, file_desc, d);
					gf_s(f, ";\n\n");
				} else {
					const char *n = gf_import_schema(f, d, false)->name;
					generate_desc_doc(f, d);
					gf_export(f, "declare const", n);
					gf_s(f, ": ");
					message_gen_type(f, m, o);
					gf_s(f, ";\n\n");
				}
				break;
			}
			case DK_ENUM: {
				DescEnum *e = (DescEnum *)d;
				if (target == TARGET_JS) {
					generate_desc_doc(f, d);
					gf_export(f, "const", gf_import_schema(f, d, false)->name);
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->enum_desc, file_desc, d);
					gf_s(f, ";\n\n");
					gf_jsdoc(f, d, "");
					gf_nl(f);
					gf_export(f, "const", gf_import_shape(f, d)->name);
					gf_s(f, " = " PURE "\n  ");
					gf_sym(f, o->erasable_syntax ? f->rt->obj_enum : f->rt->ts_enum);
					gf_s(f, "(");
					gf_sym(f, gf_import_schema(f, d, false));
					gf_s(f, ");\n\n");
					break;
				}
				generate_enum_decl(f, e, o, target);
				gf_export(f, target == TARGET_TS ? "const" : "declare const", gf_import_schema(f, d, false)->name);
				gf_s(f, ": ");
				generate_enum_gen_type(f, e, o);
				if (target == TARGET_TS) {
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->enum_desc, file_desc, d);
				}
				gf_s(f, ";\n\n");
				break;
			}
			case DK_FIELD: { /* extension */
				DescField *x = (DescField *)d;
				gf_jsdoc(f, d, "");
				gf_nl(f);
				const char *n = gf_import_schema(f, d, false)->name;
				if (target == TARGET_JS) {
					gf_export(f, "const", n);
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->ext_desc, file_desc, d);
				} else {
					gf_export(f, target == TARGET_TS ? "const" : "declare const", n);
					gf_s(f, ": ");
					extension_type(f, x);
					if (target == TARGET_TS) {
						gf_s(f, " = " PURE "\n  ");
						desc_call(f, f->rt->ext_desc, file_desc, d);
					}
				}
				gf_s(f, ";\n\n");
				break;
			}
			case DK_SERVICE: {
				DescService *sv = (DescService *)d;
				gf_jsdoc(f, d, "");
				gf_nl(f);
				const char *n = gf_import_schema(f, d, false)->name;
				if (target == TARGET_JS) {
					gf_export(f, "const", n);
					gf_s(f, " = " PURE "\n  ");
					desc_call(f, f->rt->service_desc, file_desc, d);
				} else {
					gf_export(f, target == TARGET_TS ? "const" : "declare const", n);
					gf_s(f, ": ");
					gf_sym(f, f->rt->gen_service);
					gf_s(f, "<");
					service_shape_expr(f, sv);
					gf_s(f, ">");
					if (target == TARGET_TS) {
						gf_s(f, " = " PURE "\n  ");
						desc_call(f, f->rt->service_desc, file_desc, d);
					}
				}
				gf_s(f, ";\n\n");
				break;
			}
			}
		}
	}
}

static void generate_ts(Schema *s) { generate_target(s, TARGET_TS); }
static void generate_js(Schema *s) { generate_target(s, TARGET_JS); }
static void generate_dts(Schema *s) { generate_target(s, TARGET_DTS); }

static const EsPlugin protoc_gen_es = {
	.name = "protoc-gen-es",
	.version = "v2.17.0",
	.parse_options = parse_options,
	.generate_ts = generate_ts,
	.generate_js = generate_js,
	.generate_dts = generate_dts,
};
