/* Copyright (c) 2025-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* bufbuild/py v0.7.0: a port of protoc-gen-py 0.7.0 (src/protoc_gen_py/__init__.py of the PyPI
 * sdist protoc_gen_py-0.7.0, https://github.com/bufbuild/protobuf-py), which generates Python
 * code for the protobuf-py runtime. The plugin framework protobuf.plugin and the descriptor model
 * are protobuf-py v0.7.0, ported in pkg/languages/python/protobuf-py.
 *
 * Unity build: everything but the generator below is static. */
#include "protobuf_py.h"
#include "uni.h"

#include <stdlib.h>
#include <string.h>

static const char BOOT_FILE_PATH[] = "google/protobuf/descriptor.proto";
static const char DEPRECATED_ALIAS[] = "_deprecated";
static const char OVERLOAD_ALIAS[] = "_overload";
static const char PROPERTY_ALIAS[] = "_property";
static const char DEPRECATED_INIT[] = "deprecated fields passed to init";

/* The module-level identifiers of __init__.py (created per request: the arena is reset between
 * requests in the fuzzer). */
static struct {
	PbpyIdent *desc_file, *message, *enm, *one_of, *extension, *file_desc, *boot_message, *boot, *unset, *final, *literal, *no_return,
		*type_alias, *type_checking, *overload, *enum_meta, *deprecated, *any_mixin, *duration_mixin,
		*file_descriptor_set_mixin, *list_value_mixin, *struct_mixin, *timestamp_mixin, *value_mixin;
} I;

static void init_idents(void)
{
	const char *protobuf = "protobuf", *typing = "typing", *mixin = "protobuf.wkt._mixin";
	I.desc_file = pbpy_ident(protobuf, "DescFile", true);
	I.message = pbpy_ident(protobuf, "Message", false);
	I.enm = pbpy_ident(protobuf, "Enum", false);
	I.one_of = pbpy_ident(protobuf, "Oneof", false);
	I.extension = pbpy_ident(protobuf, "Extension", false);
	I.file_desc = pbpy_ident(pbpy_module_child(protobuf, "_codegen"), "file_desc", false);
	const char *boot = pbpy_module_child(protobuf, "_boot");
	I.boot_message = pbpy_ident(boot, "Message", false);
	I.boot = pbpy_ident(boot, "boot", false);
	I.unset = pbpy_ident(boot, "unset", false);
	I.final = pbpy_ident(typing, "Final", false);
	I.literal = pbpy_ident(typing, "Literal", false);
	I.no_return = pbpy_ident(typing, "NoReturn", false);
	I.type_alias = pbpy_ident(typing, "TypeAlias", false);
	I.type_checking = pbpy_ident(typing, "TYPE_CHECKING", false);
	I.overload = pbpy_ident(typing, "overload", false);
	I.enum_meta = pbpy_ident("enum", "EnumMeta", false);
	I.deprecated = pbpy_ident("typing_extensions", "deprecated", false);
	I.any_mixin = pbpy_ident(mixin, "AnyMixin", false);
	I.duration_mixin = pbpy_ident(mixin, "DurationMixin", false);
	I.file_descriptor_set_mixin = pbpy_ident(mixin, "FileDescriptorSetMixin", false);
	I.list_value_mixin = pbpy_ident(mixin, "ListValueMixin", false);
	I.struct_mixin = pbpy_ident(mixin, "StructMixin", false);
	I.timestamp_mixin = pbpy_ident(mixin, "TimestampMixin", false);
	I.value_mixin = pbpy_ident(mixin, "ValueMixin", false);
}

static PbpyEls *els_new(void) { return xalloc(sizeof(PbpyEls)); }

/* ---- symbols ---- */

/* _all_symbols_in_file / _all_symbols_in_message */
static void symbols_in_message(const PbpyDescMessage *m, Vec *out)
{
	vec_push(out, (void *)m);
	for (int i = 0; i < m->nested_messages.n; i++)
		symbols_in_message(m->nested_messages.v[i], out);
	for (int i = 0; i < m->nested_enums.n; i++)
		vec_push(out, m->nested_enums.v[i]);
	for (int i = 0; i < m->nested_extensions.n; i++)
		vec_push(out, m->nested_extensions.v[i]);
}

static Vec all_symbols(const PbpyDescFile *f)
{
	Vec v = {0};
	for (int i = 0; i < f->messages.n; i++)
		symbols_in_message(f->messages.v[i], &v);
	for (int i = 0; i < f->enums.n; i++)
		vec_push(&v, f->enums.v[i]);
	for (int i = 0; i < f->extensions.n; i++)
		vec_push(&v, f->extensions.v[i]);
	return v;
}

static bool is_deprecated_field(const void *member)
{
	return ((const PbpyDesc *)member)->kind == PBPY_FIELD && ((const PbpyDescField *)member)->deprecated;
}

