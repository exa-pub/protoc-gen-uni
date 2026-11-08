/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/generator/message.rs of pbjson-build 0.8.0: Serialize and Deserialize of a message
 * following the proto3 JSON mapping. */

typedef struct {
	bool ignore_unknown_fields, emit_fields, preserve_proto_field_names;
	const Vec *btree_map_paths; /* const char * */
} MessageOptions;

static void write_field_empty_predicate(const Field *member, Buf *w, bool emit_fields)
{
	if (emit_fields) {
		buf_adds(w, "true");
		return;
	}
	const FieldType *t = member->type;
	const char *name = field_rust_field_name(member->name);
	int m = member->modifier;
	if (m == FM_REQUIRED)
		fail("internal error: entered unreachable code");
	if (m == FM_REPEATED || t->kind == FT_MAP ||
	    (t->kind == FT_SCALAR && (t->scalar == SC_STRING || t->scalar == SC_BYTES) && m == FM_USE_DEFAULT))
		buf_fmt(w, "!self.%s.is_empty()", name);
	else if (m == FM_OPTIONAL || t->kind == FT_MESSAGE)
		buf_fmt(w, "self.%s.is_some()", name);
	else if (t->kind == FT_SCALAR && (t->scalar == SC_F64 || t->scalar == SC_F32))
		buf_fmt(w, "self.%s != 0.", name);
	else if (t->kind == FT_SCALAR && t->scalar == SC_BOOL)
		buf_fmt(w, "self.%s", name);
	else
		buf_fmt(w, "self.%s != 0", name);
}

static void write_struct_serialize_start(int n, const Message *message, Buf *w, bool emit_fields)
{
	line(w, n, "use serde::ser::SerializeStruct;");
	int required_len = 0;
	for (int i = 0; i < message->fields.n; i++)
		required_len += VEC_AT(message->fields, Field, i)->modifier == FM_REQUIRED;
	if (required_len != message->fields.n || message->one_ofs.n)
		line(w, n, "let mut len = %d;", required_len);
	else
		line(w, n, "let len = %d;", required_len);
	for (int i = 0; i < message->fields.n; i++) {
		const Field *f = message->fields.v[i];
		if (f->modifier == FM_REQUIRED)
			continue;
		put(w, n, "if ");
		write_field_empty_predicate(f, w, emit_fields);
		buf_adds(w, " {\n");
		line(w, n + 1, "len += 1;");
		line(w, n, "}");
	}
	for (int i = 0; i < message->one_ofs.n; i++) {
		const OneOf *o = message->one_ofs.v[i];
		line(w, n, "if self.%s.is_some() {", field_rust_field_name(o->name));
		line(w, n + 1, "len += 1;");
		line(w, n, "}");
	}
	line(w, n, "let %sstruct_ser = serializer.serialize_struct(\"%s\", len)?;",
	     message->fields.n || message->one_ofs.n ? "mut " : "", type_path_display(message->path));
}

static void write_decode_variant(const Resolver *r, int n, const char *value, const TypePath *path, Buf *w)
{
	buf_fmt(w, "%s::try_from(%s)\n", rust_type(r, path), value);
	put(w, n, ".map_err(|_| serde::ser::Error::custom(format!(\"Invalid variant {}\", %s)))", value);
}

/* The ways to access a field's value. */
typedef struct {
	const char *as_ref;   /* a reference to the field's value */
	const char *as_unref; /* the field's value */
	const char *raw;      /* the field without any leading "&" or "*" */
} Variable;

static const Variable one_of_variable = {"v", "*v", "v"};

