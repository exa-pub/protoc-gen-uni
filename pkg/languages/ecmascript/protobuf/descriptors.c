/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protobuf/src/descriptors.ts (kinds and toString of descriptors). */
#include "protobuf.h"

PbMsg *desc_options(const Desc *d)
{
	static const uint8_t opt_num[] = {
		[DK_FILE] = 8, [DK_MESSAGE] = 7, [DK_ENUM] = 3, [DK_ENUM_VALUE] = 3,
		[DK_FIELD] = 8, [DK_ONEOF] = 2, [DK_SERVICE] = 3, [DK_METHOD] = 4,
	};
	return pb_msg(d->proto, opt_num[d->kind]);
}

PbMsg *desc_features(const Desc *d)
{
	static const uint8_t feat_num[] = {
		[DK_FILE] = 50, [DK_MESSAGE] = 12, [DK_ENUM] = 7, [DK_ENUM_VALUE] = 2,
		[DK_FIELD] = 21, [DK_ONEOF] = 1, [DK_SERVICE] = 34, [DK_METHOD] = 35,
	};
	return pb_msg(desc_options(d), feat_num[d->kind]);
}

const char *desc_kind(const Desc *d)
{
	switch (d->kind) {
	case DK_FILE: return "file";
	case DK_MESSAGE: return "message";
	case DK_ENUM: return "enum";
	case DK_ENUM_VALUE: return "enum_value";
	case DK_FIELD: return ((const DescField *)d)->is_extension ? "extension" : "field";
	case DK_ONEOF: return "oneof";
	case DK_SERVICE: return "service";
	case DK_METHOD: return "rpc";
	}
	return "";
}

const char *desc_string(const Desc *d)
{
	switch (d->kind) {
	case DK_FILE: return xcat("file ", pb_str(d->proto, FDP_NAME));
	case DK_MESSAGE: return xcat("message ", ((const DescMessage *)d)->type_name);
	case DK_ENUM: return xcat("enum ", ((const DescEnum *)d)->type_name);
	case DK_ENUM_VALUE: {
		const DescEnumValue *v = (const DescEnumValue *)d;
		return xcat("enum value ", v->parent->type_name, ".", v->name);
	}
	case DK_FIELD: {
		const DescField *f = (const DescField *)d;
		if (f->is_extension)
			return xcat("extension ", f->type_name);
		return xcat("field ", f->parent->type_name, ".", f->name);
	}
	case DK_ONEOF: {
		const DescOneof *o = (const DescOneof *)d;
		return xcat("oneof ", o->parent->type_name, ".", o->name);
	}
	case DK_SERVICE: return xcat("service ", ((const DescService *)d)->type_name);
	case DK_METHOD: {
		const DescMethod *m = (const DescMethod *)d;
		return xcat("rpc ", m->parent->type_name, ".", m->name);
	}
	}
	return "";
}

DescFile *desc_file_of(const Desc *d)
{
	switch (d->kind) {
	case DK_FILE: return (DescFile *)d;
	case DK_MESSAGE: return ((const DescMessage *)d)->file;
	case DK_ENUM: return ((const DescEnum *)d)->file;
	case DK_FIELD: {
		const DescField *f = (const DescField *)d;
		return f->is_extension ? f->file : f->parent->file;
	}
	case DK_SERVICE: return ((const DescService *)d)->file;
	case DK_ENUM_VALUE: return ((const DescEnumValue *)d)->parent->file;
	case DK_ONEOF: return ((const DescOneof *)d)->parent->file;
	case DK_METHOD: return ((const DescMethod *)d)->parent->file;
	}
	return NULL;
}

const char *scalar_name(int s)
{
	static const char *const names[] = {
		[PT_DOUBLE] = "DOUBLE", [PT_FLOAT] = "FLOAT", [PT_INT64] = "INT64", [PT_UINT64] = "UINT64",
		[PT_INT32] = "INT32", [PT_FIXED64] = "FIXED64", [PT_FIXED32] = "FIXED32", [PT_BOOL] = "BOOL",
		[PT_STRING] = "STRING", [PT_BYTES] = "BYTES", [PT_UINT32] = "UINT32", [PT_SFIXED32] = "SFIXED32",
		[PT_SFIXED64] = "SFIXED64", [PT_SINT32] = "SINT32", [PT_SINT64] = "SINT64",
	};
	return s >= 0 && s <= PT_SINT64 && names[s] ? names[s] : "";
}
