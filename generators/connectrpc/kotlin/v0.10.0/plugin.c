// Copyright 2022-2026 The Connect Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* A port of protoc-gen-connect-kotlin/src/main/kotlin/com/connectrpc/protocgen/connect/Main.kt and
 * internal/{Plugin,CodeGenerator}.kt of connect-kotlin v0.10.0: the request is parsed and built
 * into protobuf-java 4.36.2 descriptors twice (the second time with an ExtensionRegistry of all
 * extensions of the request), then the generator writes the response.
 *
 * Uncaught exceptions end the JVM with the stack trace on stderr, which is the error buf reports:
 * the port returns it as the response error. The frames are reproduced where the exception is
 * thrown by the plugin itself; for exceptions from inside protobuf-java and KotlinPoet only the
 * first line (Throwable.toString) is (README.md, known differences). */
#include "kotlinpoet.h"
#include "protobuf_java.h"

#include <string.h>

#define PLUGIN_CLASS "com.connectrpc.protocgen.connect.internal.Plugin"
#define PLUGIN_EXCEPTION PLUGIN_CLASS "$PluginException"
#define FRAMES_RUN                                                                                                     \
	"\tat " PLUGIN_CLASS ".run$default(Plugin.kt:84)\n"                                                            \
	"\tat com.connectrpc.protocgen.connect.Main$Companion.main(Main.kt:23)\n"                                      \
	"\tat com.connectrpc.protocgen.connect.Main.main(Main.kt)"

/* The prefix of the errors of what the port does not support (README.md). */
#define UNSUPPORTED "protoc-gen-uni does not support"

/* The stack frames of the exception being thrown, when known (NULL otherwise). */
static const char *plugin_trace;

NORETURN COLD static void plugin_throw(const char *exception, const char *trace)
{
	plugin_trace = trace;
	fail_str(str_c(exception));
}

/* A CodeGeneratorResponse.File of Plugin.Response.addFile */
typedef struct {
	const char *name, *content;
} PluginFile;

typedef struct {
	PbMsg *req;
	Map files;     /* DescriptorSource: name -> JFile* (HashMap filesByName) */
	Vec file_list; /* JFile*: request order, for the HashMap iteration order */
	Vec out;       /* PluginFile* */
} PluginRun;

/* ---- the request parse ---- */

extern const PbMsgDef pbd_NamePart;

/* CodeGeneratorRequest.parseFrom checks isInitialized: the required fields of
 * UninterpretedOption.NamePart. A missing one is an InvalidProtocolBufferException. */
static bool plugin_initialized(const PbMsg *m)
{
	if (m->def == &pbd_NamePart && (!pb_has(m, 1) || !pb_has(m, 2)))
		return false;
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].type == PT_MESSAGE)
			for (int j = 0; j < pb_count(m, m->def->f[i].num); j++)
				if (!plugin_initialized(pb_msg_at(m, m->def->f[i].num, j)))
					return false;
	return true;
}

/* ---- asDescriptors ---- */

typedef struct {
	PbMsg *proto;
	Map *byname;
	JFile *file;
} PluginBuild;

static void plugin_build(void *arg)
{
	PluginBuild *b = arg;
	b->file = jfile_build(b->proto, b->byname);
}

static void plugin_as_descriptors(PluginRun *r)
{
	jpool_reset();
	jpool_use_v4();
	for (int i = 0; i < pb_count(r->req, CGR_PROTO_FILE); i++) {
		PbMsg *proto = pb_msg_at(r->req, CGR_PROTO_FILE, i);
		for (int d = 0; d < pb_count(proto, FDP_DEPENDENCY); d++) {
			const char *dep = jstr_at(proto, FDP_DEPENDENCY, d);
			if (!map_get(&r->files, dep))
				plugin_throw(xcat(PLUGIN_EXCEPTION ": protoc asked plugin to generate a file but did not provide a "
							   "descriptor for a dependency (or provided it after the file that depends on it): ",
						  dep),
					     "\tat " PLUGIN_CLASS ".asDescriptors(Plugin.kt:189)\n\tat " PLUGIN_CLASS ".run(Plugin.kt:97)\n" FRAMES_RUN);
		}
		PluginBuild b = {proto, &r->files, NULL};
		if (try_run(plugin_build, &b)) {
			Str msg = fail_message();
			if (str_has_prefix(msg, STR_LIT("com.google.protobuf.Descriptors$DescriptorValidationException:")))
				plugin_throw(xcat(PLUGIN_EXCEPTION ": ", msg.p), NULL);
			plugin_throw(msg.p, NULL);
		}
		map_put(&r->files, b.file->name, b.file);
		vec_push(&r->file_list, b.file);
	}
}

