// Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
// github.com/grpc-ecosystem/grpc-gateway).

/* The marshalling of the OpenAPI objects: the MarshalJSON and MarshalYAML methods of types.go
 * and generator.go, with what encoding/json (Go 1.27, jsonv2 engine, v1 options) and
 * go.yaml.in/yaml/v3 do with the struct tags (field order, omitempty, inline maps). JSON is
 * written compact like json.Marshal, then indented like json.Encoder.SetIndent("", "  ");
 * YAML is built as yaml.Node trees with the styles the value encoder picks, then encoded with
 * yaml.Encoder.SetIndent(2). Included by generator.c. */

/* ---- Go slices and maps ---- */

/* qsort that accepts a NULL array of fewer than two elements */
static void sort_any(void *v, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	if (n > 1)
		qsort(v, n, size, cmp);
}

/* memmove that accepts a NULL pointer with a zero size */
static void mcopy(void *dst, const void *src, size_t n)
{
	if (n)
		memmove(dst, src, n);
}

/* runtime.roundupsize: the size classes of the Go runtime up to 32 KiB, then whole pages */
static long go_roundupsize(long size)
{
	static const int classes[] = {
		8,     16,    24,    32,    48,    64,    80,    96,    112,   128,   144,   160,   176,   192,   208,
		224,   240,   256,   288,   320,   352,   384,   416,   448,   480,   512,   576,   640,   704,   768,
		896,   1024,  1152,  1280,  1408,  1536,  1792,  2048,  2304,  2688,  3072,  3200,  3456,  4096,  4864,
		5376,  6144,  6528,  6784,  6912,  8192,  9472,  9728,  10240, 10880, 12288, 13568, 14336, 16384, 18432,
		19072, 20480, 21760, 24576, 27264, 28672, 32768,
	};
	for (size_t i = 0; i < sizeof classes / sizeof *classes; i++)
		if (size <= classes[i])
			return classes[i];
	return (size + 8191) / 8192 * 8192;
}

/* growslice: the capacity append gives a slice of oldCap elements of elemSize bytes that must hold
 * newLen */
static int go_newcap(int oldCap, int newLen, int elemSize)
{
	int newcap = oldCap, doublecap = oldCap * 2;
	if (newLen > doublecap) {
		newcap = newLen;
	} else if (oldCap < 256) {
		newcap = doublecap;
	} else {
		while (newcap < newLen)
			newcap += (newcap + 3 * 256) >> 2;
	}
	return (int)(go_roundupsize((long)newcap * elemSize) / elemSize);
}

/* append(s, x...) with growslice's capacity rule */
static GoStrs strs_append(GoStrs s, const Str *x, int n)
{
	int newLen = s.len + n;
	if (newLen > s.cap) {
		int newcap = go_newcap(s.cap, newLen, 16);
		Str *v = xalloc(sizeof *v * (size_t)newcap);
		if (s.len)
			mcopy(v, s.v, sizeof *v * (size_t)s.len);
		s.v = v;
		s.cap = newcap;
	}
	for (int i = 0; i < n; i++)
		s.v[s.len + i] = x[i];
	s.len = newLen;
	return s;
}

static GoStrs strs_append1(GoStrs s, Str x) { return strs_append(s, &x, 1); }

static int str_cmp(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	int c = n ? memcmp(a.p, b.p, n) : 0;
	return c ? c : (a.n > b.n) - (a.n < b.n);
}

/* map keys (hex, so that any bytes are a C string key) */
static const char *map_key(Str s)
{
	static const char hex[] = "0123456789abcdef";
	char *k = xalloc(s.n * 2 + 1);
	for (size_t i = 0; i < s.n; i++) {
		k[2 * i] = hex[(uint8_t)s.p[i] >> 4];
		k[2 * i + 1] = hex[(uint8_t)s.p[i] & 15];
	}
	return k;
}

static openapiDefinitionsObject *defs_new(void) { return xalloc(sizeof(openapiDefinitionsObject)); }

static openapiSchemaObject *defs_get(openapiDefinitionsObject *d, Str key)
{
	intptr_t i = (intptr_t)map_get(&d->index, map_key(key));
	return i ? &d->v[i - 1].value : NULL;
}

static void defs_set(openapiDefinitionsObject *d, Str key, openapiSchemaObject value)
{
	const char *k = map_key(key);
	intptr_t i = (intptr_t)map_get(&d->index, k);
	if (i) {
		d->v[i - 1].value = value;
		return;
	}
	if (d->n == d->cap) {
		int cap = d->cap ? d->cap * 2 : 16;
		d->v = xrealloc(d->v, sizeof *d->v * (size_t)d->cap, sizeof *d->v * (size_t)cap);
		d->cap = cap;
	}
	d->v[d->n++] = (defEntry){key, value};
	map_put(&d->index, k, (void *)(intptr_t)d->n);
}

static openapiResponseObject *responses_get(openapiResponsesObject *r, Str key)
{
	for (int i = 0; i < r->n; i++)
		if (str_equal(r->v[i].key, key))
			return &r->v[i].value;
	return NULL;
}

static void responses_set(openapiResponsesObject *r, Str key, openapiResponseObject value)
{
	openapiResponseObject *p = responses_get(r, key);
	if (p) {
		*p = value;
		return;
	}
	if (r->n == r->cap) {
		int cap = r->cap ? r->cap * 2 : 4;
		r->v = xrealloc(r->v, sizeof *r->v * (size_t)r->cap, sizeof *r->v * (size_t)cap);
		r->cap = cap;
	}
	r->v[r->n++] = (responseEntry){key, value};
}

static void props_append(openapiSchemaObjectProperties *p, Str key, openapiSchemaObject value)
{
	if (p->n == p->cap) {
		int cap = p->cap ? p->cap * 2 : 8;
		p->v = xrealloc(p->v, sizeof *p->v * (size_t)p->cap, sizeof *p->v * (size_t)cap);
		p->cap = cap;
	}
	openapiSchemaObject *v = xalloc(sizeof *v);
	*v = value;
	p->v[p->n++] = (keyVal){key, v};
}

