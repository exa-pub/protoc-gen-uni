/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Descriptors built from FileDescriptorProto messages, ported from google.golang.org/protobuf
 * v1.36.12: reflect/protodesc/desc.go (NewFile), desc_init.go, desc_resolve.go, editions.go,
 * internal/filedesc/desc.go and desc_list.go (accessors), internal/encoding/defval (Unmarshal
 * with the Descriptor format), internal/encoding/text (string unquoting for bytes defaults),
 * internal/strs (JSONCamelCase) and reflect/protoregistry (Files.RegisterFile). */
#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

/* internal/errors.New: the "proto: " prefix (regular spaces; protobuf-go may choose no-break
 * spaces per binary). */
COLD const char *go_err(const char *fmt, ...)
{
	Buf b = {0};
	buf_adds(&b, "proto: ");
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	return buf_str(&b);
}

const char *go_quote(const char *s) { return quote_go(str_c(s)); }
const char *go_quote_str(Str s) { return quote_go(s); }

COLD char *go_utoa(uint64_t v)
{
	char t[24];
	int i = (int)sizeof t;
	t[--i] = 0;
	do
		t[--i] = (char)('0' + v % 10);
	while ((v /= 10) > 0);
	return xstrdup(t + i);
}

char *go_itoa(int64_t v) { return v < 0 ? xcat("-", go_utoa(0 - (uint64_t)v)) : go_utoa((uint64_t)v); }

static bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* protoreflect.Name.IsValid */
COLD bool go_name_is_valid(const char *s)
{
	if (!*s || !is_letter(*s))
		return false;
	for (s++; *s; s++)
		if (!is_letter(*s) && !is_digit(*s))
			return false;
	return true;
}

/* protoreflect.FullName.IsValid */
COLD bool go_full_name_is_valid(const char *s)
{
	if (!*s)
		return false;
	for (;;) {
		if (!is_letter(*s))
			return false;
		for (s++; is_letter(*s) || is_digit(*s); s++)
			;
		if (!*s)
			return true;
		if (*s != '.')
			return false;
		s++;
	}
}

static COLD const char *full_name_parent(const char *s)
{
	const char *dot = strrchr(s, '.');
	return dot ? xstrndup(s, (size_t)(dot - s)) : "";
}

static COLD const char *append_name(const char *prefix, const char *name)
{
	return *prefix ? xcat(prefix, ".", name) : name;
}

COLD const char *go_kind_string(int kind)
{
	static const char *const names[] = {
		"", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool", "string",
		"group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64",
	};
	if (kind >= 1 && kind <= 18)
		return names[kind];
	return xcat("<unknown:", go_itoa(kind), ">");
}

COLD const char *go_cardinality_string(int c)
{
	switch (c) {
	case GO_OPTIONAL: return "optional";
	case GO_REQUIRED: return "required";
	case GO_REPEATED: return "repeated";
	default: return xcat("<unknown:", go_itoa(c), ">");
	}
}

COLD const char *go_edition_string(int e)
{
	switch (e) {
	case 0: return "EDITION_UNKNOWN";
	case 900: return "EDITION_LEGACY";
	case 998: return "EDITION_PROTO2";
	case 999: return "EDITION_PROTO3";
	case 1000: return "EDITION_2023";
	case 1001: return "EDITION_2024";
	case 1: return "EDITION_1_TEST_ONLY";
	case 2: return "EDITION_2_TEST_ONLY";
	case 99997: return "EDITION_99997_TEST_ONLY";
	case 99998: return "EDITION_99998_TEST_ONLY";
	case 99999: return "EDITION_99999_TEST_ONLY";
	case 9999: return "EDITION_UNSTABLE";
	case 2147483647: return "EDITION_MAX";
	default: return go_itoa(e);
	}
}

/* ---- filedesc accessors ---- */

COLD bool go_field_has_presence(const GoField *f)
{
	if (f->cardinality == GO_REPEATED)
		return false;
	if (f->is_extension)
		return true;
	return f->features.IsFieldPresence || f->message_type || f->oneof;
}

COLD bool go_field_is_packed(const GoField *f)
{
	if (f->cardinality != GO_REPEATED)
		return false;
	switch (f->kind) {
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: case PT_GROUP: return false;
	}
	return f->features.IsPacked;
}

COLD bool go_field_has_optional_keyword(const GoField *f)
{
	if (f->is_extension)
		return (f->d.file->syntax == GO_PROTO2 && f->cardinality == GO_OPTIONAL) || f->is_proto3_optional;
	return (f->d.file->syntax == GO_PROTO2 && f->cardinality == GO_OPTIONAL && !f->oneof) || f->is_proto3_optional;
}

COLD bool go_oneof_is_synthetic(const GoOneof *o)
{
	return o->d.file->syntax == GO_PROTO3 && o->n_fields == 1 && go_field_has_optional_keyword(o->fields[0]);
}

COLD GoEnumValue *go_enum_value_by_number(const GoEnum *e, int32_t n)
{
	for (int i = 0; i < e->n_values; i++)
		if (e->values[i]->number == n)
			return e->values[i];
	return NULL;
}

COLD GoEnumValue *go_enum_value_by_name(const GoEnum *e, const char *name)
{
	for (int i = 0; i < e->n_values; i++)
		if (str_eq(e->values[i]->d.name, name))
			return e->values[i];
	return NULL;
}

static COLD const char *path_key(const int32_t *path, int n)
{
	Buf b = {0};
	buf_addc(&b, '/');
	for (int i = 0; i < n; i++)
		buf_adds(&b, xcat(go_itoa(path[i]), "/"));
	return buf_str(&b);
}

COLD GoSourceLocation go_source_location_by_path(const GoFile *f, const int32_t *path, int n)
{
	GoSourceLocation *l = map_get(&f->locs_by_path, path_key(path, n));
	if (l)
		return *l;
	return (GoSourceLocation){0};
}

