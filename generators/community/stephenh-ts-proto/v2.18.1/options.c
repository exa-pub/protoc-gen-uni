/* Port of ts-proto v2.18.1 src/options.ts: defaultOptions, optionsFromParameter, parseParameter,
 * parseParamValue and getTsPoetOpts.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License.
 *
 * Upstream parses the parameter into a plain object whose values are strings, booleans or (for a
 * repeated key) nested arrays, and compares them where they are used (`options.useOptionals ===
 * "messages"`, truthiness, ...). The options keep these JavaScript values (JsVal) so that every
 * comparison sees what upstream sees, also for unexpected values. */

typedef struct {
	JsVal *o; /* the options object (defaultOptions() + parsed parameter, normalized) */
} Options;

static JsVal *opt(const Options *o, const char *key)
{
	JsVal *v = js_obj_get(o->o, str_c(key));
	return v ? v : js_undefined();
}

/* JavaScript truthiness of an option value */
static bool js_truthy(const JsVal *v)
{
	switch (v->t) {
	case JS_UNDEFINED:
	case JS_NULL:
		return false;
	case JS_BOOL:
		return v->b;
	case JS_NUMBER:
		return v->num == v->num && v->num != 0;
	case JS_STRING:
	case JS_BIGINT:
		return v->s.n > 0 && !(v->t == JS_BIGINT && str_eqc(v->s, "0"));
	default:
		return true;
	}
}

static bool opt_true(const Options *o, const char *key) { return js_truthy(opt(o, key)); }

/* options.<key> === true */
static bool opt_is_true(const Options *o, const char *key)
{
	JsVal *v = opt(o, key);
	return v->t == JS_BOOL && v->b;
}

/* options.<key> === "<s>" */
static bool opt_is(const Options *o, const char *key, const char *s)
{
	JsVal *v = opt(o, key);
	return v->t == JS_STRING && str_eqc(v->s, s);
}

/* options.<key>.includes(s): Array.prototype.includes (strict equality) or, for a string,
 * String.prototype.includes (substring) */
static bool opt_includes(const Options *o, const char *key, const char *s)
{
	JsVal *v = opt(o, key);
	if (v->t == JS_STRING) {
		size_t n = strlen(s);
		for (size_t i = 0; i + n <= v->s.n; i++)
			if (!memcmp(v->s.p + i, s, n))
				return true;
		return false;
	}
	if (v->t != JS_ARRAY)
		fail("TypeError: options.%s.includes is not a function", key);
	for (int i = 0; i < v->items.n; i++) {
		JsVal *x = v->items.v[i];
		if (x->t == JS_STRING && str_eqc(x->s, s))
			return true;
	}
	return false;
}

static void set(JsVal *o, const char *key, JsVal *v) { js_obj_set(o, str_c(key), v); }
static JsVal *jstr(const char *s) { return js_string(str_c(s)); }

static JsVal *jarr(const char *first, ...)
{
	JsVal *a = js_array();
	va_list ap;
	va_start(ap, first);
	for (const char *s = first; s; s = va_arg(ap, const char *))
		vec_push(&a->items, jstr(s));
	va_end(ap);
	return a;
}

