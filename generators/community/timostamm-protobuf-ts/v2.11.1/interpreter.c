/* Copyright (c) 2016 Timo Stamm
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

/* Ported from packages/plugin/src/interpreter.ts: runtime message types, service types and enum
 * infos built from the descriptors, custom options read from the unknown fields of the options
 * and converted to JSON. getOption of @bufbuild/protobuf for the plugin's own options
 * (protobuf-ts.proto: ts.exclude_options, ts.client, ts.server) is at the end. */

/* rpc.MethodInfo after normalizeMethodInfo */
typedef struct {
	const char *name, *local_name;
	const char *idempotency; /* NULL: undefined */
	bool server_streaming, client_streaming;
	DescMessage *I, *O;
	JsVal *options; /* {} if none */
	int index;
} MethodInfo;

typedef struct {
	const char *type_name;
	MethodInfo **methods;
	int n_methods;
	JsVal *options;
} ServiceType;

typedef struct {
	Registry *registry;
	const Options *options;
	Map message_types; /* typeName (or "$synthetic.<options type>") -> RtMessageType* */
	Map service_types;
	Map enum_infos;
	Map exclude_options; /* per file */
	int depth;           /* nesting of getMessageType (the JavaScript stack) */
	RtResolver resolver;
} Interpreter;

static RtMessageType *get_message_type(Interpreter *in, DescMessage *d);
static RtEnumInfo *get_enum_info(Interpreter *in, DescEnum *d);

static RtMessageType *resolve_message(void *ctx, void *ref) { return get_message_type(ctx, ref); }
static RtEnumInfo *resolve_enum(void *ctx, void *ref) { return get_enum_info(ctx, ref); }

static void interpreter_init(Interpreter *in, Registry *reg, const Options *o)
{
	in->registry = reg;
	in->options = o;
	in->resolver = (RtResolver){resolve_message, resolve_enum, in};
}

/* ---- getOption of @bufbuild/protobuf for repeated string / enum extensions ---- */

/* The values of the repeated string extension `num` in the unknown fields of options. */
static Vec get_option_strings(const PbMsg *options, uint32_t num)
{
	Vec out = {0};
	PbReader r = pb_unknown_fields(options);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != num)
			continue;
		if (f.wt != WT_LEN)
			fail("Error: cannot decode field %d with wire type %d", (int)num, f.wt);
		Str s = decode_utf8((Str){xstrndup((const char *)f.val, f.vlen), f.vlen});
		vec_push(&out, (void *)xstrndup(s.p, s.n));
	}
	return out;
}

/* The values of the repeated (open) enum extension `num`, packed or not. */
static Vec get_option_enums(const PbMsg *options, uint32_t num)
{
	Vec out = {0};
	PbReader r = pb_unknown_fields(options);
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != num)
			continue;
		if (f.wt == WT_VARINT) {
			vec_push(&out, (void *)(intptr_t)(int32_t)f.v);
		} else if (f.wt == WT_LEN) {
			PbReader p = {f.val, f.val + f.vlen};
			while (p.p < p.end) {
				uint64_t v;
				if (!pb_read_varint(&p, &v))
					fail("Error: premature EOF");
				vec_push(&out, (void *)(intptr_t)(int32_t)v);
			}
		} else {
			fail("Error: cannot decode field %d with wire type %d", (int)num, f.wt);
		}
	}
	return out;
}

/* getOption(descriptor.file, exclude_options), read once per file */
static const Vec *exclude_options(Interpreter *in, const DescFile *file)
{
	const char *key = xfmt("%llx", (unsigned long long)(uintptr_t)file);
	Vec *v = map_get(&in->exclude_options, key);
	if (!v) {
		v = xalloc(sizeof *v);
		*v = get_option_strings(desc_options(&file->h), 777701);
		map_put(&in->exclude_options, key, v);
	}
	return v;
}

/* The exclusion list of a descriptor: ts.exclude_options of the file, plus one more name for
 * services (excludeOptions.concat("ts.client")). */
