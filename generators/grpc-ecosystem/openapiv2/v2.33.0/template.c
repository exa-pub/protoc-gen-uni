// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
// github.com/grpc-ecosystem/grpc-gateway).

/* Port of protoc-gen-openapiv2/internal/genopenapi/template.go and helpers.go of grpc-gateway
 * v2.33.0, in the order of the Go file. Included by generator.c. Errors fail(); a Go panic fails
 * with go_panic (the port does not reproduce the crash of upstream). */

/* A Go panic of upstream: the plugin crashes (exit status 2, a goroutine trace on stderr). */
static NORETURN COLD void go_panic(const char *what)
{
	fail("unsupported by protoc-gen-uni: protoc-gen-openapiv2 panics here (%s)", what);
}

/* The recursion over message references runs on the C stack (Go's grows): a chain of more
 * distinct messages than this is not supported. */
enum { MAX_MESSAGE_DEPTH = 2000 };

static void check_depth(int depth)
{
	if (depth > MAX_MESSAGE_DEPTH)
		fail("unsupported by protoc-gen-uni: a chain of more than %d messages referencing each other", MAX_MESSAGE_DEPTH);
}

static const char *const pathParamUniqueSuffixDeliminator = "_";

/* map[string]struct{} / map[string]bool of names (any bytes) */
typedef struct StrSet StrSet;
#define paragraphDeliminator "\n\n"

/* ---- descriptor helpers (the getters of descriptorpb and the registry types) ---- */

enum {
	FILE_OPTIONS_DEPRECATED = 23,
	MESSAGE_OPTIONS_MAP_ENTRY = 7,
	METHOD_OPTIONS_DEPRECATED = 33,
	FIELD_OPTIONS_DEPRECATED = 3,
};

/* deprecateFieldsAndMethods sets Options.Deprecated of fields and methods: kept here by
 * descriptor proto (the descriptors are not changed). */
static Map deprecatedSet;

static const char *ptr_key(const void *p) { return xfmt("%llx", (unsigned long long)(uintptr_t)p); }

static bool field_deprecated(const GwField *f)
{
	return map_has(&deprecatedSet, ptr_key(f->proto)) || pb_int(pb_msg(f->proto, FD_OPTIONS), FIELD_OPTIONS_DEPRECATED, 0);
}

static bool method_deprecated(const GwMethod *m)
{
	return map_has(&deprecatedSet, ptr_key(m->proto)) || pb_int(pb_msg(m->proto, MDP_OPTIONS), METHOD_OPTIONS_DEPRECATED, 0);
}

static bool is_map_entry(const GwMessage *m) { return pb_int(pb_msg(m->proto, DP_OPTIONS), MESSAGE_OPTIONS_MAP_ENTRY, 0) != 0; }

static const char *message_fqmn(const GwMessage *m) { return gw_Message_FQMN(m); }

/* location of a type name relative to a message: its FQMN without the last component */
static const char *location_of(const GwMessage *m)
{
	const char *fqmn = message_fqmn(m);
	const char *dot = strrchr(fqmn, '.');
	return dot && dot > fqmn ? xstrndup(fqmn, (size_t)(dot - fqmn)) : "";
}

static GwMessage *lookupMsg(GwRegistry *reg, const char *location, const char *name)
{
	const char *err = NULL;
	return gw_Registry_LookupMsg(reg, location, name, &err);
}

static GwEnum *lookupEnum(GwRegistry *reg, const char *location, const char *name)
{
	const char *err = NULL;
	return gw_Registry_LookupEnum(reg, location, name, &err);
}

/* ---- the extensions linked into the plugin (proto.GetExtension, decoded once per message) ---- */

static Map optionCache; /* options message + kind -> decoded extension (or the "absent" mark) */
static Map requiredCache; /* JSONSchema message -> GoStrs * (jsonschema_required) */
static Map locationIndexes; /* file proto -> Map (location_by_path) */
typedef struct jsonNameMaps jsonNameMaps;
static jsonNameMaps *jsonNameMapsCache; /* json_name_maps */

/* Resets the caches above (arena memory) for a request. */
static void reset_caches(void)
{
	optionCache = (Map){0};
	requiredCache = (Map){0};
	locationIndexes = (Map){0};
	jsonNameMapsCache = NULL;
}
static char absentMark;

static const PbMsg *openapi_option(const PbMsg *opts, int kind)
{
	if (!opts)
		return NULL;
	const char *key = xfmt("%llx/%d", (unsigned long long)(uintptr_t)opts, kind);
	void *v = map_get(&optionCache, key);
	if (v)
		return v == &absentMark ? NULL : v;
	bool present;
	Str payload = genproto_ext_message(opts, GW_OA_EXTENSION, &present);
	const PbMsg *m = NULL;
	if (present) {
		const char *err;
		m = gw_options_decode(kind, payload, &err);
		if (!m)
			fail("%s", err); /* checked when the request was parsed */
	}
	map_put(&optionCache, key, m ? (void *)m : &absentMark);
	return m;
}

typedef struct {
	bool present;
	Str Restriction;
} VisibilityRule;

/* proto.GetExtension of a google.api.VisibilityRule (all occurrences merged: the last
 * restriction wins) */
static VisibilityRule visibility_option(const PbMsg *opts)
{
	VisibilityRule r = {0};
	if (!opts)
		return r;
	Str payload = genproto_ext_message(opts, GENPROTO_E_VISIBILITY, &r.present);
	PbReader rd = {(const uint8_t *)payload.p, (const uint8_t *)payload.p + payload.n};
	PbRawField f;
	while (pb_next_field(&rd, &f))
		if (f.num == GENPROTO_VisibilityRule_restriction && f.wt == WT_LEN)
			r.Restriction = (Str){(const char *)f.val, f.vlen};
	return r;
}

/* The required list of a JSONSchema option: a []string of the decoded message that the code
 * below changes in place (as upstream does to the option message), with the capacity
 * protobuf-go's decoder gives it (appended one by one). */
static GoStrs *jsonschema_required(const PbMsg *j)
{
	if (!j)
		return NULL;
	const char *key = ptr_key(j);
	GoStrs *r = map_get(&requiredCache, key);
	if (!r) {
		r = xalloc(sizeof *r);
		for (int i = 0; i < pb_count(j, OA_JSONSchema_required); i++)
			*r = strs_append1(*r, pb_bytes_at(j, OA_JSONSchema_required, i));
		map_put(&requiredCache, key, r);
	}
	return r;
}

/* repeated string of an option message as a new []string (nil if empty) */
static GoStrs pb_strs(const PbMsg *m, uint32_t num)
{
	GoStrs s = {0};
	for (int i = 0; i < pb_count(m, num); i++)
		s = strs_append1(s, pb_bytes_at(m, num, i));
	return s;
}

static Str S(const char *s) { return str_c(s); }

static Str cat(Str a, Str b)
{
	Buf o = {0};
	buf_add_str(&o, a);
	buf_add_str(&o, b);
	return (Str){o.p ? o.p : "", o.n};
}

static Str cat3(Str a, Str b, Str c) { return cat(cat(a, b), c); }

static Str go_quote(Str s) { return str_c(go_std_quote(s.p, s.n, NULL)); }

/* strings.TrimSpace */
static Str trim_space(Str s) { return go_trim_space(s); }

/* ---- wktSchemas ---- */

typedef struct {
	const char *name;
	const char *type, *format;
	bool items;
} wkt;

static const wkt wktSchemas[] = {
	{".google.protobuf.FieldMask", "string", "", false},
	{".google.protobuf.Timestamp", "string", "date-time", false},
	{".google.protobuf.Duration", "string", "", false},
	{".google.protobuf.StringValue", "string", "", false},
	{".google.protobuf.BytesValue", "string", "byte", false},
	{".google.protobuf.Int32Value", "integer", "int32", false},
	{".google.protobuf.UInt32Value", "integer", "int64", false},
	{".google.protobuf.Int64Value", "string", "int64", false},
	{".google.protobuf.UInt64Value", "string", "uint64", false},
	{".google.protobuf.FloatValue", "number", "float", false},
	{".google.protobuf.DoubleValue", "number", "double", false},
	{".google.protobuf.BoolValue", "boolean", "", false},
	{".google.protobuf.Empty", "object", "", false},
	{".google.protobuf.Struct", "object", "", false},
	{".google.protobuf.Value", "", "", false},
	/* A ListValue element is any JSON value, like google.protobuf.Value, so leave the items
	 * schema unconstrained. */
	{".google.protobuf.ListValue", "array", "", true},
	{".google.protobuf.NullValue", "string", "", false},
};

/* wktSchemas[name]: true and the schema core if present */
static bool wktSchema(const char *name, schemaCore *core)
{
	for (size_t i = 0; i < sizeof wktSchemas / sizeof *wktSchemas; i++)
		if (strcmp(name, wktSchemas[i].name) == 0) {
			if (core) {
				*core = (schemaCore){0};
				core->Type = S(wktSchemas[i].type);
				core->Format = S(wktSchemas[i].format);
				if (wktSchemas[i].items)
					core->Items = xalloc(sizeof *core->Items);
			}
			return true;
		}
	return false;
}

/* ---- enums ---- */

static bool isVisible(VisibilityRule r, const GwRegistry *reg);

static VisibilityRule getEnumValueVisibilityOption(const PbMsg *value) { return visibility_option(pb_msg(value, EVDP_OPTIONS)); }

static Any listEnumNames(const GwRegistry *reg, const GwEnum *e)
{
	GoStrs names = {0};
	for (int i = 0; i < pb_count(e->proto, EDP_VALUE); i++) {
		const PbMsg *value = pb_msg_at(e->proto, EDP_VALUE, i);
		if (!isVisible(getEnumValueVisibilityOption(value), reg))
			continue;
		if (reg->omitEnumDefaultValue && pb_int(value, EVDP_NUMBER, 0) == 0)
			continue;
		names = strs_append1(names, pb_bytes(value, EVDP_NAME));
	}
	if (names.len > 0)
		return (Any){.kind = ANY_STRINGS, .strs = names};
	return (Any){0};
}

static Any listEnumNumbers(const GwRegistry *reg, const GwEnum *e)
{
	int n = pb_count(e->proto, EDP_VALUE), k = 0;
	long long *numbers = xalloc(sizeof *numbers * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		const PbMsg *value = pb_msg_at(e->proto, EDP_VALUE, i);
		if (reg->omitEnumDefaultValue && pb_int(value, EVDP_NUMBER, 0) == 0)
			continue;
		if (!isVisible(getEnumValueVisibilityOption(value), reg))
			continue;
		numbers[k++] = (int32_t)pb_int(value, EVDP_NUMBER, 0);
	}
	if (k > 0)
		return (Any){.kind = ANY_INTS, .ints = numbers, .nints = k};
	return (Any){0};
}

static Any getEnumDefault(const GwRegistry *reg, const GwEnum *e)
{
	if (!reg->omitEnumDefaultValue) {
		for (int i = 0; i < pb_count(e->proto, EDP_VALUE); i++) {
			const PbMsg *value = pb_msg_at(e->proto, EDP_VALUE, i);
			if (pb_int(value, EVDP_NUMBER, 0) == 0) {
				if (!isVisible(getEnumValueVisibilityOption(value), reg))
					return (Any){0};
				return (Any){.kind = ANY_STRING, .s = pb_bytes(value, EVDP_NAME)};
			}
		}
	}
	return (Any){0};
}

static Any getEnumDefaultNumber(const GwRegistry *reg, const GwEnum *e)
{
	if (!reg->omitEnumDefaultValue) {
		for (int i = 0; i < pb_count(e->proto, EDP_VALUE); i++)
			if (pb_int(pb_msg_at(e->proto, EDP_VALUE, i), EVDP_NUMBER, 0) == 0)
				return (Any){.kind = ANY_INT, .i = 0};
	}
	return (Any){0};
}

/* ---- []descriptor.Parameter with Go's slice semantics (40-byte elements) ---- */

typedef struct {
	GwParameter *v;
	int len, cap;
} Params;

static Params params_append(Params s, GwParameter x)
{
	if (s.len == s.cap) {
		int cap = go_newcap(s.cap, s.len + 1, 40);
		GwParameter *v = xalloc(sizeof *v * (size_t)cap);
		if (s.len)
			mcopy(v, s.v, sizeof *v * (size_t)s.len);
		s.v = v;
		s.cap = cap;
	}
	s.v[s.len++] = x;
	return s;
}

/* The PathParams of a binding as the registry built them: appended one by one. */
static Params binding_params(const GwBinding *b)
{
	Params p = {0};
	for (int i = 0; i < b->nPathParams; i++)
		p = params_append(p, b->PathParams[i]);
	return p;
}

/* append(a[:i], a[i+1:]...): removes element i in place */
static Params params_remove(Params s, int i)
{
	mcopy(&s.v[i], &s.v[i + 1], sizeof *s.v * (size_t)(s.len - i - 1));
	s.len--;
	return s;
}

static openapiSchemaObject schemaOfField(GwField *f, GwRegistry *reg, StrSet *refs);
static Str fieldProtoComments(GwRegistry *reg, GwMessage *msg, GwField *field);
static Str enumValueProtoComments(GwRegistry *reg, GwEnum *e);
static Str mergeDescription(const openapiSchemaObject *schema);
static const PbMsg *getFieldOpenAPIOption(GwRegistry *reg, GwField *f);
static const PbMsg *getFieldConfiguration(GwRegistry *reg, GwField *f);
static VisibilityRule getFieldVisibilityOption(const GwField *f);
static bool primitiveSchema(int t, const char **ftype, const char **format);

/* updateOpenAPIDataFromComments: the fields reflection finds on the object (Summary or Title,
 * Description, ReadOnly). */
typedef struct {
	Str *summary;
	bool usingTitle;
	Str *description;
	bool *readOnly;
} commentTarget;

static void updateOpenAPIDataFromComments(GwRegistry *reg, commentTarget t, Str comment, bool isPackageObject);

static commentTarget schemaTarget(openapiSchemaObject *s) { return (commentTarget){&s->Title, true, &s->Description, &s->ReadOnly}; }

/* ---- query parameters ---- */

typedef struct {
	const char **names;
	int *counts;
	int n;
	int count;
	int depth; /* not upstream: the nesting of nestedQueryParams */
} cycleChecker;

/* Work done by nestedQueryParams: recursive messages make it exponential upstream (it does not
 * finish); the port stops. */
static long queryParamsWork;

static cycleChecker *newCycleChecker(int recursive)
{
	cycleChecker *c = xalloc(sizeof *c);
	c->count = recursive;
	return c;
}

/* Check returns whether name is still within recursion toleration */
static bool cycleChecker_Check(cycleChecker *c, const char *name)
{
	int i = 0;
	while (i < c->n && strcmp(c->names[i], name) != 0)
		i++;
	int count = (i < c->n ? c->counts[i] : 0) + 1;
	if (count > c->count)
		return false;
	if (i == c->n) {
		const char **names = xalloc(sizeof *names * (size_t)(c->n + 1));
		int *counts = xalloc(sizeof *counts * (size_t)(c->n + 1));
		mcopy(names, c->names, sizeof *names * (size_t)c->n);
		mcopy(counts, c->counts, sizeof *counts * (size_t)c->n);
		names[c->n] = name;
		counts[c->n] = 1;
		c->names = names;
		c->counts = counts;
		c->n++;
		return true;
	}
	c->counts[i] = count;
	return true;
}

static cycleChecker *cycleChecker_Branch(const cycleChecker *c)
{
	cycleChecker *copy = xalloc(sizeof *copy);
	*copy = *c;
	copy->names = xalloc(sizeof *copy->names * (size_t)(c->n + 1));
	copy->counts = xalloc(sizeof *copy->counts * (size_t)(c->n + 1));
	mcopy(copy->names, c->names, sizeof *c->names * (size_t)c->n);
	mcopy(copy->counts, c->counts, sizeof *c->counts * (size_t)c->n);
	return copy;
}

static void params_push(openapiParametersObject *ps, openapiParameterObject p)
{
	if (ps->n == ps->cap) {
		int cap = ps->cap ? ps->cap * 2 : 8;
		ps->v = xrealloc(ps->v, sizeof *ps->v * (size_t)ps->cap, sizeof *ps->v * (size_t)cap);
		ps->cap = cap;
	}
	ps->v[ps->n++] = p;
}

static void params_concat(openapiParametersObject *ps, openapiParametersObject more)
{
	for (int i = 0; i < more.n; i++)
		params_push(ps, more.v[i]);
}

static Str getMapParamKey(int t)
{
	const char *tType, *f;
	bool ok = primitiveSchema(t, &tType, &f);
	if (!ok || strcmp(f, "byte") == 0 || strcmp(f, "float") == 0 || strcmp(f, "double") == 0)
		fail("unsupported type: %s", go_quote(S(f)).p);
	return S(tType);
}

/* nestedQueryParams converts a field to a list of OpenAPI query parameters recursively; a
 * cyclical message reference fails (cyclical data structures are dangerous in query
 * parameters). */
