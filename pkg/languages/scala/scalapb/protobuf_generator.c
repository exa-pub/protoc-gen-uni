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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/ProtobufGenerator.scala (class
 * ProtobufGenerator, objects C and ProtobufGenerator), with Types.scala and ConstructorField.scala.
 * The sealed oneof traits (SealedOneofsGenerator.scala) and parseFrom (ParseFromGenerator.scala)
 * are in their own files. */

#define C_None "_root_.scala.None"
#define C_UnknownFieldSet "_root_.scalapb.UnknownFieldSet"
#define C_UnknownFieldSetEmpty "_root_.scalapb.UnknownFieldSet.empty"

/* ProtobufGenerator.escapeScalaString: over the UTF-16 code units of the string. */
static const char *escape_scala_string(Str raw)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < raw.n;) {
		uint32_t r;
		int w = utf8_decode(raw.p + i, raw.n - i, &r);
		i += (size_t)w;
		uint32_t units[2];
		int nu = 1;
		units[0] = r;
		if (r >= 0x10000) {
			units[0] = 0xd800 + ((r - 0x10000) >> 10);
			units[1] = 0xdc00 + ((r - 0x10000) & 0x3ff);
			nu = 2;
		}
		for (int k = 0; k < nu; k++) {
			uint32_t c = units[k];
			switch (c) {
			case '\b': buf_adds(&b, "\\b"); break;
			case '\f': buf_adds(&b, "\\f"); break;
			case '\n': buf_adds(&b, "\\n"); break;
			case '\r': buf_adds(&b, "\\r"); break;
			case '\t': buf_adds(&b, "\\t"); break;
			case '\\': buf_adds(&b, "\\\\"); break;
			case '"': buf_adds(&b, "\\\""); break;
			case '\'': buf_adds(&b, "\\'"); break;
			default:
				if (c >= ' ' && c <= '~')
					buf_addc(&b, (char)c);
				else
					buf_fmt(&b, "\\u%04x", (unsigned)c);
			}
		}
	}
	buf_addc(&b, '"');
	return buf_str(&b);
}

/* ---- Types.scala ---- */

static const char *capitalized_type(int type)
{
	switch (type) {
	case PT_BOOL: return "Bool";
	case PT_BYTES: return "Bytes";
	case PT_DOUBLE: return "Double";
	case PT_ENUM: return "Enum";
	case PT_FIXED32: return "Fixed32";
	case PT_FIXED64: return "Fixed64";
	case PT_FLOAT: return "Float";
	case PT_GROUP: return "Group";
	case PT_INT32: return "Int32";
	case PT_INT64: return "Int64";
	case PT_MESSAGE: return "Message";
	case PT_SFIXED32: return "SFixed32";
	case PT_SFIXED64: return "SFixed64";
	case PT_SINT32: return "SInt32";
	case PT_SINT64: return "SInt64";
	case PT_STRING: return "String";
	case PT_UINT32: return "UInt32";
	default: return "UInt64";
	}
}

/* 0 when the size is not fixed */
static int fixed_size(int type)
{
	switch (type) {
	case PT_BOOL: return 1;
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return 8;
	case PT_FIXED32: case PT_FLOAT: case PT_SFIXED32: return 4;
	default: return 0;
	}
}

static int wire_type(int type)
{
	switch (type) {
	case PT_BYTES: case PT_MESSAGE: case PT_STRING: return WT_LEN;
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FIXED32: case PT_FLOAT: case PT_SFIXED32: return WT_I32;
	case PT_GROUP: return WT_SGROUP;
	default: return WT_VARINT;
	}
}

/* CodedOutputStream.computeTagSize */
static int compute_tag_size(int number)
{
	uint32_t v = (uint32_t)number << 3;
	int n = 1;
	while (v >= 0x80) {
		v >>= 7;
		n++;
	}
	return n;
}

static const char *join(const Vec *v, const char *sep) { return str_join(v, sep); }

static const char *mk_string(const Vec *v, const char *start, const char *sep, const char *end)
{
	return xcat(start, str_join(v, sep), end);
}

/* asScalaDocBlock */
static Vec as_scala_doc_block(const Vec *content)
{
	Vec out = {0};
	if (!content->n)
		return out;
	for (int i = 0; i < content->n; i++) {
		const char *line = content->v[i];
		const char *prefix = i == 0 ? "/**" : "  *";
		if (line[0] == ' ' || !line[0])
			vec_push(&out, xcat(prefix, line));
		else
			vec_push(&out, xcat(prefix, " ", line));
	}
	vec_push(&out, "  */");
	return out;
}

static void p_scala_doc(Printer *p, const char *comment)
{
	Vec lines = comment ? java_split(comment, '\n') : (Vec){0};
	Vec block = as_scala_doc_block(&lines);
	p_seq(p, &block);
}

/* ---- field helpers of the class ---- */

static const char *field_accessor_symbol(JField *f)
{
	if (field_is_in_oneof(f))
		return xcat(sn_name_symbol(oneof_scala_name(jfield_real_oneof(f))), ".", as_symbol(field_scala_name(f)));
	return as_symbol(field_scala_name(f));
}

static Expr to_base_type_expr(JField *f)
{
	if (field_custom_single_scala_type_name(f))
		return e_func(xcat(sn_full_name(field_type_mapper(f)), ".toBase"));
	return IDENTITY;
}

static Expr to_base_field_type(JField *f)
{
	if (field_is_enum(f))
		return e_then(to_base_type_expr(f), e_method("javaValueDescriptor"));
	return to_base_type_expr(f);
}

static Expr to_base_field_type_with_scala_descriptors(JField *f)
{
	if (field_is_enum(f))
		return e_then(to_base_type_expr(f), e_method("scalaValueDescriptor"));
	return to_base_type_expr(f);
}

static const char *to_base_type(JField *f, const char *expr)
{
	return xcat(e_apply(to_base_type_expr(f), expr, ENCLOSING_NONE), field_is_enum(f) ? ".value" : "");
}

static Expr to_custom_type_expr(JField *f)
{
	if (!field_custom_single_scala_type_name(f))
		return IDENTITY;
	return e_func(xcat(sn_full_name(field_type_mapper(f)), ".toCustom"));
}

static const char *to_custom_type(JField *f, const char *expr) { return e_apply(to_custom_type_expr(f), expr, ENCLOSING_NONE); }

static const char *ctx_full_name_with_maybe_root(ScalaName *n, JField *f)
{
	return sn_full_name_with_maybe_root_ctx(n, msg_context_names(f->containing_type));
}

static const char *java_float_str(float v)
{
	if (isinf(v))
		return v > 0 ? "Float.PositiveInfinity" : "Float.NegativeInfinity";
	if (isnan(v))
		return "Float.NaN";
	return xcat(java_float_to_string(v), "f");
}

static const char *java_double_str(double v)
{
	if (isinf(v))
		return v > 0 ? "Double.PositiveInfinity" : "Double.NegativeInfinity";
	if (isnan(v))
		return "Double.NaN";
	return java_double_to_string(v);
}

static const char *default_value_for_get(JField *f, bool uncustomized)
{
	const char *base;
	switch (jfield_java_type(f)) {
	case JT_INT: base = xfmt("%d", (int)f->def_i); break;
	case JT_LONG: base = xfmt("%lldL", (long long)f->def_i); break;
	case JT_FLOAT: base = java_float_str((float)f->def_d); break;
	case JT_DOUBLE: base = java_double_str(f->def_d); break;
	case JT_BOOLEAN: base = f->def_i ? "true" : "false"; break;
	case JT_BYTE_STRING:
		if (!f->def_s.n) {
			base = "_root_.com.google.protobuf.ByteString.EMPTY";
		} else {
			Vec bytes = {0};
			for (size_t i = 0; i < f->def_s.n; i++)
				vec_push(&bytes, xfmt("%d", (int)(signed char)f->def_s.p[i]));
			base = mk_string(&bytes, "_root_.com.google.protobuf.ByteString.copyFrom(Array[Byte](", ", ", "))");
		}
		break;
	case JT_STRING: base = escape_scala_string(f->def_s); break;
	case JT_MESSAGE:
		base = xcat(ctx_full_name_with_maybe_root(msg_scala_type(f->msg_type), f), ".defaultInstance");
		break;
	default:
		if (!f->def_e) /* the default of a repeated field is a list */
			java_exception("java.lang.ClassCastException", NULL);
		base = xcat(ctx_full_name_with_maybe_root(enum_scala_type(f->enum_type), f), ".",
			    as_symbol(enum_value_scala_name(f->def_e)));
		break;
	}
	if (!uncustomized && field_custom_single_scala_type_name(f))
		return xcat(sn_full_name(field_type_mapper(f)), ".toCustom(", base, ")");
	return base;
}

static const char *default_value_for_default_instance(JField *f)
{
	if (field_supports_presence(f))
		return C_None;
	if (jfield_is_repeated(f))
		return coll_empty(f);
	return default_value_for_get(f, false);
}

/* ---- Java conversions ---- */

static Expr java_to_scala_conversion(JField *f)
{
	Expr base;
	switch (jfield_java_type(f)) {
	case JT_INT: base = e_method("intValue"); break;
	case JT_LONG: base = e_method("longValue"); break;
	case JT_FLOAT: base = e_method("floatValue"); break;
	case JT_DOUBLE: base = e_method("doubleValue"); break;
	case JT_BOOLEAN: base = e_method("booleanValue"); break;
	case JT_BYTE_STRING: case JT_STRING: base = IDENTITY; break;
	case JT_MESSAGE:
		base = e_func(xcat(ctx_full_name_with_maybe_root(msg_scala_type(f->msg_type), f), ".fromJavaProto"));
		break;
	default:
		if (file_is_proto3(f->file))
			base = e_then(e_method("intValue"),
				      e_func(xcat(ctx_full_name_with_maybe_root(enum_scala_type(f->enum_type), f), ".fromValue")));
		else
			base = e_func(xcat(ctx_full_name_with_maybe_root(enum_scala_type(f->enum_type), f), ".fromJavaValue"));
		break;
	}
	return e_then(base, to_custom_type_expr(f));
}

static const char *java_field_to_scala3(const char *hazzer, const char *getter, JField *f)
{
	Expr conv = java_to_scala_conversion(f);
	if (field_supports_presence(f))
		return xcat("if (", hazzer, ") Some(", e_apply(conv, getter, ENCLOSING_NONE), ") else ", C_None);
	if (jfield_is_repeated(f))
		return e_apply4(conv, xcat(getter, ".asScala"), enc_collection(ScalaSeq, NULL), field_enclosing_type(f), true);
	return e_apply(conv, getter, ENCLOSING_NONE);
}

