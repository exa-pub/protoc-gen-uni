/*
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A port of compiler/src/main/java/io/grpc/kotlin/generator/protoc/ of grpc-kotlin v1.5.0:
 * AbstractGeneratorRunner.kt and CodeGenerators.kt (the protoc plugin: request, descriptors,
 * response), the names (ProtoMethodName.kt, MemberSimpleName.kt, ClassSimpleName.kt,
 * ProtoServiceName.kt, ProtoFileName.kt), DescriptorUtil.kt (the outer class name),
 * JavaPackagePolicy.kt, Scope.kt, GeneratorConfig.kt (the Java classes of messages) and
 * KtPoetUtil.kt (FileSpec.path). Included by generator.c.
 *
 * Exceptions inside CodeGenerators.codeGeneratorResponse become the response error with the stack
 * trace (Throwables.getStackTraceAsString). The port gives the frames of the exceptions thrown at
 * fixed places of the plugin (error_trace); for the others, thrown inside protobuf-java or
 * KotlinPoet, only the first line, Throwable.toString (README.md, known differences). */
#include "kotlinpoet.h"
#include "protobuf_java.h"

#include <string.h>

/* The prefix of the errors of what the port does not support (README.md). */
#define UNSUPPORTED "protoc-gen-uni does not support"

/* The frames of the stack trace of the exception being thrown, when known (NULL otherwise). */
static const char *error_trace;

#define FRAME(f) "\tat " f "\n"
#define FRAMES_MAIN                                                                                                    \
	FRAME("io.grpc.kotlin.generator.protoc.AbstractGeneratorRunner.doMain(AbstractGeneratorRunner.kt:89)")         \
	FRAME("io.grpc.kotlin.generator.GeneratorRunner.main(GeneratorRunner.kt:27)")
/* the frames below a service generator */
#define FRAMES_GENERATE                                                                                                \
	FRAME("io.grpc.kotlin.generator.ProtoFileCodeGenerator.generateCodeForFile(ProtoFileCodeGenerator.kt:51)")    \
	FRAME("io.grpc.kotlin.generator.GeneratorRunner.generateCodeForFile(GeneratorRunner.kt:45)")                  \
	FRAME("io.grpc.kotlin.generator.protoc.AbstractGeneratorRunner.mainAsProtocPlugin(AbstractGeneratorRunner.kt:56)") \
	FRAMES_MAIN

/* ---- names: Guava CaseFormat.UPPER_CAMEL/LOWER_CAMEL ---- */

/* CaseFormat.UPPER_CAMEL.to(LOWER_CAMEL, s) and LOWER_CAMEL.to(UPPER_CAMEL, s) of Guava: the words
 * start at the upper case letters after the first character; the first word is lower cased
 * (Ascii.toLowerCase) or gets an upper case first letter, the others keep their upper case first
 * letter and have no other one. Only the first character changes. */
static const char *case_first(const char *s, bool upper)
{
	if (!*s)
		return s;
	char *r = xstrdup(s);
	if (upper && r[0] >= 'a' && r[0] <= 'z')
		r[0] = (char)(r[0] - 'a' + 'A');
	else if (!upper && r[0] >= 'A' && r[0] <= 'Z')
		r[0] = (char)(r[0] - 'A' + 'a');
	return r;
}

/* ProtoMethodName.toMemberSimpleName(): lower camel case, then the words between underscores
 * joined by MemberSimpleName.plus (each with an upper case first letter). */
static const char *method_member_name(const char *method_name)
{
	const char *name = case_first(method_name, false);
	if (!strchr(name, '_'))
		return name;
	Buf b = {0};
	bool first = true;
	for (const char *p = name;;) {
		const char *us = strchr(p, '_');
		const char *word = us ? xstrndup(p, (size_t)(us - p)) : p;
		buf_adds(&b, first ? word : case_first(word, true));
		first = false;
		if (!us)
			break;
		p = us + 1;
	}
	return xstrdup(buf_str(&b));
}

/* MemberSimpleName.withPrefix(prefix) */
static const char *member_with_prefix(const char *prefix, const char *name) { return xcat(prefix, case_first(name, true)); }

/* ---- JavaPackagePolicy.OPEN_SOURCE, Scope ---- */

static PbMsg *file_options(const JFile *f) { return pb_msg(f->proto, FDP_OPTIONS); }

/* javaPackage(file): java_package when set (even empty), else the proto package */
static const char *java_package(const JFile *f)
{
	PbMsg *opts = file_options(f);
	return pb_has(opts, 1) ? jstr(opts, 1) : f->package;
}

