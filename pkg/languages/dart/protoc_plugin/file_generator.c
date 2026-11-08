/* Port of protoc_plugin 25.1.0 lib/src/file_generator.dart.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

#define protobufImportUrl "package:protobuf/protobuf.dart"

static const char *const fileIgnores[] = {
	"annotate_overrides", "camel_case_types", "comment_references", "constant_identifier_names",
	"curly_braces_in_flow_control_structures", "deprecated_member_use_from_same_package", "library_prefixes",
	"non_constant_identifier_names", "prefer_relative_imports", NULL,
};

/* Edition names (ProtobufEnum.toString of google.protobuf.Edition). */
static const char *edition_name(int e)
{
	switch (e) {
	case 0: return "EDITION_UNKNOWN";
	case 900: return "EDITION_LEGACY";
	case 998: return "EDITION_PROTO2";
	case 999: return "EDITION_PROTO3";
	case 1000: return "EDITION_2023";
	case 1001: return "EDITION_2024";
	case 1: return "EDITION_1_TEST_ONLY";
	case 2: return "EDITION_2_TEST_ONLY";
	case 99997: return "EDITION_99997_TEST_ONLY";
	case 99998: return "EDITION_99998_TEST_ONLY";
	case 99999: return "EDITION_99999_TEST_ONLY";
	default: return "EDITION_MAX";
	}
}

/* _getEditionDefaults over ProtobufInternalDartEditionDefaults (lib/src/gen/google/protobuf/
 * dart_edition_defaults.pb.dart): minimum EDITION_PROTO2, maximum EDITION_2024. */
static Features getEditionDefaults(int edition)
{
	static const struct {
		int edition;
		int v[9];
	} defaults[] = {
		{900, {0, 1, 2, 2, 3, 1, 2, 2, 1}},
		{999, {0, 2, 1, 1, 2, 1, 1, 2, 1}},
		{1000, {0, 1, 1, 1, 2, 1, 1, 2, 1}},
		{1001, {0, 1, 1, 1, 2, 1, 1, 1, 2}},
	};
	if (edition < 998)
		dart_throw(xfmt("Invalid argument(s): Edition %s is earlier than the minimum supported edition EDITION_PROTO2!",
				edition_name(edition)));
	if (edition > 1001)
		dart_throw(xfmt("Invalid argument(s): Edition %s is later than the maximum supported edition EDITION_2024!",
				edition_name(edition)));
	int found = -1;
	for (int i = 0; i < 4; i++) {
		if (defaults[i].edition > edition)
			break;
		found = i;
	}
	Features f;
	memcpy(f.v, defaults[found].v, sizeof f.v);
	return f;
}

static int getEdition(PbMsg *file)
{
	int e = (int)pb_int(file, FDP_EDITION, 0);
	if (e != EDITION_UNKNOWN)
		return e;
	if (strcmp(pb_str(file, FDP_SYNTAX), "proto3") == 0)
		return EDITION_PROTO3;
	return EDITION_PROTO2;
}

/* ^\w+ (startsWith of the RegExp r'^\w+$': the whole string) */
static bool isDartIdentifierWord(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
			return false;
	return true;
}

typedef struct {
	PbMsg *msg;
	PbMixin *resolved;
} DeclaredMixin;

static PbMixin *resolveMixin(Map *dartMixins, Map *pbMixins, const char *name, int depth)
{
	PbMixin *p = map_get(pbMixins, name);
	if (p)
		return p;
	PbMsg *dartMixin = map_get(dartMixins, name);
	if (dartMixin) {
		if (depth > 10000)
			fail("protoc_plugin: mixin parent chain too long");
		p = xalloc(sizeof *p);
		p->name = pb_str(dartMixin, DPB_MIXIN_NAME);
		p->importFrom = pb_str(dartMixin, DPB_MIXIN_IMPORT_FROM);
		p->helper = -1;
		p->parent = resolveMixin(dartMixins, pbMixins, pb_str(dartMixin, DPB_MIXIN_PARENT), depth + 1);
		map_put(pbMixins, name, p);
		return p;
	}
	return (PbMixin *)findMixin(name);
}

