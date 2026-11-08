// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Source locations (src/google/protobuf/descriptor.cc: FileDescriptorTables::GetSourceLocation,
 * the GetLocationPath of each descriptor, protobuf v36.2): the SourceCodeInfo.Location of a path,
 * the first one when a path occurs more than once (protobuf-go SourceLocations.ByPath and
 * protobuf-es pick the first one too). The leading, trailing and detached comments are read from
 * the location with the pb.h accessors (LOC_LEADING, LOC_TRAILING, LOC_LEADING_DETACHED); how
 * they are formatted belongs to the library. */
#include "proto.h"

/* Field numbers of the lists in FileDescriptorProto, DescriptorProto, EnumDescriptorProto and
 * ServiceDescriptorProto. */
enum {
	PATH_FILE_MESSAGE = 4, PATH_FILE_ENUM = 5, PATH_FILE_SERVICE = 6, PATH_FILE_EXTENSION = 7,
	PATH_MESSAGE_FIELD = 2, PATH_MESSAGE_NESTED = 3, PATH_MESSAGE_ENUM = 4,
	PATH_MESSAGE_EXTENSION_RANGE = 5, PATH_MESSAGE_EXTENSION = 6, PATH_MESSAGE_ONEOF = 8,
	PATH_ENUM_VALUE = 2, PATH_SERVICE_METHOD = 2,
};

/* The field number of the list that holds d in its parent. */
static int32_t list_field(const ProtoDesc *d, const ProtoDesc *parent)
{
	bool in_file = parent->kind == PROTO_FILE;
	switch (d->kind) {
	case PROTO_MESSAGE: return in_file ? PATH_FILE_MESSAGE : PATH_MESSAGE_NESTED;
	case PROTO_ENUM: return in_file ? PATH_FILE_ENUM : PATH_MESSAGE_ENUM;
	case PROTO_SERVICE: return PATH_FILE_SERVICE;
	case PROTO_FIELD:
		if (((const ProtoField *)d)->is_extension)
			return in_file ? PATH_FILE_EXTENSION : PATH_MESSAGE_EXTENSION;
		return PATH_MESSAGE_FIELD;
	case PROTO_ONEOF: return PATH_MESSAGE_ONEOF;
	case PROTO_EXTENSION_RANGE: return PATH_MESSAGE_EXTENSION_RANGE;
	case PROTO_ENUM_VALUE: return PATH_ENUM_VALUE;
	case PROTO_METHOD: return PATH_SERVICE_METHOD;
	default: return 0;
	}
}

int32_t *proto_desc_source_path(const ProtoDesc *d, int *n)
{
	int depth = 0;
	for (const ProtoDesc *p = d; p->kind != PROTO_FILE; p = proto_desc_parent(p))
		depth++;
	int32_t *path = xalloc(sizeof(int32_t) * (size_t)(2 * depth + 1));
	*n = 2 * depth;
	for (int i = 2 * depth; d->kind != PROTO_FILE; i -= 2) {
		const ProtoDesc *parent = proto_desc_parent(d);
		path[i - 2] = list_field(d, parent);
		path[i - 1] = d->index;
		d = parent;
	}
	return path;
}

static char *path_key(const int32_t *path, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_fmt(&b, i ? ",%d" : "%d", (int)path[i]);
	return (char *)buf_str(&b);
}

const PbMsg *proto_file_location(ProtoFile *f, const int32_t *path, int n)
{
	if (!f->locations_built) {
		f->locations_built = true;
		for (int i = 0; i < pb_count(f->source_code_info, SCI_LOCATION); i++) {
			PbMsg *loc = pb_msg_at(f->source_code_info, SCI_LOCATION, i);
			int np = pb_count(loc, LOC_PATH);
			int32_t *p = xalloc(sizeof(int32_t) * (size_t)(np + 1));
			for (int j = 0; j < np; j++)
				p[j] = (int32_t)pb_int_at(loc, LOC_PATH, j);
			char *key = path_key(p, np);
			if (!map_has(&f->locations, key))
				map_put(&f->locations, key, loc);
		}
	}
	return map_get(&f->locations, path_key(path, n));
}

const PbMsg *proto_desc_location(const ProtoDesc *d)
{
	int n;
	int32_t *path = proto_desc_source_path(d, &n);
	return proto_file_location(proto_desc_file(d), path, n);
}
