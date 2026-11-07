/* libFuzzer harness: one plugin invocation per input.
 *
 * Input: "<parameter>\n<serialized CodeGeneratorRequest>" (scripts/fuzz-corpus.sh builds seeds from
 * the recorded requests). The parameter replaces the one of the request, and the request goes
 * through uni_main() like stdin in main.c. Any input must produce either a parse error (the exit
 * status 1 path of main.c) or a well-formed CodeGeneratorResponse, possibly with an error; a
 * crash, a sanitizer report, a fail() outside of try_run (exits) or a malformed response is a
 * finding.
 *
 * The custom mutator is structure aware: half of the mutations
 * decode the request into nested fields, change one field (payload bytes via libFuzzer, varint
 * values, field numbers, deletion, duplication) and re-encode it with correct lengths, so that
 * mutated requests reach the registry and the generator instead of failing to parse. */
#include "registry.h"

#include <stdlib.h>
#include <string.h>

size_t LLVMFuzzerMutate(uint8_t *data, size_t size, size_t max_size);

static void check_response(const Buf *out)
{
	PbReader r = {(const uint8_t *)out->p, (const uint8_t *)out->p + out->n};
	PbRawField f;
	bool error = false, files = false;
	while (pb_next_field(&r, &f)) {
		if (f.num == CGRESP_ERROR)
			error = true;
		if (f.num == CGRESP_FILE)
			files = true;
	}
	if (r.p != r.end || (error && files))
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const uint8_t *nl = memchr(data, '\n', size);
	size_t pn = nl ? (size_t)(nl - data) : size;
	Buf in = {0}, out = {0};
	if (nl)
		buf_add(&in, nl + 1, size - pn - 1);
	pb_put_bytes(&in, CGR_PARAMETER, (Str){(const char *)data, pn}); /* the last one wins */
	if (!uni_main((const uint8_t *)in.p, in.n, uni_generators, uni_n_generators, &out))
		check_response(&out);
	arena_reset();
	return 0;
}

/* ---- structure-aware mutator ---- */

static uint32_t rng;
static uint32_t rnd(uint32_t n)
{
	rng = rng * 1103515245u + 12345u;
	return n ? (rng >> 8) % n : 0;
}

typedef struct Field Field;
struct Field {
	uint32_t num;
	int wt;
	uint64_t v;            /* varint */
	const uint8_t *raw;    /* other wire types: the value bytes (LEN: the payload) */
	size_t n;
	Field *sub;            /* LEN payloads that parse as messages */
	int n_sub;
};

static int parse(const uint8_t *p, size_t n, Field **out, int depth)
{
	PbReader r = {p, p + n};
	int cap = 8, k = 0;
	Field *v = xalloc(sizeof(Field) * (size_t)cap);
	while (r.p < r.end) {
		const uint8_t *start;
		Field f = {0};
		if (!pb_read_tag(&r, &f.num, &f.wt))
			return -1;
		start = r.p;
		if (f.wt == WT_VARINT) {
			if (!pb_read_varint(&r, &f.v))
				return -1;
		} else if (f.wt == WT_LEN) {
			if (!pb_skip_value(&r, f.num, f.wt, &f.raw, &f.n, 0))
				return -1;
			if (depth < 40 && f.n)
				f.n_sub = parse(f.raw, f.n, &f.sub, depth + 1);
		} else {
			if (!pb_skip_value(&r, f.num, f.wt, NULL, NULL, 0))
				return -1;
			f.raw = start, f.n = (size_t)(r.p - start);
		}
		if (k == cap)
			v = xrealloc(v, sizeof(Field) * (size_t)cap, sizeof(Field) * (size_t)cap * 2), cap *= 2;
		v[k++] = f;
	}
	*out = v;
	return k;
}

