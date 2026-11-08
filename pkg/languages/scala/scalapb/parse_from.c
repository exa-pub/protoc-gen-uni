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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/ParseFromGenerator.scala. */

typedef struct {
	const char *name, *target_name, *type_name, *default_value, *accessor, *builder;
	bool is_repeated;
} PFField;

static PFField *pf_field(const char *name, const char *target, const char *type, const char *def, const char *accessor,
			 const char *builder, bool repeated)
{
	PFField *f = xalloc(sizeof *f);
	f->name = name, f->target_name = target, f->type_name = type, f->default_value = def;
	f->accessor = accessor, f->builder = builder, f->is_repeated = repeated;
	return f;
}

/* usesBaseTypeInBuilder */
static bool uses_base_type_in_builder(JField *f) { return field_is_singular(f); }

static Vec parse_from_fields(JMsg *m)
{
	Vec out = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (field_is_in_oneof(f))
			continue;
		const char *sn = field_scala_name(f);
		const char *sym = as_symbol(sn);
		const char *vname = xcat("__", sn);
		if (uses_base_type_in_builder(f)) {
			if (field_is_message(f))
				vec_push(&out, pf_field(vname, sym, xcat("_root_.scala.Option[", field_base_single_scala_type_name(f), "]"), C_None,
							xcat("_root_.scala.Some(", e_apply(to_base_type_expr(f), xcat("_message__.", sym), ENCLOSING_NONE), ")"),
							e_apply(to_custom_type_expr(f),
								xcat("__", sn, ".getOrElse(", sn_full_name(msg_scala_type(f->msg_type)), ".defaultInstance)"),
								ENCLOSING_NONE),
							jfield_is_repeated(f)));
			else
				vec_push(&out, pf_field(vname, sym, field_base_single_scala_type_name(f), default_value_for_get(f, true),
							e_apply(to_base_type_expr(f), xcat("_message__.", sym), ENCLOSING_NONE),
							e_apply(to_custom_type_expr(f), vname, ENCLOSING_NONE), jfield_is_repeated(f)));
		} else if (!jfield_is_repeated(f)) {
			vec_push(&out, pf_field(vname, sym, field_scala_type_name(f), default_value_for_default_instance(f),
						xcat("_message__.", sym), vname, false));
		} else {
			const char *it = field_collection_adapter(f)
						 ? e_apply(coll_iterator(f), xcat("_message__.", sym), ENCLOSING_NONE)
						 : xcat("_message__.", sym);
			vec_push(&out, pf_field(vname, sym, coll_builder_type(f), coll_new_builder(f), xcat(coll_new_builder(f), " ++= ", it),
						field_collection_adapter(f) ? xcat("__", sn, ".result().fold(throw _, identity(_))")
									    : xcat("__", sn, ".result()"),
						true));
		}
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		const char *name = sn_name(oneof_scala_name(o));
		vec_push(&out, pf_field(xcat("__", name), as_symbol(name), sn_full_name(oneof_scala_type(o)), sn_full_name(oneof_empty(o)),
					xcat("_message__.", sn_name_symbol(oneof_scala_name(o))), xcat("__", name), false));
	}
	if (msg_preserves_unknown_fields(m))
		vec_push(&out, pf_field("`_unknownFields__`", "unknownFields", "_root_.scalapb.UnknownFieldSet.Builder", "null",
					"new _root_.scalapb.UnknownFieldSet.Builder(_message__.unknownFields)",
					"if (_unknownFields__ == null) _root_.scalapb.UnknownFieldSet.empty else _unknownFields__.result()", false));
	return out;
}

/* "0x%xL".format of the sum of bits 0..n-1 */
static const char *hex_bits(int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v += (uint64_t)1 << i;
	return xfmt("0x%llxL", (unsigned long long)v);
}

