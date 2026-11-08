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

/* KotlinPoet 2.4.0 (https://github.com/square/kotlinpoet/tree/2.4.0/kotlinpoet/src): the Kotlin
 * code builder of the JVM plugins that emit Kotlin, with the differences of KotlinPoet 2.2.0
 * (kp_select_version). Generators build its specs (FileSpec, TypeSpec,
 * FunSpec, ParameterSpec, PropertySpec, AnnotationSpec, CodeBlock and TypeName) and kp_file_to_string
 * writes them like FileSpec.toString: the import pass (CodeWriter.withCollectedImports, with import
 * aliases), the emitters of the specs, CodeWriter (statements, indentation, KDoc and comments) and
 * LineWrapper (100 columns, wrapping only at the ♢ wrap points of KotlinPoet 2).
 *
 * Only the node kinds and builder calls of generated code are ported (see README.md); the
 * reflection APIs (KClass, javax.lang.model) are not: a generator names classes with
 * kp_class_name.
 *
 * Strings are UTF-8 (valid, without U+0000): Kotlin strings of the plugin, e.g. java_cstr of a
 * request field. Lengths and comparisons follow Java strings (UTF-16 code units). Exceptions of
 * KotlinPoet (IllegalArgumentException, IllegalStateException, ...) are java_exception (fail());
 * Java's Unicode character classes beyond ASCII (Character.isJavaIdentifierStart/Part) are not
 * ported: identifiers with such characters fail with an "unsupported" error. */
#ifndef KOTLINPOET_H
#define KOTLINPOET_H

#include "java_std.h"

/* KModifier, in the declaration order of the enum (the order modifiers are emitted in). */
enum {
	KM_PUBLIC, KM_PROTECTED, KM_PRIVATE, KM_INTERNAL, KM_EXPECT, KM_ACTUAL, KM_FINAL, KM_OPEN,
	KM_ABSTRACT, KM_SEALED, KM_CONST, KM_EXTERNAL, KM_OVERRIDE, KM_LATEINIT, KM_TAILREC, KM_VARARG,
	KM_SUSPEND, KM_INNER, KM_ENUM, KM_ANNOTATION, KM_VALUE, KM_FUN, KM_COMPANION, KM_INLINE,
	KM_NOINLINE, KM_CROSSINLINE, KM_REIFIED, KM_INFIX, KM_OPERATOR, KM_DATA, KM_IN, KM_OUT,
};
#define KM(m) (1u << (m))
typedef uint32_t KpModifiers; /* a set of KM(...) */

/* ---- versions ---- */

/* The KotlinPoet version of the specs built and written: 2.4.0 unless selected otherwise. A
 * generator on KotlinPoet 2.2.0 (grpc/kotlin) selects KP_V2_2_0 for its request and KP_V2_4_0 again
 * before it returns. The differences of 2.2.0 (README.md): an identifier escaped with backticks
 * that contains one of the characters .;[]/<>: or a backslash is an IllegalArgumentException; KDoc
 * does not escape the comment delimiters; a wrapped comment line does not repeat "// "; an
 * expression body is the body trimmed without keeping it balanced; a deferred type emits its "?"
 * first; members referenced in KDoc are not imported. */
enum { KP_V2_4_0, KP_V2_2_0 };
void kp_select_version(int version);

typedef struct KpType KpType;
typedef struct KpCode KpCode;
typedef struct KpAnnotation KpAnnotation;
typedef struct KpParam KpParam;
typedef struct KpProperty KpProperty;
typedef struct KpFun KpFun;
typedef struct KpTypeSpec KpTypeSpec;
typedef struct KpFile KpFile;
typedef struct KpMember KpMember;

/* ---- TypeName ---- */

enum { KT_CLASS, KT_PARAMETERIZED, KT_LAMBDA, KT_TYPEVAR };
struct KpType {
	int kind;
	bool nullable;
	/* KT_TYPEVAR: the name (no bounds, variance or reified) */
	const char *name;
	/* KT_CLASS: names[0] is the package ("" for none), then the simple names (n >= 2) */
	const char **names;
	int n;
	/* KT_PARAMETERIZED: raw type and type arguments */
	KpType *raw;
	Vec type_args; /* KpType* */
	/* KT_LAMBDA: parameters (KpParam*, unnamed) and return type */
	Vec params;
	KpType *ret;
	const char *cached; /* toString */
};

/* ClassName(packageName, simpleNames...): fails like the constructor for no or empty simple
 * names. */