COLD static JsVal *default_options(void)
{
	JsVal *o = js_object();
	set(o, "context", js_bool(false));
	set(o, "useContextDataloaders", js_bool(true));
	set(o, "snakeToCamel", jarr("json", "keys", NULL));
	set(o, "protoJsonFormat", js_bool(true));
	set(o, "emitDefaultValues", js_array());
	set(o, "globalThisPolyfill", js_bool(false));
	set(o, "forceLong", jstr("number"));
	set(o, "useJsTypeOverride", js_bool(false));
	set(o, "useOptionals", jstr("none"));
	set(o, "useDate", jstr("date"));
	set(o, "useJsonTimestamp", jstr("rfc3339"));
	set(o, "useMongoObjectId", js_bool(false));
	set(o, "oneof", jstr("properties"));
	set(o, "esModuleInterop", js_bool(false));
	set(o, "fileSuffix", jstr(""));
	set(o, "importSuffix", jstr(""));
	set(o, "lowerCaseServiceMethods", js_bool(false));
	set(o, "outputEncodeMethods", js_bool(true));
	set(o, "outputEncodeIncludeTypes", jstr(""));
	set(o, "outputDecodeIncludeTypes", jstr(""));
	set(o, "outputJsonMethods", js_bool(true));
	set(o, "outputPartialMethods", js_bool(true));
	set(o, "outputTypeAnnotations", js_bool(false));
	set(o, "outputTypeRegistry", js_bool(false));
	set(o, "stringEnums", js_bool(false));
	set(o, "constEnums", js_bool(false));
	set(o, "removeEnumPrefix", js_bool(false));
	set(o, "enumsAsLiterals", js_bool(false));
	set(o, "outputClientImpl", js_bool(true));
	set(o, "outputServices", js_array());
	set(o, "returnObservable", js_bool(false));
	set(o, "addGrpcMetadata", js_bool(false));
	set(o, "metadataType", js_undefined());
	set(o, "addNestjsRestParameter", js_bool(false));
	set(o, "useNestjsFqnServiceName", js_bool(false));
	set(o, "nestJs", js_bool(false));
	set(o, "env", jstr("both"));
	set(o, "unrecognizedEnum", js_bool(true));
	set(o, "unrecognizedEnumName", jstr("UNRECOGNIZED"));
	set(o, "unrecognizedEnumValue", js_number(-1));
	set(o, "exportCommonSymbols", js_bool(true));
	set(o, "outputSchema", js_bool(false));
	set(o, "onlyTypes", js_bool(false));
	set(o, "emitImportedFiles", js_bool(true));
	set(o, "useExactTypes", js_bool(true));
	set(o, "useAbortSignal", js_bool(false));
	set(o, "useAsyncIterable", js_bool(false));
	set(o, "unknownFields", js_bool(false));
	set(o, "usePrototypeForDefaults", js_bool(false));
	set(o, "useJsonName", js_bool(false));
	set(o, "useJsonWireFormat", js_bool(false));
	set(o, "useNumericEnumForJson", js_bool(false));
	set(o, "initializeFieldsAsUndefined", js_bool(true));
	set(o, "useMapType", js_bool(false));
	set(o, "useReadonlyTypes", js_bool(false));
	set(o, "useUnknownForValue", js_bool(false));
	set(o, "useSnakeTypeName", js_bool(true));
	set(o, "outputExtensions", js_bool(false));
	set(o, "outputIndex", js_bool(false));
	set(o, "M", js_object());
	set(o, "rpcBeforeRequest", js_bool(false));
	set(o, "rpcAfterResponse", js_bool(false));
	set(o, "rpcErrorHandler", js_bool(false));
	set(o, "comments", js_bool(true));
	set(o, "disableProto2Optionals", js_bool(false));
	set(o, "disableProto2DefaultValues", js_bool(false));
	set(o, "useNullAsOptional", js_bool(false));
	set(o, "annotateFilesWithVersion", js_bool(true));
	set(o, "noDefaultsForOptionals", js_bool(false));
	set(o, "bigIntLiteral", js_bool(true));
	set(o, "typePrefix", jstr(""));
	set(o, "typeSuffix", jstr(""));
	set(o, "messagePrefix", jstr(""));
	set(o, "messageSuffix", jstr(""));
	set(o, "enumPrefix", jstr(""));
	set(o, "enumSuffix", jstr(""));
	set(o, "useDuration", jstr("duration"));
	set(o, "anyType", jstr("@type"));
	return o;
}

/* parseParamValue */
static JsVal *parse_param_value(Str v)
{
	if (str_eqc(v, "true"))
		return js_bool(true);
	if (str_eqc(v, "false"))
		return js_bool(false);
	return js_string(v);
}

/* Properties every plain object inherits from Object.prototype: `options[key]` is truthy for them
 * although parseParameter never set them. */
static bool is_object_prototype_key(Str k)
{
	static const char *const keys[] = {"constructor", "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable",
					   "toLocaleString", "toString", "valueOf", "__defineGetter__",
					   "__defineSetter__", "__lookupGetter__", "__lookupSetter__", "__proto__"};
	for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
		if (str_eqc(k, keys[i]))
			return true;
	return false;
}

/* parseParameter: "a very naive parse function" (warnings go to stderr upstream) */
COLD static JsVal *parse_parameter(Str parameter)
{
	JsVal *options = js_object();
	JsVal *M = js_object();
	set(options, "M", M);
	Str *parts;
	int n = str_split_s(parameter, ',', &parts);
	for (int i = 0; i < n; i++) {
		Str param = parts[i];
		/* indexOf("=") is -1 without "=": substring(0, -1) is "" and the value is the whole param */
		const char *eq = memchr(param.p, '=', param.n);
		Str key = eq ? (Str){param.p, (size_t)(eq - param.p)} : (Str){param.p, 0};
		Str raw = eq ? (Str){eq + 1, param.n - (size_t)(eq + 1 - param.p)} : param;
		JsVal *value = parse_param_value(raw);
		if (key.n > 0 && key.p[0] == 'M') {
			if (value->t == JS_STRING)
				js_obj_set(M, (Str){key.p + 1, key.n - 1}, value);
			continue;
		}
		if (str_eqc(key, "__proto__"))
			continue; /* assigning a primitive to __proto__ does nothing */
		JsVal *cur = js_obj_get(options, key);
		if ((cur && js_truthy(cur)) || (!cur && is_object_prototype_key(key))) {
			JsVal *a = js_array();
			vec_push(&a->items, cur ? cur : js_undefined());
			vec_push(&a->items, value);
			js_obj_set(options, key, a);
		} else {
			js_obj_set(options, key, value);
		}
	}
	return options;
}