static openapiParametersObject nestedQueryParams(GwMessage *message, GwField *field, Str prefix, GwRegistry *reg, Params pathParams,
						 const GwBody *body, cycleChecker *cycle)
{
	openapiParametersObject params = {0};
	check_depth(cycle->depth);
	if (++queryParamsWork > 2000000)
		fail("unsupported by protoc-gen-uni: query parameters of recursive messages that upstream does not finish "
		     "generating (lower recursive-depth)");
	/* make sure the parameter is not already listed as a path parameter */
	for (int i = 0; i < pathParams.len; i++)
		if (pathParams.v[i].Target == field)
			return params;
	/* make sure the parameter is not already listed as a body parameter */
	if (body) {
		if (!body->FieldPath.v)
			return params;
		for (int i = 0; i < body->FieldPath.n; i++)
			if (body->FieldPath.v[i].Target == field)
				return params;
	}
	openapiSchemaObject schema = schemaOfField(field, reg, NULL);
	const char *fieldType = field->TypeName;
	if (message->File) {
		Str comments = fieldProtoComments(reg, message, field);
		updateOpenAPIDataFromComments(reg, schemaTarget(&schema), comments, false);
	}

	bool isEnum = field->Type == PT_ENUM;
	openapiItemsObject *items = schema.core.Items;
	/* mapKeySuffix spells out the key of a map field in the parameter name, as in
	 * "filters[string]". */
	Str mapKeySuffix = {"", 0};
	if (schema.core.Type.n || isEnum) {
		if (str_eqc(schema.core.Type, "object")) {
			GwMessage *m = lookupMsg(reg, location_of(field->Message), field->TypeName);
			if (m && is_map_entry(m)) {
				if (m->Fields.n < 2)
					go_panic("index out of range of a map entry's fields");
				GwField *k = m->Fields.v[0];
				Str kType = getMapParamKey(k->Type);
				GwField *v = m->Fields.v[1];
				if (v->Type == PT_ENUM) {
					schema.core.Type = S(reg->enumsAsInts ? "integer" : "string");
				} else if (schema.addKind != ADDPROP_NONE) {
					if (schema.addKind == ADDPROP_SCHEMA)
						schema.core.Type = schema.AdditionalProperties->core.Type;
				}
				if (!schema.core.Type.n || str_eqc(schema.core.Type, "object"))
					/* The map value has no primitive representation, so there is no way
					 * to spell it in a query parameter. */
					return params;
				/* This will generate a query in the format map_name[key_type] */
				mapKeySuffix = cat3(S("["), kType, S("]"));
			}
			if (wktSchema(fieldType, NULL) && str_eqc(schema.core.Type, "object"))
				/* Empty and Struct are read from the query string as text. */
				schema.core.Type = S("string");
		}
		if (items && (!items->core.Type.n || str_eqc(items->core.Type, "object")) && !isEnum)
			return params; /* TODO: currently, mapping object in query parameter is not supported */
		Str desc = mergeDescription(&schema);

		/* verify if the field is required */
		bool required = false;
		const char *fieldName = gw_Registry_FieldName(reg, field);
		for (int i = 0; i < schema.Required.len; i++)
			if (str_eqc(schema.Required.v[i], fieldName)) {
				required = true;
				break;
			}
		/* verify if the field is required in message options */
		const PbMsg *messageSchema = openapi_option(pb_msg(message->proto, DP_OPTIONS), GW_OA_Schema);
		const PbMsg *js = pb_msg(messageSchema, OA_Schema_json_schema);
		GoStrs *mr = jsonschema_required(js);
		for (int i = 0; mr && i < mr->len; i++) {
			/* Required fields can be field names or json_name values */
			if (str_eqc(mr->v[i], field->JsonName) || str_eqc(mr->v[i], field->Name)) {
				required = true;
				break;
			}
		}

		/* verify if the field is deprecated, either via proto or annotation */
		bool protoDeprecated = field_deprecated(field) && reg->enableFieldDeprecation;
		bool annotationDeprecated = pb_int(getFieldConfiguration(reg, field), OA_FieldConfiguration_deprecated, 0) != 0;

		openapiParameterObject param = {
			.Description = desc,
			.In = S("query"),
			.Default = schema.core.Default,
			.XExample = schema.core.Example,
			.Type = schema.core.Type,
			.Items = schema.core.Items,
			.Format = schema.core.Format,
			.Pattern = schema.Pattern,
			.Required = required,
			.Deprecated = protoDeprecated || annotationDeprecated,
			.UniqueItems = schema.UniqueItems,
			.extensions = schema.extensions,
			.Enum = schema.core.Enum,
		};
		if (str_eqc(param.Type, "array")) {
			param.CollectionFormat = S("multi");
			if (schema.MinItems > 0) {
				param.MinItems = xalloc(sizeof(int));
				*param.MinItems = (int)schema.MinItems;
			}
			if (schema.MaxItems > 0) {
				param.MaxItems = xalloc(sizeof(int));
				*param.MaxItems = (int)schema.MaxItems;
			}
		}

		param.Name = cat3(prefix, S(fieldName), mapKeySuffix);

		if (isEnum) {
			GwEnum *e = lookupEnum(reg, "", fieldType);
			if (!e)
				fail("unknown enum type %s", fieldType);
			if (items) { /* array */
				param.Items = xalloc(sizeof *param.Items);
				param.Items->core.Type = S("string");
				param.Items->core.Enum = listEnumNames(reg, e);
				if (reg->enumsAsInts) {
					param.Items->core.Type = S("integer");
					param.Items->core.Enum = listEnumNumbers(reg, e);
				}
			} else {
				param.Type = S("string");
				param.Enum = listEnumNames(reg, e);
				param.Default = getEnumDefault(reg, e);
				if (reg->enumsAsInts) {
					param.Type = S("integer");
					param.Enum = listEnumNumbers(reg, e);
					param.Default = getEnumDefaultNumber(reg, e);
				}
			}
			Str valueComments = enumValueProtoComments(reg, e);
			if (valueComments.n) {
				Str d = cat3(param.Description, S("\n\n "), valueComments);
				while (d.n && d.p[0] == '\n')
					d.p++, d.n--;
				param.Description = d;
			}
		}
		params_push(&params, param);
		return params;
	}

	/* nested type, recurse */
	GwMessage *msg = lookupMsg(reg, "", fieldType);
	if (!msg)
		fail("unknown message type %s", fieldType);

	/* Check for cyclical message reference: */
	if (!cycleChecker_Check(cycle, msg->Name))
		fail("exceeded recursive count (%d) for query parameter %s", cycle->count, go_quote(S(fieldType)).p);

	/* Construct a new map with the message name so a cycle further down the recursive path can
	 * be detected. */
	cycleChecker *touchedOut = cycleChecker_Branch(cycle);

	for (int i = 0; i < msg->Fields.n; i++) {
		GwField *nestedField = msg->Fields.v[i];
		if (!isVisible(getFieldVisibilityOption(nestedField), reg))
			continue;
		const char *fieldName = gw_Registry_FieldName(reg, field);
		openapiParametersObject p =
			nestedQueryParams(msg, nestedField, cat3(prefix, S(fieldName), S(".")), reg, pathParams, body, touchedOut);
		params_concat(&params, p);
	}
	return params;
}

/* queryParams converts a field to a list of OpenAPI query parameters recursively through the use
 * of nestedQueryParams. */
static openapiParametersObject queryParams(GwMessage *message, GwField *field, Str prefix, GwRegistry *reg, Params pathParams,
					   const GwBody *body, int recursiveCount)
{
	return nestedQueryParams(message, field, prefix, reg, pathParams, body, newCycleChecker(recursiveCount));
}

/* isPathSameOneOf returns true if the given field is a member of a oneof group of which another
 * member is already bound to a path parameter. */
static bool isPathSameOneOf(Params pathParams, const GwField *field)
{
	if (!field->HasOneofIndex)
		return false;
	for (int i = 0; i < pathParams.len; i++) {
		const GwParameter *pathParam = &pathParams.v[i];
		if (pathParam->FieldPath.n == 0)
			continue;
		/* Only the first component of the path parameter is a field of the same message as
		 * field. */
		const GwField *target = pathParam->FieldPath.v[0].Target;
		if (!target || target == field || !target->HasOneofIndex)
			continue;
		if (target->OneofIndex == field->OneofIndex)
			return true;
	}
	return false;
}

static bool isBodySameOneOf(const GwBody *body, const GwField *field)
{
	if (!field->HasOneofIndex)
		return false;
	if (!body || body->FieldPath.n == 0)
		return false;
	if (!body->FieldPath.v[0].Target->HasOneofIndex)
		return false;
	return body->FieldPath.v[0].Target->OneofIndex == field->OneofIndex;
}

/* messageToQueryParameters converts a message to a list of OpenAPI query parameters. */
static openapiParametersObject messageToQueryParameters(GwMessage *message, GwRegistry *reg, Params pathParams, const GwBody *body,
							Str httpMethod)
{
	openapiParametersObject params = {0};
	for (int i = 0; i < message->Fields.n; i++) {
		GwField *field = message->Fields.v[i];
		/* When body is set to oneof field, we want to skip other fields in the oneof group. */
		if (isBodySameOneOf(body, field))
			continue;
		/* When a path parameter is set to a oneof field, we want to skip the other fields in
		 * the oneof group. */
		if (isPathSameOneOf(pathParams, field))
			continue;
		if (!isVisible(getFieldVisibilityOption(field), reg))
			continue;
		if (reg->allowPatchFeature && strcmp(field->TypeName, ".google.protobuf.FieldMask") == 0 &&
		    strcmp(field->Name, "update_mask") == 0 && str_eqc(httpMethod, "PATCH")) {
			if (!body)
				go_panic("nil pointer dereference: a PATCH binding without body has an update_mask");
			if (body->FieldPath.n != 0)
				continue;
		}
		openapiParametersObject p = queryParams(message, field, S(""), reg, pathParams, body, reg->recursiveDepth);
		params_concat(&params, p);
	}
	return params;
}

/* ---- sets and maps keyed by strings ---- */

struct StrSet {
	Map m;     /* hex key -> index + 1 */
	Vec keys;  /* Str * in insertion order, NULL once deleted */
};

static bool set_has(const StrSet *s, Str k) { return map_get(&s->m, map_key(k)) != NULL; }

static void set_add(StrSet *s, Str k)
{
	const char *key = map_key(k);
	if (map_get(&s->m, key))
		return;
	Str *p = xalloc(sizeof *p);
	*p = k;
	vec_push(&s->keys, p);
	map_put(&s->m, key, (void *)(intptr_t)s->keys.n);
}

static void set_del(StrSet *s, Str k)
{
	const char *key = map_key(k);
	intptr_t i = (intptr_t)map_get(&s->m, key);
	if (!i)
		return;
	s->keys.v[i - 1] = NULL;
	map_put(&s->m, key, NULL);
}

static int set_len(const StrSet *s)
{
	int n = 0;
	for (int i = 0; i < s->keys.n; i++)
		n += s->keys.v[i] != NULL;
	return n;
}

/* messageMap / enumMap: name -> descriptor, iterated in sorted key order */
typedef struct {
	Map m;    /* key -> value */
	Vec keys; /* const char * */
} NameMap;

static void nm_put(NameMap *m, const char *k, void *v)
{
	if (!map_has(&m->m, k))
		vec_push(&m->keys, (void *)k);
	map_put(&m->m, k, v);
}

static int cmp_cstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static const char **nm_sorted(const NameMap *m)
{
	const char **k = xalloc(sizeof *k * (size_t)(m->keys.n + 1));
	mcopy(k, m->keys.v, sizeof *k * (size_t)m->keys.n);
	sort_any(k, (size_t)m->keys.n, sizeof *k, cmp_cstr);
	return k;
}

/* ---- the naming cache (registriesSeen) ---- */

static Map *registriesSeen;

static Map *resolveFullyQualifiedNameToOpenAPINames(const char **messages, int n, const char *namingStrategy)
{
	namingStrategyFn fn = LookupNamingStrategy(S(namingStrategy));
	if (!fn)
		return xalloc(sizeof(Map));
	return fn(messages, n);
}

static int allNames(GwRegistry *reg, const char ***out)
{
	const char **a, **b, **c;
	int na = gw_Registry_GetAllFQMNs(reg, &a), nb = gw_Registry_GetAllFQENs(reg, &b), nc = gw_Registry_GetAllFQMethNs(reg, &c);
	const char **all = xalloc(sizeof *all * (size_t)(na + nb + nc + 1));
	mcopy(all, a, sizeof *all * (size_t)na);
	mcopy(all + na, b, sizeof *all * (size_t)nb);
	mcopy(all + na + nb, c, sizeof *all * (size_t)nc);
	*out = all;
	return na + nb + nc;
}

/* Take in a FQMN or FQEN and return a OpenAPI safe version of the FQMN and a boolean indicating
 * if FQMN was properly resolved. */
static const char *fullyQualifiedNameToOpenAPIName(Str fqn, GwRegistry *reg)
{
	if (!registriesSeen) {
		const char **names;
		int n = allNames(reg, &names);
		registriesSeen = resolveFullyQualifiedNameToOpenAPINames(names, n, reg->openAPINamingStrategy);
	}
	if (memchr(fqn.p, 0, fqn.n))
		return NULL;
	return map_get(registriesSeen, xstrndup(fqn.p, fqn.n));
}

static const char *openAPIName(const char *fqn, GwRegistry *reg) { return fullyQualifiedNameToOpenAPIName(S(fqn), reg); }

/* ---- references ---- */

static VisibilityRule getServiceVisibilityOption(const GwService *s) { return visibility_option(pb_msg(s->proto, 3)); }
static VisibilityRule getMethodVisibilityOption(const GwMethod *m) { return visibility_option(pb_msg(m->proto, MDP_OPTIONS)); }
static VisibilityRule getFieldVisibilityOption(const GwField *f) { return visibility_option(pb_msg(f->proto, FD_OPTIONS)); }

static bool skipRenderingRef(const char *refName) { return wktSchema(refName, NULL); }

/* findNestedMessagesAndEnumerations those can be generated by the services. */
static void findNestedMessagesAndEnumerations_(GwMessage *message, GwRegistry *reg, NameMap *m, NameMap *e, int depth);

static void findNestedMessagesAndEnumerations(GwMessage *message, GwRegistry *reg, NameMap *m, NameMap *e)
{
	findNestedMessagesAndEnumerations_(message, reg, m, e, 0);
}

static void findNestedMessagesAndEnumerations_(GwMessage *message, GwRegistry *reg, NameMap *m, NameMap *e, int depth)
{
	check_depth(depth);
	for (int i = 0; i < message->Fields.n; i++) {
		GwField *t = message->Fields.v[i];
		if (!isVisible(getFieldVisibilityOption(t), reg))
			continue;
		const char *fieldType = t->TypeName;
		/* If the type is an empty string then it is a proto primitive */
		if (fieldType[0] && !map_has(&m->m, fieldType)) {
			GwMessage *msg = lookupMsg(reg, "", fieldType);
			if (!msg) {
				GwEnum *en = lookupEnum(reg, "", fieldType);
				if (!en)
					go_panic(xcat("no enum found: ", fieldType));
				nm_put(e, fieldType, en);
				continue;
			}
			nm_put(m, fieldType, msg);
			findNestedMessagesAndEnumerations_(msg, reg, m, e, depth + 1);
		}
	}
}

/* findServicesMessagesAndEnumerations discovers all messages and enums defined in the RPC
 * methods of the service. */
static void findServicesMessagesAndEnumerations(Vec services, GwRegistry *reg, NameMap *m, NameMap *e, StrSet *refs)
{
	for (int s = 0; s < services.n; s++) {
		GwService *svc = services.v[s];
		if (!isVisible(getServiceVisibilityOption(svc), reg))
			continue;
		for (int k = 0; k < svc->Methods.n; k++) {
			GwMethod *meth = svc->Methods.v[k];
			/* Request may be fully included in query */
			if (!isVisible(getMethodVisibilityOption(meth), reg))
				continue;
			/* Only process methods with HTTP bindings */
			if (meth->Bindings.n == 0)
				continue;
			const char *swgReqName = openAPIName(message_fqmn(meth->RequestType), reg);
			if (!swgReqName)
				continue; /* grpclog.Errorf */
			if (set_has(refs, S(xcat("#/definitions/", swgReqName))))
				if (!skipRenderingRef(message_fqmn(meth->RequestType)))
					nm_put(m, swgReqName, meth->RequestType);

			const char *swgRspName = openAPIName(message_fqmn(meth->ResponseType), reg);
			if (!swgRspName && !skipRenderingRef(message_fqmn(meth->ResponseType)))
				continue; /* grpclog.Errorf */

			findNestedMessagesAndEnumerations(meth->RequestType, reg, m, e);

			if (!skipRenderingRef(message_fqmn(meth->ResponseType)))
				nm_put(m, swgRspName ? swgRspName : "", meth->ResponseType);
			findNestedMessagesAndEnumerations(meth->ResponseType, reg, m, e);
		}
	}
}

/* collectNestedTypeFQNs recursively collects FQMNs/FQENs for all nested types of a message. */
static void collectNestedTypeFQNs_(GwMessage *message, GwRegistry *reg, StrSet *refs, int depth);

static void collectNestedTypeFQNs(GwMessage *message, GwRegistry *reg, StrSet *refs) { collectNestedTypeFQNs_(message, reg, refs, 0); }

static void collectNestedTypeFQNs_(GwMessage *message, GwRegistry *reg, StrSet *refs, int depth)
{
	check_depth(depth);
	for (int i = 0; i < message->Fields.n; i++) {
		GwField *field = message->Fields.v[i];
		if (!isVisible(getFieldVisibilityOption(field), reg))
			continue;
		const char *fieldType = field->TypeName;
		if (!fieldType[0])
			continue; /* primitive type */
		if (set_has(refs, S(fieldType)))
			continue; /* already visited */
		set_add(refs, S(fieldType));
		GwMessage *msg = lookupMsg(reg, "", fieldType);
		if (msg)
			collectNestedTypeFQNs_(msg, reg, refs, depth + 1);
	}
}

static const PbMsgDef *jsonSchemaDef;

/* collectCustomRefs adds every JSONSchema "ref" found in an OpenAPI option message to refs. */
static void collectCustomRefs(const PbMsg *m, StrSet *refs, int depth)
{
	if (!m)
		return;
	if (depth > 5000)
		go_panic("an option message nested too deeply");
	if (m->def == jsonSchemaDef) {
		Str ref = pb_bytes(m, OA_JSONSchema_ref);
		if (ref.n)
			set_add(refs, ref);
	}
	for (int i = 0; i < m->def->n; i++) {
		if (m->def->f[i].type != PT_MESSAGE)
			continue;
		const PbSlot *s = &m->slots[i];
		for (int k = 0; k < s->n; k++)
			collectCustomRefs(s->v[k].m, refs, depth + 1);
	}
}

static const PbMsg *getMethodOpenAPIOption(GwRegistry *reg, GwMethod *meth);
static const PbMsg *getMessageOpenAPIOption(GwRegistry *reg, GwMessage *msg);
static const PbMsg *getFileOpenAPIOption(GwRegistry *reg, GwFile *file);

/* collectReferencedNamesForCache scans services and messages to collect all FQMNs/FQENs that
 * will be referenced, without using the naming cache. */
static StrSet collectReferencedNamesForCache(GwFile *file, Vec services, Vec messages, GwRegistry *reg)
{
	StrSet refs = {0};
	/* Custom refs are collected separately and traversed last, so they do not short-circuit
	 * collectNestedTypeFQNs' visited check. */
	StrSet customRefs = {0};

	/* Scan services FIRST so collectNestedTypeFQNs fully traverses message graphs. */
	for (int s = 0; s < services.n; s++) {
		GwService *svc = services.v[s];
		if (!isVisible(getServiceVisibilityOption(svc), reg))
			continue;
		for (int k = 0; k < svc->Methods.n; k++) {
			GwMethod *meth = svc->Methods.v[k];
			if (!isVisible(getMethodVisibilityOption(meth), reg))
				continue;
			const PbMsg *opts = getMethodOpenAPIOption(reg, meth);
			if (opts)
				collectCustomRefs(opts, &customRefs, 0);
			if (meth->Bindings.n == 0)
				continue;
			/* Add method FQN (needed for body:"*" with path params) */
			set_add(&refs, S(gw_Method_FQMN(meth)));
			/* Add request/response types */
			set_add(&refs, S(message_fqmn(meth->RequestType)));
			set_add(&refs, S(message_fqmn(meth->ResponseType)));
			/* Recursively add nested types */
			collectNestedTypeFQNs(meth->RequestType, reg, &refs);
			collectNestedTypeFQNs(meth->ResponseType, reg, &refs);
		}
	}

	/* Add messages from the current file AFTER service scanning. */
	for (int i = 0; i < messages.n; i++) {
		GwMessage *msg = messages.v[i];
		set_add(&refs, S(message_fqmn(msg)));
		collectNestedTypeFQNs(msg, reg, &refs);
		const PbMsg *opts = getMessageOpenAPIOption(reg, msg);
		if (opts)
			collectCustomRefs(opts, &customRefs, 0);
		for (int k = 0; k < msg->Fields.n; k++) {
			const PbMsg *fo = getFieldOpenAPIOption(reg, msg->Fields.v[k]);
			if (fo)
				collectCustomRefs(fo, &customRefs, 0);
		}
	}
	const PbMsg *fopts = getFileOpenAPIOption(reg, file);
	if (fopts)
		collectCustomRefs(fopts, &customRefs, 0);

	for (int i = 0; i < customRefs.keys.n; i++) {
		Str ref = *(Str *)customRefs.keys.v[i];
		if (set_has(&refs, ref))
			continue;
		set_add(&refs, ref);
		if (!memchr(ref.p, 0, ref.n)) {
			GwMessage *msg = lookupMsg(reg, "", xstrndup(ref.p, ref.n));
			if (msg)
				collectNestedTypeFQNs(msg, reg, &refs);
		}
	}

	/* Add google.rpc.Status if default errors enabled */
	if (!reg->disableDefaultErrors) {
		set_add(&refs, S(".google.rpc.Status"));
		/* Also add nested types of Status */
		GwMessage *statusMsg = lookupMsg(reg, "google.rpc", "Status");
		if (statusMsg)
			collectNestedTypeFQNs(statusMsg, reg, &refs);
	}
	return refs;
}