static const char *java_field_to_scala(const char *container, JField *f)
{
	const char *hazzer = xcat(container, ".has", field_upper_java_name(f));
	const char *upper = field_is_enum(f) && file_is_proto3(f->file) ? xcat(field_upper_java_name(f), "Value")
									: field_upper_java_name(f);
	const char *getter = jfield_is_repeated(f) ? xcat(container, ".get", upper, "List") : xcat(container, ".get", upper);
	return java_field_to_scala3(hazzer, getter, f);
}

static const char *java_map_field_to_scala(const char *container, JField *f)
{
	JField *kf = map_key_field(f->msg_type), *vf = map_value_field(f->msg_type);
	const char *upper = field_is_enum(vf) && file_is_proto3(f->file) ? xcat(field_upper_java_name(f), "Value")
									 : field_upper_java_name(f);
	return convert_collection(xcat(container, ".get", upper, "Map.asScala.iterator.map(__pv => (",
				       e_apply(java_to_scala_conversion(kf), "__pv._1", ENCLOSING_NONE), ", ",
				       e_apply(java_to_scala_conversion(vf), "__pv._2", ENCLOSING_NONE), "))"),
				  field_enclosing_type(f));
}

static Expr scala_to_java(JField *f, bool box)
{
	switch (jfield_java_type(f)) {
	case JT_INT: return box ? e_func("_root_.scala.Int.box") : IDENTITY;
	case JT_LONG: return box ? e_func("_root_.scala.Long.box") : IDENTITY;
	case JT_FLOAT: return box ? e_func("_root_.scala.Float.box") : IDENTITY;
	case JT_DOUBLE: return box ? e_func("_root_.scala.Double.box") : IDENTITY;
	case JT_BOOLEAN: return box ? e_func("_root_.scala.Boolean.box") : IDENTITY;
	case JT_BYTE_STRING: case JT_STRING: return IDENTITY;
	case JT_MESSAGE: return e_func(xcat(ctx_full_name_with_maybe_root(msg_scala_type(f->msg_type), f), ".toJavaProto"));
	default:
		if (file_is_proto3(f->file))
			return e_then(e_method("value"), box ? e_func("_root_.scala.Int.box") : IDENTITY);
		return e_func(xcat(ctx_full_name_with_maybe_root(enum_scala_type(f->enum_type), f), ".toJavaValue"));
	}
}

static const char *assign_scala_map_to_java(const char *scala_object, const char *java_object, JField *f)
{
	JField *kf = map_key_field(f->msg_type), *vf = map_value_field(f->msg_type);
	const char *put_all = xcat("putAll", field_upper_scala_name(f), field_is_enum(vf) && file_is_proto3(f->file) ? "Value" : "");
	Expr iter = field_collection_adapter(f) ? e_func(xcat(sn_full_name(field_collection_adapter(f)), ".toIterator"))
						: e_method("iterator");
	const char *it = e_apply(iter, xcat(scala_object, ".", as_symbol(field_scala_name(f))), ENCLOSING_NONE);
	const char *k = e_apply(e_then(to_base_type_expr(kf), scala_to_java(kf, true)), "__kv._1", ENCLOSING_NONE);
	const char *v = e_apply(e_then(to_base_type_expr(vf), scala_to_java(vf, true)), "__kv._2", ENCLOSING_NONE);
	return xcat(java_object, "\n  .", put_all, "(", it, ".map {\n    __kv => (", k, ", ", v, ")\n  }.toMap.asJava)");
}

static const char *assign_scala_field_to_java(const char *scala_object, const char *java_object, JField *f)
{
	if (field_is_map(f))
		return assign_scala_map_to_java(scala_object, java_object, f);
	const char *setter = xcat(java_object, jfield_is_repeated(f) ? ".addAll" : ".set", field_upper_java_name(f),
				  field_is_enum(f) && file_is_proto3(f->file) ? "Value" : "");
	const char *getter = xcat(scala_object, ".", field_accessor_symbol(f));
	Enclosing src = field_enclosing_type(f);
	Enclosing tgt = src.kind == ENC_COLLECTION ? enc_collection(ScalaIterable, NULL) : src;
	const char *expr = e_apply4(e_then(to_base_type_expr(f), scala_to_java(f, jfield_is_repeated(f))), getter, src, tgt, false);
	if (field_supports_presence(f) || field_is_in_oneof(f))
		return xcat(expr, ".foreach(", setter, ")");
	if (jfield_is_repeated(f))
		return xcat(setter, "(", expr, ".asJava)");
	return xcat(setter, "(", expr, ")");
}

/* ---- getField ---- */

static void generate_get_field(Printer *p, JMsg *m)
{
	const char *sig = "def getFieldByNumber(__fieldNumber: _root_.scala.Int): _root_.scala.Any = ";
	Vec fields = msg_fields(m);
	if (!fields.n) {
		p_add(p, xcat(sig, "throw new MatchError(__fieldNumber)"));
		return;
	}
	p_add(p, xcat(sig, "{"));
	p_indent(p);
	p_add(p, "(__fieldNumber: @_root_.scala.unchecked) match {");
	p_indent(p);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		const char *e = e_apply4(to_base_field_type(f), field_accessor_symbol(f), field_enclosing_type(f),
					 field_map_enclosing_type(f), false);
		if (field_supports_presence(f) || field_is_in_oneof(f)) {
			p_addf(p, "case %d => %s.orNull", f->number, e);
		} else if (jfield_is_optional(f) && !field_no_box_required(f)) {
			p_addf(p, "case %d => {", f->number);
			p_indent(p);
			p_addf(p, "val __t = %s", e);
			const char *cond = !field_is_enum(f) ? xcat("__t != ", default_value_for_get(f, true)) : "__t.getNumber() != 0";
			p_addf(p, "if (%s) __t else null", cond);
			p_outdent(p);
			p_add(p, "}");
		} else {
			p_addf(p, "case %d => %s", f->number, e);
		}
	}
	p_outdent(p);
	p_add(p, "}");
	p_outdent(p);
	p_add(p, "}");
}

static Expr single_field_as_pvalue(JField *f)
{
#define D "_root_.scalapb.descriptors"
	switch (jfield_java_type(f)) {
	case JT_INT: return e_func(D ".PInt");
	case JT_LONG: return e_func(D ".PLong");
	case JT_FLOAT: return e_func(D ".PFloat");
	case JT_DOUBLE: return e_func(D ".PDouble");
	case JT_BOOLEAN: return e_func(D ".PBoolean");
	case JT_BYTE_STRING: return e_func(D ".PByteString");
	case JT_STRING: return e_func(D ".PString");
	case JT_ENUM: return e_func(D ".PEnum");
	default: return e_method("toPMessage");
	}
#undef D
}

static void generate_get_field_pvalue(Printer *p, JMsg *m)
{
	const char *sig = "def getField(__field: _root_.scalapb.descriptors.FieldDescriptor): _root_.scalapb.descriptors.PValue = ";
	Vec fields = msg_fields(m);
	if (!fields.n) {
		p_add(p, xcat(sig, "throw new MatchError(__field)"));
		return;
	}
	p_add(p, xcat(sig, "{"));
	p_indent(p);
	p_add(p, "_root_.scala.Predef.require(__field.containingMessage eq companion.scalaDescriptor)");
	p_add(p, "(__field.number: @_root_.scala.unchecked) match {");
	p_indent(p);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		Enclosing src = field_enclosing_type(f);
		Enclosing tgt = src.kind == ENC_COLLECTION ? enc_collection(ScalaVector, NULL) : src;
		const char *e = e_apply4(e_then(to_base_field_type_with_scala_descriptors(f), single_field_as_pvalue(f)),
					 field_accessor_symbol(f), src, tgt, false);
		if (field_supports_presence(f) || field_is_in_oneof(f))
			p_addf(p, "case %d => %s.getOrElse(_root_.scalapb.descriptors.PEmpty)", f->number, e);
		else if (jfield_is_repeated(f))
			p_addf(p, "case %d => _root_.scalapb.descriptors.PRepeated(%s)", f->number, e);
		else
			p_addf(p, "case %d => %s", f->number, e);
	}
	p_outdent(p);
	p_add(p, "}");
	p_outdent(p);
	p_add(p, "}");
}

/* ---- serialization ---- */

static void generate_write_single_value(Printer *p, JField *f, const char *value_expr)
{
	if (field_is_message(f)) {
		p_addf(p, "_output__.writeTag(%d, 2)\n_output__.writeUInt32NoTag(%s.serializedSize)\n%s.writeTo(_output__)",
		       f->number, value_expr, value_expr);
	} else {
		p_addf(p, "_output__.write%s(%d, %s)", capitalized_type(f->type), f->number, value_expr);
	}
}

static const char *size_expression_for_single_field(JField *f, const char *expr)
{
	if (field_is_message(f)) {
		const char *size = xcat(expr, ".serializedSize");
		return xfmt("%d + _root_.com.google.protobuf.CodedOutputStream.computeUInt32SizeNoTag(%s) + %s",
			    compute_tag_size(f->number), size, size);
	}
	return xfmt("_root_.com.google.protobuf.CodedOutputStream.compute%sSize(%d, %s)", capitalized_type(f->type), f->number, expr);
}

static const char *is_non_empty(const char *expr, JField *f)
{
	if (f->type == PT_BYTES || f->type == PT_STRING)
		return xcat("!", expr, ".isEmpty");
	if (f->type == PT_ENUM)
		return xcat(expr, " != 0");
	if (f->type == PT_MESSAGE)
		return xcat(expr, ".serializedSize != 0");
	return xcat(expr, " != ", default_value_for_get(f, true));
}

static Expr coll_size(JField *f)
{
	ScalaName *a = field_collection_adapter(f);
	return a ? e_func(xcat(sn_full_name(a), ".size")) : e_method("size");
}

static Expr coll_iterator(JField *f)
{
	ScalaName *a = field_collection_adapter(f);
	return a ? e_func(xcat(sn_full_name(a), ".toIterator")) : e_method("iterator");
}

