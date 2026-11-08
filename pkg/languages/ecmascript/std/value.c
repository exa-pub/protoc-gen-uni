/* Plain JavaScript values with the property order of ordinary objects
 * (OrdinaryOwnPropertyKeys, ECMA-262 10.1.11.1): integer indices ascending, then string keys in
 * creation order. */
#include "es_std.h"

#include <string.h>

struct JsObject {
	Vec keys; /* Str* in creation order; NULL for a deleted key */
	Vec vals;
	Map index; /* escaped key -> position + 1 */
};

static JsVal *val(JsType t)
{
	JsVal *v = xalloc(sizeof *v);
	v->t = t;
	return v;
}

JsVal *js_undefined(void) { return val(JS_UNDEFINED); }
JsVal *js_null(void) { return val(JS_NULL); }
JsVal *js_bool(bool b)
{
	JsVal *v = val(JS_BOOL);
	v->b = b;
	return v;
}
JsVal *js_number(double n)
{
	JsVal *v = val(JS_NUMBER);
	v->num = n;
	return v;
}
JsVal *js_string(Str s)
{
	JsVal *v = val(JS_STRING);
	v->s = s;
	return v;
}
JsVal *js_bigint(Str digits)
{
	JsVal *v = val(JS_BIGINT);
	v->s = digits;
	return v;
}
JsVal *js_bytes(Str b)
{
	JsVal *v = val(JS_BYTES);
	v->s = b;
	return v;
}
JsVal *js_array(void) { return val(JS_ARRAY); }
JsVal *js_object(void)
{
	JsVal *v = val(JS_OBJECT);
	v->obj = xalloc(sizeof *v->obj);
	return v;
}

/* A NUL-free form of the key for the map: \x01 -> \x01\x02, NUL -> \x01\x01. */
static const char *index_key(Str key)
{
	if (!memchr(key.p, 0, key.n) && !memchr(key.p, 1, key.n))
		return xstrndup(key.p, key.n);
	Buf b = {0};
	for (size_t i = 0; i < key.n; i++) {
		if (key.p[i] == 0 || key.p[i] == 1) {
			buf_addc(&b, 1);
			buf_addc(&b, key.p[i] == 0 ? 1 : 2);
		} else {
			buf_addc(&b, key.p[i]);
		}
	}
	return buf_str(&b);
}

void js_obj_set(JsVal *o, Str key, JsVal *v)
{
	if (str_eqc(key, "__proto__"))
		return;
	JsObject *ob = o->obj;
	const char *k = index_key(key);
	intptr_t i = (intptr_t)map_get(&ob->index, k);
	if (i) {
		ob->vals.v[i - 1] = v;
		return;
	}
	Str *ks = xalloc(sizeof *ks);
	*ks = key;
	vec_push(&ob->keys, ks);
	vec_push(&ob->vals, v);
	map_put(&ob->index, k, (void *)(intptr_t)ob->keys.n);
}

JsVal *js_obj_get(const JsVal *o, Str key)
{
	intptr_t i = (intptr_t)map_get(&o->obj->index, index_key(key));
	return i ? o->obj->vals.v[i - 1] : NULL;
}

bool js_obj_delete(JsVal *o, Str key)
{
	JsObject *ob = o->obj;
	const char *k = index_key(key);
	intptr_t i = (intptr_t)map_get(&ob->index, k);
	if (!i)
		return false;
	ob->keys.v[i - 1] = NULL;
	ob->vals.v[i - 1] = NULL;
	map_put(&ob->index, k, NULL);
	return true;
}

bool js_is_array_index(Str key, uint32_t *index)
{
	if (!key.n || key.n > 10 || (key.p[0] == '0' && key.n > 1))
		return false;
	uint64_t n = 0;
	for (size_t i = 0; i < key.n; i++) {
		if (key.p[i] < '0' || key.p[i] > '9')
			return false;
		n = n * 10 + (uint64_t)(key.p[i] - '0');
	}
	if (n >= 4294967295u)
		return false;
	*index = (uint32_t)n;
	return true;
}

typedef struct {
	uint32_t index;
	Str key;
} IndexKey;

static void sort_index_keys(IndexKey *a, int n)
{
	/* merge sort: O(n log n), keys are distinct */
	if (n < 2)
		return;
	int h = n / 2;
	sort_index_keys(a, h);
	sort_index_keys(a + h, n - h);
	IndexKey *t = xalloc(sizeof *t * (size_t)n);
	int i = 0, j = h, k = 0;
	while (i < h && j < n)
		t[k++] = a[i].index <= a[j].index ? a[i++] : a[j++];
	while (i < h)
		t[k++] = a[i++];
	while (j < n)
		t[k++] = a[j++];
	memcpy(a, t, sizeof *t * (size_t)n);
}

int js_obj_keys(const JsVal *o, Str **keys)
{
	const JsObject *ob = o->obj;
	Str *out = xalloc(sizeof *out * (size_t)(ob->keys.n + 1));
	IndexKey *ix = xalloc(sizeof *ix * (size_t)(ob->keys.n + 1));
	int n = 0, ni = 0;
	for (int i = 0; i < ob->keys.n; i++) {
		Str *k = ob->keys.v[i];
		uint32_t idx;
		if (k && js_is_array_index(*k, &idx))
			ix[ni++] = (IndexKey){idx, *k};
	}
	sort_index_keys(ix, ni);
	for (int i = 0; i < ni; i++)
		out[n++] = ix[i].key;
	for (int i = 0; i < ob->keys.n; i++) {
		Str *k = ob->keys.v[i];
		uint32_t idx;
		if (k && !js_is_array_index(*k, &idx))
			out[n++] = *k;
	}
	*keys = out;
	return n;
}
