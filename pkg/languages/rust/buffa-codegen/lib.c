/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/lib.rs (CodeGenConfig, decode options, exclude_package,
 * generate_with_diagnostics) and src/feature_gates.rs (FeatureGates::for_config, validate). */

#define CODEGEN_ERROR(...) xcat("codegen error: ", xfmt(__VA_ARGS__))

void bc_config_default(BcConfig *c)
{
	memset(c, 0, sizeof *c);
	c->generate_views = true;
	c->preserve_unknown_fields = true;
	c->emit_register_fn = true;
	c->generate_with_setters = true;
	c->idiomatic_enum_aliases = true;
	c->type_name_prefix = "";
	c->feature_gate_json = "json";
	c->feature_gate_views = "views";
	c->feature_gate_text = "text";
	c->feature_gate_reflect = "reflect";
}

/* ---- feature_gates.rs ---- */

typedef struct {
	const char *json, *views, *text, *reflect;
} FeatureGates;

static FeatureGates feature_gates_for_config(const BcConfig *c)
{
	bool gate_all = c->gate_impls_on_crate_features;
	bool gate_reflect = gate_all || c->gate_reflect_on_crate_feature;
	FeatureGates g;
	g.json = gate_all && c->generate_json ? c->feature_gate_json : NULL;
	g.views = gate_all && c->generate_views ? c->feature_gate_views : NULL;
	g.text = gate_all && c->generate_text ? c->feature_gate_text : NULL;
	g.reflect = gate_reflect && c->generate_reflection ? c->feature_gate_reflect : NULL;
	return g;
}

static bool is_ascii_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static bool is_valid_feature_name(const char *name)
{
	if (!(is_ascii_alnum(name[0]) || name[0] == '_'))
		return false;
	for (const char *p = name + 1; *p; p++)
		if (!is_ascii_alnum(*p) && *p != '_' && *p != '-' && *p != '+' && *p != '.')
			return false;
	return true;
}

const char *bc_reflect_feature_gate(const BcConfig *c) { return feature_gates_for_config(c).reflect; }

/* ---- lib.rs ---- */

bool bc_package_is_excluded(const char *package, const Vec *excludes)
{
	for (int i = 0; i < excludes->n; i++) {
		const char *ex = VEC_AT(*excludes, char, i);
		size_t n = strlen(ex);
		if (str_eq(package, ex) || (strlen(package) > n && !strncmp(package, ex, n) && package[n] == '.'))
			return true;
	}
	return false;
}

char *bc_normalize_exclude_package(const char *value, const char **err)
{
	Str s = rust_trim(str_c(value));
	if (s.n && s.p[0] == '.')
		s.p++, s.n--;
	bool bad = s.n == 0 || s.p[0] == '.' || s.p[s.n - 1] == '.';
	for (size_t i = 0; i + 1 < s.n; i++)
		if (s.p[i] == '.' && s.p[i + 1] == '.')
			bad = true;
	if (bad) {
		*err = "'exclude_package' requires a non-empty proto package with no empty components, "
		       "e.g. exclude_package=.buf.validate";
		return NULL;
	}
	return xstrndup(s.p, s.n);
}

#define TOOLING_ELEMENT_MEMORY_LIMIT ((uint64_t)1024 * 1024 * 1024)

/* parse_element_memory_limit (usize::from_str: an optional '+', ASCII digits, no overflow). */
static const char *parse_element_memory_limit(Str raw, uint64_t *out)
{
	Str t = rust_trim(raw);
	if (!t.n) {
		*out = TOOLING_ELEMENT_MEMORY_LIMIT;
		return NULL;
	}
	if (str_eqc(t, "unlimited") || str_eqc(t, "max")) {
		*out = UINT64_MAX;
		return NULL;
	}
	size_t i = t.p[0] == '+' ? 1 : 0;
	uint64_t v = 0;
	bool ok = i < t.n;
	for (; i < t.n && ok; i++) {
		unsigned d = (unsigned char)t.p[i] - '0';
		if (d > 9 || v > (UINT64_MAX - d) / 10)
			ok = false;
		else
			v = v * 10 + d;
	}
	if (!ok)
		return xcat("BUFFA_ELEMENT_MEMORY_LIMIT: expected a byte count or 'unlimited', got '",
			    xstrndup(raw.p, raw.n), "'");
	*out = v;
	return NULL;
}

