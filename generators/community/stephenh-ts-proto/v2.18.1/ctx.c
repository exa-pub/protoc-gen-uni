/* The context of ts-proto (src/context.ts), the options as the generator code compares them
 * (src/options.ts enums), src/case.ts, src/sourceInfo.ts, the helpers of src/utils.ts and
 * src/visit.ts.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* ---- options as compared by the generator ---- */

enum { LONG_NUMBER, LONG_LONG, LONG_STRING, LONG_BIGINT, LONG_OTHER };
enum { DATE_DATE, DATE_STRING, DATE_STRING_NANO, DATE_TEMPORAL, DATE_TIMESTAMP, DATE_OTHER };
enum { ONEOF_PROPERTIES, ONEOF_UNIONS, ONEOF_UNIONS_VALUE, ONEOF_OTHER };
enum { ENV_NODE, ENV_BROWSER, ENV_BOTH, ENV_OTHER };

typedef struct {
	Options opts;
	int force_long, use_date, oneof, env;
	bool snake_keys, snake_json;
	bool use_json_name, use_null_as_optional, use_readonly_types, use_unknown_for_value, use_json_wire_format;
	bool uo_true_messages_all; /* useOptionals === true || "messages" || "all" */
	bool uo_all, uo_deprecated_only, uo_none; /* uo_none: useOptionals === false || "none" */
	bool string_enums, comments, export_common_symbols, use_exact_types, initialize_fields_as_undefined;
	bool no_defaults_for_optionals, disable_proto2_optionals, disable_proto2_defaults, use_map_type;
	bool use_js_type_override, use_prototype_for_defaults, unknown_fields, proto_json_format;
	bool emit_default_values_json, use_snake_type_name, only_types, use_mongo_object_id, big_int_literal;
	bool output_type_annotations, output_type_registry, add_type_to_messages;
	bool output_encode_true, output_json_true, output_partial;
	bool use_numeric_enum_for_json, remove_enum_prefix, use_optionals_bool;
	const char *file_suffix, *import_suffix;
	const char *type_prefix, *type_suffix, *message_prefix, *message_suffix, *enum_prefix, *enum_suffix;
	JsVal *M;
} Opt;

static Opt O;

static const char *opt_str(const char *key)
{
	JsVal *v = opt(&O.opts, key);
	return v->t == JS_STRING ? xstrndup(v->s.p, v->s.n) : js_value_to_string(v);
}