static void generate_parse_from(Printer *p, JMsg *m)
{
	Vec fields = parse_from_fields(m);
	Vec all = msg_fields(m);
	/* requiredFieldMap: required (or no_box required) fields, numbered in order */
	Vec required = {0};
	for (int i = 0; i < all.n; i++)
		if (jfield_is_required(all.v[i]) || field_no_box_required(all.v[i]))
			vec_push(&required, all.v[i]);
	const char *my = msg_full_name_with_maybe_root_ctx(msg_scala_type(m), m);
	p_addf(p, "def parseFrom(`_input__`: _root_.com.google.protobuf.CodedInputStream): %s = {", my);
	p_indent(p);
	if (required.n) {
		int n = required.n;
		int full_words = (n - 1) / 64;
		int idx = 0;
		for (int i = 1; i <= full_words; i++)
			p_addf(p, "var __requiredFields%d: _root_.scala.Long = %s", idx++, hex_bits(64));
		p_addf(p, "var __requiredFields%d: _root_.scala.Long = %s", idx, hex_bits(n - 64 * full_words));
	}
	for (int i = 0; i < fields.n; i++) {
		PFField *f = fields.v[i];
		p_addf(p, "%s %s: %s = %s", f->is_repeated ? "val" : "var", f->name, f->type_name, f->default_value);
	}
	p_add(p, "var _done__ = false\nwhile (!_done__) {\n  val _tag__ = _input__.readTag()\n  _tag__ match {\n    case 0 => _done__ = true");
	int required_seen = 0;
	for (int i = 0; i < all.n; i++) {
		JField *f = all.v[i];
		const char *sn = field_scala_name(f);
		const char *new_val_base;
		if (field_is_message(f)) {
			const char *base_name = field_base_single_scala_type_name(f);
			if (jfield_is_repeated(f)) {
				new_val_base = xcat("_root_.scalapb.LiteParser.readMessage[", base_name, "](_input__)");
			} else if (uses_base_type_in_builder(f)) {
				new_val_base = xcat("_root_.scala.Some(__", sn, ".fold(_root_.scalapb.LiteParser.readMessage[", base_name,
						    "](_input__))(_root_.scalapb.LiteParser.readMessage(_input__, _)))");
			} else {
				const char *expr = field_is_in_oneof(f) ? xcat("__", sn_name(oneof_scala_name(jfield_real_oneof(f))), ".",
									       as_symbol(sn))
									: xcat("__", sn);
				const char *mapped = e_apply(to_base_field_type(f), expr, field_enclosing_type(f));
				if (field_is_in_oneof(f) || field_supports_presence(f))
					new_val_base = xcat(mapped, ".fold(_root_.scalapb.LiteParser.readMessage[", base_name,
							    "](_input__))(_root_.scalapb.LiteParser.readMessage(_input__, _))");
				else
					new_val_base = xcat("_root_.scalapb.LiteParser.readMessage[", base_name, "](_input__, ", mapped, ")");
			}
		} else if (field_is_enum(f)) {
			new_val_base = xcat(msg_full_name_with_maybe_root_ctx(enum_scala_type(f->enum_type), m), ".fromValue(_input__.readEnum())");
		} else if (f->type == PT_STRING) {
			new_val_base = "_input__.readStringRequireUtf8()";
		} else {
			new_val_base = xcat("_input__.read", capitalized_type(f->type), "()");
		}
		const char *new_val = !uses_base_type_in_builder(f) ? to_custom_type(f, new_val_base) : new_val_base;
		const char *update;
		if (field_supports_presence(f))
			update = xcat("__", sn, " = _root_.scala.Option(", new_val, ")");
		else if (field_is_in_oneof(f))
			update = xcat("__", sn_name(oneof_scala_name(jfield_real_oneof(f))), " = ", sn_full_name(field_oneof_type_name(f)), "(", new_val, ")");
		else if (jfield_is_repeated(f))
			update = xcat("__", sn, " += ", new_val);
		else
			update = xcat("__", sn, " = ", new_val);
		p_addf(p, "    case %d =>\n      %s", (int)(((uint32_t)f->number << 3) + (uint32_t)wire_type(f->type)), update);
		if (jfield_is_required(f) || field_no_box_required(f)) {
			int fn = required_seen++; /* requiredFieldMap(field): required lists them in this order */
			p_addf(p, "      __requiredFields%d &= 0x%llxL", fn / 64, (unsigned long long)~((uint64_t)1 << (fn % 64)));
		}
		if (jfield_is_packable(f)) {
			const char *read = xcat("_input__.read", capitalized_type(f->type), "()");
			if (field_is_enum(f))
				read = xcat(sn_full_name(enum_scala_type(f->enum_type)), ".fromValue(", read, ")");
			const char *read_expr = to_custom_type(f, read);
			p_addf(p,
			       "    case %d => {\n      val length = _input__.readRawVarint32()\n      val oldLimit = _input__.pushLimit(length)\n"
			       "      while (_input__.getBytesUntilLimit > 0) {\n        __%s += %s\n      }\n      _input__.popLimit(oldLimit)\n    }",
			       (int)(((uint32_t)f->number << 3) + WT_LEN), sn, read_expr);
		}
	}
	if (!msg_preserves_unknown_fields(m))
		p_add(p, "    case tag => _input__.skipField(tag)");
	else
		p_add(p, "    case tag =>\n      if (_unknownFields__ == null) {\n        _unknownFields__ = new _root_.scalapb.UnknownFieldSet.Builder()\n"
			 "      }\n      _unknownFields__.parseField(tag, _input__)");
	p_add(p, "  }");
	p_add(p, "}");
	if (required.n) {
		Vec conds = {0};
		for (int i = 0; i < (required.n + 63) / 64; i++)
			vec_push(&conds, xfmt("__requiredFields%d != 0L", i));
		p_addf(p, "if (%s) { throw new _root_.com.google.protobuf.InvalidProtocolBufferException(\"Message missing required fields.\") } ",
		       join(&conds, " || "));
	}
	p_addf(p, "%s(", my);
	p_indent(p);
	Vec lines = {0};
	for (int i = 0; i < fields.n; i++) {
		PFField *f = fields.v[i];
		vec_push(&lines, xcat("  ", f->target_name, " = ", f->builder));
	}
	p_add_with_delimiter(p, ",", &lines);
	p_outdent(p);
	p_add(p, ")");
	p_outdent(p);
	p_add(p, "}");
}
