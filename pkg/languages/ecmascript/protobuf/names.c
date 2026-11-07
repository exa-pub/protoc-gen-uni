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

/* Ported from packages/protobuf/src/reflect/names.ts. */
#include "protobuf.h"

/* Converts snake_case to protoCamelCase according to the convention used by protoc to convert a
 * field name to a JSON name. */
const char *proto_camel_case(const char *s)
{
	Buf b = {0};
	bool cap = false;
	for (; *s; s++) {
		char c = *s;
		if (c == '_') {
			cap = true;
		} else if (c >= '0' && c <= '9') {
			buf_addc(&b, c);
			cap = false;
		} else {
			if (cap && c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			cap = false;
			buf_addc(&b, c);
		}
	}
	return buf_str(&b);
}

/* Escapes names that are reserved for ECMAScript built-in object properties. */
const char *safe_object_property(const char *name)
{
	static const char *const reserved[] = {"constructor", "toString", "toJSON", "valueOf"};
	for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++)
		if (str_eq(name, reserved[i]))
			return xcat(name, "$");
	return name;
}