typedef struct {
	const Vec *file;
	const char *extra;
} Exclude;

/* ---- names ---- */

/* FieldInfoGenerator.createTypescriptLocalName */
static const char *create_typescript_local_name(const char *name, const Options *o)
{
	return o->use_proto_field_name ? name : rt_lower_camel_case(name);
}

/* createTypescriptNameForField */
static const char *create_typescript_name_for_field(const Options *o, const char *proto_name)
{
	const char *name = create_typescript_local_name(proto_name, o);
	if (str_eq(name, "__proto__") || str_eq(name, "toString"))
		name = xcat(name, "$");
	Vec disc = str_split(o->oneof_kind_discriminator, ',');
	for (int i = 0; i < disc.n; i++)
		if (str_eq(disc.v[i], name)) {
			name = xcat(name, "$");
			break;
		}
	return name;
}

/* createTypescriptNameForMethod */
static const char *create_typescript_name_for_method(const char *proto_name)
{
	static const char *const reserved[] = {
		"__proto__", "toString", "name", "constructor", "methods", "typeName", "options", "_transport",
		"close", "getChannel", "waitForReady", "makeUnaryRequest", "makeClientStreamRequest",
		"makeServerStreamRequest", "makeBidiStreamRequest",
	};
	const char *name = rt_lower_camel_case(proto_name);
	for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++)
		if (str_eq(reserved[i], name))
			return xcat(name, "$");
	return name;
}

/* ---- field infos ---- */

static bool is_long_value_type(int t)
{
	return t == PT_INT64 || t == PT_UINT64 || t == PT_FIXED64 || t == PT_SFIXED64 || t == PT_SINT64;
}

/* getL */
static int get_l(const Interpreter *in, const DescField *f)
{
	int scalar = f->field_kind == FK_SCALAR || (f->field_kind == FK_LIST && f->list_kind == FK_SCALAR) ||
				     (f->field_kind == FK_MAP && f->map_kind == FK_SCALAR)
			     ? f->scalar
			     : 0;
	if (!scalar || !is_long_value_type(scalar))
		return RT_LONG_UNSET;
	const PbMsg *o = pb_msg(f->h.proto, FD_OPTIONS);
	if (o && pb_has(o, FO_JSTYPE)) {
		switch (pb_int(o, FO_JSTYPE, 0)) {
		case 1: return RT_LONG_UNSET; /* JS_STRING */
		case 0: return RT_LONG_BIGINT;
		case 2: return RT_LONG_NUMBER;
		}
	}
	if (in->options->normal_long_type == RT_LONG_STRING)
		return RT_LONG_UNSET;
	return in->options->normal_long_type;
}