/* ---- JSON ---- */

/* An error of json.Marshal that the port does not reproduce (the text of upstream names the
 * chain of MarshalJSON calls and byte offsets). */
static NORETURN COLD void json_unsupported(const char *what)
{
	fail("unsupported by protoc-gen-uni: %s (encoding/json fails on it; its error is not reproduced)", what);
}

typedef struct {
	Buf *b;
	bool first;
	const Extensions *ext; /* fields of the embedded alias shadowed by an extension of their name */
} JObj;

static void jbegin(JObj *o, Buf *b, const Extensions *ext)
{
	*o = (JObj){b, true, ext};
	buf_addc(b, '{');
}

/* starts a member; false if an extension shadows it */
static bool jkey(JObj *o, const char *name)
{
	if (o->ext)
		for (int i = 0; i < o->ext->n; i++)
			if (str_eqc(o->ext->v[i].key, name))
				return false;
	if (!o->first)
		buf_addc(o->b, ',');
	o->first = false;
	go_std_json_quote(o->b, str_c(name));
	buf_addc(o->b, ':');
	return true;
}

static void jraw(Buf *b, Str raw, const char *what)
{
	if (!go_std_json_write_raw(b, raw))
		json_unsupported(xcat("invalid JSON in ", what));
}

/* the extension fields after the embedded alias (extensionMarshalJSON) */
static void jend(JObj *o)
{
	for (int i = 0; o->ext && i < o->ext->n; i++) {
		if (!o->first)
			buf_addc(o->b, ',');
		o->first = false;
		go_std_json_quote(o->b, o->ext->v[i].key);
		buf_addc(o->b, ':');
		jraw(o->b, o->ext->v[i].value, "an extension value");
	}
	buf_addc(o->b, '}');
}

static void jstr(JObj *o, const char *name, Str s, bool omitempty)
{
	if ((!omitempty || s.n) && jkey(o, name))
		go_std_json_quote(o->b, s);
}

static void jbool(JObj *o, const char *name, bool v, bool omitempty)
{
	if ((!omitempty || v) && jkey(o, name))
		buf_adds(o->b, v ? "true" : "false");
}

static void jfloat(JObj *o, const char *name, double v)
{
	if (v != 0 && jkey(o, name)) {
		if (v != v || v > 1.7976931348623157e308 || v < -1.7976931348623157e308)
			json_unsupported(xcat("the non-finite number ", name));
		go_std_json_float(o->b, v, 64);
	}
}

/* strconv.FormatUint(v, 10) */
static const char *u64str(uint64_t v)
{
	char d[24];
	int i = 24;
	d[--i] = 0;
	do
		d[--i] = (char)('0' + v % 10);
	while ((v /= 10));
	return xstrdup(d + i);
}

static void juint(JObj *o, const char *name, uint64_t v)
{
	if (v && jkey(o, name))
		buf_adds(o->b, u64str(v));
}

static void jstrs_value(Buf *b, GoStrs s)
{
	if (!s.v) {
		buf_adds(b, "null");
		return;
	}
	buf_addc(b, '[');
	for (int i = 0; i < s.len; i++) {
		if (i)
			buf_addc(b, ',');
		go_std_json_quote(b, s.v[i]);
	}
	buf_addc(b, ']');
}

static void jstrs(JObj *o, const char *name, GoStrs s, bool omitempty)
{
	if ((!omitempty || s.len) && jkey(o, name))
		jstrs_value(o->b, s);
}

static void jany(JObj *o, const char *name, const Any *a)
{
	if (a->kind == ANY_NIL || !jkey(o, name))
		return;
	Buf *b = o->b;
	switch (a->kind) {
	case ANY_STRING:
		go_std_json_quote(b, a->s);
		break;
	case ANY_INT:
		buf_fmt(b, "%lld", a->i);
		break;
	case ANY_STRINGS:
		jstrs_value(b, a->strs);
		break;
	case ANY_INTS:
		buf_addc(b, '[');
		for (int i = 0; i < a->nints; i++) {
			if (i)
				buf_addc(b, ',');
			buf_fmt(b, "%lld", a->ints[i]);
		}
		buf_addc(b, ']');
		break;
	}
}

static void jraw_field(JObj *o, const char *name, RawExample r)
{
	if (r.n && jkey(o, name))
		jraw(o->b, r, xcat("an example (", name, ")"));
}

static void jexternalDocs(JObj *o, const char *name, const openapiExternalDocumentationObject *d)
{
	if (!d || !jkey(o, name))
		return;
	JObj x;
	jbegin(&x, o->b, NULL);
	jstr(&x, "description", d->Description, true);
	jstr(&x, "url", d->URL, true);
	jend(&x);
}

static void json_schema(Buf *b, const openapiSchemaObject *s, bool withExtensions);

static void jcore(JObj *o, const schemaCore *c)
{
	jstr(o, "type", c->Type, true);
	jstr(o, "format", c->Format, true);
	jstr(o, "$ref", c->Ref, true);
	jbool(o, "x-nullable", c->XNullable, true);
	jraw_field(o, "example", c->Example);
	if (c->Items && jkey(o, "items"))
		json_schema(o->b, c->Items, false); /* openapiItemsObject: no MarshalJSON */
	jany(o, "enum", &c->Enum);
	jany(o, "default", &c->Default);
}

/* openapiSchemaObjectProperties.MarshalJSON */
static void json_properties(Buf *b, const openapiSchemaObjectProperties *p)
{
	buf_addc(b, '{');
	for (int i = 0; i < p->n; i++) {
		if (i)
			buf_addc(b, ',');
		go_std_json_quote(b, p->v[i].Key);
		buf_addc(b, ':');
		json_schema(b, p->v[i].Value, true);
	}
	buf_addc(b, '}');
}