static bool any_deprecated_field(const Vec *members)
{
	for (int i = 0; i < members->n; i++)
		if (is_deprecated_field(members->v[i]))
			return true;
	return false;
}

/* _has_deprecated_members */
static bool has_deprecated_members(const void *desc)
{
	const PbpyDesc *d = desc;
	if (d->kind == PBPY_MESSAGE)
		return any_deprecated_field(&((const PbpyDescMessage *)desc)->members);
	if (d->kind == PBPY_ENUM) {
		const PbpyDescEnum *e = desc;
		for (int i = 0; i < e->values.n; i++)
			if (((const PbpyDescEnumValue *)e->values.v[i])->deprecated)
				return true;
	}
	return false;
}

static void generate_deprecated_aliases(PbpyFile *f, const PbpyDescFile *desc)
{
	Vec symbols = all_symbols(desc);
	bool members = false, any = false;
	for (int i = 0; i < symbols.n; i++) {
		const PbpyDesc *d = symbols.v[i];
		members = members || has_deprecated_members(d);
		any = any || (d->kind == PBPY_MESSAGE && ((const PbpyDescMessage *)d)->deprecated) ||
			(d->kind == PBPY_ENUM && ((const PbpyDescEnum *)d)->deprecated);
	}
	if (!members && !any)
		return;
	pbpy_print(f, PS(DEPRECATED_ALIAS), PS(" = "), PI(I.deprecated));
	if (members) {
		pbpy_type_checking(f);
		pbpy_print(f, PS(OVERLOAD_ALIAS), PS(" = "), PI(I.overload));
		pbpy_print(f, PS(PROPERTY_ALIAS), PS(" = property"));
		pbpy_type_checking_end(f);
	}
	pbpy_println(f);
}

static PbpyIdent *get_mixin(const PbpyDescMessage *msg)
{
	const char *file = msg->file->name, *name = msg->name;
	if (str_eq(file, "google/protobuf/duration.proto") && str_eq(name, "Duration"))
		return I.duration_mixin;
	if (str_eq(file, "google/protobuf/timestamp.proto") && str_eq(name, "Timestamp"))
		return I.timestamp_mixin;
	if (str_eq(file, "google/protobuf/any.proto") && str_eq(name, "Any"))
		return I.any_mixin;
	if (str_eq(file, "google/protobuf/descriptor.proto") && str_eq(name, "FileDescriptorSet"))
		return I.file_descriptor_set_mixin;
	if (str_eq(file, "google/protobuf/struct.proto")) {
		if (str_eq(name, "Struct"))
			return I.struct_mixin;
		if (str_eq(name, "Value"))
			return I.value_mixin;
		if (str_eq(name, "ListValue"))
			return I.list_value_mixin;
	}
	return NULL;
}

/* ---- types ---- */

static PbpyEl element_el(const PbpyElement *e)
{
	if (e->scalar)
		return PT(e->scalar);
	return e->message ? PD(e->message) : PD(e->enm);
}

/* _field_type / _extension_type */
static PbpyEl field_type(const PbpyFieldValue *v)
{
	switch (v->kind) {
	case PBPY_VALUE_MESSAGE: return PD(v->message);
	case PBPY_VALUE_SCALAR: return PT(v->scalar);
	case PBPY_VALUE_ENUM: return PD(v->enm);
	case PBPY_VALUE_LIST: {
		PbpyEls *l = els_new();
		pbpy_els_push(l, PS("list["));
		pbpy_els_push(l, element_el(&v->element));
		pbpy_els_push(l, PS("]"));
		return PL(l);
	}
	default: {
		PbpyEls *l = els_new();
		pbpy_els_push(l, PS("dict["));
		pbpy_els_push(l, PT(v->scalar));
		pbpy_els_push(l, PS(", "));
		pbpy_els_push(l, element_el(&v->element));
		pbpy_els_push(l, PS("]"));
		return PL(l);
	}
	}
}

/* _oneof_type */
static PbpyEl oneof_type(const PbpyDescOneof *o)
{
	PbpyEls *l = els_new();
	for (int i = 0; i < o->fields.n; i++) {
		const PbpyDescField *fd = o->fields.v[i];
		if (i)
			pbpy_els_push(l, PS(" | "));
		pbpy_els_push(l, PI(I.one_of));
		pbpy_els_push(l, PS("["));
		pbpy_els_push(l, PI(I.literal));
		pbpy_els_push(l, PS("[\""));
		pbpy_els_push(l, PS(fd->name));
		pbpy_els_push(l, PS("\"], "));
		pbpy_els_push(l, field_type(&fd->value));
		pbpy_els_push(l, PS("]"));
	}
	return PL(l);
}

static PbpyEl member_type(const void *member)
{
	if (((const PbpyDesc *)member)->kind == PBPY_ONEOF)
		return oneof_type(member);
	return field_type(&((const PbpyDescField *)member)->value);
}