static void write_serialize_scalar_variable(int n, int scalar, int modifier, Variable var, const char *field_name,
					    Buf *w)
{
	const char *conversion;
	if (scalar == SC_I64 || scalar == SC_U64) {
		conversion = "ToString::to_string";
	} else if (scalar == SC_BYTES) {
		conversion = "pbjson::private::base64::encode";
	} else {
		line(w, n, "struct_ser.serialize_field(\"%s\", %s)?;", field_name, var.as_ref);
		return;
	}
	if (modifier == FM_REPEATED) {
		line(w, n, "struct_ser.serialize_field(\"%s\", &%s.iter().map(%s).collect::<Vec<_>>())?;", field_name,
		     var.raw, conversion);
	} else {
		line(w, n, "#[allow(clippy::needless_borrow)]");
		line(w, n, "#[allow(clippy::needless_borrows_for_generic_args)]");
		line(w, n, "struct_ser.serialize_field(\"%s\", %s(&%s).as_str())?;", field_name, conversion, var.raw);
	}
}

static void write_serialize_variable(const Resolver *r, int n, const Field *field, Variable var, Buf *w,
				     bool preserve_proto_field_names)
{
	const char *field_name = preserve_proto_field_names ? field->name : field_json_name(field);
	const FieldType *t = field->type;
	if (t->kind == FT_SCALAR) {
		write_serialize_scalar_variable(n, t->scalar, field->modifier, var, field_name, w);
	} else if (t->kind == FT_ENUM) {
		put(w, n, "let v = ");
		if (field->modifier == FM_REPEATED) {
			buf_fmt(w, "%s.iter().cloned().map(|v| {\n", var.raw);
			indent(w, n + 1);
			write_decode_variant(r, n + 2, "v", t->path, w);
			buf_addc(w, '\n');
			put(w, n + 1, "}).collect::<std::result::Result<Vec<_>, _>>()");
		} else {
			write_decode_variant(r, n + 1, var.as_unref, t->path, w);
		}
		buf_adds(w, "?;\n");
		line(w, n, "struct_ser.serialize_field(\"%s\", &v)?;", field_name);
	} else if (t->kind == FT_MAP &&
		   ((t->value->kind == FT_SCALAR && (t->value->scalar == SC_I64 || t->value->scalar == SC_U64 ||
						      t->value->scalar == SC_BYTES)) ||
		    t->value->kind == FT_ENUM)) {
		line(w, n, "let v: std::collections::HashMap<_, _> = %s.iter()", var.raw);
		if (t->value->kind == FT_ENUM) {
			line(w, n + 1, ".map(|(k, v)| {");
			put(w, n + 2, "let v = ");
			write_decode_variant(r, n + 3, "*v", t->value->path, w);
			buf_adds(w, "?;\n");
			line(w, n + 2, "Ok((k, v))");
			line(w, n + 1, "}).collect::<std::result::Result<_,_>>()?;");
		} else if (t->value->scalar == SC_BYTES) {
			line(w, n + 1, ".map(|(k, v)| (k, pbjson::private::base64::encode(v))).collect();");
		} else {
			line(w, n + 1, ".map(|(k, v)| (k, v.to_string())).collect();");
		}
		line(w, n, "struct_ser.serialize_field(\"%s\", &v)?;", field_name);
	} else {
		line(w, n, "struct_ser.serialize_field(\"%s\", %s)?;", field_name, var.as_ref);
	}
}

static void write_serialize_field(const Resolver *r, int n, const Field *field, Buf *w, bool emit_fields,
				  bool preserve_proto_field_names)
{
	char *as_ref = xcat("&self.", field_rust_field_name(field->name));
	Variable var = {as_ref, as_ref + 1, as_ref + 1};
	switch (field->modifier) {
	case FM_REQUIRED:
		write_serialize_variable(r, n, field, var, w, preserve_proto_field_names);
		break;
	case FM_OPTIONAL:
		line(w, n, "if let Some(v) = %s.as_ref() {", var.as_unref);
		write_serialize_variable(r, n + 1, field, one_of_variable, w, preserve_proto_field_names);
		line(w, n, "}");
		break;
	default:
		put(w, n, "if ");
		write_field_empty_predicate(field, w, emit_fields);
		buf_adds(w, " {\n");
		write_serialize_variable(r, n + 1, field, var, w, preserve_proto_field_names);
		line(w, n, "}");
	}
}