/* _getDeclaredMixins */
static void getDeclaredMixins(PbMsg *desc, Map *pbMixins)
{
	PbMsg *options = pb_msg(desc, FDP_OPTIONS);
	if (!pb_has(desc, FDP_OPTIONS) || !pb_has(options, DPB_EXT_IMPORTS))
		return;
	const char *descName = pb_str(desc, FDP_NAME);
#define MIXIN_ERROR(...) dart_throw(xfmt("Option \"mixins\" in %s: %s", descName, xfmt(__VA_ARGS__)))
	Map dartMixins = {0};
	Vec order = {0};
	PbMsg *imports = pb_msg(options, DPB_EXT_IMPORTS);
	for (int i = 0; i < pb_count(imports, DPB_IMPORTS_MIXINS); i++) {
		PbMsg *mixin = pb_msg_at(imports, DPB_IMPORTS_MIXINS, i);
		const char *name = pb_str(mixin, DPB_MIXIN_NAME);
		if (map_has(&dartMixins, name))
			MIXIN_ERROR("Duplicate mixin name: \"%s\"", name);
		if (!isDartIdentifierWord(name))
			MIXIN_ERROR("\"%s\" is not a valid dart class identifier", name);
		if (pb_has(mixin, DPB_MIXIN_PARENT) && !isDartIdentifierWord(pb_str(mixin, DPB_MIXIN_PARENT)))
			MIXIN_ERROR("Mixin parent \"%s\" of \"%s\" is not a valid dart class identifier", pb_str(mixin, DPB_MIXIN_PARENT), name);
		map_put(&dartMixins, name, mixin);
		vec_push(&order, mixin);
	}
	/* Detect cycles and unknown parents (chains already found good end the walk early). */
	Map good = {0};
	for (int i = 0; i < order.n; i++) {
		PbMsg *mixin = order.v[i];
		if (!pb_has(mixin, DPB_MIXIN_PARENT))
			continue;
		PbMsg *current = mixin;
		Vec chain = {0};
		Map inChain = {0};
		while (pb_has(current, DPB_MIXIN_PARENT)) {
			const char *parentName = pb_str(current, DPB_MIXIN_PARENT);
			bool declared = map_has(&dartMixins, parentName);
			if (!declared && findMixin(parentName))
				break;
			if (!declared)
				MIXIN_ERROR("Unknown mixin parent \"%s\" of \"%s\"", pb_str(mixin, DPB_MIXIN_PARENT), pb_str(current, DPB_MIXIN_NAME));
			if (map_has(&inChain, parentName)) {
				Buf b = {0};
				for (int k = 0; k < chain.n; k++) {
					buf_adds(&b, chain.v[k]);
					buf_adds(&b, "->");
				}
				buf_adds(&b, parentName);
				MIXIN_ERROR("Cycle in parent chain: %s", buf_str(&b));
			}
			if (map_has(&good, parentName))
				break;
			vec_push(&chain, (void *)parentName);
			map_put(&inChain, parentName, (void *)1);
			current = map_get(&dartMixins, parentName);
		}
		map_put(&good, pb_str(mixin, DPB_MIXIN_NAME), (void *)1);
		for (int k = 0; k < chain.n; k++)
			map_put(&good, chain.v[k], (void *)1);
	}
#undef MIXIN_ERROR
	for (int i = 0; i < order.n; i++)
		resolveMixin(&dartMixins, pbMixins, pb_str(order.v[i], DPB_MIXIN_NAME), 0);
}