/* ---- createExtensionRegistry ---- */

/* String.hashCode of a UTF-8 string, over its UTF-16 code units */
static int32_t plugin_java_hash(const char *s)
{
	uint32_t h = 0;
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		uint32_t cp;
		i += (size_t)utf8_decode(s + i, n - i, &cp);
		if (cp >= 0x10000) {
			h = 31 * h + (0xd800 + ((cp - 0x10000) >> 10));
			h = 31 * h + (0xdc00 + ((cp - 0x10000) & 0x3ff));
		} else {
			h = 31 * h + cp;
		}
	}
	return (int32_t)h;
}

typedef struct {
	JFile *file;
	uint32_t hash;
	int order;
} PluginEntry;

static uint32_t plugin_cap;
static int plugin_cmp_bucket(const void *a, const void *b)
{
	const PluginEntry *x = a, *y = b;
	uint32_t bx = x->hash & (plugin_cap - 1), by = y->hash & (plugin_cap - 1);
	return bx != by ? (bx < by ? -1 : 1) : (x->order > y->order) - (x->order < y->order);
}

/* filesByName.values of HashMap(protoFiles.size): the files by bucket, then insertion order (a
 * later file of the same name takes the place of the earlier one). The chains of a bucket are
 * resized like HashMap.putVal; tree bins (8 files in one bucket of a table of 64 or more) are not
 * reproduced. */
static Vec plugin_hashmap_values(PluginRun *r)
{
	int n = pb_count(r->req, CGR_PROTO_FILE);
	uint32_t cap = 1;
	while (cap < (uint32_t)n)
		cap <<= 1;
	uint32_t thr = (uint32_t)((float)cap * 0.75f);
	int *chains = xalloc(sizeof(int) * cap);
	Map seen = {0};
	Vec entries = {0};
	for (int i = 0; i < r->file_list.n; i++) {
		JFile *f = r->file_list.v[i];
		PluginEntry *e = map_get(&seen, f->name);
		if (e) {
			e->file = f;
			continue;
		}
		e = xalloc(sizeof *e);
		int32_t h = plugin_java_hash(f->name);
		e->hash = (uint32_t)h ^ ((uint32_t)h >> 16);
		e->file = f, e->order = entries.n;
		int chain = chains[e->hash & (cap - 1)]++;
		vec_push(&entries, e);
		map_put(&seen, f->name, e);
		/* treeifyBin of a small table resizes it, then size > threshold does */
		bool treeify = chain >= 8 && cap < 64;
		for (int k = 0; k < 2; k++) {
			if (k == 0 ? !treeify : (uint32_t)entries.n <= thr)
				continue;
			uint32_t nc = cap << 1;
			thr = cap >= 16 ? thr << 1 : (uint32_t)((float)nc * 0.75f);
			cap = nc;
			chains = xalloc(sizeof(int) * cap);
			for (int j = 0; j < entries.n; j++)
				chains[((PluginEntry *)entries.v[j])->hash & (cap - 1)]++;
		}
	}
	plugin_cap = cap;
	java_sort(entries.v, entries.n, plugin_cmp_bucket);
	Vec files = {0};
	for (int i = 0; i < entries.n; i++)
		vec_push(&files, ((PluginEntry *)entries.v[i])->file);
	return files;
}

/* The missing required fields of an empty DynamicMessage of m (findMissingFields). */
static const char *plugin_missing_required(const JMsg *m)
{
	Vec names = {0};
	for (int i = 0; i < m->fields.n; i++) {
		JField *f = VEC_AT(m->fields, JField, i);
		if (jfield_features(f)->v[JF_FIELD_PRESENCE] == 3)
			vec_push(&names, (void *)f->name);
	}
	return names.n ? str_join(&names, ", ") : NULL;
}

/* registry.add of one extension: the IllegalArgumentException of a group (and delimited) extension,
 * the UninitializedMessageException of DynamicMessage.build for a message type with required
 * fields. frames: the frames of the plugin from addAllExtensionsFrom*. */
static void plugin_add_extension(const JField *ext, const char *frames, int line_message, int line_other)
{
	int type = jfield_type_v4(ext);
	if (type == PT_MESSAGE) {
		const char *missing = plugin_missing_required(ext->msg_type);
		if (missing)
			plugin_throw(xcat("com.google.protobuf.UninitializedMessageException: Message missing required fields: ", missing),
				     xfmt("\tat com.google.protobuf.AbstractMessage$Builder.newUninitializedMessageException(AbstractMessage.java:467)\n"
					  "\tat com.google.protobuf.DynamicMessage$Builder.build(DynamicMessage.java:377)\n%s",
					  str_replace_all(frames, "%LINE%", xfmt("%d", line_message))));
	} else if (java_type_of(type) == JT_MESSAGE) {
		plugin_throw("java.lang.IllegalArgumentException: ExtensionRegistry.add() must be provided a default instance "
			     "when adding an embedded message extension.",
			     xcat("\tat com.google.protobuf.ExtensionRegistry.add(ExtensionRegistry.java:228)\n",
				  str_replace_all(frames, "%LINE%", xfmt("%d", line_other))));
	}
}