/* Object.assign(dst, src) */
static void assign(JsVal *dst, const JsVal *src)
{
	Str *keys;
	int n = js_obj_keys(src, &keys);
	for (int i = 0; i < n; i++)
		js_obj_set(dst, keys[i], js_obj_get(src, keys[i]));
}

/* ECMAScript ToNumber of a string (StringToNumber): white space trimmed, "" is 0, decimal
 * literals, Infinity, 0x/0o/0b integers; anything else is NaN. */
static double js_string_to_number(Str s)
{
	size_t lo = 0, hi = s.n;
	while (lo < hi && strchr(" \t\n\v\f\r", s.p[lo]))
		lo++;
	while (hi > lo && strchr(" \t\n\v\f\r", s.p[hi - 1]))
		hi--;
	if (lo == hi)
		return 0;
	char *t = xstrndup(s.p + lo, hi - lo);
	size_t n = hi - lo;
	if (n > 2 && t[0] == '0' && strchr("xXoObB", t[1])) {
		int base = (t[1] == 'x' || t[1] == 'X') ? 16 : (t[1] == 'o' || t[1] == 'O') ? 8 : 2;
		double v = 0;
		for (size_t i = 2; i < n; i++) {
			int d = isdigit((unsigned char)t[i]) ? t[i] - '0' : isalpha((unsigned char)t[i]) ? (tolower((unsigned char)t[i]) - 'a' + 10) : 99;
			if (d >= base)
				return NAN;
			v = v * base + d;
		}
		return v;
	}
	const char *p = t;
	bool neg = false;
	if (*p == '+' || *p == '-')
		neg = *p++ == '-';
	if (!strcmp(p, "Infinity"))
		return neg ? -INFINITY : INFINITY;
	/* StrUnsignedDecimalLiteral: digits [. digits] [e[+-]digits] */
	const char *q = p;
	bool digits = false;
	while (isdigit((unsigned char)*q))
		q++, digits = true;
	if (*q == '.') {
		q++;
		while (isdigit((unsigned char)*q))
			q++, digits = true;
	}
	if (!digits)
		return NAN;
	if (*q == 'e' || *q == 'E') {
		const char *e = q + 1;
		if (*e == '+' || *e == '-')
			e++;
		if (!isdigit((unsigned char)*e))
			return NAN;
		while (isdigit((unsigned char)*e))
			e++;
		q = e;
	}
	if (*q)
		return NAN;
	return strtod(t, NULL);
}

/* Number(v) */
static double js_to_number(const JsVal *v)
{
	switch (v->t) {
	case JS_UNDEFINED:
		return NAN;
	case JS_NULL:
		return 0;
	case JS_BOOL:
		return v->b ? 1 : 0;
	case JS_NUMBER:
		return v->num;
	case JS_STRING:
		return js_string_to_number(v->s);
	case JS_ARRAY:
		/* ToPrimitive: Array#join(",") */
		if (v->items.n == 0)
			return 0;
		if (v->items.n == 1) {
			const JsVal *x = v->items.v[0];
			return x->t == JS_STRING ? js_string_to_number(x->s) : NAN;
		}
		return NAN;
	default:
		return NAN;
	}
}

