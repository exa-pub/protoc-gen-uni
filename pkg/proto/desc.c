// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The descriptor accessors and predicates of src/google/protobuf/descriptor.h (protobuf v36.2):
 * FieldDescriptor::is_repeated, is_required, is_packable, is_packed, has_presence, is_map,
 * requires_utf8_validation, has_optional_keyword, real_containing_oneof,
 * EnumDescriptor::is_closed, OneofDescriptor::is_synthetic. They depend on resolved features only,
 * so protobuf-go (internal/filedesc), protobuf-es and swift-protobuf compute the same. */
#include "proto.h"

#include <string.h>

ProtoDesc *proto_desc_parent(const ProtoDesc *d)
{
	switch (d->kind) {
	case PROTO_MESSAGE: {
		const ProtoMessage *m = (const ProtoMessage *)d;
		return m->containing_type ? (ProtoDesc *)m->containing_type : (ProtoDesc *)m->file;
	}
	case PROTO_FIELD: {
		const ProtoField *f = (const ProtoField *)d;
		if (!f->is_extension)
			return (ProtoDesc *)f->containing_type;
		return f->extension_scope ? (ProtoDesc *)f->extension_scope : (ProtoDesc *)f->file;
	}
	case PROTO_ONEOF: return (ProtoDesc *)((const ProtoOneof *)d)->containing_type;
	case PROTO_ENUM: {
		const ProtoEnum *e = (const ProtoEnum *)d;
		return e->containing_type ? (ProtoDesc *)e->containing_type : (ProtoDesc *)e->file;
	}
	case PROTO_ENUM_VALUE: return (ProtoDesc *)((const ProtoEnumValue *)d)->type;
	case PROTO_SERVICE: return (ProtoDesc *)((const ProtoService *)d)->file;
	case PROTO_METHOD: return (ProtoDesc *)((const ProtoMethod *)d)->service;
	case PROTO_EXTENSION_RANGE: return (ProtoDesc *)((const ProtoExtensionRange *)d)->containing_type;
	default: return NULL;
	}
}

ProtoFile *proto_desc_file(const ProtoDesc *d)
{
	while (d && d->kind != PROTO_FILE)
		d = proto_desc_parent(d);
	return (ProtoFile *)d;
}

bool proto_type_is_packable(int type)
{
	return type != PT_STRING && type != PT_GROUP && type != PT_MESSAGE && type != PT_BYTES;
}

bool proto_field_is_repeated(const ProtoField *f) { return f->label == LABEL_REPEATED; }

bool proto_field_is_required(const ProtoField *f)
{
	return proto_feature(f->merged_features, PROTO_FEATURE_FIELD_PRESENCE) == PROTO_PRESENCE_LEGACY_REQUIRED;
}

bool proto_field_is_packable(const ProtoField *f) { return proto_field_is_repeated(f) && proto_type_is_packable(f->type); }

bool proto_field_is_packed(const ProtoField *f)
{
	return proto_field_is_packable(f) &&
	       proto_feature(f->merged_features, PROTO_FEATURE_REPEATED_FIELD_ENCODING) == PROTO_REPEATED_PACKED;
}

bool proto_field_has_presence(const ProtoField *f)
{
	if (proto_field_is_repeated(f))
		return false;
	return f->type == PT_MESSAGE || f->type == PT_GROUP || f->is_extension || f->containing_oneof ||
	       proto_feature(f->merged_features, PROTO_FEATURE_FIELD_PRESENCE) != PROTO_PRESENCE_IMPLICIT;
}

bool proto_message_is_map_entry(const ProtoMessage *m) { return m && pb_int(m->options, PROTO_MSGOPT_MAP_ENTRY, 0) != 0; }

bool proto_field_is_map(const ProtoField *f) { return f->type == PT_MESSAGE && proto_message_is_map_entry(f->message_type); }