static void plugin_add_message_extensions(const JMsg *m, const char *outer)
{
	const char *frames = xcat("\tat " PLUGIN_CLASS ".addAllExtensionsFromMessage(Plugin.kt:%LINE%)\n", outer);
	for (int i = 0; i < m->exts.n; i++)
		plugin_add_extension(VEC_AT(m->exts, JField, i), frames, 166, 168);
	const char *nested = xcat("\tat " PLUGIN_CLASS ".addAllExtensionsFromMessage(Plugin.kt:172)\n", outer);
	for (int i = 0; i < m->nested.n; i++)
		plugin_add_message_extensions(VEC_AT(m->nested, JMsg, i), nested);
}

static void plugin_create_extension_registry(PluginRun *r)
{
	Vec files = plugin_hashmap_values(r);
	const char *outer = "\tat " PLUGIN_CLASS ".createExtensionRegistry(Plugin.kt:142)\n\tat " PLUGIN_CLASS
			    ".run(Plugin.kt:98)\n" FRAMES_RUN;
	for (int i = 0; i < files.n; i++) {
		JFile *f = files.v[i];
		const char *frames = xcat("\tat " PLUGIN_CLASS ".addAllExtensionsFromFile(Plugin.kt:%LINE%)\n", outer);
		for (int j = 0; j < f->exts.n; j++)
			plugin_add_extension(VEC_AT(f->exts, JField, j), frames, 150, 152);
		const char *from_file = xcat("\tat " PLUGIN_CLASS ".addAllExtensionsFromFile(Plugin.kt:156)\n", outer);
		for (int j = 0; j < f->msgs.n; j++)
			plugin_add_message_extensions(VEC_AT(f->msgs, JMsg, j), from_file);
	}
}

/* ---- Plugin.run ---- */

static void connect_generate(PluginRun *r);

static void plugin_run(void *arg)
{
	PluginRun *r = arg;
	plugin_trace = NULL;
	if (!plugin_initialized(r->req))
		plugin_throw(PLUGIN_EXCEPTION ": protoc sent unparseable request to plugin.", NULL);
	plugin_as_descriptors(r);
	plugin_create_extension_registry(r);
	/* the request parsed again with the registry, and built again: custom options do not change
	 * what the generator reads */
	connect_generate(r);
}

static const char *plugin_main(PbMsg *req, Buf *out)
{
	PluginRun r = {0};
	r.req = req;
	if (try_run(plugin_run, &r)) {
		Str msg = fail_message();
		if (str_has_prefix(msg, STR_LIT(UNSUPPORTED))) {
			/* what the port does not support: its own error, not a JVM exception */
			pb_put_bytes(out, CGRESP_ERROR, msg);
			return NULL;
		}
		Buf e = {0};
		buf_adds(&e, "Exception in thread \"main\" ");
		buf_add_str(&e, msg);
		while (e.n && e.p[e.n - 1] == '\n')
			e.n--;
		if (plugin_trace) {
			buf_addc(&e, '\n');
			buf_adds(&e, plugin_trace);
		}
		buf_addc(&e, '\n');
		pb_put_bytes(out, CGRESP_ERROR, buf_view(&e));
		return NULL;
	}
	/* the response preamble: FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS, EDITION_PROTO2,
	 * EDITION_2026; then one message per file */
	pb_put_tag(out, CGRESP_SUPPORTED_FEATURES, WT_VARINT);
	pb_put_varint(out, 3);
	pb_put_tag(out, CGRESP_MINIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, JED_PROTO2);
	pb_put_tag(out, CGRESP_MAXIMUM_EDITION, WT_VARINT);
	pb_put_varint(out, JED_2026);
	for (int i = 0; i < r.out.n; i++) {
		PluginFile *f = r.out.v[i];
		Buf file = {0};
		pb_put_bytes(&file, CGRESP_FILE_NAME, str_c(f->name));
		pb_put_bytes(&file, CGRESP_FILE_CONTENT, str_c(f->content));
		pb_put_bytes(out, CGRESP_FILE, buf_view(&file));
	}
	return NULL;
}