KpType *kp_class_name(const char *package, int n, const char *const *simple_names);
/* ClassName of a package and one simple name. */
KpType *kp_class(const char *package, const char *simple_name);
/* ClassName.nestedClass(name): the internal constructor, which does not check the name. */
KpType *kp_nested_class(const KpType *c, const char *name);
/* TypeVariableName(name): emitted as its name, never imported. */
KpType *kp_type_variable(const char *name);
/* ClassName.parameterizedBy(typeArguments...) */
KpType *kp_parameterized(KpType *raw, int n, KpType *const *args);
/* LambdaTypeName.get(parameters = params, returnType = ret) */
KpType *kp_lambda(int n, KpParam *const *params, KpType *ret);
/* ClassName.canonicalName */
const char *kp_canonical_name(const KpType *c);
/* TypeName.toString(): the type as emitted outside of a file (no imports). */
const char *kp_type_to_string(KpType *t);

/* ---- MemberName ---- */

/* MemberName(enclosingClassName, simpleName) or MemberName(packageName, simpleName): no operators
 * or extensions. */
struct KpMember {
	const char *package;
	KpType *enclosing; /* KT_CLASS, or NULL for a top-level member */
	const char *simple_name;
	const char *canonical; /* canonicalName */
};
KpMember *kp_member(KpType *enclosing, const char *simple_name);
KpMember *kp_top_level_member(const char *package, const char *simple_name);

/* ---- CodeBlock ---- */

/* An argument of a format string: %L (a string or a code block), %S (a string, NULL emits null),
 * %N (a name), %T (a type), %M (a member). */
typedef struct {
	char kind; /* 'L', 'S', 'N', 'T', 'M' (the argument kinds of the generators) */
	const char *s;
	KpCode *code;
	KpType *type;
	KpMember *member;
} KpArg;
static inline KpArg kp_arg_s(const char *s) { return (KpArg){'L', s, NULL, NULL, NULL}; }
static inline KpArg kp_arg_code(KpCode *c) { return (KpArg){'L', NULL, c, NULL, NULL}; }
static inline KpArg kp_arg_str(const char *s) { return (KpArg){'S', s, NULL, NULL, NULL}; }
static inline KpArg kp_arg_name(const char *s) { return (KpArg){'N', s, NULL, NULL, NULL}; }
static inline KpArg kp_arg_type(KpType *t) { return (KpArg){'T', NULL, NULL, t, NULL}; }
static inline KpArg kp_arg_member(KpMember *m) { return (KpArg){'M', NULL, NULL, NULL, m}; }
/* A named argument of addNamed: the name and the argument (its kind is the one of a positional
 * argument for that placeholder). */
typedef struct {
	const char *name;
	KpArg arg;
} KpNamedArg;

/* A CodeBlock, also its Builder (a generator stops adding once it passes it on). */
struct KpCode {
	Vec parts; /* const char*: format parts */
	Vec args;  /* KpArg*: the arguments of the placeholders, after addArgument */
};
KpCode *kp_code(void);
/* CodeBlock.Builder.add(format, args...): parses the format like KotlinPoet, with its errors. */
void kp_add(KpCode *b, const char *format, int nargs, const KpArg *args);
#define kp_add0(b, format) kp_add((b), (format), 0, NULL)
/* add(codeBlock) */
void kp_add_code(KpCode *b, const KpCode *c);
/* CodeBlock.Builder.addNamed(format, arguments): %name:X placeholders, with its errors. */
void kp_add_named(KpCode *b, const char *format, int nargs, const KpNamedArg *args);
/* addStatement(format, args...) */
void kp_add_statement(KpCode *b, const char *format, int nargs, const KpArg *args);
#define kp_add_statement0(b, format) kp_add_statement((b), (format), 0, NULL)
void kp_indent(KpCode *b);
void kp_unindent(KpCode *b);
/* CodeBlock.of(format, args...) */
KpCode *kp_code_of(const char *format, int nargs, const KpArg *args);
static inline bool kp_code_is_empty(const KpCode *c) { return c->parts.n == 0; }
/* CodeBlock.toString() */
const char *kp_code_to_string(const KpCode *c);

/* ---- specs ---- */

/* AnnotationSpec: typeName, members (KpCode*), use-site target ("file" or NULL). */
struct KpAnnotation {
	KpType *type;
	Vec members;
	const char *use_site_target;
};
/* AnnotationSpec.builder(type) */
KpAnnotation *kp_annotation(KpType *type);
/* addMember(format, args...) */
void kp_annotation_add_member(KpAnnotation *a, const char *format, int nargs, const KpArg *args);