static void write_serialize_one_of(int n, const Resolver *r, const OneOf *one_of, Buf *w,
				   bool preserve_proto_field_names)
{
	line(w, n, "if let Some(v) = self.%s.as_ref() {", field_rust_field_name(one_of->name));
	line(w, n + 1, "match v {");
	for (int i = 0; i < one_of->fields.n; i++) {
		const Field *f = one_of->fields.v[i];
		line(w, n + 2, "%s::%s(v) => {", rust_type(r, one_of->path), field_rust_type_name(f));
		write_serialize_variable(r, n + 3, f, one_of_variable, w, preserve_proto_field_names);
		line(w, n + 2, "}");
	}
	line(w, n + 1, "}");
	line(w, n, "}");
}

static void write_message_serialize(const Resolver *r, int n, const Message *message, Buf *w, bool emit_fields,
				    bool preserve_proto_field_names)
{
	write_struct_serialize_start(n, message, w, emit_fields);
	for (int i = 0; i < message->fields.n; i++)
		write_serialize_field(r, n, message->fields.v[i], w, emit_fields, preserve_proto_field_names);
	for (int i = 0; i < message->one_ofs.n; i++)
		write_serialize_one_of(n, r, message->one_ofs.v[i], w, preserve_proto_field_names);
	line(w, n, "struct_ser.end()");
}

static void write_fields_enum(Buf *w, int n, const Vec *type_names, bool ignore_unknown_fields)
{
	line(w, n, "#[allow(clippy::enum_variant_names)]");
	line(w, n, "enum GeneratedField {");
	for (int i = 0; i < type_names->n; i++)
		line(w, n + 1, "%s,", escape_type(type_names->v[i]));
	if (ignore_unknown_fields)
		line(w, n + 1, "__SkipField__,");
	line(w, n, "}");
}

static void write_deserialize_field_name(int n, const Message *message, Buf *w, bool ignore_unknown_fields)
{
	/* all_fields: the fields, then the fields of the one-ofs */
	Vec all = {0};
	for (int i = 0; i < message->fields.n; i++)
		vec_push(&all, message->fields.v[i]);
	for (int i = 0; i < message->one_ofs.n; i++) {
		const OneOf *o = message->one_ofs.v[i];
		for (int j = 0; j < o->fields.n; j++)
			vec_push(&all, o->fields.v[j]);
	}
	int count = all.n;
	const char **json_names = xalloc(sizeof *json_names * (size_t)(count ? count : 1));
	const char **proto_names = xalloc(sizeof *proto_names * (size_t)(count ? count : 1));
	Vec names = {0}, type_names = {0};
	for (int i = 0; i < count; i++) {
		const Field *f = all.v[i];
		json_names[i] = field_json_name(f);
		/* only carry the original proto name if it's different from the provided json name */
		proto_names[i] = str_eq(f->name, json_names[i]) ? NULL : f->name;
		if (proto_names[i])
			vec_push(&names, (void *)proto_names[i]);
		vec_push(&names, (void *)json_names[i]);
		vec_push(&type_names, field_rust_type_name(f));
	}
	write_fields_array(w, n, &names);
	write_fields_enum(w, n, &type_names, ignore_unknown_fields);

	block(w, n,
	      "impl<'de> serde::Deserialize<'de> for GeneratedField {\n"
	      "    fn deserialize<D>(deserializer: D) -> std::result::Result<GeneratedField, D::Error>\n"
	      "    where\n"
	      "        D: serde::Deserializer<'de>,\n"
	      "    {\n"
	      "        struct GeneratedVisitor;\n"
	      "\n"
	      "        impl<'de> serde::de::Visitor<'de> for GeneratedVisitor {\n"
	      "            type Value = GeneratedField;\n"
	      "\n"
	      "            fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
	      "                write!(formatter, \"expected one of: {:?}\", &FIELDS)\n"
	      "            }\n"
	      "\n"
	      "            #[allow(unused_variables)]\n"
	      "            fn visit_str<E>(self, value: &str) -> std::result::Result<GeneratedField, E>\n"
	      "            where\n"
	      "                E: serde::de::Error,\n"
	      "            {");

	if (count) {
		line(w, n + 4, "match value {");
		for (int i = 0; i < count; i++) {
			const char *tn = escape_type(type_names.v[i]);
			if (proto_names[i])
				line(w, n + 5, "\"%s\" | \"%s\" => Ok(GeneratedField::%s),", json_names[i], proto_names[i], tn);
			else
				line(w, n + 5, "\"%s\" => Ok(GeneratedField::%s),", json_names[i], tn);
		}
		if (ignore_unknown_fields)
			line(w, n + 5, "_ => Ok(GeneratedField::__SkipField__),");
		else
			line(w, n + 5, "_ => Err(serde::de::Error::unknown_field(value, FIELDS)),");
		line(w, n + 4, "}");
	} else if (ignore_unknown_fields) {
		line(w, n + 5, "Ok(GeneratedField::__SkipField__)");
	} else {
		line(w, n + 5, "Err(serde::de::Error::unknown_field(value, FIELDS))");
	}

	block(w, n,
	      "            }\n"
	      "        }\n"
	      "        deserializer.deserialize_identifier(GeneratedVisitor)\n"
	      "    }\n"
	      "}");
}

