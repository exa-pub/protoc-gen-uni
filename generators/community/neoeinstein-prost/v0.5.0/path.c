/* Port of src/path.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0):
 * PathMap, which maps fully-qualified protobuf paths to values through path matchers. */

typedef struct {
	const char *matcher, *value;
} PathMatcher;

/* Matchers in insertion order (the order of attributes in the output). */
typedef struct {
	Vec m; /* PathMatcher * */
} PathMap;

static void path_map_insert(PathMap *pm, const char *matcher, const char *value)
{
	PathMatcher *e = xalloc(sizeof *e);
	*e = (PathMatcher){matcher, value};
	vec_push(&pm->m, e);
}

/* A path of up to three parts joined with dots, not concatenated: field paths of long packages
 * would otherwise cost an allocation of the whole path per field. */
typedef struct {
	const char *part[3];
	size_t len[3];
	int k;
	size_t n; /* the whole length */
} FullPath;

/* "<a>[.<b>][.<c>]" (b, c may be NULL) */
static FullPath full_path(const char *a, const char *b, const char *c)
{
	FullPath p = {0};
	const char *parts[3] = {a, b, c};
	for (int i = 0; i < 3; i++) {
		if (!parts[i])
			continue;
		p.part[p.k] = parts[i];
		p.len[p.k] = strlen(parts[i]);
		p.n += (p.k ? 1 : 0) + p.len[p.k];
		p.k++;
	}
	return p;
}

/* p[off..off+m] == s (off + m <= p->n) */
static bool full_path_eq(const FullPath *p, size_t off, const char *s, size_t m)
{
	for (int i = 0; i < p->k && m; i++) {
		if (i) { /* the dot before part i */
			if (off == 0) {
				if (*s != '.')
					return false;
				s++, m--;
			} else {
				off--;
			}
		}
		if (off >= p->len[i]) {
			off -= p->len[i];
			continue;
		}
		size_t k = p->len[i] - off < m ? p->len[i] - off : m;
		if (memcmp(p->part[i] + off, s, k))
			return false;
		s += k, m -= k, off = 0;
	}
	return true;
}

static char full_path_at(const FullPath *p, size_t i)
{
	for (int j = 0; j < p->k; j++) {
		if (j) {
			if (i == 0)
				return '.';
			i--;
		}
		if (i < p->len[j])
			return p->part[j][i];
		i -= p->len[j];
	}
	return 0;
}

/* sub_path_iter yields the path, its suffixes, its prefixes, then ".": for ".a.b.c" that is
 * ".a.b.c", "a.b.c", "b.c", "c", ".a.b", ".a", ".". Suffixes start after a dot and are not empty;
 * prefixes end before a dot and are not empty. Returns the position of the first sub path of
 * full that equals matcher (smaller is earlier), or -1. Upstream compares the matcher with every
 * sub path in turn; this checks the candidates of the matcher's length. */
static long sub_path_rank(const FullPath *full, const char *matcher)
{
	size_t m = strlen(matcher), n = full->n;
	if (m == n && full_path_eq(full, 0, matcher, m))
		return 0;
	if (m && m < n && full_path_at(full, n - m - 1) == '.' && full_path_eq(full, n - m, matcher, m))
		return 1 + (long)(n - m); /* longer suffixes first */
	if (m && m < n && full_path_at(full, m) == '.' && full_path_eq(full, 0, matcher, m))
		return 2 + 2 * (long)n - (long)m; /* longer prefixes first, after every suffix */
	if (str_eq(matcher, "."))
		return 3 + 2 * (long)n;
	return -1;
}

/* Iter::next: the next value (from index *i on) whose matcher is a sub path of path, or NULL. */
static const char *path_map_next(const PathMap *pm, const FullPath *path, int *i)
{
	while (*i < pm->m.n) {
		const PathMatcher *e = pm->m.v[(*i)++];
		if (sub_path_rank(path, e->matcher) >= 0)
			return e->value;
	}
	return NULL;
}

/* PathMap::get(path).next().is_some() */
static bool path_map_any(const PathMap *pm, const char *path)
{
	FullPath p = full_path(path, NULL, NULL);
	int i = 0;
	return path_map_next(pm, &p, &i) != NULL;
}

/* find_best_matching: the first sub path that some matcher equals, then the first such matcher. */
static const char *path_map_get_first(const PathMap *pm, const FullPath *path)
{
	const char *best = NULL;
	long best_rank = -1;
	for (int i = 0; i < pm->m.n; i++) {
		const PathMatcher *e = pm->m.v[i];
		long r = sub_path_rank(path, e->matcher);
		if (r >= 0 && (best_rank < 0 || r < best_rank))
			best = e->value, best_rank = r;
	}
	return best;
}

static const char *path_map_get_first_field(const PathMap *pm, const char *path, const char *field)
{
	FullPath p = full_path(path, field, NULL);
	return path_map_get_first(pm, &p);
}
