/* Ports of protoc_plugin 25.1.0 lib/mixins.dart and lib/src/well_known_types.dart.
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static const char *const reservedNamesForMap[] = {
	"[]", "[]=", "addAll", "addEntries", "cast", "containsKey", "containsValue", "entries", "forEach", "isEmpty",
	"isNotEmpty", "keys", "length", "map", "putIfAbsent", "remove", "removeWhere", "retype", "update",
	"updateAll", "values", NULL,
};

static const PbMixin mapMixin = {"MapMixin", "dart:collection", NULL, reservedNamesForMap, NULL, -1};
static const PbMixin pbMapMixin = {"PbMapMixin", "package:protobuf/src/protobuf/mixins/map_mixin.dart", &mapMixin, NULL, NULL, -1};

/* findMixin: the exported mixins */
static const PbMixin *findMixin(const char *name) { return strcmp(name, "PbMapMixin") == 0 ? &pbMapMixin : NULL; }

/* findMixinsToApply: the ancestors first. */
static int mixin_findMixinsToApply(const PbMixin *m, const PbMixin **out)
{
	int n = 0;
	for (const PbMixin *p = m; p; p = p->parent) {
		if (n == 64)
			fail("protoc_plugin: mixin parent chain too long");
		out[n++] = p;
	}
	for (int i = 0; i < n / 2; i++) {
		const PbMixin *t = out[i];
		out[i] = out[n - 1 - i];
		out[n - 1 - i] = t;
	}
	return n;
}

/* findReservedNames: names of the mixins and their reserved names. */
static const char *const *mixin_findReservedNames(const PbMixin *m)
{
	Vec names = {0};
	for (const PbMixin *p = m; p; p = p->parent) {
		vec_push(&names, (void *)p->name);
		if (p->reservedNames)
			for (const char *const *r = p->reservedNames; *r; r++)
				vec_push(&names, (void *)*r);
	}
	vec_push(&names, NULL);
	return (const char *const *)names.v;
}

#define WK(name, type, helper) {name, "package:protobuf/src/protobuf/mixins/well_known.dart", NULL, NULL, type, helper}
static const struct {
	const char *fullName;
	PbMixin mixin;
} wellKnownMixins[] = {
	{"google.protobuf.Any", WK("AnyMixin", "any", 0)},
	{"google.protobuf.Timestamp", WK("TimestampMixin", "timestamp", 1)},
	{"google.protobuf.Duration", WK("DurationMixin", "duration", 2)},
	{"google.protobuf.Struct", WK("StructMixin", "struct", -1)},
	{"google.protobuf.Value", WK("ValueMixin", "value", -1)},
	{"google.protobuf.ListValue", WK("ListValueMixin", "listValue", -1)},
	{"google.protobuf.DoubleValue", WK("DoubleValueMixin", "doubleValue", -1)},
	{"google.protobuf.FloatValue", WK("FloatValueMixin", "floatValue", -1)},
	{"google.protobuf.Int64Value", WK("Int64ValueMixin", "int64Value", -1)},
	{"google.protobuf.UInt64Value", WK("UInt64ValueMixin", "uint64Value", -1)},
	{"google.protobuf.Int32Value", WK("Int32ValueMixin", "int32Value", -1)},
	{"google.protobuf.UInt32Value", WK("UInt32ValueMixin", "uint32Value", -1)},
	{"google.protobuf.BoolValue", WK("BoolValueMixin", "boolValue", -1)},
	{"google.protobuf.StringValue", WK("StringValueMixin", "stringValue", -1)},
	{"google.protobuf.BytesValue", WK("BytesValueMixin", "bytesValue", -1)},
	{"google.protobuf.FieldMask", WK("FieldMaskMixin", "fieldMask", -1)},
};
#undef WK

static const PbMixin *wellKnownMixinForFullName(const char *name)
{
	for (size_t i = 0; i < sizeof wellKnownMixins / sizeof *wellKnownMixins; i++)
		if (strcmp(wellKnownMixins[i].fullName, name) == 0)
			return &wellKnownMixins[i].mixin;
	return NULL;
}

/* The doc comment lines of a helper. */
static void helper_comments(Node *n, const char *const *lines)
{
	for (; *lines; lines++)
		ds_comment(n, *lines);
}

/* `final result = create();` */
static Node *final_result_create(void) { return ds_var_stmt(KW_FINAL, NULL, "result", ds_call(NULL, "create", NULL, ds_args((Node *)0))); }