/* optionsFromParameter */
COLD static Options options_from_parameter(Str parameter)
{
	JsVal *o = default_options();
	if (parameter.n) {
		JsVal *parsed = parse_parameter(parameter);
		JsVal *nest = js_obj_get(parsed, STR_LIT("nestJs"));
		if (nest && js_truthy(nest)) {
			set(o, "lowerCaseServiceMethods", js_bool(true));
			set(o, "outputEncodeMethods", js_bool(false));
			set(o, "outputJsonMethods", js_bool(false));
			set(o, "outputPartialMethods", js_bool(false));
			set(o, "outputClientImpl", js_bool(false));
			set(o, "useDate", jstr("timestamp"));
		}
		assign(o, parsed);
	}
	Options r = {o};
	Options *op = &r;

	if (opt_true(op, "onlyTypes")) {
		set(o, "outputJsonMethods", js_bool(false));
		set(o, "outputEncodeMethods", js_bool(false));
		set(o, "outputClientImpl", js_bool(false));
		set(o, "nestJs", js_bool(false));
	} else if (!opt_true(op, "outputJsonMethods") && !opt_true(op, "outputEncodeMethods") &&
		   !opt_true(op, "outputClientImpl") && !opt_true(op, "nestJs")) {
		set(o, "onlyTypes", js_bool(true));
	}

	if (opt_is_true(op, "forceLong"))
		set(o, "forceLong", jstr("long"));

	JsVal *svc = opt(op, "outputServices");
	if (svc->t == JS_BOOL && !svc->b)
		set(o, "outputServices", jarr("none", NULL));
	svc = opt(op, "outputServices");
	if (svc->t == JS_STRING) {
		JsVal *a = js_array();
		vec_push(&a->items, svc);
		set(o, "outputServices", a);
	}
	/* outputServices.length (undefined for booleans: `undefined == 0` is false) */
	svc = opt(op, "outputServices");
	if (svc->t == JS_ARRAY && svc->items.n == 0 && !opt_true(op, "nestJs"))
		set(o, "outputServices", jarr("default", NULL));
	svc = opt(op, "outputServices");
	if (opt_true(op, "nestJs") && svc->t == JS_ARRAY && svc->items.n > 0)
		set(o, "outputEncodeMethods", js_bool(true));

	if (opt_is_true(op, "outputSchema"))
		set(o, "outputSchema", js_array());
	JsVal *schema = opt(op, "outputSchema");
	if (schema->t == JS_STRING) {
		JsVal *a = js_array();
		vec_push(&a->items, schema);
		set(o, "outputSchema", a);
	}

	JsVal *ud = opt(op, "useDate");
	if (ud->t == JS_BOOL)
		set(o, "useDate", jstr(ud->b ? "date" : "timestamp"));

	JsVal *stc = opt(op, "snakeToCamel");
	if (stc->t == JS_BOOL && !stc->b) {
		set(o, "snakeToCamel", js_array());
	} else if (stc->t == JS_BOOL) {
		set(o, "snakeToCamel", jarr("keys", "json", NULL));
	} else if (stc->t == JS_STRING) {
		JsVal *a = js_array();
		Str *ps;
		int np = str_split_s(stc->s, '_', &ps);
		for (int i = 0; i < np; i++)
			vec_push(&a->items, js_string(ps[i]));
		set(o, "snakeToCamel", a);
	}
	if (opt_true(op, "protoJsonFormat")) {
		stc = opt(op, "snakeToCamel");
		if (stc->t != JS_ARRAY)
			fail("TypeError: options.snakeToCamel.includes is not a function");
		if (!opt_includes(op, "snakeToCamel", "json"))
			vec_push(&stc->items, jstr("json"));
	}

	set(o, "emitDefaultValues", opt_is(op, "emitDefaultValues", "json-methods") ? jarr("json-methods", NULL) : js_array());

	if (opt_true(op, "useJsonWireFormat")) {
		if (!opt_true(op, "onlyTypes")) {
			set(o, "useJsonWireFormat", js_bool(false));
		} else {
			set(o, "stringEnums", js_bool(true));
			set(o, "useDate", jstr("string"));
		}
	}

	if (opt_true(op, "nestJs"))
		set(o, "initializeFieldsAsUndefined", js_bool(false));
	else
		set(o, "useNestjsFqnServiceName", js_bool(false));

	if (opt_true(op, "outputIndex"))
		set(o, "exportCommonSymbols", js_bool(false));

	if (opt_true(op, "rpcBeforeRequest") || opt_true(op, "rpcAfterResponse") || opt_true(op, "rpcErrorHandler")) {
		bool generic = opt_includes(op, "outputServices", "generic-definitions");
		set(o, "outputServices", generic ? jarr("default", "generic-definitions", NULL) : jarr("default", NULL));
	}

	JsVal *uev = opt(op, "unrecognizedEnumValue");
	if (js_truthy(uev))
		set(o, "unrecognizedEnumValue", js_number(js_to_number(uev)));
	return r;
}

/* String(v) for an option value used in a template */
static char *js_value_to_string(const JsVal *v)
{
	switch (v->t) {
	case JS_UNDEFINED:
		return "undefined";
	case JS_NULL:
		return "null";
	case JS_BOOL:
		return v->b ? "true" : "false";
	case JS_NUMBER:
		return (char *)js_number_to_string(v->num);
	case JS_STRING:
	case JS_BIGINT:
		return xstrndup(v->s.p, v->s.n);
	case JS_ARRAY: {
		/* Array#join(","): null and undefined elements are empty */
		Buf b = {0};
		for (int i = 0; i < v->items.n; i++) {
			const JsVal *x = v->items.v[i];
			if (i)
				buf_addc(&b, ',');
			if (x->t != JS_UNDEFINED && x->t != JS_NULL)
				buf_adds(&b, js_value_to_string(x));
		}
		return xstrdup(buf_str(&b));
	}
	default:
		return "[object Object]";
	}
}
