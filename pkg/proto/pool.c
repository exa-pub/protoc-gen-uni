// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The tables of a descriptor pool (src/google/protobuf/descriptor.cc DescriptorPool::Tables,
 * protobuf v36.2): files by name, symbols by full name (packages included), extensions by
 * extendee and number. The rules for adding them (conflicts, their errors, name resolution)
 * belong to the library's builder. */
#include "proto.h"

#include <string.h>

ProtoSymbol *proto_symbol_new(int kind, void *desc, ProtoFile *file, const char *full_name)
{
	ProtoSymbol *s = xalloc(sizeof *s);
	s->kind = (uint8_t)kind;
	s->desc = desc;
	s->file = file;
	s->full_name = full_name;
	return s;
}

void proto_pool_add_file(ProtoPool *pool, ProtoFile *file)
{
	map_put(&pool->files, file->name, file);
	vec_push(&pool->file_list, file);
}

ProtoFile *proto_pool_find_file(const ProtoPool *pool, const char *name) { return map_get(&pool->files, name); }

ProtoSymbol *proto_pool_find_symbol(const ProtoPool *pool, const char *full_name)
{
	return map_get(&pool->symbols, full_name);
}

bool proto_pool_add_symbol(ProtoPool *pool, ProtoSymbol *s)
{
	if (map_has(&pool->symbols, s->full_name))
		return false;
	map_put(&pool->symbols, s->full_name, s);
	return true;
}

ProtoMessage *proto_pool_find_message(const ProtoPool *pool, const char *full_name)
{
	ProtoSymbol *s = proto_pool_find_symbol(pool, full_name);
	return s && s->kind == PROTO_MESSAGE ? s->desc : NULL;
}

ProtoEnum *proto_pool_find_enum(const ProtoPool *pool, const char *full_name)
{
	ProtoSymbol *s = proto_pool_find_symbol(pool, full_name);
	return s && s->kind == PROTO_ENUM ? s->desc : NULL;
}

static char *extension_key(const ProtoMessage *extendee, int number)
{
	return xfmt("%d:%d", extendee ? extendee->id : -1, number);
}

bool proto_pool_add_extension(ProtoPool *pool, ProtoField *ext)
{
	char *key = extension_key(ext->containing_type, ext->number);
	if (map_has(&pool->extensions, key))
		return false;
	map_put(&pool->extensions, key, ext);
	if (!ext->containing_type)
		return true; /* no extendee (the builder reported an error) */
	Vec *list = map_get(&pool->extension_lists, ext->containing_type->full_name);
	if (!list) {
		list = xalloc(sizeof *list);
		map_put(&pool->extension_lists, ext->containing_type->full_name, list);
	}
	vec_push(list, ext);
	return true;
}

ProtoField *proto_pool_find_extension(const ProtoPool *pool, const ProtoMessage *extendee, int number)
{
	return map_get(&pool->extensions, extension_key(extendee, number));
}

const Vec *proto_pool_extensions(const ProtoPool *pool, const char *extendee_full_name)
{
	return map_get(&pool->extension_lists, extendee_full_name);
}

bool proto_symbol_is_type(const ProtoSymbol *s) { return s && (s->kind == PROTO_MESSAGE || s->kind == PROTO_ENUM); }

bool proto_symbol_is_package(const ProtoSymbol *s)
{
	return s && (s->kind == PROTO_SYM_PACKAGE || s->kind == PROTO_SYM_SUB_PACKAGE);
}

bool proto_symbol_is_aggregate(const ProtoSymbol *s)
{
	return proto_symbol_is_type(s) || proto_symbol_is_package(s) || (s && s->kind == PROTO_SERVICE);
}

bool proto_file_in_package(const ProtoFile *file, const char *package_name)
{
	size_t n = strlen(package_name);
	return strncmp(file->package, package_name, n) == 0 && (file->package[n] == 0 || file->package[n] == '.');
}