/* PackageScope.nestedClass(simpleName): ClassName(pkg, simpleName), with its checks */
static KpType *package_nested_class(const char *package, const char *simple_name) { return kp_class(package, simple_name); }

/* The line of GrpcClientStubGenerator.generateRpcStub that asks for the class of a message, or 0
 * elsewhere: the client stub is generated first, so the empty outer class name of a file fails
 * there. */
static int message_class_line;

/* ---- DescriptorUtil.kt: the outer class name ---- */

/* String.underscoresToCamel(): ASCII letters and digits; a letter after anything else but a
 * lower case letter is upper cased (characters beyond ASCII are dropped like other symbols). */
static const char *underscores_to_camel(const char *s)
{
	Buf b = {0};
	bool cap_next = true;
	for (const char *p = s; *p; p++) {
		char c = *p;
		if (c >= 'a' && c <= 'z') {
			buf_addc(&b, cap_next ? (char)(c - 'a' + 'A') : c);
			cap_next = false;
		} else if (c >= 'A' && c <= 'Z') {
			buf_addc(&b, c);
			cap_next = false;
		} else if (c >= '0' && c <= '9') {
			buf_addc(&b, c);
			cap_next = true;
		} else {
			cap_next = true;
		}
	}
	return xstrdup(buf_str(&b));
}

/* DescriptorProto.anyHaveNameRecursive(name) */
static bool any_have_name_recursive(const PbMsg *m, const char *name)
{
	if (str_eq(jstr(m, DP_NAME), name))
		return true;
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		if (str_eq(jstr(pb_msg_at(m, DP_ENUM_TYPE, i), EDP_NAME), name))
			return true;
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		if (any_have_name_recursive(pb_msg_at(m, DP_NESTED_TYPE, i), name))
			return true;
	return false;
}

static Map outer_class_names; /* the address of a JFile in hex -> its outer class simple name (per request) */

/* FileDescriptorProto.outerClassSimpleName: java_outer_classname unless empty, else the file name
 * (ProtoFileName.name: after the last '/', without ".proto") in camel case, with the suffix
 * "OuterClass" when a top-level enum, a message (or a type nested in it) or a service has that
 * name. */
static const char *outer_class_simple_name(const JFile *f)
{
	const char *key = xfmt("%llx", (long long)(uintptr_t)f);
	const char *cached = map_get(&outer_class_names, key);
	if (cached)
		return cached;
	const char *explicit_name = jstr(file_options(f), 8);
	const char *name = explicit_name;
	if (!explicit_name[0]) {
		const char *base = strrchr(f->name, '/');
		base = base ? base + 1 : f->name;
		size_t n = strlen(base);
		if (n >= 6 && str_eq(base + n - 6, ".proto"))
			n -= 6;
		name = underscores_to_camel(xstrndup(base, n));
		const PbMsg *proto = f->proto;
		bool dup = false;
		for (int i = 0; i < pb_count(proto, FDP_ENUM_TYPE) && !dup; i++)
			dup = str_eq(jstr(pb_msg_at(proto, FDP_ENUM_TYPE, i), EDP_NAME), name);
		for (int i = 0; i < pb_count(proto, FDP_MESSAGE_TYPE) && !dup; i++)
			dup = any_have_name_recursive(pb_msg_at(proto, FDP_MESSAGE_TYPE, i), name);
		for (int i = 0; i < pb_count(proto, FDP_SERVICE) && !dup; i++)
			dup = str_eq(jstr(pb_msg_at(proto, FDP_SERVICE, i), SDP_NAME), name);
		if (dup)
			name = xcat(name, "OuterClass");
	}
	map_put(&outer_class_names, key, (void *)name);
	return name;
}

/* ---- GeneratorConfig.kt ---- */

/* FileDescriptor.outerClass() */
static KpType *outer_class(const JFile *f) { return package_nested_class(java_package(f), outer_class_simple_name(f)); }

/* Descriptor.messageClass(): nested in the containing type, else in the Java package
 * (java_multiple_files), else in the outer class. depth: the messages it is nested in. */