static void generate_serialized_size_for_field(Printer *p, JField *f)
{
	const char *sym = field_accessor_symbol(f);
	if (jfield_is_required(f) || field_no_box_required(f)) {
		p_addf(p, "\n{\n  val __value = %s\n  __size += %s\n};", to_base_type(f, sym), size_expression_for_single_field(f, "__value"));
	} else if (field_is_singular(f)) {
		p_addf(p, "\n{\n  val __value = %s\n  if (%s) {\n    __size += %s\n  }\n};", to_base_type(f, sym),
		       is_non_empty("__value", f), size_expression_for_single_field(f, "__value"));
	} else if (jfield_is_optional(f)) {
		p_addf(p, "if (%s.isDefined) {\n  val __value = %s\n  __size += %s\n};", sym, to_base_type(f, xcat(sym, ".get")),
		       size_expression_for_single_field(f, "__value"));
	} else if (jfield_is_repeated(f)) {
		int tag_size = compute_tag_size(f->number);
		if (!jfield_is_packed(f)) {
			int size = fixed_size(f->type);
			if (size) {
				p_addf(p, "__size += %d * %s", size + tag_size,
				       e_apply(coll_size(f), as_symbol(field_scala_name(f)), ENCLOSING_NONE));
			} else {
				p_addf(p, "%s { __item =>\n  val __value = %s\n  __size += %s\n}", coll_foreach(f), to_base_type(f, "__item"),
				       size_expression_for_single_field(f, "__value"));
			}
		} else {
			p_addf(p,
			       "if (%s) {\n  val __localsize = %sSerializedSize\n  __size += %d + "
			       "_root_.com.google.protobuf.CodedOutputStream.computeUInt32SizeNoTag(__localsize) + __localsize\n}",
			       coll_non_empty_check(f, sym), field_scala_name(f), tag_size);
		}
	} else {
		java_exception("java.lang.RuntimeException", "Should not reach here.");
	}
}

static void generate_serialized_size(Printer *p, JMsg *m)
{
	Vec fields = msg_fields(m);
	const Compat *V = file_v(m->file);
	if (!fields.n && !msg_preserves_unknown_fields(m)) {
		p_add(p, "final override def serializedSize: _root_.scala.Int = 0");
		return;
	}
	if (!msg_is_value_class(m))
		p_addf(p, "@transient\n%s var __serializedSizeMemoized: _root_.scala.Int = 0", V->private_this);
	p_addf(p, "%s def __computeSerializedSize(): _root_.scala.Int = {", V->private_this);
	p_indent(p);
	p_add(p, "var __size = 0");
	for (int i = 0; i < fields.n; i++)
		generate_serialized_size_for_field(p, fields.v[i]);
	if (msg_preserves_unknown_fields(m))
		p_add(p, "__size += unknownFields.serializedSize");
	p_add(p, "__size");
	p_outdent(p);
	p_add(p, "}");
	p_add(p, "override def serializedSize: _root_.scala.Int = {");
	p_indent(p);
	if (msg_is_value_class(m))
		p_add(p, "__computeSerializedSize()");
	else
		p_add(p, "var __size = __serializedSizeMemoized\nif (__size == 0) {\n  __size = __computeSerializedSize() + 1\n"
			 "  __serializedSizeMemoized = __size\n}\n__size - 1\n");
	p_outdent(p);
	p_add(p, "}");
}

static void generate_serialized_size_for_packed_fields(Printer *p, JMsg *m)
{
	Vec fields = msg_fields(m);
	const Compat *V = file_v(m->file);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (!jfield_is_packed(f))
			continue;
		const char *method = xcat(field_scala_name(f), "SerializedSize");
		p_addf(p, "%s def %s = {", V->private_this, method);
		int size = fixed_size(f->type);
		if (size) {
			p_addf(p, "  %d * %s", size, e_apply(coll_size(f), as_symbol(field_scala_name(f)), ENCLOSING_NONE));
			p_add(p, "}");
		} else {
			Expr size_func = e_func(xcat("_root_.com.google.protobuf.CodedOutputStream.compute", capitalized_type(f->type), "SizeNoTag"));
			Expr from_enum = field_is_enum(f) ? e_method("value") : IDENTITY;
			Expr from_custom = field_custom_single_scala_type_name(f)
						   ? e_func(xcat(sn_full_name(field_type_mapper(f)), ".toBase"))
						   : IDENTITY;
			/* runSingleton(List(sizeFunc, fromEnum, fromCustom)): sizeFunc outermost */
			Expr all = e_then(e_then(from_custom, from_enum), size_func);
			const char *size_expr = e_run_singleton(all, "__i");
			p_indent(p);
			p_addf(p, "if (__%sField == 0) __%sField = {", method, method);
			p_add(p, "  var __s: _root_.scala.Int = 0");
			p_addf(p, "  %s(__i => __s += %s)", coll_foreach(f), size_expr);
			p_add(p, "  __s");
			p_add(p, "}");
			p_addf(p, "__%sField", method);
			p_outdent(p);
			p_add(p, "}");
			p_addf(p, "@transient %s var __%sField: _root_.scala.Int = 0", V->private_this, method);
		}
	}
}

static void generate_write_to(Printer *p, JMsg *m)
{
	p_add(p, "def writeTo(`_output__`: _root_.com.google.protobuf.CodedOutputStream): _root_.scala.Unit = {");
	p_indent(p);
	Vec fields = msg_fields(m);
	java_sort(fields.v, fields.n, jfield_cmp_number); /* sortBy(_.getNumber) */
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		const char *sym = field_accessor_symbol(f);
		const char *cap = capitalized_type(f->type);
		if (jfield_is_packed(f)) {
			Expr from_enum = field_is_enum(f) ? e_method("value") : IDENTITY;
			Expr from_custom = field_custom_single_scala_type_name(f)
						   ? e_func(xcat(sn_full_name(field_type_mapper(f)), ".toBase"))
						   : IDENTITY;
			const char *write_expr = e_run_singleton(e_then(from_custom, from_enum), "__v");
			p_addf(p,
			       "if (%s) {\n  _output__.writeTag(%d, 2)\n  _output__.writeUInt32NoTag(%sSerializedSize)\n"
			       "  %s(__v => _output__.write%sNoTag(%s))\n};",
			       coll_non_empty_check(f, sym), f->number, field_scala_name(f), coll_foreach(f), cap, write_expr);
		} else if (jfield_is_required(f) || field_no_box_required(f)) {
			p_add(p, "");
			p_add(p, "{");
			p_indent(p);
			p_addf(p, "val __v = %s", to_base_type(f, sym));
			generate_write_single_value(p, f, "__v");
			p_outdent(p);
			p_add(p, "};");
		} else if (field_is_singular(f)) {
			p_add(p, "{");
			p_indent(p);
			p_addf(p, "val __v = %s", to_base_type(f, sym));
			p_addf(p, "if (%s) {", is_non_empty("__v", f));
			p_indent(p);
			generate_write_single_value(p, f, "__v");
			p_outdent(p);
			p_add(p, "}");
			p_outdent(p);
			p_add(p, "};");
		} else {
			if (jfield_is_repeated(f))
				p_addf(p, "%s { __v =>", coll_foreach(f));
			else
				p_addf(p, "%s.foreach { __v =>", field_accessor_symbol(f));
			p_indent(p);
			p_addf(p, "val __m = %s", to_base_type(f, "__v"));
			generate_write_single_value(p, f, "__m");
			p_outdent(p);
			p_add(p, "};");
		}
	}
	if (msg_preserves_unknown_fields(m))
		p_add(p, "unknownFields.writeTo(_output__)");
	p_outdent(p);
	p_add(p, "}");
}

/* ---- ConstructorField.scala ---- */

typedef struct {
	const char *name, *type_name, *default_value; /* default_value: NULL for None */
	int index;
	Vec annotations;
} CtorField;

#define UNKNOWN_FIELDS_INDEX 0x7fffffff

static int ctor_field_cmp_index(const void *a, const void *b)
{
	int x = ((const CtorField *)a)->index, y = ((const CtorField *)b)->index;
	return x < y ? -1 : x > y;
}

static const char *ctor_full_string(const CtorField *c)
{
	return xcat(c->annotations.n ? mk_string(&c->annotations, "", " ", " ") : "", c->name, ": ", c->type_name,
		    c->default_value ? xcat(" = ", c->default_value) : "");
}

static Vec constructor_fields(JMsg *m)
{
	Vec out = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (field_is_in_oneof(f))
			continue;
		CtorField *c = xalloc(sizeof *c);
		c->name = as_symbol(field_scala_name(f));
		c->type_name = field_scala_type_name(f);
		if (field_no_default_value_in_constructor(f))
			c->default_value = NULL;
		else if (jfield_is_optional(f) && field_supports_presence(f))
			c->default_value = C_None;
		else if (field_is_singular(f) && !jfield_is_required(f) && !field_no_box_required(f))
			c->default_value = default_value_for_get(f, false);
		else if (jfield_is_repeated(f) && !coll_non_empty_type(f))
			c->default_value = coll_empty(f);
		c->index = f->index;
		c->annotations = field_annotation_list(f);
		vec_push(&out, c);
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		CtorField *c = xalloc(sizeof *c);
		c->name = sn_name_symbol(oneof_scala_name(o));
		c->type_name = msg_full_name_with_maybe_root_ctx(oneof_scala_type(o), m);
		c->default_value = file_no_default_values_in_constructor(m->file) ? NULL
										  : msg_full_name_with_maybe_root_ctx(oneof_empty(o), m);
		if (!o->fields.n)
			java_exception("java.lang.ArrayIndexOutOfBoundsException", "Index 0 out of bounds for length 0");
		c->index = VEC_AT(o->fields, JField, 0)->index;
		vec_push(&out, c);
	}
	if (msg_preserves_unknown_fields(m)) {
		CtorField *c = xalloc(sizeof *c);
		c->name = "unknownFields";
		c->type_name = C_UnknownFieldSet;
		c->default_value = C_UnknownFieldSetEmpty;
		c->index = UNKNOWN_FIELDS_INDEX;
		c->annotations = pb_strs(message_options(m), SMO_UNKNOWN_FIELDS_ANNOTATIONS);
		vec_push(&out, c);
	}
	java_sort(out.v, out.n, ctor_field_cmp_index); /* sortBy(_.index) */
	return out;
}

static void print_constructor_field_list(Printer *p, JMsg *m)
{
	Vec cf = constructor_fields(m), lines = {0};
	for (int i = 0; i < cf.n; i++)
		vec_push(&lines, (void *)ctor_full_string(cf.v[i]));
	p_add_with_delimiter(p, ",", &lines);
}

static void generate_to_java_proto(Printer *p, JMsg *m)
{
	const char *my = sn_full_name(msg_scala_type(m));
	p_addf(p, "def toJavaProto(scalaPbSource: %s): %s = {", my, msg_java_type_name(m));
	p_indent(p);
	p_addf(p, "val javaPbOut = %s.newBuilder", msg_java_type_name(m));
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++)
		p_add(p, assign_scala_field_to_java("scalaPbSource", "javaPbOut", fields.v[i]));
	p_add(p, "javaPbOut.build");
	p_outdent(p);
	p_add(p, "}");
}

