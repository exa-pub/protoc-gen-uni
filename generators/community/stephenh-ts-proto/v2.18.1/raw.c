/* The unformatted output of ts-proto v2.18.1: when a declaration is named with a reserved word
 * (`message class {}`), dprint cannot parse the generated code and ts-poet's maybePretty returns
 * the text of the templates as they are (Code.toString). This is that text for the files of
 * proto3 sources whose fields are singular or oneof members of 32-bit integer, bool, string, enum
 * or message types (outside google.protobuf), with enums, comments and the unary services of
 * outputServices default (outputClientImpl true or grpc-web), grpc-js, nice-grpc and
 * generic-definitions; any other construct in such a file is an "unsupported" error.
 *
 * Code text follows ts-poet 6.12.0: a code is its template with the placeholders' text, joinCode
 * trims each chunk (String.prototype.trim) and joins them, an array placeholder concatenates its
 * codes, a used conditional output prints its declaration where generateFile lists the utils.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

#define RAW_UNSUPPORTED(what) fail("unsupported by protoc-gen-uni: %s: %s in the unformatted output of a file with a " \
				   "reserved word as a name", C.file->name, what)

/* joinCode(chunks, { on }) */
static char *rjoin(const Vec *chunks, const char *on)
{
	Buf b = {0};
	for (int i = 0; i < chunks->n; i++) {
		if (i)
			buf_adds(&b, on);
		buf_add_str(&b, js_trim(str_c(chunks->v[i])));
	}
	return xstrdup(buf_str(&b));
}

/* `${array}`: the codes one after the other */
static char *rcat(const Vec *chunks)
{
	Buf b = {0};
	for (int i = 0; i < chunks->n; i++)
		buf_adds(&b, chunks->v[i]);
	return xstrdup(buf_str(&b));
}

#define PUSH(v, ...) vec_push(v, xfmt(__VA_ARGS__))

static const char *rgt(void) { return C.u->global_this->name; }

/* maybeAddComment: the chunk `\n\n${comment}\n\n` (trimmed by the join) */
static void rcomment(Vec *chunks, const TpdLocation *loc, bool deprecated, const char *prefix)
{
	NodeComment *cm = jsdoc_comment(loc, deprecated, prefix);
	if (cm)
		vec_push(chunks, xstrndup(cm->text.p, cm->text.n));
}

/* getPropertyAccessor(objectName, propertyName, optional) */
static const char *raccess(const char *obj, const char *name, bool optional)
{
	if (is_valid_identifier(name))
		return xcat(obj, optional ? "?" : "", ".", name);
	return xcat(obj, optional ? "?." : "", "[", quote_json(str_c(name)), "]");
}

static const char *rkey(const char *name) { return is_valid_identifier(name) ? name : quote_json(str_c(name)); }

/* the symbol of a message or enum (impProto, never aliased here) */
static const char *rsym(const char *type_name) { return type_info(type_name)->ts_name; }
static const char *renum_method(const char *type_name, const char *suffix) { return xcat(uncapitalize(rsym(type_name)), suffix); }

static long long rtag(const TpdField *f, int wire) { return (long long)(((uint32_t)f->number << 3) | (uint32_t)wire); }

static bool rbool_or_int(const TpdField *f) { return f->type == T_BOOL || is_whole_number(f); }

/* basicTypeName(field) */
static const char *rtype(const TpdField *f)
{
	if (f->type == T_STRING)
		return "string";
	if (f->type == T_BOOL)
		return "boolean";
	if (is_whole_number(f))
		return "number";
	return rsym(f->type_name);
}

/* defaultValue (proto3) */
static const char *rdefault(const TpdField *f)
{
	switch (f->type) {
	case T_STRING:
		return "\"\"";
	case T_BOOL:
		return "false";
	case T_ENUM: {
		TpdEnum *e = type_info(f->type_name)->en;
		if (!e)
			fail("TypeError: Cannot read properties of undefined (reading 'find')");
		TpdEnumValue *v = enum_default(e, f, false);
		return O.string_enums ? xcat(rsym(f->type_name), ".", get_member_name(e, v)) : xfmt("%d", (int)v->number);
	}
	case T_MESSAGE:
		return "undefined";
	default:
		return "0";
	}
}

/* notDefaultCheck */
static const char *rnot_default(const TpdField *f, TpdMessage *m, const char *place)
{
	bool optional = is_optional_property(f, m->options, true);
	const char *pre = optional ? xcat(place, " !== undefined  &&") : "";
	return xfmt("%s %s !== %s", pre, place, rdefault(f));
}

COLD static void raw_check_message(TpdMessage *m)
{
	if (m->nextension || m->nextension_range)
		RAW_UNSUPPORTED("extensions");
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		bool ok = !is_repeated(f) && !f->proto3_optional && !is_js_type_field_option(f) &&
			  (!f->has_oneof_index || (f->oneof_index >= 0 && f->oneof_index < m->noneof_decl));
		if (f->type == T_MESSAGE)
			ok &= strncmp(f->type_name, ".google.protobuf.", 17) != 0 && !(O.use_mongo_object_id && is_object_id(f));
		else
			ok &= f->type == T_STRING || f->type == T_ENUM || rbool_or_int(f);
		if (!ok)
			RAW_UNSUPPORTED("fields other than singular 32-bit integers, bools, strings, enums and messages");
	}
}

/* ---- generateInterfaceDeclaration ---- */

static void raw_oneof_property(Vec *c, TpdMessage *m, int oi, SourceInfo *si)
{
	rcomment(c, si_lookup(si, MESSAGE_ONEOF_DECL, oi), false, NULL);
	Buf u = {0};
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		if (!is_within_one_of(f) || f->oneof_index != oi)
			continue;
		if (jsdoc_comment(si_lookup(si, MESSAGE_FIELD, k), false, NULL))
			RAW_UNSUPPORTED("comments on the members of oneof unions");
		const char *name = maybe_snake_to_camel(f->name);
		buf_fmt(&u, "|\n  { $case: '%s', %s: %s }", name, oneof_value_name(name), rtype(f));
	}
	vec_push(c, xfmt("%s%s:", maybe_snake_to_camel(m->oneof_decl[oi]->name), O.uo_none ? "" : "?"));
	vec_push(c, xstrdup(buf_str(&u)));
	vec_push(c, "| undefined,");
}

static void raw_interface(void *arg, const char *full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name)
{
	Vec *chunks = arg;
	raw_check_message(m);
	Vec c = {0};
	rcomment(&c, si->self, m->options && m->options->deprecated, NULL);
	PUSH(&c, "export interface %s {", full_name);
	if (O.add_type_to_messages)
		PUSH(&c, "$type: '%s',", maybe_prefix_package(C.file, proto_full_name));
	bool *done = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		if (is_within_one_of_that_should_be_union(f)) {
			if (!done[f->oneof_index])
				raw_oneof_property(&c, m, f->oneof_index, si);
			done[f->oneof_index] = true;
			continue;
		}
		rcomment(&c, si_lookup(si, MESSAGE_FIELD, k), f->options && f->options->deprecated, NULL);
		bool optional = is_optional_property(f, m->options, true);
		/* toTypeName: finalize(type, (message && useOptionals none) || oneof properties || ensureOptional) */
		bool fin = (f->type == T_MESSAGE && !is_within_one_of(f) && O.uo_none) || (is_within_one_of(f) && O.oneof == ONEOF_PROPERTIES) || optional;
		PUSH(&c, "%s%s: %s%s, ", rkey(get_field_name(f)), optional ? "?" : "", rtype(f), fin ? " | undefined" : "");
	}
	if (O.unknown_fields)
		vec_push(&c, "_unknownFields?: {[key: number]: Uint8Array[]} | undefined,");
	vec_push(&c, "}");
	vec_push(chunks, rjoin(&c, "\n"));
}

