/* Port of src/generator/features.rs of protoc-gen-prost-crate 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0): a Cargo feature per package
 * (`#[cfg(feature = ...)]` on its module in the include file) and the features section of
 * Cargo.toml, with the dependencies between packages. */

/* An entry of PackageDependencies::dependency_graph, a BTreeMap<&str, BTreeSet<&str>>. */
typedef struct {
	const char *package;
	Vec dependencies; /* const char *: Feature.package of other entries; sorted and unique at the end */
} Feature;

/* The map while it is built: an index by package (see index.c) and the entries in insertion
 * order; sorted at the end. */
typedef struct {
	Index index;
	Vec features; /* Feature * */
	struct Dot {
		size_t len;
		uint64_t h;
	} *dots; /* scratch of depend_on_type: the prefixes followed by a `.` */
	size_t dots_cap;
} Dependencies;

/* features.entry(package).or_default() */
static Feature *feature_entry(Dependencies *d, const char *package)
{
	size_t n = strlen(package);
	uint64_t h = index_hash(package, n);
	Feature *f = index_get(&d->index, h, package, n);
	if (!f) {
		f = xalloc(sizeof *f);
		f->package = package;
		index_put(&d->index, h, package, n, f);
		vec_push(&d->features, f);
	}
	return f;
}

/* depend_on_type: current_package depends on the last key in order, other than current_package,
 * such that `depends_on_type[1..]` starts with key + ".". Those keys are prefixes of each other,
 * so the last is the longest: the prefixes followed by a `.` are looked up longest first. */
static void depend_on_type(Dependencies *d, const char *current_package, const char *depends_on_type)
{
	if (*depends_on_type != '.')
		return;
	const char *t = depends_on_type + 1;
	size_t n = 0;
	uint64_t h = INDEX_HASH_INIT;
	for (size_t i = 0; t[i]; i++) {
		if (t[i] == '.') {
			if (n == d->dots_cap) {
				size_t cap = d->dots_cap ? d->dots_cap * 2 : 64;
				d->dots = xrealloc(d->dots, sizeof *d->dots * d->dots_cap, sizeof *d->dots * cap);
				d->dots_cap = cap;
			}
			d->dots[n++] = (struct Dot){i, h};
		}
		h = index_hash_step(h, t[i]);
	}
	while (n) {
		n--;
		Feature *key = index_get(&d->index, d->dots[n].h, t, d->dots[n].len);
		if (key && !str_eq(key->package, current_package)) {
			vec_push(&feature_entry(d, current_package)->dependencies, (void *)key->package);
			return;
		}
	}
}

static int cmp_feature(const void *a, const void *b)
{
	return strcmp((*(Feature *const *)a)->package, (*(Feature *const *)b)->package);
}

static int cmp_string(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* PackageDependencies::from_iter: the entries sorted by package, their dependencies sorted and
 * unique. */
static Vec package_dependencies(const ProstModuleRequestSet *set)
{
	Dependencies d = {0};
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		if (r->output_filename && *r->proto_package_name)
			feature_entry(&d, r->proto_package_name);
	}
	for (int i = 0; i < set->requests.n; i++) {
		const ProstModuleRequest *r = set->requests.v[i];
		for (int j = 0; j < r->files.n; j++) {
			const PbMsg *file = r->files.v[j];
			const char *package = pb_str(file, FDP_PACKAGE);
			for (int k = 0; k < pb_count(file, FDP_MESSAGE_TYPE); k++) {
				const PbMsg *message = pb_msg_at(file, FDP_MESSAGE_TYPE, k);
				for (int l = 0; l < pb_count(message, DP_FIELD); l++) {
					const PbMsg *field = pb_msg_at(message, DP_FIELD, l);
					depend_on_type(&d, package, pb_str(field, FD_TYPE_NAME));
				}
			}
			for (int k = 0; k < pb_count(file, FDP_SERVICE); k++) {
				const PbMsg *service = pb_msg_at(file, FDP_SERVICE, k);
				for (int l = 0; l < pb_count(service, SDP_METHOD); l++) {
					const PbMsg *method = pb_msg_at(service, SDP_METHOD, l);
					depend_on_type(&d, package, pb_str(method, MDP_INPUT_TYPE));
					depend_on_type(&d, package, pb_str(method, MDP_OUTPUT_TYPE));
				}
			}
		}
	}
	if (d.features.n)
		qsort(d.features.v, (size_t)d.features.n, sizeof *d.features.v, cmp_feature);
	for (int i = 0; i < d.features.n; i++) {
		Vec *deps = &((Feature *)d.features.v[i])->dependencies;
		if (!deps->n)
			continue;
		qsort(deps->v, (size_t)deps->n, sizeof *deps->v, cmp_string);
		int k = 0;
		for (int j = 0; j < deps->n; j++)
			if (!k || deps->v[j] != deps->v[k - 1]) /* equal packages are the same key */
				deps->v[k++] = deps->v[j];
		deps->n = k;
	}
	return d.features;
}

static void push_feature_name(Buf *b, const char *feature, const char *package_separator)
{
	buf_addc(b, '"');
	buf_adds(b, str_replace_all(feature, ".", package_separator));
	buf_addc(b, '"');
}

/* `if let Some('[') = buf.pop() { buf.push('[') }`: drops a trailing comma. */
static void pop_comma(Buf *b)
{
	if (b->n && b->p[b->n - 1] != '[')
		b->p[--b->n] = 0;
}

static void push_file(Vec *files, const char *name, const char *insertion_point, Str content)
{
	ProstFile *f = xalloc(sizeof *f);
	*f = (ProstFile){name, insertion_point, content};
	vec_push(files, f);
}

/* FeaturesGenerator::generate */
static void features_generate(const ProstModuleRequestSet *set, const char *include_filename,
			      const char *package_separator, const PackageLimiter *limiter, Vec *files)
{
	Vec graph = package_dependencies(set);
	Buf buf = {0};
	if (graph.n) {
		buf_adds(&buf, "# @@protoc_deletion_point(features)\n# This section is automatically generated "
			       "by protoc-gen-prost-crate.\n# Changes in this area may be lost on regeneration.\n");
		buf_adds(&buf, "proto_full = [");
		for (int i = 0; i < graph.n; i++) {
			const char *feature = ((Feature *)graph.v[i])->package;
			if (!limiter_is_allowed(limiter, feature))
				continue;
			const char *feature_name = str_replace_all(feature, ".", package_separator);
			buf_addc(&buf, '"');
			buf_adds(&buf, feature_name);
			buf_adds(&buf, "\",");
			push_file(files, include_filename, xcat("attribute:", feature),
				  str_c(xcat("#[cfg(feature = \"", feature_name, "\")]\n")));
		}
		pop_comma(&buf);
		buf_adds(&buf, "]\n");
		for (int i = 0; i < graph.n; i++) {
			const Feature *f = graph.v[i];
			if (!limiter_is_allowed(limiter, f->package))
				continue;
			push_feature_name(&buf, f->package, package_separator);
			if (!f->dependencies.n) {
				buf_adds(&buf, " = []\n");
				continue;
			}
			buf_adds(&buf, " = [");
			for (int j = 0; j < f->dependencies.n; j++) {
				const char *dependency = f->dependencies.v[j];
				if (!limiter_is_allowed(limiter, dependency))
					continue;
				push_feature_name(&buf, dependency, package_separator);
				buf_addc(&buf, ',');
			}
			pop_comma(&buf);
			buf_adds(&buf, "]\n");
		}
	}
	push_file(files, "Cargo.toml", "features", buf_view(&buf));
}
