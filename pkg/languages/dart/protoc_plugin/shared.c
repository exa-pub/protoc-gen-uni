/* Ports of protoc_plugin 25.1.0 lib/src/shared.dart (import prefixes, commentBlock, toDartComment)
 * and lib/src/paths.dart.
 *
 * Copyright (c) 2021, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

#define protobufImportPrefix "$pb"
#define asyncImportPrefix "$async"
#define coreImportPrefix "$core"
#define grpcImportPrefix "$grpc"
#define fixnumImportPrefix "$fixnum"
#define mixinImportPrefix "$mixin"

static IntList il_append(IntList a, int x, int y)
{
	IntList r = {xalloc((size_t)(a.n + 2) * sizeof(int)), a.n + 2};
	if (a.n)
		memcpy(r.v, a.v, (size_t)a.n * sizeof(int));
	r.v[a.n] = x;
	r.v[a.n + 1] = y;
	return r;
}

static const char *il_key(IntList p)
{
	Buf b = {0};
	for (int i = 0; i < p.n; i++)
		buf_fmt(&b, i ? ",%d" : "%d", p.v[i]);
	return buf_str(&b);
}

/* Paths */
static IntList buildTopLevelMessagePath(IntList parent, int i) { return il_append(parent, 4, i); }
static IntList buildNestedMessagePath(IntList parent, int i) { return il_append(parent, 3, i); }
static IntList buildFieldPath(IntList parent, Field *field) { return il_append(parent, 2, field->memberNames->sourcePosition); }
static IntList buildOneofPath(IntList parent, OneofNames *oneof) { return il_append(parent, 8, oneof->index); }

/* toDartComment: leading comments as `///` lines; NULL if nothing is left. */
static const char *toDartComment(Str value)
{
	if (!value.n)
		return NULL;
	Str *lines;
	int n = dart_split_lines(value, &lines);
	size_t prefix = 0;
	while (n > 0 && prefix < lines[0].n && lines[0].p[prefix] == ' ')
		prefix++;
	if (prefix > 0) {
		bool all = true;
		for (int i = 0; i < n && all; i++)
			all = lines[i].n == 0 || (lines[i].n >= prefix && memcmp(lines[i].p, lines[0].p, prefix) == 0);
		if (all)
			for (int i = 0; i < n; i++)
				if (lines[i].n) {
					lines[i].p += prefix;
					lines[i].n -= prefix;
				}
	}
	while (n > 0 && dart_trim(lines[n - 1]).n == 0)
		n--;
	if (n == 0)
		return NULL;
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		if (i)
			buf_addc(&b, '\n');
		Buf line = {0};
		buf_adds(&line, "/// ");
		buf_add(&line, lines[i].p, lines[i].n);
		buf_add_str(&b, dart_trim_right(buf_view(&line)));
	}
	return buf_str(&b);
}

typedef struct {
	int count;
	PbMsg *location;
} LocationEntry;

/* commentBlock: the leading comments of the single location at path (several: none, upstream
 * writes "Too many source code locations. Skipping." to stderr). */
static const char *commentBlock(FileGen *f, IntList path)
{
	if (!f->locations) {
		f->locations = xalloc(sizeof *f->locations);
		PbMsg *sci = pb_msg(f->descriptor, FDP_SOURCE_CODE_INFO);
		for (int i = 0; i < pb_count(sci, SCI_LOCATION); i++) {
			PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
			const PbSlot *s = pb_slot(loc, LOC_PATH);
			IntList p = {xalloc((size_t)(s ? s->n : 0) * sizeof(int) + 1), s ? s->n : 0};
			for (int j = 0; j < p.n; j++)
				p.v[j] = (int)(int32_t)s->v[j].u;
			const char *key = il_key(p);
			LocationEntry *e = map_get(f->locations, key);
			if (!e) {
				e = xalloc(sizeof *e);
				e->location = loc;
				map_put(f->locations, key, e);
			}
			e->count++;
		}
	}
	LocationEntry *e = map_get(f->locations, il_key(path));
	if (!e || e->count != 1)
		return NULL;
	return toDartComment(pb_bytes(e->location, LOC_LEADING));
}

/* out.println(commentBlock) */
static void out_commentBlock(Out *o, const char *block)
{
	if (!block)
		return;
	Vec lines = str_split(block, '\n');
	for (int i = 0; i < lines.n; i++)
		out_comment(o, VEC_AT(lines, char, i), 0);
}

/* ConditionalConstDefinition */
static const char *ccd_constFieldName(const char *envName)
{
	Vec parts = str_split(envName, '_');
	Buf b = {0};
	buf_addc(&b, '_');
	buf_adds(&b, VEC_AT(parts, char, 0));
	for (int i = 1; i < parts.n; i++)
		buf_adds(&b, capitalize(VEC_AT(parts, char, i)));
	return buf_str(&b);
}

/* const $core.bool _x = $core.bool.fromEnvironment('protobuf.x'); */
static Node *ccd_constDefinition(const char *envName)
{
	Node *init = ds_call(ds_path(coreImportPrefix, "bool", (char *)0), "fromEnvironment", NULL,
			     ds_args(quoted_node(str_c(xcat("protobuf.", envName))), (Node *)0));
	return ds_toplevel(NONE, KW_CONST, core_type("bool"), ccd_constFieldName(envName), init);
}

/* createTernary: `_x ? '' : 'ifFalse'` */
static Node *ccd_createTernary(const char *envName, Str ifFalse)
{
	return ds_cond(ds_id(ccd_constFieldName(envName)), ds_str("''"), quoted_node(ifFalse));
}
