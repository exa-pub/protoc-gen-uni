/* Port of src/lib.rs of protoc-gen-prost-crate 0.5.0
 * (https://github.com/neoeinstein/protoc-gen-prost, Apache-2.0): Parameters and PackageLimiter.
 * execute is in generator.c. */

/* PackageLimiter: the only_include packages, each without its leading `.` and with a trailing `.`;
 * index holds them without the trailing `.` (see index.c). */
typedef struct {
	Vec include_prefixes; /* char * */
	Index index;
} PackageLimiter;

/* PackageLimiter::push: false if package does not start with `.`. */
static bool limiter_push(PackageLimiter *l, const char *package)
{
	if (*package != '.')
		return false;
	vec_push(&l->include_prefixes, xcat(package + 1, "."));
	size_t n = strlen(package + 1);
	uint64_t h = index_hash(package + 1, n);
	if (!index_get(&l->index, h, package + 1, n))
		index_put(&l->index, h, package + 1, n, (void *)1);
	return true;
}

/* PackageLimiter::is_allowed: every package without only_include, else the packages equal to or
 * nested in one of them (`package == prefix[..len - 1] || package.starts_with(prefix)`): the
 * package or one of its prefixes followed by a `.` is in the index. */
static bool limiter_is_allowed(const PackageLimiter *l, const char *package)
{
	if (!l->include_prefixes.n)
		return true;
	if (*package == '.')
		package++;
	uint64_t h = INDEX_HASH_INIT;
	for (size_t i = 0;; i++) {
		if (!package[i])
			return index_get(&l->index, h, package, i) != NULL;
		if (package[i] == '.' && index_get(&l->index, h, package, i))
			return true;
		h = index_hash_step(h, package[i]);
	}
}

typedef struct {
	const char *default_package_filename; /* NULL: "_" */
	const char *include_file;
	bool gen_crate;
	const char *gen_crate_template; /* NULL: "Cargo.toml" */
	bool no_features;
	PackageLimiter only_include;
	const char *package_separator; /* NULL: "-" */
	bool flat_output_dir;
} Parameters;

static bool is_value(const ProstParam *p, const char *name) { return p->kind == PROST_VALUE && str_eq(p->param, name); }

/* Parameters::from_str: NULL or the error. */
static const char *parse_parameters(Parameters *o, Str s)
{
	ProstParam *params;
	int n = prost_params_parse(s, &params);
	for (int i = 0; i < n; i++) {
		const ProstParam *p = &params[i];
		if (str_eq(p->param, "default_package_filename") && p->kind != PROST_KEY_VALUE) {
			o->default_package_filename = prost_param_value(p);
		} else if (is_value(p, "include_file")) {
			o->include_file = p->value;
		} else if (is_value(p, "only_include")) {
			if (!limiter_push(&o->only_include, p->value))
				return xcat("invalid parameter: proto paths must begin with `.`: only_include=", p->value);
		} else if (str_eq(p->param, "gen_crate") && p->kind != PROST_KEY_VALUE) {
			o->gen_crate = true;
			o->gen_crate_template = prost_param_value(p);
		} else if (prost_param_flag(p, "no_features", &o->no_features)) {
		} else if (is_value(p, "package_separator") &&
			   (str_eq(p->value, ".") || str_eq(p->value, "-") || str_eq(p->value, "+") ||
			    str_eq(p->value, "_"))) {
			o->package_separator = p->value;
		} else if (prost_param_flag(p, "flat_output_dir", &o->flat_output_dir)) {
		} else {
			return prost_invalid_parameter(p);
		}
	}
	return NULL;
}

typedef struct {
	Parameters params;
	PbMsg *req;
	Str raw;
	Vec files; /* ProstFile * */
	const char *error;
} Run;
