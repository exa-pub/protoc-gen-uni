/* Copyright (c) 2020 InfluxData. Licensed under the MIT License (full text in pbjson_build.c). */

/* Port of src/descriptor.rs of pbjson-build 0.8.0: packages, type paths and the descriptor set,
 * a BTreeMap<TypePath, Descriptor> (a sorted vector here). Upstream panics become fail(). */

/* A Package: its TypeNames. rank orders the packages of a DescriptorSet (0: not in a set). */
typedef struct {
	int n, rank, id;
	const char **parts;
} Package;

/* A TypePath: the package, then the type's TypeNames. The package is shared, not copied, so a
 * child costs its own path only. A Package alone is a TypePath with np == n. */
typedef struct {
	const Package *pkg;
	int np, n;
	const char **path; /* n - np */
} TypePath;

static const char *tp_part(const TypePath *t, int i) { return i < t->np ? t->pkg->parts[i] : t->path[i - t->np]; }

static const char *type_name_new(const char *s)
{
	if (strchr(s, '.'))
		fail("type name cannot contain '.', got \"%s\"", s);
	return s;
}

/* Package::new */
static TypePath *package_new(const char *s)
{
	if (*s == '.')
		fail("package cannot start with '.', got \"%s\"", s);
	Vec parts = str_split(s, '.');
	Package *pkg = xalloc(sizeof *pkg);
	pkg->n = parts.n;
	pkg->parts = xalloc(sizeof *pkg->parts * (size_t)parts.n);
	for (int i = 0; i < parts.n; i++)
		pkg->parts[i] = type_name_new(parts.v[i]);
	TypePath *p = xalloc(sizeof *p);
	p->pkg = pkg;
	p->np = p->n = pkg->n;
	return p;
}

/* TypePath::child */
static TypePath *type_path_child(const TypePath *t, const char *name)
{
	TypePath *c = xalloc(sizeof *c);
	c->pkg = t->pkg;
	c->np = t->np;
	c->n = t->n + 1;
	c->path = xalloc(sizeof *c->path * (size_t)(c->n - c->np));
	if (t->n > t->np)
		memcpy(c->path, t->path, sizeof *c->path * (size_t)(t->n - t->np));
	c->path[t->n - t->np] = type_name_new(name);
	return c;
}

/* <Package as Display>: the snake case identifiers joined with ".". */
static char *package_display(const TypePath *t)
{
	Buf b = {0};
	for (int i = 0; i < t->np; i++) {
		if (i)
			buf_addc(&b, '.');
		buf_adds(&b, snake_ident(t->pkg->parts[i]));
	}
	return xstrdup(buf_str(&b));
}

/* <TypePath as Display>: the package, then the type names as they are. */
static char *type_path_display(const TypePath *t)
{
	Buf b = {0};
	buf_adds(&b, package_display(t));
	for (int i = t->np; i < t->n; i++) {
		buf_addc(&b, '.');
		buf_adds(&b, tp_part(t, i));
	}
	return xstrdup(buf_str(&b));
}

/* Ord of Vec<TypeName>: element-wise byte order, then length. */
static int names_cmp(const char *const *a, int na, const char *const *b, int nb)
{
	for (int i = 0; i < na && i < nb; i++) {
		int c = strcmp(a[i], b[i]);
		if (c)
			return c < 0 ? -1 : 1;
	}
	return na < nb ? -1 : na > nb ? 1 : 0;
}

static int package_cmp(const Package *a, const Package *b)
{
	if (a == b)
		return 0;
	if (a->rank && b->rank)
		return a->rank < b->rank ? -1 : 1;
	return names_cmp(a->parts, a->n, b->parts, b->n);
}

static bool package_eq(const TypePath *a, const TypePath *b) { return !package_cmp(a->pkg, b->pkg); }

/* The derived Ord of TypePath: package, then path. */
static int type_path_cmp(const TypePath *a, const TypePath *b)
{
	int c = package_cmp(a->pkg, b->pkg);
	return c ? c : names_cmp(a->path, a->n - a->np, b->path, b->n - b->np);
}