static KpType *message_class_nested(const JMsg *m, int depth)
{
	if (m->parent)
		return kp_nested_class(message_class_nested(m->parent, depth + 1), m->name);
	if (jpb_bool(file_options(m->file), 10, false))
		return package_nested_class(java_package(m->file), m->name);
	if (!outer_class_simple_name(m->file)[0] && message_class_line) {
		/* the IllegalArgumentException of ClassName(pkg, "") */
		Buf t = {0};
		buf_adds(&t, FRAME("com.squareup.kotlinpoet.ClassName.<init>(ClassName.kt:58)")
				     FRAME("io.grpc.kotlin.generator.protoc.PackageScope.nestedClass(Scope.kt:39)")
					     FRAME("io.grpc.kotlin.generator.protoc.GeneratorConfig.outerClass(GeneratorConfig.kt:55)")
						     FRAME("io.grpc.kotlin.generator.protoc.GeneratorConfig.messageClass(GeneratorConfig.kt:77)"));
		for (int i = 0; i < depth; i++)
			buf_adds(&t, FRAME("io.grpc.kotlin.generator.protoc.GeneratorConfig.messageClass(GeneratorConfig.kt:75)"));
		buf_fmt(&t, FRAME("io.grpc.kotlin.generator.GrpcClientStubGenerator.generateRpcStub(GrpcClientStubGenerator.kt:%d)"),
			message_class_line);
		buf_adds(&t, FRAME("io.grpc.kotlin.generator.GrpcClientStubGenerator.generateStub(GrpcClientStubGenerator.kt:133)")
				     FRAME("io.grpc.kotlin.generator.GrpcClientStubGenerator.generate(GrpcClientStubGenerator.kt:100)")
					     FRAMES_GENERATE);
		error_trace = xstrdup(buf_str(&t));
	}
	return kp_nested_class(outer_class(m->file), m->name);
}

static KpType *message_class(const JMsg *m) { return message_class_nested(m, 0); }

/* ---- KtPoetUtil.kt: FileSpec.path ---- */

/* Paths.get(package segments..., "<name>.kt").toString() of a UnixPath: the non-empty components
 * joined by '/', runs of '/' collapsed and trailing ones removed (normalizeAndCheck). */
static const char *file_spec_path(const char *package, const char *name)
{
	Buf joined = {0};
	if (package[0]) {
		Vec parts = str_split(package, '.');
		for (int i = 0; i < parts.n; i++) {
			const char *p = parts.v[i];
			if (!p[0])
				continue;
			if (joined.n)
				buf_addc(&joined, '/');
			buf_adds(&joined, p);
		}
	}
	if (joined.n)
		buf_addc(&joined, '/');
	buf_adds(&joined, xcat(name, ".kt"));
	const char *s = buf_str(&joined);
	size_t n = strlen(s);
	while (n > 1 && s[n - 1] == '/')
		n--;
	Buf b = {0};
	for (size_t i = 0; i < n; i++)
		if (s[i] != '/' || !b.n || b.p[b.n - 1] != '/')
			buf_addc(&b, s[i]);
	return xstrdup(buf_str(&b));
}

/* ---- AbstractGeneratorRunner.kt, CodeGenerators.kt ---- */

typedef struct {
	KpFile *spec;
	const char *name, *content;
} ResponseFile;

/* The frames of an IllegalArgumentException of failIfEscapeInvalid (KotlinPoet 2.2.0) in
 * FileSpec.toString: escapeSegmentsIfNecessary of the package line, or of the first class of a
 * message, the type of the property of a method in the object of a service (properties come
 * before the nested types, and the classes in the package of the file are covered by its package
 * line; the first pass fails before any import). */
#define FRAMES_ESCAPE                                                                                                  \
	FRAME("com.squareup.kotlinpoet.UtilKt.failIfEscapeInvalid(Util.kt:277)")                                      \
	FRAME("com.squareup.kotlinpoet.UtilKt.escapeIfNecessary(Util.kt:287)")                                        \
	FRAME("com.squareup.kotlinpoet.UtilKt.escapeIfNecessary$default(Util.kt:283)")                                \
	FRAME("com.squareup.kotlinpoet.UtilKt.escapeSegmentsIfNecessary$lambda$14(Util.kt:356)")                      \
	FRAME("kotlin.text.StringsKt__AppendableKt.appendElement(Appendable.kt:84)")                                  \
	FRAME("kotlin.collections.CollectionsKt___CollectionsKt.joinTo(_Collections.kt:3601)")                        \
	FRAME("kotlin.collections.CollectionsKt___CollectionsKt.joinToString(_Collections.kt:3618)")                  \
	FRAME("kotlin.collections.CollectionsKt___CollectionsKt.joinToString$default(_Collections.kt:3617)")          \
	FRAME("com.squareup.kotlinpoet.UtilKt.escapeSegmentsIfNecessary(Util.kt:356)")                                \
	FRAME("com.squareup.kotlinpoet.UtilKt.escapeSegmentsIfNecessary$default(Util.kt:354)")