COLD int32_t *go_desc_source_path(const GoDesc *d, int *n)
{
	int32_t rev[256];
	int k = 0;
	for (; d && d->kind != GD_FILE && k < 254; d = d->parent) {
		int num;
		switch (d->kind) {
		case GD_MESSAGE:
			num = d->parent->kind == GD_FILE ? GENID_FileDescriptorProto_MessageType_field_number
							 : GENID_DescriptorProto_NestedType_field_number;
			break;
		case GD_FIELD:
			if (((const GoField *)d)->is_extension)
				num = d->parent->kind == GD_FILE ? GENID_FileDescriptorProto_Extension_field_number
								 : GENID_DescriptorProto_Extension_field_number;
			else
				num = GENID_DescriptorProto_Field_field_number;
			break;
		case GD_ONEOF: num = GENID_DescriptorProto_OneofDecl_field_number; break;
		case GD_ENUM:
			num = d->parent->kind == GD_FILE ? GENID_FileDescriptorProto_EnumType_field_number
							 : GENID_DescriptorProto_EnumType_field_number;
			break;
		case GD_ENUM_VALUE: num = GENID_EnumDescriptorProto_Value_field_number; break;
		case GD_SERVICE: num = GENID_FileDescriptorProto_Service_field_number; break;
		case GD_METHOD: num = GENID_ServiceDescriptorProto_Method_field_number; break;
		default: num = 0;
		}
		rev[k++] = d->index;
		rev[k++] = num;
	}
	int32_t *p = xalloc(sizeof *p * (size_t)(k ? k : 1));
	for (int i = 0; i < k; i++)
		p[i] = rev[k - 1 - i];
	*n = k;
	return p;
}

COLD GoSourceLocation go_source_location_by_desc(const GoFile *f, const GoDesc *d)
{
	if (d->file != f)
		return (GoSourceLocation){0};
	int n;
	int32_t *path = go_desc_source_path(d, &n);
	return go_source_location_by_path(f, path, n);
}

/* ---- registry ---- */

struct GoRegistry {
	Map files;    /* path -> GoFile* */
	Map names;    /* every declaration of a registered file: full name -> GoDesc* */
	Map top;      /* top-level names: packages (-> &pkg_marker) and top-level declarations */
};

static char pkg_marker;

GoRegistry *go_registry_new(void) { return xalloc(sizeof(GoRegistry)); }

GoFile *go_registry_find_file(const GoRegistry *r, const char *path) { return map_get(&r->files, path); }

/* ---- NewFile ---- */

typedef struct {
	GoRegistry *r;
	GoFile *f;
	Map local;    /* full name -> GoDesc* of this file */
	Map imports;  /* importSet: paths -> non-NULL */
	Vec all;      /* every declaration of this file */
} Ctx;

static COLD const char *make_base(Ctx *c, GoDesc *d, GoDescKind kind, GoDesc *parent, Str sname, int idx, PbMsg *options)
{
	const char *name = sname.p;
	if (memchr(sname.p, 0, sname.n) || !go_name_is_valid(name))
		return go_err("descriptor %s has an invalid nested name: %s", go_quote(parent->full_name), go_quote_str(sname));
	const char *full = kind == GD_ENUM_VALUE ? append_name(full_name_parent(parent->full_name), name)
						  : append_name(parent->full_name, name);
	if (map_has(&c->local, full))
		return go_err("descriptor %s already declared", go_quote(full));
	map_put(&c->local, full, d);
	vec_push(&c->all, d);
	d->kind = kind;
	d->name = name;
	d->full_name = full;
	d->index = idx;
	d->parent = parent;
	d->file = c->f;
	d->options = options;
	return NULL;
}

static COLD GoEditionFeatures parent_features(const GoDesc *parent)
{
	return parent->kind == GD_FILE ? ((const GoFile *)parent)->features : ((const GoMessage *)parent)->features;
}

static const PbMsg *features_of(const PbMsg *options, uint32_t num) { return pb_msg(options, num); }

#define NEW_LIST(T, n) ((T **)xalloc(sizeof(T *) * (size_t)((n) ? (n) : 1)))

static COLD const char *init_enums(Ctx *c, GoDesc *parent, PbMsg *owner, uint32_t num, GoEnum ***out, int *nout)
{
	int n = pb_count(owner, num);
	GoEnum **es = NEW_LIST(GoEnum, n);
	*out = es, *nout = n;
	for (int i = 0; i < n; i++) {
		PbMsg *ed = pb_msg_at(owner, num, i);
		GoEnum *e = es[i] = xalloc(sizeof *e);
		PbMsg *opts = pb_msg(ed, GENID_EnumDescriptorProto_Options_field_number);
		const char *err = make_base(c, &e->d, GD_ENUM, parent, pb_bytes(ed, GENID_EnumDescriptorProto_Name_field_number), i, opts);
		if (err)
			return err;
		e->features = features_merge(parent_features(parent), features_of(opts, GENID_EnumOptions_Features_field_number));
		e->visibility = (int)pb_int(ed, GENID_EnumDescriptorProto_Visibility_field_number, 0);
		int nv = pb_count(ed, GENID_EnumDescriptorProto_Value_field_number);
		e->values = NEW_LIST(GoEnumValue, nv);
		e->n_values = nv;
		for (int j = 0; j < nv; j++) {
			PbMsg *vd = pb_msg_at(ed, GENID_EnumDescriptorProto_Value_field_number, j);
			GoEnumValue *v = e->values[j] = xalloc(sizeof *v);
			err = make_base(c, &v->d, GD_ENUM_VALUE, &e->d, pb_bytes(vd, GENID_EnumValueDescriptorProto_Name_field_number), j,
					pb_msg(vd, GENID_EnumValueDescriptorProto_Options_field_number));
			if (err)
				return err;
			v->number = (int32_t)pb_int(vd, GENID_EnumValueDescriptorProto_Number_field_number, 0);
		}
	}
	return NULL;
}

/* JSONCamelCase (internal/strs) */
static COLD Str json_camel_case(const char *s)
{
	Buf b = {0};
	bool was_underscore = false;
	for (; *s; s++) {
		char ch = *s;
		if (ch != '_') {
			if (was_underscore && ch >= 'a' && ch <= 'z')
				ch = (char)(ch - 'a' + 'A');
			buf_addc(&b, ch);
		}
		was_underscore = ch == '_';
	}
	return buf_view(&b);
}