/* TypePath::prefix_match: the length of the match in path segments, or -1. */
static int prefix_match(const TypePath *t, const char *prefix)
{
	if (*prefix != '.')
		fail("prefix must start with a '.'");
	prefix++;
	if (!*prefix)
		return 0;
	int len = 0;
	const char *seg = prefix;
	for (int i = 0;; i++) {
		if (!seg) /* the prefix is exhausted */
			return len;
		if (i >= t->n) /* the prefix is longer */
			return -1;
		const char *dot = strchr(seg, '.');
		size_t sn = dot ? (size_t)(dot - seg) : strlen(seg);
		const char *part = tp_part(t, i);
		if (strncmp(part, seg, sn) || part[sn])
			return -1;
		len++;
		seg = dot ? dot + 1 : NULL;
	}
}

enum { SYNTAX_PROTO2, SYNTAX_PROTO3 };
enum { DESC_ENUM, DESC_MESSAGE };

typedef struct {
	TypePath *path;
	int kind;
	PbMsg *proto; /* EnumDescriptorProto or DescriptorProto */
	int syntax;   /* DESC_MESSAGE */
} Descriptor;

/* The BTreeMap is built in two steps: registration keeps the insertion order and finds duplicate
 * paths with a hash map (upstream panics at the first one); descriptor_set_sort then sorts once.
 * Packages are interned, so a TypePath never copies its package. */
typedef struct PbjsonDescriptorSet {
	Vec v;           /* Descriptor *, sorted by path after descriptor_set_sort */
	Map packages;    /* package name -> TypePath * of the package */
	Vec package_list; /* Package * */
	Map paths;       /* "<package id>|<path>" -> Descriptor *: the registered paths */
	int *package_lens, n_package_lens; /* distinct package lengths, ascending */
	Map *found;      /* type name -> Descriptor *: results of descriptor_set_find */
	Map shared_parts; /* Resolver.shared of builder_generate */
} DescriptorSet;

/* MessageDescriptor::is_map */
static bool descriptor_is_map(const Descriptor *d) { return pb_int(pb_msg(d->proto, DP_OPTIONS), 7 /* map_entry */, 0); }

static void register_descriptor(DescriptorSet *set, Descriptor *d)
{
	Buf key = {0};
	buf_fmt(&key, "%d|", d->path->pkg->id);
	for (int i = d->path->np; i < d->path->n; i++) {
		if (i > d->path->np)
			buf_addc(&key, '.');
		buf_adds(&key, tp_part(d->path, i));
	}
	const char *k = xstrdup(buf_str(&key));
	if (map_has(&set->paths, k))
		fail("descriptor already registered for %s", type_path_display(d->path));
	map_put(&set->paths, k, d);
	vec_push(&set->v, d);
}

static void register_enum(DescriptorSet *set, const TypePath *path, PbMsg *proto)
{
	if (!pb_has(proto, EDP_NAME))
		fail("expected name");
	Descriptor *d = xalloc(sizeof *d);
	d->path = type_path_child(path, pb_str(proto, EDP_NAME));
	d->kind = DESC_ENUM;
	d->proto = proto;
	register_descriptor(set, d);
}

static void register_message(DescriptorSet *set, const TypePath *path, PbMsg *proto, int syntax)
{
	if (!pb_has(proto, DP_NAME))
		fail("expected name");
	TypePath *child = type_path_child(path, pb_str(proto, DP_NAME));
	for (int i = 0; i < pb_count(proto, DP_ENUM_TYPE); i++)
		register_enum(set, child, pb_msg_at(proto, DP_ENUM_TYPE, i));
	for (int i = 0; i < pb_count(proto, DP_NESTED_TYPE); i++)
		register_message(set, child, pb_msg_at(proto, DP_NESTED_TYPE, i), syntax);
	Descriptor *d = xalloc(sizeof *d);
	d->path = child;
	d->kind = DESC_MESSAGE;
	d->proto = proto;
	d->syntax = syntax;
	register_descriptor(set, d);
}

/* Package::new, once per distinct package of the set. */
static TypePath *intern_package(DescriptorSet *set, const char *package)
{
	TypePath *path = map_get(&set->packages, package);
	if (!path) {
		path = package_new(package);
		((Package *)path->pkg)->id = set->package_list.n;
		vec_push(&set->package_list, (void *)path->pkg);
		map_put(&set->packages, package, path);
	}
	return path;
}