static void generate_from_java_proto(Printer *p, JMsg *m)
{
	const char *my = sn_full_name(msg_scala_type(m));
	p_addf(p, "def fromJavaProto(javaPbSource: %s): %s = %s(", msg_java_type_name(m), my, my);
	p_indent(p);
	Vec groups = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (field_is_in_oneof(f))
			continue;
		const char *conv = field_is_map(f) ? java_map_field_to_scala("javaPbSource", f) : java_field_to_scala("javaPbSource", f);
		Vec *g = xalloc(sizeof *g);
		vec_push(g, xcat(as_symbol(field_scala_name(f)), " = ", conv));
		vec_push(&groups, g);
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		Vec *g = xalloc(sizeof *g);
		vec_push(g, xcat(sn_name_symbol(oneof_scala_name(o)), " = javaPbSource.", oneof_java_enum_name(o), ".getNumber match {"));
		Vec of = oneof_fields(o);
		for (int j = 0; j < of.n; j++) {
			JField *f = of.v[j];
			vec_push(g, xfmt("  case %d => %s(%s)", f->number, sn_full_name(field_oneof_type_name(f)),
					 java_field_to_scala("javaPbSource", f)));
		}
		vec_push(g, xcat("  case _ => ", sn_full_name(oneof_empty(o))));
		vec_push(g, "}");
		vec_push(&groups, g);
	}
	p_add_groups_with_delimiter(p, ",", &groups);
	p_outdent(p);
	p_add(p, ")");
}

static void generate_no_default_args_factory(Printer *p, JMsg *m)
{
	Vec cf = constructor_fields(m), nt = {0}, names = {0};
	for (int i = 0; i < cf.n; i++) {
		CtorField *c = cf.v[i];
		if (c->index == UNKNOWN_FIELDS_INDEX)
			continue;
		vec_push(&nt, xcat(c->name, ": ", c->type_name));
		vec_push(&names, (void *)c->name);
	}
	p_add(p, "def of(");
	p_indent(p);
	p_add_with_delimiter(p, ",", &nt);
	p_outdent(p);
	const char *root = sn_full_name_with_maybe_root(msg_scala_type(m));
	p_addf(p, "): %s = %s(", root, root);
	p_indent(p);
	p_add_with_delimiter(p, ",", &names);
	p_outdent(p);
	p_add(p, ")");
}

static void generate_message_reads(Printer *p, JMsg *m)
{
	const char *my = sn_full_name(msg_scala_type(m));
	p_addf(p,
	       "implicit def messageReads: _root_.scalapb.descriptors.Reads[%s] = _root_.scalapb.descriptors.Reads{\n"
	       "  case _root_.scalapb.descriptors.PMessage(__fieldsMap) =>\n"
	       "    _root_.scala.Predef.require(__fieldsMap.keys.forall(_.containingMessage eq scalaDescriptor), \"FieldDescriptor does not match message type.\")\n"
	       "    %s(",
	       my, my);
	p->indent += 3;
	Vec items = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (field_is_in_oneof(f))
			continue;
		Enclosing reads;
		if (field_supports_presence(f))
			reads = ENCLOSING_OPTION;
		else if (jfield_is_repeated(f) && field_is_map(f))
			reads = enc_collection(ScalaSeq, NULL);
		else if (jfield_is_repeated(f))
			reads = field_collection_adapter(f) ? enc_collection(ScalaIterator, NULL) : enc_collection(field_collection_type(f), NULL);
		else
			reads = ENCLOSING_NONE;
		const char *base_type_name =
			enc_as_type(reads, field_is_enum(f) ? "_root_.scalapb.descriptors.EnumValueDescriptor" : field_base_single_scala_type_name(f));
		const char *value = xfmt("__fieldsMap.get(scalaDescriptor.findFieldByNumber(%d).get)", f->number);
		const char *e;
		if (field_supports_presence(f)) {
			e = xcat(value, ".flatMap(_.as[", base_type_name, "])");
		} else if (jfield_is_repeated(f)) {
			if (reads.kind != ENC_COLLECTION)
				fail("Expected a collection enclosing. Pleae report this as a bug.");
			e = xcat(value, ".map(_.as[", base_type_name, "]).getOrElse(", reads.cc, ".empty)");
		} else if (jfield_is_required(f) || field_no_box_required(f)) {
			e = xcat(value, ".get.as[", base_type_name, "]");
		} else {
			const char *t = xcat(default_value_for_get(f, true), field_is_enum(f) ? ".scalaValueDescriptor" : "");
			e = xcat(value, ".map(_.as[", base_type_name, "]).getOrElse(", t, ")");
		}
		Expr transform = field_is_enum(f) ? e_then(e_method("number"), e_func(xcat(sn_full_name(enum_scala_type(f->enum_type)), ".fromValue")))
						  : IDENTITY;
		transform = e_then(transform, to_custom_type_expr(f));
		const char *item = e_apply4(transform, e, reads, field_is_map(f) ? field_enclosing_type(f) : reads, false);
		ScalaName *a = field_collection_adapter(f);
		const char *expr = a && !field_is_map(f) ? xcat(sn_full_name(a), ".fromIteratorUnsafe(", item, ")") : item;
		vec_push(&items, xcat(as_symbol(field_scala_name(f)), " = ", expr));
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		Vec of = oneof_fields(o);
		Buf acc = {0};
		for (int j = 0; j < of.n; j++) {
			JField *f = of.v[j];
			const char *value = xfmt("__fieldsMap.get(scalaDescriptor.findFieldByNumber(%d).get)", f->number);
			const char *type_name = field_is_enum(f) ? "_root_.scalapb.descriptors.EnumValueDescriptor" : field_base_single_scala_type_name(f);
			const char *e = xcat(value, ".flatMap(_.as[_root_.scala.Option[", type_name, "]])");
			Expr transform = field_is_enum(f) ? e_then(e_method("number"), e_func(xcat(sn_full_name(enum_scala_type(f->enum_type)), ".fromValue")))
							  : IDENTITY;
			transform = e_then(e_then(transform, to_custom_type_expr(f)), e_func(sn_full_name(field_oneof_type_name(f))));
			const char *el = e_apply(transform, e, ENCLOSING_OPTION);
			/* reduceLeft((acc, e) => s"$acc\n    .orElse[...]($e)"), appended in place */
			if (j)
				buf_adds(&acc, xcat("\n    .orElse[", sn_full_name(oneof_scala_type(o)), "](", el, ")"));
			else
				buf_adds(&acc, el);
		}
		if (!of.n)
			java_exception("java.lang.UnsupportedOperationException", "empty.reduceLeft");
		vec_push(&items, xcat(sn_name_symbol(oneof_scala_name(o)), " = ", buf_str(&acc), "\n    .getOrElse(", sn_full_name(oneof_empty(o)), ")"));
	}
	p_add_with_delimiter(p, ",", &items);
	p->indent -= 3;
	p_add(p, "    )\n  case _ => throw new RuntimeException(\"Expected PMessage\")\n}");
}

static void generate_descriptors(Printer *p, JMsg *m)
{
	p_addf(p,
	       "def javaDescriptor: _root_.com.google.protobuf.Descriptors.Descriptor = %s\n"
	       "def scalaDescriptor: _root_.scalapb.descriptors.Descriptor = %s",
	       msg_java_descriptor_source(m), msg_scala_descriptor_source(m));
}

static void generate_default_instance(Printer *p, JMsg *m)
{
	const char *my = sn_full_name(msg_scala_type(m));
	p_addf(p, "lazy val defaultInstance = %s(", my);
	p_indent(p);
	Vec items = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (!field_is_in_oneof(f))
			vec_push(&items, xcat(as_symbol(field_scala_name(f)), " = ", default_value_for_default_instance(f)));
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		vec_push(&items, xcat(sn_name_symbol(oneof_scala_name(o)), " = ", sn_full_name(oneof_empty(o))));
	}
	p_add_with_delimiter(p, ",", &items);
	p_outdent(p);
	p_add(p, ")");
}

static void generate_message_lens(Printer *p, JMsg *m)
{
	const char *class_name = sn_name(msg_scala_type(m));
	const char *full = sn_full_name(msg_scala_type(m));
#define LENS(s) xcat("_root_.scalapb.lenses.Lens[UpperPB, ", s, "]")
	p_addf(p, "implicit class %sLens[UpperPB](_l: _root_.scalapb.lenses.Lens[UpperPB, %s]) extends %s[UpperPB, %s](_l) {",
	       class_name, full, file_v(m->file)->message_lens, full);
	p_indent(p);
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		const char *fname = as_symbol(field_scala_name(f));
		if (!field_is_in_oneof(f)) {
			if (field_supports_presence(f)) {
				const char *opt = xcat("optional", field_upper_scala_name(f));
				p_add(p, xcat("def ", fname, ": ", LENS(field_single_scala_type_name(f)), " = field(_.", field_get_method(f),
					      ")((c_, f_) => c_.copy(", fname, " = _root_.scala.Option(f_)))\ndef ", opt, ": ",
					      LENS(field_scala_type_name(f)), " = field(_.", fname, ")((c_, f_) => c_.copy(", fname, " = f_))"));
			} else {
				p_add(p, xcat("def ", fname, ": ", LENS(field_scala_type_name(f)), " = field(_.", fname,
					      ")((c_, f_) => c_.copy(", fname, " = f_))"));
			}
		} else {
			const char *oneof_name = sn_name_symbol(oneof_scala_name(jfield_real_oneof(f)));
			p_add(p, xcat("def ", fname, ": ", LENS(field_scala_type_name(f)), " = field(_.", field_get_method(f),
				      ")((c_, f_) => c_.copy(", oneof_name, " = ",
				      msg_full_name_with_maybe_root_ctx(field_oneof_type_name(f), m), "(f_)))"));
		}
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		const char *oname = sn_name_symbol(oneof_scala_name(o));
		p_add(p, xcat("def ", oname, ": ", LENS(msg_full_name_with_maybe_root_ctx(oneof_scala_type(o), m)), " = field(_.", oname,
			      ")((c_, f_) => c_.copy(", oname, " = f_))"));
	}
#undef LENS
	p_outdent(p);
	p_add(p, "}");
}

static void generate_field_numbers(Printer *p, JMsg *m)
{
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		p_addf(p, "final val %s = %d", field_number_constant_name(f), f->number);
	}
}

static void generate_type_mappers(Printer *p, const Vec *fields, bool public_ctor_params)
{
	for (int i = 0; i < fields->n; i++) {
		JField *f = fields->v[i];
		const char *custom = field_custom_single_scala_type_name(f);
		if (!custom)
			continue;
		const char *modifier;
		if (public_ctor_params) {
			modifier = "";
		} else {
			const char *pkg = sn_full_name(file_scala_package(f->file));
			if (!pkg[0]) {
				modifier = "private ";
			} else {
				Vec parts = java_split(pkg, '.');
				modifier = xcat("private[", parts.n ? (const char *)parts.v[parts.n - 1] : "", "] ");
			}
		}
		p_add(p, "@transient");
		p_add(p, xcat(modifier, "val ", field_type_mapper_val_name(f), ": _root_.scalapb.TypeMapper[",
			      field_base_single_scala_type_name(f), ", ", custom, "] = implicitly[_root_.scalapb.TypeMapper[",
			      field_base_single_scala_type_name(f), ", ", custom, "]]"));
	}
}