const char *bc_element_memory_limit_opt(Str parameter, uint64_t *limit)
{
	*limit = TOOLING_ELEMENT_MEMORY_LIMIT;
	Str *entries;
	int n = str_split_s(parameter, ',', &entries);
	for (int i = 0; i < n; i++) {
		Str e = rust_trim(entries[i]);
		const char *eq = memchr(e.p, '=', e.n);
		if (!eq)
			continue;
		Str key = rust_trim((Str){e.p, (size_t)(eq - e.p)});
		if (!str_eqc(key, BC_ELEMENT_MEMORY_LIMIT_OPT))
			continue;
		const char *err = parse_element_memory_limit((Str){eq + 1, (size_t)(e.p + e.n - eq - 1)}, limit);
		if (err)
			return err;
	}
	return NULL;
}

/* The element-memory charges of decoding a CodeGeneratorRequest with the buffa-descriptor types
 * (DecodeContext::register_element_memory): every element of a repeated message, string or bytes
 * field costs size_of of its Rust type (buffa 0.9.1 and 0.9.2 on 64-bit targets, measured with
 * rustc). Packed scalars are not charged. One row per field that charges or holds such fields:
 * the message, the field number, the charge (0: a singular message) and the field's message. */
enum {
	EM_NONE, EM_REQ, EM_FILE, EM_MSG, EM_FIELD, EM_ONEOF, EM_ENUM, EM_ENUM_VALUE, EM_SERVICE, EM_METHOD,
	EM_EXT_RANGE, EM_EXT_RANGE_OPTS, EM_FIELD_OPTS, EM_OPTS, EM_UNINTERPRETED, EM_SOURCE_INFO, EM_LOCATION,
};
static const struct {
	uint8_t msg;
	uint16_t num, charge;
	uint8_t child;
} element_charges[] = {
	{EM_REQ, 1, 24, EM_NONE}, {EM_REQ, 15, 704, EM_FILE}, {EM_REQ, 17, 704, EM_FILE},
	{EM_FILE, 3, 24, EM_NONE}, {EM_FILE, 4, 360, EM_MSG}, {EM_FILE, 5, 240, EM_ENUM},
	{EM_FILE, 6, 184, EM_SERVICE}, {EM_FILE, 7, 416, EM_FIELD}, {EM_FILE, 8, 0, EM_OPTS},
	{EM_FILE, 9, 0, EM_SOURCE_INFO}, {EM_FILE, 15, 24, EM_NONE},
	{EM_MSG, 2, 416, EM_FIELD}, {EM_MSG, 3, 360, EM_MSG}, {EM_MSG, 4, 240, EM_ENUM},
	{EM_MSG, 5, 176, EM_EXT_RANGE}, {EM_MSG, 6, 416, EM_FIELD}, {EM_MSG, 7, 0, EM_OPTS},
	{EM_MSG, 8, 152, EM_ONEOF}, {EM_MSG, 9, 40, EM_NONE}, {EM_MSG, 10, 24, EM_NONE},
	{EM_EXT_RANGE, 3, 0, EM_EXT_RANGE_OPTS},
	{EM_EXT_RANGE_OPTS, 2, 88, EM_NONE}, {EM_EXT_RANGE_OPTS, 999, 168, EM_UNINTERPRETED},
	{EM_FIELD, 8, 0, EM_FIELD_OPTS},
	{EM_FIELD_OPTS, 20, 56, EM_NONE}, {EM_FIELD_OPTS, 999, 168, EM_UNINTERPRETED},
	{EM_ONEOF, 2, 0, EM_OPTS},
	{EM_ENUM, 2, 232, EM_ENUM_VALUE}, {EM_ENUM, 3, 0, EM_OPTS}, {EM_ENUM, 4, 40, EM_NONE},
	{EM_ENUM, 5, 24, EM_NONE},
	{EM_ENUM_VALUE, 3, 0, EM_OPTS},
	{EM_SERVICE, 2, 216, EM_METHOD}, {EM_SERVICE, 3, 0, EM_OPTS},
	{EM_METHOD, 4, 0, EM_OPTS},
	{EM_OPTS, 999, 168, EM_UNINTERPRETED}, /* File, Message, Oneof, Enum, EnumValue, Service, Method */
	{EM_UNINTERPRETED, 2, 56, EM_NONE},
	{EM_SOURCE_INFO, 1, 144, EM_LOCATION},
	{EM_LOCATION, 6, 24, EM_NONE},
};