static const char *override_deserializer(int scalar)
{
	if (scalar == SC_BYTES)
		return "::pbjson::private::BytesDeserialize<_>";
	return scalar_is_numeric(scalar) ? "::pbjson::private::NumberDeserialize<_>" : NULL;
}

static void write_encode_scalar_field(int n, int scalar, int modifier, Buf *w)
{
	const char *deserializer = override_deserializer(scalar);
	if (!deserializer) {
		buf_adds(w, modifier == FM_OPTIONAL ? "map_.next_value()?" : "Some(map_.next_value()?)");
		return;
	}
	buf_addc(w, '\n');
	switch (modifier) {
	case FM_OPTIONAL:
		line(w, n + 1, "map_.next_value::<::std::option::Option<%s>>()?.map(|x| x.0)", deserializer);
		break;
	case FM_REPEATED:
		line(w, n + 1, "Some(map_.next_value::<Vec<%s>>()?", deserializer);
		line(w, n + 2, ".into_iter().map(|x| x.0).collect())");
		break;
	default:
		line(w, n + 1, "Some(map_.next_value::<%s>()?.0)", deserializer);
	}
	indent(w, n);
}

static void write_deserialize_field(const Resolver *r, int n, const Field *field, const OneOf *one_of, bool btree_map,
				    Buf *w)
{
	const char *field_name = field_rust_field_name(one_of ? one_of->name : field->name);
	const char *json_name = field_json_name(field);
	const char *type_name = field_rust_type_name(field);
	const FieldType *t = field->type;
	line(w, n, "GeneratedField::%s => {", type_name);
	line(w, n + 1, "if %s__.is_some() {", field_name);
	/* Note: this will report duplicate field if multiple value are specified for a one of */
	line(w, n + 2, "return Err(serde::de::Error::duplicate_field(\"%s\"));", json_name);
	line(w, n + 1, "}");
	put(w, n + 1, "%s__ = ", field_name);

	if (one_of) {
		const char *one_of_type;
		switch (t->kind) {
		case FT_SCALAR: {
			const char *deserializer = override_deserializer(t->scalar);
			one_of_type = rust_type(r, one_of->path);
			if (deserializer)
				buf_fmt(w, "map_.next_value::<::std::option::Option<%s>>()?.map(|x| %s::%s(x.0))", deserializer,
					one_of_type, type_name);
			else
				buf_fmt(w, "map_.next_value::<::std::option::Option<_>>()?.map(%s::%s)", one_of_type, type_name);
			break;
		}
		case FT_ENUM: {
			char *enum_type = rust_type(r, t->path);
			one_of_type = rust_type(r, one_of->path);
			buf_fmt(w, "map_.next_value::<::std::option::Option<%s>>()?.map(|x| %s::%s(x as i32))", enum_type,
				one_of_type, type_name);
			break;
		}
		case FT_MESSAGE:
			one_of_type = rust_type(r, one_of->path);
			buf_fmt(w, "map_.next_value::<::std::option::Option<_>>()?.map(%s::%s)\n", one_of_type, type_name);
			break;
		default:
			fail("internal error: entered unreachable code: one of cannot contain map fields");
		}
	} else {
		switch (t->kind) {
		case FT_SCALAR:
			write_encode_scalar_field(n + 1, t->scalar, field->modifier, w);
			break;
		case FT_ENUM:
			if (field->modifier == FM_OPTIONAL)
				buf_fmt(w, "map_.next_value::<::std::option::Option<%s>>()?.map(|x| x as i32)", rust_type(r, t->path));
			else if (field->modifier == FM_REPEATED)
				buf_fmt(w, "Some(map_.next_value::<Vec<%s>>()?.into_iter().map(|x| x as i32).collect())",
					rust_type(r, t->path));
			else
				buf_fmt(w, "Some(map_.next_value::<%s>()? as i32)", rust_type(r, t->path));
			break;
		case FT_MAP: {
			buf_adds(w, "Some(\n");
			put(w, n + 2, btree_map ? "map_.next_value::<std::collections::BTreeMap<"
						: "map_.next_value::<std::collections::HashMap<");
			const char *map_k, *map_v;
			if (t->scalar == SC_BYTES || t->scalar == SC_F32 || t->scalar == SC_F64)
				fail("protobuf disallows maps with floating point or bytes keys");
			if (scalar_is_numeric(t->scalar)) {
				buf_fmt(w, "::pbjson::private::NumberDeserialize<%s>", scalar_rust_type(t->scalar));
				map_k = "k.0";
			} else {
				buf_adds(w, "_");
				map_k = "k";
			}
			buf_adds(w, ", ");
			const FieldType *v = t->value;
			if (v->kind == FT_SCALAR && scalar_is_numeric(v->scalar)) {
				buf_fmt(w, "::pbjson::private::NumberDeserialize<%s>", scalar_rust_type(v->scalar));
				map_v = "v.0";
			} else if (v->kind == FT_SCALAR && v->scalar == SC_BYTES) {
				buf_adds(w, "::pbjson::private::BytesDeserialize<_>");
				map_v = "v.0";
			} else if (v->kind == FT_ENUM) {
				buf_adds(w, rust_type(r, v->path));
				map_v = "v as i32";
			} else if (v->kind == FT_MAP) {
				fail("protobuf disallows nested maps");
			} else {
				buf_adds(w, "_");
				map_v = "v";
			}
			buf_adds(w, ">>()?\n");
			if (!str_eq(map_k, "k") || !str_eq(map_v, "v"))
				line(w, n + 3, ".into_iter().map(|(k,v)| (%s, %s)).collect()", map_k, map_v);
			put(w, n + 1, ")");
			break;
		}
		default: /* FT_MESSAGE */
			/* No explicit presence for repeated fields */
			buf_adds(w, field->modifier == FM_REPEATED ? "Some(map_.next_value()?)" : "map_.next_value()?");
		}
	}
	buf_adds(w, ";\n");
	line(w, n, "}");
}