static COLD const char *init_field(Ctx *c, GoField *f, GoDesc *parent, PbMsg *fd, int idx, bool is_ext)
{
	PbMsg *opts = pb_msg(fd, GENID_FieldDescriptorProto_Options_field_number);
	const char *err = make_base(c, &f->d, GD_FIELD, parent, pb_bytes(fd, GENID_FieldDescriptorProto_Name_field_number), idx, opts);
	if (err)
		return err;
	f->proto = fd;
	f->is_extension = is_ext;
	f->features = features_merge(parent_features(parent), features_of(opts, GENID_FieldOptions_Features_field_number));
	f->is_proto3_optional = pb_int(fd, GENID_FieldDescriptorProto_Proto3Optional_field_number, 0) != 0;
	if (opts) {
		if (!is_ext)
			f->is_lazy = pb_int(opts, GENID_FieldOptions_Lazy_field_number, 0) != 0;
		if (pb_has(opts, GENID_FieldOptions_Packed_field_number))
			f->features.IsPacked = pb_int(opts, GENID_FieldOptions_Packed_field_number, 0) != 0;
	}
	f->number = (int32_t)pb_int(fd, GENID_FieldDescriptorProto_Number_field_number, 0);
	/* protoreflect.Cardinality and Kind are int8 */
	f->cardinality = (int8_t)pb_int(fd, GENID_FieldDescriptorProto_Label_field_number, LABEL_OPTIONAL);
	if (pb_has(fd, GENID_FieldDescriptorProto_Type_field_number))
		f->kind = (int8_t)pb_int(fd, GENID_FieldDescriptorProto_Type_field_number, 0);
	if (pb_has(fd, GENID_FieldDescriptorProto_JsonName_field_number)) {
		f->has_json_name = true;
		f->json_name = pb_bytes(fd, GENID_FieldDescriptorProto_JsonName_field_number);
	}
	if (!is_ext && f->features.IsLegacyRequired)
		f->cardinality = GO_REQUIRED;
	if (f->kind == PT_MESSAGE && f->features.IsDelimitedEncoded)
		f->kind = PT_GROUP;
	if (is_ext)
		f->json_name = str_c(xcat("[", f->d.full_name, "]"));
	else if (!f->has_json_name)
		f->json_name = json_camel_case(f->d.name);
	return NULL;
}

static COLD const char *init_extensions(Ctx *c, GoDesc *parent, PbMsg *owner, uint32_t num, GoField ***out, int *nout)
{
	int n = pb_count(owner, num);
	GoField **xs = NEW_LIST(GoField, n);
	*out = xs, *nout = n;
	for (int i = 0; i < n; i++) {
		xs[i] = xalloc(sizeof(GoField));
		const char *err = init_field(c, xs[i], parent, pb_msg_at(owner, num, i), i, true);
		if (err)
			return err;
	}
	return NULL;
}

static COLD const char *init_messages(Ctx *c, GoDesc *parent, PbMsg *owner, uint32_t num, GoMessage ***out, int *nout)
{
	int n = pb_count(owner, num);
	GoMessage **ms = NEW_LIST(GoMessage, n);
	*out = ms, *nout = n;
	for (int i = 0; i < n; i++) {
		PbMsg *md = pb_msg_at(owner, num, i);
		GoMessage *m = ms[i] = xalloc(sizeof *m);
		PbMsg *opts = pb_msg(md, GENID_DescriptorProto_Options_field_number);
		const char *err = make_base(c, &m->d, GD_MESSAGE, parent, pb_bytes(md, GENID_DescriptorProto_Name_field_number), i, opts);
		if (err)
			return err;
		m->proto = md;
		m->features = features_merge(parent_features(parent), features_of(opts, GENID_MessageOptions_Features_field_number));
		m->visibility = (int)pb_int(md, GENID_DescriptorProto_Visibility_field_number, 0);
		m->is_map_entry = pb_int(opts, GENID_MessageOptions_MapEntry_field_number, 0) != 0;
		m->is_message_set = pb_int(opts, GENID_MessageOptions_MessageSetWireFormat_field_number, 0) != 0;
		int nr = pb_count(md, GENID_DescriptorProto_ExtensionRange_field_number);
		m->ext_ranges = xalloc(sizeof *m->ext_ranges * (size_t)(nr ? nr : 1));
		m->n_ext_ranges = nr;
		for (int j = 0; j < nr; j++) {
			PbMsg *xr = pb_msg_at(md, GENID_DescriptorProto_ExtensionRange_field_number, j);
			m->ext_ranges[j][0] = (int32_t)pb_int(xr, GENID_DescriptorProto_ExtensionRange_Start_field_number, 0);
			m->ext_ranges[j][1] = (int32_t)pb_int(xr, GENID_DescriptorProto_ExtensionRange_End_field_number, 0);
		}
		int nf = pb_count(md, GENID_DescriptorProto_Field_field_number);
		m->fields = NEW_LIST(GoField, nf);
		m->n_fields = nf;
		for (int j = 0; j < nf; j++) {
			m->fields[j] = xalloc(sizeof(GoField));
			err = init_field(c, m->fields[j], &m->d, pb_msg_at(md, GENID_DescriptorProto_Field_field_number, j), j, false);
			if (err)
				return err;
			m->fields[j]->containing = m;
		}
		int no = pb_count(md, GENID_DescriptorProto_OneofDecl_field_number);
		m->oneofs = NEW_LIST(GoOneof, no);
		m->n_oneofs = no;
		for (int j = 0; j < no; j++) {
			PbMsg *od = pb_msg_at(md, GENID_DescriptorProto_OneofDecl_field_number, j);
			GoOneof *o = m->oneofs[j] = xalloc(sizeof *o);
			PbMsg *oopts = pb_msg(od, GENID_OneofDescriptorProto_Options_field_number);
			err = make_base(c, &o->d, GD_ONEOF, &m->d, pb_bytes(od, GENID_OneofDescriptorProto_Name_field_number), j, oopts);
			if (err)
				return err;
			o->features = features_merge(m->features, features_of(oopts, GENID_OneofOptions_Features_field_number));
			o->fields = NEW_LIST(GoField, nf);
		}
		if ((err = init_enums(c, &m->d, md, GENID_DescriptorProto_EnumType_field_number, &m->enums, &m->n_enums)))
			return err;
		if ((err = init_messages(c, &m->d, md, GENID_DescriptorProto_NestedType_field_number, &m->messages, &m->n_messages)))
			return err;
		if ((err = init_extensions(c, &m->d, md, GENID_DescriptorProto_Extension_field_number, &m->extensions, &m->n_extensions)))
			return err;
	}
	return NULL;
}