static void generate_collection_adapters(Printer *p, const Vec *fields)
{
	for (int i = 0; i < fields->n; i++) {
		JField *f = fields->v[i];
		const char *adapter = field_collection_adapter_class(f);
		if (!adapter)
			continue;
		const char *pkg = sn_full_name(file_scala_package(f->file));
		const char *modifier;
		if (!pkg[0]) {
			modifier = "private";
		} else {
			Vec parts = java_split(pkg, '.');
			modifier = xcat("private[", parts.n ? (const char *)parts.v[parts.n - 1] : "", "]");
		}
		p_add(p, "@transient");
		p_add(p, xcat(modifier, " val ", sn_name_symbol(field_collection_adapter(f)), ": _root_.scalapb.CollectionAdapter[",
			      field_single_scala_type_name(f), ", ", field_scala_type_name(f), "] = ", adapter, "()"));
	}
}

static void generate_type_mappers_for_map_entry(Printer *p, JMsg *m)
{
	JField *kf = map_key_field(m), *vf = map_value_field(m);
	const char *full = sn_full_name(msg_scala_type(m));
	const char *pair = xcat("(", field_single_scala_type_name(kf), ", ", field_single_scala_type_name(vf), ")");
	const char *pair_to_message = xcat("__p => ", full, "(", field_supports_presence(kf) ? "Some(__p._1)" : "__p._1", ", ",
					   field_supports_presence(vf) ? "Some(__p._2)" : "__p._2", ")");
	const char *message_to_pair = xcat("__m => (", field_supports_presence(kf) ? "__m.getKey" : "__m.key", ", ",
					   field_supports_presence(vf) ? "__m.getValue" : "__m.value", ")");
	p_add(p, xcat("@transient\nimplicit val keyValueMapper: _root_.scalapb.TypeMapper[", full, ", ", pair, "] =\n  _root_.scalapb.TypeMapper[",
		      full, ", ", pair, "](", message_to_pair, ")(", pair_to_message, ")"));
}

static void generate_message_companion_for_field(Printer *p, JMsg *m)
{
	const char *wildcard = file_v(m->file)->wildcard_type;
	const char *sig = xcat("def messageCompanionForFieldNumber(__number: _root_.scala.Int): _root_.scalapb.GeneratedMessageCompanion[",
			       wildcard, "] = ");
	Vec fields = msg_fields(m), msgs = {0};
	for (int i = 0; i < fields.n; i++)
		if (field_is_message(fields.v[i]))
			vec_push(&msgs, fields.v[i]);
	if (!msgs.n) {
		p_add(p, xcat(sig, "throw new MatchError(__number)"));
		return;
	}
	p_add(p, xcat(sig, "{"));
	p_indent(p);
	p_add(p, xcat("var __out: _root_.scalapb.GeneratedMessageCompanion[", wildcard, "] = null"));
	p_add(p, "(__number: @_root_.scala.unchecked) match {");
	p_indent(p);
	for (int i = 0; i < msgs.n; i++) {
		JField *f = msgs.v[i];
		p_addf(p, "case %d => __out = %s", f->number, sn_full_name(msg_scala_type(f->msg_type)));
	}
	p_outdent(p);
	p_add(p, "}");
	p_add(p, "__out");
	p_outdent(p);
	p_add(p, "}");
}

static void generate_nested_messages_companions(Printer *p, JMsg *m)
{
	const char *comp_seq = file_v(m->file)->comp_seq_type;
	const char *sig = xcat("lazy val nestedMessagesCompanions: ", comp_seq, " =");
	if (!m->nested.n) {
		p_add(p, xcat(sig, " Seq.empty"));
		return;
	}
	p_add(p, sig);
	p_indent(p);
	p_add(p, xcat(comp_seq, "("));
	p_indent(p);
	Vec names = {0};
	for (int i = 0; i < m->nested.n; i++)
		vec_push(&names, (void *)sn_full_name_with_maybe_root(msg_scala_type(VEC_AT(m->nested, JMsg, i))));
	p_add_with_delimiter(p, ",", &names);
	p_outdent(p);
	p_add(p, ")");
	p_outdent(p);
}

static void generate_messages_companions(Printer *p, JFile *f)
{
	const char *comp_seq = file_v(f)->comp_seq_type;
	const char *sig = xcat("lazy val messagesCompanions: ", comp_seq, " =");
	if (!f->msgs.n) {
		p_add(p, xcat(sig, " Seq.empty"));
		return;
	}
	p_add(p, sig);
	p_indent(p);
	p_add(p, xcat(comp_seq, "("));
	p_indent(p);
	Vec names = {0};
	for (int i = 0; i < f->msgs.n; i++)
		vec_push(&names, (void *)sn_full_name(msg_scala_type(VEC_AT(f->msgs, JMsg, i))));
	p_add_with_delimiter(p, ",", &names);
	p_outdent(p);
	p_add(p, ")");
	p_outdent(p);
}

static void generate_enum_companion_for_field(Printer *p, JMsg *m)
{
	const char *sig = xcat("def enumCompanionForFieldNumber(__fieldNumber: _root_.scala.Int): _root_.scalapb.GeneratedEnumCompanion[",
			       file_v(m->file)->wildcard_type, "] = ");
	Vec fields = msg_fields(m), enums = {0};
	for (int i = 0; i < fields.n; i++)
		if (field_is_enum(fields.v[i]))
			vec_push(&enums, fields.v[i]);
	if (!enums.n) {
		p_add(p, xcat(sig, "throw new MatchError(__fieldNumber)"));
		return;
	}
	p_add(p, xcat(sig, "{"));
	p_indent(p);
	p_add(p, "(__fieldNumber: @_root_.scala.unchecked) match {");
	p_indent(p);
	for (int i = 0; i < enums.n; i++) {
		JField *f = enums.v[i];
		p_addf(p, "case %d => %s", f->number, sn_full_name(enum_scala_type(f->enum_type)));
	}
	p_outdent(p);
	p_add(p, "}");
	p_outdent(p);
	p_add(p, "}");
}

/* ---- extensions ---- */

static void print_extension(Printer *p, JField *fd)
{
	p_add(p, xcat("val ", as_symbol(field_scala_name(fd)), ": _root_.scalapb.GeneratedExtension[",
		      sn_full_name(msg_scala_type(fd->containing_type)), ", ", field_scala_type_name(fd), "] ="));
	const char *list_lens;
	Expr to_base, from_base;
	switch (fd->type) {
	case PT_DOUBLE:
		list_lens = "fixed64Lens";
		to_base = e_func("java.lang.Double.longBitsToDouble");
		from_base = e_func("java.lang.Double.doubleToLongBits");
		break;
	case PT_FLOAT:
		list_lens = "fixed32Lens";
		to_base = e_func("java.lang.Float.intBitsToFloat");
		from_base = e_func("java.lang.Float.floatToIntBits");
		break;
	case PT_INT64: case PT_UINT64:
		list_lens = "varintLens", to_base = IDENTITY, from_base = IDENTITY;
		break;
	case PT_INT32: case PT_UINT32:
		list_lens = "varintLens", to_base = e_method("toInt"), from_base = e_method("toLong");
		break;
	case PT_FIXED64: case PT_SFIXED64:
		list_lens = "fixed64Lens", to_base = IDENTITY, from_base = IDENTITY;
		break;
	case PT_FIXED32: case PT_SFIXED32:
		list_lens = "fixed32Lens", to_base = IDENTITY, from_base = IDENTITY;
		break;
	case PT_BOOL:
		list_lens = "varintLens";
		to_base = e_op("!= 0");
		from_base = e_func("_root_.scalapb.GeneratedExtension.Internal.bool2Long");
		break;
	case PT_STRING:
		list_lens = "lengthDelimitedLens";
		to_base = e_method("toStringUtf8()");
		from_base = e_func("_root_.com.google.protobuf.ByteString.copyFromUtf8");
		break;
	case PT_GROUP:
		java_exception("java.lang.RuntimeException", "Not supported");
	case PT_MESSAGE:
		list_lens = "lengthDelimitedLens";
		to_base = e_func(xcat("_root_.scalapb.GeneratedExtension.readMessageFromByteString(", field_base_single_scala_type_name(fd), ")"));
		from_base = e_method("toByteString");
		break;
	case PT_BYTES:
		list_lens = "lengthDelimitedLens", to_base = IDENTITY, from_base = IDENTITY;
		break;
	case PT_ENUM:
		list_lens = "varintLens";
		to_base = e_then(e_method("toInt"), e_func(xcat(field_base_single_scala_type_name(fd), ".fromValue")));
		from_base = e_then(e_method("value"), e_method("toLong"));
		break;
	case PT_SINT32:
		list_lens = "varintLens";
		to_base = e_then(e_method("toInt"), e_func("_root_.com.google.protobuf.CodedInputStream.decodeZigZag32"));
		from_base = e_then(e_func("_root_.com.google.protobuf.CodedOutputStream.encodeZigZag32"), e_method("toLong"));
		break;
	default: /* SINT64 */
		list_lens = "varintLens";
		to_base = e_func("_root_.com.google.protobuf.CodedInputStream.decodeZigZag64");
		from_base = e_func("_root_.com.google.protobuf.CodedOutputStream.encodeZigZag64");
		break;
	}
	Expr from_field_to_custom = e_then(to_base, to_custom_type_expr(fd));
	Expr from_custom_to_field = e_then(to_base_type_expr(fd), from_base);
	const char *factory;
	Vec args = {0};
	if (field_supports_presence(fd) && !field_is_message(fd)) {
		factory = "_root_.scalapb.GeneratedExtension.forOptionalUnknownField";
	} else if (field_supports_presence(fd) && field_is_message(fd)) {
		factory = "_root_.scalapb.GeneratedExtension.forOptionalUnknownMessageField";
	} else if (jfield_is_repeated(fd) && jfield_is_packable(fd)) {
		factory = "_root_.scalapb.GeneratedExtension.forRepeatedPackable";
		switch (fd->type) {
		case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: vec_push(&args, "_.readFixed64()"); break;
		case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: vec_push(&args, "_.readFixed32()"); break;
		default: vec_push(&args, "_.readInt64()"); break;
		}
	} else if (jfield_is_repeated(fd)) {
		factory = "_root_.scalapb.GeneratedExtension.forRepeatedUnpackable";
	} else {
		factory = !field_is_message(fd) ? "_root_.scalapb.GeneratedExtension.forSingularUnknownField"
						: "_root_.scalapb.GeneratedExtension.forSingularUnknownMessageField";
		vec_push(&args, (void *)default_value_for_get(fd, false));
	}
	Vec arg_list = {0};
	vec_push(&arg_list, xcat("{__valueIn => ", e_apply(from_field_to_custom, "__valueIn", ENCLOSING_NONE), "}"));
	vec_push(&arg_list, xcat("{(__valueIn: ", field_single_scala_type_name(fd), ") => ",
				 e_apply(from_custom_to_field, "__valueIn", ENCLOSING_NONE), "}"));
	arg_list = vec_concat(&arg_list, &args);
	p_addf(p, "  %s(%d, _root_.scalapb.UnknownFieldSet.Field.%s)(%s)", factory, fd->number, list_lens, join(&arg_list, ", "));
}