static FileGen *file_new(PbMsg *descriptor, const Options *options)
{
	FileGen *f = xalloc(sizeof *f);
	f->descriptor = descriptor;
	f->options = options;
	f->pc.kind = PC_FILE;
	f->pc.fileGen = f;
	f->pc.classname = "";
	f->pc.package = pb_str(descriptor, FDP_PACKAGE);
	f->pc.fullName = f->pc.package;
	forbiddenTopLevelNames(&f->usedTopLevelNames);
	forbiddenTopLevelNames(&f->usedTopLevelServiceNames);
	reservedMemberNames(&f->usedExtensionNames);
	bool absolute;
	f->protoFileUri = dpath_file_uri(pb_str(descriptor, FDP_NAME), &absolute);
	f->edition = getEdition(descriptor);
	Features defaults = getEditionDefaults(f->edition);
	PbMsg *fileOptions = pb_msg(descriptor, FDP_OPTIONS);
	f->pc.features = resolveFeatures(&defaults, features_of(pb_msg(fileOptions, FILEO_FEATURES)));
	if (absolute)
		dart_throw("FAILURE: Import with absolute path is not supported");

	Map declaredMixins = {0};
	getDeclaredMixins(descriptor, &declaredMixins);
	const char *defaultMixinName = pb_str(fileOptions, DPB_EXT_DEFAULT_MIXIN);
	const PbMixin *defaultMixin = map_get(&declaredMixins, defaultMixinName);
	if (!defaultMixin)
		defaultMixin = findMixin(defaultMixinName);
	if (!defaultMixin && *defaultMixinName)
		dart_throw(xfmt("Option default_mixin on file %s: Unknown mixin %s", pb_str(descriptor, FDP_NAME), defaultMixinName));

	for (int i = 0; i < pb_count(descriptor, FDP_ENUM_TYPE); i++)
		vec_push(&f->enumGenerators, enum_new(pb_msg_at(descriptor, FDP_ENUM_TYPE, i), &f->pc, &f->usedTopLevelNames, i, 5));
	for (int i = 0; i < pb_count(descriptor, FDP_MESSAGE_TYPE); i++)
		vec_push(&f->messageGenerators, msg_new(pb_msg_at(descriptor, FDP_MESSAGE_TYPE, i), &f->pc, &declaredMixins,
							defaultMixin, &f->usedTopLevelNames,
							buildTopLevelMessagePath(f->pc.fieldPath, i)));
	for (int i = 0; i < pb_count(descriptor, FDP_EXTENSION); i++)
		vec_push(&f->extensionGenerators, ext_new(pb_msg_at(descriptor, FDP_EXTENSION, i), &f->pc, &f->usedExtensionNames, i, 7));
	for (int i = 0; i < pb_count(descriptor, FDP_SERVICE); i++) {
		PbMsg *service = pb_msg_at(descriptor, FDP_SERVICE, i);
		if (options->useGrpc) {
			vec_push(&f->grpcGenerators, grpc_new(service, f, i));
		} else {
			ServiceGen *s = svc_new(service, f, &f->usedTopLevelServiceNames, i);
			vec_push(&f->serviceGenerators, s);
			vec_push(&f->clientApiGenerators, client_new(s, &f->usedTopLevelNames, i));
		}
	}
	return f;
}

static void file_resolve(FileGen *f, GenerationContext *ctx)
{
	if (f->linked)
		dart_throw("Bad state: cross references already resolved");
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_resolve(VEC_AT(f->messageGenerators, MsgGen, i), ctx);
	for (int i = 0; i < f->extensionGenerators.n; i++)
		ext_resolve(VEC_AT(f->extensionGenerators, ExtGen, i), ctx);
	f->linked = true;
}

/* importPrefix(container): `$<n>` in the order of first use. */
static const char *importPrefix(FileGen *f, Pc *container)
{
	const char *key = container->fileGen->protoFileUri;
	const char *p = map_get(&f->importPrefixes, key);
	if (!p) {
		p = xfmt("$%d", f->nImportPrefixes++);
		map_put(&f->importPrefixes, key, (void *)p);
	}
	return p;
}

/* _writeHeading */
static void file_writeHeading(FileGen *f, Out *o, const char *extraIgnore)
{
	Vec ignores = {0};
	for (const char *const *p = fileIgnores; *p; p++)
		vec_push(&ignores, (void *)*p);
	if (extraIgnore) {
		bool dup = false;
		for (int i = 0; i < ignores.n; i++)
			dup |= strcmp(ignores.v[i], extraIgnore) == 0;
		if (!dup) {
			int i = 0;
			while (i < ignores.n && dart_compare(ignores.v[i], extraIgnore) < 0)
				i++;
			vec_push(&ignores, NULL);
			memmove(&ignores.v[i + 1], &ignores.v[i], (size_t)(ignores.n - 1 - i) * sizeof *ignores.v);
			ignores.v[i] = (void *)extraIgnore;
		}
	}
	Vec lines = {0};
	vec_push(&lines, xcat("// ignore_for_file: ", ignores.v[0]));
	for (int i = 1; i < ignores.n; i++) {
		const char *ignore = ignores.v[i];
		const char *last = lines.v[lines.n - 1];
		if (strlen(last) + strlen(ignore) + 2 > 80)
			vec_push(&lines, xcat("// ignore_for_file: ", ignore));
		else
			lines.v[lines.n - 1] = xcat(last, ", ", ignore);
	}
	out_comment(o, "// This is a generated file - do not edit.", 0);
	out_comment(o, "//", 0);
	out_comment(o, xfmt("// Generated from %s.", pb_str(f->descriptor, FDP_NAME)), 0);
	out_blank(o);
	out_comment(o, "// @dart = 3.3", 0);
	out_blank(o);
	for (int i = 0; i < lines.n; i++)
		out_comment(o, lines.v[i], 0);
	out_blank(o);
}