static const char *member_local_name(const void *member)
{
	if (((const PbpyDesc *)member)->kind == PBPY_ONEOF)
		return ((const PbpyDescOneof *)member)->local_name;
	return ((const PbpyDescField *)member)->local_name;
}

/* _member_init_default */
static PbpyEl member_init_default(const void *member)
{
	const PbpyDescField *fd = member;
	if (fd->kind == PBPY_FIELD && fd->value.kind == PBPY_VALUE_SCALAR && fd->presence == PBPY_IMPLICIT) {
		PbpyEls *l = els_new();
		pbpy_els_push(l, PS(" = "));
		switch (fd->value.scalar) {
		case PT_BOOL: pbpy_els_push(l, PB(false)); break;
		case PT_STRING: pbpy_els_push(l, PS("\"\"")); break;
		case PT_BYTES: pbpy_els_push(l, PS("b\"\"")); break;
		default: pbpy_els_push(l, PN(0)); break;
		}
		return PL(l);
	}
	return PS(" | None = None");
}

/* ---- docstrings ---- */

static void opt_add(Vec *opts, const char *s) { vec_push(opts, (void *)s); }

static const char *element_type_name(const PbpyElement *e)
{
	if (e->scalar)
		return pbpy_scalar_name(e->scalar);
	return e->message ? e->message->type_name : e->enm->type_name;
}

/* _scalar_default_str (NULL: no default) */
static Str scalar_default_str(const PbpyFieldValue *v)
{
	if (!v->has_default)
		return (Str){NULL, 0};
	switch (v->scalar) {
	case PT_BOOL:
		return str_c(v->default_bool ? "true" : "false");
	case PT_STRING: {
		Buf b = {0};
		buf_addc(&b, '"');
		for (size_t i = 0; i < v->default_str.n; i++) {
			char c = v->default_str.p[i];
			switch (c) {
			case '\\': buf_adds(&b, "\\\\"); break;
			case '"': buf_adds(&b, "\\\""); break;
			case '\n': buf_adds(&b, "\\n"); break;
			case '\r': buf_adds(&b, "\\r"); break;
			case '\t': buf_adds(&b, "\\t"); break;
			default: buf_addc(&b, c);
			}
		}
		buf_addc(&b, '"');
		return buf_view(&b);
	}
	case PT_BYTES:
		return str_c(py_repr_bytes(v->default_str));
	default:
		return str_c(v->default_repr);
	}
}

/* The provenance line of a field (_field_provenance) or an extension (_extension_provenance). */
static Str field_provenance(const void *field)
{
	bool ext = ((const PbpyDesc *)field)->kind == PBPY_EXTENSION;
	const PbpyDescField *fd = field;
	const PbpyDescExtension *xd = field;
	const PbpyFieldValue *v = pbpy_field_value(field);
	const char *name = ext ? xd->name : fd->name;
	int presence = ext ? xd->presence : fd->presence;
	Str json = ext ? str_c(xd->json_name) : fd->json_name;
	const char *label = "";
	if (presence == PBPY_LEGACY_REQUIRED)
		label = "required ";
	else if (presence == PBPY_EXPLICIT && (ext || !v->oneof))
		label = "optional ";
	Vec pre = {0}, post = {0};
	const char *type_str;
	switch (v->kind) {
	case PBPY_VALUE_MAP:
		type_str = xcat("map<", pbpy_scalar_name(v->scalar), ", ", element_type_name(&v->element), ">");
		break;
	case PBPY_VALUE_LIST:
		type_str = xcat("repeated ", element_type_name(&v->element));
		if (v->packable)
			opt_add(&pre, v->packed ? "packed = true" : "packed = false");
		break;
	case PBPY_VALUE_SCALAR: {
		type_str = xcat(label, pbpy_scalar_name(v->scalar));
		Str d = scalar_default_str(v);
		if (d.p) {
			Buf b = {0};
			buf_adds(&b, "default = ");
			buf_add_str(&b, d);
			Str *s = xalloc(sizeof *s);
			*s = buf_view(&b);
			vec_push(&post, s);
		}
		break;
	}
	case PBPY_VALUE_MESSAGE:
		type_str = xcat(label, v->message->type_name);
		break;
	default: {
		type_str = xcat(label, v->enm->type_name);
		if (v->has_default) {
			const PbpyDescEnumValue *ev = map_get(&v->enm->values_by_number, xfmt("%d", v->default_enum));
			if (ev) {
				Str *s = xalloc(sizeof *s);
				*s = str_c(xcat("default = ", ev->name));
				vec_push(&post, s);
			}
		}
		break;
	}
	}
	Buf opts = {0};
	int n = 0;
	for (int i = 0; i < pre.n; i++, n++) {
		buf_adds(&opts, n ? ", " : "");
		buf_adds(&opts, pre.v[i]);
	}
	if (!str_eqc(json, pbpy_proto_camel_case(name))) {
		buf_adds(&opts, n++ ? ", " : "");
		buf_adds(&opts, "json_name = \"");
		buf_add_str(&opts, json);
		buf_addc(&opts, '"');
	}
	for (int i = 0; i < post.n; i++, n++) {
		buf_adds(&opts, n ? ", " : "");
		buf_add_str(&opts, *(Str *)post.v[i]);
	}
	if (ext ? xd->deprecated : fd->deprecated) {
		buf_adds(&opts, n++ ? ", " : "");
		buf_adds(&opts, "deprecated = true");
	}
	Buf b = {0};
	if (ext)
		buf_fmt(&b, "extend %s { ", xd->extendee->type_name);
	buf_fmt(&b, "%s %s = %lld", type_str, name, (long long)(ext ? xd->number : fd->number));
	if (n) {
		buf_adds(&b, " [");
		buf_add_str(&b, buf_view(&opts));
		buf_adds(&b, "]");
	}
	buf_adds(&b, ext ? " }" : ";");
	return buf_view(&b);
}