/* ---- enums and oneofs ---- */

static void print_enum(Printer *p, JEnum *e)
{
	const char *name = sn_name_symbol(enum_scala_type(e));
	bool deprecated = jpb_bool(pb_msg(e->proto, EDP_OPTIONS), 3, false);
	if (deprecated)
		p_add(p, DEPRECATED_ANNOTATION);
	p_scala_doc(p, enum_comment(e));
	Vec base = enum_annotations(e, SEO_BASE_ANNOTATIONS);
	p_seq(p, &base);
	Vec bte = enum_base_trait_extends(e);
	p_addf(p, "sealed abstract class %s(val value: _root_.scala.Int) extends %s {", name, join(&bte, " with "));
	p_indent(p);
	p_addf(p, "type EnumType = %s", sn_full_name(enum_scala_type(e)));
	p_addf(p, "type RecognizedType = %s", sn_full_name(enum_recognized(e)));
	for (int i = 0; i < e->values.n; i++)
		p_addf(p, "def %s: _root_.scala.Boolean = false", enum_value_is_name(VEC_AT(e->values, JEnumValue, i)));
	p_addf(p, "def companion: _root_.scalapb.GeneratedEnumCompanion[%s] = %s", name, sn_full_name(enum_scala_type(e)));
	p_addf(p,
	       "final def asRecognized: _root_.scala.Option[%s] = if (isUnrecognized) _root_.scala.None else _root_.scala.Some(this.asInstanceOf[%s])",
	       sn_full_name(enum_recognized(e)), sn_full_name(enum_recognized(e)));
	p_outdent(p);
	p_add(p, "}");
	p_add(p, "");
	if (deprecated)
		p_add(p, DEPRECATED_ANNOTATION);
	Vec ce = enum_companion_extends(e);
	p_addf(p, "object %s extends %s {", name, join(&ce, " with "));
	p_indent(p);
	Vec rec = enum_annotations(e, SEO_RECOGNIZED_ANNOTATIONS);
	p_seq(p, &rec);
	p_addf(p, "sealed trait %s extends %s", sn_name_symbol(enum_recognized(e)), name);
	p_addf(p, "implicit def enumCompanion: _root_.scalapb.GeneratedEnumCompanion[%s] = this", name);
	p_newline(p);
	for (int i = 0; i < e->values.n; i++) {
		JEnumValue *v = VEC_AT(e->values, JEnumValue, i);
		JEnumValue *first = enum_first_with_number(v);
		p_scala_doc(p, enum_value_comment(v));
		if (first == v) {
			p_add(p, "@SerialVersionUID(0L)");
			Vec ann = enum_value_annotation_list(v);
			p_seq(p, &ann);
			Vec ve = enum_value_extends(v);
			p_addf(p, "case object %s extends %s {", as_symbol(enum_value_scala_name(v)), join(&ve, " with "));
			p_indent(p);
			p_addf(p, "val index = %d", v->index);
			p_addf(p, "val name = \"%s\"", v->name);
			JEnumValue **s = enum_by_number(e);
			for (int j = e->by_number_pos[v->index]; j < e->values.n && s[j]->number == v->number; j++)
				p_addf(p, "override def %s: _root_.scala.Boolean = true", enum_value_is_name(s[j]));
			p_outdent(p);
			p_add(p, "}");
			p_add(p, "");
		} else {
			Vec ann = enum_value_annotation_list(v);
			p_seq(p, &ann);
			p_addf(p, "@transient val %s = %s", as_symbol(enum_value_scala_name(v)), as_symbol(enum_value_scala_name(first)));
			p_add(p, "");
		}
	}
	p_add(p, "@SerialVersionUID(0L)");
	Vec unrec = enum_annotations(e, SEO_UNRECOGNIZED_ANNOTATIONS);
	p_seq(p, &unrec);
	Vec vals = {0};
	for (int i = 0; i < e->values.n; i++)
		vec_push(&vals, (void *)as_symbol(enum_value_scala_name(VEC_AT(e->values, JEnumValue, i))));
	p_addf(p,
	       "final case class Unrecognized(unrecognizedValue: _root_.scala.Int) extends %s(unrecognizedValue) with _root_.scalapb.UnrecognizedEnum\n"
	       "lazy val values: scala.collection.immutable.Seq[ValueType] = scala.collection.immutable.Seq(%s)\n"
	       "def fromValue(__value: _root_.scala.Int): %s = __value match {",
	       name, join(&vals, ", "), name);
	Vec nodup = enum_values_with_no_duplicates(e);
	for (int i = 0; i < nodup.n; i++) {
		JEnumValue *v = nodup.v[i];
		p_addf(p, "  case %d => %s", v->number, as_symbol(enum_value_scala_name(v)));
	}
	p_addf(p,
	       "  case __other => Unrecognized(__other)\n}\n"
	       "def javaDescriptor: _root_.com.google.protobuf.Descriptors.EnumDescriptor = %s\n"
	       "def scalaDescriptor: _root_.scalapb.descriptors.EnumDescriptor = %s",
	       enum_java_descriptor_source(e), enum_scala_descriptor_source(e));
	if (enum_java_conversions(e)) {
		const char *jt = enum_java_type_name(e);
		p_addf(p,
		       "def fromJavaValue(pbJavaSource: %s): %s = fromValue(pbJavaSource.getNumber)\n"
		       "def toJavaValue(pbScalaSource: %s): %s = {\n"
		       "  _root_.scala.Predef.require(!pbScalaSource.isUnrecognized, \"Unrecognized enum values can not be converted to Java\")\n"
		       "  %s.forNumber(pbScalaSource.value)\n}",
		       jt, name, name, jt, jt);
	}
	p_outdent(p);
	p_add(p, "}");
}

static void print_oneof(Printer *p, JOneof *o)
{
	Vec bc = oneof_base_classes(o);
	ScalaName *st = oneof_scala_type(o);
	p_addf(p, "sealed trait %s extends %s {", sn_name_symbol(st), join(&bc, " with "));
	p_indent(p);
	p_add(p, "def isEmpty: _root_.scala.Boolean = false");
	p_add(p, "def isDefined: _root_.scala.Boolean = true");
	Vec fields = oneof_fields(o);
	for (int i = 0; i < fields.n; i++)
		p_addf(p, "def is%s: _root_.scala.Boolean = false", field_upper_scala_name(fields.v[i]));
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		p_addf(p, "def %s: _root_.scala.Option[%s] = %s", as_symbol(field_scala_name(f)), field_scala_type_name(f), C_None);
	}
	p_outdent(p);
	p_addf(p,
	       "}\nobject %s {\n  @SerialVersionUID(0L)\n  case object Empty extends %s {\n"
	       "    type ValueType = _root_.scala.Nothing\n"
	       "    override def isEmpty: _root_.scala.Boolean = true\n"
	       "    override def isDefined: _root_.scala.Boolean = false\n"
	       "    override def number: _root_.scala.Int = 0\n"
	       "    override def value: _root_.scala.Nothing = throw new java.util.NoSuchElementException(\"Empty.value\")\n  }\n",
	       sn_name_symbol(st), sn_full_name(st));
	p_indent(p);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		const char *t = field_scala_type_name(f);
		const char *up = field_upper_scala_name(f);
		p_addf(p,
		       "@SerialVersionUID(0L)%s\nfinal case class %s(value: %s) extends %s {\n  type ValueType = %s\n"
		       "  override def is%s: _root_.scala.Boolean = true\n"
		       "  override def %s: _root_.scala.Option[%s] = Some(value)\n"
		       "  override def number: _root_.scala.Int = %d\n}",
		       jpb_bool(jfield_options(f), FO_DEPRECATED, false) ? " " DEPRECATED_ANNOTATION : "", up, t, sn_full_name(st), t, up,
		       as_symbol(field_scala_name(f)), t, f->number);
	}
	p_outdent(p);
	p_add(p, "}");
}

/* ---- messages ---- */

static void print_message(Printer *p, JMsg *m);
static void generate_parse_from(Printer *p, JMsg *m);
static void generate_sealed_oneof_trait(Printer *p, JMsg *m);

static void generate_message_companion(Printer *p, JMsg *m)
{
	const char *class_name = sn_name_symbol(msg_scala_type(m));
	Vec cbc = msg_companion_base_classes(m);
	const char *companion_type = join(&cbc, " with ");
	const char *companion_type_decl = join(&cbc, file_v(m->file)->with_operator);
	Vec ann = msg_companion_annotation_list(m);
	p_seq(p, &ann);
	p_addf(p, "object %s extends %s {\n  implicit def messageCompanion: %s = this", class_name, companion_type, companion_type_decl);
	p_indent(p);
	if (msg_java_conversions(m)) {
		generate_to_java_proto(p, m);
		generate_from_java_proto(p, m);
	}
	generate_parse_from(p, m);
	generate_message_reads(p, m);
	generate_descriptors(p, m);
	generate_message_companion_for_field(p, m);
	generate_nested_messages_companions(p, m);
	generate_enum_companion_for_field(p, m);
	generate_default_instance(p, m);
	for (int i = 0; i < m->enums.n; i++)
		print_enum(p, VEC_AT(m->enums, JEnum, i));
	for (int i = 0; i < m->real_oneofs; i++)
		print_oneof(p, VEC_AT(m->oneofs, JOneof, i));
	for (int i = 0; i < m->nested.n; i++)
		print_message(p, VEC_AT(m->nested, JMsg, i));
	for (int i = 0; i < m->exts.n; i++)
		print_extension(p, VEC_AT(m->exts, JField, i));
	if (msg_generate_lenses(m))
		generate_message_lens(p, m);
	generate_field_numbers(p, m);
	Vec fields = msg_fields(m);
	Vec with_ext = vec_concat(&fields, &m->exts);
	generate_type_mappers(p, &with_ext, file_public_ctor_params(m->file));
	generate_collection_adapters(p, &with_ext);
	if (jmsg_is_map_entry(m))
		generate_type_mappers_for_map_entry(p, m);
	generate_no_default_args_factory(p, m);
	p_addf(p, "// @@protoc_insertion_point(GeneratedMessageCompanion[%s])", m->full_name);
	p_outdent(p);
	p_add(p, "}");
	p_add(p, "");
}

