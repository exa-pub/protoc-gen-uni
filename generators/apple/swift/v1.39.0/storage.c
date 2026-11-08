/* Copyright (c) 2014 - 2016 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/protoc-gen-swift/MessageStorageDecision.swift and
 * MessageStorageClassGenerator.swift. */

#define TOTAL_FIELD_COST_REQUIRING_STORAGE 17

typedef struct {
	bool uses_storage;
	int cost_as_field;
} AnalyzeResult;

static const AnalyzeResult use_storage = {true, 1};

/* The process-wide caches of MessageStorageDecision.swift (analysisCache, recursiveMessageCache),
 * reset for every request: keyed by full message name, they carry over between the files of a
 * request like upstream. */
static Map analysis_cache, recursive_cache;
static int recursive_true, recursive_false; /* recursive_cache values */

static void storage_decision_reset(void)
{
	analysis_cache = (Map){0};
	recursive_cache = (Map){0};
	map_put(&analysis_cache, "google.protobuf.Any", (void *)&use_storage);
}

static void set_analysis(const char *name, AnalyzeResult r)
{
	AnalyzeResult *p = xalloc(sizeof *p);
	*p = r;
	map_put(&analysis_cache, name, p);
}

static AnalyzeResult analyze(const SwMessage *d, int depth);

static int estimate(const SwField *f, int depth)
{
	if (sw_field_is_repeated(f))
		return 1;
	if (f->type == PT_GROUP || f->type == PT_MESSAGE)
		return analyze(f->message_type, depth + 1).cost_as_field;
	return 1;
}

typedef struct {
	const SwFile *initial_file;
	Vec stack; /* messageStack */
} RecursionCtx;

static bool recursion_helper(RecursionCtx *c, const SwMessage *d)
{
	if (c->stack.n > MAX_RECURSION)
		fail("messages nested too deeply");
	vec_push(&c->stack, (void *)d);
	bool result = false;
	for (int i = 0; i < d->fields.n && !result; i++) {
		const SwField *f = d->fields.v[i];
		if (sw_field_is_repeated(f))
			continue;
		if (f->type != PT_MESSAGE && f->type != PT_GROUP)
			continue;
		const SwMessage *t = f->message_type;
		if (!t || t->file != c->initial_file)
			continue;
		if (map_get(&recursive_cache, t->full_name) == &recursive_false)
			continue;
		int first = vec_index(&c->stack, t);
		if (first >= 0) {
			for (int j = first; j < c->stack.n; j++) {
				const SwMessage *msg = c->stack.v[j];
				map_put(&recursive_cache, msg->full_name, &recursive_true);
				set_analysis(msg->full_name, use_storage);
			}
			if (first == 0)
				result = true;
			continue;
		}
		result = recursion_helper(c, t);
	}
	map_put(&recursive_cache, d->full_name, result ? &recursive_true : &recursive_false);
	c->stack.n--;
	return result;
}

static bool contains_recursive_singular_field(const SwMessage *d)
{
	void *cached = map_get(&recursive_cache, d->full_name);
	if (cached)
		return cached == &recursive_true;
	RecursionCtx c = {d->file, {0}};
	return recursion_helper(&c, d);
}

static AnalyzeResult analyze_helper(const SwMessage *d, int depth)
{
	if (contains_recursive_singular_field(d))
		return use_storage;
	int fields_cost = 0;
	for (int i = 0; i < d->fields.n; i++) {
		const SwField *f = d->fields.v[i];
		if (f->has_oneof_index)
			continue;
		fields_cost += estimate(f, depth);
		if (fields_cost >= TOTAL_FIELD_COST_REQUIRING_STORAGE)
			return use_storage;
	}
	for (int i = 0; i < d->oneofs.n; i++) {
		const SwOneof *o = d->oneofs.v[i];
		int oneof_cost = 0;
		for (int j = 0; j < o->fields.n; j++) {
			int c = estimate(o->fields.v[j], depth);
			if (c > oneof_cost)
				oneof_cost = c;
			if (fields_cost + oneof_cost >= TOTAL_FIELD_COST_REQUIRING_STORAGE)
				return use_storage;
		}
		fields_cost += oneof_cost;
	}
	return (AnalyzeResult){false, fields_cost};
}

static AnalyzeResult analyze(const SwMessage *d, int depth)
{
	if (depth > MAX_RECURSION)
		fail("messages nested too deeply");
	const AnalyzeResult *cached = map_get(&analysis_cache, d->full_name);
	if (cached)
		return *cached;
	AnalyzeResult r = analyze_helper(d, depth);
	set_analysis(d->full_name, r);
	return r;
}

/* MessageStorageDecision.shouldUseHeapStorage(descriptor:) */
static bool should_use_heap_storage(const SwMessage *d) { return analyze(d, 0).uses_storage; }
