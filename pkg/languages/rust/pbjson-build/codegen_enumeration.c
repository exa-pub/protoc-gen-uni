/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/generator/enumeration.rs of pbjson-build 0.8.0: Serialize and Deserialize of an
 * enumeration, from its value name or its integer tag number. */

typedef struct {
	const char *name;
	int32_t number;
	const char *rust_variant;
} Variant;

static void write_enum_visitor(Buf *w, int n, const char *rust_type, const Vec *variants)
{
	/* Protobuf supports deserialization of enumerations both from string and integer values */
	block(w, n,
	      "struct GeneratedVisitor;\n"
	      "\n"
	      "impl<'de> serde::de::Visitor<'de> for GeneratedVisitor {\n"
	      "    type Value = %s;\n"
	      "\n"
	      "    fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
	      "        write!(formatter, \"expected one of: {:?}\", &FIELDS)\n"
	      "    }\n"
	      "\n"
	      "    fn visit_i64<E>(self, v: i64) -> std::result::Result<Self::Value, E>\n"
	      "    where\n"
	      "        E: serde::de::Error,\n"
	      "    {\n"
	      "        i32::try_from(v)\n"
	      "            .ok()\n"
	      "            .and_then(|x| x.try_into().ok())\n"
	      "            .ok_or_else(|| {\n"
	      "                serde::de::Error::invalid_value(serde::de::Unexpected::Signed(v), &self)\n"
	      "            })\n"
	      "    }\n"
	      "\n"
	      "    fn visit_u64<E>(self, v: u64) -> std::result::Result<Self::Value, E>\n"
	      "    where\n"
	      "        E: serde::de::Error,\n"
	      "    {\n"
	      "        i32::try_from(v)\n"
	      "            .ok()\n"
	      "            .and_then(|x| x.try_into().ok())\n"
	      "            .ok_or_else(|| {\n"
	      "                serde::de::Error::invalid_value(serde::de::Unexpected::Unsigned(v), &self)\n"
	      "            })\n"
	      "    }\n"
	      "\n"
	      "    fn visit_str<E>(self, value: &str) -> std::result::Result<Self::Value, E>\n"
	      "    where\n"
	      "        E: serde::de::Error,\n"
	      "    {",
	      rust_type);
	line(w, n + 2, "match value {");
	for (int i = 0; i < variants->n; i++) {
		const Variant *v = variants->v[i];
		line(w, n + 3, "\"%s\" => Ok(%s::%s),", v->name, rust_type, v->rust_variant);
	}
	line(w, n + 3, "_ => Err(serde::de::Error::unknown_variant(value, FIELDS)),");
	line(w, n + 2, "}");
	line(w, n + 1, "}");
	line(w, n, "}");
}

static void generate_enum(const Resolver *r, const TypePath *path, const PbMsg *proto, Buf *w,
			  bool use_integers_for_enums)
{
	char *rust_type_ = rust_type(r, path);

	/* Skip duplicates if we've seen the number before: allow_alias permits them. */
	Map seen = {0};
	Vec variants = {0}, names = {0};
	for (int i = 0; i < pb_count(proto, EDP_VALUE); i++) {
		const PbMsg *value = pb_msg_at(proto, EDP_VALUE, i);
		int32_t number = (int32_t)pb_int(value, EVDP_NUMBER, 0);
		char *key = xfmt("%d", (int)number);
		if (map_has(&seen, key))
			continue;
		map_put(&seen, key, (void *)1);
		if (!pb_has(value, EVDP_NAME))
			fail("called `Option::unwrap()` on a `None` value");
		Variant *v = xalloc(sizeof *v);
		v->name = pb_str(value, EVDP_NAME);
		v->number = number;
		v->rust_variant = rust_variant(r, path, v->name);
		vec_push(&variants, v);
		vec_push(&names, (void *)v->name);
	}

	/* Generate Serialize */
	write_serialize_start(0, rust_type_, w);
	line(w, 2, "let variant = match self {");
	for (int i = 0; i < variants.n; i++) {
		const Variant *v = variants.v[i];
		if (use_integers_for_enums)
			line(w, 3, "Self::%s => %d,", v->rust_variant, (int)v->number);
		else
			line(w, 3, "Self::%s => \"%s\",", v->rust_variant, v->name);
	}
	line(w, 2, "};");
	line(w, 2, use_integers_for_enums ? "serializer.serialize_i32(variant)" : "serializer.serialize_str(variant)");
	write_serialize_end(0, w);

	/* Generate Deserialize */
	write_deserialize_start(0, rust_type_, w);
	write_fields_array(w, 2, &names);
	write_enum_visitor(w, 2, rust_type_, &variants);
	/* Use deserialize_any to allow users to provide integers or strings */
	line(w, 2, "deserializer.deserialize_any(GeneratedVisitor)");
	write_deserialize_end(0, w);
}