static uint64_t element_footprint(int msg, const uint8_t *p, size_t n, int depth)
{
	uint64_t total = 0;
	PbReader r = {p, p + n};
	PbRawField f;
	while (depth < PB_MAX_DEPTH && pb_next_field(&r, &f)) {
		if (f.wt != WT_LEN)
			continue;
		for (size_t i = 0; i < sizeof element_charges / sizeof *element_charges; i++)
			if (element_charges[i].msg == msg && element_charges[i].num == f.num) {
				total += element_charges[i].charge;
				if (element_charges[i].child != EM_NONE)
					total += element_footprint(element_charges[i].child, f.val, f.vlen, depth + 1);
				break;
			}
	}
	return total;
}

/* decode_failure with describe_byte_budget (the limit is below the footprint, so below 2^63) */
static const char *element_memory_failure(uint64_t limit)
{
	const uint64_t mib = 1024 * 1024;
	char *budget = limit >= mib && limit % mib == 0 ? xfmt("%lld MiB", (long long)(limit / mib))
							: xfmt("%lld-byte", (long long)limit);
	return xcat("failed to decode CodeGeneratorRequest: element memory limit exceeded\nThese descriptors exceed "
		    "the ", budget, " element-memory budget in force. Raise it with the " BC_ELEMENT_MEMORY_LIMIT_OPT
		    "=<bytes> plugin option or the BUFFA_ELEMENT_MEMORY_LIMIT environment variable; either "
		    "accepts 'unlimited'. See \"Very large schemas\" in the buffa guide.");
}

const char *bc_decode_request(Str raw, Str parameter)
{
	uint64_t limit;
	const char *err = bc_element_memory_limit_opt(parameter, &limit);
	if (err)
		return err;
	if (element_footprint(EM_REQ, (const uint8_t *)raw.p, raw.n, 0) > limit)
		return element_memory_failure(limit);
	return NULL;
}

bool bc_is_valid_feature_name(const char *name) { return is_valid_feature_name(name); }

/* CodeGenConfig::validate_type_name_prefix */
static const char *validate_type_name_prefix(const BcConfig *c)
{
	const char *p = c->type_name_prefix;
	bool valid = !*p || (*p >= 'A' && *p <= 'Z');
	for (; *p && valid; p++)
		valid = is_ascii_alnum(*p);
	if (valid)
		return NULL;
	return xcat("invalid type_name_prefix '", c->type_name_prefix,
		    "': must be empty or PascalCase (start with an ASCII uppercase letter, followed by ASCII "
		    "letters and digits only)");
}

/* generate_with_diagnostics: the configuration checks, then generation. */
const char *bc_generate(const BcConfig *c, const PbMsg *req, Vec *files)
{
	if (c->generate_reflection_vtable && !c->generate_reflection)
		return CODEGEN_ERROR("generate_reflection_vtable requires generate_reflection to be enabled (it "
				     "provides the descriptor pool the reflect impls resolve against)");
	if (c->shared_descriptor_pool && !c->generate_reflection)
		return CODEGEN_ERROR("shared_descriptor_pool requires generate_reflection to be enabled (it "
				     "deduplicates the embedded reflection descriptor pool)");
	if (c->idiomatic_imports && !c->file_per_package)
		return CODEGEN_ERROR("idiomatic_imports requires file_per_package to be enabled (the multi-file "
				     "layout include!-merges every proto's content into the shared package root, "
				     "where emitted `use` directives could collide across files)");
	FeatureGates g = feature_gates_for_config(c);
	const char *kinds[] = {"json", "views", "text", "reflect"};
	const char *names[] = {g.json, g.views, g.text, g.reflect};
	for (int i = 0; i < 4; i++)
		if (names[i] && !is_valid_feature_name(names[i])) {
			Buf b = {0};
			rust_str_debug(&b, str_c(names[i]));
			return CODEGEN_ERROR("invalid %s feature-gate name %s: a Cargo feature name starts with an "
					     "ASCII alphanumeric or '_' and contains only alphanumerics, '_', '-', "
					     "'+', or '.'; an invalid name would leave the emitted #[cfg(feature = "
					     "...)] permanently false, silently compiling the gated impls away",
					     kinds[i], buf_str(&b));
		}
	const char *err = validate_type_name_prefix(c);
	if (err)
		return err;
	return bc_generate_files(c, req, files);
}