#define FRAMES_TO_STRING                                                                                               \
	FRAME("com.squareup.kotlinpoet.FileSpec.writeTo$lambda$2(FileSpec.kt:88)")                                    \
	FRAME("com.squareup.kotlinpoet.CodeWriter$Companion.withCollectedImports(CodeWriter.kt:752)")                 \
	FRAME("com.squareup.kotlinpoet.FileSpec.writeTo(FileSpec.kt:84)")                                             \
	FRAME("com.squareup.kotlinpoet.FileSpec.toString(FileSpec.kt:222)")                                           \
	FRAME("io.grpc.kotlin.generator.protoc.CodeGenerators.toCodeGeneratorResponseFile(CodeGenerators.kt:69)")     \
	FRAME("io.grpc.kotlin.generator.protoc.AbstractGeneratorRunner.mainAsProtocPlugin(AbstractGeneratorRunner.kt:106)") \
	FRAMES_MAIN

/* A segment of the package that escapeIfNecessary rejects (one of .;[]/<>: or a backslash; the
 * backticks of the escape add none) */
static bool package_escape_invalid(const char *package)
{
	for (const char *c = package; *c; c++)
		if (strchr(";[]/<>:\\", *c))
			return true;
	return false;
}

static void response_file_content(void *arg)
{
	ResponseFile *rf = arg;
	rf->content = kp_file_to_string(rf->spec);
}

typedef struct {
	PbMsg *req;
	Vec out; /* ResponseFile* */
} Runner;

/* GeneratorRunner.generateCodeForFile: the FileSpec of the file, or NULL (generator.c) */
static KpFile *generate_code_for_file(const JFile *file);

/* ProtoFileName.toString() */
static const char *proto_file_name(const char *path) { return xcat("ProtoFileName(path=", path, ")"); }

/* Map.getValue of a missing key, in CodeGenerators.descriptorMap (a dependency) or in
 * mainAsProtocPlugin (a file to generate) */
NORETURN COLD static void missing_key(const char *path, bool dependency)
{
	error_trace = dependency ? FRAME("kotlin.collections.MapsKt__MapWithDefaultKt.getOrImplicitDefaultNullable(MapWithDefault.kt:24)")
					   FRAME("kotlin.collections.MapsKt__MapsKt.getValue(Maps.kt:370)")
						   FRAME("io.grpc.kotlin.generator.protoc.CodeGenerators.descriptorMap(CodeGenerators.kt:34)")
							   FRAME("io.grpc.kotlin.generator.protoc.AbstractGeneratorRunner.mainAsProtocPlugin(AbstractGeneratorRunner.kt:53)")
								   FRAMES_MAIN
				 : FRAME("kotlin.collections.MapsKt__MapWithDefaultKt.getOrImplicitDefaultNullable(MapWithDefault.kt:24)")
					   FRAME("kotlin.collections.MapsKt__MapsKt.getValue(Maps.kt:370)")
						   FRAME("io.grpc.kotlin.generator.protoc.AbstractGeneratorRunner.mainAsProtocPlugin(AbstractGeneratorRunner.kt:55)")
							   FRAMES_MAIN;
	java_exception("java.util.NoSuchElementException", xcat("Key ", proto_file_name(path), " is missing in the map."));
}

extern const PbMsgDef pbd_NamePart;

/* CodeGeneratorRequest.parseFrom checks isInitialized: the required fields of
 * UninterpretedOption.NamePart. */
static bool request_initialized(const PbMsg *m)
{
	if (m->def == &pbd_NamePart && (!pb_has(m, 1) || !pb_has(m, 2)))
		return false;
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].type == PT_MESSAGE)
			for (int j = 0; j < pb_count(m, m->def->f[i].num); j++)
				if (!request_initialized(pb_msg_at(m, m->def->f[i].num, j)))
					return false;
	return true;
}