/* _addImport */
static void file_addImport(FileGen *f, ImportWriter *w, FileGen *target, const char *ext)
{
	const char *url = resolveImport(target->protoFileUri, f->protoFileUri, ext);
	if (strcmp(ext, ".pb.dart") == 0 || strcmp(f->protoFileUri, target->protoFileUri) != 0)
		iw_addImport(w, url, importPrefix(f, &target->pc));
	else
		iw_addImport(w, url, NULL);
}

static void file_addExport(FileGen *f, ImportWriter *w, const char *target, const char *ext)
{
	iw_addExport(w, resolveImport(target, f->protoFileUri, ext), NULL);
}

static const char *publicDependencyUri(FileGen *f, int i)
{
	int idx = (int)pb_int_at(f->descriptor, FDP_PUBLIC_DEPENDENCY, i);
	int n = pb_count(f->descriptor, FDP_DEPENDENCY);
	if (idx < 0 || idx >= n)
		dart_throw(xfmt("RangeError (index): Index out of range: index should be less than %d: %d", n, idx));
	bool absolute;
	return dpath_file_uri(pb_str_at(f->descriptor, FDP_DEPENDENCY, idx), &absolute);
}

static int file_enumCount(FileGen *f)
{
	int count = f->enumGenerators.n;
	for (int i = 0; i < f->messageGenerators.n; i++)
		count += msg_enumCount(VEC_AT(f->messageGenerators, MsgGen, i));
	return count;
}

static bool file_hasEnums(FileGen *f) { return file_enumCount(f) > 0; }

static void file_writeMainHeader(FileGen *f, Out *o)
{
	ImportWriter w = {0};
	if (f->clientApiGenerators.n)
		iw_addImport(&w, "dart:async", asyncImportPrefix);
	iw_addImport(&w, "dart:core", coreImportPrefix);
	bool needsFixnum = false;
	for (int i = 0; i < f->messageGenerators.n && !needsFixnum; i++)
		needsFixnum = msg_needsFixnumImport(VEC_AT(f->messageGenerators, MsgGen, i));
	for (int i = 0; i < f->extensionGenerators.n && !needsFixnum; i++)
		needsFixnum = ext_needsFixnumImport(VEC_AT(f->extensionGenerators, ExtGen, i));
	if (needsFixnum)
		iw_addImport(&w, "package:fixnum/fixnum.dart", fixnumImportPrefix);
	if (f->messageGenerators.n || f->extensionGenerators.n || f->clientApiGenerators.n)
		iw_addImport(&w, protobufImportUrl, protobufImportPrefix);
	/* findMixinImports: sorted, distinct */
	Vec mixins = {0}, uris = {0};
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_addMixinsTo(VEC_AT(f->messageGenerators, MsgGen, i), &mixins);
	for (int i = 0; i < mixins.n; i++) {
		const char *uri = ((const PbMixin *)mixins.v[i])->importFrom;
		int k = 0;
		while (k < uris.n && dart_compare(uris.v[k], uri) < 0)
			k++;
		if (k < uris.n && strcmp(uris.v[k], uri) == 0)
			continue;
		vec_push(&uris, NULL);
		memmove(&uris.v[k + 1], &uris.v[k], (size_t)(uris.n - 1 - k) * sizeof *uris.v);
		uris.v[k] = (void *)uri;
	}
	for (int i = 0; i < uris.n; i++)
		iw_addImport(&w, uris.v[i], mixinImportPrefix);
	/* _findProtosToImport */
	Vec imports = {0}, enumImports = {0};
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_addImportsTo(VEC_AT(f->messageGenerators, MsgGen, i), &imports, &enumImports);
	for (int i = 0; i < f->extensionGenerators.n; i++)
		ext_addImportsTo(VEC_AT(f->extensionGenerators, ExtGen, i), &imports, &enumImports);
	for (int i = 0; i < f->serviceGenerators.n; i++)
		svc_addImportsTo(VEC_AT(f->serviceGenerators, ServiceGen, i), &imports);
	int self = vec_index(&imports, f);
	if (self >= 0) {
		memmove(&imports.v[self], &imports.v[self + 1], (size_t)(imports.n - self - 1) * sizeof *imports.v);
		imports.n--;
	}
	for (int i = 0; i < imports.n; i++)
		file_addImport(f, &w, imports.v[i], ".pb.dart");
	for (int i = 0; i < enumImports.n; i++)
		if (vec_index(&imports, enumImports.v[i]) < 0)
			file_addImport(f, &w, enumImports.v[i], ".pbenum.dart");
	iw_addExport(&w, protobufImportUrl, "GeneratedMessageGenericExtensions");
	for (int i = 0; i < pb_count(f->descriptor, FDP_PUBLIC_DEPENDENCY); i++)
		file_addExport(f, &w, publicDependencyUri(f, i), ".pb.dart");
	if (file_hasEnums(f))
		iw_addExport(&w, resolveImport(f->protoFileUri, f->protoFileUri, ".pbenum.dart"), NULL);
	file_writeHeading(f, o, iw_hasSrcImport(&w) ? "implementation_imports" : NULL);
	iw_emit(&w, o);
}

