/* Small containers standing in for Dart's List, Set and Map of objects (identity semantics). */

static void pv_push(PtrVec *v, void *p)
{
	if (v->n == v->cap) {
		int cap = v->cap ? v->cap * 2 : 4;
		v->v = xrealloc(v->v, (size_t)v->cap * sizeof *v->v, (size_t)cap * sizeof *v->v);
		v->cap = cap;
	}
	v->v[v->n++] = p;
}

static void *pv_last(const PtrVec *v) { return v->v[v->n - 1]; }

static void *pv_pop(PtrVec *v) { return v->v[--v->n]; }

static bool pv_contains(const PtrVec *v, const void *p)
{
	for (int i = 0; i < v->n; i++)
		if (v->v[i] == p)
			return true;
	return false;
}

static uint32_t pm_hash(const void *p)
{
	uintptr_t x = (uintptr_t)p;
	x ^= x >> 17;
	x *= 0xed5ad4bbU;
	x ^= x >> 11;
	return (uint32_t)x;
}

static int pm_find(const PtrMap *m, const void *key)
{
	if (!m->cap)
		return -1;
	uint32_t mask = (uint32_t)m->cap - 1;
	for (uint32_t i = pm_hash(key) & mask;; i = (i + 1) & mask) {
		if (m->keys[i] == key)
			return (int)i;
		if (!m->keys[i])
			return -1;
	}
}

static void pm_put(PtrMap *m, void *key, int val);

static void pm_grow(PtrMap *m)
{
	PtrMap old = *m;
	int cap = old.cap ? old.cap * 2 : 8;
	m->keys = xalloc((size_t)cap * sizeof *m->keys);
	m->vals = xalloc((size_t)cap * sizeof *m->vals);
	m->cap = cap;
	m->n = 0;
	for (int i = 0; i < old.cap; i++)
		if (old.keys[i])
			pm_put(m, old.keys[i], old.vals[i]);
}

static void pm_put(PtrMap *m, void *key, int val)
{
	if ((m->n + 1) * 4 > m->cap * 3)
		pm_grow(m);
	uint32_t mask = (uint32_t)m->cap - 1;
	for (uint32_t i = pm_hash(key) & mask;; i = (i + 1) & mask) {
		if (m->keys[i] == key) {
			m->vals[i] = val;
			return;
		}
		if (!m->keys[i]) {
			m->keys[i] = key;
			m->vals[i] = val;
			m->n++;
			return;
		}
	}
}

static bool pm_has(const PtrMap *m, const void *key) { return pm_find(m, key) >= 0; }

/* Set add: returns true if newly added. */
static bool ps_add(PtrMap *m, void *key)
{
	if (pm_has(m, key))
		return false;
	pm_put(m, key, 1);
	return true;
}

static bool pm_get(const PtrMap *m, const void *key, int *val)
{
	int i = pm_find(m, key);
	if (i < 0)
		return false;
	*val = m->vals[i];
	return true;
}

/* Number of UTF-16 code units of UTF-8 text (Dart String.length). */
static int utf16_len(const char *p, size_t n)
{
	int len = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)p[i];
		if ((c & 0xC0) == 0x80)
			continue;
		len += c >= 0xF0 ? 2 : 1;
	}
	return len;
}
