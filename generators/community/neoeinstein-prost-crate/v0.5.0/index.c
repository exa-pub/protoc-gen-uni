/* Not a port: a hash index of byte strings, so that PackageLimiter::is_allowed and the package
 * search of PackageDependencies take time linear in the package or type name instead of a scan
 * of all only_include prefixes or all packages (upstream's Vec and BTreeMap scans). The results
 * are the same. Lookups of the prefixes of a string reuse its running hash (FNV-1a). */

#define INDEX_HASH_INIT 0xcbf29ce484222325u

static uint64_t index_hash_step(uint64_t h, char c) { return (h ^ (uint8_t)c) * 0x100000001b3u; }

static uint64_t index_hash(const char *s, size_t n)
{
	uint64_t h = INDEX_HASH_INIT;
	for (size_t i = 0; i < n; i++)
		h = index_hash_step(h, s[i]);
	return h;
}

typedef struct {
	uint64_t h;
	const char *s; /* NULL: empty slot */
	size_t n;
	void *val;
} IndexSlot;

typedef struct {
	IndexSlot *slots;
	size_t cap, len; /* cap: 0 or a power of two */
} Index;

/* The value of the key s[0:n] with hash h, or NULL. */
static void *index_get(const Index *ix, uint64_t h, const char *s, size_t n)
{
	if (!ix->cap)
		return NULL;
	for (size_t i = (size_t)(h ^ h >> 32) & (ix->cap - 1);; i = (i + 1) & (ix->cap - 1)) {
		const IndexSlot *e = &ix->slots[i];
		if (!e->s)
			return NULL;
		if (e->h == h && e->n == n && !memcmp(e->s, s, n))
			return e->val;
	}
}

static void index_insert(IndexSlot *slots, size_t cap, IndexSlot e)
{
	size_t i = (size_t)(e.h ^ e.h >> 32) & (cap - 1);
	while (slots[i].s)
		i = (i + 1) & (cap - 1);
	slots[i] = e;
}

/* Adds the key s[0:n] (not in ix, s outlives ix) with hash h. */
static void index_put(Index *ix, uint64_t h, const char *s, size_t n, void *val)
{
	if ((ix->len + 1) * 2 > ix->cap) {
		size_t cap = ix->cap ? ix->cap * 2 : 16;
		IndexSlot *slots = xalloc(sizeof *slots * cap);
		for (size_t i = 0; i < ix->cap; i++)
			if (ix->slots[i].s)
				index_insert(slots, cap, ix->slots[i]);
		ix->slots = slots, ix->cap = cap;
	}
	index_insert(ix->slots, ix->cap, (IndexSlot){h, s, n, val});
	ix->len++;
}