bool proto_field_requires_utf8_validation(const ProtoField *f)
{
	return f->type == PT_STRING && proto_feature(f->merged_features, PROTO_FEATURE_UTF8_VALIDATION) == PROTO_UTF8_VERIFY;
}

bool proto_field_has_optional_keyword(const ProtoField *f)
{
	return f->proto3_optional || (f->file->edition == PROTO_EDITION_PROTO2 && f->label == LABEL_OPTIONAL &&
				      !f->containing_oneof);
}

ProtoOneof *proto_field_real_oneof(const ProtoField *f)
{
	return f->containing_oneof && !proto_oneof_is_synthetic(f->containing_oneof) ? f->containing_oneof : NULL;
}

ProtoField *proto_message_field_by_number(const ProtoMessage *m, int number)
{
	for (int i = 0; i < m->n_fields; i++)
		if (m->fields[i].number == number)
			return &m->fields[i];
	return NULL;
}

ProtoField *proto_message_field_by_name(const ProtoMessage *m, const char *name)
{
	for (int i = 0; i < m->n_fields; i++)
		if (strcmp(m->fields[i].name, name) == 0)
			return &m->fields[i];
	return NULL;
}

/* FileDescriptorTables::FieldsByCamelcaseNamesLazyInitInternal: one entry per scope and camelcase
 * name, keeping the field or extension with the smallest number. */
static void add_by_camelcase(Map *map, const char *scope, ProtoField *f)
{
	char *key = xcat(scope, "/", proto_to_camel_case(f->name, true));
	ProtoField *found = map_get(map, key);
	if (!found || found->number > f->number)
		map_put(map, key, f);
}

ProtoField *proto_message_field_by_camelcase_name(const ProtoMessage *m, const char *camelcase)
{
	ProtoFile *file = m->file;
	if (!file->fields_by_camelcase_built) {
		file->fields_by_camelcase_built = true;
		Vec todo = {0}; /* explicit stack: nesting depth comes from the input */
		for (int i = 0; i < file->n_messages; i++)
			vec_push(&todo, &file->messages[i]);
		while (todo.n) {
			ProtoMessage *msg = todo.v[--todo.n];
			for (int i = 0; i < msg->n_fields; i++)
				add_by_camelcase(&file->fields_by_camelcase, msg->full_name, &msg->fields[i]);
			for (int i = 0; i < msg->n_extensions; i++)
				add_by_camelcase(&file->fields_by_camelcase, msg->full_name, &msg->extensions[i]);
			for (int i = 0; i < msg->n_nested; i++)
				vec_push(&todo, &msg->nested[i]);
		}
	}
	ProtoField *f = map_get(&file->fields_by_camelcase, xcat(m->full_name, "/", camelcase));
	return f && f->is_extension ? NULL : f;
}

const ProtoExtensionRange *proto_message_extension_range(const ProtoMessage *m, int number)
{
	for (int i = 0; i < m->n_extension_ranges; i++)
		if (number >= m->extension_ranges[i].start && number < m->extension_ranges[i].end)
			return &m->extension_ranges[i];
	return NULL;
}

bool proto_enum_is_closed(const ProtoEnum *e)
{
	return proto_feature(e->merged_features, PROTO_FEATURE_ENUM_TYPE) == PROTO_ENUM_TYPE_CLOSED;
}

ProtoEnumValue *proto_enum_value_by_number(const ProtoEnum *e, int64_t number)
{
	for (int i = 0; i < e->n_values; i++)
		if (e->values[i].number == number)
			return &e->values[i];
	return NULL;
}

ProtoEnumValue *proto_enum_value_by_name(const ProtoEnum *e, const char *name)
{
	for (int i = 0; i < e->n_values; i++)
		if (strcmp(e->values[i].name, name) == 0)
			return &e->values[i];
	return NULL;
}

bool proto_oneof_is_synthetic(const ProtoOneof *o) { return o->n_fields == 1 && proto_oneof_field(o, 0)->proto3_optional; }