/* _provenance_text */
static Str provenance_text(const void *desc)
{
	const PbpyDesc *d = desc;
	switch (d->kind) {
	case PBPY_MESSAGE: {
		const PbpyDescMessage *m = desc;
		return str_c(xcat("message ", m->type_name, m->deprecated ? " [deprecated = true]" : ""));
	}
	case PBPY_ENUM: {
		const PbpyDescEnum *e = desc;
		return str_c(xcat("enum ", e->type_name, e->deprecated ? " [deprecated = true]" : ""));
	}
	case PBPY_ENUM_VALUE: {
		const PbpyDescEnumValue *v = desc;
		return str_c(xfmt("%s = %d%s", v->name, (int)v->number, v->deprecated ? " [deprecated = true]" : ""));
	}
	case PBPY_ONEOF:
		return str_c(xcat("oneof ", ((const PbpyDescOneof *)desc)->name));
	default:
		return field_provenance(desc);
	}
}

static void generate_docstring(PbpyFile *f, const void *desc)
{
	Str *lines;
	int n = pbpy_comment_lines(desc, &lines);
	if (n) {
		for (int i = 0; i < n; i++)
			pbpy_print(f, PSN(lines[i]));
		pbpy_println(f); /* separate comments from provenance */
	}
	pbpy_print(f, PS("```proto"));
	pbpy_print(f, PSN(provenance_text(desc)));
	pbpy_print(f, PS("```"));
}

/* _generate_message_docstring / _generate_enum_docstring */
static void generate_member_docstring(PbpyFile *f, const void *desc, const Vec *members)
{
	pbpy_doc_v(f, NULL);
	generate_docstring(f, desc);
	if (members->n) {
		pbpy_println(f);
		pbpy_scope(f, PS("Attributes:"));
		for (int i = 0; i < members->n; i++) {
			const void *m = members->v[i];
			const char *local = ((const PbpyDesc *)m)->kind == PBPY_ENUM_VALUE ? ((const PbpyDescEnumValue *)m)->local_name
											      : member_local_name(m);
			pbpy_scope(f, PS(local), PS(":"));
			generate_docstring(f, m);
			pbpy_end(f);
		}
		pbpy_end(f);
	}
	pbpy_doc_end(f);
	pbpy_println(f);
}

/* ---- deprecation ---- */

static PbpyEl deprecated_decorator(const char *name)
{
	PbpyEls *l = els_new();
	pbpy_els_push(l, PS("@"));
	pbpy_els_push(l, PS(DEPRECATED_ALIAS));
	pbpy_els_push(l, PS("(\""));
	pbpy_els_push(l, PS(name));
	pbpy_els_push(l, PS(" is deprecated.\", category=None)"));
	return PL(l);
}

static void generate_deprecated(PbpyFile *f, bool deprecated, const char *type_name)
{
	if (deprecated)
		pbpy_print(f, deprecated_decorator(type_name));
}

static void generate_deprecated_property(PbpyFile *f, const char *full_name, const char *name, const char *self_name,
	PbpyEl value_type, bool setter)
{
	pbpy_print(f, PS("@"), PS(PROPERTY_ALIAS));
	pbpy_print(f, deprecated_decorator(full_name));
	pbpy_print(f, PS("def "), PS(name), PS("("), PS(self_name), PS(") -> "), value_type, PS(": ..."));
	if (setter) {
		pbpy_print(f, PS("@"), PS(name), PS(".setter"));
		pbpy_print(f, deprecated_decorator(full_name));
		pbpy_print(f, PS("def "), PS(name), PS("("), PS(self_name), PS(", value: "), value_type, PS(") -> None: ..."));
	}
}

/* ---- messages ---- */