static COLD const char *init_services(Ctx *c, GoFile *f)
{
	PbMsg *fd = f->proto;
	int n = pb_count(fd, GENID_FileDescriptorProto_Service_field_number);
	f->services = NEW_LIST(GoService, n);
	f->n_services = n;
	for (int i = 0; i < n; i++) {
		PbMsg *sd = pb_msg_at(fd, GENID_FileDescriptorProto_Service_field_number, i);
		GoService *s = f->services[i] = xalloc(sizeof *s);
		const char *err = make_base(c, &s->d, GD_SERVICE, &f->d, pb_bytes(sd, GENID_ServiceDescriptorProto_Name_field_number), i,
					    pb_msg(sd, GENID_ServiceDescriptorProto_Options_field_number));
		if (err)
			return err;
		int nm = pb_count(sd, GENID_ServiceDescriptorProto_Method_field_number);
		s->methods = NEW_LIST(GoMethod, nm);
		s->n_methods = nm;
		for (int j = 0; j < nm; j++) {
			PbMsg *md = pb_msg_at(sd, GENID_ServiceDescriptorProto_Method_field_number, j);
			GoMethod *m = s->methods[j] = xalloc(sizeof *m);
			err = make_base(c, &m->d, GD_METHOD, &s->d, pb_bytes(md, GENID_MethodDescriptorProto_Name_field_number), j,
					pb_msg(md, GENID_MethodDescriptorProto_Options_field_number));
			if (err)
				return err;
			m->client_streaming = pb_int(md, GENID_MethodDescriptorProto_ClientStreaming_field_number, 0) != 0;
			m->server_streaming = pb_int(md, GENID_MethodDescriptorProto_ServerStreaming_field_number, 0) != 0;
		}
	}
	return NULL;
}

/* ---- resolution ---- */

typedef struct {
	const char *ref; /* without the leading dot if full */
	bool full;
} PartialName;

static COLD bool partial_is_valid(const char *s)
{
	if (*s == '.')
		return go_full_name_is_valid(s + 1);
	return go_full_name_is_valid(s);
}

static const char *partial_full_name(const char *s) { return *s == '.' ? s + 1 : xcat("*.", s); }

/* findDescriptor: *out = NULL and no error means not found. */
static const char *find_descriptor(Ctx *c, const char *scope, Str sref, GoDesc **out)
{
	*out = NULL;
	const char *ref = sref.p;
	if (memchr(sref.p, 0, sref.n) || !partial_is_valid(ref))
		return go_err("invalid name reference: %s", go_quote_str(sref));
	if (*ref == '.')
		scope = "", ref++;
	GoDesc *found_not_imported = NULL;
	for (;;) {
		const char *s = *scope ? xcat(scope, ".", ref) : ref;
		GoDesc *d = map_get(&c->local, s);
		if (d) {
			*out = d;
			return NULL;
		}
		d = map_get(&c->r->names, s);
		if (d) {
			if (map_has(&c->imports, d->file->d.name)) {
				*out = d;
				return NULL;
			}
			found_not_imported = d;
		}
		if (!*scope) {
			if (found_not_imported)
				return go_err("resolved %s, but %s is not imported", go_quote(found_not_imported->full_name),
					      go_quote(found_not_imported->file->d.name));
			return NULL;
		}
		scope = full_name_parent(scope);
	}
}

static const char *find_enum(Ctx *c, const char *scope, Str ref, GoEnum **out)
{
	GoDesc *d;
	const char *err = find_descriptor(c, scope, ref, &d);
	if (err)
		return err;
	if (!d)
		return go_err("%s not found", go_quote(partial_full_name(ref.p)));
	if (d->kind != GD_ENUM)
		return go_err("resolved %s, but it is not an enum", go_quote(d->full_name));
	*out = (GoEnum *)d;
	return NULL;
}

static const char *find_message(Ctx *c, const char *scope, Str ref, GoMessage **out)
{
	GoDesc *d;
	const char *err = find_descriptor(c, scope, ref, &d);
	if (err)
		return err;
	if (!d)
		return go_err("%s not found", go_quote(partial_full_name(ref.p)));
	if (d->kind != GD_MESSAGE)
		return go_err("resolved %s, but it is not an message", go_quote(d->full_name));
	*out = (GoMessage *)d;
	return NULL;
}

/* findTarget */
static COLD const char *find_target(Ctx *c, GoField *f, const char *scope, Str ref)
{
	switch (f->kind) {
	case PT_ENUM:
		return find_enum(c, scope, ref, &f->enum_type);
	case PT_MESSAGE:
	case PT_GROUP:
		return find_message(c, scope, ref, &f->message_type);
	case 0: {
		GoDesc *d;
		const char *err = find_descriptor(c, scope, ref, &d);
		if (err)
			return err;
		if (!d)
			return go_err("%s not found", go_quote(partial_full_name(ref.p)));
		if (d->kind == GD_ENUM) {
			f->kind = PT_ENUM;
			f->enum_type = (GoEnum *)d;
		} else if (d->kind == GD_MESSAGE) {
			f->kind = PT_MESSAGE;
			f->message_type = (GoMessage *)d;
		} else {
			return go_err("unknown kind");
		}
		return NULL;
	}
	default:
		if (ref.n)
			return go_err("target name cannot be specified for %s", go_kind_string(f->kind));
		if (f->kind < 1 || f->kind > 18)
			return go_err("invalid kind: %d", f->kind);
		return NULL;
	}
}

/* ---- default values (defval.Unmarshal, Descriptor format) ---- */