/* ---- enums.ts ---- */

static void raw_enum(void *arg, const char *full_name, TpdEnum *e, SourceInfo *si, const char *proto_full_name)
{
	(void)proto_full_name;
	Vec *chunks = arg;
	Vec c = {0};
	bool lit = opt_true(&O.opts, "enumsAsLiterals");
	bool unrec_on = opt_true(&O.opts, "unrecognizedEnum");
	const char *uname = opt_str("unrecognizedEnumName");
	const char *uval = js_value_to_string(opt(&O.opts, "unrecognizedEnumValue"));
	JsVal *unrec = opt(&O.opts, "unrecognizedEnumValue");
	UnrecognizedEnum u = {0};
	rcomment(&c, si->self, e->options && e->options->deprecated, NULL);
	if (lit)
		PUSH(&c, "export const %s = {", full_name);
	else
		PUSH(&c, "export %senum %s {", opt_true(&O.opts, "constEnums") ? "const " : "", full_name);
	const char *delim = lit ? ":" : "=";
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		const char *member = get_member_name(e, v);
		if (unrec->t == JS_NUMBER && v->number == unrec->num)
			u = (UnrecognizedEnum){true, member, v->name};
		rcomment(&c, si_lookup(si, ENUM_VALUE, i), v->options && v->options->deprecated, xcat(member, " - "));
		PUSH(&c, "%s %s %s,", member, delim, O.string_enums ? xcat("\"", v->name, "\"") : xfmt("%d", (int)v->number));
	}
	if (unrec_on && !u.present)
		PUSH(&c, "\n      %s %s %s,", uname, delim, O.string_enums ? xcat("\"", uname, "\"") : uval);
	if (lit) {
		vec_push(&c, "} as const");
		vec_push(&c, "\n");
		PUSH(&c, "export type %s = typeof %s[keyof typeof %s]", full_name, full_name, full_name);
		vec_push(&c, "\n");
		PUSH(&c, "export namespace %s {", full_name);
		for (int i = 0; i < e->nvalue; i++) {
			const char *member = get_member_name(e, e->value[i]);
			PUSH(&c, "export type %s = typeof %s.%s;", member, full_name, member);
		}
		if (unrec_on && !u.present)
			PUSH(&c, "export type %s = typeof %s.%s;", uname, full_name, uname);
	}
	vec_push(&c, "}");
	const char *unrecognized_throw = xfmt("\n      default:\n        throw new %s.Error(\"Unrecognized enum value \" + object + \" for enum %s\");\n    ", rgt(),
					      full_name);
	bool encode = opt_true(&O.opts, "outputEncodeMethods");
	if (O.output_json_true || opt_is(&O.opts, "outputJsonMethods", "from-only") || (O.string_enums && encode)) {
		Vec f = {0};
		PUSH(&f, "export function %sFromJSON(object: any): %s {", uncapitalize(full_name), full_name);
		vec_push(&f, "switch (object) {");
		for (int i = 0; i < e->nvalue; i++) {
			TpdEnumValue *v = e->value[i];
			PUSH(&f, "\n      case %d:\n      case \"%s\":\n        return %s.%s;\n    ", (int)v->number, v->name, full_name, get_member_name(e, v));
		}
		if (!unrec_on)
			vec_push(&f, (char *)unrecognized_throw);
		else if (!u.present)
			PUSH(&f, "\n        case %s:\n        case \"%s\":\n        default:\n          return %s.%s;\n      ", uval, uname, full_name, uname);
		else
			PUSH(&f, "\n        default:\n          return %s.%s;\n      ", full_name, u.name);
		vec_push(&f, "}");
		vec_push(&f, "}");
		vec_push(&c, "\n");
		vec_push(&c, rjoin(&f, "\n"));
	}
	if (O.output_json_true || opt_is(&O.opts, "outputJsonMethods", "to-only")) {
		Vec f = {0};
		bool num = O.use_numeric_enum_for_json;
		PUSH(&f, "export function %sToJSON(object: %s): %s {", uncapitalize(full_name), full_name, num ? "number" : "string");
		vec_push(&f, "switch (object) {");
		for (int i = 0; i < e->nvalue; i++) {
			TpdEnumValue *v = e->value[i];
			if (num)
				PUSH(&f, "case %s.%s: return %d;", full_name, get_member_name(e, v), (int)v->number);
			else
				PUSH(&f, "case %s.%s: return \"%s\";", full_name, get_member_name(e, v), v->name);
		}
		if (!unrec_on) {
			vec_push(&f, (char *)unrecognized_throw);
		} else if (!u.present) {
			PUSH(&f, "\n        case %s.%s:", full_name, uname);
			if (num)
				PUSH(&f, "\n        default:\n          return %s;\n      ", uval);
			else
				PUSH(&f, "\n        default:\n          return \"%s\";\n      ", uname);
		} else if (num) {
			PUSH(&f, "\n        default:\n          return %s;\n      ", uval);
		} else {
			PUSH(&f, "\n      default:\n        return \"%s\";\n    ", u.original_name);
		}
		vec_push(&f, "}");
		vec_push(&f, "}");
		vec_push(&c, "\n");
		vec_push(&c, rjoin(&f, "\n"));
	}
	if (O.string_enums && encode) {
		Vec f = {0};
		PUSH(&f, "export function %sToNumber(object: %s): number {", uncapitalize(full_name), full_name);
		vec_push(&f, "switch (object) {");
		for (int i = 0; i < e->nvalue; i++)
			PUSH(&f, "case %s.%s: return %d;", full_name, get_member_name(e, e->value[i]), (int)e->value[i]->number);
		if (!unrec_on)
			vec_push(&f, (char *)unrecognized_throw);
		else if (!u.present)
			PUSH(&f, "\n        case %s.%s:\n        default:\n          return %s;\n      ", full_name, uname, uval);
		else
			PUSH(&f, "\n        default:\n          return %s;\n      ", uval);
		vec_push(&f, "}");
		vec_push(&f, "}");
		vec_push(&c, "\n");
		vec_push(&c, rjoin(&f, "\n"));
	}
	vec_push(chunks, rjoin(&c, "\n"));
}

/* ---- the methods ---- */

/* the property name of a oneof union and its accessors */
static const char *roneof_name(TpdMessage *m, const TpdField *f) { return maybe_snake_to_camel(m->oneof_decl[f->oneof_index]->name); }