/* buildFieldInfo */
static RtPartialFieldInfo *build_field_info(Interpreter *in, DescField *f)
{
	RtPartialFieldInfo *p = xalloc(sizeof *p);
	RtFieldInfo *info = &p->f;
	info->desc = f;
	info->no = f->number;
	info->name = f->name;
	info->L = RT_LONG_UNSET;
	info->V_L = RT_LONG_UNSET;
	const char *local = create_typescript_name_for_field(in->options, f->name);
	const char *lcc = rt_lower_camel_case(f->name);
	if (!str_eq(local, lcc)) {
		info->local_name = local;
		p->has_local_name = true;
	}
	Str json = pb_bytes(f->h.proto, FD_JSON_NAME);
	if (!str_eqc(json, lcc)) {
		info->json_name = xstrndup(json.p, json.n);
		p->has_json_name = true;
	}
	if (f->oneof)
		info->oneof = create_typescript_name_for_field(in->options, f->oneof->name);
	if (f->field_kind == FK_LIST) {
		info->repeat = f->packed ? RT_REPEAT_PACKED : RT_REPEAT_UNPACKED;
		p->has_repeat = true;
	}
	if ((f->field_kind == FK_SCALAR || f->field_kind == FK_ENUM) && !f->oneof) {
		bool proto3_optional = pb_int(f->h.proto, FD_PROTO3_OPTIONAL, 0) != 0;
		const Desc *parent = f->is_extension ? (f->parent ? &f->parent->h : NULL) : &f->parent->h;
		bool proto2_optional = parent && desc_file_of(parent)->edition == ED_PROTO2 &&
				       pb_int(f->h.proto, FD_LABEL, LABEL_OPTIONAL) == LABEL_OPTIONAL;
		if (proto2_optional || proto3_optional) {
			info->opt = true;
			p->has_opt = true;
		}
	}
	if (f->field_kind == FK_SCALAR || (f->field_kind == FK_LIST && f->list_kind == FK_SCALAR)) {
		info->kind = RT_SCALAR;
		info->T = f->scalar;
		info->L = get_l(in, f);
	} else if (f->field_kind == FK_ENUM || (f->field_kind == FK_LIST && f->list_kind == FK_ENUM)) {
		info->kind = RT_ENUM;
		info->ref = f->enm;
	} else if (f->field_kind == FK_MESSAGE || (f->field_kind == FK_LIST && f->list_kind == FK_MESSAGE)) {
		info->kind = RT_MESSAGE;
		info->ref = f->message;
	} else if (f->field_kind == FK_MAP) {
		info->kind = RT_MAP;
		info->K = f->map_key;
		switch (f->map_kind) {
		case FK_SCALAR:
			info->V_kind = RT_SCALAR;
			info->V_T = f->scalar;
			info->V_L = get_l(in, f);
			break;
		case FK_MESSAGE:
			info->V_kind = RT_MESSAGE;
			info->ref = f->message;
			break;
		case FK_ENUM:
			info->V_kind = RT_ENUM;
			info->ref = f->enm;
			break;
		}
	}
	if (f->is_extension) {
		info->opt = info->repeat == RT_REPEAT_NO;
		p->has_opt = true;
		info->name = f->type_name;
		info->local_name = f->type_name;
		info->json_name = f->type_name;
		p->has_local_name = p->has_json_name = true;
		info->oneof = NULL;
	}
	return p;
}

/* buildFieldInfos: skips groups */
static RtPartialFieldInfo **build_field_infos(Interpreter *in, DescField **fields, int n, int *out_n)
{
	RtPartialFieldInfo **r = xalloc(sizeof *r * (size_t)(n + 1));
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (pb_int(fields[i]->h.proto, FD_TYPE, 0) == PT_GROUP)
			continue;
		r[k++] = build_field_info(in, fields[i]);
	}
	*out_n = k;
	return r;
}

/* ---- options ---- */

static bool wildcard_match(const char *key, const char *pattern)
{
	/* key.match(re) where re is the pattern with '*' as '.*', unanchored */
	Vec pieces = str_split(pattern, '*');
	const char *p = key;
	for (int i = 0; i < pieces.n; i++) {
		const char *piece = pieces.v[i];
		if (!*piece)
			continue;
		const char *hit = strstr(p, piece);
		if (!hit)
			return false;
		p = hit + strlen(piece);
	}
	return true;
}

static const char *options_type_name(const Desc *d)
{
	switch (d->kind) {
	case DK_FIELD: return "google.protobuf.FieldOptions";
	case DK_METHOD: return "google.protobuf.MethodOptions";
	case DK_FILE: return "google.protobuf.FileOptions";
	case DK_SERVICE: return "google.protobuf.ServiceOptions";
	default: return "google.protobuf.MessageOptions";
	}
}

/* The extensions of the registry in its iteration order (the insertion order of the types). */
static void collect_extensions(Interpreter *in, const char *extendee, const Desc *d, Vec *out)
{
	const Vec *exts = d->kind == DK_FILE ? &((const DescFile *)d)->extensions
					     : &((const DescMessage *)d)->nested_extensions;
	for (int i = 0; i < exts->n; i++) {
		DescField *e = exts->v[i];
		if (map_get(&in->registry->types, e->type_name) == e && str_eq(e->extendee->type_name, extendee))
			vec_push(out, e);
	}
	const Vec *msgs = d->kind == DK_FILE ? &((const DescFile *)d)->messages : &((const DescMessage *)d)->nested_messages;
	for (int i = 0; i < msgs->n; i++)
		collect_extensions(in, extendee, msgs->v[i], out);
}