/* openapiSchemaObject.MarshalJSON (withExtensions) or openapiItemsObject */
static void json_schema(Buf *b, const openapiSchemaObject *s, bool withExtensions)
{
	JObj o;
	jbegin(&o, b, withExtensions ? &s->extensions : NULL);
	jcore(&o, &s->core);
	if (s->Properties && jkey(&o, "properties"))
		json_properties(b, s->Properties);
	if (s->addKind != ADDPROP_NONE && jkey(&o, "additionalProperties")) {
		if (s->addKind == ADDPROP_ALLOWED)
			buf_adds(b, "true");
		else
			json_schema(b, s->AdditionalProperties, true);
	}
	jstr(&o, "description", s->Description, true);
	jstr(&o, "title", s->Title, true);
	jexternalDocs(&o, "externalDocs", s->ExternalDocs);
	jbool(&o, "readOnly", s->ReadOnly, true);
	jfloat(&o, "multipleOf", s->MultipleOf);
	jfloat(&o, "maximum", s->Maximum);
	jbool(&o, "exclusiveMaximum", s->ExclusiveMaximum, true);
	jfloat(&o, "minimum", s->Minimum);
	jbool(&o, "exclusiveMinimum", s->ExclusiveMinimum, true);
	juint(&o, "maxLength", s->MaxLength);
	juint(&o, "minLength", s->MinLength);
	jstr(&o, "pattern", s->Pattern, true);
	juint(&o, "maxItems", s->MaxItems);
	juint(&o, "minItems", s->MinItems);
	jbool(&o, "uniqueItems", s->UniqueItems, true);
	juint(&o, "maxProperties", s->MaxProperties);
	juint(&o, "minProperties", s->MinProperties);
	jstrs(&o, "required", s->Required, true);
	if (s->nAllOf && jkey(&o, "allOf")) {
		buf_addc(b, '[');
		for (int i = 0; i < s->nAllOf; i++) {
			if (i)
				buf_addc(b, ',');
			JObj a;
			jbegin(&a, b, NULL);
			jstr(&a, "$ref", s->AllOf[i], true);
			jend(&a);
		}
		buf_addc(b, ']');
	}
	if (!withExtensions)
		o.ext = NULL;
	jend(&o);
}

static int cmp_keyed(const void *a, const void *b) { return str_cmp(*(const Str *)a, *(const Str *)b); }