static const char *raw_base_instance(const char *full_name, TpdMessage *m, const char *full_type_name)
{
	Vec fields = {0};
	if (O.add_type_to_messages) /* fields.unshift */
		PUSH(&fields, "$type: '%s'", full_type_name);
	bool *done = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		if (is_within_one_of_that_should_be_union(f)) {
			if (!done[f->oneof_index])
				PUSH(&fields, "%s: undefined", rkey(roneof_name(m, f)));
			done[f->oneof_index] = true;
			continue;
		}
		PUSH(&fields, "%s: %s", rkey(get_field_name(f)), is_within_one_of(f) ? "undefined" : rdefault(f));
	}
	if (O.unknown_fields)
		vec_push(&fields, "_unknownFields: {}");
	return xfmt("\n    function createBase%s(): %s {\n      return { %s };\n    }\n  ", full_name, full_name, rjoin(&fields, ","));
}

/* getEncodeWriteSnippet(field)(place) */
static const char *rwrite(const TpdField *f, const char *place)
{
	if (f->type == T_ENUM && O.string_enums)
		return xfmt("writer.uint32(%lld).int32(%s(%s))", rtag(f, 0), renum_method(f->type_name, "ToNumber"), place);
	if (f->type == T_MESSAGE)
		return xfmt("%s.encode(%s, writer.uint32(%lld).fork()).join()", rsym(f->type_name), place, rtag(f, 2));
	return xfmt("writer.uint32(%lld).%s(%s)", rtag(f, basic_wire_type(f->type)), to_reader_call(f->type), place);
}

static const char *raw_encode(const char *full_name, TpdMessage *m)
{
	Vec c = {0};
	PUSH(&c, "\n    encode(\n      %s: %s,\n      writer: BinaryWriter = new BinaryWriter(),\n    ): BinaryWriter {\n  ",
	     m->nfield > 0 || O.unknown_fields ? "message" : "_", full_name);
	bool *done = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *mp = raccess("message", get_field_name(f), false);
		if (is_within_one_of_that_should_be_union(f)) {
			int oi = f->oneof_index;
			if (done[oi])
				continue;
			done[oi] = true;
			const char *on = raccess("message", roneof_name(m, f), false);
			PUSH(&c, "switch (%s?.$case) {", on);
			for (int j = 0; j < m->nfield; j++) {
				TpdField *g = m->field[j];
				if (!is_within_one_of_that_should_be_union(g) || g->oneof_index != oi)
					continue;
				const char *gn = maybe_snake_to_camel(g->name);
				PUSH(&c, "case \"%s\":\n            %s;\n            break;", gn, rwrite(g, xcat(on, ".", oneof_value_name(gn))));
			}
			vec_push(&c, "}");
		} else if (is_within_one_of(f) || f->type == T_MESSAGE) {
			PUSH(&c, "\n        if (%s !== undefined ) {\n          %s;\n        }\n      ", mp, rwrite(f, mp));
		} else {
			PUSH(&c, "\n        if (%s) {\n          %s;\n        }\n      ", rnot_default(f, m, mp), rwrite(f, mp));
		}
	}
	if (O.unknown_fields)
		PUSH(&c, "if (message._unknownFields !== undefined) {\n      for (const [key, values] of %s.Object.entries(message._unknownFields)) {\n"
			 "        const tag = parseInt(key, 10);\n        for (const value of values) {\n          writer.uint32(tag).raw(value);\n"
			 "        }\n      }\n    }",
		     rgt());
	vec_push(&c, "return writer;");
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}

static const char *raw_decode(const char *full_name, TpdMessage *m)
{
	Vec c = {0};
	PUSH(&c, "\n    decode(\n      input: BinaryReader | Uint8Array,\n      length?: number,\n    ): %s {\n"
		 "      const reader = input instanceof BinaryReader ? input : new BinaryReader(input);\n"
		 "      const previousRecursionDepth = (reader as any).__tsProtoDecodeDepth ?? 0;\n"
		 "      if (previousRecursionDepth >= 100) {\n"
		 "        throw new %s.Error(\"protobuf decode recursion limit exceeded\");\n      }\n"
		 "      (reader as any).__tsProtoDecodeDepth = previousRecursionDepth + 1;\n      try {\n"
		 "        const end = length === undefined ? reader.len : reader.pos + length;\n  ",
	     full_name, rgt());
	PUSH(&c, "const message = createBase%s();", full_name);
	vec_push(&c, "\n    while (reader.pos < end) {\n      const tag = reader.uint32();\n      switch (tag >>> 3) {\n  ");
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *name = get_field_name(f);
		PUSH(&c, "case %d: {", (int)f->number);
		const char *read;
		if (f->type == T_MESSAGE)
			read = xfmt("%s.decode(reader, reader.uint32())", rsym(f->type_name));
		else if (f->type == T_ENUM && O.string_enums)
			read = xfmt("%s(reader.int32())", renum_method(f->type_name, "FromJSON"));
		else if (f->type == T_ENUM)
			read = "reader.int32() as any";
		else
			read = xfmt("reader.%s()", to_reader_call(f->type));
		const char *check = xfmt("\n      if (tag !== %lld) {\n        break;\n      }\n    ", rtag(f, basic_wire_type(f->type)));
		if (is_within_one_of_that_should_be_union(f))
			PUSH(&c, "\n        %s\n        %s = { $case: '%s', %s: %s };\n      ", check, raccess("message", roneof_name(m, f), false), name,
			     oneof_value_name(name), read);
		else
			PUSH(&c, "\n        %s\n        %s = %s;\n      ", check, raccess("message", name, false), read);
		vec_push(&c, "continue; }");
	}
	vec_push(&c, "}");
	vec_push(&c, "\n      if ((tag & 7) === 4 || tag === 0) {\n        break;\n      }\n  ");
	if (O.unknown_fields)
		vec_push(&c, "\n      const buf = reader.skip(tag & 7);\n\n      \n      const list = message._unknownFields![tag];\n\n"
			     "      if (list === undefined ) {\n        message._unknownFields![tag] = [buf];\n      } else {\n        list.push(buf);\n      }\n    ");
	else
		vec_push(&c, "\n        reader.skip(tag & 7);\n    ");
	vec_push(&c, "}");
	vec_push(&c, "return message;");
	vec_push(&c, "\n      } finally {\n        (reader as any).__tsProtoDecodeDepth = previousRecursionDepth;\n      }\n    }\n  ");
	return rjoin(&c, "\n");
}

/* generate-async-iterable.ts */
static const char *raw_transform(const char *n, bool encode)
{
	if (encode)
		return xfmt("\n    // encodeTransform encodes a source of message objects.\n    // Transform<%s, Uint8Array>\n    async *encodeTransform(\n"
			    "      source: AsyncIterable<%s | %s[]> | Iterable<%s | %s[]>\n    ): AsyncIterable<Uint8Array> {\n"
			    "      for await (const pkt of source) {\n        if (%s.Array.isArray(pkt)) {\n          for (const p of (pkt as any)) {\n"
			    "            yield* [%s.encode(p).finish()]\n          }\n        } else {\n          yield* [%s.encode(pkt as any).finish()]\n"
			    "        }\n      }\n    }\n  ",
			    n, n, n, n, n, rgt(), n, n);
	return xfmt("\n    // decodeTransform decodes a source of encoded messages.\n    // Transform<Uint8Array, %s>\n    async *decodeTransform(\n"
		    "      source: AsyncIterable<Uint8Array | Uint8Array[]> | Iterable<Uint8Array | Uint8Array[]>\n    ): AsyncIterable<%s> {\n"
		    "      for await (const pkt of source) {\n        if (%s.Array.isArray(pkt)) {\n          for (const p of (pkt as any)) {\n"
		    "            yield* [%s.decode(p)]\n          }\n        } else {\n          yield* [%s.decode(pkt as any)]\n        }\n      }\n    }\n  ",
		    n, n, rgt(), n, n);
}