/* isEmptyObjectSchema reports whether schema is an object carrying no information. */
static bool isEmptyObjectSchema(const openapiSchemaObject *schema)
{
	return str_eqc(schema->core.Type, "object") && (!schema->Properties || schema->Properties->n == 0) &&
	       schema->addKind == ADDPROP_NONE;
}

/* ---- schemas of fields and messages ---- */

/* primitiveSchema returns a pair of "Type" and "Format" in JSON Schema for the given primitive
 * field type; false if the field type is not primitive. */
static bool primitiveSchema(int t, const char **ftype, const char **format)
{
	const char *ty, *fo;
	switch (t) {
	case PT_DOUBLE:
		ty = "number", fo = "double";
		break;
	case PT_FLOAT:
		ty = "number", fo = "float";
		break;
	case PT_INT64:
		ty = "string", fo = "int64";
		break;
	case PT_UINT64:
		/* 64bit integer types are marshaled as string in the default JSONPb marshaler. */
		ty = "string", fo = "uint64";
		break;
	case PT_INT32:
		ty = "integer", fo = "int32";
		break;
	case PT_FIXED64:
		ty = "string", fo = "uint64";
		break;
	case PT_FIXED32:
		ty = "integer", fo = "int64";
		break;
	case PT_BOOL:
		/* NOTE: in OpenAPI specification, format should be empty on boolean type */
		ty = "boolean", fo = "";
		break;
	case PT_STRING:
		ty = "string", fo = "";
		break;
	case PT_BYTES:
		ty = "string", fo = "byte";
		break;
	case PT_UINT32:
		ty = "integer", fo = "int64";
		break;
	case PT_SFIXED32:
		ty = "integer", fo = "int32";
		break;
	case PT_SFIXED64:
		ty = "string", fo = "int64";
		break;
	case PT_SINT32:
		ty = "integer", fo = "int32";
		break;
	case PT_SINT64:
		ty = "string", fo = "int64";
		break;
	default:
		*ftype = *format = "";
		return false;
	}
	*ftype = ty;
	*format = fo;
	return true;
}

/* FieldDescriptorProto_Type.String() */
static const char *fieldTypeString(int t)
{
	static const char *const names[] = {
		NULL,		 "TYPE_DOUBLE", "TYPE_FLOAT",	 "TYPE_INT64",	  "TYPE_UINT64",   "TYPE_INT32",
		"TYPE_FIXED64",	 "TYPE_FIXED32", "TYPE_BOOL",	 "TYPE_STRING",	  "TYPE_GROUP",	   "TYPE_MESSAGE",
		"TYPE_BYTES",	 "TYPE_UINT32",	 "TYPE_ENUM",	 "TYPE_SFIXED32", "TYPE_SFIXED64", "TYPE_SINT32",
		"TYPE_SINT64",
	};
	if (t >= 1 && t <= 18)
		return names[t];
	return xfmt("%d", t);
}

/* Map *refs: refMap (map[string]struct{}) or NULL; the keys are names (StrSet). */
static void refs_add(StrSet *refs, const char *name)
{
	if (refs)
		set_add(refs, S(name));
}

/* schemaOfFieldBase returns a base Schema Object for a protobuf field. */
static openapiSchemaObject schemaOfFieldBase(GwField *f, GwRegistry *reg, StrSet *refs)
{
	enum { singular, array, object };
	schemaCore core = {0};
	int aggregate = singular;

	/* fd: the field, or the value of a map entry */
	int fdType = f->Type, fdLabel = f->Label;
	const char *fdTypeName = f->TypeName;
	GwMessage *m = lookupMsg(reg, location_of(f->Message), f->TypeName);
	if (m && is_map_entry(m)) {
		if (m->Fields.n < 2)
			go_panic("index out of range of a map entry's fields");
		GwField *v = m->Fields.v[1];
		fdType = v->Type, fdLabel = v->Label, fdTypeName = v->TypeName;
		aggregate = object;
	}
	if (fdLabel == LABEL_REPEATED)
		aggregate = array;

	openapiSchemaObjectProperties *props = NULL;

	switch (fdType) {
	case PT_ENUM:
	case PT_MESSAGE:
	case PT_GROUP:
		if (wktSchema(fdTypeName, &core)) {
			if (strcmp(fdTypeName, ".google.protobuf.Empty") == 0)
				props = xalloc(sizeof *props);
		} else {
			const char *swgRef = openAPIName(fdTypeName, reg);
			if (!swgRef)
				go_panic(xcat("can't resolve OpenAPI ref from typename ", go_quote(S(fdTypeName)).p));
			core = (schemaCore){0};
			core.Ref = S(xcat("#/definitions/", swgRef));
			refs_add(refs, fdTypeName);
		}
		break;
	default: {
		const char *ftype, *format;
		if (primitiveSchema(fdType, &ftype, &format)) {
			core = (schemaCore){.Type = S(ftype), .Format = S(format)};
		} else {
			core = (schemaCore){.Type = S(fieldTypeString(fdType)), .Format = S("UNKNOWN")};
		}
	}
	}

	openapiSchemaObject ret = {0};
	switch (aggregate) {
	case array:
		/* Only set core.Type = "object" for MESSAGE types with $ref if the flag is not set. */
		if (!reg->omitArrayItemTypeWhenRefSibling) {
			if (!wktSchema(fdTypeName, NULL) && fdType == PT_MESSAGE)
				core.Type = S("object");
		}
		ret.core.Type = S("array");
		ret.core.Items = xalloc(sizeof *ret.core.Items);
		ret.core.Items->core = core;
		break;
	case object:
		ret.core.Type = S("object");
		ret.addKind = ADDPROP_SCHEMA;
		ret.AdditionalProperties = xalloc(sizeof *ret.AdditionalProperties);
		ret.AdditionalProperties->Properties = props;
		ret.AdditionalProperties->core = core;
		break;
	default:
		ret.core = core;
		ret.Properties = props;
	}
	return ret;
}

/* ---- the openapiv2 options ---- */

static const PbMsg *getFieldOpenAPIOption(GwRegistry *reg, GwField *f)
{
	(void)reg; /* GetOpenAPIFieldOption: openapi_configuration is not supported */
	return openapi_option(pb_msg(f->proto, FD_OPTIONS), GW_OA_JSONSchema);
}

static const PbMsg *getMethodOpenAPIOption(GwRegistry *reg, GwMethod *meth)
{
	(void)reg;
	return openapi_option(pb_msg(meth->proto, MDP_OPTIONS), GW_OA_Operation);
}

static const PbMsg *getMessageOpenAPIOption(GwRegistry *reg, GwMessage *msg)
{
	(void)reg;
	return openapi_option(pb_msg(msg->proto, DP_OPTIONS), GW_OA_Schema);
}

static const PbMsg *getEnumOpenAPIOption(GwRegistry *reg, GwEnum *e)
{
	(void)reg;
	return openapi_option(pb_msg(e->proto, EDP_OPTIONS), GW_OA_EnumSchema);
}

static const PbMsg *getServiceOpenAPIOption(GwRegistry *reg, GwService *svc)
{
	(void)reg;
	return openapi_option(pb_msg(svc->proto, 3), GW_OA_Tag);
}

static const PbMsg *getFileOpenAPIOption(GwRegistry *reg, GwFile *file)
{
	(void)reg;
	return openapi_option(pb_msg(file->proto, FDP_OPTIONS), GW_OA_Swagger);
}

/* getFieldBehaviorOption: the google.api.field_behavior values (n == 0: none) */
static int getFieldBehaviorOption(GwRegistry *reg, GwField *f, int32_t **out)
{
	(void)reg;
	return genproto_ext_enums(pb_msg(f->proto, FD_OPTIONS), GENPROTO_E_FIELD_BEHAVIOR, out);
}

static const PbMsg *getFieldConfiguration(GwRegistry *reg, GwField *f)
{
	return pb_msg(getFieldOpenAPIOption(reg, f), OA_JSONSchema_field_configuration);
}

/* processExtensions: the extensions sorted by key, their values as protojson. */
static Extensions processExtensions(const PbMsg *m, uint32_t num)
{
	GwMapEntry *e;
	int n = gw_options_map(m, num, &e);
	Extensions exts = {xalloc(sizeof *exts.v * (size_t)(n + 1)), 0, true};
	for (int i = 0; i < n; i++) {
		Str k = e[i].key;
		if (!str_has_prefix(k, STR_LIT("x-")))
			fail("extension keys need to start with \"x-\": %s", go_quote(k).p);
		Buf b = {0};
		const PbMsg *v = pb_msg(e[i].value, 2);
		const char *err = NULL;
		if (!v) /* a map value that is not set: an empty message */
			v = gw_options_decode(GW_OA_Value, (Str){"", 0}, &err);
		err = protojson_marshal_value(v, &b);
		if (err)
			fail("%s", err);
		exts.v[exts.n++] = (extension){k, buf_view(&b)};
	}
	return exts;
}

static bool has_map(const PbMsg *m, uint32_t num) { return pb_count(m, num) > 0; }

typedef struct {
	const PbMsg *m;
	uint32_t num;
	Extensions out;
} ExtCtx;

static void processExtensions_run(void *arg)
{
	ExtCtx *c = arg;
	c->out = processExtensions(c->m, c->num);
}

/* protoJSONSchemaTypeToFormat: the first type only */
static void protoJSONSchemaTypeToFormat(const PbMsg *j, Str *type, Str *format)
{
	*type = *format = S("");
	if (pb_count(j, OA_JSONSchema_type) == 0)
		return;
	switch (pb_int_at(j, OA_JSONSchema_type, 0)) {
	case 6: /* OBJECT */
		*type = S("object");
		break;
	case 1: /* ARRAY */
		*type = S("array");
		break;
	case 2: /* BOOLEAN */
		*type = S("boolean");
		break;
	case 3: /* INTEGER */
		*type = S("integer"), *format = S("int32");
		break;
	case 5: /* NUMBER */
		*type = S("number"), *format = S("double");
		break;
	case 7: /* STRING */
		*type = S("string");
		break;
	}
}

/* JSONSchema_JSONSchemaSimpleTypes.String() */
static const char *simpleTypeName(int64_t t)
{
	static const char *const names[] = {"UNKNOWN", "ARRAY", "BOOLEAN", "INTEGER", "NULL", "NUMBER", "OBJECT", "STRING"};
	if (t >= 0 && t <= 7)
		return names[t];
	return xfmt("%lld", (long long)t);
}

static schemaCore protoJSONSchemaToOpenAPISchemaCore(const PbMsg *j, GwRegistry *reg, StrSet *refs)
{
	schemaCore ret = {0};
	Str ref = pb_bytes(j, OA_JSONSchema_ref);
	if (ref.n) {
		const char *openapiName = fullyQualifiedNameToOpenAPIName(ref, reg);
		if (openapiName) {
			ret.Ref = S(xcat("#/definitions/", openapiName));
			if (refs)
				set_add(refs, ref);
		} else {
			ret.Ref = ref;
		}
	} else {
		protoJSONSchemaTypeToFormat(j, &ret.Type, &ret.Format);
	}
	return ret;
}

static double pb_double(const PbMsg *m, uint32_t num)
{
	const PbSlot *s = pb_slot(m, num);
	if (!s || !s->n)
		return 0;
	double d;
	memcpy(&d, &s->v[s->n - 1].u, sizeof d);
	return d;
}

static uint64_t pb_u64(const PbMsg *m, uint32_t num) { return (uint64_t)pb_int(m, num, 0); }

static void updateswaggerObjectFromJSONSchema(openapiSchemaObject *s, const PbMsg *j, GwRegistry *reg)
{
	(void)reg;
	s->Title = pb_bytes(j, OA_JSONSchema_title);
	s->Description = pb_bytes(j, OA_JSONSchema_description);
	Str def = pb_bytes(j, OA_JSONSchema_default);
	GoStrs en = pb_strs(j, OA_JSONSchema_enum);
	openapiSchemaObject *t = s;
	if (str_eqc(s->core.Type, "array")) {
		t = s->core.Items;
		if (!t)
			go_panic("nil pointer dereference: a JSON schema of type array without items");
	}
	t->MaxLength = pb_u64(j, OA_JSONSchema_max_length);
	t->MinLength = pb_u64(j, OA_JSONSchema_min_length);
	t->Pattern = pb_bytes(j, OA_JSONSchema_pattern);
	t->core.Default = (Any){.kind = ANY_STRING, .s = def};
	t->MaxProperties = pb_u64(j, OA_JSONSchema_max_properties);
	t->MinProperties = pb_u64(j, OA_JSONSchema_min_properties);
	t->Minimum = pb_double(j, OA_JSONSchema_minimum);
	t->Maximum = pb_double(j, OA_JSONSchema_maximum);
	t->ReadOnly = pb_int(j, OA_JSONSchema_read_only, 0) != 0;
	t->MultipleOf = pb_double(j, OA_JSONSchema_multiple_of);
	t->ExclusiveMaximum = pb_int(j, OA_JSONSchema_exclusive_maximum, 0) != 0;
	t->ExclusiveMinimum = pb_int(j, OA_JSONSchema_exclusive_minimum, 0) != 0;
	t->core.Enum = (Any){.kind = ANY_STRINGS, .strs = en};
	if (!def.n)
		t->core.Default = (Any){0};
	if (en.len == 0)
		t->core.Enum = (Any){0};
	Str format = pb_bytes(j, OA_JSONSchema_format);
	if (format.n)
		t->core.Format = format;
	/* Required is not a constraint on the values, so for repeated fields it is kept on the
	 * field's schema rather than its items. */
	GoStrs *req = jsonschema_required(j);
	s->Required = req ? *req : (GoStrs){0};
	s->UniqueItems = pb_int(j, OA_JSONSchema_unique_items, 0) != 0;
	s->MaxItems = pb_u64(j, OA_JSONSchema_max_items);
	s->MinItems = pb_u64(j, OA_JSONSchema_min_items);

	if (has_map(j, OA_JSONSchema_extensions)) {
		/* an error: panic(err) */
		ExtCtx c = {j, OA_JSONSchema_extensions, {0}};
		if (try_run(processExtensions_run, &c)) {
			Str msg = fail_message();
			if (str_has_prefix(msg, STR_LIT("unsupported by protoc-gen-uni: ")))
				fail_str(msg);
			go_panic(xstrndup(msg.p, msg.n));
		}
		s->extensions = c.out;
	}
	if (pb_count(j, OA_JSONSchema_type) > 0) {
		/* strings.ToLower(overrideType[0].String()) */
		const char *name = simpleTypeName(pb_int_at(j, OA_JSONSchema_type, 0));
		char *l = xstrdup(name);
		for (char *p = l; *p; p++)
			if (*p >= 'A' && *p <= 'Z')
				*p = (char)(*p + 32);
		s->core.Type = S(l);
	}
	Str example = pb_bytes(j, OA_JSONSchema_example);
	if (example.n)
		s->core.Example = example;
}

static void updateSwaggerObjectFromFieldBehavior(openapiSchemaObject *s, const int32_t *j, int n, GwRegistry *reg, GwField *field)
{
	bool required = false;
	if (reg->useProto3FieldSemantics)
		required = !field->Proto3Optional && !field->HasOneofIndex;
	for (int i = 0; i < n; i++) {
		switch (j[i]) {
		case GENPROTO_FIELD_BEHAVIOR_REQUIRED:
			required = true;
			break;
		case GENPROTO_FIELD_BEHAVIOR_OUTPUT_ONLY:
			s->ReadOnly = true;
			break;
		case GENPROTO_FIELD_BEHAVIOR_OPTIONAL:
			required = false;
			break;
		}
	}
	if (required)
		s->Required = strs_append1(s->Required, S(reg->useJSONNamesForFields ? field->JsonName : field->Name));
}

static int cmp_str(const void *a, const void *b) { return str_cmp(*(const Str *)a, *(const Str *)b); }

/* schemaOfField returns a OpenAPI Schema Object for a protobuf field. */
static openapiSchemaObject schemaOfField(GwField *f, GwRegistry *reg, StrSet *refs)
{
	openapiSchemaObject ret = schemaOfFieldBase(f, reg, refs);

	const PbMsg *j = getFieldOpenAPIOption(reg, f);
	if (j)
		updateswaggerObjectFromJSONSchema(&ret, j, reg);
	else
		updateswaggerObjectFromJSONSchema(&ret, NULL, reg);

	int32_t *fb;
	int nfb = getFieldBehaviorOption(reg, f, &fb);
	updateSwaggerObjectFromFieldBehavior(&ret, fb, nfb, reg, f);

	for (int i = 0; i < ret.Required.len; i++)
		if (str_eqc(ret.Required.v[i], f->Name))
			ret.Required.v[i] = S(gw_Registry_FieldName(reg, f));

	/* slices.Sort, slices.Compact (in place; Compact zeroes the obsolete elements) */
	if (ret.Required.len > 1) {
		Str *v = ret.Required.v;
		int n = ret.Required.len;
		/* a stable sort gives the same result as pdqsort for strings (equal ones are identical) */
		sort_any(v, (size_t)n, sizeof *v, cmp_str);
		int k = 1;
		for (int i = 1; i < n; i++)
			if (!str_equal(v[i], v[k - 1]))
				v[k++] = v[i];
		for (int i = k; i < n; i++)
			v[i] = (Str){"", 0};
		ret.Required.len = k;
	}

	if (reg->proto3OptionalNullable && f->Proto3Optional)
		ret.core.XNullable = true;
	return ret;
}

static openapiExternalDocumentationObject *protoExternalDocumentationToOpenAPIExternalDocumentation(const PbMsg *in)
{
	if (!in)
		return NULL;
	openapiExternalDocumentationObject *d = xalloc(sizeof *d);
	d->Description = pb_bytes(in, OA_ExternalDocumentation_description);
	d->URL = pb_bytes(in, OA_ExternalDocumentation_url);
	return d;
}

static openapiSchemaObject openapiSchemaFromProtoSchema(const PbMsg *s, GwRegistry *reg, StrSet *refs)
{
	openapiSchemaObject ret = {0};
	ret.ExternalDocs = protoExternalDocumentationToOpenAPIExternalDocumentation(pb_msg(s, OA_Schema_external_docs));
	const PbMsg *j = pb_msg(s, OA_Schema_json_schema);
	ret.core = protoJSONSchemaToOpenAPISchemaCore(j, reg, refs);
	updateswaggerObjectFromJSONSchema(&ret, j, reg);
	Str example = pb_bytes(s, OA_Schema_example);
	if (s && example.n)
		ret.core.Example = example;
	return ret;
}