/* the options after optionsFromParameter and generateFile's normalization of useOptionals */
COLD static void opt_resolve(Options o)
{
	O.opts = o;
	Options *p = &O.opts;
	O.force_long = opt_is(p, "forceLong", "number")	  ? LONG_NUMBER
		       : opt_is(p, "forceLong", "long")	  ? LONG_LONG
		       : opt_is(p, "forceLong", "string") ? LONG_STRING
		       : opt_is(p, "forceLong", "bigint") ? LONG_BIGINT
							  : LONG_OTHER;
	O.use_date = opt_is(p, "useDate", "date")	     ? DATE_DATE
		     : opt_is(p, "useDate", "string")	     ? DATE_STRING
		     : opt_is(p, "useDate", "string-nano") ? DATE_STRING_NANO
		     : opt_is(p, "useDate", "temporal")    ? DATE_TEMPORAL
		     : opt_is(p, "useDate", "timestamp")   ? DATE_TIMESTAMP
							   : DATE_OTHER;
	O.oneof = opt_is(p, "oneof", "properties")	? ONEOF_PROPERTIES
		  : opt_is(p, "oneof", "unions")	? ONEOF_UNIONS
		  : opt_is(p, "oneof", "unions-value") ? ONEOF_UNIONS_VALUE
						       : ONEOF_OTHER;
	O.env = opt_is(p, "env", "node") ? ENV_NODE : opt_is(p, "env", "browser") ? ENV_BROWSER : opt_is(p, "env", "both") ? ENV_BOTH : ENV_OTHER;
	O.snake_keys = opt_includes(p, "snakeToCamel", "keys");
	O.snake_json = opt_includes(p, "snakeToCamel", "json");
	O.use_json_name = opt_true(p, "useJsonName");
	O.use_null_as_optional = opt_true(p, "useNullAsOptional");
	O.use_readonly_types = opt_true(p, "useReadonlyTypes");
	O.use_unknown_for_value = opt_true(p, "useUnknownForValue");
	O.use_json_wire_format = opt_true(p, "useJsonWireFormat");
	/* generateFile: useOptionals === false -> "none", === true -> "messages" */
	JsVal *uo = opt(p, "useOptionals");
	if (uo->t == JS_BOOL)
		js_obj_set(p->o, STR_LIT("useOptionals"), js_string(uo->b ? STR_LIT("messages") : STR_LIT("none")));
	O.uo_true_messages_all = opt_is(p, "useOptionals", "messages") || opt_is(p, "useOptionals", "all");
	O.uo_all = opt_is(p, "useOptionals", "all");
	O.uo_deprecated_only = opt_is(p, "useOptionals", "deprecatedOnly");
	O.uo_none = opt_is(p, "useOptionals", "none");
	O.string_enums = opt_true(p, "stringEnums");
	O.comments = opt_true(p, "comments");
	O.export_common_symbols = opt_true(p, "exportCommonSymbols");
	O.use_exact_types = opt_true(p, "useExactTypes");
	O.initialize_fields_as_undefined = opt_true(p, "initializeFieldsAsUndefined");
	O.no_defaults_for_optionals = opt_true(p, "noDefaultsForOptionals");
	O.disable_proto2_optionals = opt_true(p, "disableProto2Optionals");
	O.disable_proto2_defaults = opt_true(p, "disableProto2DefaultValues");
	O.use_map_type = opt_true(p, "useMapType");
	O.use_js_type_override = opt_true(p, "useJsTypeOverride");
	O.use_prototype_for_defaults = opt_true(p, "usePrototypeForDefaults");
	O.unknown_fields = opt_true(p, "unknownFields");
	O.proto_json_format = opt_true(p, "protoJsonFormat");
	O.emit_default_values_json = opt_includes(p, "emitDefaultValues", "json-methods");
	O.use_snake_type_name = opt_true(p, "useSnakeTypeName");
	O.only_types = opt_true(p, "onlyTypes");
	O.use_mongo_object_id = opt_true(p, "useMongoObjectId");
	O.big_int_literal = opt_true(p, "bigIntLiteral");
	O.output_type_annotations = opt_true(p, "outputTypeAnnotations");
	O.output_type_registry = opt_true(p, "outputTypeRegistry");
	/* addTypeToMessages */
	O.add_type_to_messages = (O.output_type_annotations || O.output_type_registry) &&
				 !opt_is(p, "outputTypeAnnotations", "static-only");
	O.output_encode_true = opt_is_true(p, "outputEncodeMethods");
	O.output_json_true = opt_is_true(p, "outputJsonMethods");
	O.output_partial = opt_true(p, "outputPartialMethods");
	O.use_numeric_enum_for_json = opt_true(p, "useNumericEnumForJson");
	O.remove_enum_prefix = opt_true(p, "removeEnumPrefix");
	O.file_suffix = opt_str("fileSuffix");
	O.import_suffix = opt_str("importSuffix");
	O.type_prefix = opt_str("typePrefix");
	O.type_suffix = opt_str("typeSuffix");
	O.message_prefix = opt_str("messagePrefix");
	O.message_suffix = opt_str("messageSuffix");
	O.enum_prefix = opt_str("enumPrefix");
	O.enum_suffix = opt_str("enumSuffix");
	O.M = opt(p, "M");
}

/* ---- context.ts ---- */

typedef struct {
	const char *module;  /* file name without ".proto" */
	const char *ts_name; /* FooMessage_InnerMessage */
	TpdMessage *msg;     /* or */
	TpdEnum *en;
} TypeInfo;

typedef struct Utils Utils;

static struct {
	TpdRequest *req;
	Map type_map; /* ".pkg.Message" -> TypeInfo */
	Utils *u;
	TpdFile *file; /* currentFile */
	bool is_proto3, is_edition;
	const char *proto_version, *ts_proto_version;
} C;