static void serialize(const Field *v, int n, Buf *b)
{
	for (int i = 0; i < n; i++) {
		const Field *f = &v[i];
		pb_put_tag(b, f->num, f->wt);
		if (f->wt == WT_VARINT) {
			pb_put_varint(b, f->v);
		} else if (f->wt == WT_LEN && f->n_sub > 0) {
			Buf t = {0};
			serialize(f->sub, f->n_sub, &t);
			pb_put_varint(b, t.n);
			buf_add(b, t.p, t.n);
		} else if (f->wt == WT_LEN) {
			pb_put_varint(b, f->n);
			buf_add(b, f->raw, f->n);
		} else {
			buf_add(b, f->raw, f->n);
		}
	}
}

static const uint64_t interesting[] = {0, 1, 2, 3, 5, 9, 11, 14, 17, 998, 999, 1000, 1001, 1002, 9999,
				       0x7fffffff, 0x80000000, 0xffffffff, 1ull << 32, UINT64_MAX};

/* Applies one mutation to the fields v[0:*n] (or to a nested message). */
static void mutate_fields(Field **pv, int *pn, size_t max_size)
{
	Field *v = *pv;
	int n = *pn;
	if (!n)
		return;
	Field *f = &v[rnd((uint32_t)n)];
	if (f->n_sub > 0 && rnd(3)) {
		mutate_fields(&f->sub, &f->n_sub, max_size);
		return;
	}
	switch (rnd(6)) {
	case 0: /* delete */
		memmove(f, f + 1, sizeof(Field) * (size_t)(n - 1 - (f - v)));
		*pn = n - 1;
		return;
	case 1: { /* duplicate (repeated fields, merges) */
		Field *w = xalloc(sizeof(Field) * (size_t)(n + 1));
		memcpy(w, v, sizeof(Field) * (size_t)n);
		size_t at = rnd((uint32_t)n + 1);
		memmove(w + at + 1, w + at, sizeof(Field) * (size_t)(n - (int)at));
		w[at] = *f;
		*pv = w, *pn = n + 1;
		return;
	}
	case 2: /* field number */
		f->num = f->num > 1 && rnd(2) ? f->num - 1 : f->num + 1;
		return;
	case 3: /* varint value */
		if (f->wt == WT_VARINT) {
			f->v = rnd(2) ? interesting[rnd(sizeof interesting / sizeof *interesting)] : f->v ^ (1ull << rnd(64));
			return;
		}
		/* fallthrough */
	default: { /* payload bytes */
		if (f->wt != WT_LEN)
			return;
		size_t cap = f->n * 2 + 16;
		if (cap > max_size)
			cap = max_size > f->n ? max_size : f->n;
		uint8_t *p = xalloc(cap);
		if (f->n)
			memcpy(p, f->raw, f->n);
		f->n = LLVMFuzzerMutate(p, f->n, cap);
		f->raw = p;
		f->n_sub = 0;
		return;
	}
	}
}

size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size, size_t max_size, unsigned seed)
{
	rng = seed;
	const uint8_t *nl = memchr(data, '\n', size);
	if (!nl || rnd(2) == 0)
		return LLVMFuzzerMutate(data, size, max_size);
	size_t pn = (size_t)(nl - data);
	if (rnd(8) == 0) { /* the parameter only */
		uint8_t *tmp = malloc(max_size);
		if (!tmp)
			return size;
		memcpy(tmp, data, pn);
		size_t m = LLVMFuzzerMutate(tmp, pn, pn + 64);
		for (size_t i = 0; i < m; i++)
			if (tmp[i] == '\n')
				tmp[i] = ',';
		size_t rest = size - pn;
		if (m + rest > max_size)
			m = max_size - rest;
		memmove(data + m, nl, rest);
		memcpy(data, tmp, m);
		free(tmp);
		return m + rest;
	}
	Field *v;
	int n = parse(nl + 1, size - pn - 1, &v, 0);
	size_t res = size;
	if (n >= 0) {
		mutate_fields(&v, &n, max_size);
		Buf b = {0};
		serialize(v, n, &b);
		if (pn + 1 + b.n <= max_size) {
			if (b.n)
				memcpy(data + pn + 1, b.p, b.n);
			res = pn + 1 + b.n;
		}
	} else {
		res = LLVMFuzzerMutate(data, size, max_size);
	}
	arena_reset();
	return res;
}