static void generate_enum(PbpyFile *f, const PbpyDescEnum *e);
static void generate_extension(PbpyFile *f, const PbpyDescExtension *x);

static void generate_init_signature(PbpyFile *f, const Vec *members, bool overload)
{
	pbpy_scope(f, PS("def __init__("));
	pbpy_print(f, PS("self,"));
	if (members->n)
		pbpy_print(f, PS("*,")); /* avoids positional arguments */
	for (int i = 0; i < members->n; i++) {
		const void *m = members->v[i];
		pbpy_print(f, PS(member_local_name(m)), PS(": "), member_type(m), member_init_default(m), PS(","));
	}
	pbpy_end(f);
	if (overload) {
		pbpy_print(f, PS(") -> None: ..."));
	} else {
		pbpy_scope(f, PS(") -> None:"));
		pbpy_print(f, PS("pass"));
		pbpy_end(f);
	}
}

static void generate_message_init(PbpyFile *f, const Vec *members)
{
	if (any_deprecated_field(members)) {
		/* a parameter cannot be deprecated: passing a deprecated field selects a deprecated overload */
		pbpy_print(f, PS("@"), PS(OVERLOAD_ALIAS));
		Vec kept = {0};
		for (int i = 0; i < members->n; i++)
			if (!is_deprecated_field(members->v[i]))
				vec_push(&kept, members->v[i]);
		generate_init_signature(f, &kept, true);
		pbpy_print(f, PS("@"), PS(OVERLOAD_ALIAS));
		pbpy_print(f, PS("@"), PS(DEPRECATED_ALIAS), PS("(\""), PS(DEPRECATED_INIT), PS("\", category=None)"));
		generate_init_signature(f, members, true);
	}
	generate_init_signature(f, members, false);
	pbpy_println(f);
}

static void generate_message_members(PbpyFile *f, const Vec *members)
{
	for (int i = 0; i < members->n; i++) {
		const void *m = members->v[i];
		const PbpyDescField *fd = m;
		bool allow_none = fd->kind == PBPY_ONEOF || fd->value.kind == PBPY_VALUE_MESSAGE;
		PbpyEls *type = els_new();
		pbpy_els_push(type, member_type(m));
		pbpy_els_push(type, PS(allow_none ? " | None" : ""));
		if (is_deprecated_field(m))
			generate_deprecated_property(f, xcat(fd->parent->type_name, ".", fd->name), fd->local_name, "self", PL(type), true);
		else
			pbpy_print(f, PS(member_local_name(m)), PS(": "), PL(type));
	}
	if (members->n)
		pbpy_println(f);
}

static void generate_message(PbpyFile *f, const PbpyDescMessage *msg)
{
	PbpyEls *field_names = els_new();
	Vec slots = {0};
	if (!msg->fields.n && !msg->oneofs.n) {
		pbpy_els_push(field_names, PI(I.no_return));
	} else {
		pbpy_els_push(field_names, PI(I.literal));
		pbpy_els_push(field_names, PS("["));
		for (int i = 0; i < msg->fields.n; i++) {
			const PbpyDescField *fd = msg->fields.v[i];
			if (i)
				pbpy_els_push(field_names, PS(", "));
			pbpy_els_push(field_names, PS(xcat("\"", fd->name, "\"")));
			bool singular = fd->value.kind == PBPY_VALUE_SCALAR || fd->value.kind == PBPY_VALUE_MESSAGE ||
				fd->value.kind == PBPY_VALUE_ENUM;
			if (!singular || !fd->value.oneof)
				vec_push(&slots, xcat("\"", fd->local_name, "\""));
		}
		for (int i = 0; i < msg->oneofs.n; i++)
			vec_push(&slots, xcat("\"", ((const PbpyDescOneof *)msg->oneofs.v[i])->local_name, "\""));
		pbpy_els_push(field_names, PS("]"));
	}
	PbpyIdent *field_names_var = pbpy_file_ident(f, xcat("_", msg->local_name, "Fields"), false);
	pbpy_print(f, PI(field_names_var), PS(": "), PI(I.type_alias), PS(" = "), PL(field_names));
	pbpy_println(f);

	/* consistently output a tuple by forcing a trailing comma for a single slot */
	const char *slots_str = slots.n == 1 ? xcat(slots.v[0], ",") : str_join(&slots, ", ");

	PbpyIdent *mixin = get_mixin(msg);
	PbpyEls *base = els_new();
	pbpy_els_push(base, PI(str_eq(msg->file->name, BOOT_FILE_PATH) ? I.boot_message : I.message));
	pbpy_els_push(base, PS("["));
	pbpy_els_push(base, PI(field_names_var));
	pbpy_els_push(base, PS(mixin ? "], " : "]"));
	if (mixin)
		pbpy_els_push(base, PI(mixin));

	generate_deprecated(f, msg->deprecated, msg->type_name);
	pbpy_scope(f, PS("class "), PI(pbpy_file_ident(f, msg->local_name, false)), PS("("), PL(base), PS("):"));
	generate_member_docstring(f, msg, &msg->members);
	if (any_deprecated_field(&msg->members)) {
		/* type checkers would resolve a deprecated field to its slot instead of its property */
		pbpy_scope(f, PS("if not "), PI(I.type_checking), PS(":"));
		pbpy_print(f, PS("__slots__ = ("), PS(slots_str), PS(")"));
		pbpy_end(f);
	} else {
		pbpy_print(f, PS("__slots__ = ("), PS(slots_str), PS(")"));
	}
	pbpy_println(f);
	pbpy_type_checking(f);
	pbpy_println(f);
	generate_message_init(f, &msg->members);
	generate_message_members(f, &msg->members);
	pbpy_type_checking_end(f);
	for (int i = 0; i < msg->nested_messages.n; i++)
		generate_message(f, msg->nested_messages.v[i]);
	for (int i = 0; i < msg->nested_enums.n; i++)
		generate_enum(f, msg->nested_enums.v[i]);
	for (int i = 0; i < msg->nested_extensions.n; i++)
		generate_extension(f, msg->nested_extensions.v[i]);
	pbpy_end(f);
}