static void file_generateMainFile(FileGen *f, Out *o)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	file_writeMainHeader(f, o);
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_generate(VEC_AT(f->messageGenerators, MsgGen, i), o);
	if (f->extensionGenerators.n) {
		const char *className = extensionClassName(f->descriptor, &f->usedTopLevelNames);
		Node *cls = out_add(o, ds_class(NONE, false, className, NULL, NONE, NONE));
		OutBody body = out_begin_body(o, &cls->body->list);
		for (int i = 0; i < f->extensionGenerators.n; i++)
			ext_generate(VEC_AT(f->extensionGenerators, ExtGen, i), o);
		NodeList stmts = {0};
		for (int i = 0; i < f->extensionGenerators.n; i++) {
			const char *name = ext_name(VEC_AT(f->extensionGenerators, ExtGen, i));
			const char *dot = strchr(name, '.');
			Node *arg = dot ? ds_path(xstrndup(name, (size_t)(dot - name)), dot + 1, (char *)0) : ds_id(name);
			ds_push(&stmts, ds_expr_stmt(ds_call(ds_id("registry"), "add", NULL, ds_args(arg, (Node *)0))));
		}
		out_add(o, ds_method(NONE, KW_STATIC, ds_type(NULL, "void", NULL, false), KW_NONE, "registerAllExtensions",
				     ds_params(NL(ds_param(pb_type("ExtensionRegistry"), "registry", NULL)), NONE, false),
				     ds_bbody(KW_NONE, false, ds_block(stmts))));
		out_end_body(o, body, cls->body->rb);
		out_blank(o); /* '}\n' */
	}
	for (int i = 0; i < f->clientApiGenerators.n; i++)
		client_generate(VEC_AT(f->clientApiGenerators, ClientApiGen, i), o);
}

static void file_generateEnumFile(FileGen *f, Out *o)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	file_writeHeading(f, o, NULL);
	ImportWriter w = {0};
	if (file_hasEnums(f)) {
		iw_addImport(&w, "dart:core", coreImportPrefix);
		iw_addImport(&w, protobufImportUrl, protobufImportPrefix);
	}
	for (int i = 0; i < pb_count(f->descriptor, FDP_PUBLIC_DEPENDENCY); i++)
		file_addExport(f, &w, publicDependencyUri(f, i), ".pbenum.dart");
	if (iw_hasImports(&w))
		iw_emit(&w, o);
	for (int i = 0; i < f->enumGenerators.n; i++)
		enum_generate(VEC_AT(f->enumGenerators, EnumGen, i), o);
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_generateEnums(VEC_AT(f->messageGenerators, MsgGen, i), o);
}

