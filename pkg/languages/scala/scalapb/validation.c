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

/* A port of compiler-plugin/src/main/scala/scalapb/compiler/ProtoValidation.scala. */

static void validate_enum(JEnum *e)
{
	for (int i = 0; i < e->values.n; i++) {
		char *up = xstrdup(VEC_AT(e->values, JEnumValue, i)->name);
		for (char *p = up; *p; p++)
			if (is_lower(*p))
				*p = (char)(*p - 'a' + 'A');
		if (str_eq(up, "UNRECOGNIZED"))
			fail("The enum value 'UNRECOGNIZED' in %s is not allowed due to conflict with the catch-all Unrecognized(v: Int) value.",
			     e->name);
	}
}

static void validate_field(JField *fd)
{
	PbMsg *o = field_options(fd);
	if (is_forbidden_field_name(field_scala_name(fd)))
		fail("Field named '%s' in message '%s' is not allowed. See https://scalapb.github.io/docs/customizations#custom-names",
		     fd->name, fd->full_name);
	if (!jfield_is_repeated(fd) && pb_has(o, SFO_COLLECTION_TYPE))
		fail("%s: Field %s has collection_type set but is not a repeated field.", fd->full_name, fd->name);
	if (!field_is_map(fd) && (pb_has(o, SFO_KEY_TYPE) || pb_has(o, SFO_VALUE_TYPE)))
		fail("%s: Field %s is not a map but specifies key_type or value_type.", fd->full_name, fd->name);
	if (field_is_map(fd) && pb_has(o, SFO_COLLECTION_TYPE))
		fail("%s: Field %s is a map but has collection_type specified. Use map_type instead.", fd->full_name, fd->name);
	if (!field_is_map(fd) && pb_has(o, SFO_MAP_TYPE))
		fail("%s: Field %s has map_type specified, but it is not a map field", fd->full_name, fd->name);
	if (field_is_map(fd) && pb_has(o, SFO_TYPE))
		fail("%s: Field %s is a map and has type specified. Use key_type or value_type instead.", fd->full_name, fd->name);
	if (!jfield_is_optional(fd) && pb_has(o, SFO_NO_BOX))
		fail("%s: Field %s has no_box set but is not an optional field.", fd->full_name, fd->name);
	if (field_is_message(fd) && msg_is_sealed_oneof_type(fd->msg_type) && pb_has(o, SFO_TYPE))
		fail("%s: Sealed oneofs can not be type mapped. Use regular oneofs instead.", fd->full_name);
	if (field_is_message(fd) && field_is_in_oneof(fd) && jpb_bool(message_options(fd->msg_type), SMO_NO_BOX, false))
		fail("%s: message fields in oneofs are not allowed to have no_box set.", fd->full_name);
	if (field_no_box_required(fd)) {
		if (pb_has(o, SFO_NO_BOX) && !jpb_bool(o, SFO_NO_BOX, false))
			fail("%s: setting no_box to false is not allowed while setting required to true.", fd->full_name);
		if (field_is_in_oneof(fd))
			fail("%s: setting required is not allowed on oneof fields.", fd->full_name);
		if (jfield_is_repeated(fd) || field_is_map(fd))
			fail("%s: required is not allowed on repeated fields.", fd->full_name);
		if (!field_is_message(fd))
			fail("%s: required can only be applied to message fields.", fd->full_name);
	}
}

/* JMsg.dup_mark: duplicate checks (distinct, groupBy) in linear time */
static unsigned dup_marks;