/* openapiSchemaFromProtoEnumSchema: the fields of the EnumSchema as a JSONSchema */
static openapiSchemaObject openapiSchemaFromProtoEnumSchema(const PbMsg *s, GwRegistry *reg, StrSet *refs)
{
	openapiSchemaObject ret = {0};
	ret.ExternalDocs = protoExternalDocumentationToOpenAPIExternalDocumentation(pb_msg(s, OA_EnumSchema_external_docs));
	Buf b = {0};
	static const uint32_t strs[][2] = {
		{OA_EnumSchema_ref, OA_JSONSchema_ref},
		{OA_EnumSchema_title, OA_JSONSchema_title},
		{OA_EnumSchema_description, OA_JSONSchema_description},
		{OA_EnumSchema_default, OA_JSONSchema_default},
		{OA_EnumSchema_example, OA_JSONSchema_example},
	};
	for (size_t i = 0; i < sizeof strs / sizeof *strs; i++) {
		Str v = pb_bytes(s, strs[i][0]);
		if (v.n)
			pb_put_bytes(&b, strs[i][1], v);
	}
	if (pb_int(s, OA_EnumSchema_read_only, 0)) {
		pb_put_tag(&b, OA_JSONSchema_read_only, WT_VARINT);
		pb_put_varint(&b, 1);
	}
	for (int i = 0; i < pb_count(s, OA_EnumSchema_extensions); i++) {
		Buf e = {0};
		gopb_encode(pb_msg_at(s, OA_EnumSchema_extensions, i), &e);
		pb_put_bytes(&b, OA_JSONSchema_extensions, buf_view(&e));
	}
	const char *err;
	const PbMsg *jsonSchema = gw_options_decode(GW_OA_JSONSchema, buf_view(&b), &err);
	if (!jsonSchema)
		fail("%s", err);
	ret.core = protoJSONSchemaToOpenAPISchemaCore(jsonSchema, reg, refs);
	updateswaggerObjectFromJSONSchema(&ret, jsonSchema, reg);
	return ret;
}

/* find: the index of x in a, or -1 */
static int find(GoStrs a, Str x)
{
	for (int i = 0; i < a.len; i++)
		if (str_equal(a.v[i], x))
			return i;
	return -1;
}

/* this method will filter the same fields and return the unique one */
static GoStrs getUniqueFields(GoStrs schemaFieldsRequired, GoStrs fieldsRequired)
{
	GoStrs unique = {0};
	for (int j = 0; j < schemaFieldsRequired.len; j++)
		if (find(fieldsRequired, schemaFieldsRequired.v[j]) < 0)
			unique = strs_append1(unique, schemaFieldsRequired.v[j]);
	return unique;
}

static bool shouldExcludeField(const char *name, Params excluded)
{
	for (int i = 0; i < excluded.len; i++)
		if (excluded.v[i].FieldPath.n == 1 && strcmp(name, excluded.v[i].FieldPath.v[0].Name) == 0)
			return true;
	return false;
}

static GoStrs filterOutExcludedFields(GoStrs fields, Params excluded)
{
	GoStrs filtered = {0};
	for (int i = 0; i < fields.len; i++) {
		bool excl = false;
		for (int k = 0; k < excluded.len && !excl; k++)
			excl = excluded.v[k].FieldPath.n == 1 && str_eqc(fields.v[i], excluded.v[k].FieldPath.v[0].Name);
		if (!excl)
			filtered = strs_append1(filtered, fields.v[i]);
	}
	return filtered;
}

/* Make a deep copy of the outer parameters that has paramName as the first component, but
 * remove the first component of the field path. */
static Params subPathParams(const char *paramName, Params outerParams)
{
	Params innerParams = {0};
	for (int i = 0; i < outerParams.len; i++) {
		const GwParameter *p = &outerParams.v[i];
		if (p->FieldPath.n > 1 && strcmp(p->FieldPath.v[0].Name, paramName) == 0) {
			GwParameter sub = {{p->FieldPath.v + 1, p->FieldPath.n - 1}, p->Target, p->Method};
			innerParams = params_append(innerParams, sub);
		}
	}
	return innerParams;
}

static Str protoComments(GwRegistry *reg, GwFile *file, const char **outers, int nOuters, const char *typeName, int32_t typeIndex,
			 const int32_t *fieldPaths, int nFieldPaths);

static openapiSchemaObject renderMessageAsDefinition(GwMessage *msg, GwRegistry *reg, StrSet *customRefs, Params pathParams);

static openapiSchemaObject renderFieldAsDefinition(GwField *f, GwRegistry *reg, StrSet *refs, Params pathParams)
{
	if (pathParams.len == 0)
		return schemaOfField(f, reg, refs);
	GwMessage *msg = lookupMsg(reg, location_of(f->Message), f->TypeName);
	if (!msg)
		fail("no message found: %s", f->TypeName);
	openapiSchemaObject schema = renderMessageAsDefinition(msg, reg, refs, pathParams);
	Str comments = fieldProtoComments(reg, f->Message, f);
	if (comments.n > 0) {
		/* Use title and description from field instead of nested message if present. */
		Str first = comments, rest = {"", 0};
		const char *sep = NULL;
		for (size_t i = 0; i + 1 < comments.n; i++)
			if (comments.p[i] == '\n' && comments.p[i + 1] == '\n') {
				sep = comments.p + i;
				break;
			}
		if (sep) {
			first = (Str){comments.p, (size_t)(sep - comments.p)};
			rest = (Str){sep + 2, comments.n - (size_t)(sep - comments.p) - 2};
		}
		Str firstParagraph = trim_space(first);
		if (!memchr(firstParagraph.p, '\n', firstParagraph.n)) {
			schema.Title = firstParagraph;
			schema.Description = trim_space(rest);
		} else {
			schema.Description = trim_space(comments);
		}
	}

	/* the behaviour of the field, as schemaOfField does without path params */
	int32_t *fb;
	int nfb = getFieldBehaviorOption(reg, f, &fb);
	updateSwaggerObjectFromFieldBehavior(&schema, fb, nfb, reg, f);
	return schema;
}

/* transformAnyForJSON replaces the properties of a google.protobuf.Any schema with "@type". */
static void transformAnyForJSON(openapiSchemaObject *schema, bool useJSONNames)
{
	const char *typeFieldName = useJSONNames ? "typeUrl" : "type_url";
	if (!schema->Properties)
		go_panic("nil pointer dereference: google.protobuf.Any without properties");
	for (int i = 0; i < schema->Properties->n; i++) {
		keyVal property = schema->Properties->v[i];
		if (str_eqc(property.Key, typeFieldName)) {
			schema->addKind = ADDPROP_ALLOWED;
			schema->Properties = xalloc(sizeof *schema->Properties);
			props_append(schema->Properties, S("@type"), *property.Value);
			break;
		}
	}
}

static Str x_go_type(GwMessage *msg)
{
	const char *goTypeName = gw_casing_JSONCamelCase(msg->Name);
	return S(xcat("{\n                \"import\": {\n                    \"package\": \"", msg->File->GoPkg.Path,
		      "\"\n                },\n                \"type\": \"", goTypeName, "\"\n            }"));
}

static openapiSchemaObject renderMessageAsDefinition(GwMessage *msg, GwRegistry *reg, StrSet *customRefs, Params pathParams)
{
	openapiSchemaObject schema = {0};
	schema.core.Type = S("object");

	if (reg->generateXGoType && msg->File->GoPkg.Path[0]) {
		schema.extensions.nonnil = true;
		schema.extensions.v = xalloc(sizeof *schema.extensions.v);
		schema.extensions.v[0] = (extension){S("x-go-type"), x_go_type(msg)};
		schema.extensions.n = 1;
	}

	Str msgComments = protoComments(reg, msg->File, msg->Outers, msg->nOuters, "MessageType", msg->Index, NULL, 0);
	updateOpenAPIDataFromComments(reg, schemaTarget(&schema), msgComments, false);
	const PbMsg *opts = getMessageOpenAPIOption(reg, msg);
	if (opts) {
		openapiSchemaObject protoSchema = openapiSchemaFromProtoSchema(opts, reg, customRefs);

		/* Warning: Make sure not to overwrite any fields already set on the schema type. */
		schema.ExternalDocs = protoSchema.ExternalDocs;
		schema.ReadOnly = protoSchema.ReadOnly;
		schema.MultipleOf = protoSchema.MultipleOf;
		schema.Maximum = protoSchema.Maximum;
		schema.ExclusiveMaximum = protoSchema.ExclusiveMaximum;
		schema.Minimum = protoSchema.Minimum;
		schema.ExclusiveMinimum = protoSchema.ExclusiveMinimum;
		schema.MaxLength = protoSchema.MaxLength;
		schema.MinLength = protoSchema.MinLength;
		schema.Pattern = protoSchema.Pattern;
		schema.core.Default = protoSchema.core.Default;
		schema.MaxItems = protoSchema.MaxItems;
		schema.MinItems = protoSchema.MinItems;
		schema.UniqueItems = protoSchema.UniqueItems;
		schema.MaxProperties = protoSchema.MaxProperties;
		schema.MinProperties = protoSchema.MinProperties;
		schema.Required = protoSchema.Required;
		schema.core.XNullable = protoSchema.core.XNullable;
		schema.extensions = protoSchema.extensions;
		if (protoSchema.core.Type.n || protoSchema.core.Ref.n)
			schema.core = protoSchema.core;
		if (protoSchema.Title.n)
			schema.Title = protoSchema.Title;
		if (protoSchema.Description.n)
			schema.Description = protoSchema.Description;
		if (protoSchema.core.Example.p)
			schema.core.Example = protoSchema.core.Example;
	}

	schema.Required = filterOutExcludedFields(schema.Required, pathParams);

	for (int i = 0; i < msg->Fields.n; i++) {
		GwField *f = msg->Fields.v[i];
		if (!isVisible(getFieldVisibilityOption(f), reg))
			continue;
		if (shouldExcludeField(f->Name, pathParams))
			continue;
		Params sub = subPathParams(f->Name, pathParams);
		openapiSchemaObject fieldSchema = renderFieldAsDefinition(f, reg, customRefs, sub);
		Str comments = fieldProtoComments(reg, msg, f);
		updateOpenAPIDataFromComments(reg, schemaTarget(&fieldSchema), comments, false);

		int requiredIdx = find(schema.Required, S(f->Name));
		if (requiredIdx != -1 && reg->useJSONNamesForFields)
			schema.Required.v[requiredIdx] = S(f->JsonName);

		if (fieldSchema.Required.v) {
			/* Only hoist required fields to parent if there are no path params inside this
			 * field. */
			if (sub.len == 0) {
				schema.Required = getUniqueFields(schema.Required, fieldSchema.Required);
				schema.Required = strs_append(schema.Required, fieldSchema.Required.v, fieldSchema.Required.len);
				/* To avoid populating both the field schema require and message schema
				 * require, unset the field schema require. See issue #2635. */
				fieldSchema.Required = (GoStrs){0};
			} else {
				/* The field name itself (if required) goes to the parent's required, nested
				 * field names stay in the nested schema's required. */
				Str fieldName = S(reg->useJSONNamesForFields ? f->JsonName : f->Name);
				GoStrs nestedRequired = {0};
				bool fieldIsRequired = false;
				for (int k = 0; k < fieldSchema.Required.len; k++) {
					if (str_equal(fieldSchema.Required.v[k], fieldName))
						fieldIsRequired = true;
					else
						nestedRequired = strs_append1(nestedRequired, fieldSchema.Required.v[k]);
				}
				if (fieldIsRequired && find(schema.Required, fieldName) == -1)
					schema.Required = strs_append1(schema.Required, fieldName);
				fieldSchema.Required = nestedRequired;
			}
		}

		if (reg->useAllOfForRefs) {
			if (fieldSchema.core.Ref.n) {
				/* Use allOf to specify Title/Description/Example/readOnly fields. */
				if (fieldSchema.Title.n || fieldSchema.Description.n || fieldSchema.core.Example.n || fieldSchema.ReadOnly ||
				    fieldSchema.core.XNullable || fieldSchema.extensions.n > 0) {
					openapiSchemaObject a = {0};
					a.Title = fieldSchema.Title;
					a.Description = fieldSchema.Description;
					a.core.Example = fieldSchema.core.Example;
					a.core.XNullable = fieldSchema.core.XNullable;
					a.ReadOnly = fieldSchema.ReadOnly;
					a.extensions = fieldSchema.extensions;
					a.AllOf = xalloc(sizeof *a.AllOf);
					a.AllOf[0] = fieldSchema.core.Ref;
					a.nAllOf = 1;
					fieldSchema = a;
				} else {
					openapiSchemaObject a = {0};
					a.core.Ref = fieldSchema.core.Ref;
					fieldSchema = a;
				}
			}
		}

		/* If this field's only sub-fields are bound to path parameters, it renders as an empty
		 * object that carries no information. Omit it from the body schema. See #2624. */
		if (sub.len > 0 && isEmptyObjectSchema(&fieldSchema)) {
			int idx = find(schema.Required, S(gw_Registry_FieldName(reg, f)));
			if (idx != -1) {
				mcopy(&schema.Required.v[idx], &schema.Required.v[idx + 1], sizeof(Str) * (size_t)(schema.Required.len - idx - 1));
				schema.Required.len--;
			}
			continue;
		}

		if (!schema.Properties)
			schema.Properties = xalloc(sizeof *schema.Properties);
		props_append(schema.Properties, S(gw_Registry_FieldName(reg, f)), fieldSchema);
	}

	if (strcmp(message_fqmn(msg), ".google.protobuf.Any") == 0)
		transformAnyForJSON(&schema, reg->useJSONNamesForFields);
	return schema;
}

/* Sort keys so that when two messages flatten to the same OpenAPI definition name the winner is
 * deterministic (last in sorted order wins). */
static void renderMessagesAsDefinition(const NameMap *messages, openapiDefinitionsObject *d, GwRegistry *reg, StrSet *customRefs,
				       Params pathParams)
{
	const char **names = nm_sorted(messages);
	for (int i = 0; i < messages->keys.n; i++) {
		const char *name = names[i];
		GwMessage *msg = map_get(&messages->m, name);
		const char *swgName = openAPIName(message_fqmn(msg), reg);
		if (!swgName)
			fail("can't resolve OpenAPI name from %s", go_quote(S(message_fqmn(msg))).p);
		if (skipRenderingRef(name))
			continue;
		if (is_map_entry(msg))
			continue;
		/* grpclog.Warningf on a collision */
		openapiSchemaObject s = renderMessageAsDefinition(msg, reg, customRefs, pathParams);
		defs_set(d, S(swgName), s);
	}
}

/* isVisible checks if a field/RPC is visible based on the visibility restriction combined with
 * the `visibility_restriction_selectors`. Elements without `google.api.VisibilityRule`
 * annotations entirely are always visible. */
static bool isVisible(VisibilityRule r, const GwRegistry *reg)
{
	if (!r.present)
		return true;
	Str *restrictions;
	int n = str_split_s(trim_space(r.Restriction), ',', &restrictions);
	for (int i = 0; i < n; i++)
		if (gw_Registry_IsVisibilityRestrictionSelector(reg, trim_space(restrictions[i])))
			return true;
	return false;
}

/* renderEnumerationsAsDefinition inserts enums into the definitions object. */
static void renderEnumerationsAsDefinition(const NameMap *enums, openapiDefinitionsObject *d, GwRegistry *reg, StrSet *customRefs)
{
	const char **keys = nm_sorted(enums);
	for (int i = 0; i < enums->keys.n; i++) {
		GwEnum *e = map_get(&enums->m, keys[i]);
		const char *swgName = openAPIName(gw_Enum_FQEN(e), reg);
		if (!swgName)
			go_panic(xcat("can't resolve OpenAPI name from FQEN ", go_quote(S(gw_Enum_FQEN(e))).p));
		Str enumComments = protoComments(reg, e->File, e->Outers, e->nOuters, "EnumType", e->Index, NULL, 0);

		/* it may be necessary to sort the result of the GetValue function. */
		Any enumNames = listEnumNames(reg, e);
		Any defaultValue = getEnumDefault(reg, e);
		Str valueComments = enumValueProtoComments(reg, e);
		if (valueComments.n) {
			Str c = cat3(enumComments, S("\n\n "), valueComments);
			while (c.n && c.p[0] == '\n')
				c.p++, c.n--;
			enumComments = c;
		}
		openapiSchemaObject enumSchemaObject = {0};
		enumSchemaObject.core.Type = S("string");
		enumSchemaObject.core.Enum = enumNames;
		enumSchemaObject.core.Default = defaultValue;

		if (reg->enumsAsInts) {
			enumSchemaObject.core.Type = S("integer");
			enumSchemaObject.core.Format = S("int32");
			enumSchemaObject.core.Default = getEnumDefaultNumber(reg, e);
			enumSchemaObject.core.Enum = listEnumNumbers(reg, e);
		}
		const PbMsg *opts = getEnumOpenAPIOption(reg, e);
		if (opts) {
			openapiSchemaObject protoSchema = openapiSchemaFromProtoEnumSchema(opts, reg, customRefs);
			/* only a subset of the fields from JsonSchema: most of them only apply to arrays or
			 * objects, not enums */
			enumSchemaObject.ExternalDocs = protoSchema.ExternalDocs;
			enumSchemaObject.ReadOnly = protoSchema.ReadOnly;
			enumSchemaObject.extensions = protoSchema.extensions;
			if (protoSchema.core.Type.n || protoSchema.core.Ref.n)
				enumSchemaObject.core = protoSchema.core;
			if (protoSchema.Title.n)
				enumSchemaObject.Title = protoSchema.Title;
			if (protoSchema.Description.n)
				enumSchemaObject.Description = protoSchema.Description;
			if (protoSchema.core.Example.p)
				enumSchemaObject.core.Example = protoSchema.core.Example;
		}
		updateOpenAPIDataFromComments(reg, schemaTarget(&enumSchemaObject), enumComments, false);

		/* Enum comments should go to Description, not Title. */
		if (enumComments.n && !enumSchemaObject.Description.n && enumSchemaObject.Title.n) {
			enumSchemaObject.Description = enumSchemaObject.Title;
			enumSchemaObject.Title = S("");
		}
		/* grpclog.Warningf on a collision */
		defs_set(d, S(swgName), enumSchemaObject);
	}
}

/* addCustomRefs adds the definitions of references added by users that aren't otherwise
 * rendered, again until no new references appear. */
static void addCustomRefs(openapiDefinitionsObject *d, GwRegistry *reg, StrSet *refs)
{
	for (;;) {
		if (set_len(refs) == 0)
			return;
		NameMap msgMap = {0}, enumMap = {0};
		bool progress = false;
		int ndefs = d->n;
		int n = refs->keys.n;
		for (int i = 0; i < n; i++) {
			if (!refs->keys.v[i])
				continue;
			Str ref = *(Str *)refs->keys.v[i];
			const char *swgName = fullyQualifiedNameToOpenAPIName(ref, reg);
			if (!swgName)
				continue; /* grpclog.Errorf */
			if (defs_get(d, S(swgName))) {
				/* Skip already existing definitions */
				set_del(refs, ref);
				progress = true;
				continue;
			}
			const char *name = xstrndup(ref.p, ref.n);
			GwMessage *msg = lookupMsg(reg, "", name);
			if (msg) {
				nm_put(&msgMap, swgName, msg);
				continue;
			}
			GwEnum *e = lookupEnum(reg, "", name);
			if (e) {
				nm_put(&enumMap, swgName, e);
				continue;
			}
		}
		renderMessagesAsDefinition(&msgMap, d, reg, refs, (Params){0});
		renderEnumerationsAsDefinition(&enumMap, d, reg, refs);
		/* Upstream recurses until no reference is left: forever if a reference stays without
		 * a definition. */
		if (!progress && d->n == ndefs)
			go_panic("goroutine stack exceeds limit: addCustomRefs recurses forever on a reference without "
				 "a definition");
	}
}