/* DescriptorSet::register_file_descriptor */
static void register_file_descriptor(DescriptorSet *set, PbMsg *file)
{
	int syntax = SYNTAX_PROTO2;
	if (pb_has(file, FDP_SYNTAX)) {
		const char *s = pb_str(file, FDP_SYNTAX);
		if (str_eq(s, "proto3"))
			syntax = SYNTAX_PROTO3;
		else if (!str_eq(s, "proto2"))
			fail("unknown syntax: %s", s);
	}
	if (!pb_has(file, FDP_PACKAGE))
		fail("expected package");
	TypePath *path = intern_package(set, pb_str(file, FDP_PACKAGE));
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++)
		register_message(set, path, pb_msg_at(file, FDP_MESSAGE_TYPE, i), syntax);
	for (int i = 0; i < pb_count(file, FDP_ENUM_TYPE); i++)
		register_enum(set, path, pb_msg_at(file, FDP_ENUM_TYPE, i));
}

static int cmp_package_ptr(const void *a, const void *b)
{
	const Package *x = *(const Package *const *)a, *y = *(const Package *const *)b;
	return names_cmp(x->parts, x->n, y->parts, y->n);
}

static int cmp_descriptor_ptr(const void *a, const void *b)
{
	return type_path_cmp((*(const Descriptor *const *)a)->path, (*(const Descriptor *const *)b)->path);
}

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

/* Sorts the registered descriptors by path (the BTreeMap order), after ranking the packages. */
COLD static void descriptor_set_sort(DescriptorSet *set)
{
	Vec *pl = &set->package_list;
	if (pl->n)
		qsort(pl->v, (size_t)pl->n, sizeof *pl->v, cmp_package_ptr);
	set->package_lens = xalloc(sizeof *set->package_lens * (size_t)(pl->n ? pl->n : 1));
	for (int i = 0; i < pl->n; i++) {
		Package *p = pl->v[i];
		p->rank = i + 1;
		set->package_lens[i] = p->n;
	}
	if (pl->n)
		qsort(set->package_lens, (size_t)pl->n, sizeof *set->package_lens, cmp_int);
	int n = 0;
	for (int i = 0; i < pl->n; i++)
		if (!n || set->package_lens[n - 1] != set->package_lens[i])
			set->package_lens[n++] = set->package_lens[i];
	set->n_package_lens = n;
	set->found = xalloc(sizeof *set->found);
	if (set->v.n)
		qsort(set->v.v, (size_t)set->v.n, sizeof *set->v.v, cmp_descriptor_ptr);
}

/* The index of the first descriptor whose path is not less than key. */
static int lower_bound(const DescriptorSet *set, const TypePath *key)
{
	int lo = 0, hi = set->v.n;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (type_path_cmp(VEC_AT(set->v, Descriptor, mid)->path, key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* descriptors.iter().find(|(path, _)| path.prefix_match(type_name).is_some()), for a type name
 * starting with '.', without the linear scan: the paths that start with the segments s[0..k] of
 * the type name have the package s[0..j] for some j <= k and a path starting with s[j..k], or a
 * longer package starting with s[0..k]. In each of these groups the first path in order is the
 * lower bound of the group's smallest key, so the first match is the first of these lower bounds
 * that matches. Only package lengths that occur are tried. */
static const Descriptor *descriptor_set_find(const DescriptorSet *set, const char *type_name)
{
	const Descriptor *d = set->found ? map_get(set->found, type_name) : NULL;
	if (d)
		return d;
	if (!type_name[1])
		d = set->v.n ? set->v.v[0] : NULL;
	Vec segs = str_split(type_name + 1, '.');
	int k = segs.n, best = set->v.n;
	for (int i = 0; type_name[1] && i < set->n_package_lens; i++) {
		int j = set->package_lens[i];
		Package pkg = {0};
		TypePath key = {&pkg, 0, 0, NULL};
		if (j <= k) {
			pkg.n = key.np = j;
			pkg.parts = (const char **)segs.v;
			key.n = k;
			key.path = (const char **)segs.v + j;
		} else { /* the smallest package that extends s[0..k] */
			pkg.n = key.np = key.n = k + 1;
			pkg.parts = xalloc(sizeof *pkg.parts * (size_t)(k + 1));
			memcpy(pkg.parts, segs.v, sizeof *pkg.parts * (size_t)k);
			pkg.parts[k] = "";
		}
		int lb = lower_bound(set, &key);
		if (lb < best && prefix_match(VEC_AT(set->v, Descriptor, lb)->path, type_name) >= 0)
			best = lb;
		if (j > k)
			break;
	}
	if (best < set->v.n)
		d = set->v.v[best];
	if (d && set->found)
		map_put(set->found, type_name, (void *)d);
	return d;
}
