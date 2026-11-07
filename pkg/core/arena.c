/* Arena allocation: a bump allocator over large chunks. Nothing is freed individually; the whole
 * arena is freed by arena_reset() (used between fuzzer iterations).
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

static Chunk *chunks;
static char *arena_p, *arena_end;

static NORETURN COLD void oom(void) { sys_die("protoc-gen-uni: out of memory\n"); }

static void *new_chunk(size_t payload)
{
	if (payload > SIZE_MAX - sizeof(Chunk))
		oom();
	Chunk *c = malloc(sizeof(Chunk) + payload);
	if (!c)
		oom();
	c->prev = chunks;
	chunks = c;
	return c + 1;
}

void *xalloc(size_t n)
{
#ifdef UNI_NO_ARENA
	void *q = new_chunk(n);
	memset(q, 0, n);
	return q;
#else
	if (n > SIZE_MAX - 15)
		oom();
	n = (n + 15) & ~(size_t)15;
	if (n == 0)
		n = 16;
	if ((size_t)(arena_end - arena_p) < n) {
		size_t chunk = n > (1u << 20) ? n : (1u << 20);
		arena_p = new_chunk(chunk);
		arena_end = arena_p + chunk;
	}
	void *r = arena_p;
	arena_p += n;
	memset(r, 0, n);
	return r;
#endif
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

void arena_reset(void)
{
	while (chunks) {
		Chunk *prev = chunks->prev;
		free(chunks);
		chunks = prev;
	}
	arena_p = arena_end = NULL;
}