/* ---- path templates ---- */

/* lowerCamelCase: the JSON name of a field (path), looked up in fields, else in the messages of
 * the file (their fields by short message name), else casing.JSONCamelCase. The maps of msgs are
 * built once per message list (upstream builds them on every call). */
struct jsonNameMaps {
	const Vec *msgs;
	int n;
	Map messageNameToFieldsToJSONName; /* message name -> Map (field name -> JSON name) */
	Map fieldNameToType;               /* field name -> type name */
};

static jsonNameMaps *json_name_maps(const Vec *msgs)
{
	jsonNameMaps *cache = jsonNameMapsCache;
	if (cache && cache->msgs == msgs && cache->n == msgs->n)
		return cache;
	cache = jsonNameMapsCache = xalloc(sizeof *cache);
	cache->msgs = msgs;
	cache->n = msgs->n;
	for (int i = 0; i < msgs->n; i++) {
		GwMessage *msg = msgs->v[i];
		Map *fieldNameToJSONName = xalloc(sizeof *fieldNameToJSONName);
		for (int k = 0; k < pb_count(msg->proto, DP_FIELD); k++) {
			const PbMsg *f = pb_msg_at(msg->proto, DP_FIELD, k);
			map_put(fieldNameToJSONName, pb_str(f, FD_NAME), (void *)pb_str(f, FD_JSON_NAME));
			map_put(&cache->fieldNameToType, pb_str(f, FD_NAME), (void *)pb_str(f, FD_TYPE_NAME));
		}
		map_put(&cache->messageNameToFieldsToJSONName, msg->Name, fieldNameToJSONName);
	}
	return cache;
}

static Str getReservedJSONName(Str fieldName, jsonNameMaps *maps)
{
	Str *fieldNames;
	int n = str_split_s(fieldName, '.', &fieldNames);
	while (n > 2) {
		fieldNames++;
		n--;
	}
	if (n < 2)
		return S("");
	const char *firstVariable = xstrndup(fieldNames[0].p, fieldNames[0].n);
	const char *firstType = map_get(&maps->fieldNameToType, firstVariable);
	if (!firstType)
		firstType = "";
	const char *dot = strrchr(firstType, '.');
	const char *firstTypeShortName = dot ? dot + 1 : firstType;
	Map *m = map_get(&maps->messageNameToFieldsToJSONName, firstTypeShortName);
	const char *j = m ? map_get(m, xstrndup(fieldNames[1].p, fieldNames[1].n)) : NULL;
	return S(j ? j : "");
}

static Str lowerCamelCase(Str fieldName, const Vec *fields, const Vec *msgs)
{
	for (int i = 0; i < fields->n; i++) {
		GwField *f = fields->v[i];
		if (str_eqc(fieldName, f->Name))
			return S(f->JsonName);
	}
	if (memchr(fieldName.p, 0, fieldName.n))
		return S(gw_casing_JSONCamelCase(xstrndup(fieldName.p, fieldName.n)));
	if (memchr(fieldName.p, '.', fieldName.n)) {
		jsonNameMaps *maps = json_name_maps(msgs);
		Str *fieldNames;
		int n = str_split_s(fieldName, '.', &fieldNames);
		Buf prefix = {0};
		for (int i = 0; i < n - 1; i++) {
			if (i)
				buf_addc(&prefix, '.');
			buf_adds(&prefix, gw_casing_JSONCamelCase(xstrndup(fieldNames[i].p, fieldNames[i].n)));
		}
		Str reservedJSONName = getReservedJSONName(fieldName, maps);
		if (reservedJSONName.n)
			return cat3(buf_view(&prefix), S("."), reservedJSONName);
	}
	return S(gw_casing_JSONCamelCase(xstrndup(fieldName.p, fieldName.n)));
}

/* Str vector (a []string) */
typedef struct {
	Str *v;
	int n, cap;
} Strs;

static void strs_push(Strs *s, Str x)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 8;
		s->v = xrealloc(s->v, sizeof *s->v * (size_t)s->cap, sizeof *s->v * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = x;
}

/* templateToParts splits a URL template into path segments, with the parser of
 * protoc-gen-grpc-gateway. The first part is always empty (the leading "/"), and if the template
 * has a verb, the last part is the verb prefixed with ":". */
static Strs templateToParts(Str path, GwRegistry *reg, const Vec *fields, const Vec *msgs)
{
	GwSegment *segments;
	int nsegments;
	Str verb, err;
	if (!gw_httprule_SplitTemplate(path, &segments, &nsegments, &verb, &err))
		go_panic("invalid path template");
	Strs parts = {0};
	strs_push(&parts, S(""));
	if (nsegments == 0) /* The root path "/". */
		strs_push(&parts, S(""));
	for (int i = 0; i < nsegments; i++) {
		GwSegment *segment = &segments[i];
		if (!segment->FieldPath.n) {
			strs_push(&parts, segment->Value);
			continue;
		}
		Str name = segment->FieldPath;
		if (reg->useJSONNamesForFields)
			name = lowerCamelCase(name, fields, msgs);
		if (str_eqc(segment->Value, "*")) {
			/* "{name}" is equivalent to "{name=*}". */
			strs_push(&parts, cat3(S("{"), name, S("}")));
			continue;
		}
		strs_push(&parts, cat(cat3(S("{"), name, S("=")), cat(segment->Value, S("}"))));
	}
	/* A trailing ":" is parsed as an empty verb; keep it in the path. */
	if (verb.n || (path.n && path.p[path.n - 1] == ':'))
		strs_push(&parts, cat(S(":"), verb));
	return parts;
}

static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* canRegexp = {([a-zA-Z][a-zA-Z0-9_.-]*)([^}]*)}: a match at s[i] (s[i] == '{'): its end, and
 * the end of group 1; 0 if none */
static size_t can_match(Str s, size_t i, size_t *g1end)
{
	size_t j = i + 1;
	if (j >= s.n || !is_alpha(s.p[j]))
		return 0;
	j++;
	while (j < s.n && (is_alpha(s.p[j]) || (s.p[j] >= '0' && s.p[j] <= '9') || s.p[j] == '_' || s.p[j] == '.' || s.p[j] == '-'))
		j++;
	*g1end = j;
	while (j < s.n && s.p[j] != '}')
		j++;
	return j < s.n ? j + 1 : 0;
}

/* canRegexp.ReplaceAllString(part, "{$1}") */
static Str canReplace(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		size_t g1end, end;
		if (s.p[i] == '{' && (end = can_match(s, i, &g1end))) {
			buf_add(&b, s.p + i, g1end - i);
			buf_addc(&b, '}');
			i = end;
			continue;
		}
		buf_addc(&b, s.p[i]);
		i++;
	}
	return (Str){b.p ? b.p : "", b.n};
}

/* a map[string]string of path parameter names */
typedef struct {
	Str *keys, *values;
	int n, cap;
} StrMap;

static Str *strmap_get(const StrMap *m, Str k)
{
	for (int i = m->n - 1; i >= 0; i--)
		if (str_equal(m->keys[i], k))
			return &m->values[i];
	return NULL;
}

static void strmap_set(StrMap *m, Str k, Str v)
{
	Str *p = strmap_get(m, k);
	if (p) {
		*p = v;
		return;
	}
	if (m->n == m->cap) {
		int cap = m->cap ? m->cap * 2 : 8;
		m->keys = xrealloc(m->keys, sizeof *m->keys * (size_t)m->cap, sizeof *m->keys * (size_t)cap);
		m->values = xrealloc(m->values, sizeof *m->values * (size_t)m->cap, sizeof *m->values * (size_t)cap);
		m->cap = cap;
	}
	m->keys[m->n] = k;
	m->values[m->n++] = v;
}

/* partsToOpenAPIPath converts each path part of the form /path/{string_value=strprefix/ *} to the
 * OpenAPI expected form /path/{string_value}. It changes parts in place. */
static Str partsToOpenAPIPath(Strs parts, const StrMap *overrides)
{
	for (int i = 0; i < parts.n; i++) {
		Str part = canReplace(parts.v[i]);
		Str *override = strmap_get(overrides, part);
		if (override)
			part = *override;
		parts.v[i] = part;
	}
	bool hasTrailingSlash = parts.n > 0 && parts.v[parts.n - 1].n == 0;
	if (hasTrailingSlash)
		parts.n--;
	Buf b = {0};
	int last = parts.n - 1;
	bool verb = last >= 0 && parts.v[last].n && parts.v[last].p[0] == ':';
	int njoin = verb ? last : parts.n;
	for (int i = 0; i < njoin; i++) {
		if (i)
			buf_addc(&b, '/');
		buf_add_str(&b, parts.v[i]);
	}
	if (verb)
		buf_add_str(&b, parts.v[last]);
	if (hasTrailingSlash)
		buf_addc(&b, '/');
	return (Str){b.p ? b.p : "", b.n};
}

/* strings.ReplaceAll */
static Str replace_all(Str s, const char *from, const char *to)
{
	Buf b = {0};
	size_t fn = strlen(from);
	for (size_t i = 0; i < s.n;) {
		if (i + fn <= s.n && memcmp(s.p + i, from, fn) == 0) {
			buf_adds(&b, to);
			i += fn;
		} else {
			buf_addc(&b, s.p[i++]);
		}
	}
	return (Str){b.p ? b.p : "", b.n};
}

/* strings.ReplaceAll with Str arguments (old non-empty) */
static Str replace_all_s(Str s, Str from, Str to)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		if (from.n && i + from.n <= s.n && memcmp(s.p + i, from.p, from.n) == 0) {
			buf_add_str(&b, to);
			i += from.n;
		} else {
			buf_addc(&b, s.p[i++]);
		}
	}
	return (Str){b.p ? b.p : "", b.n};
}

/* partsToRegexpMap returns a map of parameter name to ECMA 262 patterns: "*" matches a path
 * element, "**" any characters. */
static StrMap partsToRegexpMap(Strs parts)
{
	StrMap regExps = {0};
	for (int i = 0; i < parts.n; i++) {
		Str part = parts.v[i];
		/* grpclog.Warningf if the part contains '/' */
		for (size_t k = 0; k < part.n; k++) {
			size_t g1end, end;
			if (part.p[k] != '{' || !(end = can_match(part, k, &g1end)))
				continue;
			Str name = {part.p + k + 1, g1end - k - 1};
			Str rest = {part.p + g1end, end - 1 - g1end};
			if (rest.n && rest.p[0] == '=') {
				/* assume the string's characters other than "**" and "*" are literals */
				Str regex = {rest.p + 1, rest.n - 1};
				regex = replace_all(regex, "**", ".+");
				regex = replace_all(regex, "*", "[^/]+");
				strmap_set(&regExps, name, regex);
			}
			break;
		}
	}
	return regExps;
}

static void updateTagFromComments(GwRegistry *reg, openapiTagObject *tag, Str comment);

/* findServiceIndex finds the index of a service within its file's service list. */
static int findServiceIndex(const GwService *svc)
{
	for (int i = 0; i < svc->File->Services.n; i++)
		if (svc->File->Services.v[i] == svc)
			return i;
	return -1;
}

static Tags renderServiceTags(Vec services, GwRegistry *reg)
{
	Tags tags = {0};
	tags.v = xalloc(sizeof *tags.v * (size_t)(services.n + 1));
	for (int i = 0; i < services.n; i++) {
		GwService *svc = services.v[i];
		if (!isVisible(getServiceVisibilityOption(svc), reg))
			continue;
		Str tagName = S(svc->Name);
		if (svc->File->package[0] && reg->includePackageInTags)
			tagName = S(xcat(svc->File->package, ".", svc->Name));
		openapiTagObject tag = {.Name = tagName};
		const PbMsg *opts = getServiceOpenAPIOption(reg, svc);
		if (opts) {
			tag.Description = pb_bytes(opts, OA_Tag_description);
			const PbMsg *ed = pb_msg(opts, OA_Tag_external_docs);
			if (ed) {
				tag.ExternalDocs = xalloc(sizeof *tag.ExternalDocs);
				tag.ExternalDocs->Description = pb_bytes(ed, OA_ExternalDocumentation_description);
				tag.ExternalDocs->URL = pb_bytes(ed, OA_ExternalDocumentation_url);
			}
			if (pb_bytes(opts, OA_Tag_name).n)
				tag.Name = pb_bytes(opts, OA_Tag_name);
		}
		/* If no description is set from options, use proto comments */
		if (!tag.Description.n) {
			int svcIdx = findServiceIndex(svc);
			if (svcIdx >= 0) {
				Str svcComments = protoComments(reg, svc->File, NULL, 0, "Service", svcIdx, NULL, 0);
				updateTagFromComments(reg, &tag, svcComments);
			}
		}
		tags.v[tags.n++] = tag;
	}
	return tags;
}

/* expandPathPatterns searches the URI parts for path parameters with pattern and when the
 * pattern contains a sub-path, it expands the pattern into the URI parts and adds the new path
 * parameters to the pathParams slice (whose backing array it shares, as upstream does). */
static Strs expandPathPatterns(Strs pathParts, Params *pathParams, GwRegistry *reg)
{
	Strs expandedPathParts = {0};
	Params modifiedPathParams = *pathParams;
	for (int i = 0; i < pathParts.n; i++) {
		Str pathPart = pathParts.v[i];
		if (!(pathPart.n && pathPart.p[0] == '{') || !(pathPart.n && pathPart.p[pathPart.n - 1] == '}') || pathPart.n < 2) {
			if (pathPart.n == 1 && pathPart.p[0] == '{') {
				/* "{" has both prefix and suffix in Go only if it is "{...}" of length >= 2 */
			}
			strs_push(&expandedPathParts, pathPart);
			continue;
		}
		Str woBraces = {pathPart.p + 1, pathPart.n - 2};
		const char *eq = memchr(woBraces.p, '=', woBraces.n);
		if (!eq) {
			strs_push(&expandedPathParts, pathPart);
			continue;
		}
		Str paramName = {woBraces.p, (size_t)(eq - woBraces.p)};
		Str pattern = {eq + 1, woBraces.n - (size_t)(eq - woBraces.p) - 1};
		if (str_eqc(pattern, "*")) {
			strs_push(&expandedPathParts, pathPart);
			continue;
		}
		int pathParamIndex = -1;
		for (int k = 0; k < modifiedPathParams.len; k++) {
			const char *fp = gw_FieldPath_String(modifiedPathParams.v[k].FieldPath);
			if (reg->useJSONNamesForFields)
				fp = gw_casing_JSONCamelCase(fp);
			if (str_eqc(paramName, fp)) {
				pathParamIndex = k;
				break;
			}
		}
		if (pathParamIndex == -1)
			go_panic(xcat("Path parameter ", go_quote(paramName).p, " not found in path parameters"));
		GwParameter pathParam = modifiedPathParams.v[pathParamIndex];
		Str *patternParts;
		int np = str_split_s(pattern, '/', &patternParts);
		for (int k = 0; k < np; k++) {
			Str patternPart = patternParts[k];
			if (!str_eqc(patternPart, "*")) {
				strs_push(&expandedPathParts, patternPart);
				continue;
			}
			Str lastPart = expandedPathParts.v[expandedPathParts.n - 1];
			Str pn = lastPart;
			if (pn.n && pn.p[pn.n - 1] == 's')
				pn.n--;
			const char *pname = xstrndup(pn.p, pn.n);
			if (reg->useJSONNamesForFields)
				pname = gw_casing_JSONCamelCase(pname);
			strs_push(&expandedPathParts, S(xcat("{", pname, "}")));
			GwField *target = xalloc(sizeof *target);
			target->proto = pb_new(&pbd_FieldDescriptorProto);
			pb_set_str(target->proto, FD_NAME, pname);
			target->Name = pname;
			target->Type = PT_STRING;
			target->TypeName = "";
			target->JsonName = "";
			target->Message = pathParam.Target->Message;
			target->FieldMessage = pathParam.Target->FieldMessage;
			target->ForcePrefixedName = pathParam.Target->ForcePrefixedName;
			GwFieldPathComponent *c = xalloc(sizeof *c);
			c->Name = pname;
			GwParameter newParam = {{c, 1}, target, NULL};
			modifiedPathParams = params_append(modifiedPathParams, newParam);
			if (pathParamIndex != -1) {
				/* the new parameter from the pattern replaces the old path parameter */
				modifiedPathParams = params_remove(modifiedPathParams, pathParamIndex);
				pathParamIndex = -1;
			}
		}
	}
	*pathParams = modifiedPathParams;
	return expandedPathParts;
}

/* ---- headers ---- */