static void validate_message(JMsg *m)
{
	for (int i = 0; i < m->enums.n; i++)
		validate_enum(VEC_AT(m->enums, JEnum, i));
	for (int i = 0; i < m->nested.n; i++)
		validate_message(VEC_AT(m->nested, JMsg, i));
	for (int i = 0; i < m->fields.n; i++)
		validate_field(VEC_AT(m->fields, JField, i));
	if (msg_is_sealed_oneof_type(m)) {
		JOneof *oneof = VEC_AT(m->oneofs, JOneof, 0);
		if (m->fields.n != oneof->fields.n)
			fail("%s: sealed oneofs must have all their fields inside a single oneof", m->full_name);
		for (int i = 0; i < oneof->fields.n; i++) {
			JField *f = VEC_AT(oneof->fields, JField, i);
			if (!field_is_message(f))
				fail("%s.%s: sealed oneofs must have all their fields be message types", m->full_name, f->name);
		}
		for (int i = 0; i < oneof->fields.n; i++) {
			JField *f = VEC_AT(oneof->fields, JField, i);
			if (msg_is_sealed_oneof_type(f->msg_type))
				fail("%s.%s: sealed oneofs may not be a case within another sealed oneof", m->full_name, f->name);
		}
		for (int i = 0; i < oneof->fields.n; i++) {
			JField *f = VEC_AT(oneof->fields, JField, i);
			if (f->msg_type->parent != m->parent)
				fail("%s.%s: sealed oneofs must be in the same containing message (if any) as all case messages", m->full_name,
				     f->name);
		}
		for (int i = 0; i < oneof->fields.n; i++) {
			JField *f = VEC_AT(oneof->fields, JField, i);
			if (f->msg_type->file != m->file)
				fail("%s.%s: all sealed oneof cases must be defined in the same file as the sealed oneof field.", m->full_name,
				     f->name);
		}
		for (int i = 0; i < oneof->fields.n; i++) {
			JField *f = VEC_AT(oneof->fields, JField, i);
			if (field_custom_single_scala_type_name(f))
				fail("%s.%s: sealed oneof cases may not have custom types.", m->full_name, f->name);
		}
		unsigned mark = ++dup_marks;
		for (int i = 0; i < oneof->fields.n; i++) {
			JMsg *t = VEC_AT(oneof->fields, JField, i)->msg_type;
			if (t->dup_mark == mark)
				fail("%s: all sealed oneof cases must be of a distinct message type", m->full_name);
			t->dup_mark = mark;
		}
		if (m->nested.n)
			fail("%s: sealed oneofs may not contain nested messages", m->full_name);
		if (m->enums.n)
			fail("%s: sealed oneofs may not contain nested enums", m->full_name);
	} else {
		if (pb_count(message_options(m), SMO_SEALED_ONEOF_EXTENDS) > 0)
			fail("%s: `sealed_oneof_extends` option can only be used on a sealed oneof. Did you mean `extends`?", m->full_name);
		if (pb_count(message_options(m), SMO_SEALED_ONEOF_COMPANION_EXTENDS) > 0)
			fail("%s: `sealed_oneof_companion_extends` option can only be used on a sealed oneof. Did you mean `companion_extends`?",
			     m->full_name);
	}
}

static void validate_file(JFile *fd)
{
	for (int i = 0; i < fd->enums.n; i++)
		validate_enum(VEC_AT(fd->enums, JEnum, i));
	for (int i = 0; i < fd->msgs.n; i++)
		validate_message(VEC_AT(fd->msgs, JMsg, i));
	PbMsg *o = file_opts(fd);
	if (pb_has(o, SPO_PRIMITIVE_WRAPPERS) && jpb_bool(o, SPO_NO_PRIMITIVE_WRAPPERS, false))
		fail("%s: primitive_wrappers and no_primitive_wrappers must not be used at the same time.", fd->name);
	if (file_java_conversions(fd) && !file_java_package(fd)[0])
		fail("%s: java_conversions require either a package statement or setting the java_package option.", fd->name);
	/* allSealedOneofCases.groupBy(identity): a case of more than one sealed oneof */
	Vec cases = {0};
	for (int i = 0; i < fd->msgs.n; i++) {
		Vec *c = sealed_cases_of(VEC_AT(fd->msgs, JMsg, i));
		for (int j = 0; c && j < c->n; j++)
			vec_push(&cases, c->v[j]);
	}
	unsigned mark = ++dup_marks;
	for (int i = 0; i < cases.n; i++) {
		JMsg *c = cases.v[i];
		if (c->dup_mark == mark)
			fail("%s: message may belong to at most one sealed oneof", c->full_name);
		c->dup_mark = mark;
	}
}

static void validate_files(void)
{
	for (int i = 0; i < all_files.n; i++)
		validate_file(VEC_AT(all_files, JFile, i));
	if (!file_opts_built)
		build_file_options_cache();
}
