/* The ECMAScript standard library as far as it shows in generated code: Number#toString
 * (ECMA-262 Number::toString, radix 10) on the shortest round-trip digits of pkg/num, and plain
 * values with the property order of ordinary objects (for code generators that build JavaScript
 * values and print them). */
#ifndef ES_STD_H
#define ES_STD_H

#include "util.h"

/* String(v) for a JavaScript number: "NaN", "Infinity", "-Infinity", "0" for both zeros, else
 * the shortest digits in plain notation for exponents -7 < e < 21 and in exponent notation
 * ("1e+21", "1.5e-7") otherwise. */
const char *js_number_to_string(double v);

/* ---- value.c ---- */

typedef enum { JS_UNDEFINED, JS_NULL, JS_BOOL, JS_NUMBER, JS_STRING, JS_BIGINT, JS_BYTES, JS_ARRAY, JS_OBJECT } JsType;

typedef struct JsVal JsVal;
typedef struct JsObject JsObject;

/* A value: JS_STRING holds UTF-8 (the code units of the JavaScript string), JS_BIGINT its decimal
 * digits with an optional '-', JS_BYTES a Uint8Array. */
struct JsVal {
	JsType t;
	bool b;
	double num;
	Str s;
	Vec items; /* JS_ARRAY: JsVal* */
	JsObject *obj;
};

JsVal *js_undefined(void);
JsVal *js_null(void);
JsVal *js_bool(bool b);
JsVal *js_number(double v);
JsVal *js_string(Str s);
JsVal *js_bigint(Str digits);
JsVal *js_bytes(Str b);
JsVal *js_array(void);
JsVal *js_object(void);

/* Property access on JS_OBJECT values. Keys are strings (any bytes). Setting "__proto__" changes
 * the prototype and adds no own property, so it is ignored. js_obj_get returns NULL for a
 * missing key. */
void js_obj_set(JsVal *o, Str key, JsVal *v);
JsVal *js_obj_get(const JsVal *o, Str key);
bool js_obj_delete(JsVal *o, Str key);
/* Object.keys order: integer keys (array indices) ascending, then the other keys in insertion
 * order. Returns the number of keys. */
int js_obj_keys(const JsVal *o, Str **keys);
/* ToString of a canonical array index ("0", "17"), or false for any other key. */
bool js_is_array_index(Str key, uint32_t *index);

/* ---- path.c: Node.js `path` (posix) ---- */

char *node_path_normalize(const char *path);
char *node_path_resolve(const char *path); /* process.cwd() is "/" */
char *node_path_relative(const char *from, const char *to);
char *node_path_dirname(const char *path);
char *node_path_basename(const char *path, const char *suffix); /* suffix may be NULL */

#endif