/* textproto.CanonicalMIMEHeaderKey */
static bool validHeaderFieldByte(uint8_t c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static Str CanonicalMIMEHeaderKey(Str s)
{
	bool upper = true, need = false;
	for (size_t i = 0; i < s.n; i++) {
		uint8_t c = (uint8_t)s.p[i];
		if (!validHeaderFieldByte(c))
			return s;
		if ((upper && c >= 'a' && c <= 'z') || (!upper && c >= 'A' && c <= 'Z')) {
			need = true;
			break;
		}
		upper = c == '-';
	}
	if (!need)
		return s;
	/* canonicalMIMEHeaderKey: a space or an invalid byte leaves it unchanged */
	for (size_t i = 0; i < s.n; i++)
		if (!validHeaderFieldByte((uint8_t)s.p[i]))
			return s;
	char *a = xstrndup(s.p, s.n);
	upper = true;
	for (size_t i = 0; i < s.n; i++) {
		char c = a[i];
		if (upper && c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		else if (!upper && c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		a[i] = c;
		upper = c == '-';
	}
	return (Str){a, s.n};
}

static void validateHeaderTypeAndFormat(Str headerType, Str format)
{
	static const char *const number[] = {"uint",  "uint8",	 "uint16",  "uint32",	  "uint64",	"int",	"int8",
					     "int16", "int32",	 "int64",   "float",	  "float32",	"float64", "complex64",
					     "complex128", "double", "byte", "rune", "uintptr", "", NULL};
	static const char *const integer[] = {"uint", "uint8", "uint16", "uint32", "uint64", "int",
					      "int8", "int16", "int32",	 "int64",  "",	     NULL};
	const char *const *formats = NULL;
	if (str_eqc(headerType, "string") || str_eqc(headerType, "boolean"))
		return;
	if (str_eqc(headerType, "number"))
		formats = number;
	else if (str_eqc(headerType, "integer"))
		formats = integer;
	else
		fail("the provided header type %s is not supported", go_quote(headerType).p);
	for (const char *const *f = formats; *f; f++)
		if (str_eqc(format, *f))
			return;
	fail("the provided format %s is not a valid extension of the type %s", go_quote(format).p, go_quote(headerType).p);
}

static bool isQuotedString(Str s) { return s.n >= 2 && s.p[0] == '"' && s.p[s.n - 1] == '"'; }

/* strings.Trim(s, `"`) */
static Str trim_quotes(Str s)
{
	while (s.n && s.p[0] == '"')
		s.p++, s.n--;
	while (s.n && s.p[s.n - 1] == '"')
		s.n--;
	return s;
}

static void validateDefaultValueTypeAndFormat(Str headerType, Str defaultValue, Str format)
{
	const char *qd = go_quote(defaultValue).p, *qt = go_quote(headerType).p;
	if (str_eqc(headerType, "string")) {
		if (!isQuotedString(defaultValue))
			fail("the provided default value %s does not match provider type %s, or is not properly quoted with "
			     "escaped quotations",
			     qd, qt);
		if (str_eqc(format, "date-time")) {
			if (!go_std_time_parse("2006-01-02T15:04:05Z07:00", trim_quotes(defaultValue)))
				fail("the provided default value %s is not a valid RFC3339 date-time string", qd);
		} else if (str_eqc(format, "date")) {
			if (!go_std_time_parse("2006-01-02", trim_quotes(defaultValue)))
				fail("the provided default value %s is not a valid RFC3339 date-time string", qd);
		}
	} else if (str_eqc(headerType, "number")) {
		double val;
		if (go_std_parse_float(defaultValue.p, defaultValue.n, 64, &val))
			fail("the provided default value %s does not match provider type %s", qd, qt);
		/* Floating point values that cannot be represented as sequences of digits (such as
		 * Infinity and NaN) are not permitted. */
		if (val != val || val > 1.7976931348623157e308 || val < -1.7976931348623157e308)
			fail("the provided number %s is not a valid JSON number", qd);
	} else if (str_eqc(headerType, "integer")) {
		int64_t i;
		uint64_t u;
		const char *qf = go_quote(format).p;
		if (str_eqc(format, "int32")) {
			if (go_std_parse_int(defaultValue.p, defaultValue.n, 0, 32, &i))
				fail("the provided default value %s does not match provided format %s", qd, qf);
		} else if (str_eqc(format, "uint32")) {
			if (go_std_parse_uint(defaultValue.p, defaultValue.n, 0, 32, &u))
				fail("the provided default value %s does not match provided format %s", qd, qf);
		} else if (str_eqc(format, "int64")) {
			if (go_std_parse_int(defaultValue.p, defaultValue.n, 0, 64, &i))
				fail("the provided default value %s does not match provided format %s", qd, qf);
		} else if (str_eqc(format, "uint64")) {
			if (go_std_parse_uint(defaultValue.p, defaultValue.n, 0, 64, &u))
				fail("the provided default value %s does not match provided format %s", qd, qf);
		} else {
			if (go_std_parse_int(defaultValue.p, defaultValue.n, 0, 64, &i))
				fail("the provided default value %s does not match provided type %s", qd, qt);
		}
	} else if (str_eqc(headerType, "boolean")) {
		/* type: boolean represents two values: true and false */
		if (!str_eqc(defaultValue, "true") && !str_eqc(defaultValue, "false"))
			fail("the provided default value %s does not match provider type %s", qd, qt);
	}
}

/* processHeaders: the headers by canonical MIME key (the entries are checked in key order;
 * upstream visits its map in random order) */
static openapiHeadersObject processHeaders(const PbMsg *resp)
{
	GwMapEntry *e;
	int n = gw_options_map(resp, OA_Response_headers, &e);
	openapiHeadersObject hdrs = {xalloc(sizeof *hdrs.v * (size_t)(n + 1)), 0, true};
	for (int i = 0; i < n; i++) {
		const PbMsg *v = pb_msg(e[i].value, 2);
		Str header = CanonicalMIMEHeaderKey(e[i].key);
		openapiHeaderObject ret = {
			.Description = pb_bytes(v, OA_Header_description),
			.Format = pb_bytes(v, OA_Header_format),
			.Pattern = pb_bytes(v, OA_Header_pattern),
		};
		Str type = pb_bytes(v, OA_Header_type), def = pb_bytes(v, OA_Header_default);
		validateHeaderTypeAndFormat(type, ret.Format);
		ret.Type = type;
		if (def.n) {
			validateDefaultValueTypeAndFormat(type, def, ret.Format);
			ret.Default = def;
		}
		int k = 0;
		while (k < hdrs.n && !str_equal(hdrs.v[k].key, header))
			k++;
		if (k == hdrs.n)
			hdrs.n++;
		hdrs.v[k] = (headerEntry){header, ret};
	}
	return hdrs;
}

/* ---- comments ---- */

/* strings.TrimRight(s, " \t") */
static Str trim_right_blank(Str s)
{
	while (s.n && (s.p[s.n - 1] == ' ' || s.p[s.n - 1] == '\t'))
		s.n--;
	return s;
}

static const char *str_index(Str s, const char *sub)
{
	size_t n = strlen(sub);
	for (size_t i = 0; i + n <= s.n; i++)
		if (memcmp(s.p + i, sub, n) == 0)
			return s.p + i;
	return NULL;
}

/* removeInternalComments removes the substrings that start with "(--" and end with "--)". */
static Str removeInternalComments(Str comment)
{
	Buf c = {0};
	while (comment.n > 0) {
		const char *open = str_index(comment, "(--");
		if (!open) {
			buf_add_str(&c, comment);
			break;
		}
		Str ex = trim_right_blank((Str){comment.p, (size_t)(open - comment.p)});
		/* Trim only one line prior to all spaces */
		if (ex.n >= 2 && ex.p[ex.n - 2] == '\r' && ex.p[ex.n - 1] == '\n')
			ex.n -= 2;
		else if (ex.n >= 1 && ex.p[ex.n - 1] == '\n')
			ex.n -= 1;
		if (ex.n)
			buf_add_str(&c, ex);
		comment = (Str){open + 3, comment.n - (size_t)(open + 3 - comment.p)};
		const char *close = str_index(comment, "--)");
		if (!close)
			break;
		comment = (Str){close + 3, comment.n - (size_t)(close + 3 - comment.p)};
	}
	return (Str){c.p ? c.p : "", c.n};
}

static bool contains(Str s, const char *sub) { return str_index(s, sub) != NULL; }

/* updateOpenAPIDataFromComments updates a OpenAPI object based on a comment from the proto
 * file: the first paragraph is the summary (or the title, if the object has no summary and the
 * paragraph does not end with a period), the others the description. */
static void updateOpenAPIDataFromComments(GwRegistry *reg, commentTarget t, Str comment, bool isPackageObject)
{
	if (comment.n == 0)
		return;
	/* Checks whether the "ignore_comments" flag is set to true */
	if (reg->ignoreComments)
		return;
	/* Checks whether the "remove_internal_comments" flag is set to true */
	if (reg->removeInternalComments)
		comment = removeInternalComments(comment);

	if (t.readOnly && contains(comment, "Output only."))
		*t.readOnly = true;

	if (t.summary) {
		Str first = comment, rest = {"", 0};
		const char *sep = str_index(comment, paragraphDeliminator);
		if (sep) {
			first = (Str){comment.p, (size_t)(sep - comment.p)};
			rest = (Str){sep + 2, comment.n - (size_t)(sep + 2 - comment.p)};
		}
		Str summary = trim_space(first);
		Str description = trim_space(rest);
		if (!t.usingTitle || (summary.n > 0 && summary.p[summary.n - 1] != '.')) {
			/* overrides the schema value only if it's empty; keep the comment precedence when
			 * updating the package definition */
			if (t.summary->n == 0 || isPackageObject)
				*t.summary = summary;
			if (description.n > 0) {
				if (t.description->n == 0 || isPackageObject)
					*t.description = description;
			}
			return;
		}
	}
	/* There was no summary field on the swaggerObject. Try to apply the whole comment into
	 * description if the OpenAPI object description is empty. */
	if (t.description->n == 0 || isPackageObject)
		*t.description = comment;
}

static void updateTagFromComments(GwRegistry *reg, openapiTagObject *tag, Str comment)
{
	updateOpenAPIDataFromComments(reg, (commentTarget){NULL, false, &tag->Description, NULL}, comment, false);
}

/* The locations of a file by path (the first of each path), built once per file. */
static const PbMsg *location_by_path(GwFile *file, const int32_t *path, int n)
{
	const char *fk = ptr_key(file->proto);
	Map *index = map_get(&locationIndexes, fk);
	if (!index) {
		index = xalloc(sizeof *index);
		const PbMsg *sci = pb_msg(file->proto, FDP_SOURCE_CODE_INFO);
		for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
			const PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
			Buf k = {0};
			for (int j = 0; j < pb_count(loc, LOC_PATH); j++)
				buf_fmt(&k, "%lld,", (long long)(int32_t)pb_int_at(loc, LOC_PATH, j));
			const char *key = buf_str(&k);
			if (!map_has(index, key))
				map_put(index, key, (void *)loc);
			/* the package comments: the first location whose path starts with 2 */
			if (pb_count(loc, LOC_PATH) > 0 && pb_int_at(loc, LOC_PATH, 0) == 2 && !map_has(index, "package"))
				map_put(index, "package", (void *)loc);
		}
		map_put(&locationIndexes, fk, index);
	}
	if (!path)
		return map_get(index, "package");
	Buf k = {0};
	for (int j = 0; j < n; j++)
		buf_fmt(&k, "%lld,", (long long)path[j]);
	return map_get(index, buf_str(&k));
}

/* protoComments: the comments of a declaration (typeName "MessageType", "EnumType", "Service"
 * or "Package"), found by its source path. */
static Str protoComments(GwRegistry *reg, GwFile *file, const char **outers, int nOuters, const char *typeName, int32_t typeIndex,
			 const int32_t *fieldPaths, int nFieldPaths)
{
	if (!pb_has(file->proto, FDP_SOURCE_CODE_INFO))
		return S(""); /* stderr: "<file> descriptor.File should not contain nil SourceCodeInfo" */

	int32_t *path = xalloc(sizeof *path * (size_t)(2 * nOuters + 2 + nFieldPaths + 1));
	int n = 0;
	for (int i = 0; i < nOuters; i++) {
		const char *location = file->has_package ? file->package : "";
		Buf name = {0};
		for (int k = 0; k <= i; k++) {
			if (k)
				buf_addc(&name, '.');
			buf_adds(&name, outers[k]);
		}
		GwMessage *msg = lookupMsg(reg, location, buf_str(&name));
		if (!msg)
			go_panic(xcat("no message found: ", buf_str(&name)));
		path[n++] = i == 0 ? 4 : 3; /* FileDescriptorProto.message_type, DescriptorProto.nested_type */
		path[n++] = msg->Index;
	}
	const PbMsg *loc;
	if (strcmp(typeName, "Package") == 0) {
		/* path for package comments is just [2] */
		loc = location_by_path(file, NULL, 0);
	} else {
		int32_t field;
		if (strcmp(typeName, "MessageType") == 0)
			field = nOuters ? 3 : 4;
		else if (strcmp(typeName, "EnumType") == 0)
			field = nOuters ? 4 : 5;
		else /* Service */
			field = nOuters ? -1 : 6;
		path[n++] = field;
		path[n++] = typeIndex;
		for (int i = 0; i < nFieldPaths; i++)
			path[n++] = fieldPaths[i];
		loc = location_by_path(file, path, n);
	}
	if (!loc)
		return S("");
	Str comments = {"", 0};
	if (pb_has(loc, LOC_LEADING)) {
		/* Comments from a proto file with CRLF line endings keep the \r. */
		comments = replace_all(pb_bytes(loc, LOC_LEADING), "\r\n", "\n");
		while (comments.n && comments.p[comments.n - 1] == '\n')
			comments.n--;
		comments = trim_space(comments);
		/* TODO(ivucica): this is a hack to fix "// " being interpreted as "//". */
		comments = replace_all(comments, "\n ", "\n");
		comments = removeInternalComments(comments);
	}
	if (pb_has(loc, LOC_TRAILING)) {
		Str trailing = trim_space(replace_all(pb_bytes(loc, LOC_TRAILING), "\r\n", "\n"));
		if (comments.n == 0)
			comments = trailing;
		else
			comments = cat3(comments, S("\n\n"), trailing);
	}
	return comments;
}

static Str fieldProtoComments(GwRegistry *reg, GwMessage *msg, GwField *field)
{
	for (int i = 0; i < msg->Fields.n; i++)
		if (msg->Fields.v[i] == field) {
			int32_t fp[2] = {2, i}; /* DescriptorProto.field */
			return protoComments(reg, msg->File, msg->Outers, msg->nOuters, "MessageType", msg->Index, fp, 2);
		}
	return S("");
}

static Str enumValueProtoComments(GwRegistry *reg, GwEnum *e)
{
	Strs comments = {0};
	for (int idx = 0; idx < pb_count(e->proto, EDP_VALUE); idx++) {
		const PbMsg *value = pb_msg_at(e->proto, EDP_VALUE, idx);
		if (reg->omitEnumDefaultValue && pb_int(value, EVDP_NUMBER, 0) == 0)
			continue;
		if (!isVisible(getEnumValueVisibilityOption(value), reg))
			continue;
		Str name = pb_bytes(value, EVDP_NAME);
		if (reg->enumsAsInts)
			name = S(xfmt("%d", (int)(int32_t)pb_int(value, EVDP_NUMBER, 0)));
		int32_t fp[2] = {2, idx}; /* EnumDescriptorProto.value */
		Str str = protoComments(reg, e->File, e->Outers, e->nOuters, "EnumType", e->Index, fp, 2);
		if (str.n)
			strs_push(&comments, cat3(name, S(": "), str));
	}
	if (comments.n == 0)
		return S("");
	Buf b = {0};
	buf_adds(&b, "- ");
	for (int i = 0; i < comments.n; i++) {
		if (i)
			buf_adds(&b, "\n - ");
		buf_add_str(&b, comments.v[i]);
	}
	return buf_view(&b);
}

static Str mergeDescription(const openapiSchemaObject *schema)
{
	Str desc = schema->Description;
	if (schema->Title.n) /* join title because title of parameter object will be ignored */
		desc = trim_space(cat3(schema->Title, S(paragraphDeliminator), schema->Description));
	return desc;
}

/* ---- operations ---- */

static openapiOperationObject **operationForMethod(openapiPathItemObject *obj, Str httpMethod)
{
	if (str_eqc(httpMethod, "GET"))
		return &obj->Get;
	if (str_eqc(httpMethod, "POST"))
		return &obj->Post;
	if (str_eqc(httpMethod, "PUT"))
		return &obj->Put;
	if (str_eqc(httpMethod, "DELETE"))
		return &obj->Delete;
	if (str_eqc(httpMethod, "PATCH"))
		return &obj->Patch;
	if (str_eqc(httpMethod, "HEAD"))
		return &obj->Head;
	if (str_eqc(httpMethod, "OPTIONS"))
		return &obj->Options;
	return NULL;
}

/* Returns the openapiPathItemObject associated with a path (a copy). If path is not present,
 * returns an empty openapiPathItemObject and false. */
static bool getPathItemObject(const openapiPathsObject *paths, Str path, openapiPathItemObject *out)
{
	for (int i = 0; i < paths->n; i++)
		if (str_equal(paths->v[i].Path, path)) {
			*out = paths->v[i].PathItemObject;
			return true;
		}
	*out = (openapiPathItemObject){0};
	return false;
}

/* If a path already exists in openapiPathsObject, updates that path's openapiPathItemObject. If
 * not, appends a new path and openapiPathItemObject to the openapiPathsObject. */
static void updatePaths(openapiPathsObject *paths, Str path, openapiPathItemObject pathItemObject)
{
	for (int i = 0; i < paths->n; i++)
		if (str_equal(paths->v[i].Path, path)) {
			paths->v[i].PathItemObject = pathItemObject;
			return;
		}
	if (paths->n == paths->cap) {
		int cap = paths->cap ? paths->cap * 2 : 16;
		paths->v = xrealloc(paths->v, sizeof *paths->v * (size_t)paths->cap, sizeof *paths->v * (size_t)cap);
		paths->cap = cap;
	}
	paths->v[paths->n++] = (pathData){path, pathItemObject};
}

static const char *setRefFromFQN(schemaCore *s, const char *ref, GwRegistry *reg)
{
	const char *name = openAPIName(ref, reg);
	if (!name)
		fail("setRefFromFQN: can't resolve OpenAPI name from %s", go_quote(S(ref)).p);
	s->Ref = S(xcat("#/definitions/", name));
	return NULL;
}

static Examples openapiExamplesFromProtoExamples(const PbMsg *resp)
{
	GwMapEntry *e;
	int n = gw_options_map(resp, OA_Response_examples, &e);
	Examples out = {0};
	if (n == 0)
		return out;
	out.v = xalloc(sizeof *out.v * (size_t)n);
	for (int i = 0; i < n; i++) {
		/* JSON example objects are rendered raw; all other mimetype examples as strings. */
		out.v[i] = (exampleEntry){e[i].key, str_eqc(e[i].key, "application/json"), pb_bytes(e[i].value, 2)};
	}
	out.n = n;
	return out;
}

static GoStrs pb_strs_copy(const PbMsg *m, uint32_t num)
{
	int n = pb_count(m, num);
	GoStrs s = {xalloc(sizeof(Str) * (size_t)(n + 1)), n, n};
	for (int i = 0; i < n; i++)
		s.v[i] = pb_bytes_at(m, num, i);
	return s;
}

/* the security requirements of an option (proto map values are never nil) */
static openapiSecurityRequirementObject securityRequirement(const PbMsg *secReq)
{
	GwMapEntry *e;
	int n = gw_options_map(secReq, OA_SecurityRequirement_security_requirement, &e);
	openapiSecurityRequirementObject r = {xalloc(sizeof *r.v * (size_t)(n + 1)), 0};
	for (int i = 0; i < n; i++)
		r.v[r.n++] = (secReqEntry){e[i].key, pb_strs_copy(pb_msg(e[i].value, 2), OA_SecurityRequirementValue_scope)};
	return r;
}

static void renderServices(Vec services, openapiPathsObject *paths, GwRegistry *reg, StrSet *requestResponseRefs, StrSet *customRefs,
			   const Vec *msgs, openapiDefinitionsObject *defs)
{
	/* Correctness of svcIdx and methIdx depends on 'services' containing the services in the
	 * same order as the 'file.Service' array. */
	int svcBaseIdx = 0;
	GwFile *lastFile = NULL;
	for (int svcIdx = 0; svcIdx < services.n; svcIdx++) {
		GwService *svc = services.v[svcIdx];
		if (svc->File != lastFile) {
			lastFile = svc->File;
			svcBaseIdx = svcIdx;
		}
		if (!isVisible(getServiceVisibilityOption(svc), reg))
			continue;

		for (int methIdx = 0; methIdx < svc->Methods.n; methIdx++) {
			GwMethod *meth = svc->Methods.v[methIdx];
			if (!isVisible(getMethodVisibilityOption(meth), reg))
				continue;
			bool deprecated = reg->enableRpcDeprecation && method_deprecated(meth);

			for (int bIdx = 0; bIdx < meth->Bindings.n; bIdx++) {
				GwBinding *b = meth->Bindings.v[bIdx];
				Params bPathParams = binding_params(b);
				/* Iterate over all the OpenAPI parameters */
				openapiParametersObject parameters = {0};
				/* split the path template into its parts */
				Strs parts = templateToParts(b->PathTmpl.Template, reg, &meth->RequestType->Fields, msgs);
				Params pathParams = bPathParams;
				if (reg->expandSlashedPathPatterns)
					parts = expandPathPatterns(parts, &pathParams, reg);
				/* extract any constraints specified in the path placeholders into ECMA regular
				 * expressions */
				StrMap pathParamRegexpMap = partsToRegexpMap(parts);
				/* Keep track of path parameter overrides */
				StrMap pathParamNames = {0};
				for (int pi = 0; pi < pathParams.len; pi++) {
					GwParameter *parameter = &pathParams.v[pi];
					Str paramType = {"", 0}, paramFormat = {"", 0}, desc = {"", 0}, collectionFormat = {"", 0},
					    schemaPattern = {"", 0};
					Any defaultValue = {0}, enumNames = {0};
					RawExample example = {0};
					openapiItemsObject *items = NULL;
					int *minItems = NULL;
					Extensions extensions = {0};
					int pt = parameter->Target->Type;
					if (pt == PT_GROUP || pt == PT_MESSAGE) {
						if (gw_IsWellKnownType(parameter->Target->TypeName)) {
							if (gw_Parameter_IsRepeated(parameter))
								fail("only primitive and enum types are allowed in repeated path parameters");
							openapiSchemaObject schema = schemaOfField(parameter->Target, reg, customRefs);
							paramType = schema.core.Type;
							paramFormat = schema.core.Format;
							desc = schema.Description;
							defaultValue = schema.core.Default;
							example = schema.core.Example;
							schemaPattern = schema.Pattern;
							extensions = schema.extensions;
						} else {
							fail("only primitive and well-known types are allowed in path parameters");
						}
					} else if (pt == PT_ENUM) {
						GwEnum *e = lookupEnum(reg, "", parameter->Target->TypeName);
						if (!e)
							fail("no enum found: %s", parameter->Target->TypeName);
						paramType = S("string");
						enumNames = listEnumNames(reg, e);
						if (reg->enumsAsInts) {
							paramType = S("integer");
							enumNames = listEnumNumbers(reg, e);
						}
						openapiSchemaObject schema = schemaOfField(parameter->Target, reg, customRefs);
						desc = schema.Description;
						defaultValue = schema.core.Default;
						example = schema.core.Example;
						schemaPattern = schema.Pattern;
						extensions = schema.extensions;
					} else {
						const char *t, *f;
						if (!primitiveSchema(pt, &t, &f))
							fail("unknown field type %s", fieldTypeString(pt));
						paramType = S(t);
						paramFormat = S(f);
						openapiSchemaObject schema = schemaOfField(parameter->Target, reg, customRefs);
						desc = schema.Description;
						defaultValue = schema.core.Default;
						example = schema.core.Example;
						schemaPattern = schema.Pattern;
						extensions = schema.extensions;
						/* If there is no mandatory format based on the field, allow it to be
						 * overridden by the user */
						if (!paramFormat.n)
							paramFormat = schema.core.Format;
					}

					if (gw_Parameter_IsRepeated(parameter)) {
						schemaCore core = {.Type = paramType, .Format = paramFormat};
						if (gw_Parameter_IsEnum(parameter)) {
							core.Enum = enumNames;
							enumNames = (Any){0};
						}
						items = xalloc(sizeof *items);
						items->core = core;
						paramType = S("array");
						paramFormat = S("");
						collectionFormat = reg->repeatedPathParamSeparatorName;
						minItems = xalloc(sizeof *minItems);
						*minItems = 1;
					}

					if (!desc.n)
						desc = fieldProtoComments(reg, parameter->Target->Message, parameter->Target);
					Str parameterString = S(gw_FieldPath_String(parameter->FieldPath));
					if (reg->useJSONNamesForFields)
						parameterString = lowerCamelCase(parameterString, &meth->RequestType->Fields, msgs);
					Str pattern = schemaPattern;
					Str *regExp = strmap_get(&pathParamRegexpMap, parameterString);
					if (regExp)
						pattern = *regExp;
					const PbMsg *fc = getFieldConfiguration(reg, parameter->Target);
					if (fc) {
						Str pathParamName = pb_bytes(fc, OA_FieldConfiguration_path_param_name);
						if (pathParamName.n && !str_equal(pathParamName, parameterString)) {
							strmap_set(&pathParamNames, cat3(S("{"), parameterString, S("}")),
								   cat3(S("{"), pathParamName, S("}")));
							const char *eq = memchr(pathParamName.p, '=', pathParamName.n);
							parameterString = eq ? (Str){pathParamName.p, (size_t)(eq - pathParamName.p)} : pathParamName;
						}
					}

					/* verify if the parameter is deprecated, either via proto or annotation */
					bool protoDeprecated = field_deprecated(parameter->Target) && reg->enableFieldDeprecation;
					bool annotationDeprecated = pb_int(fc, OA_FieldConfiguration_deprecated, 0) != 0;

					openapiParameterObject param = {
						.Name = parameterString,
						.Description = desc,
						.In = S("path"),
						.Required = true,
						.Deprecated = protoDeprecated || annotationDeprecated,
						.Default = defaultValue,
						.XExample = example,
						/* Parameters in gRPC-Gateway can only be strings? */
						.Type = paramType,
						.Format = paramFormat,
						.Enum = enumNames,
						.Items = items,
						.CollectionFormat = collectionFormat,
						.MinItems = minItems,
						.Pattern = pattern,
						.extensions = extensions,
					};
					params_push(&parameters, param);
				}
				/* Now check if there is a body parameter */
				if (b->Body) {
					/* Recursively render fields as definitions as long as they contain path
					 * parameters. Special case for top level body if we don't have a body
					 * field. */
					openapiSchemaObject *schema = xalloc(sizeof *schema);
					Str desc = {"", 0};
					Str bodyFieldName;
					if (b->Body->FieldPath.n == 0) {
						/* No field for body, use type. */
						bodyFieldName = S("body");
						schemaCore wknSchemaCore;
						if (wktSchema(message_fqmn(meth->RequestType), &wknSchemaCore)) {
							schema->core = wknSchemaCore;
							/* Special workaround for Empty */
							if (strcmp(message_fqmn(meth->RequestType), ".google.protobuf.Empty") == 0)
								schema->Properties = xalloc(sizeof *schema->Properties);
						} else {
							openapiSchemaObject messageSchema =
								renderMessageAsDefinition(meth->RequestType, reg, customRefs, bPathParams);
							if (bPathParams.len == 0) {
								setRefFromFQN(&schema->core, message_fqmn(meth->RequestType), reg);
								desc = messageSchema.Description;
							} else {
								const char *methFQN = openAPIName(gw_Method_FQMN(meth), reg);
								if (!methFQN)
									go_panic(xcat("failed to resolve method FQN: '", gw_Method_FQMN(meth), "'"));
								const char *defName = xcat(methFQN, "Body");
								schema->core.Ref = S(xcat("#/definitions/", defName));
								defs_set(defs, S(defName), messageSchema);
							}
						}
					} else {
						/* grpc-gateway accepts nested body field paths, so OpenAPI rendering
						 * also follows the full path and uses the terminal field type. */
						GwFieldPath bodyFieldPath = b->Body->FieldPath;
						GwFieldPathComponent bodyField = bodyFieldPath.v[bodyFieldPath.n - 1];
						Str bodyFieldRequiredName = S(bodyField.Name);
						if (reg->useJSONNamesForFields) {
							bodyFieldName = lowerCamelCase(S(gw_FieldPath_String(bodyFieldPath)), &meth->RequestType->Fields, msgs);
							bodyFieldRequiredName = lowerCamelCase(S(bodyField.Name), &bodyField.Target->Message->Fields, msgs);
						} else {
							bodyFieldName = S(gw_FieldPath_String(bodyFieldPath));
						}
						/* Align pathParams with body field path. */
						Params bpp = bPathParams;
						for (int k = 0; k < bodyFieldPath.n; k++)
							bpp = subPathParams(bodyFieldPath.v[k].Name, bpp);

						if (bpp.len == 0) {
							/* When there are no path parameters, we only need the base schema of
							 * the field. */
							*schema = schemaOfFieldBase(bodyField.Target, reg, customRefs);
						} else {
							*schema = renderFieldAsDefinition(bodyField.Target, reg, customRefs, bpp);
							/* The body field name should only be in the schema's required array if
							 * it's actually a property of the schema. */
							if (schema->Required.v && schema->Properties) {
								GoStrs filteredRequired = {xalloc(sizeof(Str) * (size_t)(schema->Required.len + 1)), 0,
											   schema->Required.len};
								bool seenBodyFieldName = false;
								for (int k = 0; k < schema->Required.len; k++) {
									Str req = schema->Required.v[k];
									if (str_equal(req, bodyFieldRequiredName)) {
										bool isProperty = false;
										for (int q = 0; q < schema->Properties->n && !isProperty; q++)
											isProperty = str_equal(schema->Properties->v[q].Key, req);
										if (isProperty && !seenBodyFieldName) {
											filteredRequired.v[filteredRequired.len++] = req;
											seenBodyFieldName = true;
										}
									} else {
										filteredRequired.v[filteredRequired.len++] = req;
									}
								}
								schema->Required = filteredRequired;
							}
						}
						if (schema->Title.n)
							desc = mergeDescription(schema);
						else
							desc = fieldProtoComments(reg, bodyField.Target->Message, bodyField.Target);
					}

					if (meth->ClientStreaming)
						desc = cat(desc, S(" (streaming inputs)"));
					openapiParameterObject param = {
						.Name = bodyFieldName,
						.Description = desc,
						.In = S("body"),
						.Required = true,
						.Schema = schema,
					};
					params_push(&parameters, param);
				}

				/* add the parameters to the query string */
				openapiParametersObject queryParams =
					messageToQueryParameters(meth->RequestType, reg, bPathParams, b->Body, b->HTTPMethod);
				params_concat(&parameters, queryParams);

				Str path = partsToOpenAPIPath(parts, &pathParamNames);

				openapiPathItemObject pathItemObject;
				if (getPathItemObject(paths, path, &pathItemObject)) {
					/* handle case where we have an existing mapping for the same path and method */
					openapiOperationObject **existing = operationForMethod(&pathItemObject, b->HTTPMethod);
					if (existing && *existing) {
						int firstParamIndex = -1;
						for (int k = 0; k < parameters.n; k++)
							if (str_eqc(parameters.v[k].In, "path")) {
								firstParamIndex = k;
								break;
							}
						if (firstParamIndex >= 0) {
							openapiParameterObject firstPathParameter = parameters.v[firstParamIndex];
							int newPathCount = 0;
							Str newPath = {"", 0}, newPathElement = {"", 0};
							/* Iterate until there is not an existing operation that matches the
							 * same escaped path. */
							bool more = true;
							while (more) {
								newPathCount++;
								newPathElement = cat(cat(firstPathParameter.Name, S(pathParamUniqueSuffixDeliminator)),
										     S(xfmt("%d", newPathCount)));
								newPath = replace_all_s(path, cat3(S("{"), firstPathParameter.Name, S("}")),
											cat3(S("{"), newPathElement, S("}")));
								openapiPathItemObject newPathItemObject;
								if (getPathItemObject(paths, newPath, &newPathItemObject)) {
									openapiOperationObject **o = operationForMethod(&newPathItemObject, b->HTTPMethod);
									more = o && *o;
								} else {
									more = false;
								}
							}
							/* update the pathItemObject we are adding to with the new path */
							getPathItemObject(paths, newPath, &pathItemObject);
							firstPathParameter.Name = newPathElement;
							path = newPath;
							parameters.v[firstParamIndex] = firstPathParameter;
						}
						/* else: grpclog.Errorf("Duplicate mapping for path ...") and overwrite */
					}
				}

				Str desc = S("A successful response.");
				openapiSchemaObject responseSchema = {0};

				if (!b->ResponseBody || b->ResponseBody->FieldPath.n == 0) {
					/* Don't link to a full definition for empty; it's overly verbose. */
					schemaCore wknSchemaCore;
					if (!wktSchema(message_fqmn(meth->ResponseType), &wknSchemaCore)) {
						setRefFromFQN(&responseSchema.core, message_fqmn(meth->ResponseType), reg);
					} else {
						responseSchema.core = wknSchemaCore;
						/* Special workaround for Empty */
						if (strcmp(message_fqmn(meth->ResponseType), ".google.protobuf.Empty") == 0)
							responseSchema.Properties = xalloc(sizeof *responseSchema.Properties);
					}
				} else {
					/* This is resolving the value of response_body in the google.api.HttpRule */
					GwFieldPathComponent lastField = b->ResponseBody->FieldPath.v[b->ResponseBody->FieldPath.n - 1];
					responseSchema = schemaOfField(lastField.Target, reg, customRefs);
					if (responseSchema.Description.n)
						desc = responseSchema.Description;
					else
						desc = fieldProtoComments(reg, lastField.Target->Message, lastField.Target);
				}
				if (meth->ServerStreaming) {
					desc = cat(desc, S("(streaming responses)"));
					responseSchema.core.Type = S("object");
					const char *swgRef = openAPIName(message_fqmn(meth->ResponseType), reg);
					responseSchema.Title = S(xcat("Stream result of ", swgRef ? swgRef : ""));

					openapiSchemaObjectProperties *props = xalloc(sizeof *props);
					openapiSchemaObject result = {0};
					result.core.Ref = responseSchema.core.Ref;
					props_append(props, S("result"), result);
					if (!reg->disableDefaultErrors) {
						const char *statusDef = openAPIName(".google.rpc.Status", reg);
						if (statusDef) {
							openapiSchemaObject e = {0};
							e.core.Ref = S(xcat("#/definitions/", statusDef));
							props_append(props, S("error"), e);
						}
					}
					/* Special case HttpBody responses, they will be unformatted bytes */
					if (strcmp(message_fqmn(meth->ResponseType), ".google.api.HttpBody") == 0) {
						responseSchema.core.Type = S("string");
						responseSchema.core.Format = S("binary");
						responseSchema.Title = S("Free form byte stream");
						/* The error response is still JSON, but technically the full response
						 * is still unformatted, so don't include the error response structure. */
						props = xalloc(sizeof *props);
					}
					responseSchema.Properties = props;
					responseSchema.core.Ref = S("");
				}

				openapiOperationObject *operationObject = xalloc(sizeof *operationObject);
				operationObject->Parameters = parameters;
				operationObject->Responses = xalloc(sizeof *operationObject->Responses);
				operationObject->Deprecated = deprecated;

				if (!reg->disableDefaultResponses) {
					openapiResponseObject r = {.Description = desc, .Schema = responseSchema};
					r.Headers.nonnil = true;
					responses_set(operationObject->Responses, S("200"), r);
				}

				if (!reg->disableServiceTags) {
					Str tag = S(svc->Name);
					if (svc->File->package[0] && reg->includePackageInTags)
						tag = S(xcat(svc->File->package, ".", svc->Name));
					operationObject->Tags = strs_append1((GoStrs){0}, tag);
				}

				if (!reg->disableDefaultErrors) {
					const char *errDef = openAPIName(".google.rpc.Status", reg);
					if (errDef) {
						openapiResponseObject r = {.Description = S("An unexpected error response.")};
						r.Schema.core.Ref = S(xcat("#/definitions/", errDef));
						responses_set(operationObject->Responses, S("default"), r);
					}
				}
				operationObject->OperationID = S(xcat(svc->Name, "_", meth->Name));
				if (reg->simpleOperationIDs)
					operationObject->OperationID = S(meth->Name);
				if (bIdx != 0)
					/* OperationID must be unique in an OpenAPI v2 definition. */
					operationObject->OperationID = cat(operationObject->OperationID, S(xfmt("%d", bIdx + 1)));

				/* Fill reference map with referenced request messages */
				for (int k = 0; k < operationObject->Parameters.n; k++) {
					const openapiParameterObject *param = &operationObject->Parameters.v[k];
					if (param->Schema && param->Schema->core.Ref.n)
						set_add(requestResponseRefs, param->Schema->core.Ref);
				}

				int32_t fp[2] = {2, methIdx}; /* ServiceDescriptorProto.method */
				Str methComments = protoComments(reg, svc->File, NULL, 0, "Service", svcIdx - svcBaseIdx, fp, 2);
				updateOpenAPIDataFromComments(
					reg, (commentTarget){&operationObject->Summary, false, &operationObject->Description, NULL}, methComments,
					false);

				const PbMsg *svcOpts = getServiceOpenAPIOption(reg, svc);
				/* Set Tag with the user-defined service name */
				if (pb_bytes(svcOpts, OA_Tag_name).n)
					operationObject->Tags = strs_append1((GoStrs){0}, pb_bytes(svcOpts, OA_Tag_name));

				const PbMsg *opts = getMethodOpenAPIOption(reg, meth);
				if (opts) {
					operationObject->ExternalDocs =
						protoExternalDocumentationToOpenAPIExternalDocumentation(pb_msg(opts, OA_Operation_external_docs));
					if (pb_int(opts, OA_Operation_deprecated, 0))
						operationObject->Deprecated = true;
					if (pb_bytes(opts, OA_Operation_summary).n)
						operationObject->Summary = pb_bytes(opts, OA_Operation_summary);
					if (pb_bytes(opts, OA_Operation_description).n)
						operationObject->Description = pb_bytes(opts, OA_Operation_description);
					if (pb_count(opts, OA_Operation_tags) > 0)
						operationObject->Tags = pb_strs_copy(opts, OA_Operation_tags);
					if (pb_bytes(opts, OA_Operation_operation_id).n)
						operationObject->OperationID = pb_bytes(opts, OA_Operation_operation_id);
					if (pb_count(opts, OA_Operation_security) > 0) {
						SecurityRequirements *newSecurity = xalloc(sizeof *newSecurity);
						newSecurity->nonnil = true;
						int ns = pb_count(opts, OA_Operation_security);
						newSecurity->v = xalloc(sizeof *newSecurity->v * (size_t)(ns + 1));
						for (int k = 0; k < ns; k++) {
							openapiSecurityRequirementObject newSecReq =
								securityRequirement(pb_msg_at(opts, OA_Operation_security, k));
							if (newSecReq.n > 0)
								newSecurity->v[newSecurity->n++] = newSecReq;
						}
						operationObject->Security = newSecurity;
					}
					if (pb_count(opts, OA_Operation_responses) > 0) {
						GwMapEntry *e;
						int n = gw_options_map(opts, OA_Operation_responses, &e);
						for (int k = 0; k < n; k++) {
							const PbMsg *resp = pb_msg(e[k].value, 2);
							/* Merge response data into default response if available. */
							openapiResponseObject respObj = {0};
							openapiResponseObject *existing = responses_get(operationObject->Responses, e[k].key);
							if (existing)
								respObj = *existing;
							if (pb_bytes(resp, OA_Response_description).n)
								respObj.Description = pb_bytes(resp, OA_Response_description);
							if (pb_has(resp, OA_Response_schema))
								respObj.Schema = openapiSchemaFromProtoSchema(pb_msg(resp, OA_Response_schema), reg, customRefs);
							if (pb_count(resp, OA_Response_examples) > 0)
								respObj.Examples = openapiExamplesFromProtoExamples(resp);
							if (pb_count(resp, OA_Response_headers) > 0)
								respObj.Headers = processHeaders(resp);
							if (pb_count(resp, OA_Response_extensions) > 0)
								respObj.extensions = processExtensions(resp, OA_Response_extensions);
							responses_set(operationObject->Responses, e[k].key, respObj);
						}
					}

					if (pb_count(opts, OA_Operation_extensions) > 0)
						operationObject->extensions = processExtensions(opts, OA_Operation_extensions);
					if (pb_count(opts, OA_Operation_consumes) > 0)
						operationObject->Consumes = pb_strs_copy(opts, OA_Operation_consumes);
					if (pb_count(opts, OA_Operation_produces) > 0)
						operationObject->Produces = pb_strs_copy(opts, OA_Operation_produces);

					const PbMsg *params = pb_msg(opts, OA_Operation_parameters);
					for (int k = 0; params && k < pb_count(params, OA_Parameters_headers); k++) {
						const PbMsg *header = pb_msg_at(params, OA_Parameters_headers, k);
						openapiParameterObject param = {
							.In = S("header"),
							.Name = pb_bytes(header, OA_HeaderParameter_name),
							.Description = pb_bytes(header, OA_HeaderParameter_description),
							.Required = pb_int(header, OA_HeaderParameter_required, 0) != 0,
							.Format = pb_bytes(header, OA_HeaderParameter_format),
						};
						int64_t t = pb_int(header, OA_HeaderParameter_type, 0);
						static const char *const types[] = {NULL, "string", "number", "integer", "boolean"};
						if (t < 1 || t > 4)
							fail("invalid header parameter type: %s", t == 0 ? "UNKNOWN" : xfmt("%d", (int)(int32_t)t));
						param.Type = S(types[t]);
						params_push(&operationObject->Parameters, param);
					}
					/* TODO(ivucica): add remaining fields of operation object */
				}

				openapiOperationObject **slot = operationForMethod(&pathItemObject, b->HTTPMethod);
				if (slot)
					*slot = operationObject;
				updatePaths(paths, path, pathItemObject);
			}
		}
	}
}

/* ---- the document ---- */

typedef struct {
	Vec services;
	openapiPathsObject *paths;
	GwRegistry *reg;
	StrSet *requestResponseRefs, *customRefs;
	const Vec *msgs;
	openapiDefinitionsObject *defs;
} RenderServicesCtx;

static void renderServices_run(void *arg)
{
	RenderServicesCtx *c = arg;
	renderServices(c->services, c->paths, c->reg, c->requestResponseRefs, c->customRefs, c->msgs, c->defs);
}

static void tags_append(Tags *t, openapiTagObject tag)
{
	openapiTagObject *v = xalloc(sizeof *v * (size_t)(t->n + 1));
	if (t->n)
		mcopy(v, t->v, sizeof *v * (size_t)t->n);
	v[t->n] = tag;
	t->v = v;
	t->n++;
}

static Tags mergeTags(Tags existingTags, Tags tags)
{
	for (int i = 0; i < tags.n; i++) {
		openapiTagObject tag = tags.v[i];
		bool matched = false;
		for (int k = 0; k < existingTags.n; k++) {
			openapiTagObject *existingTag = &existingTags.v[k];
			if (!str_equal(existingTag->Name, tag.Name))
				continue;
			if (!existingTag->Description.n)
				existingTag->Description = tag.Description;
			if (!existingTag->ExternalDocs) {
				existingTag->ExternalDocs = tag.ExternalDocs;
			} else if (tag.ExternalDocs) {
				if (!existingTag->ExternalDocs->Description.n)
					existingTag->ExternalDocs->Description = tag.ExternalDocs->Description;
				if (!existingTag->ExternalDocs->URL.n)
					existingTag->ExternalDocs->URL = tag.ExternalDocs->URL;
			}
			if (!existingTag->extensions.nonnil) {
				existingTag->extensions = tag.extensions;
			} else if (tag.extensions.nonnil) {
				for (int e = 0; e < tag.extensions.n; e++) {
					bool matchedExt = false;
					for (int x = 0; x < existingTag->extensions.n && !matchedExt; x++)
						matchedExt = str_equal(existingTag->extensions.v[x].key, tag.extensions.v[e].key);
					if (!matchedExt) {
						extension *v = xalloc(sizeof *v * (size_t)(existingTag->extensions.n + 1));
						mcopy(v, existingTag->extensions.v, sizeof *v * (size_t)existingTag->extensions.n);
						v[existingTag->extensions.n++] = tag.extensions.v[e];
						existingTag->extensions.v = v;
					}
				}
			}
			matched = true;
			break;
		}
		if (!matched)
			tags_append(&existingTags, tag);
	}
	return existingTags;
}

/* param: the file (with the messages, enums and services of merged files) */
typedef struct {
	GwFile *File;
	GwRegistry *reg;
} param;

static void secdefs_set(openapiSecurityDefinitionsObject *d, Str key, openapiSecuritySchemeObject v)
{
	for (int i = 0; i < d->n; i++)
		if (str_equal(d->v[i].key, key)) {
			d->v[i].value = v;
			return;
		}
	if (d->n == d->cap) {
		int cap = d->cap ? d->cap * 2 : 4;
		d->v = xrealloc(d->v, sizeof *d->v * (size_t)d->cap, sizeof *d->v * (size_t)cap);
		d->cap = cap;
	}
	d->v[d->n++] = (secDefEntry){key, v};
}

/* applyTemplate is called with a param which contains the entire definition of a method. */
static openapiSwaggerObject *applyTemplate(param p)
{
	GwRegistry *reg = p.reg;
	/* Create the basic template object. This is the object that everything is defined off
	 * of. */
	openapiSwaggerObject *s = xalloc(sizeof *s);
	s->Swagger = S("2.0");
	s->Consumes = strs_append1((GoStrs){0}, S("application/json"));
	s->Produces = strs_append1((GoStrs){0}, S("application/json"));
	s->Definitions = defs_new();
	s->Info.Title = S(p.File->name);
	s->Info.Version = S("version not set");

	/* Initialize the naming cache BEFORE any code that uses fullyQualifiedNameToOpenAPIName:
	 * pre-scan to collect referenced names without using the naming cache. */
	StrSet referencedNames = collectReferencedNamesForCache(p.File, p.File->Services, p.File->Messages, reg);

	/* Get all names from the registry */
	const char **names;
	int nnames = allNames(reg, &names);

	/* Filter: EXCLUDE names that are from a DIFFERENT package AND are NOT referenced. */
	const char *currentPackage = p.File->package;
	const char *pkgPrefix = xcat(currentPackage, ".");
	const char **filteredNames = xalloc(sizeof *filteredNames * (size_t)(nnames + 1));
	int nfiltered = 0;
	for (int i = 0; i < nnames; i++) {
		const char *name = names[i];
		const char *trimmedName = name[0] == '.' ? name + 1 : name;
		if (!trimmedName[0])
			continue;
		/* Include if: (1) from current package, OR (2) actually referenced, OR (3) from the
		 * google and grpc packages */
		bool isCurrentPackage = has_prefix(trimmedName, pkgPrefix);
		bool isGoogle = has_prefix(trimmedName, "google.");
		bool isGRPC = has_prefix(trimmedName, "grpc.");
		if (isCurrentPackage || set_has(&referencedNames, S(name)) || isGoogle || isGRPC)
			filteredNames[nfiltered++] = name;
	}

	/* Initialize the naming cache BEFORE renderServices so all lookups use consistent
	 * naming */
	registriesSeen = resolveFullyQualifiedNameToOpenAPINames(filteredNames, nfiltered, reg->openAPINamingStrategy);

	/* Loops through all the services and their exposed GET/POST/PUT/DELETE definitions and
	 * create entries for all of them. Also adds custom user specified references to second
	 * map. */
	StrSet requestResponseRefs = {0}, customRefs = {0};
	/* an error of renderServices: panic(err) */
	RenderServicesCtx rc = {p.File->Services, &s->Paths, reg, &requestResponseRefs, &customRefs, &p.File->Messages, s->Definitions};
	if (try_run(renderServices_run, &rc)) {
		Str msg = fail_message();
		if (str_has_prefix(msg, STR_LIT("unsupported by protoc-gen-uni: ")))
			fail_str(msg);
		go_panic(xstrndup(msg.p, msg.n));
	}

	NameMap messages = {0}, enums = {0};

	if (!reg->disableDefaultErrors) {
		/* Add the error type to the message map */
		GwMessage *runtimeError = lookupMsg(reg, "google.rpc", "Status");
		if (runtimeError) {
			const char *swgRef = openAPIName(message_fqmn(runtimeError), reg);
			if (swgRef)
				nm_put(&messages, swgRef, runtimeError);
			/* else: grpclog.Error(can't map OpenAPI name) */
		}
	}

	/* Find all the service's messages and enumerations that are defined (recursively) and
	 * write request, response and other custom (but referenced) types out as definition
	 * objects. */
	findServicesMessagesAndEnumerations(p.File->Services, reg, &messages, &enums, &requestResponseRefs);

	renderMessagesAsDefinition(&messages, s->Definitions, reg, &customRefs, (Params){0});
	renderEnumerationsAsDefinition(&enums, s->Definitions, reg, &requestResponseRefs);

	/* File itself might have some comments and metadata. */
	Str packageComments = protoComments(reg, p.File, NULL, 0, "Package", 2, NULL, 0);
	updateOpenAPIDataFromComments(reg, (commentTarget){&s->Info.Title, true, &s->Info.Description, NULL}, packageComments,
				      true);

	/* There may be additional options in the OpenAPI option in the proto. */
	const PbMsg *spb = getFileOpenAPIOption(reg, p.File);
	if (spb) {
		if (pb_bytes(spb, OA_Swagger_swagger).n)
			s->Swagger = pb_bytes(spb, OA_Swagger_swagger);
		const PbMsg *info = pb_msg(spb, OA_Swagger_info);
		if (info) {
			if (pb_bytes(info, OA_Info_title).n)
				s->Info.Title = pb_bytes(info, OA_Info_title);
			if (pb_bytes(info, OA_Info_description).n)
				s->Info.Description = pb_bytes(info, OA_Info_description);
			if (pb_bytes(info, OA_Info_terms_of_service).n)
				s->Info.TermsOfService = pb_bytes(info, OA_Info_terms_of_service);
			if (pb_bytes(info, OA_Info_version).n)
				s->Info.Version = pb_bytes(info, OA_Info_version);
			const PbMsg *contact = pb_msg(info, OA_Info_contact);
			if (contact) {
				if (!s->Info.Contact)
					s->Info.Contact = xalloc(sizeof *s->Info.Contact);
				if (pb_bytes(contact, OA_Contact_name).n)
					s->Info.Contact->Name = pb_bytes(contact, OA_Contact_name);
				if (pb_bytes(contact, OA_Contact_url).n)
					s->Info.Contact->URL = pb_bytes(contact, OA_Contact_url);
				if (pb_bytes(contact, OA_Contact_email).n)
					s->Info.Contact->Email = pb_bytes(contact, OA_Contact_email);
			}
			const PbMsg *license = pb_msg(info, OA_Info_license);
			if (license) {
				if (!s->Info.License)
					s->Info.License = xalloc(sizeof *s->Info.License);
				if (pb_bytes(license, OA_License_name).n)
					s->Info.License->Name = pb_bytes(license, OA_License_name);
				if (pb_bytes(license, OA_License_url).n)
					s->Info.License->URL = pb_bytes(license, OA_License_url);
			}
			if (pb_count(info, OA_Info_extensions) > 0)
				s->Info.extensions = processExtensions(info, OA_Info_extensions);
		}
		if (pb_bytes(spb, OA_Swagger_host).n)
			s->Host = pb_bytes(spb, OA_Swagger_host);
		if (pb_bytes(spb, OA_Swagger_base_path).n)
			s->BasePath = pb_bytes(spb, OA_Swagger_base_path);
		int nschemes = pb_count(spb, OA_Swagger_schemes);
		if (nschemes > 0) {
			s->Schemes = (GoStrs){xalloc(sizeof(Str) * (size_t)nschemes), nschemes, nschemes};
			static const char *const schemes[] = {"unknown", "http", "https", "ws", "wss"};
			for (int i = 0; i < nschemes; i++) {
				int64_t v = pb_int_at(spb, OA_Swagger_schemes, i);
				/* strings.ToLower(scheme.String()) */
				s->Schemes.v[i] = S(v >= 0 && v <= 4 ? schemes[v] : xfmt("%d", (int)(int32_t)v));
			}
		}
		if (pb_count(spb, OA_Swagger_consumes) > 0)
			s->Consumes = pb_strs_copy(spb, OA_Swagger_consumes);
		if (pb_count(spb, OA_Swagger_produces) > 0)
			s->Produces = pb_strs_copy(spb, OA_Swagger_produces);
		const PbMsg *sd = pb_msg(spb, OA_Swagger_security_definitions);
		if (sd && pb_count(sd, OA_SecurityDefinitions_security) > 0) {
			if (!s->SecurityDefinitions)
				s->SecurityDefinitions = xalloc(sizeof *s->SecurityDefinitions);
			GwMapEntry *e;
			int n = gw_options_map(sd, OA_SecurityDefinitions_security, &e);
			for (int i = 0; i < n; i++) {
				const PbMsg *secDefValue = pb_msg(e[i].value, 2);
				openapiSecuritySchemeObject newSecDefValue = {0};
				for (int k = 0; k < s->SecurityDefinitions->n; k++)
					if (str_equal(s->SecurityDefinitions->v[k].key, e[i].key))
						newSecDefValue = s->SecurityDefinitions->v[k].value;
				switch (pb_int(secDefValue, OA_SecurityScheme_type, 0)) {
				case 1:
					newSecDefValue.Type = S("basic");
					break;
				case 2:
					newSecDefValue.Type = S("apiKey");
					break;
				case 3:
					newSecDefValue.Type = S("oauth2");
					break;
				}
				if (pb_bytes(secDefValue, OA_SecurityScheme_description).n)
					newSecDefValue.Description = pb_bytes(secDefValue, OA_SecurityScheme_description);
				if (pb_bytes(secDefValue, OA_SecurityScheme_name).n)
					newSecDefValue.Name = pb_bytes(secDefValue, OA_SecurityScheme_name);
				switch (pb_int(secDefValue, OA_SecurityScheme_in, 0)) {
				case 1:
					newSecDefValue.In = S("query");
					break;
				case 2:
					newSecDefValue.In = S("header");
					break;
				}
				switch (pb_int(secDefValue, OA_SecurityScheme_flow, 0)) {
				case 1:
					newSecDefValue.Flow = S("implicit");
					break;
				case 2:
					newSecDefValue.Flow = S("password");
					break;
				case 3:
					newSecDefValue.Flow = S("application");
					break;
				case 4:
					newSecDefValue.Flow = S("accessCode");
					break;
				}
				if (pb_bytes(secDefValue, OA_SecurityScheme_authorization_url).n)
					newSecDefValue.AuthorizationURL = pb_bytes(secDefValue, OA_SecurityScheme_authorization_url);
				if (pb_bytes(secDefValue, OA_SecurityScheme_token_url).n)
					newSecDefValue.TokenURL = pb_bytes(secDefValue, OA_SecurityScheme_token_url);
				const PbMsg *scopes = pb_msg(secDefValue, OA_SecurityScheme_scopes);
				if (scopes) {
					GwMapEntry *se;
					int ns = gw_options_map(scopes, OA_Scopes_scope, &se);
					scopeEntry *v = xalloc(sizeof *v * (size_t)(newSecDefValue.Scopes.n + ns + 1));
					mcopy(v, newSecDefValue.Scopes.v, sizeof *v * (size_t)newSecDefValue.Scopes.n);
					int nv = newSecDefValue.Scopes.n;
					for (int k = 0; k < ns; k++) {
						Str val = pb_bytes(se[k].value, 2);
						int q = 0;
						while (q < nv && !str_equal(v[q].key, se[k].key))
							q++;
						if (q == nv)
							nv++;
						v[q] = (scopeEntry){se[k].key, val};
					}
					newSecDefValue.Scopes = (openapiScopesObject){v, nv, true};
				}
				if (pb_count(secDefValue, OA_SecurityScheme_extensions) > 0)
					newSecDefValue.extensions = processExtensions(secDefValue, OA_SecurityScheme_extensions);
				secdefs_set(s->SecurityDefinitions, e[i].key, newSecDefValue);
			}
		}
		int nsec = pb_count(spb, OA_Swagger_security);
		if (nsec > 0) {
			SecurityRequirements newSecurity = s->Security;
			for (int i = 0; i < nsec; i++) {
				openapiSecurityRequirementObject newSecReq = securityRequirement(pb_msg_at(spb, OA_Swagger_security, i));
				openapiSecurityRequirementObject *v = xalloc(sizeof *v * (size_t)(newSecurity.n + 1));
				mcopy(v, newSecurity.v, sizeof *v * (size_t)newSecurity.n);
				v[newSecurity.n] = newSecReq;
				newSecurity.v = v;
				newSecurity.n++;
				newSecurity.nonnil = true;
			}
			s->Security = newSecurity;
		}
		s->ExternalDocs = protoExternalDocumentationToOpenAPIExternalDocumentation(pb_msg(spb, OA_Swagger_external_docs));
		/* Populate all Paths with Responses set at top level, preferring Responses already
		 * set over those at the top level. */
		if (pb_count(spb, OA_Swagger_responses) > 0) {
			GwMapEntry *e;
			int n = gw_options_map(spb, OA_Swagger_responses, &e);
			for (int i = 0; i < s->Paths.n; i++) {
				openapiPathItemObject *verbs = &s->Paths.v[i].PathItemObject;
				openapiOperationObject *ops[] = {verbs->Delete, verbs->Get, verbs->Post, verbs->Put,
								 verbs->Patch,	verbs->Head, verbs->Options};
				for (int k = 0; k < n; k++) {
					const PbMsg *v = pb_msg(e[k].value, 2);
					for (size_t o = 0; o < sizeof ops / sizeof *ops; o++) {
						if (!ops[o])
							continue;
						openapiResponsesObject *respMap = ops[o]->Responses;
						if (responses_get(respMap, e[k].key))
							continue; /* Don't overwrite already existing Responses */
						openapiResponseObject r = {
							.Description = pb_bytes(v, OA_Response_description),
							.Schema = openapiSchemaFromProtoSchema(pb_msg(v, OA_Response_schema), reg, &customRefs),
							.Examples = openapiExamplesFromProtoExamples(v),
						};
						responses_set(respMap, e[k].key, r);
					}
				}
			}
		}

		if (pb_count(spb, OA_Swagger_extensions) > 0)
			s->extensions = processExtensions(spb, OA_Swagger_extensions);

		for (int i = 0; i < pb_count(spb, OA_Swagger_tags); i++) {
			const PbMsg *v = pb_msg_at(spb, OA_Swagger_tags, i);
			openapiTagObject newTag = {0};
			newTag.Name = pb_bytes(v, OA_Tag_name);
			newTag.Description = pb_bytes(v, OA_Tag_description);
			const PbMsg *ed = pb_msg(v, OA_Tag_external_docs);
			if (ed) {
				newTag.ExternalDocs = xalloc(sizeof *newTag.ExternalDocs);
				newTag.ExternalDocs->Description = pb_bytes(ed, OA_ExternalDocumentation_description);
				newTag.ExternalDocs->URL = pb_bytes(ed, OA_ExternalDocumentation_url);
			}
			if (pb_count(v, OA_Tag_extensions) > 0)
				newTag.extensions = processExtensions(v, OA_Tag_extensions);
			tags_append(&s->Tags, newTag);
		}
	}

	if (!reg->disableServiceTags)
		s->Tags = mergeTags(s->Tags, renderServiceTags(p.File->Services, reg));

	/* Finally add any references added by users that aren't otherwise rendered. */
	addCustomRefs(s->Definitions, reg, &customRefs);
	return s;
}
