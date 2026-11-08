/* Port of src/util.rs of protoc-gen-tonic 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0). */

static char *to_snake(const char *s)
{
	ProstModule *m = prost_module_from_package(s);
	if (m->n != 1)
		fail("assertion `left == right` failed: unexpected `.` in name part\n  left: %d\n right: 1", m->n);
	return m->parts[0];
}

/* Converts a `snake_case` identifier to an `UpperCamel` case Rust type identifier. */
static char *to_upper_camel(const char *s)
{
	char *ident = heck_upper_camel(s);
	/* Suffix an underscore for the `Self` Rust keyword as it is not allowed as raw identifier. */
	return str_eq(ident, "Self") ? "Self_" : ident;
}

/* A SourceCodeInfo.Location and its path. */
typedef struct {
	int32_t *path;
	int n;
	const PbMsg *loc;
} Loc;

/* The locations of a file in request order (upstream searches them unsorted). */
typedef struct {
	bool has_source_info;
	Loc *locs;
	int n;
} Locations;

static Locations file_locations(const PbMsg *file)
{
	Locations l = {0};
	const PbMsg *sci = pb_msg(file, FDP_SOURCE_CODE_INFO);
	if (!sci)
		return l;
	l.has_source_info = true;
	l.n = pb_count(sci, SCI_LOCATION);
	l.locs = xalloc(sizeof *l.locs * (size_t)(l.n ? l.n : 1));
	for (int i = 0; i < l.n; i++) {
		Loc *loc = &l.locs[i];
		loc->loc = pb_msg_at(sci, SCI_LOCATION, i);
		loc->n = pb_count(loc->loc, LOC_PATH);
		loc->path = xalloc(sizeof *loc->path * (size_t)(loc->n ? loc->n : 1));
		for (int j = 0; j < loc->n; j++)
			loc->path[j] = (int32_t)pb_int_at(loc->loc, LOC_PATH, j);
	}
	return l;
}

/* <[i32]>::cmp */
static int path_cmp(const int32_t *a, int an, const int32_t *b, int bn)
{
	for (int i = 0; i < an && i < bn; i++)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return an < bn ? -1 : an > bn;
}

/* get_comments: binary_search_by of Rust 1.91 over the locations; the leading comment of the
 * location with this path, as the only line (*n 1), or no lines. */
static Str *get_comments(const Locations *l, const int32_t *path, int path_n, int *n)
{
	*n = 0;
	if (!l->has_source_info || !l->n)
		return NULL;
	int size = l->n, base = 0;
	while (size > 1) {
		int half = size / 2, mid = base + half;
		if (path_cmp(l->locs[mid].path, l->locs[mid].n, path, path_n) <= 0)
			base = mid;
		size -= half;
	}
	if (path_cmp(l->locs[base].path, l->locs[base].n, path, path_n))
		return NULL;
	Str *leading = xalloc(sizeof *leading);
	*leading = pb_bytes(l->locs[base].loc, LOC_LEADING);
	*n = 1;
	return leading;
}

static Str *get_service_comments(const Locations *l, int service_index, int *n)
{
	int32_t path[] = {6, service_index};
	return get_comments(l, path, 2, n);
}

static Str *get_method_comments(const Locations *l, int service_index, int method_index, int *n)
{
	int32_t path[] = {6, service_index, 2, method_index};
	return get_comments(l, path, 4, n);
}