/* ParameterSpec: name ("" for unnamed), type, default value (NULL for none). Kdoc, annotations
 * and modifiers of parameters are not used by the ported generators. */
struct KpParam {
	const char *name;
	KpType *type;
	KpCode *default_value;
};
KpParam *kp_param(const char *name, KpType *type);

/* PropertySpec: val (not mutable), name, type, modifiers, initializer (NULL for none),
 * annotations and a getter (NULL for none). */
struct KpProperty {
	const char *name;
	KpType *type;
	KpModifiers modifiers;
	KpCode *initializer;
	Vec annotations; /* KpAnnotation* */
	KpFun *getter;
};
KpProperty *kp_property(const char *name, KpType *type, KpModifiers modifiers);

/* FunSpec: a function, the primary constructor (kp_constructor) or a getter (kp_getter). */
struct KpFun {
	const char *name;
	bool is_constructor, is_getter;
	KpCode *kdoc;
	Vec annotations; /* KpAnnotation* */
	KpModifiers modifiers;
	Vec params;      /* KpParam* */
	KpType *ret;     /* UNIT when NULL */
	KpCode *body;
};
KpFun *kp_fun(const char *name);
KpFun *kp_constructor(void);
/* FunSpec.getterBuilder() */
KpFun *kp_getter(void);
/* addKdoc(format) without arguments */
void kp_fun_add_kdoc(KpFun *f, const char *format);
/* build(): the checks of the FunSpec constructor. */
void kp_fun_build(KpFun *f);

/* TypeSpec: a class, an object or an interface. */
enum { KK_CLASS, KK_OBJECT, KK_INTERFACE };
struct KpTypeSpec {
	int kind;
	const char *name;
	KpCode *kdoc;
	Vec annotations; /* KpAnnotation* */
	KpModifiers modifiers;
	KpFun *primary_constructor; /* with its annotations and modifiers */
	KpType *superclass;         /* NULL: Any */
	Vec super_ctor_params;      /* KpCode*: superclassConstructorParameters */
	Vec superinterfaces;        /* KpType*, distinct (TypeName equality) */
	Vec properties;             /* KpProperty* */
	Vec funs;                   /* KpFun* */
	Vec types;                  /* KpTypeSpec*: nested types */
	Map *fun_names;             /* the names of funs, built when written (kotlinpoet internal) */
};
KpTypeSpec *kp_class_builder(const char *name);
KpTypeSpec *kp_object_builder(const char *name);
KpTypeSpec *kp_interface_builder(const char *name);
void kp_type_add_kdoc(KpTypeSpec *t, const char *format);
void kp_type_add_superinterface(KpTypeSpec *t, KpType *superinterface);
void kp_type_primary_constructor(KpTypeSpec *t, KpFun *constructor);
/* superclass(type), addSuperclassConstructorParameter(format, args...) */
void kp_type_superclass(KpTypeSpec *t, KpType *superclass);
void kp_type_add_superclass_ctor_param(KpTypeSpec *t, const char *format, int nargs, const KpArg *args);
/* build(): the checks of TypeSpec.Builder.build. */
void kp_type_build(KpTypeSpec *t);

/* FileSpec: package, file comment, file annotations, member imports and types. */
struct KpFile {
	const char *package, *name;
	KpCode *comment;
	Vec annotations;    /* KpAnnotation* (use-site target file) */
	Vec member_imports; /* const char*: qualified names, a sorted set */
	Vec members;        /* KpTypeSpec* */
};
KpFile *kp_file(const char *package, const char *name);
/* addFileComment(format) without arguments */
void kp_file_add_comment(KpFile *f, const char *format);
/* addAnnotation(annotationSpec): sets the use-site target file. */
void kp_file_add_annotation(KpFile *f, KpAnnotation *a);
/* addImport(packageName, name) */
void kp_file_add_import(KpFile *f, const char *package, const char *name);
void kp_file_add_type(KpFile *f, KpTypeSpec *t);
/* FileSpec.toString() */
const char *kp_file_to_string(KpFile *f);

/* ---- common types (kotlin.Unit) ---- */
KpType *kp_unit(void);

/* ---- utilities of KotlinPoet (Util.kt), for tests and generators ---- */

/* String.escapeIfNecessary(validate = true) */
const char *kp_escape_if_necessary(const char *s);
/* String.escapeSegmentsIfNecessary() */
const char *kp_escape_segments_if_necessary(const char *s);
/* stringLiteralWithQuotes(value, isInsideRawString = false, isConstantContext) */
const char *kp_string_literal_with_quotes(const char *value, bool is_constant_context);

#endif