/* strconv.ParseInt/ParseUint with base 0 and the given bit size. */
static COLD bool parse_uint_base0(const char *s, int bits, uint64_t *out)
{
	if (!*s)
		return false;
	int base = 10;
	const char *p = s;
	bool prefixed = false;
	if (p[0] == '0') {
		if (p[1] == 'x' || p[1] == 'X')
			base = 16, p += 2, prefixed = true;
		else if (p[1] == 'b' || p[1] == 'B')
			base = 2, p += 2, prefixed = true;
		else if (p[1] == 'o' || p[1] == 'O')
			base = 8, p += 2, prefixed = true;
		else
			base = 8, p += 1, prefixed = true;
	}
	(void)prefixed;
	/* underscores: only allowed with a base prefix, between digits (underscoreOK) */
	bool underscores = false;
	uint64_t max = bits == 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1;
	uint64_t v = 0;
	bool any = base == 8 && p == s + 1; /* "0" alone */
	for (; *p; p++) {
		int d;
		char ch = *p;
		if (ch == '_') {
			underscores = true;
			continue;
		}
		if (ch >= '0' && ch <= '9')
			d = ch - '0';
		else if (ch >= 'a' && ch <= 'z')
			d = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'Z')
			d = ch - 'A' + 10;
		else
			return false;
		if (d >= base)
			return false;
		if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base)
			return false;
		v = v * (uint64_t)base + (uint64_t)d;
		if (v > max)
			return false;
		any = true;
	}
	if (!any)
		return false;
	if (underscores) {
		/* underscoreOK: base prefix required, '_' must separate digits */
		if (s[0] != '0')
			return false;
		const char *q = s;
		char saw = '^';
		if (q[0] == '0' && q[1] && strchr("xXbBoO", q[1]))
			q += 2, saw = '0';
		else
			q += 1, saw = '0';
		for (; *q; q++) {
			if (*q == '_') {
				if (saw != '0')
					return false;
				saw = '_';
			} else {
				if (saw == '^')
					return false;
				saw = '0';
			}
		}
		if (saw == '_')
			return false;
	}
	*out = v;
	return true;
}

static COLD bool parse_int_base0(const char *s, int bits, int64_t *out)
{
	bool neg = false;
	if (*s == '+' || *s == '-')
		neg = *s == '-', s++;
	uint64_t u;
	if (!parse_uint_base0(s, 64, &u))
		return false;
	uint64_t cutoff = (uint64_t)1 << (bits - 1);
	if (!neg && u >= cutoff)
		return false;
	if (neg && u > cutoff)
		return false;
	*out = neg ? (int64_t)(0 - u) : (int64_t)u;
	return true;
}

/* strconv.ParseFloat(s, 64) for the syntax protoc writes into default_value (decimal and
 * special values); hexadecimal floats and underscores follow strtod. */
static COLD bool parse_float(const char *s, double *out)
{
	if (!*s || strchr(s, '_'))
		return false;
	const char *p = s;
	if (*p == '+' || *p == '-')
		p++;
	/* Go accepts inf, +inf, -inf, infinity, nan (any case); strtod accepts the same plus
	 * "nan(...)", which Go rejects. */
	if (strchr(p, '(') || *p == ' ' || *p == '\t' || *p == '\n')
		return false;
	if ((p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) && !strpbrk(p, "pP"))
		return false; /* Go requires the p exponent in hex floats */
	char *end;
	errno = 0;
	double v = strtod(s, &end);
	if (*end || end == s)
		return false;
	if (errno == ERANGE && isinf(v))
		return false;
	*out = v;
	return true;
}

/* internal/encoding/text: the body of a double-quoted string. */
static COLD bool unquote_bytes(Str s, Str *out)
{
	Buf b = {0};
	size_t i = 0;
	while (i < s.n) {
		unsigned char ch = (unsigned char)s.p[i];
		if (ch == '"' || ch == '\n' || ch == 0)
			return false;
		if (ch != '\\') {
			/* invalid UTF-8 is an error in the text format */
			uint32_t r;
			int w = utf8_decode(s.p + i, s.n - i, &r);
			if (r == 0xfffd && w == 1)
				return false;
			buf_add(&b, s.p + i, (size_t)w);
			i += (size_t)w;
			continue;
		}
		if (++i >= s.n)
			return false;
		char e = s.p[i++];
		switch (e) {
		case '"': buf_addc(&b, '"'); break;
		case '\'': buf_addc(&b, '\''); break;
		case '\\': buf_addc(&b, '\\'); break;
		case '?': buf_addc(&b, '?'); break;
		case 'a': buf_addc(&b, '\a'); break;
		case 'b': buf_addc(&b, '\b'); break;
		case 'n': buf_addc(&b, '\n'); break;
		case 'r': buf_addc(&b, '\r'); break;
		case 't': buf_addc(&b, '\t'); break;
		case 'v': buf_addc(&b, '\v'); break;
		case 'f': buf_addc(&b, '\f'); break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			/* one to three octal digits */
			unsigned v = (unsigned)(e - '0');
			int k = 1;
			while (k < 3 && i < s.n && s.p[i] >= '0' && s.p[i] <= '7')
				v = v * 8 + (unsigned)(s.p[i++] - '0'), k++;
			if (v > 255)
				return false;
			buf_addc(&b, (char)v);
			break;
		}
		case 'x': case 'X': {
			/* one or two hex digits */
			unsigned v = 0;
			int k = 0;
			while (k < 2 && i < s.n && strchr("0123456789abcdefABCDEF", s.p[i]) && s.p[i]) {
				char h = s.p[i++];
				v = v * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
				k++;
			}
			if (!k)
				return false;
			buf_addc(&b, (char)v);
			break;
		}
		case 'u': case 'U': {
			int k = e == 'u' ? 4 : 8;
			if (i + (size_t)k > s.n)
				return false;
			uint32_t v = 0;
			for (int j = 0; j < k; j++) {
				char h = s.p[i + (size_t)j];
				if (!h || !strchr("0123456789abcdefABCDEF", h))
					return false;
				v = v * 16 + (uint32_t)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
			}
			i += (size_t)k;
			if (e == 'u' && v >= 0xd800 && v < 0xdc00) {
				/* a surrogate pair */
				if (i + 6 > s.n || s.p[i] != '\\' || s.p[i + 1] != 'u')
					return false;
				uint32_t lo = 0;
				for (int j = 0; j < 4; j++) {
					char h = s.p[i + 2 + (size_t)j];
					if (!h || !strchr("0123456789abcdefABCDEF", h))
						return false;
					lo = lo * 16 + (uint32_t)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
				}
				if (lo < 0xdc00 || lo >= 0xe000)
					return false;
				v = 0x10000 + ((v - 0xd800) << 10) + (lo - 0xdc00);
				i += 6;
			} else if (v > 0x10ffff || (v >= 0xd800 && v < 0xe000)) {
				return false;
			}
			char u[4];
			int w;
			if (v < 0x80)
				u[0] = (char)v, w = 1;
			else if (v < 0x800)
				u[0] = (char)(0xc0 | v >> 6), u[1] = (char)(0x80 | (v & 0x3f)), w = 2;
			else if (v < 0x10000)
				u[0] = (char)(0xe0 | v >> 12), u[1] = (char)(0x80 | ((v >> 6) & 0x3f)), u[2] = (char)(0x80 | (v & 0x3f)), w = 3;
			else
				u[0] = (char)(0xf0 | v >> 18), u[1] = (char)(0x80 | ((v >> 12) & 0x3f)),
				u[2] = (char)(0x80 | ((v >> 6) & 0x3f)), u[3] = (char)(0x80 | (v & 0x3f)), w = 4;
			buf_add(&b, u, (size_t)w);
			break;
		}
		default:
			return false;
		}
	}
	*out = buf_view(&b);
	if (!out->p)
		*out = (Str){"", 0};
	return true;
}

