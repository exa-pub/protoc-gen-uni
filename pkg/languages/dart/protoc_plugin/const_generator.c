/* Port of protoc_plugin 25.1.0 lib/const_generator.dart: JSON data as a Dart constant
 * expression.
 *
 * Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static bool nonEmptyListOrMap(const Jv *x) { return (x->kind == JV_LIST || x->kind == JV_MAP) && x->n > 0; }

static Node *writeJsonConst(const Jv *val);

/* A collection written vertically (addBlock: one item per line with a trailing comma) when one of
 * its values is a non-empty collection, else on one line. */
static Node *json_collection(const Jv *val)
{
	bool vertical = false;
	for (int i = 0; i < val->n && !vertical; i++)
		vertical = nonEmptyListOrMap(val->vals[i]);
	NodeList items = {0};
	for (int i = 0; i < val->n; i++) {
		Node *v = writeJsonConst(val->vals[i]);
		ds_push(&items, val->kind == JV_MAP ? ds_entry(quoted_node(str_c(val->keys[i])), v) : v);
	}
	Node *n = val->kind == JV_MAP ? ds_map(NULL, NULL, items) : ds_list(NULL, NULL, items);
	if (vertical)
		n->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
	return n;
}

static Node *writeJsonConst(const Jv *val)
{
	switch (val->kind) {
	case JV_MAP: case JV_LIST: return json_collection(val);
	case JV_STRING: return quoted_node(val->s);
	case JV_NUMBER: return literal_node(xstrndup(val->s.p, val->s.n));
	case JV_BOOL: return ds_lit(N_BooleanLiteral, TT_IDENTIFIER, val->b ? "true" : "false");
	}
	return NULL;
}