COLD static void create_file_context(TpdFile *f)
{
	C.file = f;
	int32_t edition = f->edition;
	C.is_edition = edition != TPD_EDITION_UNKNOWN;
	bool proto3_edition = C.is_edition && (edition == TPD_EDITION_PROTO3 || edition == TPD_EDITION_2023 || edition == TPD_EDITION_2024);
	C.is_proto3 = str_eq(f->syntax, "proto3") || (!str_eq(f->syntax, "proto2") && proto3_edition);
}

/* String.prototype.replace(from, to) with a string pattern: the first occurrence */
static char *js_replace_first(const char *s, const char *from, const char *to)
{
	const char *p = strstr(s, from);
	if (!p)
		return xstrdup(s);
	return xcat(xstrndup(s, (size_t)(p - s)), to, p + strlen(from));
}

/* ---- case.ts (ASCII case mapping: protobuf names are ASCII) ---- */

static char *capitalize(const char *s)
{
	char *r = xstrdup(s);
	if (r[0] >= 'a' && r[0] <= 'z')
		r[0] = (char)(r[0] - 32);
	return r;
}

static char *uncapitalize(const char *s)
{
	char *r = xstrdup(s);
	if (r[0] >= 'A' && r[0] <= 'Z')
		r[0] = (char)(r[0] + 32);
	return r;
}

static char *snake_to_camel(const char *s)
{
	bool has_lower = false;
	for (const char *p = s; *p; p++)
		has_lower |= *p >= 'a' && *p <= 'z';
	Buf b = {0};
	Vec words = str_split(s, '_');
	for (int i = 0; i < words.n; i++) {
		char *w = words.v[i];
		if (!has_lower)
			for (char *p = w; *p; p++)
				if (*p >= 'A' && *p <= 'Z')
					*p = (char)(*p + 32);
		buf_adds(&b, i == 0 ? w : capitalize(w));
	}
	return xstrdup(buf_str(&b));
}

static const char *maybe_snake_to_camel(const char *key)
{
	return O.snake_keys && strchr(key, '_') ? snake_to_camel(key) : key;
}

static bool is_lower_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }

static char *camel_to_snake(const char *s)
{
	Buf a = {0}, b = {0};
	size_t n = strlen(s);
	/* .replace(/[a-z0-9]([A-Z])/g, (m) => m[0] + "_" + m[1]) */
	for (size_t i = 0; i < n; i++) {
		if (i + 1 < n && is_lower_digit(s[i]) && is_upper(s[i + 1])) {
			buf_addc(&a, s[i]);
			buf_addc(&a, '_');
			buf_addc(&a, s[i + 1]);
			i++;
		} else {
			buf_addc(&a, s[i]);
		}
	}
	/* .replace(/[A-Z]([A-Z][a-z])/g, (m) => m[0] + "_" + m.substring(1)) */
	const char *t = buf_str(&a);
	n = a.n;
	for (size_t i = 0; i < n; i++) {
		if (i + 2 < n && is_upper(t[i]) && is_upper(t[i + 1]) && t[i + 2] >= 'a' && t[i + 2] <= 'z') {
			buf_addc(&b, t[i]);
			buf_addc(&b, '_');
			buf_addc(&b, t[i + 1]);
			buf_addc(&b, t[i + 2]);
			i += 2;
		} else {
			buf_addc(&b, t[i]);
		}
	}
	char *r = xstrdup(buf_str(&b));
	for (char *p = r; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 32);
	return r;
}

/* ---- sourceInfo.ts ---- */

typedef struct {
	Map *map;	    /* path "4.0.2.1" -> TpdLocation (last one wins) */
	const char *prefix; /* "4.0." for an opened element */
	TpdLocation *self;
} SourceInfo;

static TpdLocation empty_location;

static SourceInfo source_info_from(TpdFile *f)
{
	SourceInfo si = {xalloc(sizeof(Map)), "", &empty_location};
	if (f->has_source_code_info) {
		for (int i = 0; i < f->nlocation; i++) {
			TpdLocation *l = f->location[i];
			Buf k = {0};
			for (int j = 0; j < l->npath; j++)
				buf_fmt(&k, j ? ".%d" : "%d", (int)l->path[j]);
			map_put(si.map, xstrdup(buf_str(&k)), l);
		}
	}
	return si;
}

static TpdLocation *si_lookup(const SourceInfo *si, int type, int index)
{
	TpdLocation *l = map_get(si->map, xfmt(index < 0 ? "%s%d" : "%s%d.%d", si->prefix, type, index));
	return l ? l : &empty_location;
}