/* readOptions */
static JsVal *read_options(Interpreter *in, const Desc *d, const Exclude *exclude)
{
	if (in->options->force_exclude_all_options)
		return NULL;
	const PbMsg *opts = desc_options(d);
	if (!opts || !opts->unknown.n)
		return NULL;
	const char *opt_type = options_type_name(d);
	const char *type_name = xcat("$synthetic.", opt_type);
	RtMessageType *type = map_get(&in->message_types, type_name);
	if (!type) {
		Vec exts = {0};
		for (int i = 0; i < in->registry->files.n; i++)
			collect_extensions(in, opt_type, in->registry->files.v[i], &exts);
		int n;
		RtPartialFieldInfo **fi = build_field_infos(in, (DescField **)exts.v, exts.n, &n);
		type = rt_message_type(type_name, fi, n, js_object(), &in->resolver);
		map_put(&in->message_types, type_name, type);
	}
	JsVal *json = rt_to_json(type, rt_from_binary(type, buf_view((Buf *)&opts->unknown)));
	Str *keys;
	int nk = js_obj_keys(json, &keys);
	for (int k = 0; k < nk; k++) {
		const char *key = xstrndup(keys[k].p, keys[k].n);
		int n_ex = exclude->file->n + (exclude->extra ? 1 : 0);
		for (int i = 0; i < n_ex; i++) {
			const char *ex = i < exclude->file->n ? exclude->file->v[i] : exclude->extra;
			if (!strchr(ex, '*') ? str_eq(key, ex) : wildcard_match(key, ex))
				js_obj_delete(json, keys[k]);
		}
	}
	if (!js_obj_keys(json, &keys))
		return NULL;
	return json;
}

/* ---- types ---- */

/* getMessageType */
static RtMessageType *get_message_type(Interpreter *in, DescMessage *d)
{
	RtMessageType *type = map_get(&in->message_types, d->type_name);
	if (type)
		return type;
	/* upstream recurses on the JavaScript stack (options of a type that use a custom option type) */
	if (++in->depth > 200)
		fail("unsupported by protoc-gen-uni: custom option types whose options use custom option "
		     "types nested deeper than 200 levels");
	JsVal *placeholder = js_object();
	int n;
	RtPartialFieldInfo **fi = build_field_infos(in, (DescField **)d->fields.v, d->fields.n, &n);
	type = rt_message_type(d->type_name, fi, n, placeholder, &in->resolver);
	map_put(&in->message_types, d->type_name, type);
	Exclude exclude = {exclude_options(in, d->file), NULL};
	JsVal *mo = read_options(in, &d->h, &exclude);
	if (mo) {
		Str *keys;
		int nk = js_obj_keys(mo, &keys);
		for (int i = 0; i < nk; i++)
			js_obj_set(placeholder, keys[i], js_obj_get(mo, keys[i]));
	}
	/* sic: field infos (without groups) and descriptor fields (with groups) are paired by index */
	for (int i = 0; i < type->n_fields; i++)
		type->fields[i]->options = read_options(in, d->fields.v[i], &exclude);
	in->depth--;
	return type;
}