/* the indexes of n entries whose first member is a Str key, sorted by key (byte order) */
static int *sorted_by_key(const void *v, size_t size, int n)
{
	typedef struct {
		Str key;
		int index;
	} K;
	K *k = xalloc(sizeof *k * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		k[i] = (K){*(const Str *)((const char *)v + size * (size_t)i), i};
	sort_any(k, (size_t)n, sizeof *k, cmp_keyed);
	int *order = xalloc(sizeof *order * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		order[i] = k[i].index;
	return order;
}

static void json_header(Buf *b, const openapiHeaderObject *h)
{
	JObj o;
	jbegin(&o, b, NULL);
	jstr(&o, "description", h->Description, true);
	jstr(&o, "type", h->Type, true);
	jstr(&o, "format", h->Format, true);
	jraw_field(&o, "default", h->Default);
	jstr(&o, "pattern", h->Pattern, true);
	jend(&o);
}

static void json_response(Buf *b, const openapiResponseObject *r)
{
	JObj o;
	jbegin(&o, b, &r->extensions);
	jstr(&o, "description", r->Description, false);
	if (jkey(&o, "schema"))
		json_schema(b, &r->Schema, true);
	if (r->Examples.n && jkey(&o, "examples")) {
		int *order = sorted_by_key(r->Examples.v, sizeof *r->Examples.v, r->Examples.n);
		buf_addc(b, '{');
		for (int i = 0; i < r->Examples.n; i++) {
			const exampleEntry *e = &r->Examples.v[order[i]];
			if (i)
				buf_addc(b, ',');
			go_std_json_quote(b, e->key);
			buf_addc(b, ':');
			if (e->raw)
				jraw(b, e->value, "an example");
			else
				go_std_json_quote(b, e->value);
		}
		buf_addc(b, '}');
	}
	if (r->Headers.n && jkey(&o, "headers")) {
		int *order = sorted_by_key(r->Headers.v, sizeof *r->Headers.v, r->Headers.n);
		buf_addc(b, '{');
		for (int i = 0; i < r->Headers.n; i++) {
			if (i)
				buf_addc(b, ',');
			go_std_json_quote(b, r->Headers.v[order[i]].key);
			buf_addc(b, ':');
			json_header(b, &r->Headers.v[order[i]].value);
		}
		buf_addc(b, '}');
	}
	jend(&o);
}

static void json_secreq(Buf *b, const openapiSecurityRequirementObject *r)
{
	int *order = sorted_by_key(r->v, sizeof *r->v, r->n);
	buf_addc(b, '{');
	for (int i = 0; i < r->n; i++) {
		if (i)
			buf_addc(b, ',');
		go_std_json_quote(b, r->v[order[i]].key);
		buf_addc(b, ':');
		jstrs_value(b, r->v[order[i]].value);
	}
	buf_addc(b, '}');
}

static void json_security(Buf *b, const SecurityRequirements *s)
{
	buf_addc(b, '[');
	for (int i = 0; i < s->n; i++) {
		if (i)
			buf_addc(b, ',');
		json_secreq(b, &s->v[i]);
	}
	buf_addc(b, ']');
}

static void json_parameter(Buf *b, const openapiParameterObject *p)
{
	JObj o;
	jbegin(&o, b, &p->extensions);
	jstr(&o, "name", p->Name, false);
	jstr(&o, "description", p->Description, true);
	jstr(&o, "in", p->In, true);
	jbool(&o, "required", p->Required, false);
	jbool(&o, "deprecated", p->Deprecated, true);
	jstr(&o, "type", p->Type, true);
	jstr(&o, "format", p->Format, true);
	jbool(&o, "uniqueItems", p->UniqueItems, true);
	if (p->Items && jkey(&o, "items"))
		json_schema(b, p->Items, false);
	jany(&o, "enum", &p->Enum);
	jstr(&o, "collectionFormat", p->CollectionFormat, true);
	jany(&o, "default", &p->Default);
	if (p->MinItems && jkey(&o, "minItems"))
		buf_fmt(b, "%d", *p->MinItems);
	if (p->MaxItems && jkey(&o, "maxItems"))
		buf_fmt(b, "%d", *p->MaxItems);
	jstr(&o, "pattern", p->Pattern, true);
	jraw_field(&o, "x-example", p->XExample);
	if (p->Schema && jkey(&o, "schema"))
		json_schema(b, p->Schema, true);
	jend(&o);
}

static void json_operation(Buf *b, const openapiOperationObject *op)
{
	JObj o;
	jbegin(&o, b, &op->extensions);
	jstr(&o, "summary", op->Summary, true);
	jstr(&o, "description", op->Description, true);
	jstr(&o, "operationId", op->OperationID, false);
	if (jkey(&o, "responses")) {
		const openapiResponsesObject *r = op->Responses;
		if (!r) {
			buf_adds(b, "null");
		} else {
			int *order = sorted_by_key(r->v, sizeof *r->v, r->n);
			buf_addc(b, '{');
			for (int i = 0; i < r->n; i++) {
				if (i)
					buf_addc(b, ',');
				go_std_json_quote(b, r->v[order[i]].key);
				buf_addc(b, ':');
				json_response(b, &r->v[order[i]].value);
			}
			buf_addc(b, '}');
		}
	}
	if (op->Parameters.n && jkey(&o, "parameters")) {
		buf_addc(b, '[');
		for (int i = 0; i < op->Parameters.n; i++) {
			if (i)
				buf_addc(b, ',');
			json_parameter(b, &op->Parameters.v[i]);
		}
		buf_addc(b, ']');
	}
	jstrs(&o, "tags", op->Tags, true);
	jbool(&o, "deprecated", op->Deprecated, true);
	jstrs(&o, "consumes", op->Consumes, true);
	jstrs(&o, "produces", op->Produces, true);
	if (op->Security && jkey(&o, "security")) {
		if (!op->Security->nonnil)
			buf_adds(b, "null");
		else
			json_security(b, op->Security);
	}
	jexternalDocs(&o, "externalDocs", op->ExternalDocs);
	jend(&o);
}

static void json_pathItem(Buf *b, const openapiPathItemObject *p)
{
	JObj o;
	jbegin(&o, b, NULL);
	const struct {
		const char *name;
		const openapiOperationObject *op;
	} ops[] = {{"get", p->Get},	{"delete", p->Delete}, {"post", p->Post},	{"put", p->Put},
		   {"patch", p->Patch}, {"head", p->Head},     {"options", p->Options}};
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++)
		if (ops[i].op && jkey(&o, ops[i].name))
			json_operation(b, ops[i].op);
	jend(&o);
}

static void json_tag(Buf *b, const openapiTagObject *t)
{
	JObj o;
	jbegin(&o, b, &t->extensions);
	jstr(&o, "name", t->Name, false);
	jstr(&o, "description", t->Description, true);
	jexternalDocs(&o, "externalDocs", t->ExternalDocs);
	jend(&o);
}

static void json_securityScheme(Buf *b, const openapiSecuritySchemeObject *s)
{
	JObj o;
	jbegin(&o, b, &s->extensions);
	jstr(&o, "type", s->Type, false);
	jstr(&o, "description", s->Description, true);
	jstr(&o, "name", s->Name, true);
	jstr(&o, "in", s->In, true);
	jstr(&o, "flow", s->Flow, true);
	jstr(&o, "authorizationUrl", s->AuthorizationURL, true);
	jstr(&o, "tokenUrl", s->TokenURL, true);
	if (s->Scopes.n && jkey(&o, "scopes")) {
		int *order = sorted_by_key(s->Scopes.v, sizeof *s->Scopes.v, s->Scopes.n);
		buf_addc(b, '{');
		for (int i = 0; i < s->Scopes.n; i++) {
			if (i)
				buf_addc(b, ',');
			go_std_json_quote(b, s->Scopes.v[order[i]].key);
			buf_addc(b, ':');
			go_std_json_quote(b, s->Scopes.v[order[i]].value);
		}
		buf_addc(b, '}');
	}
	jend(&o);
}

/* json.Encoder{SetIndent("", "  ")}.Encode(swagger) */
static void json_swagger(Buf *out, const openapiSwaggerObject *s)
{
	Buf b = {0};
	JObj o;
	jbegin(&o, &b, &s->extensions);
	jstr(&o, "swagger", s->Swagger, false);
	if (jkey(&o, "info")) {
		const openapiInfoObject *in = &s->Info;
		JObj i;
		jbegin(&i, &b, &in->extensions);
		jstr(&i, "title", in->Title, false);
		jstr(&i, "description", in->Description, true);
		jstr(&i, "termsOfService", in->TermsOfService, true);
		jstr(&i, "version", in->Version, false);
		if (in->Contact && jkey(&i, "contact")) {
			JObj c;
			jbegin(&c, &b, NULL);
			jstr(&c, "name", in->Contact->Name, true);
			jstr(&c, "url", in->Contact->URL, true);
			jstr(&c, "email", in->Contact->Email, true);
			jend(&c);
		}
		if (in->License && jkey(&i, "license")) {
			JObj l;
			jbegin(&l, &b, NULL);
			jstr(&l, "name", in->License->Name, true);
			jstr(&l, "url", in->License->URL, true);
			jend(&l);
		}
		jend(&i);
	}
	if (s->Tags.n && jkey(&o, "tags")) {
		buf_addc(&b, '[');
		for (int i = 0; i < s->Tags.n; i++) {
			if (i)
				buf_addc(&b, ',');
			json_tag(&b, &s->Tags.v[i]);
		}
		buf_addc(&b, ']');
	}
	jstr(&o, "host", s->Host, true);
	jstr(&o, "basePath", s->BasePath, true);
	jstrs(&o, "schemes", s->Schemes, true);
	jstrs(&o, "consumes", s->Consumes, false);
	jstrs(&o, "produces", s->Produces, false);
	if (jkey(&o, "paths")) {
		/* openapiPathsObject.MarshalJSON */
		buf_addc(&b, '{');
		for (int i = 0; i < s->Paths.n; i++) {
			if (i)
				buf_addc(&b, ',');
			go_std_json_quote(&b, s->Paths.v[i].Path);
			buf_addc(&b, ':');
			json_pathItem(&b, &s->Paths.v[i].PathItemObject);
		}
		buf_addc(&b, '}');
	}
	if (jkey(&o, "definitions")) {
		const openapiDefinitionsObject *d = s->Definitions;
		int *order = sorted_by_key(d->v, sizeof *d->v, d->n);
		buf_addc(&b, '{');
		for (int i = 0; i < d->n; i++) {
			if (i)
				buf_addc(&b, ',');
			go_std_json_quote(&b, d->v[order[i]].key);
			buf_addc(&b, ':');
			json_schema(&b, &d->v[order[i]].value, true);
		}
		buf_addc(&b, '}');
	}
	if (s->SecurityDefinitions && s->SecurityDefinitions->n && jkey(&o, "securityDefinitions")) {
		const openapiSecurityDefinitionsObject *d = s->SecurityDefinitions;
		int *order = sorted_by_key(d->v, sizeof *d->v, d->n);
		buf_addc(&b, '{');
		for (int i = 0; i < d->n; i++) {
			if (i)
				buf_addc(&b, ',');
			go_std_json_quote(&b, d->v[order[i]].key);
			buf_addc(&b, ':');
			json_securityScheme(&b, &d->v[order[i]].value);
		}
		buf_addc(&b, '}');
	}
	if (s->Security.n && jkey(&o, "security"))
		json_security(&b, &s->Security);
	jexternalDocs(&o, "externalDocs", s->ExternalDocs);
	jend(&o);
	go_std_json_indent(out, buf_view(&b), "", "  ");
	buf_addc(out, '\n');
}

/* ---- YAML ---- */

static YamlNode *ystr(Str s) { return yaml_node_string(s); }

static bool utf8_valid(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xFFFD && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

static YamlNode *yplain(const char *s)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->value = str_c(s);
	return n;
}

static YamlNode *yfloat(double f)
{
	const char *s;
	if (f != f)
		s = ".nan";
	else if (f > 1.7976931348623157e308)
		s = ".inf";
	else if (f < -1.7976931348623157e308)
		s = "-.inf";
	else
		s = go_std_format_float(f, 'g', -1, 64);
	return yplain(s);
}

/* a struct being encoded (structv): its fields, then the inline extension map */
typedef struct {
	YamlNode *m;
	const char *const *fieldNames; /* every field of the struct (FieldsMap), NULL-terminated */
} YObj;

static void ybegin(YObj *o, const char *const *fieldNames)
{
	o->m = yaml_node_new(YAML_MAPPING_NODE);
	o->fieldNames = fieldNames;
}

static void yfield(YObj *o, const char *name, YamlNode *v)
{
	yaml_node_append(o->m, ystr(str_c(name)));
	yaml_node_append(o->m, v);
}

static void ystrf(YObj *o, const char *name, Str s, bool omitempty)
{
	if (!omitempty || s.n)
		yfield(o, name, ystr(s));
}

static void ybool(YObj *o, const char *name, bool v, bool omitempty)
{
	if (!omitempty || v)
		yfield(o, name, yplain(v ? "true" : "false"));
}

static void yfloatf(YObj *o, const char *name, double v)
{
	if (v != 0)
		yfield(o, name, yfloat(v));
}

static void yuint(YObj *o, const char *name, uint64_t v)
{
	if (v)
		yfield(o, name, yplain(u64str(v)));
}

static YamlNode *ystrs_value(GoStrs s)
{
	if (!s.v)
		return yplain("null");
	YamlNode *n = yaml_node_new(YAML_SEQUENCE_NODE);
	n->content_set = true;
	for (int i = 0; i < s.len; i++)
		yaml_node_append(n, ystr(s.v[i]));
	return n;
}

static void ystrsf(YObj *o, const char *name, GoStrs s, bool omitempty)
{
	if (!omitempty || s.len)
		yfield(o, name, ystrs_value(s));
}

static void yany(YObj *o, const char *name, const Any *a)
{
	YamlNode *n;
	switch (a->kind) {
	case ANY_NIL:
		return;
	case ANY_STRING:
		n = ystr(a->s);
		break;
	case ANY_INT:
		n = yplain(xfmt("%lld", a->i));
		break;
	case ANY_STRINGS:
		n = ystrs_value(a->strs);
		break;
	default:
		n = yaml_node_new(YAML_SEQUENCE_NODE);
		n->content_set = true;
		for (int i = 0; i < a->nints; i++)
			yaml_node_append(n, yplain(xfmt("%lld", a->ints[i])));
	}
	yfield(o, name, n);
}

/* a map's mapping node with the keys in keyList order */
static YamlNode *ymap(const void *v, size_t size, int n, YamlNode *(*value)(const void *entry))
{
	Str *keys = xalloc(sizeof *keys * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		keys[i] = *(const Str *)((const char *)v + size * (size_t)i);
	int *order = xalloc(sizeof *order * (size_t)(n + 1));
	yaml_sort_keys(order, keys, n);
	YamlNode *m = yaml_node_new(YAML_MAPPING_NODE);
	m->content_set = true;
	for (int i = 0; i < n; i++) {
		yaml_node_append(m, ystr(keys[order[i]]));
		yaml_node_append(m, value((const char *)v + size * (size_t)order[i]));
	}
	return m;
}

/* RawExample.MarshalYAML: json.Unmarshal into an interface{}, then that value */
static YamlNode *yjson(const go_std_json_value *v)
{
	switch (v->kind) {
	case GO_JSON_NULL:
		return yplain("null");
	case GO_JSON_BOOL:
		return yplain(v->b ? "true" : "false");
	case GO_JSON_NUMBER:
		return yfloat(v->f);
	case GO_JSON_STRING:
		return ystr(v->s);
	case GO_JSON_ARRAY: {
		YamlNode *n = yaml_node_new(YAML_SEQUENCE_NODE);
		n->content_set = true;
		for (int i = 0; i < v->n; i++)
			yaml_node_append(n, yjson(v->items[i].value));
		return n;
	}
	default: {
		Str *keys = xalloc(sizeof *keys * (size_t)(v->n + 1));
		for (int i = 0; i < v->n; i++)
			keys[i] = v->items[i].key;
		int *order = xalloc(sizeof *order * (size_t)(v->n + 1));
		yaml_sort_keys(order, keys, v->n);
		YamlNode *m = yaml_node_new(YAML_MAPPING_NODE);
		m->content_set = true;
		for (int i = 0; i < v->n; i++) {
			yaml_node_append(m, ystr(keys[order[i]]));
			yaml_node_append(m, yjson(v->items[order[i]].value));
		}
		return m;
	}
	}
}

static YamlNode *yraw(RawExample r)
{
	const char *err;
	go_std_json_value *v = go_std_json_unmarshal_any(r, &err);
	if (!v) {
		if (strncmp(err, "unsupported", 11) == 0)
			fail("%s", err);
		fail("unsupported by protoc-gen-uni: invalid JSON in an example or extension value (its encoding/json "
		     "error is not reproduced)");
	}
	return yjson(v);
}

static void yrawf(YObj *o, const char *name, RawExample r)
{
	if (r.n)
		yfield(o, name, yraw(r));
}

/* the inline extensions map (sorted keys); a key that is a field of the struct makes yaml.v3
 * panic */
static YamlNode *yend(YObj *o, const Extensions *ext)
{
	if (ext && ext->n) {
		Str *keys = xalloc(sizeof *keys * (size_t)(ext->n + 1));
		for (int i = 0; i < ext->n; i++) {
			keys[i] = ext->v[i].key;
			for (const char *const *f = o->fieldNames; f && *f; f++)
				if (str_eqc(keys[i], *f))
					fail("unsupported by protoc-gen-uni: the extension %s conflicts with a field (yaml.v3 panics)", *f);
		}
		int *order = xalloc(sizeof *order * (size_t)(ext->n + 1));
		yaml_sort_keys(order, keys, ext->n);
		for (int i = 0; i < ext->n; i++) {
			yaml_node_append(o->m, ystr(keys[order[i]]));
			yaml_node_append(o->m, yraw(ext->v[order[i]].value));
		}
	}
	o->m->content_set = true;
	return o->m;
}

static YamlNode *yexternalDocs(const openapiExternalDocumentationObject *d)
{
	YObj o;
	ybegin(&o, NULL);
	ystrf(&o, "description", d->Description, true);
	ystrf(&o, "url", d->URL, true);
	return yend(&o, NULL);
}

static const char *const schemaFields[] = {
	"type", "format", "$ref", "x-nullable", "example", "items", "enum", "default", "properties",
	"additionalProperties", "description", "title", "externalDocs", "readOnly", "multipleOf", "maximum",
	"exclusiveMaximum", "minimum", "exclusiveMinimum", "maxLength", "minLength", "pattern", "maxItems",
	"minItems", "uniqueItems", "maxProperties", "minProperties", "required", "allOf", NULL,
};

static YamlNode *yaml_schema(const openapiSchemaObject *s, bool withExtensions);

static YamlNode *yaml_schema_entry(const void *e) { return yaml_schema(((const keyVal *)e)->Value, true); }

/* openapiSchemaObject.MarshalYAML (withExtensions) or openapiItemsObject */
static YamlNode *yaml_schema(const openapiSchemaObject *s, bool withExtensions)
{
	YObj o;
	ybegin(&o, schemaFields);
	const schemaCore *c = &s->core;
	ystrf(&o, "type", c->Type, true);
	ystrf(&o, "format", c->Format, true);
	ystrf(&o, "$ref", c->Ref, true);
	ybool(&o, "x-nullable", c->XNullable, true);
	yrawf(&o, "example", c->Example);
	if (c->Items)
		yfield(&o, "items", yaml_schema(c->Items, false));
	yany(&o, "enum", &c->Enum);
	yany(&o, "default", &c->Default);
	if (s->Properties) {
		/* openapiSchemaObjectProperties.MarshalYAML: the keys in order */
		YamlNode *m = yaml_node_new(YAML_MAPPING_NODE);
		m->content_set = true;
		for (int i = 0; i < s->Properties->n; i++) {
			yaml_node_append(m, ystr(s->Properties->v[i].Key));
			yaml_node_append(m, yaml_schema_entry(&s->Properties->v[i]));
		}
		yfield(&o, "properties", m);
	}
	if (s->addKind == ADDPROP_ALLOWED)
		yfield(&o, "additionalProperties", yplain("true"));
	else if (s->addKind == ADDPROP_SCHEMA)
		yfield(&o, "additionalProperties", yaml_schema(s->AdditionalProperties, true));
	ystrf(&o, "description", s->Description, true);
	ystrf(&o, "title", s->Title, true);
	if (s->ExternalDocs)
		yfield(&o, "externalDocs", yexternalDocs(s->ExternalDocs));
	ybool(&o, "readOnly", s->ReadOnly, true);
	yfloatf(&o, "multipleOf", s->MultipleOf);
	yfloatf(&o, "maximum", s->Maximum);
	ybool(&o, "exclusiveMaximum", s->ExclusiveMaximum, true);
	yfloatf(&o, "minimum", s->Minimum);
	ybool(&o, "exclusiveMinimum", s->ExclusiveMinimum, true);
	yuint(&o, "maxLength", s->MaxLength);
	yuint(&o, "minLength", s->MinLength);
	ystrf(&o, "pattern", s->Pattern, true);
	yuint(&o, "maxItems", s->MaxItems);
	yuint(&o, "minItems", s->MinItems);
	ybool(&o, "uniqueItems", s->UniqueItems, true);
	yuint(&o, "maxProperties", s->MaxProperties);
	yuint(&o, "minProperties", s->MinProperties);
	ystrsf(&o, "required", s->Required, true);
	if (s->nAllOf) {
		YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
		seq->content_set = true;
		for (int i = 0; i < s->nAllOf; i++) {
			YObj a;
			ybegin(&a, NULL);
			ystrf(&a, "$ref", s->AllOf[i], true);
			yaml_node_append(seq, yend(&a, NULL));
		}
		yfield(&o, "allOf", seq);
	}
	return yend(&o, withExtensions ? &s->extensions : NULL);
}

static YamlNode *yaml_header(const void *e)
{
	const openapiHeaderObject *h = &((const headerEntry *)e)->value;
	YObj o;
	ybegin(&o, NULL);
	ystrf(&o, "description", h->Description, true);
	ystrf(&o, "type", h->Type, true);
	ystrf(&o, "format", h->Format, true);
	yrawf(&o, "default", h->Default);
	ystrf(&o, "pattern", h->Pattern, true);
	return yend(&o, NULL);
}

static YamlNode *yaml_example(const void *e)
{
	const exampleEntry *x = e;
	return x->raw ? yraw(x->value) : ystr(x->value);
}

static YamlNode *yaml_response(const void *e)
{
	static const char *const fields[] = {"description", "schema", "examples", "headers", NULL};
	const openapiResponseObject *r = &((const responseEntry *)e)->value;
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "description", r->Description, false);
	yfield(&o, "schema", yaml_schema(&r->Schema, true));
	if (r->Examples.n)
		yfield(&o, "examples", ymap(r->Examples.v, sizeof *r->Examples.v, r->Examples.n, yaml_example));
	if (r->Headers.n)
		yfield(&o, "headers", ymap(r->Headers.v, sizeof *r->Headers.v, r->Headers.n, yaml_header));
	return yend(&o, &r->extensions);
}

static YamlNode *yaml_secreq_value(const void *e) { return ystrs_value(((const secReqEntry *)e)->value); }

static YamlNode *yaml_security(const SecurityRequirements *s)
{
	YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
	seq->content_set = true;
	for (int i = 0; i < s->n; i++)
		yaml_node_append(seq, ymap(s->v[i].v, sizeof *s->v[i].v, s->v[i].n, yaml_secreq_value));
	return seq;
}

static YamlNode *yaml_parameter(const openapiParameterObject *p)
{
	static const char *const fields[] = {
		"name",	    "description", "in",	       "required", "deprecated", "type",	   "format",
		"uniqueItems", "items",	"enum",	       "collectionFormat", "default", "minItems",
		"maxItems",    "pattern",     "x-example", "schema",   NULL,
	};
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "name", p->Name, false);
	ystrf(&o, "description", p->Description, true);
	ystrf(&o, "in", p->In, true);
	ybool(&o, "required", p->Required, false);
	ybool(&o, "deprecated", p->Deprecated, true);
	ystrf(&o, "type", p->Type, true);
	ystrf(&o, "format", p->Format, true);
	ybool(&o, "uniqueItems", p->UniqueItems, true);
	if (p->Items)
		yfield(&o, "items", yaml_schema(p->Items, false));
	yany(&o, "enum", &p->Enum);
	ystrf(&o, "collectionFormat", p->CollectionFormat, true);
	yany(&o, "default", &p->Default);
	if (p->MinItems)
		yfield(&o, "minItems", yplain(xfmt("%d", *p->MinItems)));
	if (p->MaxItems)
		yfield(&o, "maxItems", yplain(xfmt("%d", *p->MaxItems)));
	ystrf(&o, "pattern", p->Pattern, true);
	yrawf(&o, "x-example", p->XExample);
	if (p->Schema)
		yfield(&o, "schema", yaml_schema(p->Schema, true));
	return yend(&o, &p->extensions);
}