static void generate_scala_doc_message(Printer *p, JMsg *m)
{
	const char *c = msg_comment(m);
	Vec main_doc = c ? java_split(c, '\n') : (Vec){0};
	if (str_eq(m->full_name, "google.protobuf.Any")) {
		for (int i = 0; i < main_doc.n; i++) {
			Str t = str_c(main_doc.v[i]);
			while (t.n && (unsigned char)t.p[0] <= ' ')
				t.p++, t.n--;
			while (t.n && (unsigned char)t.p[t.n - 1] <= ' ')
				t.n--;
			if (str_eqc(t, "===="))
				main_doc.v[i] = "";
		}
	}
	Vec fields_doc = {0};
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		if (field_is_in_oneof(f))
			continue;
		const char *fc = field_comment(f);
		Vec lines = fc ? java_split(fc, '\n') : (Vec){0};
		if (!lines.n)
			continue;
		vec_push(&fields_doc, xcat("@param ", field_scala_name(f)));
		for (int j = 0; j < lines.n; j++)
			vec_push(&fields_doc, xcat("  ", (const char *)lines.v[j]));
	}
	Vec all = main_doc;
	if (main_doc.n && fields_doc.n)
		vec_push(&all, "");
	all = vec_concat(&all, &fields_doc);
	Vec block = as_scala_doc_block(&all);
	p_seq(p, &block);
}

static void print_message(Printer *p, JMsg *m)
{
	const char *full_name = msg_full_name_with_maybe_root_ctx(msg_scala_type(m), m);
	Vec derives_list = pb_strs(message_options(m), SMO_DERIVES);
	const char *derives = derives_list.n ? mk_string(&derives_list, " derives ", ", ", "") : "";
	const char *self = sn_name_symbol(msg_scala_type(m));
	generate_sealed_oneof_trait(p, m);
	generate_scala_doc_message(p, m);
	p_add(p, "@SerialVersionUID(0L)");
	Vec ann = msg_annotation_list(m);
	p_seq(p, &ann);
	p_addf(p, "final case class %s(", self);
	p_indent(p);
	p_indent(p);
	print_constructor_field_list(p, m);
	Vec bc = msg_base_classes(m);
	p_addf(p, ") extends %s%s {", join(&bc, " with "), derives);
	generate_serialized_size_for_packed_fields(p, m);
	generate_serialized_size(p, m);
	generate_write_to(p, m);
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++) {
		JField *f = fields.v[i];
		const char *with_method = xcat("with", field_upper_scala_name(f));
		const char *clear_method = xcat("clear", field_upper_scala_name(f));
		const char *single_type = field_single_scala_type_name(f);
		if ((field_supports_presence(f) || field_is_in_oneof(f)) &&
		    (msg_generate_getters(m) || msg_generate_lenses(m) || jmsg_is_map_entry(m))) {
			const char *def = default_value_for_get(f, false);
			const char *scope = msg_generate_getters(m) ? "" : "private ";
			p_add(p, xcat(scope, "def ", field_get_method(f), ": ", field_single_scala_type_name(f), " = ",
				      field_accessor_symbol(f), ".getOrElse(", def, ")"));
		}
		if (field_supports_presence(f)) {
			const char *sym = as_symbol(field_scala_name(f));
			p_add(p, xcat("def ", clear_method, ": ", self, " = copy(", sym, " = ", C_None, ")\ndef ", with_method, "(__v: ",
				      single_type, "): ", self, " = copy(", sym, " = Option(__v))"));
		}
		if (field_is_in_oneof(f)) {
			p_add(p, xcat("def ", with_method, "(__v: ", single_type, "): ", self, " = copy(",
				      sn_name_symbol(oneof_scala_name(jfield_real_oneof(f))), " = ",
				      msg_full_name_with_maybe_root_ctx(field_oneof_type_name(f), m), "(__v))"));
		}
		if (jfield_is_repeated(f)) {
			const char *sym = as_symbol(field_scala_name(f));
			const char *concat = coll_concat(f, sym, "__vs");
			if (!coll_non_empty_type(f))
				p_add(p, xcat("def ", clear_method, " = copy(", sym, " = ", coll_empty(f), ")"));
			p_add(p, xcat("def add", field_upper_scala_name(f), "(__vs: ", single_type, " *): ", self, " = addAll",
				      field_upper_scala_name(f), "(__vs)\ndef addAll", field_upper_scala_name(f), "(__vs: Iterable[",
				      single_type, "]): ", self, " = copy(", sym, " = ", concat, ")"));
		}
		if (jfield_is_repeated(f) || field_is_singular(f))
			p_add(p, xcat("def ", with_method, "(__v: ", field_scala_type_name(f), "): ", self, " = copy(",
				      as_symbol(field_scala_name(f)), " = __v)"));
	}
	for (int i = 0; i < m->real_oneofs; i++) {
		JOneof *o = VEC_AT(m->oneofs, JOneof, i);
		const char *oname = sn_name_symbol(oneof_scala_name(o));
		p_add(p, xcat("def clear", sn_name(oneof_scala_type(o)), ": ", self, " = copy(", oname, " = ",
			      msg_full_name_with_maybe_root_ctx(oneof_empty(o), m), ")\ndef with", sn_name(oneof_scala_type(o)),
			      "(__v: ", msg_full_name_with_maybe_root_ctx(oneof_scala_type(o), m), "): ", self, " = copy(", oname,
			      " = __v)"));
	}
	if (msg_preserves_unknown_fields(m))
		p_add(p, "def withUnknownFields(__v: " C_UnknownFieldSet ") = copy(unknownFields = __v)\n"
			 "def discardUnknownFields = copy(unknownFields = " C_UnknownFieldSetEmpty ")");
	generate_get_field(p, m);
	generate_get_field_pvalue(p, m);
	if (!P.single_line_to_proto_string)
		p_add(p, "def toProtoString: _root_.scala.Predef.String = _root_.scalapb.TextFormat.printToUnicodeString(this)");
	else
		p_add(p, "def toProtoString: _root_.scala.Predef.String = _root_.scalapb.TextFormat.printToSingleLineUnicodeString(this)");
	if (P.ascii_format_to_string)
		p_add(p, "override def toString: _root_.scala.Predef.String = toProtoString");
	p_addf(p, "def companion: %s.type = %s", full_name, full_name);
	if (msg_is_sealed_oneof_type(m)) {
		const char *scala_type = msg_sealed_oneof_scala_type(m);
		const char *name = sn_name(msg_sealed_oneof_trait_scala_type(m));
		p_addf(p, "def to%s: %s = %s.toCustom(this)", name, scala_type, sn_full_name(msg_sealed_oneof_type_mapper(m)));
	}
	p_addf(p, "// @@protoc_insertion_point(GeneratedMessage[%s])", m->full_name);
	p_outdent(p);
	p_outdent(p);
	p_add(p, "}\n");
	generate_message_companion(p, m);
}

/* ---- files ---- */

static Printer scala_file_header(JFile *f, bool java_converter_import, bool include_preamble)
{
	PbMsg *o = file_opts(f);
	if (pb_count(o, SPO_PREAMBLE) && !jpb_bool(o, SPO_SINGLE_FILE, false))
		fail("%s: single_file must be true when a preamble is provided.", f->name);
	Printer p = {0};
	p_addf(&p, "// Generated by the Scala Plugin for the Protocol Buffer Compiler.\n// Do not edit!\n//\n// Protofile syntax: %s\n",
	       f->syntax == SYN_PROTO3 ? "PROTO3" : f->syntax == SYN_EDITIONS ? "EDITIONS" : "PROTO2");
	const char *pkg = sn_full_name(file_scala_package(f));
	if (pkg[0])
		p_add(&p, xcat("package ", pkg));
	if (java_converter_import)
		p_add(&p, xcat("import _root_.scalapb.internal.compat.JavaConverters.", file_v(f)->wildcard_import));
	for (int i = 0; i < pb_count(o, SPO_IMPORT); i++)
		p_add(&p, xcat("import ", jstr_at(o, SPO_IMPORT, i)));
	p_newline(&p);
	if (include_preamble) {
		Vec pre = pb_strs(o, SPO_PREAMBLE);
		p_seq(&p, &pre);
		if (pre.n)
			p_add(&p, "");
	}
	return p;
}

/* updateDescriptor: the file proto with the scala names of the fields and enum values set in
 * their scalapb options. */
static PbMsg *update_field(JField *f)
{
	PbMsg *fb = jpb_copy(f->proto);
	PbMsg *opts = pb_msg(fb, FD_OPTIONS);
	opts = opts ? jpb_copy(opts) : pb_new(&jd_FieldOptions);
	PbMsg *ext = pb_msg(opts, SCALAPB_EXT);
	ext = ext ? jpb_copy(ext) : pb_new(&sp_FieldOptions);
	const char *scala_name = field_scala_name(f);
	if (pb_has(ext, SFO_SCALA_NAME) && !str_eq(jstr(ext, SFO_SCALA_NAME), scala_name))
		java_exception("java.lang.AssertionError", "assertion failed");
	pb_set_str(ext, SFO_SCALA_NAME, scala_name);
	PbMsg *exts[1] = {ext};
	jpb_set_msgs(opts, SCALAPB_EXT, exts, 1);
	PbMsg *optss[1] = {opts};
	jpb_set_msgs(fb, FD_OPTIONS, optss, 1);
	return fb;
}

static PbMsg *update_enum_value(JEnumValue *v)
{
	PbMsg *ev = jpb_copy(v->proto);
	PbMsg *opts = pb_msg(ev, EVDP_OPTIONS);
	opts = opts ? jpb_copy(opts) : pb_new(&jd_EnumValueOptions);
	PbMsg *ext = pb_msg(opts, SCALAPB_EXT);
	ext = ext ? jpb_copy(ext) : pb_new(&sp_EnumValueOptions);
	const char *scala_name = enum_value_scala_name(v);
	if (pb_has(ext, SEVO_SCALA_NAME) && !str_eq(jstr(ext, SEVO_SCALA_NAME), scala_name))
		java_exception("java.lang.AssertionError", "assertion failed");
	pb_set_str(ext, SEVO_SCALA_NAME, scala_name);
	PbMsg *exts[1] = {ext};
	jpb_set_msgs(opts, SCALAPB_EXT, exts, 1);
	PbMsg *optss[1] = {opts};
	jpb_set_msgs(ev, EVDP_OPTIONS, optss, 1);
	return ev;
}

static PbMsg *update_enum(JEnum *e)
{
	PbMsg *ep = jpb_copy(e->proto);
	PbMsg **vals = xalloc(sizeof(PbMsg *) * (size_t)(e->values.n ? e->values.n : 1));
	for (int i = 0; i < e->values.n; i++)
		vals[i] = update_enum_value(VEC_AT(e->values, JEnumValue, i));
	jpb_set_msgs(ep, EDP_VALUE, vals, e->values.n);
	return ep;
}

