/* Arena allocation: a bump allocator over large chunks. Nothing is freed individually; the whole
 * arena is freed by arena_reset() (used between fuzzer iterations), the part allocated after a mark
 * by arena_release(). xalloc_keep() allocates from a second list that only arena_reset() frees.
 *
 * With -DUNI_NO_ARENA (sanitizer builds) every object is its own calloc'ed block, so ASan detects
 * overflows; the blocks are kept on a list for arena_reset(). */
#include "sys.h"

#include <stdlib.h>
#include <string.h>

/* Chunk header; the payload follows, aligned to 16. */
typedef struct Chunk {
	struct Chunk *prev;
	char pad[16 - sizeof(struct Chunk *)];
} Chunk;

static Chunk *chunks, *kept;
static char *arena_p, *arena_end, *kept_p, *kept_end;

static NORETURN COLD void oom(void) { sys_die("protoc-gen-uni: out of memory\n"); }

static void *new_chunk(Chunk **list, size_t payload)
{
	if (payload > SIZE_MAX - sizeof(Chunk))
		oom();
	Chunk *c = malloc(sizeof(Chunk) + payload);
	if (!c)
		oom();
	c->prev = *list;
	*list = c;
	return c + 1;
}

static void *alloc_in(Chunk **list, char **p, char **end, size_t n)
{
#ifdef UNI_NO_ARENA
	(void)p;
	(void)end;
	void *q = new_chunk(list, n);
	memset(q, 0, n);
	return q;
#else
	if (n > SIZE_MAX - 15)
		oom();
	n = (n + 15) & ~(size_t)15;
	if (n == 0)
		n = 16;
	if ((size_t)(*end - *p) < n) {
		size_t chunk = n > (1u << 20) ? n : (1u << 20);
		*p = new_chunk(list, chunk);
		*end = *p + chunk;
	}
	void *r = *p;
	*p += n;
	memset(r, 0, n);
	return r;
#endif
}

void *xalloc(size_t n) { return alloc_in(&chunks, &arena_p, &arena_end, n); }

void *xalloc_keep(size_t n) { return alloc_in(&kept, &kept_p, &kept_end, n); }

ArenaMark arena_mark(void)
{
	ArenaMark m = {chunks, arena_p, arena_end};
	return m;
}

void arena_release(ArenaMark m)
{
	while (chunks && chunks != m.chunks) {
		Chunk *prev = chunks->prev;
		free(chunks);
		chunks = prev;
	}
	arena_p = m.p;
	arena_end = m.end;
}

void *xrealloc(void *p, size_t old, size_t n)
{
	void *r = xalloc(n);
	if (p && old)
		memcpy(r, p, old < n ? old : n);
	return r;
}

char *xstrndup(const char *s, size_t n)
{
	char *r = xalloc(n + 1);
	if (n)
		memcpy(r, s, n);
	return r;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

static void free_list(Chunk **list)
{
	while (*list) {
		Chunk *prev = (*list)->prev;
		free(*list);
		*list = prev;
	}
}

void arena_reset(void)
{
	free_list(&chunks);
	free_list(&kept);
	arena_p = arena_end = kept_p = kept_end = NULL;
}