/* ---- enums, extensions ---- */

static void generate_enum(PbpyFile *f, const PbpyDescEnum *e)
{
	PbpyEls *base = els_new();
	pbpy_els_push(base, PI(I.enm));
	bool any_deprecated = false;
	for (int i = 0; i < e->values.n; i++)
		any_deprecated = any_deprecated || ((const PbpyDescEnumValue *)e->values.v[i])->deprecated;
	if (any_deprecated) {
		/* type checkers report access to a deprecated metaclass property, the only way to mark an
		 * enum member as deprecated */
		PbpyIdent *meta = pbpy_file_ident(f, xcat("_", e->local_name, "Meta"), false);
		pbpy_type_checking(f);
		pbpy_scope(f, PS("class "), PI(meta), PS("("), PI(I.enum_meta), PS("):"));
		for (int i = 0; i < e->values.n; i++) {
			const PbpyDescEnumValue *v = e->values.v[i];
			if (v->deprecated)
				generate_deprecated_property(f, xcat(e->type_name, ".", v->name), v->local_name, "cls", PD(e), false);
		}
		pbpy_end(f);
		pbpy_type_checking_end(f);
		pbpy_scope(f, PS("else:"));
		pbpy_print(f, PI(meta), PS(" = "), PI(I.enm), PS(".__class__"));
		pbpy_end(f);
		pbpy_println(f);
		pbpy_els_push(base, PS(", metaclass="));
		pbpy_els_push(base, PI(meta));
	}
	generate_deprecated(f, e->deprecated, e->type_name);
	pbpy_scope(f, PS("class "), PI(pbpy_file_ident(f, e->local_name, false)), PS("("), PL(base), PS("):"));
	generate_member_docstring(f, e, &e->values);
	for (int i = 0; i < e->values.n; i++) {
		const PbpyDescEnumValue *v = e->values.v[i];
		if (v->deprecated) {
			/* type checkers would resolve the member instead of the metaclass property */
			pbpy_scope(f, PS("if not "), PI(I.type_checking), PS(":"));
			pbpy_print(f, PS(v->local_name), PS(" = "), PN(v->number));
			pbpy_end(f);
		} else {
			pbpy_print(f, PS(v->local_name), PS(" = "), PN(v->number));
		}
	}
	pbpy_end(f);
	pbpy_println(f);
}

static void generate_extension(PbpyFile *f, const PbpyDescExtension *x)
{
	PbpyEls *type = els_new();
	pbpy_els_push(type, PI(I.final));
	pbpy_els_push(type, PS("["));
	pbpy_els_push(type, PI(I.extension));
	pbpy_els_push(type, PS("["));
	pbpy_els_push(type, PD(x->extendee));
	pbpy_els_push(type, PS(", "));
	pbpy_els_push(type, field_type(&x->value));
	pbpy_els_push(type, PS("]]"));
	pbpy_print(f, PI(pbpy_file_ident(f, pbpy_escape_extension_name(x->name), false)), PS(": "), PL(type), PS(" = "),
		PI(I.extension), PS("()"));
	pbpy_doc_v(f, NULL);
	generate_docstring(f, x);
	pbpy_doc_end(f);
	pbpy_println(f);
}

/* ---- the file descriptor ---- */