static SourceInfo si_open(const SourceInfo *si, int type, int index)
{
	SourceInfo r = {si->map, xfmt("%s%d.%d.", si->prefix, type, index), si_lookup(si, type, index)};
	return r;
}

enum { FILE_SYNTAX = 12, FILE_MESSAGE_TYPE = 4, FILE_ENUM_TYPE = 5, FILE_SERVICE = 6, FILE_EXTENSION = 7,
       MESSAGE_FIELD = 2, MESSAGE_NESTED_TYPE = 3, MESSAGE_ENUM_TYPE = 4, MESSAGE_ONEOF_DECL = 8, ENUM_VALUE = 2,
       SERVICE_METHOD = 2 };

/* ---- utils.ts ---- */

static bool is_js_space(const char *p, size_t n, size_t *w)
{
	unsigned char c = (unsigned char)p[0];
	if (c == ' ' || (c >= 9 && c <= 13)) {
		*w = 1;
		return true;
	}
	if (c < 0x80)
		return false;
	uint32_t r;
	int k = utf8_decode(p, n, &r);
	*w = (size_t)k;
	return r == 0xA0 || r == 0x1680 || (r >= 0x2000 && r <= 0x200A) || r == 0x2028 || r == 0x2029 || r == 0x202F ||
	       r == 0x205F || r == 0x3000 || r == 0xFEFF;
}

/* String.prototype.trim */
static Str js_trim(Str s)
{
	size_t w;
	while (s.n && is_js_space(s.p, s.n, &w)) {
		s.p += w;
		s.n -= w;
	}
	for (;;) {
		/* find the last code point */
		size_t i = s.n;
		if (!i)
			break;
		size_t j = i - 1;
		while (j > 0 && ((unsigned char)s.p[j] & 0xC0) == 0x80 && i - j < 4)
			j--;
		if (!is_js_space(s.p + j, s.n - j, &w) || j + w != s.n)
			break;
		s.n = j;
	}
	return s;
}

/* maybeAddComment: the JSDoc block for a description (NULL if none) */
static NodeComment *jsdoc_comment(const TpdLocation *desc, bool deprecated, const char *prefix)
{
	if (!O.comments)
		return NULL;
	Vec lines = {0};
	Str src = desc->leading_comments.n ? desc->leading_comments : desc->trailing_comments;
	if (src.n) {
		/* .replace(/\*\//g, "* /").trim() */
		Buf b = {0};
		for (size_t i = 0; i < src.n; i++) {
			if (src.p[i] == '*' && i + 1 < src.n && src.p[i + 1] == '/') {
				buf_adds(&b, "* /");
				i++;
			} else {
				buf_addc(&b, src.p[i]);
			}
		}
		Str content = js_trim(buf_view(&b));
		if (content.n && content.p[0] == '*')
			content = js_trim((Str){content.p + 1, content.n - 1});
		Buf c = {0};
		if (prefix && *prefix)
			buf_adds(&c, prefix);
		buf_add_str(&c, content);
		Str *parts;
		int np = str_split_s(buf_view(&c), '\n', &parts);
		for (int i = 0; i < np; i++) {
			Str l = parts[i];
			if (l.n && l.p[0] == ' ')
				l.p++, l.n--;
			vec_push(&lines, xstrndup(l.p, l.n));
		}
	}
	if (deprecated) {
		if (lines.n > 0)
			vec_push(&lines, "");
		vec_push(&lines, "@deprecated");
	}
	if (!lines.n)
		return NULL;
	Buf t = {0};
	if (lines.n == 1) {
		buf_fmt(&t, "/** %s */", (char *)lines.v[0]);
	} else {
		buf_adds(&t, "/**\n * ");
		for (int i = 0; i < lines.n; i++) {
			if (i)
				buf_adds(&t, "\n * ");
			buf_adds(&t, lines.v[i]);
		}
		buf_adds(&t, "\n */");
	}
	NodeComment *cm = xalloc(sizeof *cm);
	cm->text = buf_view(&t);
	cm->block = true;
	return cm;
}

static void add_comment(Node *n, const NodeComment *cm, int nl)
{
	if (cm)
		dts_comment(n, cm->text, true, nl);
}