static COLD const char *unmarshal_default(GoField *f, Str s)
{
	GoValue v = {0};
	GoEnumValue *ev = NULL;
	bool ok = false;
	const char *cs = s.p; /* NUL-terminated copy; a NUL inside fails the numeric parses */
	bool has_nul = memchr(s.p, 0, s.n) != NULL;
	switch (f->kind) {
	case PT_BOOL:
		if (str_eqc(s, "true"))
			v.b = true, ok = true;
		else if (str_eqc(s, "false"))
			v.b = false, ok = true;
		break;
	case PT_ENUM:
		if (f->enum_type && !has_nul && (ev = go_enum_value_by_name(f->enum_type, cs)))
			v.i = ev->number, ok = true;
		break;
	case PT_INT32: case PT_SINT32: case PT_SFIXED32:
		ok = !has_nul && parse_int_base0(cs, 32, &v.i);
		break;
	case PT_INT64: case PT_SINT64: case PT_SFIXED64:
		ok = !has_nul && parse_int_base0(cs, 64, &v.i);
		break;
	case PT_UINT32: case PT_FIXED32:
		ok = !has_nul && parse_uint_base0(cs, 32, &v.u);
		break;
	case PT_UINT64: case PT_FIXED64:
		ok = !has_nul && parse_uint_base0(cs, 64, &v.u);
		break;
	case PT_FLOAT:
	case PT_DOUBLE:
		if (str_eqc(s, "-inf"))
			v.f = -INFINITY, ok = true;
		else if (str_eqc(s, "inf"))
			v.f = INFINITY, ok = true;
		else if (str_eqc(s, "nan"))
			v.f = NAN, ok = true;
		else
			ok = !has_nul && parse_float(cs, &v.f);
		if (ok && f->kind == PT_FLOAT)
			v.f = (double)(float)v.f;
		break;
	case PT_STRING:
		v.s = s, ok = true;
		break;
	case PT_BYTES:
		ok = unquote_bytes(s, &v.s);
		break;
	}
	if (!ok)
		return go_err("could not parse value for %s: %s", go_kind_string(f->kind), go_quote_str(s));
	if (!go_field_has_presence(f))
		return go_err("cannot be specified with implicit field presence");
	if (f->kind == PT_MESSAGE || f->kind == PT_GROUP || f->cardinality == GO_REPEATED)
		return go_err("cannot be specified on composite types");
	f->has_default = true;
	f->default_value = v;
	f->default_enum = ev;
	return NULL;
}

/* strips the "proto: " prefix of a nested error (internal/errors format chaining) */
static const char *inner(const char *err) { return has_prefix(err, "proto: ") ? err + 7 : err; }

static COLD const char *resolve_extensions(Ctx *c, GoField **xs, int n)
{
	for (int i = 0; i < n; i++) {
		GoField *x = xs[i];
		const char *scope = x->d.parent->kind == GD_FILE ? x->d.file->d.full_name : x->d.parent->full_name;
		const char *err = find_message(c, scope, pb_bytes(x->proto, GENID_FieldDescriptorProto_Extendee_field_number), &x->containing);
		if (err)
			return go_err("extension field %s cannot resolve extendee: %s", go_quote(x->d.full_name), inner(err));
		if ((err = find_target(c, x, scope, pb_bytes(x->proto, GENID_FieldDescriptorProto_TypeName_field_number))))
			return go_err("extension field %s cannot resolve type: %s", go_quote(x->d.full_name), inner(err));
		if (pb_has(x->proto, GENID_FieldDescriptorProto_DefaultValue_field_number) &&
		    (err = unmarshal_default(x, pb_bytes(x->proto, GENID_FieldDescriptorProto_DefaultValue_field_number))))
			return go_err("extension field %s has invalid default: %s", go_quote(x->d.full_name), inner(err));
	}
	return NULL;
}

static COLD const char *resolve_messages(Ctx *c, GoMessage **ms, int n)
{
	for (int i = 0; i < n; i++) {
		GoMessage *m = ms[i];
		for (int j = 0; j < m->n_fields; j++) {
			GoField *f = m->fields[j];
			if (pb_has(f->proto, GENID_FieldDescriptorProto_OneofIndex_field_number)) {
				int64_t k = pb_int(f->proto, GENID_FieldDescriptorProto_OneofIndex_field_number, 0);
				if (!(k >= 0 && k < m->n_oneofs))
					return go_err("message field %s has an invalid oneof index: %d", go_quote(f->d.full_name), (int)k);
				GoOneof *o = m->oneofs[k];
				f->oneof = o;
				o->fields[o->n_fields++] = f;
			}
			const char *err = find_target(c, f, m->d.full_name, pb_bytes(f->proto, GENID_FieldDescriptorProto_TypeName_field_number));
			if (err)
				return go_err("message field %s cannot resolve type: %s", go_quote(f->d.full_name), inner(err));
			if (f->kind == PT_GROUP && (go_field_is_map(f) || m->is_map_entry))
				f->kind = PT_MESSAGE; /* maps never use delimited encoding */
			if (pb_has(f->proto, GENID_FieldDescriptorProto_DefaultValue_field_number) &&
			    (err = unmarshal_default(f, pb_bytes(f->proto, GENID_FieldDescriptorProto_DefaultValue_field_number))))
				return go_err("message field %s has invalid default: %s", go_quote(f->d.full_name), inner(err));
		}
		const char *err = resolve_messages(c, m->messages, m->n_messages);
		if (err)
			return err;
		if ((err = resolve_extensions(c, m->extensions, m->n_extensions)))
			return err;
	}
	return NULL;
}