/* findEnumSharedPrefix */
static const char *find_enum_shared_prefix(const DescEnum *e)
{
	Buf b = {0};
	for (const char *p = e->name; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') {
			buf_addc(&b, '_');
			buf_addc(&b, (char)(*p + 32));
		} else {
			buf_addc(&b, *p);
		}
	}
	const char *s = buf_str(&b);
	if (*s == '_')
		s++;
	char *prefix = xcat(s, "_");
	for (char *p = prefix; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 32);
	size_t pl = strlen(prefix);
	for (int i = 0; i < e->values.n; i++) {
		const char *name = ((DescEnumValue *)e->values.v[i])->name;
		if (!has_prefix(name, prefix))
			return NULL;
	}
	for (int i = 0; i < e->values.n; i++) {
		const char *stripped = ((DescEnumValue *)e->values.v[i])->name + pl;
		/* name.length > 0 && /^[A-Z].+/.test(name) */
		if (!(stripped[0] >= 'A' && stripped[0] <= 'Z') || !stripped[1] || stripped[1] == '\n' || stripped[1] == '\r')
			return NULL;
	}
	return prefix;
}

/* buildEnumInfo */
static RtEnumInfo *build_enum_info(Interpreter *in, DescEnum *e)
{
	const char *prefix = in->options->keep_enum_prefix ? NULL : find_enum_shared_prefix(e);
	bool has_zero = false;
	for (int i = 0; i < e->values.n; i++)
		if (((DescEnumValue *)e->values.v[i])->number == 0)
			has_zero = true;
	int n = 0;
	const char **names = xalloc(sizeof *names * (size_t)(e->values.n + 2));
	int32_t *numbers = xalloc(sizeof *numbers * (size_t)(e->values.n + 2));
	if (!has_zero && in->options->synthesize_enum_zero_value) {
		names[n] = in->options->synthesize_enum_zero_value;
		numbers[n++] = 0;
	}
	for (int i = 0; i < e->values.n; i++) {
		DescEnumValue *v = e->values.v[i];
		names[n] = prefix ? v->name + strlen(prefix) : v->name;
		numbers[n++] = v->number;
	}
	RtEnumInfo *info = xalloc(sizeof *info);
	info->type_name = e->type_name;
	info->obj = rt_enum_object(names, numbers, n);
	info->shared_prefix = prefix;
	return info;
}

/* getEnumInfo */
static RtEnumInfo *get_enum_info(Interpreter *in, DescEnum *e)
{
	RtEnumInfo *info = map_get(&in->enum_infos, e->type_name);
	if (!info) {
		info = build_enum_info(in, e);
		map_put(&in->enum_infos, e->type_name, info);
	}
	return info;
}

/* buildMethodInfo */
static MethodInfo *build_method_info(Interpreter *in, DescMethod *m, const Exclude *exclude, int index)
{
	MethodInfo *mi = xalloc(sizeof *mi);
	mi->index = index;
	mi->name = m->name;
	mi->local_name = create_typescript_name_for_method(m->name);
	switch (pb_int(desc_options(&m->h), 34, 0)) {
	case 1: mi->idempotency = "NO_SIDE_EFFECTS"; break;
	case 2: mi->idempotency = "IDEMPOTENT"; break;
	}
	mi->server_streaming = pb_int(m->h.proto, MDP_SERVER_STREAMING, 0) != 0;
	mi->client_streaming = pb_int(m->h.proto, MDP_CLIENT_STREAMING, 0) != 0;
	get_message_type(in, m->input);
	mi->I = m->input;
	get_message_type(in, m->output);
	mi->O = m->output;
	mi->options = read_options(in, &m->h, exclude);
	if (!mi->options)
		mi->options = js_object();
	return mi;
}

/* getServiceType */
static ServiceType *get_service_type(Interpreter *in, DescService *s)
{
	ServiceType *t = map_get(&in->service_types, s->type_name);
	if (t)
		return t;
	Exclude exclude = {exclude_options(in, s->file), "ts.client"};
	t = xalloc(sizeof *t);
	t->type_name = s->type_name;
	t->n_methods = s->methods.n;
	t->methods = xalloc(sizeof *t->methods * (size_t)(s->methods.n + 1));
	for (int i = 0; i < s->methods.n; i++)
		t->methods[i] = build_method_info(in, s->methods.v[i], &exclude, i);
	t->options = read_options(in, &s->h, &exclude);
	if (!t->options)
		t->options = js_object();
	map_put(&in->service_types, s->type_name, t);
	return t;
}
