/*
 * Copyright (C) 2015 Square, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Internals of the KotlinPoet port shared by its files. */
#ifndef KOTLINPOET_INTERNAL_H
#define KOTLINPOET_INTERNAL_H

#include "kotlinpoet.h"

/* The formatting characters of KotlinPoet, UTF-8. */
#define KP_WRAP "\xe2\x99\xa2"   /* ♢ U+2662: a space or a line break */
#define KP_NBSP "\xc2\xb7"       /* · U+00B7: a space that never wraps */
#define KP_INDENT "\xe2\x87\xa5" /* ⇥ U+21E5 */
#define KP_UNINDENT "\xe2\x87\xa4" /* ⇤ U+21E4 */
#define KP_OPEN "\xc2\xab"       /* « U+00AB: begins a statement */
#define KP_CLOSE "\xc2\xbb"      /* » U+00BB: ends a statement */

/* The nesting of code blocks (%L arguments) that CodeWriter.emitCode recurses into before the JVM
 * stack overflows (StackOverflowError): about 5000 levels on the JVM of the buf.build plugins,
 * varying from run to run above that. Deeper blocks throw java.lang.StackOverflowError, which also
 * bounds the C stack. */
#define KP_MAX_CODE_DEPTH 5000

/* ---- util.c ---- */

/* The selected version (kp_select_version) */
extern int kp_version;
#define KP_V22 (kp_version == KP_V2_2_0)

NORETURN COLD void kp_illegal_argument(const char *msg);
NORETURN COLD void kp_illegal_state(const char *msg);
/* String.length: UTF-16 code units of a valid UTF-8 string of n bytes. */
size_t kp_utf16_len(const char *s, size_t n);
/* Width in bytes of the UTF-8 sequence at s (1 for an invalid lead byte). */
int kp_utf8_width(const char *s);
bool kp_is_keyword(const char *s);
/* escapeIfNecessary(validate = true): the validation of KotlinPoet 2.4.0 never fails (see
 * util.c), the one of 2.2.0 does for illegal characters. */
const char *kp_escape(const char *s);
/* escapeIfNecessary(validate = false) */
const char *kp_escape_unvalidated(const char *s);
/* String.isIdentifier of ASCII strings (IDENTIFIER_REGEX) and extractMemberName of CodeWriter */
const char *kp_extract_member_name(const char *part);
/* escapeAsAlias() */
const char *kp_escape_as_alias(const char *s);
/* characterLiteralWithoutSingleQuotes of every character (escapeCharacterLiterals) */
const char *kp_escape_character_literals(const char *s);
/* Char.uppercaseChar() / lowercaseChar() of the first character (replaceFirstChar). */
const char *kp_upper_first(const char *s);
const char *kp_lower_first(const char *s);

/* ---- code_block.c ---- */

bool kp_is_single_char_placeholder(const char *part);
/* String.isPlaceholder */
bool kp_part_is_placeholder(const char *part);
KpCode *kp_code_new(Vec parts, Vec args);
/* CodeBlock.trim(keepBalanced) */
KpCode *kp_code_trim(const KpCode *c, bool keep_balanced);
/* CodeBlock.withoutPrefix(prefix), or NULL */
KpCode *kp_code_without_prefix(const KpCode *c, const KpCode *prefix);
bool kp_code_has_statements(const KpCode *c);
bool kp_code_has_unmatched_closing_statement(const KpCode *c);
/* ensureEndsWithNewLine() and trimTrailingNewLine() */
KpCode *kp_code_ensure_ends_with_newline(const KpCode *c);
KpCode *kp_code_trim_trailing_newline(const KpCode *c, char replace_with);
/* joinToCode(separator, prefix, suffix) of code blocks */
KpCode *kp_join_to_code(const Vec *blocks, const char *separator, const char *prefix, const char *suffix);
/* The toString of an argument (for error messages). */
const char *kp_arg_to_string(const KpArg *a);

/* ---- code_writer.c ---- */

typedef struct KpWriter KpWriter;
/* A CodeWriter writing to out (NULL: NullAppendable), with the imports of a first pass. */
KpWriter *kp_writer_new(Buf *out, int column_limit);
void kp_writer_close(KpWriter *w);
void kp_writer_indent(KpWriter *w, int levels);
void kp_writer_unindent(KpWriter *w, int levels);
void kp_writer_push_package(KpWriter *w, const char *package);
void kp_writer_pop_package(KpWriter *w);
void kp_writer_push_type(KpWriter *w, KpTypeSpec *t);
void kp_writer_pop_type(KpWriter *w);
int kp_writer_statement_line(KpWriter *w);
void kp_writer_set_statement_line(KpWriter *w, int line);
void kp_emit(KpWriter *w, const char *s);
void kp_emit_code(KpWriter *w, const KpCode *c, bool is_constant_context, bool ensure_trailing_newline);
/* emitCode(format, args...) */
void kp_emit_codef(KpWriter *w, const char *format, int nargs, const KpArg *args);
void kp_emit_comment(KpWriter *w, const KpCode *c);
void kp_emit_kdoc(KpWriter *w, const KpCode *c);
void kp_emit_annotations(KpWriter *w, const Vec *annotations, bool inline_);
void kp_emit_modifiers(KpWriter *w, KpModifiers modifiers, KpModifiers implicit);
const char *kp_lookup_name(KpWriter *w, const KpType *c);
/* MemberName.emit */
void kp_member_emit(const KpMember *m, KpWriter *w);
/* The imports of the writer (CodeWriter.imports): canonical names and aliases (NULL for none). */
int kp_writer_imports(KpWriter *w, const char ***names, const char ***aliases);
/* CodeWriter.withCollectedImports: runs emit_step on a collecting writer, then returns the writer
 * for out with the imports. member_imports: qualified names (const char*). */
KpWriter *kp_writer_with_collected_imports(Buf *out, const Vec *member_imports, void (*emit_step)(KpWriter *, void *),
					   void *arg);

/* ---- specs.c ---- */

bool kp_type_equals(const KpType *a, const KpType *b);
bool kp_member_equals(const KpMember *a, const KpMember *b);
void kp_type_emit(KpType *t, KpWriter *w);
void kp_type_emit_nullable(const KpType *t, KpWriter *w);
KpType *kp_top_level_class_name(const KpType *c);
KpType *kp_enclosing_class_name(const KpType *c);
void kp_annotation_emit(KpAnnotation *a, KpWriter *w, bool inline_);
void kp_params_emit(const Vec *params, KpWriter *w, bool force_new_lines, void (*emit_block)(KpParam *, KpWriter *, void *),
		    void *arg);
void kp_param_emit(KpParam *p, KpWriter *w, bool include_type);

#endif