static PbMsg *update_message_type(JMsg *m)
{
	PbMsg *mp = jpb_copy(m->proto);
	PbMsg **fs = xalloc(sizeof(PbMsg *) * (size_t)(m->fields.n ? m->fields.n : 1));
	for (int i = 0; i < m->fields.n; i++)
		fs[i] = update_field(VEC_AT(m->fields, JField, i));
	jpb_set_msgs(mp, DP_FIELD, fs, m->fields.n);
	PbMsg **ns = xalloc(sizeof(PbMsg *) * (size_t)(m->nested.n ? m->nested.n : 1));
	for (int i = 0; i < m->nested.n; i++)
		ns[i] = update_message_type(VEC_AT(m->nested, JMsg, i));
	jpb_set_msgs(mp, DP_NESTED_TYPE, ns, m->nested.n);
	PbMsg **es = xalloc(sizeof(PbMsg *) * (size_t)(m->enums.n ? m->enums.n : 1));
	for (int i = 0; i < m->enums.n; i++)
		es[i] = update_enum(VEC_AT(m->enums, JEnum, i));
	jpb_set_msgs(mp, DP_ENUM_TYPE, es, m->enums.n);
	return mp;
}

static PbMsg *update_descriptor(JFile *f)
{
	PbMsg *fp = jpb_copy(f->proto);
	PbMsg **ms = xalloc(sizeof(PbMsg *) * (size_t)(f->msgs.n ? f->msgs.n : 1));
	for (int i = 0; i < f->msgs.n; i++)
		ms[i] = update_message_type(VEC_AT(f->msgs, JMsg, i));
	jpb_set_msgs(fp, FDP_MESSAGE_TYPE, ms, f->msgs.n);
	PbMsg **es = xalloc(sizeof(PbMsg *) * (size_t)(f->enums.n ? f->enums.n : 1));
	for (int i = 0; i < f->enums.n; i++)
		es[i] = update_enum(VEC_AT(f->enums, JEnum, i));
	jpb_set_msgs(fp, FDP_ENUM_TYPE, es, f->enums.n);
	return fp;
}

/* scalapb.internal.Encoding.toBase64: standard alphabet with padding */
static const char *to_base64(const uint8_t *p, size_t n)
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	Buf b = {0};
	size_t i = 0;
	for (; i + 2 < n; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
		buf_addc(&b, tbl[v >> 18]);
		buf_addc(&b, tbl[(v >> 12) & 63]);
		buf_addc(&b, tbl[(v >> 6) & 63]);
		buf_addc(&b, tbl[v & 63]);
	}
	if (i + 1 == n) {
		uint32_t v = (uint32_t)p[i] << 16;
		buf_addc(&b, tbl[v >> 18]);
		buf_addc(&b, tbl[(v >> 12) & 63]);
		buf_adds(&b, "==");
	} else if (i + 2 == n) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
		buf_addc(&b, tbl[v >> 18]);
		buf_addc(&b, tbl[(v >> 12) & 63]);
		buf_addc(&b, tbl[(v >> 6) & 63]);
		buf_addc(&b, '=');
	}
	return buf_str(&b);
}

static void generate_file_descriptor(Printer *p, JFile *f)
{
	PbMsg *d = update_descriptor(f);
	if (!file_retain_source_code_info(f))
		jpb_clear(d, FDP_SOURCE_CODE_INFO);
	Buf bytes = {0};
	jpb_encode(d, &bytes);
	const char *b64 = to_base64((const uint8_t *)bytes.p, bytes.n);
	size_t len = strlen(b64);
	/* grouped(55000), each group: ("\"\"\"" + group).grouped(100), the last line + "\"\"\"" */
	Vec groups = {0};
	for (size_t g = 0; g < len; g += 55000) {
		size_t gn = len - g < 55000 ? len - g : 55000;
		const char *s = xcat("\"\"\"", xstrndup(b64 + g, gn));
		size_t sn = strlen(s);
		Vec *lines = xalloc(sizeof *lines);
		for (size_t k = 0; k < sn; k += 100)
			vec_push(lines, xstrndup(s + k, sn - k < 100 ? sn - k : 100));
		lines->v[lines->n - 1] = xcat(lines->v[lines->n - 1], "\"\"\"");
		vec_push(&groups, lines);
	}
	p_add(p, "private lazy val ProtoBytes: _root_.scala.Array[Byte] =");
	p_add(p, "    scalapb.Encoding.fromBase64(scala.collection.immutable.Seq(");
	p_add_groups_with_delimiter(p, ",", &groups);
	p_add(p, "    ).mkString)");
	p_add(p, "lazy val scalaDescriptor: _root_.scalapb.descriptors.FileDescriptor = {");
	p_add(p, "  val scalaProto = com.google.protobuf.descriptor.FileDescriptorProto.parseFrom(ProtoBytes)");
	p_add(p, "  _root_.scalapb.descriptors.FileDescriptor.buildFrom(scalaProto, dependencies.map(_.scalaDescriptor))");
	p_add(p, "}");
	if (file_java_conversions(f)) {
		p_add(p, "lazy val javaDescriptor: com.google.protobuf.Descriptors.FileDescriptor =");
		p_add(p, xcat("  ", file_java_full_outer_class_name(f), ".getDescriptor()"));
	} else {
		p_add(p, "lazy val javaDescriptor: com.google.protobuf.Descriptors.FileDescriptor = {");
		p_add(p, "  val javaProto = com.google.protobuf.DescriptorProtos.FileDescriptorProto.parseFrom(ProtoBytes)");
		p_add(p, "  com.google.protobuf.Descriptors.FileDescriptor.buildFrom(javaProto, _root_.scala.Array(");
		Vec deps = {0};
		for (int i = 0; i < f->deps.n; i++)
			vec_push(&deps, xcat("    ", sn_full_name(file_descriptor_object(VEC_AT(f->deps, JFile, i))), ".javaDescriptor"));
		p_add_with_delimiter(p, ",", &deps);
		p_add(p, "  ))");
		p_add(p, "}");
	}
	p_add(p, "@deprecated(\"Use javaDescriptor instead. In a future version this will refer to scalaDescriptor.\", \"ScalaPB 0.5.47\")");
	p_add(p, "def descriptor: com.google.protobuf.Descriptors.FileDescriptor = javaDescriptor");
}

typedef CodeGenFile OutFile;

static void print_service(Printer *p, JService *s);

static bool message_contains_repeated_fields(JMsg *m)
{
	Vec fields = msg_fields(m);
	for (int i = 0; i < fields.n; i++)
		if (jfield_is_repeated(fields.v[i]))
			return true;
	for (int i = 0; i < m->nested.n; i++)
		if (message_contains_repeated_fields(VEC_AT(m->nested, JMsg, i)))
			return true;
	return false;
}

static bool file_any_message_contains_repeated(JFile *f)
{
	for (int i = 0; i < f->msgs.n; i++)
		if (message_contains_repeated_fields(VEC_AT(f->msgs, JMsg, i)))
			return true;
	return false;
}

static void out_file(Vec *out, const char *name, const char *content)
{
	OutFile *o = xalloc(sizeof *o);
	o->name = name;
	o->content = content;
	vec_push(out, o);
}

static void generate_service_files(JFile *f, Vec *out)
{
	if (!P.grpc)
		return;
	for (int i = 0; i < f->services.n; i++) {
		JService *s = VEC_AT(f->services, JService, i);
		Printer p = scala_file_header(f, file_java_conversions(f) && file_any_message_contains_repeated(f), false);
		print_service(&p, s);
		out_file(out, xcat(file_scala_directory(f), "/", sn_name(service_companion_object(s)), ".scala"), p_result(&p));
	}
}

static void generate_file_object(Printer *p, JFile *f)
{
	p_addf(p, "object %s extends _root_.scalapb.GeneratedFileObject {", sn_name_symbol(file_descriptor_object(f)));
	p_indent(p);
	if (!f->deps.n) {
		p_add(p, "lazy val dependencies: Seq[_root_.scalapb.GeneratedFileObject] = Seq.empty");
	} else {
		p_add(p, "lazy val dependencies: Seq[_root_.scalapb.GeneratedFileObject] = Seq(");
		p_indent(p);
		Vec deps = {0};
		for (int i = 0; i < f->deps.n; i++)
			vec_push(&deps, (void *)sn_full_name(file_descriptor_object(VEC_AT(f->deps, JFile, i))));
		p_add_with_delimiter(p, ",", &deps);
		p_outdent(p);
		p_add(p, ")");
	}
	generate_messages_companions(p, f);
	generate_file_descriptor(p, f);
	for (int i = 0; i < f->exts.n; i++)
		print_extension(p, VEC_AT(f->exts, JField, i));
	generate_type_mappers(p, &f->exts, file_public_ctor_params(f));
	p_outdent(p);
	p_add(p, "}");
}

static void generate_single_scala_file(JFile *f, Vec *out)
{
	Printer p = scala_file_header(f, file_java_conversions(f) && file_any_message_contains_repeated(f), true);
	for (int i = 0; i < f->enums.n; i++)
		print_enum(&p, VEC_AT(f->enums, JEnum, i));
	for (int i = 0; i < f->msgs.n; i++)
		print_message(&p, VEC_AT(f->msgs, JMsg, i));
	generate_file_object(&p, f);
	const char *code = p_result(&p);
	generate_service_files(f, out);
	out_file(out, file_scala_file_name(f), code);
}

static void generate_multiple_scala_files(JFile *f, Vec *out)
{
	generate_service_files(f, out);
	for (int i = 0; i < f->enums.n; i++) {
		JEnum *e = VEC_AT(f->enums, JEnum, i);
		Printer p = scala_file_header(f, false, false);
		print_enum(&p, e);
		out_file(out, xcat(file_scala_directory(f), "/", e->name, ".scala"), p_result(&p));
	}
	for (int i = 0; i < f->msgs.n; i++) {
		JMsg *m = VEC_AT(f->msgs, JMsg, i);
		if (msg_is_sealed_oneof_case(m))
			continue;
		Vec *cases = sealed_cases_of(m);
		bool any_case_repeated = false;
		for (int j = 0; cases && j < cases->n; j++)
			any_case_repeated |= message_contains_repeated_fields(cases->v[j]);
		const char *name = msg_scala_file_name(m);
		Printer p = scala_file_header(f, file_java_conversions(f) && (message_contains_repeated_fields(m) || any_case_repeated), true);
		print_message(&p, m);
		for (int j = 0; cases && j < cases->n; j++)
			print_message(&p, cases->v[j]);
		out_file(out, name, p_result(&p));
	}
	Printer p = scala_file_header(f, false, true);
	generate_file_object(&p, f);
	out_file(out, file_scala_file_name(f), p_result(&p));
}