static YamlNode *yaml_operation(const openapiOperationObject *op)
{
	static const char *const fields[] = {"summary",  "description", "operationId", "responses",
					     "parameters", "tags", "deprecated", "consumes", "produces",
					     "security", "externalDocs", NULL};
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "summary", op->Summary, true);
	ystrf(&o, "description", op->Description, true);
	ystrf(&o, "operationId", op->OperationID, false);
	if (!op->Responses)
		yfield(&o, "responses", yplain("null"));
	else
		yfield(&o, "responses", ymap(op->Responses->v, sizeof *op->Responses->v, op->Responses->n, yaml_response));
	if (op->Parameters.n) {
		YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
		seq->content_set = true;
		for (int i = 0; i < op->Parameters.n; i++)
			yaml_node_append(seq, yaml_parameter(&op->Parameters.v[i]));
		yfield(&o, "parameters", seq);
	}
	ystrsf(&o, "tags", op->Tags, true);
	ybool(&o, "deprecated", op->Deprecated, true);
	ystrsf(&o, "consumes", op->Consumes, true);
	ystrsf(&o, "produces", op->Produces, true);
	if (op->Security)
		yfield(&o, "security", op->Security->nonnil ? yaml_security(op->Security) : yplain("null"));
	if (op->ExternalDocs)
		yfield(&o, "externalDocs", yexternalDocs(op->ExternalDocs));
	return yend(&o, &op->extensions);
}