/* injectHelpers: out.println(injectedHelpers.join('\n')) of the well-known types. */
static void mixin_injectHelpers(const PbMixin *m, Out *o)
{
	if (m->helper < 0)
		return;
	if (m->helper == 0) {
		static const char *const doc[] = {
			"/// Creates a new [Any] encoding [message].", "///",
			"/// The [typeUrl] will be [typeUrlPrefix]/`fullName` where `fullName` is",
			"/// the fully qualified name of the type of [message].", NULL,
		};
		Node *params = ds_params(NL(ds_param(pb_type("GeneratedMessage"), "message", NULL)),
					 NL(ds_param(core_type("String"), "typeUrlPrefix", ds_str("'type.googleapis.com'"))), true);
		ds_tnl(params->lb, 1, 0);
		Node *typeUrl = ds_named("typeUrlPrefix", ds_id("typeUrlPrefix"));
		ds_nl(typeUrl, 1, 4);
		Node *pack = ds_call(ds_path(mixinImportPrefix, "AnyMixin", (char *)0), "packIntoAny", NULL,
				     ds_args(ds_id("result"), ds_id("message"), typeUrl, (Node *)0));
		Node *method = ds_method(NONE, KW_STATIC, ds_type(NULL, "Any", NULL, false), KW_NONE, "pack", params,
					 ds_bbody(KW_NONE, false, ds_block(NL(final_result_create(), ds_expr_stmt(pack), ds_return(ds_id("result"))))));
		helper_comments(method, doc);
		out_add(o, method);
	} else if (m->helper == 1) {
		static const char *const doc[] = {
			"/// Creates a new instance from [dateTime].", "///", "/// Time zone information will not be preserved.", NULL,
		};
		Node *set = ds_call(ds_path(mixinImportPrefix, "TimestampMixin", (char *)0), "setFromDateTime", NULL,
				    ds_args(ds_id("result"), ds_id("dateTime"), (Node *)0));
		Node *method = ds_method(NONE, KW_STATIC, ds_type(NULL, "Timestamp", NULL, false), KW_NONE, "fromDateTime",
					 ds_params(NL(ds_param(core_type("DateTime"), "dateTime", NULL)), NONE, false),
					 ds_bbody(KW_NONE, false, ds_block(NL(final_result_create(), ds_expr_stmt(set), ds_return(ds_id("result"))))));
		helper_comments(method, doc);
		out_add(o, method);
	} else {
		static const char *const doc1[] = {
			"/// Converts the [Duration] to [$core.Duration].", "///",
			"/// This is a lossy conversion, as [$core.Duration] is limited to [int]",
			"/// microseconds and also does not support nanosecond precision.", NULL,
		};
		static const char *const doc2[] = {"/// Creates a new instance from [$core.Duration].", NULL};
		Node *seconds = ds_named("seconds", ds_call(ds_id("seconds"), "toInt", NULL, ds_args((Node *)0)));
		Node *micros = ds_named("microseconds", ds_bin(ds_id("nanos"), TT_TILDE_SLASH, "~/", ds_int(1000)));
		ds_nl(seconds, 1, 4);
		ds_nl(micros, 1, 4);
		Node *args = ds_args(seconds, micros, (Node *)0);
		args->flags |= DS_TRAILING_COMMA;
		ds_tnl(args->rp, 1, 2);
		Node *dur = ds_call(ds_id(coreImportPrefix), "Duration", NULL, args);
		ds_nl(dur, 1, 2);
		Node *toDart = ds_method(NONE, KW_NONE, core_type("Duration"), KW_NONE, "toDart", ds_params(NONE, NONE, false), ds_arrow(dur));
		helper_comments(toDart, doc1);
		out_add(o, toDart);
		out_blank(o);
		Node *s1 = ds_csec_assign("seconds", ds_call(ds_id(fixnumImportPrefix), "Int64", NULL,
							     ds_args(ds_path("duration", "inSeconds", (char *)0), (Node *)0)));
		Node *s2 = ds_csec_assign("nanos", ds_bin(ds_paren(ds_bin(ds_path("duration", "inMicroseconds", (char *)0), TT_PERCENT, "%",
									   ds_path(coreImportPrefix, "Duration", "microsecondsPerSecond", (char *)0))),
							   TT_STAR, "*", ds_int(1000)));
		ds_nl(s1, 1, 2);
		ds_nl(s2, 1, 2);
		Node *fromDart = ds_method(NONE, KW_STATIC, ds_type(NULL, "Duration", NULL, false), KW_NONE, "fromDart",
					   ds_params(NL(ds_param(core_type("Duration"), "duration", NULL)), NONE, false),
					   ds_arrow(ds_cascade(ds_call(NULL, "Duration", NULL, ds_args((Node *)0)), NL(s1, s2))));
		helper_comments(fromDart, doc2);
		out_add(o, fromDart);
		out_blank(o); /* the helper ends with a newline */
	}
}
