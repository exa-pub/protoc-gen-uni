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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/SealedOneofsGenerator.scala. */

static void generate_sealed_oneof_trait(Printer *p, JMsg *m)
{
	if (!msg_is_sealed_oneof_type(m))
		return;
	const char *base_type = sn_full_name(msg_scala_type(m));
	const char *sealed_type = msg_sealed_oneof_scala_type(m);
	const char *sealed_name = sn_name_symbol(msg_sealed_oneof_trait_scala_type(m));
	const char *type_mapper = xcat("_root_.scalapb.TypeMapper[", base_type, ", ", sealed_type, "]");
	JOneof *oneof = VEC_AT(m->oneofs, JOneof, 0);
	const char *type_mapper_name = sn_name(msg_sealed_oneof_type_mapper(m));
	Vec base_classes = msg_sealed_oneof_base_classes(m);
	Vec derives_list = pb_strs(message_options(m), SMO_SEALED_ONEOF_DERIVES);
	const char *derives = derives_list.n ? xcat("derives ", join(&derives_list, ", "), " ") : "";
	const char *bases = base_classes.n ? xcat("extends ", join(&base_classes, " with "), " ", derives) : derives;
	Vec comp = pb_strs(message_options(m), SMO_SEALED_ONEOF_COMPANION_EXTENDS);
	const char *companion_bases = comp.n ? xcat("extends ", join(&comp, " with "), " ") : "";
	Vec fields = oneof_fields(oneof);
	const char *oneof_sym = sn_name_symbol(oneof_scala_name(oneof));
	const char *empty = sn_full_name(oneof_empty(oneof));
	if (msg_sealed_oneof_style(m) != SEALED_OPTIONAL) {
		const char *ne_name = sn_name_symbol(msg_sealed_oneof_non_empty_scala_type(m));
		const char *ne_type = sn_full_name(msg_sealed_oneof_non_empty_scala_type(m));
		const char *universal = msg_is_universal_trait(m) ? "Any with " : "";
		Vec ee = vec_of(sealed_type), x = pb_strs(message_options(m), SMO_SEALED_ONEOF_EMPTY_EXTENDS);
		ee = vec_concat(&ee, &x);
		p_addf(p, "sealed trait %s %s{", sealed_name, bases);
		p_indent(p);
		p_addf(p, "type MessageType = %s", base_type);
		p_addf(p, "final def isEmpty = this.isInstanceOf[%s.Empty.type]", sealed_type);
		p_add(p, "final def isDefined = !isEmpty");
		p_addf(p, "final def asMessage: %s = %s.toBase(this)", base_type, sn_full_name(msg_sealed_oneof_type_mapper(m)));
		p_addf(p, "final def asNonEmpty: Option[%s] = if (isEmpty) None else Some(this.asInstanceOf[%s])", ne_type, ne_type);
		p_outdent(p);
		p_add(p, "}");
		p_add(p, "");
		p_addf(p, "object %s %s{", sealed_name, companion_bases);
		p_indent(p);
		p_addf(p, "case object Empty extends %s", join(&ee, " with "));
		p_add(p, "");
		p_addf(p, "sealed trait %s extends %s%s", ne_name, universal, sealed_type);
		p_addf(p, "def defaultInstance: %s = Empty", sealed_type);
		p_add(p, "");
		p_addf(p, "implicit val %s: %s = new %s {", type_mapper_name, type_mapper, type_mapper);
		p_indent(p);
		p_addf(p, "override def toCustom(__base: %s): %s = __base.%s match {", base_type, sealed_type, oneof_sym);
		p_indent(p);
		for (int i = 0; i < fields.n; i++)
			p_addf(p, "case __v: %s => __v.value", sn_full_name(field_oneof_type_name(fields.v[i])));
		p_addf(p, "case %s => Empty", empty);
		p_outdent(p);
		p_add(p, "}");
		p_addf(p, "override def toBase(__custom: %s): %s = %s(__custom match {", sealed_type, base_type, base_type);
		p_indent(p);
		for (int i = 0; i < fields.n; i++)
			p_addf(p, "case __v: %s => %s(__v)", field_scala_type_name(fields.v[i]), sn_full_name(field_oneof_type_name(fields.v[i])));
		p_addf(p, "case Empty => %s", empty);
		p_outdent(p);
		p_add(p, "})");
		p_outdent(p);
		p_add(p, "}");
		p_outdent(p);
		p_add(p, "}");
	} else {
		p_addf(p, "sealed trait %s %s{", sealed_name, bases);
		p_indent(p);
		p_addf(p, "type MessageType = %s", base_type);
		p_addf(p, "final def asMessage: %s = %s.toBase(Some(this))", base_type, sn_full_name(msg_sealed_oneof_type_mapper(m)));
		p_outdent(p);
		p_add(p, "}");
		p_add(p, "");
		p_addf(p, "object %s %s {", sealed_name, companion_bases);
		p_indent(p);
		p_addf(p, "implicit val %s: %s = new %s {", type_mapper_name, type_mapper, type_mapper);
		p_indent(p);
		p_addf(p, "override def toCustom(__base: %s): %s = __base.%s match {", base_type, sealed_type, oneof_sym);
		p_indent(p);
		for (int i = 0; i < fields.n; i++)
			p_addf(p, "case __v: %s => Some(__v.value)", sn_full_name(field_oneof_type_name(fields.v[i])));
		p_addf(p, "case %s => None", empty);
		p_outdent(p);
		p_add(p, "}");
		p_addf(p, "override def toBase(__custom: %s): %s = %s(__custom match {", sealed_type, base_type, base_type);
		p_indent(p);
		for (int i = 0; i < fields.n; i++)
			p_addf(p, "case Some(__v: %s) => %s(__v)", field_scala_type_name(fields.v[i]), sn_full_name(field_oneof_type_name(fields.v[i])));
		p_addf(p, "case None => %s", empty);
		p_outdent(p);
		p_add(p, "})");
		p_outdent(p);
		p_add(p, "}");
		p_outdent(p);
		p_add(p, "}");
	}
}