static void write_deserialize_message(const Resolver *r, int n, const Message *message, const char *rust_type_,
				      Buf *w, const MessageOptions *o)
{
	write_deserialize_field_name(2, message, w, o->ignore_unknown_fields);
	line(w, n, "struct GeneratedVisitor;");
	char *name = type_path_display(message->path);
	block(w, n,
	      "impl<'de> serde::de::Visitor<'de> for GeneratedVisitor {\n"
	      "    type Value = %s;\n"
	      "\n"
	      "    fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
	      "        formatter.write_str(\"struct %s\")\n"
	      "    }\n"
	      "\n"
	      "    fn visit_map<V>(self, mut map_: V) -> std::result::Result<%s, V::Error>\n"
	      "        where\n"
	      "            V: serde::de::MapAccess<'de>,\n"
	      "    {",
	      rust_type_, name, rust_type_);

	for (int i = 0; i < message->fields.n; i++)
		line(w, n + 2, "let mut %s__ = None;", field_rust_field_name(VEC_AT(message->fields, Field, i)->name));
	for (int i = 0; i < message->one_ofs.n; i++)
		line(w, n + 2, "let mut %s__ = None;", field_rust_field_name(VEC_AT(message->one_ofs, OneOf, i)->name));

	if (message->fields.n || message->one_ofs.n) {
		line(w, n + 2, "while let Some(k) = map_.next_key()? {");
		line(w, n + 3, "match k {");
		bool btree_map = false;
		for (int i = 0; i < o->btree_map_paths->n && !btree_map; i++)
			btree_map = prefix_match(message->path, o->btree_map_paths->v[i]) >= 0;
		for (int i = 0; i < message->fields.n; i++)
			write_deserialize_field(r, n + 4, message->fields.v[i], NULL, btree_map, w);
		for (int i = 0; i < message->one_ofs.n; i++) {
			const OneOf *one_of = message->one_ofs.v[i];
			for (int j = 0; j < one_of->fields.n; j++)
				write_deserialize_field(r, n + 4, one_of->fields.v[j], one_of, btree_map, w);
		}
		if (o->ignore_unknown_fields) {
			line(w, n + 4, "GeneratedField::__SkipField__ => {");
			line(w, n + 5, "let _ = map_.next_value::<serde::de::IgnoredAny>()?;");
			line(w, n + 4, "}");
		}
		line(w, n + 3, "}");
		line(w, n + 2, "}");
	} else {
		line(w, n + 2, "while map_.next_key::<GeneratedField>()?.is_some() {");
		line(w, n + 3, "let _ = map_.next_value::<serde::de::IgnoredAny>()?;");
		line(w, n + 2, "}");
	}

	line(w, n + 2, "Ok(%s {", rust_type_);
	for (int i = 0; i < message->fields.n; i++) {
		const Field *f = message->fields.v[i];
		const char *fname = field_rust_field_name(f->name);
		if (f->modifier == FM_REQUIRED)
			line(w, n + 3, "%s: %s__.ok_or_else(|| serde::de::Error::missing_field(\"%s\"))?,", fname, fname,
			     field_json_name(f));
		else if (f->modifier == FM_USE_DEFAULT || f->modifier == FM_REPEATED)
			/* Note: this currently does not hydrate optional proto2 fields with defaults */
			line(w, n + 3, "%s: %s__.unwrap_or_default(),", fname, fname);
		else
			line(w, n + 3, "%s: %s__,", fname, fname);
	}
	for (int i = 0; i < message->one_ofs.n; i++) {
		const char *oname = field_rust_field_name(VEC_AT(message->one_ofs, OneOf, i)->name);
		line(w, n + 3, "%s: %s__,", oname, oname);
	}
	line(w, n + 2, "})");
	line(w, n + 1, "}");
	line(w, n, "}");
	line(w, n, "deserializer.deserialize_struct(\"%s\", FIELDS, GeneratedVisitor)", name);
}

static void generate_message(const Resolver *r, const Message *message, Buf *w, const MessageOptions *o)
{
	char *rust_type_ = rust_type(r, message->path);
	/* Generate Serialize */
	write_serialize_start(0, rust_type_, w);
	write_message_serialize(r, 2, message, w, o->emit_fields, o->preserve_proto_field_names);
	write_serialize_end(0, w);
	/* Generate Deserialize */
	write_deserialize_start(0, rust_type_, w);
	write_deserialize_message(r, 2, message, rust_type_, w, o);
	write_deserialize_end(0, w);
}