static Str file_generateServerFile(FileGen *f)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	Out o;
	out_init(&o, pb_str(f->descriptor, FDP_NAME), f->options->generateMetadata);
	file_writeHeading(f, &o, "deprecated_member_use_from_same_package");
	ImportWriter w = {0};
	if (f->serviceGenerators.n) {
		iw_addImport(&w, "dart:async", asyncImportPrefix);
		iw_addImport(&w, "dart:core", coreImportPrefix);
		iw_addImport(&w, protobufImportUrl, protobufImportPrefix);
	}
	Vec imports = {0};
	for (int i = 0; i < f->serviceGenerators.n; i++)
		svc_addImportsTo(VEC_AT(f->serviceGenerators, ServiceGen, i), &imports);
	for (int i = 0; i < imports.n; i++)
		file_addImport(f, &w, imports.v[i], ".pb.dart");
	if (f->serviceGenerators.n)
		file_addImport(f, &w, f, ".pbjson.dart");
	iw_addExport(&w, resolveImport(f->protoFileUri, f->protoFileUri, ".pb.dart"), NULL);
	if (iw_hasImports(&w))
		iw_emit(&w, &o);
	for (int i = 0; i < f->serviceGenerators.n; i++)
		svc_generate(VEC_AT(f->serviceGenerators, ServiceGen, i), &o);
	return out_emitSource(&o, true);
}

static Str file_generateGrpcFile(FileGen *f)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	Out o;
	out_init(&o, pb_str(f->descriptor, FDP_NAME), f->options->generateMetadata);
	file_writeHeading(f, &o, NULL);
	ImportWriter w = {0};
	iw_addImport(&w, "dart:async", asyncImportPrefix);
	iw_addImport(&w, "dart:core", coreImportPrefix);
	iw_addImport(&w, "package:grpc/service_api.dart", grpcImportPrefix);
	iw_addImport(&w, protobufImportUrl, protobufImportPrefix);
	Vec imports = {0};
	for (int i = 0; i < f->grpcGenerators.n; i++)
		svc_addImportsTo(VEC_AT(f->grpcGenerators, ServiceGen, i), &imports);
	for (int i = 0; i < imports.n; i++)
		file_addImport(f, &w, imports.v[i], ".pb.dart");
	iw_addExport(&w, resolveImport(f->protoFileUri, f->protoFileUri, ".pb.dart"), NULL);
	iw_emit(&w, &o);
	for (int i = 0; i < f->grpcGenerators.n; i++)
		grpc_generate(VEC_AT(f->grpcGenerators, ServiceGen, i), &o);
	return out_emitSource(&o, true);
}

/* writeBinaryDescriptor */
static void file_writeBinaryDescriptor(Out *o, const char *identifierName, const char *name, PbMsg *descriptor,
				       const char *qualifiedMessageName)
{
	Buf bytes = {0};
	dpb_write(descriptor, &bytes);
	const char *base64 = dart_base64_encode((const uint8_t *)bytes.p, bytes.n);
	out_comment(o, xfmt("/// Descriptor for `%s`. Decode as a `%s`.", name, qualifiedMessageName), 0);
	NodeList strings = {0};
	size_t n = strlen(base64);
	for (size_t i = 0; i < n; i += 74) {
		size_t len = n - i < 74 ? n - i : 74;
		Node *s = ds_str(xfmt("'%s'", xstrndup(base64 + i, len)));
		ds_nl(s, 1, 4);
		ds_push(&strings, s);
	}
	Node *args = strings.n == 0 ? ds_args((Node *)0) : strings.n == 1 ? ds_argl(strings) : ds_args(ds_adjacent(strings), (Node *)0);
	if (strings.n == 0)
		ds_tnl(args->rp, 1, 4);
	Node *init = ds_call(ds_id("$convert"), "base64Decode", NULL, args);
	out_add(o, ds_toplevel(NONE, KW_FINAL, ds_type("$typed_data", "Uint8List", NULL, false), identifierName, init));
}