static COLD const char *resolve_services(Ctx *c, GoFile *f)
{
	for (int i = 0; i < f->n_services; i++) {
		GoService *s = f->services[i];
		for (int j = 0; j < s->n_methods; j++) {
			GoMethod *m = s->methods[j];
			PbMsg *md = pb_msg_at(pb_msg_at(f->proto, GENID_FileDescriptorProto_Service_field_number, i),
					      GENID_ServiceDescriptorProto_Method_field_number, j);
			const char *err = find_message(c, s->d.full_name, pb_bytes(md, GENID_MethodDescriptorProto_InputType_field_number), &m->input);
			if (err)
				return go_err("service method %s cannot resolve input: %s", go_quote(m->d.full_name), inner(err));
			if ((err = find_message(c, s->d.full_name, pb_bytes(md, GENID_MethodDescriptorProto_OutputType_field_number), &m->output)))
				return go_err("service method %s cannot resolve output: %s", go_quote(m->d.full_name), inner(err));
		}
	}
	return NULL;
}

/* importSet.importPublic */
static COLD void import_public(Ctx *c, const GoFile *f)
{
	for (int i = 0; i < f->n_imports; i++) {
		const GoImport *imp = &f->imports[i];
		if (imp->is_public && imp->file) {
			map_put(&c->imports, imp->file->d.name, (void *)imp->file);
			import_public(c, imp->file);
		}
	}
}

static COLD const char *int_list(const PbSlot *s)
{
	Buf b = {0};
	buf_addc(&b, '[');
	for (int i = 0; s && i < s->n; i++)
		buf_adds(&b, xcat(i ? " " : "", go_itoa((int32_t)s->v[i].u)));
	buf_addc(&b, ']');
	return buf_str(&b);
}

static COLD const char *new_file(Ctx *c, PbMsg *fd)
{
	GoFile *f = c->f;
	f->proto = fd;
	f->d.kind = GD_FILE;
	f->d.file = f;
	const char *syntax = pb_str(fd, GENID_FileDescriptorProto_Syntax_field_number);
	Str syn = pb_bytes(fd, GENID_FileDescriptorProto_Syntax_field_number);
	if (str_eqc(syn, "proto2") || !syn.n) {
		f->syntax = GO_PROTO2, f->edition = GENID_Edition_EDITION_PROTO2;
	} else if (str_eqc(syn, "proto3")) {
		f->syntax = GO_PROTO3, f->edition = GENID_Edition_EDITION_PROTO3;
	} else if (str_eqc(syn, "editions")) {
		f->syntax = GO_EDITIONS;
		f->edition = (int)pb_int(fd, GENID_FileDescriptorProto_Edition_field_number, 0);
	} else {
		(void)syntax;
		return go_err("invalid syntax: %s", go_quote_str(syn));
	}
	f->d.name = pb_str(fd, GENID_FileDescriptorProto_Name_field_number);
	if (!*f->d.name)
		return go_err("file path must be populated");
	int ed = (int)pb_int(fd, GENID_FileDescriptorProto_Edition_field_number, 0);
	if (f->syntax == GO_EDITIONS && (ed < GENID_Edition_EDITION_PROTO2 || ed > GENID_Edition_EDITION_2024) &&
	    ed != GENID_Edition_EDITION_UNSTABLE && !has_prefix(f->d.name, "cmd/protoc-gen-go/testdata/"))
		return go_err("use of edition %s not yet supported by the Go Protobuf runtime", go_edition_string(ed));
	Str pkg = pb_bytes(fd, GENID_FileDescriptorProto_Package_field_number);
	f->d.full_name = pkg.p;
	if (pkg.n && (memchr(pkg.p, 0, pkg.n) || !go_full_name_is_valid(f->d.full_name)))
		return go_err("invalid package: %s", go_quote_str(pkg));
	PbMsg *opts = pb_msg(fd, GENID_FileDescriptorProto_Options_field_number);
	f->d.options = opts;
	f->features = features_merge(features_for_edition(f->edition), features_of(opts, GENID_FileOptions_Features_field_number));

	int ni = pb_count(fd, GENID_FileDescriptorProto_Dependency_field_number);
	f->imports = xalloc(sizeof *f->imports * (size_t)(ni ? ni : 1));
	f->n_imports = ni;
	for (int k = 0; k < pb_count(fd, GENID_FileDescriptorProto_PublicDependency_field_number); k++) {
		int64_t i = (int32_t)pb_int_at(fd, GENID_FileDescriptorProto_PublicDependency_field_number, k);
		if (!(i >= 0 && i < ni) || f->imports[i].is_public)
			return go_err("invalid or duplicate public import index: %d", (int)i);
		f->imports[i].is_public = true;
	}
	map_put(&c->imports, f->d.name, f);
	for (int i = 0; i < ni; i++) {
		Str dep = pb_bytes_at(fd, GENID_FileDescriptorProto_Dependency_field_number, i);
		const char *path = dep.p;
		GoFile *imp = memchr(dep.p, 0, dep.n) ? NULL : go_registry_find_file(c->r, path);
		if (!imp)
			return go_err("could not resolve import %s: not found", go_quote_str(dep));
		f->imports[i].file = imp;
		if (map_has(&c->imports, imp->d.name))
			return go_err("already imported %s", go_quote(path));
		map_put(&c->imports, imp->d.name, imp);
	}
	for (int i = 0; i < ni; i++)
		import_public(c, f->imports[i].file);
	/* option imports: always allowed to be unresolvable; only duplicates are errors */
	Map option_imps = {0};
	map_put(&option_imps, f->d.name, f);
	for (int i = 0; i < pb_count(fd, GENID_FileDescriptorProto_OptionDependency_field_number); i++) {
		const char *path = pb_str_at(fd, GENID_FileDescriptorProto_OptionDependency_field_number, i);
		if (map_has(&c->imports, path) || map_has(&option_imps, path))
			return go_err("already imported %s", go_quote(path));
		map_put(&option_imps, path, f);
	}

	/* source locations */
	PbMsg *sci = pb_msg(fd, GENID_FileDescriptorProto_SourceCodeInfo_field_number);
	int nl = pb_count(sci, GENID_SourceCodeInfo_Location_field_number);
	f->locations = xalloc(sizeof *f->locations * (size_t)(nl ? nl : 1));
	f->n_locations = nl;
	for (int i = 0; i < nl; i++) {
		PbMsg *loc = pb_msg_at(sci, GENID_SourceCodeInfo_Location_field_number, i);
		const PbSlot *ps = pb_slot(loc, GENID_SourceCodeInfo_Location_Path_field_number);
		const PbSlot *ss = pb_slot(loc, GENID_SourceCodeInfo_Location_Span_field_number);
		int nspan = ss ? ss->n : 0;
		if (nspan != 3 && nspan != 4)
			return go_err("invalid span: %s", int_list(ss));
		GoSourceLocation *l = &f->locations[i];
		int np = ps ? ps->n : 0;
		int32_t *path = xalloc(sizeof *path * (size_t)(np ? np : 1));
		for (int j = 0; j < np; j++)
			path[j] = (int32_t)ps->v[j].u;
		l->path = path;
		l->n_path = np;
		l->leading = pb_bytes(loc, GENID_SourceCodeInfo_Location_LeadingComments_field_number);
		l->trailing = pb_bytes(loc, GENID_SourceCodeInfo_Location_TrailingComments_field_number);
		int nd = pb_count(loc, GENID_SourceCodeInfo_Location_LeadingDetachedComments_field_number);
		l->detached = xalloc(sizeof *l->detached * (size_t)(nd ? nd : 1));
		l->n_detached = nd;
		for (int j = 0; j < nd; j++)
			l->detached[j] = pb_bytes_at(loc, GENID_SourceCodeInfo_Location_LeadingDetachedComments_field_number, j);
		const char *key = path_key(path, np);
		if (!map_has(&f->locs_by_path, key))
			map_put(&f->locs_by_path, key, l); /* the first location for this path */
	}

	/* Step 1: declarations */
	const char *err;
	if ((err = init_enums(c, &f->d, fd, GENID_FileDescriptorProto_EnumType_field_number, &f->enums, &f->n_enums)))
		return err;
	if ((err = init_messages(c, &f->d, fd, GENID_FileDescriptorProto_MessageType_field_number, &f->messages, &f->n_messages)))
		return err;
	if ((err = init_extensions(c, &f->d, fd, GENID_FileDescriptorProto_Extension_field_number, &f->extensions, &f->n_extensions)))
		return err;
	if ((err = init_services(c, f)))
		return err;
	/* Step 2: references */
	if ((err = resolve_messages(c, f->messages, f->n_messages)))
		return err;
	if ((err = resolve_extensions(c, f->extensions, f->n_extensions)))
		return err;
	if ((err = resolve_services(c, f)))
		return err;
	/* Step 3: validation */
	if ((err = go_validate_enums(f->enums, f->n_enums)))
		return err;
	if ((err = go_validate_messages(f, f->messages, f->n_messages)))
		return err;
	return go_validate_extensions(f, f->extensions, f->n_extensions);
}