/* fromJSON's readSnippet(from) */
static const char *rfrom_json(const TpdField *f, const char *from)
{
	if (f->type == T_ENUM)
		return xfmt("%s(%s)", renum_method(f->type_name, "FromJSON"), from);
	if (f->type == T_MESSAGE)
		return xfmt("%s.fromJSON(%s)", rsym(f->type_name), from);
	return xfmt("%s.%s(%s)", rgt(), capitalize(rtype(f)), from);
}

static const char *raw_from_json(const char *full_name, TpdMessage *m)
{
	Vec c = {0};
	PUSH(&c, "\n      fromJSON(%s: any): %s {\n        return {\n    ", m->nfield > 0 ? "object" : "_", full_name);
	if (O.add_type_to_messages)
		PUSH(&c, "$type: %s.$type,", full_name);
	/* the first and last member of each oneof (oneofFieldsCases) */
	int no = m->noneof_decl > 0 ? m->noneof_decl : 0;
	int *firsts = xalloc(sizeof(int) * (size_t)(no + 1)), *lasts = xalloc(sizeof(int) * (size_t)(no + 1));
	for (int i = 0; i < no; i++)
		firsts[i] = lasts[i] = -1;
	for (int j = 0; j < m->nfield; j++) {
		int oi = m->field[j]->oneof_index;
		if (is_within_one_of(m->field[j]) && oi >= 0 && oi < no) {
			lasts[oi] = j;
			if (firsts[oi] < 0)
				firsts[oi] = j;
		}
	}
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *name = get_field_name(f);
		const char *jp = raccess("object", get_field_json_name(f->name, f->json_name), false);
		bool proto_json = O.proto_json_format && !str_eq(f->name, f->json_name);
		const char *pp = raccess("object", f->name, false);
		if (is_within_one_of_that_should_be_union(f)) {
			int first = -1, last = -1;
			if (f->oneof_index >= 0 && f->oneof_index < no) {
				first = firsts[f->oneof_index];
				last = lasts[f->oneof_index];
			} else {
				for (int j = 0; j < m->nfield; j++)
					if (is_within_one_of(m->field[j]) && m->field[j]->oneof_index == f->oneof_index)
						last = j, first = first < 0 ? j : first;
			}
			if (k == first)
				PUSH(&c, "%s: ", roneof_name(m, f));
			const char *vn = oneof_value_name(rkey(name));
			PUSH(&c, "isSet(%s) ? { $case: '%s', %s: %s} : ", jp, name, vn, rfrom_json(f, jp));
			if (proto_json)
				PUSH(&c, "isSet(%s) ? { $case: '%s', %s: %s} : ", pp, name, vn, rfrom_json(f, pp));
			if (k == last)
				vec_push(&c, "undefined,");
			continue;
		}
		const char *cmp = proto_json ? xfmt(" : isSet(%s) ? %s", pp, rfrom_json(f, pp)) : "";
		PUSH(&c, "\n        %s: isSet(%s) ? %s\n        %s\n          : %s,\n      ", rkey(name), jp, rfrom_json(f, jp), cmp,
		     is_within_one_of(f) ? "undefined" : rdefault(f));
	}
	vec_push(&c, "};");
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}

/* toJSON's readSnippet(from) */
static const char *rto_json(const TpdField *f, const char *from)
{
	if (f->type == T_ENUM)
		return xfmt("%s(%s)", renum_method(f->type_name, "ToJSON"), from);
	if (f->type == T_MESSAGE)
		return xfmt("%s.toJSON(%s)", rsym(f->type_name), from);
	if (is_whole_number(f))
		return xfmt("Math.round(%s)", from);
	return from;
}

static const char *raw_to_json(const char *full_name, TpdMessage *m)
{
	Vec c = {0};
	PUSH(&c, "\n    toJSON(%s: %s): unknown {\n      const obj: any = {};\n  ", m->nfield > 0 ? "message" : "_", full_name);
	const char *target = "";
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *name = get_field_name(f);
		const char *json_name = get_field_json_name(f->name, f->json_name);
		const char *mp = raccess("message", name, false);
#define RSET(value)                                                                                                  \
	(str_eq(json_name, "__proto__") ? xfmt("%s.Object.defineProperty(obj, \"__proto__\", { value: %s, enumerable: true });", rgt(), value) \
					: xfmt("%s = %s;", raccess("obj", json_name, false), value))
		if (is_within_one_of_that_should_be_union(f)) {
			const char *on = raccess("message", roneof_name(m, f), false);
			PUSH(&c, "\n        %sif (%s?.$case === '%s') {\n          %s\n        }\n      ", str_eq(target, on) ? "else " : "", on, name,
			     RSET(rto_json(f, xcat(on, ".", oneof_value_name(name)))));
			target = on;
			continue;
		}
		const char *check = (is_scalar(f) || f->type == T_ENUM) && !(is_within_one_of(f) || O.emit_default_values_json)
					    ? rnot_default(f, m, mp)
					    : xfmt("%s !== undefined ", mp);
		PUSH(&c, "\n        if (%s) {\n          %s\n        }\n      ", check, RSET(rto_json(f, mp)));
#undef RSET
	}
	vec_push(&c, "return obj;");
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}

static const char *raw_from_partial(const char *full_name, TpdMessage *m)
{
	Vec c = {0};
	PUSH(&c, "\n      create<I extends Exact<DeepPartial<%s>, I>>(base?: I): %s {\n        return %s.fromPartial(base ?? ({} as any));\n      },\n    ",
	     full_name, full_name, full_name);
	PUSH(&c, "\n      fromPartial<I extends Exact<DeepPartial<%s>, I>>(%s: I): %s {\n    ", full_name, m->nfield > 0 ? "object" : "_", full_name);
	PUSH(&c, "const message = createBase%s();", full_name);
	const char *target = NULL;
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *name = get_field_name(f);
		const char *mp = raccess("message", name, false);
		const char *op = raccess("object", name, false);
		bool union_ = is_within_one_of_that_should_be_union(f);
		if (target && !union_) {
			vec_push(&c, "}");
			target = NULL;
		}
		if (union_) {
			const char *oname = roneof_name(m, f);
			const char *om = raccess("message", oname, false), *oo = raccess("object", oname, false);
			const char *vn = oneof_value_name(name);
			const char *from = xcat(oo, ".", vn);
			const char *v = f->type == T_MESSAGE ? xfmt("%s.fromPartial(%s)", rsym(f->type_name), from) : from;
			if (target && !str_eq(target, oo)) {
				vec_push(&c, "}");
				target = NULL;
			}
			if (!target)
				PUSH(&c, "switch (%s?.$case) {", oo);
			PUSH(&c, "\n        case '%s': {\n          if (%s?.%s !== undefined\n              && %s?.%s !== null) {\n            %s = { $case: '%s', %s: %s };\n"
				 "          }\n          break;\n        }\n      ",
			     name, oo, vn, oo, vn, om, name, vn, v);
			target = oo;
			continue;
		}
		const char *fallback = is_within_one_of(f) ? "undefined" : rdefault(f);
		if (f->type != T_MESSAGE)
			PUSH(&c, "%s = %s ?? %s;", mp, op, fallback);
		else
			PUSH(&c, "\n        %s = (%s !== undefined && %s !== null)\n          ? %s.fromPartial(%s)\n          : %s;\n      ", mp, op, op, rsym(f->type_name),
			     op, fallback);
	}
	if (target)
		vec_push(&c, "}");
	vec_push(&c, "return message;");
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}

