// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/tracker.{h,cc} (protobuf v36.2): the
 * $annotate_*$ variables of the field listener events. */

typedef struct {
	const char *var;
	const char *call;
	int field_index; /* -1: none */
	const char *thiz; /* NULL: none */
	const char *args[2];
	int n_args;
	bool suppressed;
} TrackerCall;

static void generate_tracker_calls(Subs *subs, const Options *o, const LpMessage *message, const char *alt_annotation,
				   const TrackerCall *calls, int n)
{
	bool enable_tracking = has_tracker(message, o);
	for (int i = 0; i < n; i++) {
		const TrackerCall *call = &calls[i];
		Buf call_str = {0};
		if (enable_tracking && !call->suppressed && !forbidden_event(o, call->var)) {
			buf_adds(&call_str, "Impl_::_tracker_.");
			buf_adds(&call_str, call->call);
			if (call->field_index >= 0)
				buf_adds(&call_str, xcat("<", cpp_itoa(call->field_index), ">"));
			buf_addc(&call_str, '(');
			const char *arg_sep = "";
			if (call->thiz) {
				buf_adds(&call_str, call->thiz);
				arg_sep = ", ";
			}
			for (int j = 0; j < call->n_args; j++) {
				buf_adds(&call_str, arg_sep);
				buf_adds(&call_str, call->args[j]);
				arg_sep = ", ";
			}
			buf_adds(&call_str, ");");
		} else if (o->annotate_accessor && alt_annotation) {
			buf_adds(&call_str, alt_annotation);
		}
		const char *s = buf_str(&call_str);
		if (*s) {
			s = xcat(s, "\n");
			if (enable_tracking)
				s = xcat("if (::", protobuf_namespace(o), "::internal::cpp::IsTrackingEnabled()) ", s);
		}
		subs_push(subs, SSUF(xcat("annotate_", call->var), xstrdup(s), ";"));
	}
}

static Subs make_tracker_calls_message(const LpMessage *message, const Options *o)
{
	const char *extns = is_map_entry_message(message) ? "_extensions_" : "_impl_._extensions_";
	const char *tt = "_proto_TypeTraits";
	char *prim = xcat(tt, "::GetPtr(id.number(), ", extns, ", id.default_value_ref())");
	char *index = xcat(tt, "::GetPtr(id.number(), ", extns, ", index)");
	char *add = xcat(tt, "::GetPtr(id.number(), ", extns, ", ", extns, ".ExtensionSize(id.number()) - 1)");
	char *list = xcat(tt, "::GetRepeatedPtr(id.number(), ", extns, ")");
#define CALL(v, c, t) {v, c, -1, t, {0}, 0, false}
#define EXT(v, c, a) {v, c, -1, "this", {"id.number()", a}, 2, false}
	TrackerCall calls[] = {
		CALL("serialize", "OnSerialize", "&this_"),
		CALL("deserialize", "OnDeserialize", "_this"),
		CALL("reflection", "OnGetMetadata", NULL),
		CALL("bytesize", "OnByteSize", "&this_"),
		{"mergefrom", "OnMergeFrom", -1, "_this", {"&from"}, 1, false},
		CALL("unknown_fields", "OnUnknownFields", "this"),
		CALL("mutable_unknown_fields", "OnMutableUnknownFields", "this"),
		EXT("extension_has", "OnHasExtension", prim),
		EXT("extension_get", "OnGetExtension", prim),
		EXT("extension_mutable", "OnMutableExtension", prim),
		EXT("extension_set", "OnSetExtension", prim),
		EXT("extension_release", "OnReleaseExtension", prim),
		EXT("repeated_extension_get", "OnGetExtension", index),
		EXT("repeated_extension_mutable", "OnMutableExtension", index),
		EXT("repeated_extension_set", "OnSetExtension", index),
		EXT("repeated_extension_add", "OnAddExtension", add),
		EXT("repeated_extension_add_mutable", "OnAddMutableExtension", add),
		EXT("extension_repeated_size", "OnExtensionSize", list),
		EXT("repeated_extension_list", "OnListExtension", list),
		EXT("repeated_extension_list_mutable", "OnMutableListExtension", list),
		{"extension_clear", "OnClearExtension", -1, "this", {0}, 0, true},
	};
#undef CALL
#undef EXT
	Subs subs = {0};
	generate_tracker_calls(&subs, o, message, NULL, calls, (int)(sizeof calls / sizeof *calls));
	return subs;
}

static Subs make_tracker_calls_field(const LpField *f, const Options *o)
{
	const char *base = "nullptr", *for_last = "nullptr", *for_flat = "nullptr";
	if (is_repeated(f)) {
		if (!is_map(f) && cpp_type(f) != LP_CPPTYPE_MESSAGE) {
			char *accessor = xcat("_internal_", field_name(f), "()");
			base = xcat("&", accessor, ".Get(index)");
			for_last = xcat("&", accessor, ".Get(", accessor, ".size() - 1)");
			for_flat = xcat("&", accessor);
		}
	} else if (cpp_type(f) == LP_CPPTYPE_STRING) {
		const LpOneof *oneof = real_oneof(f);
		char *member = field_member_name(f, should_split(f, o));
		if (oneof) {
			char *field_ptr = member;
			if (is_arena_string_ptr(f, o))
				field_ptr = xcat(member, ".UnsafeGetPointer()");
			else if (is_micro_string(f, o))
				field_ptr = xcat("&", member);
			char *has = xcat(oneof->name, "_case() == k", underscores_to_camel_case(f->name, true));
			char *default_field = make_default_field_name(f);
			if (is_arena_string_ptr(f, o))
				default_field = xcat(default_field, ".get()");
			if (!f->default_value_string.n || is_micro_string(f, o))
				base = xcat(has, " ? ", field_ptr, " : nullptr");
			else
				base = xcat(has, " ? ", field_ptr, " : &", default_field);
		} else if (is_arena_string_ptr(f, o) && f->default_value_string.n) {
			base = xcat(member, ".IsDefault() ? &", make_default_field_name(f), ".get() : ", member,
				    ".UnsafeGetPointer()");
		} else {
			base = xcat("&", member);
		}
		for_flat = base;
	} else if (cpp_type(f) != LP_CPPTYPE_MESSAGE || is_explicit_lazy(f)) {
		char *member = field_member_name(f, should_split(f, o));
		base = xcat("&", member);
		if (cpp_type(f) != LP_CPPTYPE_MESSAGE)
			for_flat = xcat("&", member);
	}
	int index = f->index;
#define FCALL(v, c, a) {v, c, index, "this", {a}, 1, false}
	TrackerCall calls[] = {
		FCALL("get", "OnGet", base),
		FCALL("set", "OnSet", base),
		FCALL("has", "OnHas", base),
		FCALL("mutable", "OnMutable", base),
		FCALL("release", "OnRelease", base),
		FCALL("clear", "OnClear", for_flat),
		FCALL("size", "OnSize", for_flat),
		FCALL("list", "OnList", for_flat),
		FCALL("mutable_list", "OnMutableList", for_flat),
		FCALL("add", "OnAdd", for_last),
		FCALL("add_mutable", "OnAddMutable", for_last),
	};
#undef FCALL
	Subs subs = {0};
	generate_tracker_calls(&subs, o, f->containing_type, xcat(field_name(f), "_AccessedNoStrip = true;"), calls,
			       (int)(sizeof calls / sizeof *calls));
	return subs;
}