/* protoregistry.Files.RegisterFile */
static COLD const char *register_file(Ctx *c)
{
	GoRegistry *r = c->r;
	GoFile *f = c->f;
	if (map_has(&r->files, f->d.name))
		return go_err("file %s is already registered", go_quote(f->d.name));
	for (const char *name = f->d.full_name; *name; name = full_name_parent(name)) {
		void *prev = map_get(&r->top, name);
		if (prev && prev != &pkg_marker)
			return go_err("file %s has a package name conflict over %s", go_quote(f->d.name), name);
	}
	/* top-level declarations (rangeTopLevelDescriptors: enums with their values, messages,
	 * extensions, services, each in reverse order; the last conflict is reported) */
	const char *conflict = NULL;
	Vec top = {0};
	for (int i = f->n_enums - 1; i >= 0; i--) {
		vec_push(&top, &f->enums[i]->d);
		for (int j = f->enums[i]->n_values - 1; j >= 0; j--)
			vec_push(&top, &f->enums[i]->values[j]->d);
	}
	for (int i = f->n_messages - 1; i >= 0; i--)
		vec_push(&top, &f->messages[i]->d);
	for (int i = f->n_extensions - 1; i >= 0; i--)
		vec_push(&top, &f->extensions[i]->d);
	for (int i = f->n_services - 1; i >= 0; i--)
		vec_push(&top, &f->services[i]->d);
	for (int i = 0; i < top.n; i++) {
		GoDesc *d = top.v[i];
		if (map_has(&r->top, d->full_name))
			conflict = go_err("file %s has a name conflict over %s", go_quote(f->d.name), d->full_name);
	}
	if (conflict)
		return conflict;
	for (const char *name = f->d.full_name; *name; name = full_name_parent(name))
		if (!map_has(&r->top, name))
			map_put(&r->top, name, &pkg_marker);
	for (int i = 0; i < top.n; i++) {
		GoDesc *d = top.v[i];
		map_put(&r->top, d->full_name, d);
	}
	for (int i = 0; i < c->all.n; i++) {
		GoDesc *d = c->all.v[i];
		map_put(&r->names, d->full_name, d);
	}
	map_put(&r->files, f->d.name, f);
	return NULL;
}

COLD GoFile *go_registry_new_file(GoRegistry *r, PbMsg *fd, const char **err)
{
	Ctx c = {0};
	c.r = r;
	c.f = xalloc(sizeof(GoFile));
	*err = new_file(&c, fd);
	if (*err) {
		*err = xcat("invalid FileDescriptorProto ", go_quote_str(pb_bytes(fd, GENID_FileDescriptorProto_Name_field_number)), ": ", *err);
		return NULL;
	}
	*err = register_file(&c);
	if (*err) {
		*err = xcat("cannot register descriptor ", go_quote_str(pb_bytes(fd, GENID_FileDescriptorProto_Name_field_number)), ": ", *err);
		return NULL;
	}
	return c.f;
}