static void raw_methods(void *arg, const char *full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name)
{
	(void)si;
	Vec *chunks = arg;
	const char *full_type_name = maybe_prefix_package(C.file, proto_full_name);
	if (is_wrapper_type(full_type_name))
		RAW_UNSUPPORTED("the well-known types");
	vec_push(chunks, (char *)raw_base_instance(full_name, m, full_type_name));
	Vec members = {0};
	if (has_type_member())
		PUSH(&members, "$type: '%s' as const", full_type_name);
	vec_push(&members, (char *)raw_encode(full_name, m));
	vec_push(&members, (char *)raw_decode(full_name, m));
	if (opt_true(&O.opts, "useAsyncIterable")) {
		vec_push(&members, (char *)raw_transform(full_name, true));
		vec_push(&members, (char *)raw_transform(full_name, false));
	}
	vec_push(&members, (char *)raw_from_json(full_name, m));
	vec_push(&members, (char *)raw_to_json(full_name, m));
	vec_push(&members, (char *)raw_from_partial(full_name, m));
	const char *params = has_type_member() ? xfmt("%s, '%s'", full_name, full_type_name) : full_name;
	PUSH(chunks, "\n            export const %s: MessageFns<%s> = {\n              %s\n            };\n          ", full_name, params, rjoin(&members, ",\n\n"));
	if (O.output_type_registry)
		PUSH(chunks, "\n            messageTypeRegistry.set(%s.$type, %s);\n          ", full_name, full_name);
}

/* ---- services ---- */

static const char *rmsg(const char *type_name)
{
	if (!strncmp(type_name, ".google.protobuf.", 17))
		RAW_UNSUPPORTED("well-known types in services");
	return rsym(type_name);
}

/* generate-services.ts generateService */
static const char *raw_service(TpdService *s, SourceInfo *si, bool grpc_web)
{
	Vec c = {0};
	rcomment(&c, si->self, s->options && s->options->deprecated, NULL);
	PUSH(&c, "export interface %s {", s->name);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		rcomment(&c, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		Vec params = {0};
		const char *in = rmsg(md->input_type);
		PUSH(&params, "request: %s", grpc_web ? xfmt("DeepPartial<%s>", in) : in);
		if (grpc_web)
			vec_push(&params, "metadata?: grpc.Metadata");
		if (opt_true(&O.opts, "useAbortSignal"))
			vec_push(&params, "abortSignal?: AbortSignal");
		PUSH(&c, "%s(%s): Promise<%s>;", format_method_name(md->name), rjoin(&params, ","), rmsg(md->output_type));
	}
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}

static const char *raw_service_client_impl(TpdFile *f, TpdService *s)
{
	Vec c = {0};
	const char *name = s->name;
	PUSH(&c, "export const %sServiceName = \"%s\";", name, maybe_prefix_package(f, name));
	PUSH(&c, "export class %sClientImpl implements %s {", name, name);
	vec_push(&c, "private readonly rpc: Rpc;");
	vec_push(&c, "private readonly service: string;");
	vec_push(&c, "constructor(rpc: Rpc, opts?: {service?: string}) {");
	PUSH(&c, "this.service = opts?.service || %sServiceName;", name);
	vec_push(&c, "this.rpc = rpc;");
	for (int k = 0; k < s->nmethod; k++) {
		const char *mn = format_method_name(s->method[k]->name);
		PUSH(&c, "this.%s = this.%s.bind(this);", mn, mn);
	}
	vec_push(&c, "}");
	bool abort = opt_true(&O.opts, "useAbortSignal");
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		const char *in = rmsg(md->input_type), *out = rmsg(md->output_type);
		Vec params = {0};
		PUSH(&params, "request: %s", in);
		if (abort)
			vec_push(&params, "abortSignal?: AbortSignal");
		PUSH(&c, "\n    %s(\n      %s\n    ): Promise<%s> {\n      const data = %s.encode(request).finish(); \n      const promise = this.rpc.request(\n"
			 "        \n        this.service,\n        \"%s\",\n        data,\n        \n        %s\n      );\n"
			 "      return promise.then((data) => %s.decode(new BinaryReader(data)));\n    }\n  ",
		     format_method_name(md->name), rjoin(&params, ","), out, in, md->name, abort ? "abortSignal || undefined," : "", out);
	}
	vec_push(&c, "}");
	return rcat(&c);
}

static const char *raw_rpc_type(void)
{
	Vec c = {0};
	vec_push(&c, "    interface Rpc {");
	PUSH(&c, "\n      request(\n        \n        service: string,\n        method: string,\n        data: Uint8Array,\n        \n        %s\n      ): Promise<Uint8Array>;",
	     opt_true(&O.opts, "useAbortSignal") ? "abortSignal?: AbortSignal," : "");
	vec_push(&c, "    }");
	return rjoin(&c, "\n");
}