static Str file_generateJsonFile(FileGen *f)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	Out o;
	out_init(&o, pb_str(f->descriptor, FDP_NAME), f->options->generateMetadata);
	file_writeHeading(f, &o, "unused_import");
	ImportWriter w = {0};
	iw_addImport(&w, "dart:convert", "$convert");
	iw_addImport(&w, "dart:core", coreImportPrefix);
	iw_addImport(&w, "dart:typed_data", "$typed_data");
	/* _findJsonProtosToImport */
	Vec imports = {0};
	for (int i = 0; i < f->messageGenerators.n; i++)
		msg_addConstantImportsTo(VEC_AT(f->messageGenerators, MsgGen, i), &imports);
	for (int i = 0; i < f->extensionGenerators.n; i++)
		ext_checkResolved(VEC_AT(f->extensionGenerators, ExtGen, i));
	for (int i = 0; i < f->serviceGenerators.n; i++)
		svc_addConstantImportsTo(VEC_AT(f->serviceGenerators, ServiceGen, i), &imports);
	int self = vec_index(&imports, f);
	if (self >= 0) {
		memmove(&imports.v[self], &imports.v[self + 1], (size_t)(imports.n - self - 1) * sizeof *imports.v);
		imports.n--;
	}
	for (int i = 0; i < imports.n; i++)
		file_addImport(f, &w, imports.v[i], ".pbjson.dart");
	iw_emit(&w, &o);
	for (int i = 0; i < f->enumGenerators.n; i++) {
		EnumGen *e = VEC_AT(f->enumGenerators, EnumGen, i);
		enum_generateConstants(e, &o);
		file_writeBinaryDescriptor(&o, xcat(lowerCaseFirstLetter(e->pc.classname), "Descriptor"), pb_str(e->descriptor, EDP_NAME),
					   e->descriptor, "google.protobuf.EnumDescriptorProto");
		out_blank(&o);
	}
	for (int i = 0; i < f->messageGenerators.n; i++) {
		MsgGen *m = VEC_AT(f->messageGenerators, MsgGen, i);
		msg_generateConstants(m, &o);
		file_writeBinaryDescriptor(&o, xcat(lowerCaseFirstLetter(m->pc.classname), "Descriptor"), pb_str(m->descriptor, DP_NAME),
					   m->descriptor, "google.protobuf.DescriptorProto");
		out_blank(&o);
	}
	for (int i = 0; i < f->serviceGenerators.n; i++) {
		ServiceGen *s = VEC_AT(f->serviceGenerators, ServiceGen, i);
		svc_generateConstants(s, &o);
		file_writeBinaryDescriptor(&o, svc_binaryDescriptorName(s), pb_str(s->descriptor, SDP_NAME), s->descriptor,
					   "google.protobuf.ServiceDescriptorProto");
		out_blank(&o);
	}
	return out_emitSource(&o, true);
}

typedef struct {
	const char *name;
	Str content;
} OutFile;

/* generateFiles */
static void file_generateFiles(FileGen *f, Vec *files)
{
	if (!f->linked)
		dart_throw("Bad state: not linked");
	const char *name = pb_str(f->descriptor, FDP_NAME);
	bool generateMetadata = f->options->generateMetadata;
	Out mainWriter, enumWriter;
	out_init(&mainWriter, name, generateMetadata);
	out_init(&enumWriter, name, generateMetadata);
	file_generateMainFile(f, &mainWriter);
	file_generateEnumFile(f, &enumWriter);
#define MAKE_FILE(ext_, content_)                                                                                        \
	do {                                                                                                           \
		OutFile *of = xalloc(sizeof *of);                                                                      \
		of->name = outputPathFor(f->protoFileUri, ext_);                                                        \
		of->content = (content_);                                                                               \
		vec_push(files, of);                                                                                   \
	} while (0)
	MAKE_FILE(".pb.dart", out_emitSource(&mainWriter, !generateMetadata));
	MAKE_FILE(".pbenum.dart", out_emitSource(&enumWriter, !generateMetadata));
	MAKE_FILE(".pbjson.dart", file_generateJsonFile(f));
	if (generateMetadata) {
		MAKE_FILE(".pb.dart.meta", out_metadataJson(&mainWriter));
		MAKE_FILE(".pbenum.dart.meta", out_metadataJson(&enumWriter));
	}
	if (f->options->useGrpc) {
		if (f->grpcGenerators.n)
			MAKE_FILE(".pbgrpc.dart", file_generateGrpcFile(f));
	} else if (f->serviceGenerators.n) {
		MAKE_FILE(".pbserver.dart", file_generateServerFile(f));
	}
#undef MAKE_FILE
}