static YamlNode *yaml_tag(const openapiTagObject *t)
{
	static const char *const fields[] = {"name", "description", "externalDocs", NULL};
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "name", t->Name, false);
	ystrf(&o, "description", t->Description, true);
	if (t->ExternalDocs)
		yfield(&o, "externalDocs", yexternalDocs(t->ExternalDocs));
	return yend(&o, &t->extensions);
}

static YamlNode *yaml_scope(const void *e) { return ystr(((const scopeEntry *)e)->value); }

static YamlNode *yaml_securityScheme(const void *e)
{
	static const char *const fields[] = {"type", "description", "name", "in", "flow", "authorizationUrl",
					     "tokenUrl", "scopes", NULL};
	const openapiSecuritySchemeObject *s = &((const secDefEntry *)e)->value;
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "type", s->Type, false);
	ystrf(&o, "description", s->Description, true);
	ystrf(&o, "name", s->Name, true);
	ystrf(&o, "in", s->In, true);
	ystrf(&o, "flow", s->Flow, true);
	ystrf(&o, "authorizationUrl", s->AuthorizationURL, true);
	ystrf(&o, "tokenUrl", s->TokenURL, true);
	if (s->Scopes.n)
		yfield(&o, "scopes", ymap(s->Scopes.v, sizeof *s->Scopes.v, s->Scopes.n, yaml_scope));
	return yend(&o, &s->extensions);
}