static const char *stub_key(const void *desc)
{
	const PbpyDesc *d = desc;
	const char *type_name = d->kind == PBPY_MESSAGE ? ((const PbpyDescMessage *)desc)->type_name
		: d->kind == PBPY_ENUM ? ((const PbpyDescEnum *)desc)->type_name
		: ((const PbpyDescExtension *)desc)->type_name;
	const PbpyDescFile *file = d->kind == PBPY_MESSAGE ? ((const PbpyDescMessage *)desc)->file
		: d->kind == PBPY_ENUM ? ((const PbpyDescEnum *)desc)->file
		: ((const PbpyDescExtension *)desc)->file;
	const char *pkg = pb_str(file->proto, FDP_PACKAGE);
	if (!*pkg)
		return type_name;
	const char *prefix = xcat(pkg, ".");
	return has_prefix(type_name, prefix) ? type_name + strlen(prefix) : type_name;
}

static void generate_stub_map(PbpyFile *f, const PbpyDescFile *desc)
{
	pbpy_scope(f, PS("{"));
	Vec symbols = all_symbols(desc);
	for (int i = 0; i < symbols.n; i++)
		pbpy_print(f, PS("\""), PS(stub_key(symbols.v[i])), PS("\": "), PD(symbols.v[i]), PS(","));
	pbpy_end(f);
	pbpy_print(f, PS("},"));
}

static void repr_scalar(PbpyEls *l, const PbField *f, const PbpyFieldInfo *fi, PbVal v)
{
	switch (f->type) {
	case PT_STRING:
		pbpy_els_push(l, PS(py_repr_str(v.s)));
		return;
	case PT_BYTES:
		pbpy_els_push(l, PS(py_repr_bytes(v.s)));
		return;
	case PT_BOOL:
		pbpy_els_push(l, PS(v.u ? "True" : "False"));
		return;
	case PT_DOUBLE: {
		double d;
		memcpy(&d, &v.u, sizeof d);
		pbpy_els_push(l, PS(py_repr_float(d)));
		return;
	}
	case PT_UINT64: case PT_FIXED64:
		pbpy_els_push(l, PS(py_repr_uint(v.u)));
		return;
	case PT_ENUM:
		for (int i = 0; i < fi->enm->n; i++)
			if (fi->enm->v[i].number == (int32_t)v.u) {
				pbpy_els_push(l, PS(xcat(fi->enm->qualname, ".", fi->enm->v[i].name)));
				return;
			}
		fail("ValueError: %lld is not a valid %s", (long long)(int64_t)v.u, fi->enm->qualname);
	default:
		pbpy_els_push(l, PS(py_repr_int((int64_t)v.u)));
	}
}

/* _repr of a value of protobuf-py's descriptor.proto messages; info names the class of m (the
 * address of m->def does not: identical defs may be merged) */
static void repr_message(PbpyEls *l, const PbMsg *m, const PbpyMsgInfo *info, uint32_t unset_num)
{
	pbpy_els_push(l, PS(info->qualname));
	pbpy_els_push(l, PS("("));
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbpyFieldInfo *fi = &info->f[i];
		const PbSlot *s = &m->slots[i];
		bool set = s->n && f->num != unset_num;
		pbpy_els_push(l, PS(fi->local_name));
		pbpy_els_push(l, PS("="));
		if (f->label == PL_REP || f->label == PL_PACKED) {
			pbpy_els_push(l, PS("["));
			for (int k = 0; k < s->n; k++) {
				if (f->type == PT_MESSAGE)
					repr_message(l, s->v[k].m, &pbpy_descriptor_info[fi->msg], 0);
				else
					repr_scalar(l, f, fi, s->v[k]);
				pbpy_els_push(l, PS(","));
			}
			pbpy_els_push(l, PS("]"));
		} else if (f->type == PT_MESSAGE) {
			if (set)
				repr_message(l, s->v[0].m, &pbpy_descriptor_info[fi->msg], 0);
			else
				pbpy_els_push(l, PS("None"));
		} else if (!set) {
			pbpy_els_push(l, PI(I.unset));
			pbpy_els_push(l, PS("("));
			pbpy_els_push(l, PS(fi->default_repr));
			pbpy_els_push(l, PS(")"));
		} else {
			repr_scalar(l, f, fi, s->v[0]);
		}
		pbpy_els_push(l, PS(","));
	}
	pbpy_els_push(l, PS(")"));
}

/* _generate_boot: the code-generation counterpart of protobuf._codegen.boot() */
static void generate_boot(PbpyFile *f, const PbpyDescFile *desc)
{
	pbpy_scope(f, PS("_DESC = "), PI(I.boot), PS("("));
	/* the descriptor without source_code_info */
	PbpyEls *l = els_new();
	const PbpyMsgInfo *info = pbpy_descriptor_info;
	while (!str_eq(info->qualname, "FileDescriptorProto"))
		info++;
	repr_message(l, desc->proto, info, FDP_SOURCE_CODE_INFO);
	pbpy_print(f, PL(l), PS(","));
	generate_stub_map(f, desc);
	pbpy_end(f);
	pbpy_print(f, PS(")"));
}