/* generate-grpc-js.ts */
static const char *raw_grpc_js(TpdFile *f, TpdService *s, SourceInfo *si)
{
	const char *fq = maybe_prefix_package(f, s->name);
	Vec parts = {0};
	Vec c = {0};
	rcomment(&c, si->self, s->options && s->options->deprecated, NULL);
	PUSH(&c, "\n    export type %sService = typeof %sService;\n  ", s->name, s->name);
	PUSH(&c, "\n    export const %sService = {\n  ", s->name);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		const char *in = rmsg(md->input_type), *out = rmsg(md->output_type);
		rcomment(&c, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		PUSH(&c, "\n      %s: {\n        path: '/%s/%s' as const,\n        requestStream: false as const,\n        responseStream: false as const,\n"
			 "        requestSerialize: (value: %s): Buffer =>\n          Buffer.from(%s.encode(value).finish()),\n"
			 "        requestDeserialize: (value: Buffer): %s => %s.decode(value),\n"
			 "        responseSerialize: (value: %s): Buffer =>\n          Buffer.from(%s.encode(value).finish()),\n"
			 "        responseDeserialize: (value: Buffer): %s => %s.decode(value),\n      },\n    ",
		     format_method_name(md->name), fq, md->name, in, in, in, in, out, out, out, out);
	}
	vec_push(&c, "} as const;");
	vec_push(&parts, rjoin(&c, "\n"));
	Vec sv = {0};
	PUSH(&sv, "export interface %sServer extends UntypedServiceImplementation {", s->name);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		rcomment(&sv, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		PUSH(&sv, "\n      %s: handleUnaryCall<%s, %s>;\n    ", format_method_name(md->name), rmsg(md->input_type), rmsg(md->output_type));
	}
	vec_push(&sv, "}");
	vec_push(&parts, rjoin(&sv, "\n"));
	if (js_truthy(opt(&O.opts, "outputClientImpl"))) {
		Vec cl = {0};
		PUSH(&cl, "export interface %sClient extends Client {", s->name);
		for (int k = 0; k < s->nmethod; k++) {
			TpdMethod *md = s->method[k];
			const char *mn = format_method_name(md->name), *in = rmsg(md->input_type);
			const char *cb = xfmt("(error: ServiceError | null, response: %s) => void", rmsg(md->output_type));
			rcomment(&cl, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
			PUSH(&cl, "\n          %s(\n            request: %s,\n            callback: %s,\n          ): ClientUnaryCall;\n"
				  "          %s(\n            request: %s,\n            metadata: Metadata,\n            callback: %s,\n          ): ClientUnaryCall;\n"
				  "          %s(\n            request: %s,\n            metadata: Metadata,\n            options: Partial<CallOptions>,\n"
				  "            callback: %s,\n          ): ClientUnaryCall;\n        ",
			     mn, in, cb, mn, in, cb, mn, in, cb);
		}
		vec_push(&cl, "}");
		vec_push(&parts, rjoin(&cl, "\n"));
		PUSH(&parts, "\n    export const %sClient = makeGenericClientConstructor(\n      %sService,\n      '%s'\n    ) as unknown as {\n      new (\n"
			     "        address: string,\n        credentials: ChannelCredentials,\n        options?: Partial<ClientOptions>,\n      ): %sClient;\n"
			     "      service: typeof %sService;\n      serviceName: string;\n    }\n  ",
		     s->name, s->name, fq, s->name, s->name);
	}
	return rjoin(&parts, "\n\n");
}

/* generate-nice-grpc.ts */
static const char *raw_nice_grpc(TpdService *s, SourceInfo *si)
{
	Vec sv = {0}, cl = {0};
	PUSH(&sv, "export interface %s%sImplementation<CallContextExt = {}> {", s->name, has_suffix(s->name, "Service") ? "" : "Service");
	PUSH(&cl, "export interface %sClient<CallOptionsExt = {}> {", s->name);
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		const char *mn = uncapitalize(md->name), *in = rmsg(md->input_type), *out = rmsg(md->output_type);
		rcomment(&sv, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		PUSH(&sv, "\n          %s(\n            request: %s,\n            context: CallContext & CallContextExt,\n          ): Promise<%s>;\n        ", mn, in,
		     O.output_partial ? xfmt("DeepPartial<%s>", out) : out);
		rcomment(&cl, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		PUSH(&cl, "\n          %s(\n            request: %s,\n            options?: CallOptions & CallOptionsExt,\n          ): Promise<%s>;\n        ", mn,
		     O.output_partial ? xfmt("DeepPartial<%s>", in) : in, out);
	}
	vec_push(&sv, "}");
	vec_push(&cl, "}");
	Vec parts = {0};
	vec_push(&parts, rjoin(&sv, "\n"));
	vec_push(&parts, rjoin(&cl, "\n"));
	return rjoin(&parts, "\n\n");
}

/* generate-generic-service-definition.ts */

static const char *raw_method_options(const TpdMethodOptions *o)
{
	Vec c = {0};
	vec_push(&c, "{");
	if (o && o->idempotency_level == 2)
		vec_push(&c, "idempotencyLevel: 'IDEMPOTENT',");
	else if (o && o->idempotency_level == 1)
		vec_push(&c, "idempotencyLevel: 'NO_SIDE_EFFECTS',");
	if (o && o->unknown) {
		bool node = O.env == ENV_NODE;
		Vec u = {0};
		vec_push(&u, "{");
		for (int i = 0; i < o->nunknown; i++) {
			const TpdUnknownField *f = &o->unknown[i];
			Vec values = {0};
			for (int k = 0; k < f->nvalue; k++) {
				Buf b = {0};
				buf_adds(&b, node ? "Buffer.from([" : "new Uint8Array([");
				for (size_t j = 0; j < f->value[k].n; j++)
					buf_fmt(&b, "%s%d", j ? ", " : "", (unsigned char)f->value[k].p[j]);
				buf_adds(&b, node ? "]) as Buffer" : "]) as Uint8Array");
				vec_push(&values, xstrdup(buf_str(&b)));
			}
			PUSH(&u, "%lld: [\n%s\n],", (long long)f->tag, rjoin(&values, ","));
		}
		vec_push(&u, "}");
		PUSH(&c, "_unknownFields: %s", rjoin(&u, "\n"));
	}
	vec_push(&c, "}");
	return rjoin(&c, "\n");
}
static const char *raw_generic(TpdFile *f, TpdService *s, SourceInfo *si)
{
	Vec c = {0};
	rcomment(&c, si->self, s->options && s->options->deprecated, NULL);
	PUSH(&c, "\n    export type %sDefinition = typeof %sDefinition;\n  ", s->name, s->name);
	PUSH(&c, "\n    export const %sDefinition = {\n  ", s->name);
	PUSH(&c, "\n      name: '%s',\n      fullName: '%s',\n      methods: {\n  ", s->name, maybe_prefix_package(f, s->name));
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		const char *in = rmsg(md->input_type), *out = rmsg(md->output_type);
		rcomment(&c, si_lookup(si, SERVICE_METHOD, k), md->options && md->options->deprecated, NULL);
		PUSH(&c, "\n      %s: \n    {\n      name: '%s',\n      requestType: %s as typeof %s,\n      requestStream: false,\n"
			 "      responseType: %s as typeof %s,\n      responseStream: false,\n      options: %s\n    }\n  ,\n    ",
		     uncapitalize(md->name), md->name, in, in, out, out, raw_method_options(md->options));
	}
	vec_push(&c, "\n      },\n    } as const;\n  ");
	return rjoin(&c, "\n");
}

/* generate-grpc-web.ts */
static const char *raw_grpc_web_client(TpdService *s)
{
	bool abort = opt_true(&O.opts, "useAbortSignal");
	const char *as = abort ? "abortSignal?: AbortSignal," : "";
	Buf b = {0};
	buf_fmt(&b, "\n    export class %sClientImpl implements %s {\n  \n", s->name, s->name);
	buf_adds(&b, "\n    private readonly rpc: Rpc;\n\n    constructor(rpc: Rpc) {\n  \nthis.rpc = rpc;\n");
	for (int k = 0; k < s->nmethod; k++) {
		const char *mn = format_method_name(s->method[k]->name);
		buf_fmt(&b, "this.%s = this.%s.bind(this);\n", mn, mn);
	}
	buf_adds(&b, "}\n");
	for (int k = 0; k < s->nmethod; k++) {
		TpdMethod *md = s->method[k];
		const char *in = rmsg(md->input_type);
		buf_fmt(&b, "\n    %s(\n      request: DeepPartial<%s>,\n      metadata?: grpc.Metadata,\n      %s\n    ): Promise<%s> {\n"
			    "      return this.rpc.unary(\n        %s%sDesc,\n        %s.fromPartial(request),\n        metadata,\n        %s\n      );\n    }\n  \n",
			format_method_name(md->name), in, as, rmsg(md->output_type), s->name, md->name, in, abort ? "abortSignal," : "");
	}
	buf_adds(&b, "}");
	return xstrdup(buf_str(&b));
}

static const char *raw_grpc_web_misc(void)
{
	bool abort = opt_true(&O.opts, "useAbortSignal");
	const char *as = abort ? "abortSignal?: AbortSignal," : "";
	Vec parts = {0};
	vec_push(&parts, "\n    interface UnaryMethodDefinitionishR extends grpc.UnaryMethodDefinition<any, any> { requestStream: any; responseStream: any; }\n  ");
	vec_push(&parts, "type UnaryMethodDefinitionish = UnaryMethodDefinitionishR;");
	Vec r = {0};
	vec_push(&r, "interface Rpc {");
	PUSH(&r, "\n    unary<T extends UnaryMethodDefinitionish>(\n      methodDesc: T,\n      request: any,\n      metadata: grpc.Metadata | undefined,\n      %s\n"
		 "    ): Promise<any>;\n  ",
	     as);
	vec_push(&r, "}");
	vec_push(&parts, rjoin(&r, "\n"));
	const char *options = "\n    {\n      transport?: grpc.TransportFactory,\n      \n      debug?: boolean,\n      metadata?: grpc.Metadata,\n"
			      "      upStreamRetryCodes?: number[],\n    }\n  ";
	const char *abort_body = abort ? "\n      if (abortSignal) abortSignal.addEventListener(\"abort\", () => {\n        client.close();\n        reject(abortSignal.reason);\n      });" : "";
	PUSH(&parts,
	     "\n    export class GrpcWebImpl {\n      private host: string;\n      private options: %s;\n      \n      constructor(host: string, options: %s) {\n"
	     "        this.host = host;\n        this.options = options;\n      }\n  \n    unary<T extends UnaryMethodDefinitionish>(\n      methodDesc: T,\n"
	     "      _request: any,\n      metadata: grpc.Metadata | undefined,\n      %s\n    ): Promise<any> {\n"
	     "      const request = { ..._request, ...methodDesc.requestType };\n      const maybeCombinedMetadata = metadata && this.options.metadata\n"
	     "        ? new BrowserHeaders({ ...this.options?.metadata.headersMap, ...metadata?.headersMap })\n        : metadata ?? this.options.metadata;\n"
	     "      return new Promise((resolve, reject) => {\n        %s grpc.unary(methodDesc, {\n          request,\n          host: this.host,\n"
	     "          metadata: maybeCombinedMetadata ?? {},\n          ...(this.options.transport !== undefined ? {transport: this.options.transport} : {}),\n"
	     "          debug: this.options.debug ?? false,\n          onEnd: function (response) {\n            if (response.status === grpc.Code.OK) {\n"
	     "              resolve(response.message!.toObject());\n            } else {\n"
	     "              const err = new GrpcWebError(response.statusMessage, response.status, response.trailers);\n              reject(err);\n            }\n"
	     "          },\n        });\n\n        %s\n      });\n    }\n  }",
	     options, options, as, abort ? "const client =" : "", abort_body);
	return rjoin(&parts, "\n\n");
}

COLD static void raw_services(Vec *chunks, TpdFile *f, SourceInfo *si)
{
	if (!f->nservice)
		return;
	JsVal *oci = opt(&O.opts, "outputClientImpl");
	bool impl = oci->t == JS_BOOL && oci->b, grpc_web = oci->t == JS_STRING && str_eqc(oci->s, "grpc-web");
	if (opt_true(&O.opts, "context") || opt_true(&O.opts, "addGrpcMetadata") || opt_true(&O.opts, "metadataType") ||
	    opt_true(&O.opts, "rpcBeforeRequest") || opt_true(&O.opts, "rpcAfterResponse") || opt_true(&O.opts, "rpcErrorHandler") ||
	    opt_true(&O.opts, "returnObservable") || opt_true(&O.opts, "addNestjsRestParameter") || (js_truthy(oci) && !impl && !grpc_web))
		RAW_UNSUPPORTED("these service options");
	Vec uniq = output_services_sorted(f);
	for (int k = 0; k < f->nservice; k++) {
		TpdService *s = f->service[k];
		SourceInfo ssi = si_open(si, FILE_SERVICE, k);
		for (int m = 0; m < s->nmethod; m++)
			if (s->method[m]->client_streaming || s->method[m]->server_streaming)
				RAW_UNSUPPORTED("streaming methods");
		for (int u = 0; u < uniq.n; u++) {
			const char *svc = uniq.v[u];
			if (str_eq(svc, "grpc-js")) {
				vec_push(chunks, (char *)raw_grpc_js(f, s, &ssi));
			} else if (str_eq(svc, "nice-grpc")) {
				vec_push(chunks, (char *)raw_nice_grpc(s, &ssi));
			} else if (str_eq(svc, "generic-definitions")) {
				vec_push(chunks, (char *)raw_generic(f, s, &ssi));
			} else if (str_eq(svc, "default")) {
				vec_push(chunks, (char *)raw_service(s, &ssi, grpc_web));
				if (impl) {
					vec_push(chunks, (char *)raw_service_client_impl(f, s));
				} else if (grpc_web) {
					vec_push(chunks, (char *)raw_grpc_web_client(s));
					PUSH(chunks, "\n    export const %sDesc = {\n      serviceName: \"%s\",\n    };\n  ", s->name, maybe_prefix_package(f, s->name));
					for (int m = 0; m < s->nmethod; m++) {
						TpdMethod *md = s->method[m];
						PUSH(chunks,
						     "\n    export const %s%sDesc: UnaryMethodDefinitionish = {\n      methodName: \"%s\",\n      service: %sDesc,\n"
						     "      requestStream: false,\n      responseStream: false,\n      requestType: {\n    serializeBinary() {\n"
						     "      return %s.encode(this).finish();\n    },\n  } as any,\n      responseType: {\n"
						     "    deserializeBinary(data: Uint8Array) {\n      const value = %s.decode(data);\n      return {\n        ...value,\n"
						     "        toObject() { return value; },\n      };\n    }\n  } as any,\n    };\n  ",
						     s->name, md->name, md->name, s->name, rmsg(md->input_type), rmsg(md->output_type));
					}
				}
			}
		}
	}
	if (opt_includes(&O.opts, "outputServices", "default") && js_truthy(oci)) {
		if (impl)
			vec_push(chunks, (char *)raw_rpc_type());
		else if (grpc_web)
			vec_push(chunks, (char *)raw_grpc_web_misc());
	}
}

/* ---- the declarations of the utils (makeUtils) ---- */

static const char *raw_util(const char *name)
{
	const char *ex = O.export_common_symbols ? "export" : "";
	if (str_eq(name, "globalThis"))
		return "";
	if (str_eq(name, "gt"))
		return "\n      declare const self: any | undefined;\n      declare const window: any | undefined;\n      declare const global: any | undefined;\n"
		       "      const gt: any = (() => {\n        if (typeof globalThis !== \"undefined\") return globalThis;\n"
		       "        if (typeof self !== \"undefined\") return self;\n        if (typeof window !== \"undefined\") return window;\n"
		       "        if (typeof global !== \"undefined\") return global;\n        throw \"Unable to locate global object\";\n      })();\n    ";
	if (str_eq(name, "Builtin")) {
		const char *more = O.force_long == LONG_BIGINT ? (O.use_date == DATE_TEMPORAL ? "| bigint | Temporal.Instant " : "| bigint ")
				   : O.use_date == DATE_TEMPORAL ? "| Temporal.Instant "
								 : "";
		return xfmt("type Builtin = Date | Function | Uint8Array | string | number | boolean %s| undefined;", more);
	}
	if (str_eq(name, "Exact"))
		return xfmt("\n      type KeysOfUnion<T> = T extends T ? keyof T : never;\n      %s type Exact<P, I> = %sP extends Builtin\n        ? P\n"
			    "        : P &\n        { [K in keyof P as Extract<K, keyof I>]: Exact<P[K], I[K & keyof I]> } & { [K in Exclude<keyof I, KeysOfUnion<P> %s>]: never };\n    ",
			    ex, O.use_unknown_for_value ? "unknown extends P ? P : " : "", O.add_type_to_messages ? "| '$type'" : "");
	if (str_eq(name, "DeepPartial")) {
		const char *oneof = O.oneof == ONEOF_UNIONS ? "\n      : T extends { $case: string }\n"
							      "      ? { [K in keyof Omit<T, '$case'>]?: DeepPartial<T[K]> } & { $case: T['$case'] }\n    "
				    : O.oneof == ONEOF_UNIONS_VALUE ? "\n      : T extends { $case: string; value: unknown; }\n"
								      "      ? { $case: T['$case']; value?: DeepPartial<T['value']>; }\n    "
								    : "";
		return xfmt("\n      %s type DeepPartial<T> = %s%s T extends Builtin\n        ? T\n        %s\n        : T extends globalThis.Array<infer U>\n"
			    "        ? globalThis.Array<DeepPartial<U>>\n        : T extends ReadonlyArray<infer U>\n        ? ReadonlyArray<DeepPartial<U>>%s\n"
			    "        : T extends {}\n        ? { [K in %s]?: DeepPartial<T[K]> }\n        : Partial<T>;\n    ",
			    ex, O.use_unknown_for_value ? "unknown extends T ? T : " : "", O.force_long == LONG_BIGINT ? "T extends bigint ? string | number | bigint : " : "",
			    O.force_long == LONG_LONG ? " : T extends Long ? string | number | Long " : "", oneof,
			    O.add_type_to_messages ? "Exclude<keyof T, '$type'>" : "keyof T");
	}
	if (str_eq(name, "isSet"))
		return "\n    function isSet(value: any): boolean {\n      return value !== null && value !== undefined;\n    }";
	if (str_eq(name, "GrpcWebError"))
		return xfmt("\n      export class GrpcWebError extends %s.Error {\n        constructor(message: string, public code: grpc.Code, public metadata: grpc.Metadata) {\n"
			    "          super(message);\n        }\n      }\n    ",
			    rgt());
	if (str_eq(name, "MessageFns")) {
		Vec c = {0};
		if (has_type_member())
			vec_push(&c, "readonly $type: V;");
		vec_push(&c, "\n        encode(message: T, writer?: BinaryWriter): BinaryWriter;\n      ");
		vec_push(&c, "\n        decode(input: BinaryReader | Uint8Array, length?: number): T;\n      ");
		if (opt_true(&O.opts, "useAsyncIterable")) {
			vec_push(&c, "\n      encodeTransform(\n        source: AsyncIterable<T | T[]> | Iterable<T | T[]>\n      ): AsyncIterable<Uint8Array>;\n    ");
			vec_push(&c, "\n      decodeTransform(\n        source: AsyncIterable<Uint8Array | Uint8Array[]> | Iterable<Uint8Array | Uint8Array[]>\n"
				     "      ): AsyncIterable<T>;\n    ");
		}
		vec_push(&c, "fromJSON(object: any): T;");
		vec_push(&c, "toJSON(message: T): unknown;");
		vec_push(&c, "create<I extends Exact<DeepPartial<T>, I>>(base?: I): T;");
		vec_push(&c, "fromPartial<I extends Exact<DeepPartial<T>, I>>(object: I): T;");
		return xfmt("\n            %s interface MessageFns<T%s> {\n              %s\n            }\n          ", ex,
			    has_type_member() ? ", V extends string" : "", rjoin(&c, "\n"));
	}
	RAW_UNSUPPORTED(xfmt("the helper %s", name));
}

/* generateFile + Code.toString: the body (bodyPart) of a file that dprint cannot parse */
COLD static void raw_file_body(void *arg, Buf *out)
{
	TpdFile *f = arg;
	if (opt_true(&O.opts, "nestJs") || !O.output_encode_true || !O.output_json_true || !O.output_partial || !O.use_exact_types ||
	    O.output_type_annotations || O.use_null_as_optional || O.use_readonly_types || O.use_prototype_for_defaults ||
	    !O.initialize_fields_as_undefined || O.no_defaults_for_optionals || O.remove_enum_prefix ||
	    (opt_true(&O.opts, "context") && opt_true(&O.opts, "useContextDataloaders")) || !C.is_proto3)
		RAW_UNSUPPORTED("these options or syntax");
	if (f->nextension)
		RAW_UNSUPPORTED("extensions");
	Vec chunks = {0};
	if (O.export_common_symbols) {
		PUSH(&chunks, "export const protobufPackage = '%s';", f->package);
		PUSH(&chunks, "export const protobufFileName = '%s';", f->name);
	}
	SourceInfo si = source_info_from(f);
	rcomment(&chunks, si_lookup(&si, FILE_SYNTAX, -1), f->options && f->options->deprecated, NULL);
	visit(f, &si, raw_interface, raw_enum, &chunks);
	visit(f, &si, raw_methods, NULL, &chunks);
	raw_services(&chunks, f, &si);
	if (opt_true(&O.opts, "esModuleInterop") && chunks.n == 0)
		vec_push(&chunks, "export {};");
	/* Object.values(utils): the helpers with Long after longToNumber and MessageFns always there */
	for (int i = 0; i < C.u->nhelpers; i++) {
		TpCond *co = C.u->helpers[i].co;
		vec_push(&chunks, tp_used(co) ? (char *)raw_util(co->name) : "");
		if (str_eq(co->name, "longToNumber"))
			vec_push(&chunks, "");
		if (str_eq(co->name, "fail") && !C.u->message_fns)
			vec_push(&chunks, "");
	}
	buf_adds(out, rjoin(&chunks, "\n\n"));
}