static const char *maybe_prefix_package(TpdFile *f, const char *rest)
{
	return *f->package ? xcat(f->package, ".", rest) : rest;
}

static const char *get_field_json_name(const char *name, const char *json_name)
{
	if (O.use_json_name)
		return json_name;
	if (O.snake_json)
		return json_name;
	return str_eq(snake_to_camel(name), json_name) ? name : json_name;
}

static const char *get_field_name(const TpdField *f)
{
	if (O.use_json_name)
		return f->json_name;
	return maybe_snake_to_camel(f->name);
}

/* /^[a-zA-Z_$][\w$]*$/ */
static bool is_valid_identifier(const char *s)
{
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_' || *s == '$'))
		return false;
	for (s++; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' ||
		      *s == '$'))
			return false;
	return true;
}

/* safeAccessor: `bar` or `"bar"` as a property key */
static Node *safe_key(const char *name)
{
	return is_valid_identifier(name) ? dts_id(name) : str_raw(quote_json(str_c(name)));
}

/* getPropertyAccessor(objectName, propertyName, optional) as an expression */
static Node *prop_access(Node *obj, const char *name, bool optional)
{
	if (is_valid_identifier(name))
		return dts_opt_member(obj, name, optional);
	return dts_opt_index(obj, str_raw(quote_json(str_c(name))), optional);
}

static Node *prop(const char *obj, const char *name) { return prop_access(dts_id(obj), name, false); }

UNUSED static TpImport *imp_file(const char *spec) { return tp_imp(xcat(spec, O.import_suffix)); }

static TpImport *imp_proto(const char *module, const char *type)
{
	const char *prefix = opt_true(&O.opts, "onlyTypes") ? "t:" : "";
	const char *proto_file = xcat(module, ".proto");
	JsVal *m = js_obj_get(O.M, str_c(proto_file));
	if (m && js_truthy(m))
		return tp_imp(xcat(prefix, type, "@", js_value_to_string(m)));
	return tp_imp(xcat(prefix, type, "@./", module, O.file_suffix, O.import_suffix));
}

/* nullOrUndefined(options) in an expression and as a type */
static Node *null_or_undefined(void) { return O.use_null_as_optional ? dts_null() : UNDEF(); }
static Node *null_or_undefined_type(void) { return KW(O.use_null_as_optional ? "null" : "undefined"); }

/* `T | <nullOrUndefined(options, hasProto3Optional)>`: the union of the text ts-poet prints */
static Node *tunion2(Node *a, Node *b)
{
	Node *u = dts_new(N_TS_UNION_TYPE);
	Node *parts[2] = {a, b};
	for (int k = 0; k < 2; k++) {
		if (parts[k]->kind == N_TS_UNION_TYPE && !(parts[k]->hints & H_LEADING_OP))
			addl(&u->list, parts[k]->list);
		else
			dts_add(&u->list, parts[k]);
	}
	return u;
}

static Node *or_null_or_undefined(Node *type, bool has_proto3_optional)
{
	if (!O.use_null_as_optional)
		return tunion2(type, KW("undefined"));
	Node *u = tunion2(type, KW("null"));
	if (has_proto3_optional)
		u = tunion2(u, KW("undefined"));
	return u;
}

/* maybeCheckIsNotNull with prefix: ` && x !== null` appended to cond (nothing without useNullAsOptional) */
static Node *and_not_null(Node *cond, Node *x)
{
	return O.use_null_as_optional ? BIN(LOGICAL_AND, cond, BIN(NOTEQEQ, x, dts_null())) : cond;
}
static Node *or_not_null(Node *cond, Node *x)
{
	return O.use_null_as_optional ? BIN(LOGICAL_OR, cond, BIN(NOTEQEQ, x, dts_null())) : cond;
}
static Node *or_is_null(Node *cond, Node *x)
{
	return O.use_null_as_optional ? BIN(LOGICAL_OR, cond, BIN(EQEQEQ, x, dts_null())) : cond;
}

static const char *oneof_value_name(const char *field_name) { return O.oneof == ONEOF_UNIONS ? field_name : "value"; }

