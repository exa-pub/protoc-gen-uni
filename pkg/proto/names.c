// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* ToJsonName and ToCamelCase of src/google/protobuf/descriptor.cc (protobuf v36.2). ToJsonName
 * gives the default json_name that protoc puts in every FieldDescriptorProto it sends to plugins. */
#include "proto.h"

char *proto_json_name(const char *name)
{
	Buf b = {0};
	bool capitalize_next = false;
	for (const char *p = name; *p; p++) {
		if (*p == '_') {
			capitalize_next = true;
		} else if (capitalize_next) {
			buf_addc(&b, *p >= 'a' && *p <= 'z' ? (char)(*p - 'a' + 'A') : *p);
			capitalize_next = false;
		} else {
			buf_addc(&b, *p);
		}
	}
	return (char *)buf_str(&b);
}

char *proto_to_camel_case(const char *name, bool lower_first)
{
	Buf b = {0};
	bool capitalize_next = !lower_first;
	for (const char *p = name; *p; p++) {
		if (*p == '_') {
			capitalize_next = true;
		} else if (capitalize_next) {
			buf_addc(&b, *p >= 'a' && *p <= 'z' ? (char)(*p - 'a' + 'A') : *p);
			capitalize_next = false;
		} else {
			buf_addc(&b, *p);
		}
	}
	char *s = (char *)buf_str(&b);
	if (lower_first && s[0] >= 'A' && s[0] <= 'Z')
		s[0] = (char)(s[0] - 'A' + 'a');
	return s;
}