/* The lambda of codeGeneratorResponse in mainAsProtocPlugin, then the files of the response. */
static void runner_build(void *arg)
{
	Runner *r = arg;
	jpool_reset();
	outer_class_names = (Map){0};
	error_trace = NULL;
	message_class_line = 0;
	/* CodeGenerators.descriptorMap: every file built from the files before it */
	Map byname = {0};
	for (int i = 0; i < pb_count(r->req, CGR_PROTO_FILE); i++) {
		PbMsg *proto = pb_msg_at(r->req, CGR_PROTO_FILE, i);
		for (int d = 0; d < pb_count(proto, FDP_DEPENDENCY); d++) {
			const char *dep = jstr_at(proto, FDP_DEPENDENCY, d);
			if (!map_get(&byname, dep))
				missing_key(dep, true);
		}
		JFile *f = jfile_build(proto, &byname);
		map_put(&byname, f->name, f);
	}
	Vec files = {0};
	for (int i = 0; i < pb_count(r->req, CGR_FILE_TO_GENERATE); i++) {
		const char *name = jstr_at(r->req, CGR_FILE_TO_GENERATE, i);
		JFile *f = map_get(&byname, name);
		if (!f)
			missing_key(name, false);
		vec_push(&files, f);
	}
	Vec specs = {0};
	for (int i = 0; i < files.n; i++) {
		KpFile *spec = generate_code_for_file(files.v[i]);
		if (spec)
			vec_push(&specs, spec);
	}
	/* toCodeGeneratorResponseFile */
	for (int i = 0; i < specs.n; i++) {
		KpFile *spec = specs.v[i];
		ResponseFile *rf = xalloc(sizeof *rf);
		rf->spec = spec;
		rf->name = file_spec_path(spec->package, spec->name);
		if (try_run(response_file_content, rf)) {
			Str msg = fail_message();
			if (str_has_prefix(msg, STR_LIT("java.lang.IllegalArgumentException: Can't escape identifier ")))
				error_trace = package_escape_invalid(spec->package)
						      ? FRAMES_ESCAPE FRAME("com.squareup.kotlinpoet.FileSpec.emit(FileSpec.kt:164)") FRAMES_TO_STRING
						      : FRAMES_ESCAPE FRAME("com.squareup.kotlinpoet.ClassName.emit$kotlinpoet(ClassName.kt:190)")
							FRAME("com.squareup.kotlinpoet.ParameterizedTypeName.emit$kotlinpoet(ParameterizedTypeName.kt:91)")
							FRAME("com.squareup.kotlinpoet.CodeWriter.emitCode(CodeWriter.kt:339)")
							FRAME("com.squareup.kotlinpoet.CodeWriter.emitCode$default(CodeWriter.kt:270)")
							FRAME("com.squareup.kotlinpoet.CodeWriter.emitCode(CodeWriter.kt:268)")
							FRAME("com.squareup.kotlinpoet.PropertySpec.emit$kotlinpoet(PropertySpec.kt:100)")
							FRAME("com.squareup.kotlinpoet.PropertySpec.emit$kotlinpoet$default(PropertySpec.kt:67)")
							FRAME("com.squareup.kotlinpoet.TypeSpec.emit$kotlinpoet(TypeSpec.kt:294)")
							FRAME("com.squareup.kotlinpoet.TypeSpec.emit$kotlinpoet$default(TypeSpec.kt:111)")
							FRAME("com.squareup.kotlinpoet.FileSpec.emit(FileSpec.kt:201)") FRAMES_TO_STRING;
			fail_str(msg);
		}
		vec_push(&r->out, rf);
	}
}

/* AbstractGeneratorRunner.mainAsProtocPlugin */
static const char *runner_main(PbMsg *req, Buf *out)
{
	if (!request_initialized(req)) {
		/* the IOException around the InvalidProtocolBufferException of parseFrom ends the JVM */
		pb_put_bytes(out, CGRESP_ERROR,
			     STR_LIT("Exception in thread \"main\" java.io.IOException: Attempted to run proto extension generator as "
				     "protoc plugin, but could not read\nCodeGeneratorRequest.\n"));
		return NULL;
	}
	Runner r = {0};
	r.req = req;
	kp_select_version(KP_V2_2_0);
	const char *err = try_run(runner_build, &r);
	kp_select_version(KP_V2_4_0);
	if (err) {
		Str msg = fail_message();
		if (str_eq(msg.p, "java.lang.StackOverflowError")) {
			/* an Error, not caught by codeGeneratorResponse: it ends the JVM (the frames, the
			 * recursion of CodeWriter.emitCode, are not reproduced) */
			pb_put_bytes(out, CGRESP_ERROR, STR_LIT("Exception in thread \"main\" java.lang.StackOverflowError\n"));
			return NULL;
		}
		Buf e = {0};
		buf_add_str(&e, msg);
		if (!str_has_prefix(msg, STR_LIT(UNSUPPORTED))) {
			/* Throwables.getStackTraceAsString: the first line, then the frames when known */
			while (e.n && e.p[e.n - 1] == '\n')
				e.n--;
			buf_addc(&e, '\n');
			if (error_trace)
				buf_adds(&e, error_trace);
		}
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&e));
	}
	/* FEATURE_PROTO3_OPTIONAL, set before the files are built */
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 1);
	if (err)
		return NULL;
	for (int i = 0; i < r.out.n; i++) {
		ResponseFile *f = r.out.v[i];
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, str_c(f->content));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
	return NULL;
}