static YamlNode *yaml_definition(const void *e) { return yaml_schema(&((const defEntry *)e)->value, true); }

/* yaml.Encoder{SetIndent(2)}.Encode(swagger) */
static void yaml_swagger(Buf *out, const openapiSwaggerObject *s)
{
	static const char *const fields[] = {"swagger",	    "info",	   "tags",	    "host",
					     "basePath",	    "schemes",	   "consumes",	    "produces",
					     "paths",	    "definitions", "securityDefinitions", "security",
					     "externalDocs", NULL};
	YObj o;
	ybegin(&o, fields);
	ystrf(&o, "swagger", s->Swagger, false);
	{
		static const char *const infoFields[] = {"title", "description", "termsOfService", "version", "contact",
							 "license", NULL};
		const openapiInfoObject *in = &s->Info;
		YObj i;
		ybegin(&i, infoFields);
		ystrf(&i, "title", in->Title, false);
		ystrf(&i, "description", in->Description, true);
		ystrf(&i, "termsOfService", in->TermsOfService, true);
		ystrf(&i, "version", in->Version, false);
		if (in->Contact) {
			YObj c;
			ybegin(&c, NULL);
			ystrf(&c, "name", in->Contact->Name, true);
			ystrf(&c, "url", in->Contact->URL, true);
			ystrf(&c, "email", in->Contact->Email, true);
			yfield(&i, "contact", yend(&c, NULL));
		}
		if (in->License) {
			YObj l;
			ybegin(&l, NULL);
			ystrf(&l, "name", in->License->Name, true);
			ystrf(&l, "url", in->License->URL, true);
			yfield(&i, "license", yend(&l, NULL));
		}
		yfield(&o, "info", yend(&i, &in->extensions));
	}
	if (s->Tags.n) {
		YamlNode *seq = yaml_node_new(YAML_SEQUENCE_NODE);
		seq->content_set = true;
		for (int i = 0; i < s->Tags.n; i++)
			yaml_node_append(seq, yaml_tag(&s->Tags.v[i]));
		yfield(&o, "tags", seq);
	}
	ystrf(&o, "host", s->Host, true);
	ystrf(&o, "basePath", s->BasePath, true);
	ystrsf(&o, "schemes", s->Schemes, true);
	ystrsf(&o, "consumes", s->Consumes, false);
	ystrsf(&o, "produces", s->Produces, false);
	{
		/* openapiPathsObject.MarshalYAML: the path keys with Node.SetString */
		YamlNode *m = yaml_node_new(YAML_MAPPING_NODE);
		m->content_set = true;
		for (int i = 0; i < s->Paths.n; i++) {
			YamlNode *k = yaml_node_new(YAML_SCALAR_NODE);
			k->value = s->Paths.v[i].Path;
			/* invalid UTF-8: SetString makes it !!binary, as node() does with an untagged one */
			if (utf8_valid(k->value)) {
				k->tag = STR_LIT("!!str");
				if (memchr(k->value.p, '\n', k->value.n))
					k->style = YAML_LITERAL_STYLE;
			}
			yaml_node_append(m, k);
			const openapiPathItemObject *p = &s->Paths.v[i].PathItemObject;
			YObj pi;
			ybegin(&pi, NULL);
			const struct {
				const char *name;
				const openapiOperationObject *op;
			} ops[] = {{"get", p->Get},	{"delete", p->Delete}, {"post", p->Post},	{"put", p->Put},
				   {"patch", p->Patch}, {"head", p->Head},     {"options", p->Options}};
			for (size_t k2 = 0; k2 < sizeof ops / sizeof *ops; k2++)
				if (ops[k2].op)
					yfield(&pi, ops[k2].name, yaml_operation(ops[k2].op));
			yaml_node_append(m, yend(&pi, NULL));
		}
		yfield(&o, "paths", m);
	}
	yfield(&o, "definitions", ymap(s->Definitions->v, sizeof *s->Definitions->v, s->Definitions->n, yaml_definition));
	if (s->SecurityDefinitions && s->SecurityDefinitions->n)
		yfield(&o, "securityDefinitions",
		       ymap(s->SecurityDefinitions->v, sizeof *s->SecurityDefinitions->v, s->SecurityDefinitions->n,
			    yaml_securityScheme));
	if (s->Security.n)
		yfield(&o, "security", yaml_security(&s->Security));
	if (s->ExternalDocs)
		yfield(&o, "externalDocs", yexternalDocs(s->ExternalDocs));
	YamlNode *doc = yend(&o, &s->extensions);
	const char *err = yaml_encode(doc, 2, out);
	if (err)
		fail("%s", err);
}