static const char *wrap_type_name(const char *name, bool is_enum)
{
	return xcat(O.type_prefix, is_enum ? O.enum_prefix : O.message_prefix, name, is_enum ? O.enum_suffix : O.message_suffix,
		    O.type_suffix);
}

/* FormattedMethodDescriptor.formatName */
UNUSED static const char *format_method_name(const char *name)
{
	if (opt_true(&O.opts, "lowerCaseServiceMethods") || opt_includes(&O.opts, "outputServices", "grpc-js"))
		if (opt_true(&O.opts, "snakeToCamel"))
			return ca_camel_case(name);
	return name;
}

/* ---- visit.ts ---- */

typedef void (*MessageVisitor)(void *arg, const char *full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name);
typedef void (*EnumVisitor)(void *arg, const char *full_name, TpdEnum *e, SourceInfo *si, const char *proto_full_name);

static const char *message_name(const char *name)
{
	return str_eq(name, "Date") || str_eq(name, "Function") ? xcat(name, "Message") : name;
}

static void visit_rec(TpdMessage **messages, int nmessages, TpdEnum **enums, int nenums, bool root, SourceInfo *si,
		      MessageVisitor mf, EnumVisitor ef, void *arg, const char *ts_prefix, const char *proto_prefix)
{
	int child_enum = root ? FILE_ENUM_TYPE : MESSAGE_ENUM_TYPE;
	for (int i = 0; i < nenums; i++) {
		TpdEnum *e = enums[i];
		const char *proto_full = xcat(proto_prefix, e->name);
		const char *ts_full = xcat(ts_prefix, maybe_snake_to_camel(e->name));
		SourceInfo nsi = si_open(si, child_enum, i);
		if (ef)
			ef(arg, message_name(wrap_type_name(ts_full, true)), e, &nsi, proto_full);
	}
	int child_type = root ? FILE_MESSAGE_TYPE : MESSAGE_NESTED_TYPE;
	for (int i = 0; i < nmessages; i++) {
		TpdMessage *m = messages[i];
		const char *proto_full = xcat(proto_prefix, m->name);
		const char *ts_full = xcat(ts_prefix, maybe_snake_to_camel(m->name));
		SourceInfo nsi = si_open(si, child_type, i);
		mf(arg, message_name(wrap_type_name(ts_full, false)), m, &nsi, proto_full);
		visit_rec(m->nested_type, m->nnested_type, m->enum_type, m->nenum_type, false, &nsi, mf, ef, arg,
			  xcat(ts_full, O.use_snake_type_name ? "_" : ""), xcat(proto_full, "."));
	}
}

static void visit(TpdFile *f, SourceInfo *si, MessageVisitor mf, EnumVisitor ef, void *arg)
{
	visit_rec(f->message_type, f->nmessage_type, f->enum_type, f->nenum_type, true, si, mf, ef, arg, "", "");
}

/* the text of a simple expression (an accessor ts-proto has as a string) */
static void node_text_buf(Buf *b, const Node *n)
{
	switch (n->kind) {
	case N_IDENT:
	case N_STR:
	case N_NUMBER:
		buf_add_str(b, n->text);
		break;
	case N_OPT_CHAIN_EXPR:
		node_text_buf(b, n->a);
		break;
	case N_MEMBER_EXPR:
		node_text_buf(b, n->a);
		if (n->b->kind == N_COMPUTED_PROP_NAME) {
			buf_addc(b, '[');
			node_text_buf(b, n->b->a);
			buf_addc(b, ']');
		} else {
			buf_addc(b, '.');
			node_text_buf(b, n->b);
		}
		break;
	case N_TS_NON_NULL_EXPR:
		node_text_buf(b, n->a);
		buf_addc(b, '!');
		break;
	case N_CALL_EXPR:
		node_text_buf(b, n->a);
		buf_addc(b, '(');
		for (int i = 0; i < n->list.n; i++) {
			if (i)
				buf_adds(b, ", ");
			node_text_buf(b, n->list.v[i]->kind == N_EXPR_OR_SPREAD ? n->list.v[i]->a : n->list.v[i]);
		}
		buf_addc(b, ')');
		break;
	default:
		fail("node_text: unexpected node %d", n->kind);
	}
}

static const char *node_text(const Node *n)
{
	Buf b = {0};
	node_text_buf(&b, n);
	return xstrdup(buf_str(&b));
}
