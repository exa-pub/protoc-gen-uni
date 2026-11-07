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

/* Ported from packages/protoplugin/src/safe-identifier.ts. */
#include "internal.h"

/* Escapes ECMAScript reserved words and built-in names that generated code must not shadow. */
const char *safe_identifier(const char *name)
{
	static const char *const reserved[] = {
		/* ECMAScript 2015 keywords */
		"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
		"else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
		"instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try",
		"typeof", "var", "void", "while", "with", "yield",
		/* ECMAScript 2015 future reserved keywords */
		"enum", "implements", "interface", "let", "package", "private", "protected", "public", "static",
		/* Class name cannot be 'Object' when targeting ES5 with module CommonJS */
		"Object",
		/* TypeScript keywords that cannot be used for types (as opposed to variables) */
		"bigint", "number", "boolean", "string", "object",
		/* Identifiers reserved for the runtime, so we can generate legible code */
		"globalThis", "Uint8Array", "Partial",
	};
	for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++)
		if (str_eq(name, reserved[i]))
			return xcat(name, "$");
	return name;
}