static void generate_desc(PbpyFile *f, const PbpyDescFile *desc)
{
	pbpy_println(f);
	if (str_eq(desc->name, BOOT_FILE_PATH)) {
		generate_boot(f, desc);
	} else {
		pbpy_scope(f, PS("_DESC = "), PI(I.file_desc), PS("("));
		/* the FileDescriptorProto without source_code_info */
		Buf bytes = {0};
		pbpy_to_binary(desc->proto, FDP_SOURCE_CODE_INFO, &bytes);
		pbpy_print(f, PX(buf_view(&bytes)), PS(","));
		if (desc->dependencies.n) {
			pbpy_scope(f, PS("["));
			for (int i = 0; i < desc->dependencies.n; i++)
				pbpy_print(f, PD(desc->dependencies.v[i]), PS(".desc(),"));
			pbpy_end(f);
			pbpy_print(f, PS("],"));
		} else {
			pbpy_print(f, PS("[],"));
		}
		generate_stub_map(f, desc);
		pbpy_end(f);
		pbpy_print(f, PS(")"));
	}
	pbpy_print(f, PS(""));
	pbpy_print(f, PS(""));
	pbpy_scope(f, PS("def desc() -> "), PI(I.desc_file), PS(":"));
	pbpy_doc(f, PS(xcat("Returns the descriptor for the file `", desc->name, "`.")));
	pbpy_doc_end(f);
	pbpy_print(f, PS("return _DESC"));
	pbpy_end(f);
}

static void generate_file(PbpyFile *f, const PbpyDescFile *desc)
{
	pbpy_preamble(f, desc);
	generate_deprecated_aliases(f, desc);
	for (int i = 0; i < desc->messages.n; i++)
		generate_message(f, desc->messages.v[i]);
	for (int i = 0; i < desc->enums.n; i++)
		generate_enum(f, desc->enums.v[i]);
	for (int i = 0; i < desc->extensions.n; i++)
		generate_extension(f, desc->extensions.v[i]);
	generate_desc(f, desc);
}

static int cmp_str(const void *a, const void *b) { return py_str_cmp(str_c(*(const char *const *)a), str_c(*(const char *const *)b)); }

/* _generate_init_files: __init__.py for every package directory of the generated files (upstream
 * iterates a set: the response order differs between runs; here it is sorted). */
static void generate_init_files(PbpySchema *s, const Vec *generated_paths, const Map *generated)
{
	Map seen = {0};
	Vec dirs = {0};
	for (int i = 0; i < generated_paths->n; i++) {
		/* the parent directories from the longest: once one is known, so are its parents (a
		 * path only copies the directories it adds, plus one) */
		const char *path = generated_paths->v[i];
		for (size_t k = strlen(path); k-- > 1;) {
			if (path[k] != '/')
				continue;
			char *dir = xstrndup(path, k);
			if (map_has(&seen, dir))
				break;
			map_put(&seen, dir, (void *)1);
			vec_push(&dirs, dir);
		}
	}
	if (dirs.n > 1)
		qsort(dirs.v, (size_t)dirs.n, sizeof *dirs.v, cmp_str);
	bool generated_init = false;
	for (int i = 0; i < dirs.n; i++) {
		char *init_path = xcat(dirs.v[i], "/__init__.py");
		if (!map_has(generated, init_path)) {
			pbpy_generate_file_path(s, init_path);
			generated_init = true;
		}
	}
	/* always include a root __init__.py for the output directory */
	if (!map_has(generated, "__init__.py") && generated_init)
		pbpy_generate_file_path(s, "__init__.py");
}

static void generate(PbpySchema *s, const PbpyOptValue *options)
{
	init_idents();
	Vec paths = {0};
	Map generated = {0};
	const Vec *files = pbpy_files_to_generate(s);
	for (int i = 0; i < files->n; i++) {
		PbpyFile *f = pbpy_generate_file_desc(s, files->v[i], "_pb.py");
		generate_file(f, files->v[i]);
		const char *path = pbpy_file_path(f);
		if (!map_has(&generated, path)) {
			map_put(&generated, path, (void *)1);
			vec_push(&paths, (void *)path);
		}
	}
	if (options[0].b)
		generate_init_files(s, &paths, &generated);
}

/* _Options */
static const PbpyOptDef OPTIONS[] = {{.name = "init_files", .type = PBPY_OPT_BOOL, .default_bool = true}};

static const PbpyPlugin PLUGIN = {
	.name = "protoc-gen-py",
	.version = "0.7.0",
	.options = OPTIONS,
	.n_options = 1,
	.generate = generate,
};

static const char *uni_generate(PbMsg *req, Str raw, Buf *out)
{
	return pbpy_run(&PLUGIN, raw, pb_bytes(req, CGR_PARAMETER), out);
}

const UniGenerator gen_bufbuild_py_v0_7_0 = {
	.name = "bufbuild/py",
	.version = "v0.7.0",
	.generate = uni_generate,
	.parses_raw = true,
};
