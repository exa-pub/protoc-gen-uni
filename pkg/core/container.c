/* Pointer vectors and the string map. */
#include "util.h"

#include <string.h>

void vec_push(Vec *v, void *p)
{
	if (v->n == v->cap) {
		int cap = v->cap ? v->cap * 2 : 8;
		v->v = xrealloc(v->v, sizeof(void *) * (size_t)v->cap, sizeof(void *) * (size_t)cap);
		v->cap = cap;
	}
	v->v[v->n++] = p;
}

int vec_index(const Vec *v, const void *p)
{
	for (int i = 0; i < v->n; i++)
		if (v->v[i] == p)
			return i;
	return -1;
}

/* FNV-1a */
static size_t hash_str(const char *s)
{
	size_t h = (size_t)1469598103934665603u;
	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * (size_t)1099511628211u;
	return h;
}

static size_t map_slot(const Map *m, const char *key)
{
	size_t i = hash_str(key) & (m->cap - 1);
	while (m->keys[i] && strcmp(m->keys[i], key) != 0)
		i = (i + 1) & (m->cap - 1);
	return i;
}

void *map_get(const Map *m, const char *key)
{
	if (!m->cap)
		return NULL;
	size_t i = map_slot(m, key);
	return m->keys[i] ? m->vals[i] : NULL;
}

bool map_has(const Map *m, const char *key) { return m->cap && m->keys[map_slot(m, key)] != NULL; }

void map_put(Map *m, const char *key, void *val)
{
	if ((m->n + 1) * 2 > m->cap) {
		Map n = {0};
		n.cap = m->cap ? m->cap * 2 : 32;
		n.keys = xalloc(sizeof(char *) * n.cap);
		n.vals = xalloc(sizeof(void *) * n.cap);
		for (size_t i = 0; i < m->cap; i++)
			if (m->keys[i]) {
				size_t j = map_slot(&n, m->keys[i]);
				n.keys[j] = m->keys[i];
				n.vals[j] = m->vals[i];
				n.n++;
			}
		*m = n;
	}
	size_t i = map_slot(m, key);
	if (!m->keys[i]) {
		m->keys[i] = key;
		m->n++;
	}
	m->vals[i] = val;
}
